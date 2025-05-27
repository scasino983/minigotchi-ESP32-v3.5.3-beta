#include "channel.h"
#include "wifi_sniffer.h"
#include "minigotchi.h" // For mood
#include "esp_timer.h" // For timing metrics

// Define the task handle here
TaskHandle_t channel_hopping_task_handle = NULL;
static bool task_should_exit = false;
static uint32_t successful_hops = 0;
static uint32_t failed_hops = 0;
static int64_t last_hop_time = 0;

// Configurable parameters for channel hopping
static const uint32_t MIN_HOP_INTERVAL_MS = 500;    // Minimum time between channel hops
static const uint32_t MAX_HOP_INTERVAL_MS = 2000;   // Maximum time between channel hops
static const uint32_t ADAPTIVE_HOP_INCREASE_MS = 100; // Increase interval by this much on failure
static const uint32_t RECOVERY_PAUSE_MS = 2000;     // Pause time after multiple failures
static uint32_t current_hop_interval_ms = MIN_HOP_INTERVAL_MS; // Start with minimum interval

// Helper function to start the channel hopping task
esp_err_t start_channel_hopping() {
    if (channel_hopping_task_handle == NULL) {
        // Reset state variables
        task_should_exit = false;
        successful_hops = 0;
        failed_hops = 0;
        current_hop_interval_ms = MIN_HOP_INTERVAL_MS;
        last_hop_time = esp_timer_get_time() / 1000;
        
        Serial.println(Minigotchi::getMood().getIntense() + " SNIFFER_START: Creating channel hopping task...");
        
        BaseType_t result = xTaskCreatePinnedToCore(
            channel_hopping_task,    
            "chan_hop_task",         
            4096,                    // Increased stack size for better stability
            NULL,                    
            5,                       
            &channel_hopping_task_handle, 
            0                        // Run on core 0 (Arduino loop() is on core 1)
        );
        
        if (result == pdPASS && channel_hopping_task_handle != NULL) {
            Serial.println(Minigotchi::getMood().getHappy() + " SNIFFER_START: Channel hopping task created successfully.");
            return ESP_OK;
        } else {
            Serial.println(Minigotchi::getMood().getBroken() + " SNIFFER_START: FAILED to create channel hopping task.");
            return ESP_FAIL;
        }
    }
    return ESP_OK;
}

// Helper function to stop the channel hopping task
void stop_channel_hopping() {
    if (channel_hopping_task_handle != NULL) {
        Serial.println(Minigotchi::getMood().getNeutral() + " SNIFFER_STOP: Signaling channel hopping task to exit...");
        
        // Signal the task to exit
        task_should_exit = true;
        
        // Wait for the task to exit gracefully with timeout
        TickType_t start_time = xTaskGetTickCount();
        const TickType_t max_wait_ticks = pdMS_TO_TICKS(2000); // 2 second timeout
        
        while (channel_hopping_task_handle != NULL && 
               (xTaskGetTickCount() - start_time) < max_wait_ticks) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        
        // If task still exists, force delete
        if (channel_hopping_task_handle != NULL) {
            Serial.println(Minigotchi::getMood().getBroken() + " SNIFFER_STOP: Channel hopping task did not exit in time, forcing deletion.");
            vTaskDelete(channel_hopping_task_handle);
            channel_hopping_task_handle = NULL;
        } else {
            Serial.println(Minigotchi::getMood().getHappy() + " SNIFFER_STOP: Channel hopping task exited gracefully.");
        }
        
        // Log channel hopping stats
        Serial.printf("%s Channel hopping stats - Successful: %d, Failed: %d, Last interval: %d ms\n",
                     Minigotchi::getMood().getNeutral().c_str(),
                     successful_hops, failed_hops, current_hop_interval_ms);
    }
}

// Channel hopping task - improved with adaptive timing and error tracking
void channel_hopping_task(void *pvParameter) {
    Serial.println(Minigotchi::getMood().getHappy() + " CHAN_HOP_TASK: Task started with improved channel hopping logic.");
    
    int consecutive_failures = 0;
    const int MAX_CONSECUTIVE_FAILURES = 5;
    bool channel_hop_paused = false;
    TickType_t last_recovery_time = 0;
    
    while (!task_should_exit) {
        // Check if sniffer is still running
        if (!is_sniffer_running()) {
            Serial.println(Minigotchi::getMood().getNeutral() + " CHAN_HOP_TASK: Sniffer stopped, task exiting.");
            break;
        }
        
        // Get current time
        int64_t current_time = esp_timer_get_time() / 1000; // Convert to ms
        int64_t elapsed = current_time - last_hop_time;
        
        // Only attempt channel hopping if enough time has passed
        if (elapsed >= current_hop_interval_ms) {
            // Update the last hop time
            last_hop_time = current_time;
            
            if (channel_hop_paused) {
                // We're in recovery mode - just wait
                Serial.println(Minigotchi::getMood().getNeutral() + " CHAN_HOP_TASK: Channel hopping paused for recovery. Waiting longer.");
                vTaskDelay(pdMS_TO_TICKS(RECOVERY_PAUSE_MS)); // Explicit longer pause
                channel_hop_paused = false; // Try again next time
                consecutive_failures = 0; 
                last_recovery_time = xTaskGetTickCount(); // Keep this
            } else {
                // Try to do the channel hop
                int prev_channel = Channel::getChannel();
                int prev_channel_for_debug = prev_channel; // Capture before Channel::cycle changes it
                
                // Call the Channel class to handle the actual channel switching
                Channel::cycle();
                
                // Add a delay to ensure the channel has time to switch
                delay(250); // New delay
                
                // Check if channel switch was successful
                int new_channel = Channel::getChannel();
                Serial.printf("CHAN_HOP_TASK: prev_channel_for_debug=%d, Channel::getChannel() after cycle and delay reports: %d\n", prev_channel_for_debug, new_channel);
                if (new_channel != prev_channel) {
                    // Success!
                    successful_hops++;
                    consecutive_failures = 0;
                    
                    // Gradually decrease the interval back to minimum if it was increased
                    if (current_hop_interval_ms > MIN_HOP_INTERVAL_MS) {
                        current_hop_interval_ms -= ADAPTIVE_HOP_INCREASE_MS/2; // Decrease slower than increase
                        if (current_hop_interval_ms < MIN_HOP_INTERVAL_MS) {
                            current_hop_interval_ms = MIN_HOP_INTERVAL_MS;
                        }
                    }
                } else {
                    // Failed to change channel
                    failed_hops++;
                    consecutive_failures++;
                    
                    // Increase hop interval to reduce channel switching pressure
                    current_hop_interval_ms += ADAPTIVE_HOP_INCREASE_MS;
                    if (current_hop_interval_ms > MAX_HOP_INTERVAL_MS) {
                        current_hop_interval_ms = MAX_HOP_INTERVAL_MS;
                    }
                    
                    Serial.printf("%s CHAN_HOP_TASK: Channel switch failed (%d consecutive). Increasing interval to %d ms\n",
                                 Minigotchi::getMood().getSad().c_str(),
                                 consecutive_failures, 
                                 current_hop_interval_ms);
                    
                    // If too many consecutive failures, take a break and try to restart WiFi
                    if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
                        Serial.println(Minigotchi::getMood().getBroken() + " CHAN_HOP_TASK: Too many consecutive failures. Pausing channel hopping briefly.");
                        channel_hop_paused = true;
                        // Removed WiFi reset block as per instructions
                    }
                }
            }
        }
        
        // Use a shorter delay to keep responsive to task_should_exit
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    // Clean up
    channel_hopping_task_handle = NULL;
    Serial.println(Minigotchi::getMood().getNeutral() + " CHAN_HOP_TASK: Task exiting normally.");
    vTaskDelete(NULL);
}

// Statistics accessor functions
uint32_t get_successful_channel_hops() {
    return successful_hops;
}

uint32_t get_failed_channel_hops() {
    return failed_hops;
}

uint32_t get_channel_hop_interval_ms() {
    return current_hop_interval_ms;
}

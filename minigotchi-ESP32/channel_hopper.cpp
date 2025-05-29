#include "channel.h"
#include "wifi_sniffer.h"
#include "minigotchi.h" // For mood (can be removed if getMood() is no longer used)
#include "mood.h"       // Directly include mood.h
#include "esp_timer.h" // For timing metrics
#include <WiFi.h> // Include WiFi.h for WiFi.mode() calls
#include "esp_task_wdt.h" // For watchdog timer management
#include "wifi_manager.h" // Include the WiFi Manager

// Add mutex for task management
static portMUX_TYPE channel_hopper_mutex = portMUX_INITIALIZER_UNLOCKED;

// Define the task handle here
TaskHandle_t channel_hopping_task_handle = NULL;
static bool task_should_exit = false;
static uint32_t successful_hops = 0;
static uint32_t failed_hops = 0;
static int64_t last_hop_time = 0;

// Add task state tracking
static int consecutive_failures = 0;
static bool channel_hop_paused = false;

// Configurable parameters for channel hopping
static const uint32_t MIN_HOP_INTERVAL_MS = 500;    // Minimum time between channel hops
static const uint32_t MAX_HOP_INTERVAL_MS = 2000;   // Maximum time between channel hops
static const uint32_t ADAPTIVE_HOP_INCREASE_MS = 100; // Increase interval by this much on failure
static const uint32_t RECOVERY_PAUSE_MS = 2000;     // Pause time after multiple failures
static uint32_t current_hop_interval_ms = MIN_HOP_INTERVAL_MS; // Start with minimum interval

// Helper function to start the channel hopping task
esp_err_t start_channel_hopping() {
    // First make sure any existing task is properly stopped
    if (channel_hopping_task_handle != NULL) {
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
            Serial.println(Mood::getInstance().getBroken() + " SNIFFER_START: Forcing deletion of previous task.");
            vTaskDelete(channel_hopping_task_handle);
            channel_hopping_task_handle = NULL;
        }
        
        // Add delay to ensure task is fully terminated
        delay(100);
    }

    // Request monitor mode from WifiManager
    if (!WifiManager::getInstance().request_monitor_mode("channel_hopper")) {
        Serial.println(Mood::getInstance().getBroken() + " SNIFFER_START: Failed to acquire monitor mode from WifiManager.");
        return ESP_FAIL; // Indicate failure
    }
    Serial.println(Mood::getInstance().getHappy() + " SNIFFER_START: Monitor mode acquired via WifiManager.");
    
    // Reset state variables
    task_should_exit = false;
    successful_hops = 0;
    failed_hops = 0;
    consecutive_failures = 0;
    channel_hop_paused = false;
    current_hop_interval_ms = MIN_HOP_INTERVAL_MS;
    last_hop_time = esp_timer_get_time() / 1000;
    
    Serial.println(Mood::getInstance().getIntense() + " SNIFFER_START: Creating channel hopping task...");
    
    BaseType_t result = xTaskCreatePinnedToCore(
        channel_hopping_task,    
        "chan_hop_task",         
        8192,                    // Doubled stack size for better stability
        NULL,                    
        1,                       // Lower priority to avoid interference with UI
        &channel_hopping_task_handle, 
        0                        // Run on core 0 (Arduino loop() is on core 1)
    );
    
    if (result == pdPASS && channel_hopping_task_handle != NULL) {
        Serial.println(Mood::getInstance().getHappy() + " SNIFFER_START: Channel hopping task created successfully.");
        return ESP_OK;
    } else {
        Serial.println(Mood::getInstance().getBroken() + " SNIFFER_START: FAILED to create channel hopping task.");
        return ESP_FAIL;
    }
}

// Helper function to stop the channel hopping task
void stop_channel_hopping() {
    if (channel_hopping_task_handle != NULL) {
        Serial.println(Mood::getInstance().getNeutral() + " SNIFFER_STOP: Signaling channel hopping task to exit...");
        
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
            Serial.println(Mood::getInstance().getBroken() + " SNIFFER_STOP: Channel hopping task did not exit in time, forcing deletion.");
            vTaskDelete(channel_hopping_task_handle);
            
            // Critical section to update handle
            portENTER_CRITICAL(&channel_hopper_mutex);
            channel_hopping_task_handle = NULL;
            portEXIT_CRITICAL(&channel_hopper_mutex);
        } else {
            Serial.println(Mood::getInstance().getHappy() + " SNIFFER_STOP: Channel hopping task exited gracefully.");
        }
        
        // Log channel hopping stats
        Serial.printf("%s Channel hopping stats - Successful: %d, Failed: %d, Last interval: %d ms\n",
                     Mood::getInstance().getNeutral().c_str(),
                     successful_hops, failed_hops, current_hop_interval_ms);
    }
    // Release WiFi control via WifiManager
    WifiManager::getInstance().release_wifi_control("channel_hopper");
    Serial.println(Mood::getInstance().getNeutral() + " SNIFFER_STOP: Released WiFi control via WifiManager.");
}

// Task runner function
void channel_hopping_task(void *pvParameters) {
    // Register with watchdog timer to avoid resets - using a safer approach
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    if (wdt_err == ESP_OK) {
        Serial.println(Mood::getInstance().getNeutral() + " CHAN_HOP_TASK: Registered with watchdog timer.");
    } else {
        Serial.printf("%s CHAN_HOP_TASK: Failed to register with watchdog timer: %s\n",
                     Mood::getInstance().getBroken().c_str(), 
                     esp_err_to_name(wdt_err));
    }
    
    Serial.println(Mood::getInstance().getHappy() + " CHAN_HOP_TASK: Task started with improved channel hopping logic.");
    
    // Print heap usage at task start
    Serial.print("[CHAN_HOP_TASK] Free heap at start: ");
    Serial.println(ESP.getFreeHeap());
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    Serial.print("[CHAN_HOP_TASK] Stack high water mark at start: ");
    Serial.println(stackHighWaterMark);
    
    const int MAX_CONSECUTIVE_FAILURES = 5;
    TickType_t last_recovery_time = 0;
    
    while (!task_should_exit) {
        // Pet the watchdog to prevent resets - safely
        if (wdt_err == ESP_OK) {
            esp_task_wdt_reset();
        }
        
        // Check if sniffer is still running
        if (!is_sniffer_running()) {
            Serial.println(Mood::getInstance().getNeutral() + " CHAN_HOP_TASK: Sniffer stopped, task exiting.");
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
                Serial.println(Mood::getInstance().getNeutral() + " CHAN_HOP_TASK: Channel hopping paused for recovery");
                channel_hop_paused = false; // Try again next time
                
                // Store recovery time for monitoring
                last_recovery_time = xTaskGetTickCount();
            } else {
                // Try to do the channel hop
                int prev_channel = Channel::getChannel();
                
                // Call the Channel class to handle the actual channel switching
                Channel::cycle();
                
                // Yield to the watchdog after each major operation
                if (wdt_err == ESP_OK) {
                    esp_task_wdt_reset();
                }
                
                // Add a short delay to ensure the channel has time to switch
                // but yield to watchdog by using vTaskDelay instead of delay
                vTaskDelay(pdMS_TO_TICKS(50));
                
                // Check if channel switch was successful
                int new_channel = Channel::getChannel();
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
                                 Mood::getInstance().getSad().c_str(),
                                 consecutive_failures, 
                                 current_hop_interval_ms);
                      
                    // If too many consecutive failures, trigger recovery in smaller steps
                    if (consecutive_failures >= MAX_CONSECUTIVE_FAILURES) {
                        Serial.println(Mood::getInstance().getBroken() + " CHAN_HOP_TASK: Too many consecutive failures. Requesting WiFi reset via WifiManager.");
                        channel_hop_paused = true; // Keep this to pause hopping attempts during reset

                        if (WifiManager::getInstance().perform_wifi_reset("channel_hopper_recovery")) {
                            Serial.println(Mood::getInstance().getHappy() + " CHAN_HOP_TASK: WiFi reset successful via WifiManager.");
                            // WifiManager::perform_wifi_reset leaves WiFi OFF. We need monitor mode.
                            if (wdt_err == ESP_OK) esp_task_wdt_reset(); // Pet watchdog before next blocking call
                            vTaskDelay(pdMS_TO_TICKS(50)); // Brief pause

                            if (WifiManager::getInstance().request_monitor_mode("channel_hopper_recovery")) {
                                Serial.println(Mood::getInstance().getHappy() + " CHAN_HOP_TASK: Monitor mode re-acquired after reset.");
                            } else {
                                Serial.println(Mood::getInstance().getBroken() + " CHAN_HOP_TASK: FAILED to re-acquire monitor mode after reset. Task may not function.");
                                task_should_exit = true; // Exit the task if monitor mode cannot be re-established.
                            }
                        } else {
                            Serial.println(Mood::getInstance().getBroken() + " CHAN_HOP_TASK: WiFi reset FAILED via WifiManager. Task may not function.");
                            task_should_exit = true; // Exit the task if reset fails.
                        }
                        if (wdt_err == ESP_OK) esp_task_wdt_reset(); // Pet watchdog after WifiManager operations
                        vTaskDelay(pdMS_TO_TICKS(50)); // Brief pause
                        
                        // Reset consecutive failures counter
                        consecutive_failures = 0;
                        // channel_hop_paused will be reset at the start of the next valid hop attempt cycle.
                    }
                }
            }
        }
        
        // Print heap and stack usage after each hop attempt
        Serial.print("[CHAN_HOP_TASK] Free heap after hop/check: ");
        Serial.println(ESP.getFreeHeap());
        stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
        Serial.print("[CHAN_HOP_TASK] Stack high water mark: ");
        Serial.println(stackHighWaterMark);
        
        // Use a shorter delay to keep responsive to task_should_exit
        // This also yields to the watchdog
        vTaskDelay(pdMS_TO_TICKS(50));
    }// Clean up with critical section to prevent race condition
    portENTER_CRITICAL(&channel_hopper_mutex);
    channel_hopping_task_handle = NULL;
    portEXIT_CRITICAL(&channel_hopper_mutex);
    
    Serial.println(Mood::getInstance().getNeutral() + " CHAN_HOP_TASK: Task exiting normally.");
    // Print final heap and stack usage
    Serial.print("[CHAN_HOP_TASK] Free heap at task end: ");
    Serial.println(ESP.getFreeHeap());
    stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    Serial.print("[CHAN_HOP_TASK] Stack high water mark at end: ");
    Serial.println(stackHighWaterMark);
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

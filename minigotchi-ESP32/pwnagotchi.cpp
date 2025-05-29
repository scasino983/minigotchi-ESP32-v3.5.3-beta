/*
 * Minigotchi: An even smaller Pwnagotchi
 * Copyright (C) 2024 dj1ch
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/**
 * pwnagotchi.cpp: sniffs for pwnagotchi beacon frames
 * source: https://github.com/justcallmekoko/ESP32Marauder
 */

#include "pwnagotchi.h"
#include "wifi_manager.h" // Include the WiFi Manager
#include "config.h"       // For Config::shortDelay, Config::longDelay
#include "parasite.h"     // For Parasite::sendPwnagotchiStatus
#include "mood.h"         // Ensured
#include "display.h"      // Ensured
#include "task_manager.h"
// #include <esp_task_wdt.h> // Commented out as these functions aren't available in this build

// Static member definitions
TaskHandle_t Pwnagotchi::pwnagotchi_scan_task_handle = NULL;
bool Pwnagotchi::pwnagotchiDetected = false; 
std::string Pwnagotchi::essid = ""; 

static portMUX_TYPE pwnagotchi_mutex = portMUX_INITIALIZER_UNLOCKED;
static volatile bool pwnagotchi_should_stop_scan = false;

// Forward declaration for the task runner
void pwnagotchi_scan_task_runner(void *pvParameters);


/** developer note:
 *
 * essentially the pwnagotchi sends out a frame(with JSON) while associated to a
 * network if the minigotchi listens for a while it should find something this
 * is under the assumption that we put the minigotchi on the same channel as the
 * pwnagotchi or one of the channels that the pwnagotchi listens on the JSON
 * frame it sends out should have some magic id attached to it (numbers 222-226)
 * so it is identified by pwngrid however we don't need to search for such
 * things
 *
 */

// Mood &Pwnagotchi::mood = Mood::getInstance(); // Already REMOVED in prior attempt, ensuring it's gone.

/**
 * Get's the mac based on source address
 * @param addr Address to use
 * @param buff Buffer to use
 * @param offset Data offset
 */
void Pwnagotchi::getMAC(char *addr, const unsigned char *buff, int offset) {
  snprintf(addr, 18, "%02x:%02x:%02x:%02x:%02x:%02x", buff[offset],
           buff[offset + 1], buff[offset + 2], buff[offset + 3],
           buff[offset + 4], buff[offset + 5]);
}

/**
 * Extract Mac Address using getMac()
 * @param buff Buffer to use
 */
std::string Pwnagotchi::extractMAC(const unsigned char *buff) {
  char addr[] = "00:00:00:00:00:00";
  getMAC(addr, buff, 10);
  return std::string(addr);
}


/**
 * Detect a Pwnagotchi
 */
void Pwnagotchi::detect() {
    if (!Config::scan) {
        Serial.println(Mood::getInstance().getNeutral() + " Pwnagotchi::detect - Scan disabled in config.");
        return;
    }
    portENTER_CRITICAL(&pwnagotchi_mutex);
    if (Pwnagotchi::pwnagotchi_scan_task_handle != NULL) {
        portEXIT_CRITICAL(&pwnagotchi_mutex);
        Serial.println(Mood::getInstance().getNeutral() + " Pwnagotchi scan is already in progress.");
        Display::updateDisplay(Mood::getInstance().getNeutral(), "Pwn scan active");
        return;
    }
    portEXIT_CRITICAL(&pwnagotchi_mutex);
    pwnagotchi_should_stop_scan = false;

    // Use TaskManager to create the scan task with HIGHER PRIORITY (2 instead of 1)
    // WiFi operations need higher priority to prevent watchdog timeouts
    bool created = TaskManager::getInstance().createTask(
        "pwn_scan_task",
        pwnagotchi_scan_task_runner,
        8192, 2, nullptr, 0 // Increased priority from 1 to 2, keep 8192 stack size
    );
    
    if (!created) {
        Serial.println(Mood::getInstance().getBroken() + " FAILED to create Pwnagotchi scan task.");
        portENTER_CRITICAL(&pwnagotchi_mutex);
        Pwnagotchi::pwnagotchi_scan_task_handle = NULL;
        portEXIT_CRITICAL(&pwnagotchi_mutex);
    } else {
        Serial.println(Mood::getInstance().getNeutral() + " Created Pwnagotchi scan task successfully.");
        // Add a yield to give the task time to start
        yield();
    }
}


/**
 * Stops Pwnagotchi scan
 */
void Pwnagotchi::stop_scan() { // Renamed from stopCallback
    Serial.println(Mood::getInstance().getNeutral() + " Pwnagotchi::stop_scan - Received stop request.");
    portENTER_CRITICAL(&pwnagotchi_mutex);
    if (Pwnagotchi::pwnagotchi_scan_task_handle == NULL) {
        portEXIT_CRITICAL(&pwnagotchi_mutex);
        Serial.println(Mood::getInstance().getNeutral() + " Pwnagotchi::stop_scan - No scan task seems to be running.");
        pwnagotchi_should_stop_scan = false; // Reset if no task, though task also resets on start
        return;
    }
    portEXIT_CRITICAL(&pwnagotchi_mutex);
    pwnagotchi_should_stop_scan = true;
    // The task will see this flag and terminate itself.
    // Promiscuous mode and WiFi control release are handled by the task itself.
}

bool Pwnagotchi::is_scanning() {
    portENTER_CRITICAL(&pwnagotchi_mutex);
    bool running = (Pwnagotchi::pwnagotchi_scan_task_handle != NULL);
    portEXIT_CRITICAL(&pwnagotchi_mutex);
    return running;
}


// Task runner function
void pwnagotchi_scan_task_runner(void *pvParameters) {
    Pwnagotchi::pwnagotchiDetected = false; // Reset detection flag for this scan session

    Serial.println(Mood::getInstance().getNeutral() + " Pwnagotchi scan task started.");

    // Print heap usage at start
    Serial.print("[DEBUG] Free heap at scan start: ");
    Serial.println(ESP.getFreeHeap());

    // Add a yield to give the main loop time to process (prevent watchdog)
    yield();    // Store the task handle for task management
    portENTER_CRITICAL(&pwnagotchi_mutex);
    Pwnagotchi::pwnagotchi_scan_task_handle = xTaskGetCurrentTaskHandle();
    portEXIT_CRITICAL(&pwnagotchi_mutex);
    
    // ===== Critical Section: Acquiring monitor mode (potential deadlock source) =====
    // Use a watchdog approach to monitor the entire WiFi acquisition process
    // This allows us to recover if something gets stuck
    unsigned long overallStartTime = millis();
    unsigned long maxTotalTime = 15000; // 15 seconds maximum for the entire initialization
    
    Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Starting monitor mode acquisition with timeout protection");
    
    // ===== Monitor Mode Acquisition with stronger safety mechanisms =====
    unsigned long startTime = millis();
    bool monitorSuccess = false;
    bool resetAttempted = false;
      // Try for up to 6 seconds to get monitor mode before trying a reset
    Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Starting monitor mode acquisition sequence");
    
    // First attempt: Try normal acquisition with timeout
    while (millis() - startTime < 6000 && !monitorSuccess && (millis() - overallStartTime < maxTotalTime)) {
        Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Attempting to acquire monitor mode...");
        
        // Check if we should exit before making request
        if (pwnagotchi_should_stop_scan || taskShouldExit("pwn_scan_task")) {
            Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Stop requested during monitor mode acquisition");
            goto cleanup_and_exit;
        }
        
        // Try to acquire monitor mode with time-bounded attempt
        unsigned long requestStartTime = millis();
        bool requestSuccess = WifiManager::getInstance().request_monitor_mode("pwnagotchi_scan_task");
        unsigned long requestTime = millis() - requestStartTime;
        
        // Log the time it took to process the request
        Serial.printf("%s PWN_SCAN_TASK: Monitor mode request processed in %lu ms, result: %s\n",
                     Mood::getInstance().getNeutral().c_str(),
                     requestTime,
                     requestSuccess ? "SUCCESS" : "FAILED");
        
        // Check for success
        if (requestSuccess) {
            monitorSuccess = true;
            Serial.println(Mood::getInstance().getHappy() + " PWN_SCAN_TASK: Monitor mode acquired on first attempt!");
            break;
        }
        
        // Check for overall timeout (safety measure)
        if (millis() - overallStartTime >= maxTotalTime) {
            Serial.println(Mood::getInstance().getBroken() + " PWN_SCAN_TASK: Overall timeout reached during monitor mode acquisition");
            goto cleanup_and_exit;
        }
        
        // Yield to prevent watchdog timeouts and allow other tasks to run
        yield();
        delay(200); // Longer delay between retries to avoid overwhelming WiFi system
    }
      // If first attempt failed, try WiFi reset and retry once more
    if (!monitorSuccess && !pwnagotchi_should_stop_scan && !taskShouldExit("pwn_scan_task") && (millis() - overallStartTime < maxTotalTime)) {
        resetAttempted = true;
        Serial.println(Mood::getInstance().getIntense() + " PWN_SCAN_TASK: First monitor mode attempt timed out, resetting WiFi...");
        
        // Check if we should exit before reset
        if (pwnagotchi_should_stop_scan || taskShouldExit("pwn_scan_task")) {
            Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Stop requested before WiFi reset");
            goto cleanup_and_exit;
        }
        
        // Reset the WiFi system completely
        Display::updateDisplay(Mood::getInstance().getIntense(), "Resetting WiFi...");
        
        // Log start time for the reset operation
        unsigned long resetStartTime = millis();
        bool resetSuccess = WifiManager::getInstance().perform_wifi_reset("pwnagotchi_scan_recovery");
        unsigned long resetTime = millis() - resetStartTime;
        
        // Log the time it took to reset
        Serial.printf("%s PWN_SCAN_TASK: WiFi reset completed in %lu ms, result: %s\n",
                     Mood::getInstance().getNeutral().c_str(),
                     resetTime,
                     resetSuccess ? "SUCCESS" : "FAILED");
        
        if (!resetSuccess) {
            Serial.println(Mood::getInstance().getBroken() + " PWN_SCAN_TASK: WiFi reset failed!");
            Display::updateDisplay(Mood::getInstance().getBroken(), "WiFi reset failed!");
            delay(500);
            goto cleanup_and_exit;
        }
        
        // Check for overall timeout (safety measure)
        if (millis() - overallStartTime >= maxTotalTime) {
            Serial.println(Mood::getInstance().getBroken() + " PWN_SCAN_TASK: Overall timeout reached after WiFi reset");
            goto cleanup_and_exit;
        }
        
        delay(300); // Give the system time to stabilize after reset
        yield();
        
        // Second attempt after reset with shorter timeout
        Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Trying monitor mode again after reset...");
        Display::updateDisplay(Mood::getInstance().getNeutral(), "Trying monitor again...");
        
        // Reset the timer for the second attempt
        startTime = millis();
        
        // Try one more time with a shorter timeout
        while (millis() - startTime < 4000 && !monitorSuccess) {
            // Check if we should exit
            if (pwnagotchi_should_stop_scan || taskShouldExit("pwn_scan_task")) {
                Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Stop requested during second monitor attempt");
                goto cleanup_and_exit;
            }
            
            if (WifiManager::getInstance().request_monitor_mode("pwnagotchi_scan_recovery")) {
                monitorSuccess = true;
                Serial.println(Mood::getInstance().getHappy() + " PWN_SCAN_TASK: Monitor mode acquired after reset!");
                break;
            }
            
            yield();
            delay(250);
        }
    }
    
    // Final check if monitor mode was successfully acquired
    if (!monitorSuccess) {
        Serial.println(Mood::getInstance().getBroken() + " PWN_SCAN_TASK: Failed to acquire monitor mode after all attempts");
        Display::updateDisplay(Mood::getInstance().getBroken(), "Monitor mode failed");
        delay(1000); // Show error message before cleanup
        goto cleanup_and_exit;
    }
    
    // If we got here, monitor mode was successfully acquired
    Serial.println(Mood::getInstance().getHappy() + " PWN_SCAN_TASK: Monitor mode acquisition sequence completed successfully");
    Display::updateDisplay(Mood::getInstance().getHappy(), "Monitor mode ready");
    delay(200);
    
    // Continue with the rest of the scan task...
    goto continue_scan;
    
cleanup_and_exit:
    // Clean up and exit if we failed to get monitor mode
    portENTER_CRITICAL(&pwnagotchi_mutex);
    Pwnagotchi::pwnagotchi_scan_task_handle = NULL;
    portEXIT_CRITICAL(&pwnagotchi_mutex);
    pwnagotchi_should_stop_scan = false; // Reset flag
    
    // Print diagnostics
    Serial.print("[DEBUG] Free heap at task exit (failed monitor): ");
    Serial.println(ESP.getFreeHeap());
    
    vTaskDelete(NULL);
    return;

continue_scan:
    // Successfully acquired monitor mode, continue with scan
    Serial.println(Mood::getInstance().getNeutral() + " Pwnagotchi Task: Monitor mode acquired.");
    
    // Explicit channel set to ensure we're on a good channel
    uint8_t scan_channel = 1; // Start on channel 1 (common channel for beacon frames)
    esp_err_t channel_err = esp_wifi_set_channel(scan_channel, WIFI_SECOND_CHAN_NONE);
    if (channel_err != ESP_OK) {
        Serial.printf("%s PWN_SCAN_TASK: Failed to set channel %d: %s\n", 
                     Mood::getInstance().getBroken().c_str(),
                     scan_channel,
                     esp_err_to_name(channel_err));
    } else {
        Serial.printf("%s PWN_SCAN_TASK: Set to channel %d for scanning\n", 
                     Mood::getInstance().getNeutral().c_str(),
                     scan_channel);
    }
      // Make sure we're starting with a clean callback state
    esp_wifi_set_promiscuous_rx_cb(NULL);
    yield();
    
    // Register callback AFTER we've set up WiFi correctly
    Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Registering packet callback");
    esp_wifi_set_promiscuous_rx_cb(Pwnagotchi::pwnagotchiCallback);
    
    // Verify promiscuous mode is still enabled (sometimes it can get disabled during setup)
    bool is_promiscuous = false;
    esp_err_t get_prom_err = esp_wifi_get_promiscuous(&is_promiscuous);
    if (get_prom_err != ESP_OK || !is_promiscuous) {
        Serial.printf("%s PWN_SCAN_TASK: Promiscuous mode verification failed. Enabled: %d, error: %s\n", 
                     Mood::getInstance().getBroken().c_str(), 
                     is_promiscuous,
                     esp_err_to_name(get_prom_err));
        // Try to re-enable it
        esp_err_t prom_err = esp_wifi_set_promiscuous(true);
        if (prom_err != ESP_OK) {
            Serial.printf("%s PWN_SCAN_TASK: Failed to re-enable promiscuous mode: %s\n", 
                         Mood::getInstance().getBroken().c_str(),
                         esp_err_to_name(prom_err));
        } else {
            Serial.println(Mood::getInstance().getHappy() + " PWN_SCAN_TASK: Successfully re-enabled promiscuous mode");
        }
    } else {
        Serial.println(Mood::getInstance().getHappy() + " PWN_SCAN_TASK: Promiscuous mode confirmed active");
    }
    
    // Yield to prevent watchdog timeouts
    yield();
    
    // Animation part
    for (int i = 0; i < 5 && !pwnagotchi_should_stop_scan; ++i) {
        if (taskShouldExit("pwn_scan_task") || pwnagotchi_should_stop_scan) break;
        Serial.println(Mood::getInstance().getLooking1() + " Scanning for Pwnagotchi.");
        Display::updateDisplay(Mood::getInstance().getLooking1(), "Scanning for Pwnagotchi.");
        vTaskDelay(pdMS_TO_TICKS(Config::shortDelay)); 
        yield(); // Reset watchdog
        if (taskShouldExit("pwn_scan_task") || pwnagotchi_should_stop_scan) break;
        Serial.println(Mood::getInstance().getLooking2() + " Scanning for Pwnagotchi..");
        Display::updateDisplay(Mood::getInstance().getLooking2(), "Scanning for Pwnagotchi..");
        vTaskDelay(pdMS_TO_TICKS(Config::shortDelay));
        yield(); // Reset watchdog
        if (taskShouldExit("pwn_scan_task") || pwnagotchi_should_stop_scan) break;
        
        Serial.println(Mood::getInstance().getLooking1() + " Scanning for Pwnagotchi...");
        Display::updateDisplay(Mood::getInstance().getLooking1(), "Scanning for Pwnagotchi...");
        vTaskDelay(pdMS_TO_TICKS(Config::shortDelay));
        yield(); // Reset watchdog
        if (taskShouldExit("pwn_scan_task") || pwnagotchi_should_stop_scan) break;        Serial.println(" ");
        vTaskDelay(pdMS_TO_TICKS(Config::shortDelay));
    }      // A minimal yield instead of a delay - just enough to let the system process events
    // We're removing the longer delay here as it might not be necessary
    Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Proceeding with channel scanning...");
    Display::updateDisplay(Mood::getInstance().getNeutral(), "Starting scan...");
    yield();
    
    // Set up channel hopping for the scan
    Serial.println(Mood::getInstance().getIntense() + " PWN_SCAN_TASK: Setting up channel hopping for scan");
      // Try each channel in sequence, spending more time on channels 1, 6, and 11
    int priorityChannels[] = {1, 6, 11}; // Non-overlapping channels
    int channelDwell = 800; // ms to spend on each channel (reduced from 1000ms)
    int priorityDwell = 1500; // ms to spend on priority channels (reduced from 2000ms)
    
    // Manually hop through channels during our scan (without using the channel hopper task)
    for (int channel = 1; channel <= 13 && !pwnagotchi_should_stop_scan && !taskShouldExit("pwn_scan_task"); channel++) {
        // Yield to prevent watchdog timeouts
        yield();
        
        // Set the channel
        esp_err_t ch_err = esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
        if (ch_err != ESP_OK) {
            Serial.printf("%s PWN_SCAN_TASK: Failed to set channel %d: %s\n", 
                         Mood::getInstance().getBroken().c_str(),
                         channel,
                         esp_err_to_name(ch_err));
            continue; // Skip to next channel
        }
        
        // Check if this is a priority channel
        bool isPriority = false;
        for (int i = 0; i < 3; i++) {
            if (priorityChannels[i] == channel) {
                isPriority = true;
                break;
            }
        }
        
        // Update display with current scan channel
        Serial.printf("%s Scanning on channel %d...\n", 
                     Mood::getInstance().getLooking1().c_str(), 
                     channel);
        Display::updateDisplay(Mood::getInstance().getLooking1(), 
                             "Scanning CH " + String(channel));
        
        // Dwell on this channel for the appropriate time
        unsigned long startDwell = millis();
        unsigned long dwellTime = isPriority ? priorityDwell : channelDwell;
          while (millis() - startDwell < dwellTime && !pwnagotchi_should_stop_scan && !taskShouldExit("pwn_scan_task")) {
            // Check every 50ms if we should exit (reduced from 100ms)
            vTaskDelay(pdMS_TO_TICKS(50));
            yield(); // Keep watchdog happy
            
            // If pwnagotchi was detected during this dwell time, we can exit early
            if (Pwnagotchi::pwnagotchiDetected) {
                Serial.println(Mood::getInstance().getHappy() + " PWN_SCAN_TASK: Pwnagotchi detected! Stopping channel hopping.");
                break;
            }
        }
        
        // If pwnagotchi was detected or stop was requested, exit the loop
        if (Pwnagotchi::pwnagotchiDetected || pwnagotchi_should_stop_scan || taskShouldExit("pwn_scan_task")) {
            break;
        }
    }    // Always clean up WiFi resources before exiting
    Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Cleaning up WiFi resources");
      // First, unregister the callback to prevent it from being called during cleanup
    Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Removing packet callback");
    esp_wifi_set_promiscuous_rx_cb(NULL);
    yield();
    
    // Next, explicitly disable promiscuous mode with retries
    bool prom_disabled = false;
    for (int i = 0; i < 3 && !prom_disabled; i++) {
        Serial.printf("%s PWN_SCAN_TASK: Disabling promiscuous mode (attempt %d)\n", 
                     Mood::getInstance().getNeutral().c_str(), i+1);
        esp_err_t err = esp_wifi_set_promiscuous(false);
        if (err == ESP_OK) {
            prom_disabled = true;
        } else {
            Serial.printf("%s PWN_SCAN_TASK: Failed to disable promiscuous mode: %s\n", 
                         Mood::getInstance().getBroken().c_str(),
                         esp_err_to_name(err));
            yield();
        }
    }
    
    // Finally, properly release WiFi control
    Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Releasing WiFi control");
    WifiManager::getInstance().release_wifi_control("pwnagotchi_scan_task");
    
    // If we failed to release with the task tag, try with the recovery tag
    if (!prom_disabled) {
        Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: Trying release with recovery tag");
        WifiManager::getInstance().release_wifi_control("pwnagotchi_scan_recovery");
    }
    
    Serial.println(Mood::getInstance().getNeutral() + " PWN_SCAN_TASK: WiFi resources cleaned up");
    Serial.print("[DEBUG] Free heap after scan and WiFi release: ");
    Serial.println(ESP.getFreeHeap());

    // Report findings
    if (!Pwnagotchi::pwnagotchiDetected && !taskShouldExit("pwn_scan_task") && !pwnagotchi_should_stop_scan) { // Only report "not found" if scan completed fully
        Serial.println(Mood::getInstance().getSad() + " No Pwnagotchi found during scan task.");
        Display::updateDisplay(Mood::getInstance().getSad(), "No Pwnagotchi found.");
        Parasite::sendPwnagotchiStatus(NO_FRIEND_FOUND);
    } else if (Pwnagotchi::pwnagotchiDetected) {
        Serial.println(Mood::getInstance().getHappy() + " Pwnagotchi detection process complete (details in callback).");
    }
    
    // Task cleanup
    portENTER_CRITICAL(&pwnagotchi_mutex);
    Pwnagotchi::pwnagotchi_scan_task_handle = NULL;
    portEXIT_CRITICAL(&pwnagotchi_mutex);
    pwnagotchi_should_stop_scan = false; // Reset flag for next run

    Serial.println(Mood::getInstance().getNeutral() + " Pwnagotchi scan task finished.");
    Serial.print("[DEBUG] Free heap at task end: ");
    Serial.println(ESP.getFreeHeap());
    
    // --- EXTREME DIAGNOSTICS AND SAFETY ---
    Serial.println("[EXTREME] Pwnagotchi scan task about to finish. Checking all diagnostics...");
    // Stack high water mark diagnostic
    UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
    Serial.print("[EXTREME] Stack high water mark at task end: ");
    Serial.println(stackHighWaterMark);
    
    // Heap integrity check (ESP-IDF)
    bool heap_ok = heap_caps_check_integrity_all(true);
    Serial.print("[EXTREME] Heap integrity at task end: ");
    Serial.println(heap_ok ? "OK" : "CORRUPT");
    
    // Print all FreeRTOS task states
    Serial.println("[EXTREME] Listing all FreeRTOS tasks:");
    char *taskListBuf = (char*)malloc(1024);
    if (taskListBuf) {
        vTaskList(taskListBuf);
        Serial.println(taskListBuf);
        free(taskListBuf);
    } else {
        Serial.println("[EXTREME] Failed to allocate buffer for vTaskList.");
    }
    
    // Print heap summary
    multi_heap_info_t heapInfo;
    heap_caps_get_info(&heapInfo, MALLOC_CAP_DEFAULT);
    Serial.printf("[EXTREME] Heap total: %u, free: %u, largest free block: %u, min free ever: %u\n", 
                 heapInfo.total_allocated_bytes, heapInfo.total_free_bytes, 
                 heapInfo.largest_free_block, heapInfo.minimum_free_bytes);
    
    // Print WiFi state
    wifi_mode_t mode;
    esp_err_t mode_err = esp_wifi_get_mode(&mode);
    if (mode_err == ESP_OK) {
        Serial.printf("[EXTREME] WiFi mode at scan task end: %d\n", mode);
    } else {
        Serial.printf("[EXTREME] Failed to get WiFi mode: %s\n", esp_err_to_name(mode_err));
    }
    
    // Final WiFi safety check - make sure promiscuous mode is disabled
    bool is_promiscuous = false;
    esp_wifi_get_promiscuous(&is_promiscuous);
    if (is_promiscuous) {
        Serial.println("[EXTREME] WARNING: Promiscuous mode still enabled at task exit. Disabling...");
        esp_wifi_set_promiscuous(false);
    }
      Serial.println("[EXTREME] About to vTaskDelete(NULL) in scan task");
    vTaskDelete(NULL); // Deletes the current task
}



/**
 * Pwnagotchi Scanning callback
 * Source:
 * https://github.com/justcallmekoko/ESP32Marauder/blob/master/esp32_marauder/WiFiScan.cpp#L2439
 * @param buf Packet recieved to use as a buffer
 * @param len Length of the buffer
 */
void Pwnagotchi::pwnagotchiCallback(void *buf,
                                    wifi_promiscuous_pkt_type_t type) {
  // Validate buffer
  if (buf == NULL) {
    Serial.println(Mood::getInstance().getBroken() + " PWN_CALLBACK: Received NULL buffer!");
    return;
  }

  // Cast packet data
  wifi_promiscuous_pkt_t *snifferPacket = (wifi_promiscuous_pkt_t *)buf;
  WifiMgmtHdr *frameControl = (WifiMgmtHdr *)snifferPacket->payload;
  wifi_pkt_rx_ctrl_t ctrl = (wifi_pkt_rx_ctrl_t)snifferPacket->rx_ctrl;
  int len = snifferPacket->rx_ctrl.sig_len;

  // Check for basic packet validity
  if (len <= 0 || len > 1500) {
    return; // Invalid packet length
  }

  // Prevent reprocessing if we already found one
  if (Pwnagotchi::pwnagotchiDetected) {
    return; // Already found a pwnagotchi, no need to process more packets
  }

  // Log callback activity for debug (only first execution and every 100th packet)
  static int packet_counter = 0;
  if (packet_counter == 0 || packet_counter % 100 == 0) {
    Serial.printf("[PWN_CALLBACK] Processing packet #%d, type: %d, length: %d\n", 
                 packet_counter, type, len);
  }
  packet_counter++;
  // Process only management frames (type 0, beacon frames are subtype 8)
  if (type == WIFI_PKT_MGMT) {
    len -= 4; // FCS
    
    // Check if it is a beacon frame (type 0, subtype 8 = 0x80)
    uint8_t frameType = snifferPacket->payload[0];
    if (frameType != 0x80) {
      return; // Early exit for non-beacon frames to improve efficiency
    }
    
    // extract mac
    char addr[] = "00:00:00:00:00:00";
    getMAC(addr, snifferPacket->payload, 10);
    String src = addr;
      
      // Debug output for any beacon frame (helpful to diagnose issues)
      if (packet_counter % 50 == 0) {
        Serial.printf("[PWN_CALLBACK] Beacon from: %s, RSSI: %d\n", 
                     src.c_str(), ctrl.rssi);
      }

      // Check for target MAC or pwnagotchi patterns
      bool strict_mac_match = (src == "de:ad:be:ef:de:ad");
      bool possible_pwnagotchi = false;
      
      // Look for JSON patterns in ESSID (only if not a strict match)
      if (!strict_mac_match && len > 38) {
        // Safely extract ESSID with bounds checking
        String essid = "";
        int essid_start = 38; // Standard position where ESSID starts in beacon
        int essid_max_len = min(len - essid_start, 32); // Max ESSID length is 32
        
        // Only extract ESSID if we have valid data
        if (essid_max_len > 0) {
          for (int i = 0; i < essid_max_len; i++) {
            char c = snifferPacket->payload[essid_start + i];
            if (isAscii(c)) {
              essid.concat(c);
            }
          }
          
          // Check for JSON indicators
          if (essid.indexOf("{") >= 0 && essid.indexOf("}") >= 0) {
            if (essid.indexOf("name") >= 0 || essid.indexOf("pwnd") >= 0) {
              possible_pwnagotchi = true;
              Serial.println("[PWN_CALLBACK] Possible pwnagotchi beacon detected by ESSID pattern");
            }
          }
        }
      }

      // Proceed if we found a potential pwnagotchi
      if (strict_mac_match || possible_pwnagotchi) {
        Pwnagotchi::pwnagotchiDetected = true;
        Serial.println(Mood::getInstance().getHappy() + " Pwnagotchi detected!");
        Display::updateDisplay(Mood::getInstance().getHappy(), "Pwnagotchi detected!");
        
        // Extract the ESSID from the beacon frame (with bounds checking)
        String essid = "";
        int essid_start = 38;
        int essid_len = min(len - essid_start, 100); // Limit to 100 bytes for safety
        
        if (essid_len > 0) {
          for (int i = 0; i < essid_len; i++) {
            if (isAscii(snifferPacket->payload[essid_start + i])) {
              essid.concat((char)snifferPacket->payload[essid_start + i]);
            } else {
              essid.concat("?");
            }
          }
        }

        // Check if ESSID looks like it might contain JSON
        if (essid.indexOf("{") < 0 || essid.indexOf("}") < 0) {
          Serial.println(Mood::getInstance().getSad() + " ESSID doesn't appear to contain valid JSON");
          Display::updateDisplay(Mood::getInstance().getSad(), "No JSON in beacon");
          delay(1000);
          return;
        }

        // Print heap before JSON parse
        Serial.print("[PWNAGOTCHI] Heap before JSON parse: ");
        Serial.println(ESP.getFreeHeap());

        // Use a scoped block for JSON processing to ensure memory cleanup
        {
          // Parse the ESSID as JSON
          DynamicJsonDocument* jsonBuffer = new DynamicJsonDocument(2048);
          if (jsonBuffer == NULL) {
            Serial.println(Mood::getInstance().getBroken() + " Failed to allocate JSON buffer");
            Display::updateDisplay(Mood::getInstance().getBroken(), "JSON memory error");
            return;
          }
          
          DeserializationError error = deserializeJson(*jsonBuffer, essid);

          // Print heap after JSON parse
          Serial.print("[PWNAGOTCHI] Heap after JSON parse: ");
          Serial.println(ESP.getFreeHeap());

          // Check if JSON parsing is successful
          if (error) {
            Serial.println(Mood::getInstance().getBroken() + " Could not parse Pwnagotchi JSON: " + error.c_str());
            Display::updateDisplay(Mood::getInstance().getBroken(), "JSON parse error: " + (String)error.c_str());
            delete jsonBuffer; // Free memory
            return;
          }

          Serial.println(Mood::getInstance().getHappy() + " Successfully parsed JSON!");
          Display::updateDisplay(Mood::getInstance().getHappy(), "Successfully parsed JSON!");
          
          // Extract data from JSON with null checks
          bool pal = jsonBuffer->containsKey("pal") ? (*jsonBuffer)["pal"].as<bool>() : false;
          bool minigotchi = jsonBuffer->containsKey("minigotchi") ? (*jsonBuffer)["minigotchi"].as<bool>() : false;
          
          String name = jsonBuffer->containsKey("name") ? (*jsonBuffer)["name"].as<String>() : "N/A";
          String pwndTot = jsonBuffer->containsKey("pwnd_tot") ? (*jsonBuffer)["pwnd_tot"].as<String>() : "N/A";

          // Determine device type
          String deviceType = "";
          if (minigotchi) {
            deviceType = "Minigotchi";
          } else if (pal) {
            deviceType = "Palnagotchi";
          } else {
            deviceType = "Pwnagotchi";
          }

          // Display information
          Serial.println(Mood::getInstance().getHappy() + " " + deviceType + " name: " + name);
          Serial.println(Mood::getInstance().getHappy() + " Pwned Networks: " + pwndTot);
          
          Display::updateDisplay(Mood::getInstance().getHappy(), deviceType + " name: " + name);
          delay(Config::shortDelay);
          Display::updateDisplay(Mood::getInstance().getHappy(), "Pwned Networks: " + pwndTot);
          
          // Send status via Parasite
          Parasite::sendPwnagotchiStatus(FRIEND_FOUND, name.c_str());
          
          // Clear and delete JSON buffer
          jsonBuffer->clear();
          delete jsonBuffer;
          
          // Print heap after JSON cleanup
          Serial.print("[PWNAGOTCHI] Heap after JSON cleanup: ");
          Serial.println(ESP.getFreeHeap());
        }    }
  }
}

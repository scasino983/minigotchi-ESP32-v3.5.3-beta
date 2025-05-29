#include "wifi_manager.h"
#include "minigotchi.h" // For Minigotchi::getMood, potentially monStart/Stop if not fully abstracted
#include "mood.h"       // Directly include mood.h
#include <WiFi.h>
#include "esp_wifi.h" // For esp_wifi_init, esp_wifi_deinit etc.
#include "Arduino.h"  // For Serial, delay

// Initialize static members
wifi_operational_state_t WifiManager::current_state = WIFI_STATE_UNINITIALIZED;
const char* WifiManager::current_controller_tag = "none";
SemaphoreHandle_t WifiManager::wifi_mutex = NULL;
bool WifiManager::is_initialized = false;

WifiManager::WifiManager() {
    if (!is_initialized) {
        wifi_mutex = xSemaphoreCreateMutex();
        if (wifi_mutex != NULL) {
            // Perform initial WiFi setup if not done elsewhere (e.g. in main setup)
            // For now, assume basic WiFi init happens early in application startup.
            // We will manage state from WIFI_STATE_OFF or WIFI_STATE_STA initially.
            initialize_wifi(); // Ensure basic stack is up
            current_state = WIFI_STATE_OFF; // Default to OFF after init
            is_initialized = true;
            Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Initialized and WiFi stack ready.");
        } else {
            Serial.println(Mood::getInstance().getBroken() + " WifiManager: Failed to create mutex!");
            // Handle error: system can't function without mutex
        }
    }
}

WifiManager& WifiManager::getInstance() {
    static WifiManager instance; // Guaranteed to be destroyed and instantiated on first use.
    return instance;
}

void WifiManager::initialize_wifi() {
    // Minimal initialization to get the WiFi stack ready.
    // More comprehensive setup (like starting in STA or AP) should be managed by explicit requests.
    if (current_state == WIFI_STATE_UNINITIALIZED) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        esp_err_t init_err = esp_wifi_init(&cfg);
        if (init_err != ESP_OK) {
            Serial.printf("%s WifiManager: esp_wifi_init failed: %s\n",
                          Mood::getInstance().getBroken().c_str(), esp_err_to_name(init_err));
            return; // Major issue
        }
        esp_err_t start_err = esp_wifi_start(); // Starts the WiFi task
        if (start_err != ESP_OK) {
            Serial.printf("%s WifiManager: esp_wifi_start failed: %s\n",
                          Mood::getInstance().getBroken().c_str(), esp_err_to_name(start_err));
            // esp_wifi_deinit(); // Clean up
            return; // Major issue
        }
        // Default to STA mode and turn off for now, actual mode changes will be explicit
        WiFi.mode(WIFI_OFF); 
        Serial.println(Mood::getInstance().getNeutral() + " WifiManager: WiFi stack initialized and started, mode set to OFF.");
    }
}

void WifiManager::deinitialize_wifi() {
    if (current_state != WIFI_STATE_UNINITIALIZED) {
        esp_wifi_stop();
        esp_wifi_deinit();
        current_state = WIFI_STATE_UNINITIALIZED;
        Serial.println(Mood::getInstance().getNeutral() + " WifiManager: WiFi stack de-initialized.");
    }
}

bool WifiManager::transition_to_state(wifi_operational_state_t target_state, const char* requester_tag) {
    // Set timeout for mutex acquisition to prevent deadlocks
    const TickType_t xMaxWait = pdMS_TO_TICKS(5000); // 5 second timeout
    
    // Start tracking performance/timing
    unsigned long start_time = millis();
    bool mutex_acquired = false;
    
    // Track initial heap state for debugging memory issues
    uint32_t initial_heap = ESP.getFreeHeap();
    
    // Try to acquire the mutex with timeout
    if (xSemaphoreTake(wifi_mutex, xMaxWait) == pdTRUE) {
        mutex_acquired = true;
        unsigned long mutex_wait_time = millis() - start_time;
        
        // Log timing information for monitoring system health
        Serial.printf("%s WifiManager: %s requests transition from %d to %d (mutex acquired in %lu ms).\n",
                     Mood::getInstance().getNeutral().c_str(), requester_tag, current_state, target_state, mutex_wait_time);

        // If already in the requested state, just update the controller tag
        if (current_state == target_state) {
            current_controller_tag = requester_tag; // Allow re-tagging if same state
            Serial.printf("%s WifiManager: Already in state %d. Controller updated to %s.\n",
                         Mood::getInstance().getHappy().c_str(), target_state, requester_tag);
            xSemaphoreGive(wifi_mutex);
            return true;
        }

        bool success = false;
        
        // Record previous state in case we need to rollback
        wifi_operational_state_t previous_state = current_state;
        const char* previous_controller = current_controller_tag;
        
        // Notify before changing state
        Serial.printf("%s WifiManager: Preparing to stop current state %d...\n",
                     Mood::getInstance().getNeutral().c_str(), current_state);
        
        // Mark as changing early to indicate transition in progress
        current_state = WIFI_STATE_CHANGING;
        
        // Stop current activities before changing state - with retries
        bool stop_success = false;
        int stop_attempts = 0;
        unsigned long stop_start_time = millis();
        
        while (!stop_success && stop_attempts < 3 && (millis() - stop_start_time < 3000)) {
            stop_attempts++;
            
            // Clear any interrupt handlers or callbacks first for safety
            if (previous_state == WIFI_STATE_MONITOR) {
                // Explicitly clear any promiscuous callbacks first
                esp_wifi_set_promiscuous_rx_cb(NULL);
                yield();
                
                stop_success = actual_stop_monitor();
            }
            else if (previous_state == WIFI_STATE_STA) {
                stop_success = actual_stop_sta();
            }
            else if (previous_state == WIFI_STATE_AP) {
                stop_success = actual_stop_ap();
            }
            else if (previous_state == WIFI_STATE_OFF || previous_state == WIFI_STATE_UNINITIALIZED) {
                // Nothing to stop if already off or uninitialized
                stop_success = true;
            }
            else if (previous_state == WIFI_STATE_CHANGING) {
                // Already in transition - this is unusual but we can proceed
                Serial.println(Mood::getInstance().getSad() + " WifiManager: WARNING: State was already CHANGING");
                stop_success = true;
            }
            
            if (!stop_success && stop_attempts < 3) {
                Serial.printf("%s WifiManager: Stop attempt %d failed, retrying...\n",
                             Mood::getInstance().getSad().c_str(), stop_attempts);
                delay(50);
                yield();
            }
        }
        
        // If we couldn't stop the current state properly, try more aggressive cleanup
        if (!stop_success) {
            Serial.printf("%s WifiManager: Failed to stop state %d after %d attempts. Forcing cleanup.\n",
                         Mood::getInstance().getBroken().c_str(), previous_state, stop_attempts);
            
            // Force cleanup of any WiFi resources
            esp_wifi_set_promiscuous_rx_cb(NULL);
            esp_wifi_set_promiscuous(false);
            esp_wifi_disconnect();
            yield();
            
            // Consider the stop partially successful and continue with caution
            stop_success = true;
        }

        // Begin transition to target state
        Serial.printf("%s WifiManager: Transitioning to state %d...\n",
                     Mood::getInstance().getNeutral().c_str(), target_state);
        
        switch (target_state) {
            case WIFI_STATE_MONITOR:
                success = actual_start_monitor();
                break;
            case WIFI_STATE_STA:
                success = actual_start_sta();
                break;
            case WIFI_STATE_AP:
                success = actual_start_ap();
                break;
            case WIFI_STATE_OFF:
                success = actual_turn_wifi_off();
                break;
            case WIFI_STATE_SCANNING: // Special case, usually part of another request
                // This state is more of a conceptual one for this manager
                // actual_wifi_scan() will be called by perform_wifi_scan
                success = true; // Placeholder
                break;
            default:
                Serial.printf("%s WifiManager: Unknown target state %d requested by %s.\n",
                             Mood::getInstance().getBroken().c_str(), target_state, requester_tag);
                success = false;
                break;
        }
        
        if (success) {
            current_state = target_state;
            current_controller_tag = requester_tag;
            
            // Log successful transition with timing information
            unsigned long transition_time = millis() - start_time;
            uint32_t heap_after = ESP.getFreeHeap();
            int heap_change = initial_heap - heap_after;
            
            Serial.printf("%s WifiManager: Transition to %d by %s successful in %lu ms (heap change: %d bytes).\n",
                         Mood::getInstance().getHappy().c_str(), target_state, requester_tag, 
                         transition_time, heap_change);
        } else {
            // Enhanced recovery logic with better logging and state management
            Serial.printf("%s WifiManager: Transition to %d by %s FAILED. Attempting recovery...\n",
                         Mood::getInstance().getBroken().c_str(), target_state, requester_tag);
            
            // Try multiple recovery approaches in sequence
            bool recovery_success = false;
            
            // Approach 1: Try to roll back to previous state if it wasn't monitor mode
            if (previous_state != WIFI_STATE_MONITOR && previous_state != WIFI_STATE_CHANGING) {
                Serial.printf("%s WifiManager: Attempting to roll back to previous state %d...\n",
                             Mood::getInstance().getSad().c_str(), previous_state);
                
                // Try to restore previous state based on what it was
                switch (previous_state) {
                    case WIFI_STATE_STA:
                        recovery_success = actual_start_sta();
                        break;
                    case WIFI_STATE_AP:
                        recovery_success = actual_start_ap();
                        break;
                    case WIFI_STATE_OFF:
                        recovery_success = actual_turn_wifi_off();
                        break;
                    default:
                        recovery_success = false;
                        break;
                }
                
                if (recovery_success) {
                    current_state = previous_state;
                    current_controller_tag = previous_controller;
                    Serial.printf("%s WifiManager: Successfully rolled back to previous state %d.\n",
                                 Mood::getInstance().getNeutral().c_str(), previous_state);
                    xSemaphoreGive(wifi_mutex);
                    return false; // Report failure of original transition but system is recovered
                }
            }
            
            // Approach 2: Try to turn WiFi off as the safest fallback
            Serial.println(Mood::getInstance().getSad() + " WifiManager: Rollback failed, trying to set WiFi OFF");
            recovery_success = actual_turn_wifi_off();
            
            if (recovery_success) {
                current_state = WIFI_STATE_OFF;
                current_controller_tag = "system_recovery";
                Serial.printf("%s WifiManager: Recovery successful. Set to OFF state.\n",
                             Mood::getInstance().getNeutral().c_str());
            } else {
                // Approach 3: Last resort - full WiFi reset
                Serial.printf("%s WifiManager: Basic recovery failed. Attempting full WiFi reset...\n",
                             Mood::getInstance().getBroken().c_str());
                
                // Force WiFi reset at ESP-IDF level with proper error handling
                esp_err_t stop_err = esp_wifi_stop();
                Serial.printf("  Stop result: %s\n", esp_err_to_name(stop_err));
                delay(100);
                yield();
                
                esp_err_t deinit_err = esp_wifi_deinit();
                Serial.printf("  Deinit result: %s\n", esp_err_to_name(deinit_err));
                delay(150);
                yield();
                
                // Re-initialize with enhanced buffer settings for better stability
                wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
                cfg.static_rx_buf_num = 16;    // Default is usually 10
                cfg.dynamic_rx_buf_num = 64;   // Default is usually 32
                
                esp_err_t init_err = esp_wifi_init(&cfg);
                Serial.printf("  Init result: %s\n", esp_err_to_name(init_err));
                delay(50);
                yield();
                
                esp_err_t start_err = esp_wifi_start();
                Serial.printf("  Start result: %s\n", esp_err_to_name(start_err));
                
                if (init_err == ESP_OK && start_err == ESP_OK) {
                    // Final verification
                    wifi_mode_t mode;
                    esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_NULL);
                    esp_err_t get_mode_err = esp_wifi_get_mode(&mode);
                    
                    if (mode_err == ESP_OK && get_mode_err == ESP_OK) {
                        current_state = WIFI_STATE_OFF;
                        current_controller_tag = "system_emergency_recovery";
                        Serial.printf("%s WifiManager: Emergency recovery completed.\n",
                                     Mood::getInstance().getNeutral().c_str());
                    } else {
                        current_state = WIFI_STATE_UNINITIALIZED;
                        current_controller_tag = "none";
                        Serial.println(Mood::getInstance().getBroken() + " WifiManager: CRITICAL FAILURE: Mode setting failed!");
                    }
                } else {
                    // Total failure - mark WiFi as uninitialized so next operation will fully reset
                    current_state = WIFI_STATE_UNINITIALIZED;
                    current_controller_tag = "none";
                    Serial.println(Mood::getInstance().getBroken() + " WifiManager: CRITICAL FAILURE: All recovery attempts failed!");
                }
            }
        }
        xSemaphoreGive(wifi_mutex);
        return success;
    }
    
    // Mutex acquisition failed - this is a serious issue
    Serial.printf("%s WifiManager: %s FAILED to take mutex for state transition after %lu ms.\n",
                 Mood::getInstance().getBroken().c_str(), requester_tag, millis() - start_time);
    
    // EMERGENCY: Since we couldn't get the mutex, we need to check if the system is deadlocked
    // Try to detect if this is a deadlock situation by checking how long the mutex has been held
    static unsigned long last_mutex_warning = 0;
    if (millis() - last_mutex_warning > 10000) { // Only print this warning every 10 seconds
        Serial.println(Mood::getInstance().getBroken() + " WifiManager: WARNING: Potential deadlock detected!");
        last_mutex_warning = millis();
    }
    
    return false;
}

// --- Public Request Methods ---
bool WifiManager::request_monitor_mode(const char* requester_tag) {
    if (!ensure_wifi_initialized()) {
        Serial.println(Mood::getInstance().getBroken() + " WifiManager: Failed to ensure WiFi initialized for monitor mode.");
        return false;
    }
    return transition_to_state(WIFI_STATE_MONITOR, requester_tag);
}

bool WifiManager::request_sta_mode(const char* requester_tag) {
    if (!ensure_wifi_initialized()) {
        Serial.println(Mood::getInstance().getBroken() + " WifiManager: Failed to ensure WiFi initialized for STA mode.");
        return false;
    }
    return transition_to_state(WIFI_STATE_STA, requester_tag);
}

bool WifiManager::request_ap_mode(const char* requester_tag) {
    if (!ensure_wifi_initialized()) {
        Serial.println(Mood::getInstance().getBroken() + " WifiManager: Failed to ensure WiFi initialized for AP mode.");
        return false;
    }
    return transition_to_state(WIFI_STATE_AP, requester_tag);
}

bool WifiManager::request_wifi_off(const char* requester_tag) {
    if (!ensure_wifi_initialized()) {
        Serial.println(Mood::getInstance().getBroken() + " WifiManager: Failed to ensure WiFi initialized for OFF mode.");
        return false;
    }
    return transition_to_state(WIFI_STATE_OFF, requester_tag);
}

bool WifiManager::release_wifi_control(const char* requester_tag) {
    // Set timeout for mutex acquisition to prevent deadlocks
    const TickType_t xMaxWait = pdMS_TO_TICKS(3000); // 3 second timeout
    
    // Track performance metrics
    unsigned long start_time = millis();
    uint32_t initial_heap = ESP.getFreeHeap();
    bool mutex_acquired = false;
    
    if (xSemaphoreTake(wifi_mutex, xMaxWait) == pdTRUE) {
        mutex_acquired = true;
        unsigned long mutex_wait_time = millis() - start_time;
        
        Serial.printf("%s WifiManager: %s releasing WiFi control. Current state %d (mutex acquired in %lu ms).\n",
                     Mood::getInstance().getNeutral().c_str(), requester_tag, current_state, mutex_wait_time);
        
        // Compare requesters using strcmp instead of direct comparison
        bool is_controller = (strcmp(current_controller_tag, requester_tag) == 0);
        bool is_system_tag = (strcmp(current_controller_tag, "none") == 0 || 
                            strcmp(current_controller_tag, "system_recovery") == 0 ||
                            strcmp(current_controller_tag, "system_emergency_recovery") == 0);
        
        if (is_controller || is_system_tag) {
            // Log special case handling
            if (!is_controller && is_system_tag) {
                Serial.printf("%s WifiManager: Special case: allowing %s to release control from %s.\n",
                             Mood::getInstance().getNeutral().c_str(), requester_tag, current_controller_tag);
            }
            
            // Keep track of current state for proper cleanup
            wifi_operational_state_t previous_state = current_state;
            
            // First, clean up any promiscuous mode resources - this is critical
            if (previous_state == WIFI_STATE_MONITOR) {
                Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Cleaning up monitor mode resources...");
                
                // Explicitly unregister any callbacks first - most important step
                esp_wifi_set_promiscuous_rx_cb(NULL);
                yield();
                
                // Then disable promiscuous mode with retry and verification
                bool promiscuous_disabled = false;
                for (int attempt = 1; attempt <= 3 && !promiscuous_disabled; attempt++) {
                    esp_err_t promisc_err = esp_wifi_set_promiscuous(false);
                    
                    if (promisc_err == ESP_OK) {
                        // Verify that promiscuous mode is actually disabled
                        bool is_promisc = false;
                        esp_err_t check_err = esp_wifi_get_promiscuous(&is_promisc);
                        
                        if (check_err == ESP_OK && !is_promisc) {
                            promiscuous_disabled = true;
                            Serial.printf("%s WifiManager: Promiscuous mode disabled during release (attempt %d)\n", 
                                         Mood::getInstance().getNeutral().c_str(), attempt);
                        } else {
                            Serial.printf("%s WifiManager: Promiscuous mode not fully disabled (still %d) - retrying\n", 
                                         Mood::getInstance().getSad().c_str(), is_promisc ? 1 : 0);
                        }
                    } else {
                        Serial.printf("%s WifiManager: Failed to disable promiscuous mode (attempt %d): %s\n", 
                                     Mood::getInstance().getBroken().c_str(), 
                                     attempt, 
                                     esp_err_to_name(promisc_err));
                    }
                    
                    // Add delay between attempts
                    if (!promiscuous_disabled && attempt < 3) {
                        delay(50);
                        yield();
                    }
                }
                
                // If failed to disable promiscuous mode, log but continue with cleanup
                if (!promiscuous_disabled) {
                    Serial.println(Mood::getInstance().getBroken() + " WifiManager: WARNING: Failed to disable promiscuous mode during release");
                    // Additional forced cleanup that might help
                    WiFi.mode(WIFI_OFF);
                    delay(50);
                    WiFi.mode(WIFI_STA);
                    delay(50);
                    WiFi.mode(WIFI_OFF);
                    yield();
                }
            }
            
            // Turn WiFi off as the safe state
            Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Setting WiFi to OFF state...");
            bool success = false;
            
            // Try up to 3 times to turn WiFi off properly
            for (int attempt = 1; attempt <= 3 && !success; attempt++) {
                success = actual_turn_wifi_off();
                
                if (success) {
                    Serial.printf("%s WifiManager: Successfully turned WiFi OFF on attempt %d\n", 
                                 Mood::getInstance().getHappy().c_str(), attempt);
                } else if (attempt < 3) {
                    Serial.printf("%s WifiManager: Failed to turn WiFi OFF on attempt %d - retrying\n", 
                                 Mood::getInstance().getSad().c_str(), attempt);
                    delay(50);
                    yield();
                }
            }
            
            if (success) {
                current_state = WIFI_STATE_OFF;
                current_controller_tag = "none";
                
                // Report success with timing information
                unsigned long total_time = millis() - start_time;
                uint32_t heap_after = ESP.getFreeHeap();
                int heap_change = initial_heap - heap_after;
                
                Serial.printf("%s WifiManager: WiFi successfully released and turned OFF in %lu ms (heap change: %d bytes).\n",
                             Mood::getInstance().getHappy().c_str(), total_time, heap_change);
            } else {
                // If we failed to turn WiFi off normally, try more aggressive approach
                Serial.println(Mood::getInstance().getBroken() + " WifiManager: Failed to turn WiFi OFF normally, trying forced reset");
                
                // Force reset WiFi - try a sequence of operations with verification
                WiFi.disconnect(true);  // Disconnect with parameter to clear credentials
                yield();
                
                esp_wifi_disconnect();
                yield();
                
                esp_wifi_stop();
                delay(100);
                yield();
                
                // Try again to set mode to OFF
                if (WiFi.mode(WIFI_OFF)) {
                    // Verify that WiFi is actually off
                    wifi_mode_t mode;
                    esp_err_t mode_err = esp_wifi_get_mode(&mode);
                    
                    if (mode_err == ESP_OK && mode == WIFI_MODE_NULL) {
                        current_state = WIFI_STATE_OFF;
                        current_controller_tag = "none";
                        Serial.println(Mood::getInstance().getNeutral() + " WifiManager: WiFi forced OFF during release.");
                    } else {
                        // The mode setting appeared to succeed but verification failed
                        Serial.println(Mood::getInstance().getSad() + " WifiManager: WiFi mode verification failed after setting OFF");
                        current_state = WIFI_STATE_OFF; // Still mark as OFF in our state
                        current_controller_tag = "none";
                    }
                } else {
                    // Last resort - full ESP-IDF level reset
                    Serial.println(Mood::getInstance().getBroken() + " WifiManager: Arduino mode setting failed, trying ESP-IDF reset");
                    
                    esp_wifi_stop();
                    delay(100);
                    yield();
                    
                    esp_wifi_deinit();
                    delay(150);
                    yield();
                    
                    // Re-initialize WiFi stack
                    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
                    esp_err_t init_err = esp_wifi_init(&cfg);
                    esp_err_t start_err = ESP_OK;
                    
                    if (init_err == ESP_OK) {
                        start_err = esp_wifi_start();
                    }
                    
                    if (init_err == ESP_OK && start_err == ESP_OK) {
                        // Set to NULL mode explicitly
                        esp_wifi_set_mode(WIFI_MODE_NULL);
                        current_state = WIFI_STATE_OFF;
                        current_controller_tag = "none";
                        Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Full WiFi reset successful during release.");
                    } else {
                        // Mark as uninitialized so it will be reset on next use
                        current_state = WIFI_STATE_UNINITIALIZED;
                        current_controller_tag = "none";
                        Serial.println(Mood::getInstance().getBroken() + " WifiManager: Critical failure during release - marking as uninitialized.");
                    }
                }
            }
            
            xSemaphoreGive(wifi_mutex);
            return true;
        } else {
            Serial.printf("%s WifiManager: %s attempted to release control, but %s is current controller.\n",
                         Mood::getInstance().getSad().c_str(), requester_tag, current_controller_tag);
            xSemaphoreGive(wifi_mutex);
            return false;
        }
    }
    
    // Mutex acquisition failed - potential deadlock situation
    Serial.printf("%s WifiManager: %s failed to take mutex for release (timeout after 3s).\n",
                 Mood::getInstance().getBroken().c_str(), requester_tag);
    
    // EMERGENCY DEADLOCK RECOVERY: If we couldn't get the mutex after timeout, 
    // we need to check if this is a potential deadlock
    static unsigned long last_emergency_attempt = 0;
    unsigned long now = millis();
    
    // Only attempt emergency release once per 15 seconds to prevent cascading failures
    if (now - last_emergency_attempt > 15000) {
        last_emergency_attempt = now;
        
        Serial.println(Mood::getInstance().getBroken() + 
                      " WifiManager: EMERGENCY DEADLOCK DETECTED - Forcing WiFi release without mutex!");
        
        // Log current system state
        Serial.printf("Current state: %d, Controller: %s, Heap: %u\n", 
                     current_state, current_controller_tag, ESP.getFreeHeap());
        
        // Try emergency cleanup without mutex (normally dangerous but better than hanging)
        Serial.println("Executing emergency forced cleanup sequence:");
        
        // 1. Clear callback and disable promiscuous mode
        Serial.println("  1. Clearing promiscuous callback");
        esp_err_t cb_err = esp_wifi_set_promiscuous_rx_cb(NULL);
        Serial.printf("     Result: %s\n", esp_err_to_name(cb_err));
        yield();
        
        Serial.println("  2. Disabling promiscuous mode");
        esp_err_t prom_err = esp_wifi_set_promiscuous(false);
        Serial.printf("     Result: %s\n", esp_err_to_name(prom_err));
        yield();
        
        // 2. Force disconnect and set WiFi off
        Serial.println("  3. Forcing WiFi OFF");
        WiFi.disconnect(true);
        bool mode_result = WiFi.mode(WIFI_OFF);
        Serial.printf("     Result: %d\n", mode_result ? 1 : 0);
        delay(50);
        yield();
        
        // 3. ESP-IDF level operations
        Serial.println("  4. Stopping WiFi at ESP-IDF level");
        esp_err_t stop_err = esp_wifi_stop();
        Serial.printf("     Result: %s\n", esp_err_to_name(stop_err));
        
        // 4. If task identity is known, set state
        // We can't safely update state variables here normally, but in a deadlock emergency, 
        // we need to attempt to break out of it
        if (strcmp(requester_tag, current_controller_tag) == 0) {
            current_state = WIFI_STATE_OFF;
            current_controller_tag = "none";
            Serial.println("  5. Reset state variables since requester was controller");
        } else {
            Serial.printf("  5. Cannot reset state variables (%s ≠ %s)\n", 
                         requester_tag, current_controller_tag);
        }
        
        Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Emergency recovery sequence completed.");
        return true; // Return true to indicate the requester should consider it released
    }
    
    return false;
}

bool WifiManager::perform_wifi_scan(const char* requester_tag) {
    if (!ensure_wifi_initialized()) {
        Serial.println(Mood::getInstance().getBroken() + " WifiManager: Failed to ensure WiFi initialized for scan.");
        return false;
    }
    if (xSemaphoreTake(wifi_mutex, portMAX_DELAY) == pdTRUE) {
        Serial.printf("%s WifiManager: %s requests WiFi scan.\n",
                      Mood::getInstance().getNeutral().c_str(), requester_tag);
        wifi_operational_state_t previous_state = current_state;
        const char* previous_controller = current_controller_tag;

        // Ensure WiFi is in STA mode for scanning, or at least not monitor.
        if (current_state == WIFI_STATE_MONITOR) actual_stop_monitor();
        if (current_state != WIFI_STATE_STA) actual_start_sta(); // Ensure STA for scan

        current_state = WIFI_STATE_SCANNING;
        current_controller_tag = requester_tag;

        bool scan_success = actual_wifi_scan(); // This is the blocking WiFi.scanNetworks()

        if (scan_success) {
             Serial.printf("%s WifiManager: Scan by %s successful.\n", Mood::getInstance().getHappy().c_str(), requester_tag);
        } else {
             Serial.printf("%s WifiManager: Scan by %s FAILED.\n", Mood::getInstance().getBroken().c_str(), requester_tag);
        }

        // Revert to previous state or a sensible default.
        // For simplicity, let's try to revert or go to STA.
        // More complex logic might be needed if previous_state was AP or MONITOR.
        if (previous_state == WIFI_STATE_MONITOR) { // If it was monitor, try to go back
             actual_start_monitor();
             current_state = WIFI_STATE_MONITOR;
        } else { // Default to STA after scan if not monitor before
             actual_start_sta(); // Re-ensure STA if it wasn't monitor
             current_state = WIFI_STATE_STA;
        }
        current_controller_tag = previous_controller; // Restore controller tag or set to "system_after_scan"

        xSemaphoreGive(wifi_mutex);
        return scan_success;
    }
    Serial.printf("%s WifiManager: %s FAILED to take mutex for WiFi scan.\n",
                  Mood::getInstance().getBroken().c_str(), requester_tag);
    return false;
}

bool WifiManager::perform_wifi_reset(const char* requester_tag) {
    if (!ensure_wifi_initialized()) {
        Serial.println(Mood::getInstance().getBroken() + " WifiManager: Failed to ensure WiFi initialized for reset.");
        return false;
    }
    if (xSemaphoreTake(wifi_mutex, portMAX_DELAY) == pdTRUE) {
        Serial.printf("%s WifiManager: %s requests WiFi reset.\n",
                      Mood::getInstance().getNeutral().c_str(), requester_tag);
        current_state = WIFI_STATE_CHANGING;
        current_controller_tag = requester_tag;

        bool reset_success = actual_wifi_reset();

        if (reset_success) {
            current_state = WIFI_STATE_OFF; // Reset usually leaves WiFi off or in a basic STA state.
            current_controller_tag = requester_tag; // Tag remains with requester
            Serial.printf("%s WifiManager: WiFi reset by %s successful. State is now OFF.\n",
                          Mood::getInstance().getHappy().c_str(), requester_tag);
        } else {
            Serial.printf("%s WifiManager: WiFi reset by %s FAILED. State is now OFF.\n",
                          Mood::getInstance().getBroken().c_str(), requester_tag);
            current_state = WIFI_STATE_OFF;
            current_controller_tag = "system_recovery";
        }
        xSemaphoreGive(wifi_mutex);
        return reset_success;
    }
    Serial.printf("%s WifiManager: %s FAILED to take mutex for WiFi reset.\n",
                  Mood::getInstance().getBroken().c_str(), requester_tag);
    return false;
}


wifi_operational_state_t WifiManager::get_current_state() {
    // Mutex not strictly needed for reading enum/pointer if atomicity isn't critical,
    // but good practice if state can change during read. For now, direct read.
    return current_state;
}

const char* WifiManager::get_current_controller_tag() {
    return current_controller_tag;
}

// --- Private Actual Implementation Methods ---
// These assume wifi_mutex is already taken by the calling public method.

bool WifiManager::actual_start_monitor() {
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Starting monitor mode...");
    
    // Add yield to prevent watchdog timeouts
    yield();
    
    // IMPROVED APPROACH: Let's make this process more robust with better error handling
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Starting full WiFi reset sequence for monitor mode");
    
    // First, check if WiFi is already initialized
    wifi_mode_t mode;
    esp_err_t mode_check = esp_wifi_get_mode(&mode);
    
    // Track initialization state
    bool was_initialized = (mode_check != ESP_ERR_WIFI_NOT_INIT);
    
    if (was_initialized) {
        Serial.println(Mood::getInstance().getNeutral() + " WifiManager: WiFi already initialized, performing clean shutdown");
        // 1. Clean up existing WiFi state completely
        esp_wifi_set_promiscuous_rx_cb(NULL); // Clear any callbacks first
        esp_wifi_set_promiscuous(false);      // Disable promiscuous mode
        delay(50);
        yield();
        
        esp_wifi_disconnect();
        delay(50);
        yield();
        
        esp_wifi_stop();
        delay(100);
        yield();
        
        esp_wifi_deinit();
        delay(150);
        yield();
    } else {
        Serial.println(Mood::getInstance().getNeutral() + " WifiManager: WiFi not initialized, starting fresh");
    }
    
    // 2. Re-initialize WiFi with increased buffer configuration for better stability
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Initializing WiFi stack with optimized settings");
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    
    // Improve buffer configuration for better stability in monitor mode
    cfg.static_rx_buf_num = 16;    // Default is usually 10
    cfg.dynamic_rx_buf_num = 64;   // Default is usually 32
    cfg.tx_buf_type = 1;           // Enable dynamic TX buffers
    cfg.dynamic_tx_buf_num = 32;   // Default is usually 32
    cfg.ampdu_rx_enable = 0;       // Disable AMPDU for simpler packet processing
    
    esp_err_t init_err = esp_wifi_init(&cfg);
    if (init_err != ESP_OK) {
        Serial.printf("%s WifiManager: WiFi init failed: %s\n", 
                     Mood::getInstance().getBroken().c_str(), 
                     esp_err_to_name(init_err));
        return false;
    }
    
    // 3. Start WiFi with longer delay for stability
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Starting WiFi driver");
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK) {
        Serial.printf("%s WifiManager: WiFi start failed: %s\n", 
                     Mood::getInstance().getBroken().c_str(), 
                     esp_err_to_name(start_err));
        return false;
    }
      // Critical: Shorter delay after start to ensure WiFi task is initialized but not waste time
    delay(100);  // Reduced from 250ms to 100ms total
    yield();
    
    // 4. Set to STA mode for monitor mode with retries
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Setting STA mode for monitor...");
    bool sta_mode_set = false;
    
    for (int retry = 0; retry < 3; retry++) {
        esp_err_t sta_err = esp_wifi_set_mode(WIFI_MODE_STA);
        if (sta_err == ESP_OK) {
            sta_mode_set = true;
            break;
        }
        
        Serial.printf("%s WifiManager: Attempt %d to set STA mode failed: %s\n", 
                     Mood::getInstance().getNeutral().c_str(),
                     retry + 1, 
                     esp_err_to_name(sta_err));
        
        delay(100 * (retry + 1));  // Increasing delay between retries
        yield();
    }
    
    if (!sta_mode_set) {
        Serial.println(Mood::getInstance().getBroken() + " WifiManager: Failed to set STA mode after all attempts");
        // Try to clean up
        esp_wifi_stop();
        esp_wifi_deinit();
        return false;
    }
    
    // 5. Verify mode was set correctly
    esp_err_t get_mode_err = esp_wifi_get_mode(&mode);
    if (get_mode_err != ESP_OK || mode != WIFI_MODE_STA) {
        Serial.printf("%s WifiManager: Mode verification failed. Expected STA, got: %d, error: %s\n",
                     Mood::getInstance().getBroken().c_str(), 
                     mode,
                     esp_err_to_name(get_mode_err));
        return false;
    }
    
    // 6. Explicitly disconnect from any networks
    esp_wifi_disconnect();
    delay(50);
    yield();
    
    // 7. Set channel explicitly before enabling promiscuous mode
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Setting initial channel to 1");
    esp_err_t channel_err = esp_wifi_set_channel(1, WIFI_SECOND_CHAN_NONE);
    if (channel_err != ESP_OK) {
        Serial.printf("%s WifiManager: Failed to set channel: %s\n", 
                     Mood::getInstance().getBroken().c_str(), 
                     esp_err_to_name(channel_err));
        // Not fatal, continue
    }
    delay(50);
    yield();
    
    // 8. Enable promiscuous mode with retries
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Enabling promiscuous mode...");
    bool prom_success = false;
    
    for (int retry = 0; retry < 3; retry++) {
        esp_err_t prom_err = esp_wifi_set_promiscuous(true);
        if (prom_err == ESP_OK) {
            prom_success = true;
            break;
        }
        
        Serial.printf("%s WifiManager: Attempt %d to enable promiscuous mode failed: %s\n", 
                     Mood::getInstance().getNeutral().c_str(),
                     retry + 1, 
                     esp_err_to_name(prom_err));
        
        delay(100 * (retry + 1));
        yield();
    }
    
    if (!prom_success) {
        Serial.println(Mood::getInstance().getBroken() + " WifiManager: Failed to enable promiscuous mode after all attempts");
        return false;
    }
    
    // 9. Verify promiscuous mode is enabled
    bool is_promiscuous = false;
    esp_err_t get_prom_err = esp_wifi_get_promiscuous(&is_promiscuous);
    if (get_prom_err != ESP_OK || !is_promiscuous) {
        Serial.printf("%s WifiManager: Promiscuous mode verification failed. Enabled: %d, error: %s\n",
                     Mood::getInstance().getBroken().c_str(), 
                     is_promiscuous,
                     esp_err_to_name(get_prom_err));
        return false;
    }
    
    // Final yield
    yield();
    
    Serial.println(Mood::getInstance().getHappy() + " WifiManager: Monitor mode successfully enabled");
    return true;
}

bool WifiManager::actual_stop_monitor() {
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Stopping monitor mode...");
    
    // First, disable any active callbacks to prevent them from being called after disabling promiscuous mode
    esp_wifi_set_promiscuous_rx_cb(NULL);
    yield();
    
    // Disable promiscuous mode with retries
    bool promiscuous_disabled = false;
    for (int attempt = 1; attempt <= 3; attempt++) {
        esp_err_t promisc_err = esp_wifi_set_promiscuous(false);
        if (promisc_err == ESP_OK) {
            promiscuous_disabled = true;
            break;
        }
        Serial.printf("%s WifiManager: Failed to disable promiscuous mode (attempt %d): %s\n", 
                     Mood::getInstance().getBroken().c_str(), 
                     attempt, 
                     esp_err_to_name(promisc_err));
        delay(50 * attempt);
        yield();
    }
    
    if (!promiscuous_disabled) {
        // More aggressive approach if multiple attempts failed
        Serial.println(Mood::getInstance().getIntense() + " WifiManager: Multiple promiscuous disable attempts failed, performing WiFi reset");
        
        // Full reset approach
        esp_wifi_stop();
        delay(100);
        yield();
        
        esp_wifi_deinit();
        delay(150);
        yield();
        
        // Re-initialize WiFi in STA mode
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&cfg) == ESP_OK && esp_wifi_start() == ESP_OK) {
            esp_wifi_set_mode(WIFI_MODE_STA);
            Serial.println(Mood::getInstance().getNeutral() + " WifiManager: WiFi reset to STA mode after failed promiscuous disable");
        } else {
            Serial.println(Mood::getInstance().getBroken() + " WifiManager: Complete WiFi reset failed");
            return false;
        }
    } else {
        Serial.println(Mood::getInstance().getHappy() + " WifiManager: Promiscuous mode disabled successfully");
    }
    
    // Final cleanup - ensure we're in a valid state
    WiFi.mode(WIFI_STA); // Use the Arduino WiFi library to ensure we're in a known good state
    
    // One final yield
    yield();
    
    Serial.println(Mood::getInstance().getHappy() + " WifiManager: Monitor mode stopped completely");
    return true;
}

bool WifiManager::actual_start_sta() {
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Setting WiFi mode to STA...");
    bool success = WiFi.mode(WIFI_STA);
    if(success) Serial.println(Mood::getInstance().getHappy() + " WifiManager: WiFi mode set to STA.");
    else Serial.println(Mood::getInstance().getBroken() + " WifiManager: FAILED to set WiFi mode to STA.");
    return success;
}

bool WifiManager::actual_stop_sta() {
    // Often, just changing to another mode (like OFF or AP) is the "stop" for STA.
    // If specific STA cleanup is needed, add here.
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: STA mode stopped (usually by changing mode).");
    return true;
}

bool WifiManager::actual_start_ap() {
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Setting WiFi mode to AP...");
    bool success = WiFi.mode(WIFI_AP);
    if(success) Serial.println(Mood::getInstance().getHappy() + " WifiManager: WiFi mode set to AP.");
    else Serial.println(Mood::getInstance().getBroken() + " WifiManager: FAILED to set WiFi mode to AP.");
    return success;
}

bool WifiManager::actual_stop_ap() {
    // Similar to STA, changing mode usually stops AP.
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: AP mode stopped (usually by changing mode).");
    return true;
}

bool WifiManager::actual_turn_wifi_off() {
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Turning WiFi OFF...");
    bool success = WiFi.mode(WIFI_OFF);
    if(success) Serial.println(Mood::getInstance().getHappy() + " WifiManager: WiFi turned OFF.");
    else Serial.println(Mood::getInstance().getBroken() + " WifiManager: FAILED to turn WiFi OFF.");
    // Optionally: esp_wifi_stop(); if a deeper off is needed. WiFi.mode(WIFI_OFF) might be sufficient.
    return success;
}

bool WifiManager::actual_wifi_scan() {
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Performing blocking WiFi scan...");
    // Ensure STA mode is set before scanning (WiFi.scanNetworks implicitly uses STA)
    // This is already handled by perform_wifi_scan's logic
    int n = WiFi.scanNetworks();
    Serial.printf("%s WifiManager: Scan found %d networks.\n", Mood::getInstance().getNeutral().c_str(), n);
    return n >= 0; // scanNetworks returns -1 on fail, -2 if scan is not triggered
}

bool WifiManager::actual_wifi_reset() {
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: Performing FULL WiFi reset sequence...");
    
    // PHASE 1: GRACEFUL SHUTDOWN
    // First try to cleanly stop any WiFi activity
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: RESET PHASE 1 - Graceful shutdown");
    
    // 1.1: Disable any packet callbacks
    esp_wifi_set_promiscuous_rx_cb(NULL);
    yield();
    
    // 1.2: Disable promiscuous mode
    esp_err_t prom_err = esp_wifi_set_promiscuous(false);
    if (prom_err != ESP_OK) {
        Serial.printf("%s WifiManager: Failed to disable promiscuous mode during reset: %s\n",
                     Mood::getInstance().getSad().c_str(), esp_err_to_name(prom_err));
        // Continue with reset anyway
    }
    yield();
    
    // 1.3: Disconnect from any networks
    esp_wifi_disconnect();
    yield();
    
    // 1.4: Turn WiFi off via Arduino API
    bool mode_off_success = WiFi.mode(WIFI_OFF);
    if (!mode_off_success) {
        Serial.println(Mood::getInstance().getSad() + " WifiManager: Failed to set mode WIFI_OFF via Arduino API");
        // Continue reset, this is not critical
    }
    delay(50);
    yield();
    
    // PHASE 2: FORCED SHUTDOWN
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: RESET PHASE 2 - Forced shutdown");
    
    // 2.1: Stop WiFi at ESP-IDF level
    esp_err_t stop_err = esp_wifi_stop();
    if (stop_err != ESP_OK) {
        Serial.printf("%s WifiManager: esp_wifi_stop failed during reset: %s\n",
                     Mood::getInstance().getSad().c_str(), esp_err_to_name(stop_err));
        // Continue anyway - the deinit should clean up
    }
    delay(100);
    yield();
    
    // 2.2: Deinitialize WiFi completely
    esp_err_t deinit_err = esp_wifi_deinit();
    if (deinit_err != ESP_OK) {
        Serial.printf("%s WifiManager: esp_wifi_deinit failed during reset: %s\n",
                     Mood::getInstance().getBroken().c_str(), esp_err_to_name(deinit_err));
        // This is more concerning but still try to reinitialize
    }
    delay(150);
    yield();
    
    // PHASE 3: REINITIALIZATION
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: RESET PHASE 3 - Reinitialization");
    
    // 3.1: Initialize WiFi with optimized configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    // Enhanced buffer settings for better stability
    cfg.static_rx_buf_num = 16;    // Default is usually 10
    cfg.dynamic_rx_buf_num = 64;   // Default is usually 32
    cfg.tx_buf_type = 1;           // Enable dynamic TX buffers
    cfg.dynamic_tx_buf_num = 32;   // Default is usually 32
    cfg.ampdu_rx_enable = 0;       // Disable AMPDU for simpler packet processing
    
    esp_err_t init_err = esp_wifi_init(&cfg);
    if (init_err != ESP_OK) {
        Serial.printf("%s WifiManager: CRITICAL FAILURE: esp_wifi_init failed during reset: %s\n",
                     Mood::getInstance().getBroken().c_str(), esp_err_to_name(init_err));
        return false; // Cannot proceed if this fails
    }
    delay(100);
    yield();
    
    // 3.2: Start WiFi task
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK) {
        Serial.printf("%s WifiManager: CRITICAL FAILURE: esp_wifi_start failed during reset: %s\n",
                     Mood::getInstance().getBroken().c_str(), esp_err_to_name(start_err));
        return false; // Cannot proceed if this fails
    }
    delay(100);
    yield();
    
    // 3.3: Set mode to NULL (OFF) at ESP-IDF level
    esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_NULL);
    if (mode_err != ESP_OK) {
        Serial.printf("%s WifiManager: esp_wifi_set_mode(WIFI_MODE_NULL) failed during reset: %s\n",
                     Mood::getInstance().getSad().c_str(), esp_err_to_name(mode_err));
        // Not critical - try setting via Arduino API as fallback
        WiFi.mode(WIFI_OFF);
    }
    delay(50);
    yield();
    
    // PHASE 4: VERIFICATION
    Serial.println(Mood::getInstance().getNeutral() + " WifiManager: RESET PHASE 4 - Verification");
    
    // 4.1: Verify WiFi is initialized and in expected state
    wifi_mode_t mode;
    esp_err_t get_mode_err = esp_wifi_get_mode(&mode);
    
    if (get_mode_err == ESP_OK) {
        Serial.printf("%s WifiManager: WiFi reset completed. Current mode: %d\n",
                     Mood::getInstance().getHappy().c_str(), mode);
    } else {
        Serial.printf("%s WifiManager: Failed to verify WiFi mode after reset: %s\n",
                     Mood::getInstance().getSad().c_str(), esp_err_to_name(get_mode_err));
        // Not critical enough to fail the whole reset
    }
    
    // 4.2: Verify promiscuous mode is disabled
    bool is_promiscuous = false;
    esp_err_t get_prom_err = esp_wifi_get_promiscuous(&is_promiscuous);
    
    if (get_prom_err == ESP_OK && !is_promiscuous) {
        Serial.println(Mood::getInstance().getHappy() + " WifiManager: Confirmed promiscuous mode is disabled after reset");
    } else if (get_prom_err == ESP_OK && is_promiscuous) {
        Serial.println(Mood::getInstance().getSad() + " WifiManager: WARNING: Promiscuous mode still enabled after reset! Attempting to disable...");
        esp_wifi_set_promiscuous(false);
    }
    
    // Final stabilization delay
    delay(50);
    yield();
    
    Serial.println(Mood::getInstance().getHappy() + " WifiManager: WiFi reset sequence COMPLETED SUCCESSFULLY");
    return true; // Successfully completed all critical phases
}

bool WifiManager::ensure_wifi_initialized() {
    if (xSemaphoreTake(wifi_mutex, portMAX_DELAY) == pdTRUE) {
        // Check if WiFi is already initialized at ESP-IDF level
        wifi_mode_t mode;
        esp_err_t err = esp_wifi_get_mode(&mode);
        
        if (err == ESP_ERR_WIFI_NOT_INIT) {
            Serial.println(Mood::getInstance().getIntense() + " WifiManager: WiFi not initialized, performing full initialization...");
            
            // Step 1: Deinitialize WiFi completely first to ensure clean state
            esp_wifi_stop();  // Just in case a partial init left it running
            esp_wifi_deinit(); // Clean slate
            delay(50);  // Short delay to ensure cleanup
            
            // Step 2: Initialize ESP-IDF WiFi with default config
            wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
            // Increase buffers for more stability
            cfg.static_rx_buf_num = 16;  // Default is usually 10
            cfg.dynamic_rx_buf_num = 32; // Default is usually 32
            cfg.tx_buf_type = 1;         // Enable dynamic TX buffers
            cfg.dynamic_tx_buf_num = 32; // Default is usually 32
            
            esp_err_t init_err = esp_wifi_init(&cfg);
            if (init_err != ESP_OK) {
                Serial.printf("%s WifiManager: esp_wifi_init failed: %s\n",
                            Mood::getInstance().getBroken().c_str(), esp_err_to_name(init_err));
                xSemaphoreGive(wifi_mutex);
                return false;
            }
            
            // Step 3: Start WiFi at ESP-IDF level
            esp_err_t start_err = esp_wifi_start();
            if (start_err != ESP_OK) {
                Serial.printf("%s WifiManager: esp_wifi_start failed: %s\n",
                            Mood::getInstance().getBroken().c_str(), esp_err_to_name(start_err));
                esp_wifi_deinit(); // Clean up
                xSemaphoreGive(wifi_mutex);
                return false;
            }
            
            // Step 4: Critical delay to allow ESP-IDF WiFi task to start (increased from 150ms to 250ms)
            for (int i = 0; i < 5; i++) {
                delay(50);
                yield(); // Allow other processes to run during the delay
            }
            
            // Step 5: Set mode to OFF using ESP-IDF directly (not Arduino WiFi)
            esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_NULL);
            if (mode_err != ESP_OK) {
                Serial.printf("%s WifiManager: esp_wifi_set_mode(WIFI_MODE_NULL) failed: %s\n",
                            Mood::getInstance().getBroken().c_str(), esp_err_to_name(mode_err));
                // Continue anyway, not critical
            }
            
            // Step 6: Verify that WiFi is initialized
            esp_err_t verify_err = esp_wifi_get_mode(&mode);
            if (verify_err != ESP_OK) {
                Serial.printf("%s WifiManager: Verification failed after initialization: %s\n",
                            Mood::getInstance().getBroken().c_str(), esp_err_to_name(verify_err));
                xSemaphoreGive(wifi_mutex);
                return false;
            }
            
            // Step 7: Final stabilization delay
            delay(50);
            
            Serial.println(Mood::getInstance().getHappy() + " WifiManager: WiFi stack initialized and set to OFF by ensure_wifi_initialized().");
        } else if (err != ESP_OK) {
            // Some other error occurred when checking WiFi mode
            Serial.printf("%s WifiManager: Error checking WiFi mode: %s\n",
                        Mood::getInstance().getBroken().c_str(), esp_err_to_name(err));
            xSemaphoreGive(wifi_mutex);
            return false;
        } else {
            // WiFi is already initialized, just log current mode
            Serial.printf("%s WifiManager: WiFi already initialized, current mode: %d\n",
                        Mood::getInstance().getNeutral().c_str(), mode);
        }
        
        xSemaphoreGive(wifi_mutex);
        return true;
    }
    
    Serial.println(Mood::getInstance().getBroken() + " WifiManager: ensure_wifi_initialized: Failed to take mutex!");
    return false;
}

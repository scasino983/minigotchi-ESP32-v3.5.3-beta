/**
 * Enhanced WiFi reset implementation to replace the existing one in wifi_manager.cpp
 * This provides a more robust and bulletproof implementation with better error handling
 * and recovery mechanisms.
 * 
 * To use: Replace the actual_wifi_reset function in wifi_manager.cpp with this implementation.
 */

#include "wifi_manager.h"
#include "mood.h"
#include <Arduino.h>
#include <WiFi.h>
#include "esp_wifi.h"
#include <cstddef>

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

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
 * minigotchi.cpp: handles system usage info, etc
 */

#include "minigotchi.h"
#include <SPI.h>
#include <SD.h>
#define SD_CS_PIN 5
#include "wifi_sniffer.h" // Include the WiFi sniffer header
#include "handshake_logger.h" // Include the handshake logger header
#include "channel_hopper.h" // Include the channel hopper header
#include "wifi_manager.h" // Include the WiFi Manager
#include <nvs_flash.h> // Include for NVS functions

// Status display variables
unsigned long lastStatsUpdate = 0;
const unsigned long STATS_UPDATE_INTERVAL = 10000; // 10 seconds
bool sniffer_active = false;

// Serial command buffer
String serialBuffer = "";
bool commandMode = false;

// Arduino required setup function - runs once at startup
void setup() {
  Serial.begin(115200);

  // Normal boot procedure (which includes Mood::init())
  Minigotchi::boot();         // MOVED EARLIER

  // Initialize the WiFi Manager Singleton (constructor uses Mood::getInstance())
  WifiManager::getInstance(); 
  
  // Add 3-second window to enter reset command mode
  Serial.println("\n\n*** Press 'r' within 3 seconds to enter reset configuration mode ***");
  
  unsigned long startTime = millis();
  while (millis() - startTime < 3000) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'r' || c == 'R') {
        commandMode = true;
        Serial.println("\n*** COMMAND MODE ACTIVATED ***");
        Serial.println("Type 'reset' to reset device configuration");
        Serial.println("Type 'exit' to continue normal boot");
        break;
      }
    }
    delay(10);
  }
  
  // If command mode activated, wait for commands
  if (commandMode) {
    while (commandMode) {
      if (Serial.available()) {
        char c = Serial.read();
        
        // Add character to buffer or process on newline
        if (c == '\n' || c == '\r') {
          if (serialBuffer.length() > 0) {
            processCommand(serialBuffer);
            serialBuffer = "";
          }
        } else {
          serialBuffer += c;
        }
      }
      delay(10);
    }
  }
  
  // Normal boot procedure - MOVED EARLIER
  // Minigotchi::boot(); 
  
  // Start the WiFi sniffer automatically at boot
  sniffer_active = (wifi_sniffer_start() == ESP_OK);
}

// Process command from serial
void processCommand(String command) {
  command.trim();
  Serial.println("Command: " + command);
  
  if (command == "reset") {
    resetConfiguration();
  } else if (command == "exit") {
    Serial.println("Exiting command mode, continuing normal boot...");
    commandMode = false;
  } else {
    Serial.println("Unknown command. Available commands: reset, exit");
  }
}

// Reset configuration and reboot
void resetConfiguration() {
  Serial.println("Resetting device configuration...");
  Display::updateDisplay(Minigotchi::getMood().getIntense(), "Resetting config...");
  
  // Erase NVS storage
  esp_err_t err = nvs_flash_erase();
  if (err != ESP_OK) {
    Serial.println("Error erasing NVS: " + String(esp_err_to_name(err)));
    Display::updateDisplay(Minigotchi::getMood().getBroken(), "Reset failed!");
  } else {
    // Reinitialize NVS
    err = nvs_flash_init();
    if (err != ESP_OK) {
      Serial.println("Error initializing NVS: " + String(esp_err_to_name(err)));
      Display::updateDisplay(Minigotchi::getMood().getBroken(), "Reset failed!");
    } else {
      // Set configured flag to false and save
      Config::configured = false;
      Config::saveConfig();
      
      Serial.println("Configuration reset successful!");
      Serial.println("Device will reboot in 3 seconds...");
      Display::updateDisplay(Minigotchi::getMood().getHappy(), "Reset complete! Rebooting...");
      
      delay(3000);
      ESP.restart();
    }
  }
  
  commandMode = false;
}

// Arduino required loop function - runs repeatedly
void loop() {
  Serial.println("[MAINLOOP] Heartbeat: Entry");
  // Print heap and stack diagnostics
  Serial.printf("[MAINLOOP] Free heap: %d\n", ESP.getFreeHeap());
  UBaseType_t stackHighWaterMark = uxTaskGetStackHighWaterMark(NULL);
  Serial.printf("[MAINLOOP] Stack high water mark: %u\n", stackHighWaterMark);
  // Print FreeRTOS task list
  char *taskListBuf = (char*)malloc(1024);
  if (taskListBuf) {
    vTaskList(taskListBuf);
    Serial.println("[MAINLOOP] FreeRTOS task list:");
    Serial.println(taskListBuf);
    free(taskListBuf);
  }

  Serial.println("[loop] Entry");
  // Handle serial commands in normal operation mode
  if (Serial.available()) {
    char c = Serial.read();
    // Check for special command sequence
    if (c == '!') {
      serialBuffer = "";
      Serial.println("\n*** COMMAND MODE ***");
    } else if (c == '\n' || c == '\r') {
      if (serialBuffer.startsWith("reset")) {
        resetConfiguration();
      }
      serialBuffer = "";
    } else {
      serialBuffer += c;
    }
  }
  Serial.println("[loop] Before Minigotchi::cycle()");
  Minigotchi::cycle();
  yield();
  Serial.println("[loop] After Minigotchi::cycle(), before Minigotchi::detect()");
  Minigotchi::detect();
  yield();
  Serial.println("[loop] After Minigotchi::detect(), before Minigotchi::advertise()");
  // Only advertise if sniffer is running (prevents WiFi state confusion)
  if (sniffer_active && is_sniffer_running()) {
    Minigotchi::advertise();
    yield();
    Serial.println("[loop] After Minigotchi::advertise()");
  } else {
    Serial.println("[loop] Skipping advertise() (sniffer not active)");
  }

  // Regular epoch update
  static unsigned long lastEpochUpdate = 0;
  if (millis() - lastEpochUpdate > 30000) {
    Minigotchi::epoch();
    lastEpochUpdate = millis();
    yield();
  }

  // Channel hopping stats display
  unsigned long currentMillis = millis();
  if (currentMillis - lastStatsUpdate > STATS_UPDATE_INTERVAL) {
    lastStatsUpdate = currentMillis;
    if (sniffer_active && is_sniffer_running() && channel_hopping_task_handle != NULL) {
      int currentChannel = Channel::getChannel();
      uint32_t successful_hops = get_successful_channel_hops();
      uint32_t failed_hops = get_failed_channel_hops();
      uint32_t hop_interval = get_channel_hop_interval_ms();
      float success_rate = 0;
      if (successful_hops + failed_hops > 0) {
        success_rate = (float)successful_hops / (successful_hops + failed_hops) * 100.0;
      }
      Serial.printf("%s CH:%d | Hop Stats: %d OK, %d Fail (%.1f%%), Interval: %dms\n", 
                   Minigotchi::getMood().getNeutral().c_str(),
                   currentChannel,
                   successful_hops, 
                   failed_hops,
                   success_rate,
                   hop_interval);
      char stats_buf[64];
      snprintf(stats_buf, sizeof(stats_buf), "CH:%d | %.1f%% hop success", 
              currentChannel, success_rate);
      Display::updateDisplay(Minigotchi::getMood().getNeutral(), stats_buf);
    }
    yield();
  }

  Serial.println("[loop] End");
  Serial.println("[MAINLOOP] Heartbeat: End");
  delay(100); // Always delay to feed watchdog
  yield();
}

// Toggle WiFi sniffer state
void toggleSniffer() {
  if (is_sniffer_running()) {
    Serial.println(Minigotchi::getMood().getNeutral() + " Stopping WiFi sniffer...");
    wifi_sniffer_stop();
    sniffer_active = false;
    Display::updateDisplay(Minigotchi::getMood().getNeutral(), "Sniffer stopped");
  } else {
    Serial.println(Minigotchi::getMood().getIntense() + " Starting WiFi sniffer...");
    sniffer_active = (wifi_sniffer_start() == ESP_OK);
    if (sniffer_active) {
      Display::updateDisplay(Minigotchi::getMood().getHappy(), "Sniffer started");
    } else {
      Display::updateDisplay(Minigotchi::getMood().getBroken(), "Sniffer start failed");
    }
  }
  delay(1000); // Show status message before returning to normal display
}

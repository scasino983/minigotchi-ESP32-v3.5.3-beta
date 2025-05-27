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

// Status display variables
unsigned long lastStatsUpdate = 0;
const unsigned long STATS_UPDATE_INTERVAL = 10000; // 10 seconds
bool sniffer_active = false;

// Arduino required setup function - runs once at startup
void setup() {
  Serial.begin(115200);
  Minigotchi::boot();
  
  // Start the WiFi sniffer automatically at boot
  sniffer_active = (wifi_sniffer_start() == ESP_OK);
}

// Arduino required loop function - runs repeatedly
void loop() {
  Minigotchi::cycle();
  Minigotchi::detect();
  Minigotchi::advertise();
  
  // Regular epoch update
  static unsigned long lastEpochUpdate = 0;
  if (millis() - lastEpochUpdate > 30000) {
    Minigotchi::epoch();
    lastEpochUpdate = millis();
  }
  
  // Channel hopping stats display
  unsigned long currentMillis = millis();
  if (currentMillis - lastStatsUpdate > STATS_UPDATE_INTERVAL) {
    lastStatsUpdate = currentMillis;
    
    if (sniffer_active && is_sniffer_running()) {
      // Get current channel
      int currentChannel = Channel::getChannel();
      
      // Get channel hopping stats
      uint32_t successful_hops = get_successful_channel_hops();
      uint32_t failed_hops = get_failed_channel_hops();
      uint32_t hop_interval = get_channel_hop_interval_ms();
      
      // Calculate success rate
      float success_rate = 0;
      if (successful_hops + failed_hops > 0) {
        success_rate = (float)successful_hops / (successful_hops + failed_hops) * 100.0;
      }
      
      // Display stats in serial monitor
      Serial.printf("%s CH:%d | Hop Stats: %d OK, %d Fail (%.1f%%), Interval: %dms\n", 
                   Minigotchi::getMood().getNeutral().c_str(),
                   currentChannel,
                   successful_hops, 
                   failed_hops,
                   success_rate,
                   hop_interval);
      
      // Optional: Display brief stats on screen
      char stats_buf[64];
      snprintf(stats_buf, sizeof(stats_buf), "CH:%d | %.1f%% hop success", 
              currentChannel, success_rate);
      Display::updateDisplay(Minigotchi::getMood().getNeutral(), stats_buf);
    }
  }
  
  delay(100);
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

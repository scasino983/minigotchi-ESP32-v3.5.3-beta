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
 * minigotchi-ESP32.ino: Main Arduino sketch file with setup() and loop()
 */

#include "minigotchi.h"
#include <SD.h>
#include <SD_MMC.h>
#define SD_CS_PIN 5

// Arduino required setup function - runs once at startup
void setup() {
  // Initialize Serial
  Serial.begin(115200);
  
  // Boot up the Minigotchi
  Minigotchi::boot();
}

// Arduino required loop function - runs repeatedly
void loop() {
  // Handle channel cycling
  Minigotchi::cycle();
  
  // Detect pwnagotchis
  Minigotchi::detect();
  
  // Advertise presence
  Minigotchi::advertise();
  
  // Show current epoch periodically
  static unsigned long lastEpochUpdate = 0;
  if (millis() - lastEpochUpdate > 30000) { // Every 30 seconds
    Minigotchi::epoch();
    lastEpochUpdate = millis();
  }
  
  // Small delay to prevent CPU hogging
  delay(100);
}

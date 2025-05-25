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

// Arduino required setup function - runs once at startup
void setup() {
  Serial.begin(115200);
  Minigotchi::boot();
}

// Arduino required loop function - runs repeatedly
void loop() {
  Minigotchi::cycle();
  Minigotchi::detect();
  Minigotchi::advertise();
  static unsigned long lastEpochUpdate = 0;
  if (millis() - lastEpochUpdate > 30000) {
    Minigotchi::epoch();
    lastEpochUpdate = millis();
  }
  delay(100);
}

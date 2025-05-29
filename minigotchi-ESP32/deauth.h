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
 * deauth.h: header files for deauth.cpp
 */

#ifndef DEAUTH_H
#define DEAUTH_H

// Necessary includes that deauth.cpp will need, formerly in deauth.h
// config.h, minigotchi.h, parasite.h were removed from here to break cycles.
// They must be in deauth.cpp.

#include <Arduino.h> // For String, delay etc.
#include <WiFi.h>    // For WiFi related functions
#include <esp_wifi.h>  // For esp_wifi_80211_tx etc.
#include <string>
#include <vector>
#include <sstream>   // For std::stringstream in Deauth::add
#include <algorithm> // For std::find, std::fill, std::copy

#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // For TaskHandle_t

// Forward declaration
class Mood;
// class Config; // Not needed by Deauth class declaration
// class Minigotchi; // Not needed by Deauth class declaration
// class Parasite; // Not needed by Deauth class declaration

class Deauth {
public:
  // Public static methods
  static void deauth();
  static void list();
  static void add(const std::string &bssids);
  static void stop();
  static bool is_running();
  static void start(); // Made public for deauth_task_runner

  // Public static members (data related to deauth frames)
  static uint8_t deauthTemp[26];
  static uint8_t deauthFrame[26];
  static uint8_t disassociateFrame[26];
  static uint8_t broadcastAddr[6];
  static int randomIndex; // Index of the selected AP from WiFi scan

  // Public static task handle
  static TaskHandle_t deauth_task_handle;

private:
  // Private static helper methods
  static bool send(uint8_t *buf, uint16_t len, bool sys_seq);
  static bool broadcast(uint8_t *mac);
  static void printMac(uint8_t *mac);
  static String printMacStr(uint8_t *mac);
  static bool select();
  // static void start(); // Moved to public

  // Private static members
  // static Mood &mood; // REMOVED - use Mood::getInstance() directly in .cpp
  static uint8_t bssid[6]; // Should this be here or in .cpp? If only used in .cpp, move it. For now, keep if it was like this.
  static bool deauth_should_stop; 
  static std::vector<String> whitelist;
  static String randomAP;
};

#endif // DEAUTH_H

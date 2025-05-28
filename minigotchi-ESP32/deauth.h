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

#include "config.h"
#include "minigotchi.h"
#include "parasite.h"
#include <Arduino.h>
#include <WiFi.h>
#include <algorithm>
#include <esp_wifi.h>
#include <sstream>
#include <string>
#include <vector>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // For TaskHandle_t

// forward declaration of mood class
class Mood;

class Deauth {
public:
  static void deauth();
  static void list();
  static void add(const std::string &bssids);
  static void stop(); // New stop method
  static bool is_running(); // New is_running method
  static uint8_t deauthTemp[26];
  static uint8_t deauthFrame[26];  static uint8_t disassociateFrame[26];
  static uint8_t broadcastAddr[6];
  static int randomIndex;
  static TaskHandle_t deauth_task_handle; // Changed to static member inside class
  static Mood &mood; // Moved to public for access from task function
  static void start(); // Moved to public for access from task function
  static String randomAP; // Moved to public for access from task function
  static bool deauth_should_stop; // Moved to public for access from task function
  static std::vector<String> whitelist; // Moved to public for access from task function

private:
  static bool send(uint8_t *buf, uint16_t len, bool sys_seq);
  static bool broadcast(uint8_t *mac);
  static void printMac(uint8_t *mac);
  static String printMacStr(uint8_t *mac);
  static bool select();
  static uint8_t bssid[6];
  // static bool running; // Ensure this is fully removed
};

#endif // DEAUTH_H

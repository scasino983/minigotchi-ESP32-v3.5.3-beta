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
 * minigotchi.h: header files for minigotchi.cpp
 */

#ifndef MINIGOTCHI_H
#define MINIGOTCHI_H

#include "AXP192.h"
// #include "ble.h" // BLE functionality removed
#include "channel.h"
#include "config.h"
#include "deauth.h"
#include "display.h"
#include "frame.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "mood.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "parasite.h"
#include "pwnagotchi.h"
#include "webui.h"
#include <Arduino.h>
#include <WiFi.h>
#include <esp_wifi.h>

class WebUI;

class Minigotchi {
public:
  static void boot();
  static void finish();
  static void info();
  static void version();
  static void mem();
  static void cpu();
  static bool monStart();
  static void monStop();
  static void cycle();
  static void detect();
  static void deauth();
  static void advertise();
  // static void spam(); // BLE functionality removed
  static void displaySecurityEvaluation(); // New function for security stats
  static void epoch();
  static int addEpoch();
  static void loadConfig();
  static void saveConfig();
  static int currentEpoch;
  static Mood& getMood();

private:
  static void WebUITask(void *pvParameters);
  static void waitForInput();
  // static Mood &mood; // Removed
  static WebUI *web;
};

#endif // MINIGOTCHI_H

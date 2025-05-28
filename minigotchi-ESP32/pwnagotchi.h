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
 * pwnagotchi.h: header files for pwnagotchi.cpp
 */

#ifndef PWNAGOTCHI_H
#define PWNAGOTCHI_H

#include "config.h"
#include "frame.h"
#include "minigotchi.h"
#include "parasite.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_wifi_types.h>
#include <stdint.h>
#include <string>

// FreeRTOS includes
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// forward declaration of mood class
class Mood;

class Pwnagotchi {
public:
  static void detect();
  static void pwnagotchiCallback(void *buf, wifi_promiscuous_pkt_type_t type);
  // static void stopCallback(); // Old name
  static void stop_scan();      // New name
  static bool is_scanning();    // New method
  static TaskHandle_t pwnagotchi_scan_task_handle; // Added task handle as static member
  static Mood &mood; // Moved to public for access from task function
  static bool pwnagotchiDetected; // Moved to public for access from task function
  static std::string essid; // Moved to public for access from task function

private:
  static std::string extractMAC(const unsigned char *buff);
  static void getMAC(char *addr, const unsigned char *buff, int offset);

  // source:
  // https://github.com/justcallmekoko/ESP32Marauder/blob/c0554b95ceb379d29b9a8925d27cc2c0377764a9/esp32_marauder/WiFiScan.h#L213
  typedef struct {
    int16_t fctl;
    int16_t duration;
    uint8_t da;
    uint8_t sa;
    uint8_t bssid;
    int16_t seqctl;
    unsigned char payload[];
  } __attribute__((packed)) WifiMgmtHdr;

  typedef struct {
    uint8_t payload[0];
    WifiMgmtHdr hdr;
  } wifi_ieee80211_packet_t;
};

#endif // PWNAGOTCHI_H

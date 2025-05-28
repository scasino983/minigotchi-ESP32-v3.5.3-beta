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
 * pwnagotchi.cpp: sniffs for pwnagotchi beacon frames
 * source: https://github.com/justcallmekoko/ESP32Marauder
 */

#include "pwnagotchi.h"
#include "wifi_manager.h" // Include the WiFi Manager
#include "config.h"       // For Config::shortDelay, Config::longDelay
#include "parasite.h"     // For Parasite::sendPwnagotchiStatus

// Static member definitions
TaskHandle_t Pwnagotchi::pwnagotchi_scan_task_handle = NULL;
static portMUX_TYPE pwnagotchi_mutex = portMUX_INITIALIZER_UNLOCKED;
static volatile bool pwnagotchi_should_stop_scan = false;

// Forward declaration for the task runner if needed, or define before use.
void pwnagotchi_scan_task_runner(void *pvParameters);


/** developer note:
 *
 * essentially the pwnagotchi sends out a frame(with JSON) while associated to a
 * network if the minigotchi listens for a while it should find something this
 * is under the assumption that we put the minigotchi on the same channel as the
 * pwnagotchi or one of the channels that the pwnagotchi listens on the JSON
 * frame it sends out should have some magic id attached to it (numbers 222-226)
 * so it is identified by pwngrid however we don't need to search for such
 * things
 *
 */

// start off false
bool Pwnagotchi::pwnagotchiDetected = false;
std::string Pwnagotchi::essid = ""; // Definition for static member

/**
 * Gets first instance of mood class
 */
Mood &Pwnagotchi::mood = Mood::getInstance();

/**
 * Get's the mac based on source address
 * @param addr Address to use
 * @param buff Buffer to use
 * @param offset Data offset
 */
void Pwnagotchi::getMAC(char *addr, const unsigned char *buff, int offset) {
  snprintf(addr, 18, "%02x:%02x:%02x:%02x:%02x:%02x", buff[offset],
           buff[offset + 1], buff[offset + 2], buff[offset + 3],
           buff[offset + 4], buff[offset + 5]);
}

/**
 * Extract Mac Address using getMac()
 * @param buff Buffer to use
 */
std::string Pwnagotchi::extractMAC(const unsigned char *buff) {
  char addr[] = "00:00:00:00:00:00";
  getMAC(addr, buff, 10);
  return std::string(addr);
}

/**
 * Detect a Pwnagotchi
 */
void Pwnagotchi::detect() {
    if (!Config::scan) {
        Serial.println(mood.getNeutral() + " Pwnagotchi::detect - Scan disabled in config.");
        return;
    }

    portENTER_CRITICAL(&pwnagotchi_mutex);
    if (pwnagotchi_scan_task_handle != NULL) {
        portEXIT_CRITICAL(&pwnagotchi_mutex);
        Serial.println(mood.getNeutral() + " Pwnagotchi scan is already in progress.");
        Display::updateDisplay(mood.getNeutral(), "Pwn scan active");
        return;
    }
    // No existing task, so we can proceed to create one.
    // The task handle will be set by xTaskCreatePinnedToCore, so no need to set it here yet.
    portEXIT_CRITICAL(&pwnagotchi_mutex);

    pwnagotchi_should_stop_scan = false; // Reset stop flag

    BaseType_t result = xTaskCreatePinnedToCore(
        pwnagotchi_scan_task_runner,
        "pwn_scan_task",    // Task name
        4096,               // Stack size (adjust as needed)
        NULL,               // Parameters
        1,                  // Priority
        &pwnagotchi_scan_task_handle, // Task handle
        0                   // Core (usually 0, Arduino loop on 1)
    );

    if (result == pdPASS && pwnagotchi_scan_task_handle != NULL) {
        Serial.println(mood.getIntense() + " Pwnagotchi scan task created successfully.");
    } else {
        Serial.println(mood.getBroken() + " FAILED to create Pwnagotchi scan task.");
        // Ensure task handle is NULL if creation failed.
        portENTER_CRITICAL(&pwnagotchi_mutex);
        pwnagotchi_scan_task_handle = NULL; 
        portEXIT_CRITICAL(&pwnagotchi_mutex);
    }
}


/**
 * Stops Pwnagotchi scan
 */
void Pwnagotchi::stop_scan() { // Renamed from stopCallback
    Serial.println(mood.getNeutral() + " Pwnagotchi::stop_scan - Received stop request.");
    portENTER_CRITICAL(&pwnagotchi_mutex);
    if (pwnagotchi_scan_task_handle == NULL) {
        portEXIT_CRITICAL(&pwnagotchi_mutex);
        Serial.println(mood.getNeutral() + " Pwnagotchi::stop_scan - No scan task seems to be running.");
        pwnagotchi_should_stop_scan = false; // Reset if no task, though task also resets on start
        return;
    }
    portEXIT_CRITICAL(&pwnagotchi_mutex);
    pwnagotchi_should_stop_scan = true;
    // The task will see this flag and terminate itself.
    // Promiscuous mode and WiFi control release are handled by the task itself.
}

bool Pwnagotchi::is_scanning() {
    portENTER_CRITICAL(&pwnagotchi_mutex);
    bool running = (pwnagotchi_scan_task_handle != NULL);
    portEXIT_CRITICAL(&pwnagotchi_mutex);
    return running;
}


// Task runner function
void pwnagotchi_scan_task_runner(void *pvParameters) {
    Pwnagotchi::pwnagotchiDetected = false; // Reset detection flag for this scan session

    Serial.println(Pwnagotchi::mood.getNeutral() + " Pwnagotchi scan task started.");

    if (!WifiManager::getInstance().request_monitor_mode("pwnagotchi_scan_task")) {
        Serial.println(Pwnagotchi::mood.getBroken() + " Pwnagotchi Task: Failed to acquire monitor mode.");
        portENTER_CRITICAL(&pwnagotchi_mutex);
        Pwnagotchi::pwnagotchi_scan_task_handle = NULL;
        portEXIT_CRITICAL(&pwnagotchi_mutex);
        pwnagotchi_should_stop_scan = false; // Reset flag
        vTaskDelete(NULL);
        return;
    }
    Serial.println(Pwnagotchi::mood.getNeutral() + " Pwnagotchi Task: Monitor mode acquired.");
    esp_wifi_set_promiscuous_rx_cb(Pwnagotchi::pwnagotchiCallback);

    // Animation part
    for (int i = 0; i < 5 && !pwnagotchi_should_stop_scan; ++i) {
        Serial.println(Pwnagotchi::mood.getLooking1() + " Scanning for Pwnagotchi.");
        Display::updateDisplay(Pwnagotchi::mood.getLooking1(), "Scanning for Pwnagotchi.");
        vTaskDelay(pdMS_TO_TICKS(Config::shortDelay)); 
        if (pwnagotchi_should_stop_scan) break;

        Serial.println(Pwnagotchi::mood.getLooking2() + " Scanning for Pwnagotchi..");
        Display::updateDisplay(Pwnagotchi::mood.getLooking2(), "Scanning for Pwnagotchi..");
        vTaskDelay(pdMS_TO_TICKS(Config::shortDelay));
        if (pwnagotchi_should_stop_scan) break;
        
        Serial.println(Pwnagotchi::mood.getLooking1() + " Scanning for Pwnagotchi...");
        Display::updateDisplay(Pwnagotchi::mood.getLooking1(), "Scanning for Pwnagotchi...");
        vTaskDelay(pdMS_TO_TICKS(Config::shortDelay));
        if (pwnagotchi_should_stop_scan) break;

        Serial.println(" ");
        vTaskDelay(pdMS_TO_TICKS(Config::shortDelay));
    }

    // Main scanning duration loop, checking stop flag
    TickType_t scan_start_time = xTaskGetTickCount();
    TickType_t scan_duration_ticks = pdMS_TO_TICKS(Config::longDelay); 
    
    if (!pwnagotchi_should_stop_scan) { // Only enter timed scan if not already stopped by animation loop
        while (!pwnagotchi_should_stop_scan && (xTaskGetTickCount() - scan_start_time < scan_duration_ticks)) {
            vTaskDelay(pdMS_TO_TICKS(100)); // Check stop flag every 100ms
        }
    }

    if (pwnagotchi_should_stop_scan) {
        Serial.println(Pwnagotchi::mood.getNeutral() + " Pwnagotchi Task: Scan stopped by request.");
    } else {
        Serial.println(Pwnagotchi::mood.getNeutral() + " Pwnagotchi Task: Scan duration complete.");
    }
    
    // Stop promiscuous mode and release WiFi resources
    esp_wifi_set_promiscuous_rx_cb(nullptr);
    WifiManager::getInstance().release_wifi_control("pwnagotchi_scan_task");
    Serial.println(Pwnagotchi::mood.getNeutral() + " Pwnagotchi Task: Promiscuous mode stopped, WiFi control released.");

    // Report findings
    if (!Pwnagotchi::pwnagotchiDetected && !pwnagotchi_should_stop_scan) { // Only report "not found" if scan completed fully
        Serial.println(Pwnagotchi::mood.getSad() + " No Pwnagotchi found during scan task.");
        Display::updateDisplay(Pwnagotchi::mood.getSad(), "No Pwnagotchi found.");
        Parasite::sendPwnagotchiStatus(NO_FRIEND_FOUND);
    } else if (Pwnagotchi::pwnagotchiDetected) {
        // Messages for found Pwnagotchi are handled by the callback.
        // This can be a final summary or removed if redundant.
        Serial.println(Pwnagotchi::mood.getHappy() + " Pwnagotchi detection process complete (details in callback).");
    }
    // If stopped early and something was detected, callback would have handled it.
    
    // Task cleanup
    portENTER_CRITICAL(&pwnagotchi_mutex);
    Pwnagotchi::pwnagotchi_scan_task_handle = NULL;
    portEXIT_CRITICAL(&pwnagotchi_mutex);
    pwnagotchi_should_stop_scan = false; // Reset flag for next run

    Serial.println(Pwnagotchi::mood.getNeutral() + " Pwnagotchi scan task finished.");
    vTaskDelete(NULL); // Deletes the current task
}


/**
 * Pwnagotchi Scanning callback
 * Source:
 * https://github.com/justcallmekoko/ESP32Marauder/blob/master/esp32_marauder/WiFiScan.cpp#L2439
 * @param buf Packet recieved to use as a buffer
 * @param len Length of the buffer
 */
void Pwnagotchi::pwnagotchiCallback(void *buf,
                                    wifi_promiscuous_pkt_type_t type) {
  wifi_promiscuous_pkt_t *snifferPacket = (wifi_promiscuous_pkt_t *)buf;
  WifiMgmtHdr *frameControl = (WifiMgmtHdr *)snifferPacket->payload;
  wifi_pkt_rx_ctrl_t ctrl = (wifi_pkt_rx_ctrl_t)snifferPacket->rx_ctrl;
  int len = snifferPacket->rx_ctrl.sig_len;

  // start off false
  pwnagotchiDetected = false;

  if (type == WIFI_PKT_MGMT) {
    len -= 4;
    int fctl = ntohs(frameControl->fctl);
    const wifi_ieee80211_packet_t *ipkt =
        (wifi_ieee80211_packet_t *)snifferPacket->payload;
    const WifiMgmtHdr *hdr = &ipkt->hdr;

    // check if it is a beacon frame
    if (snifferPacket->payload[0] == 0x80) {
      // extract mac
      char addr[] = "00:00:00:00:00:00";
      getMAC(addr, snifferPacket->payload, 10);
      String src = addr;
      // Serial.println("'" + src + "'");

      // check if the source MAC matches the target
      if (src == "de:ad:be:ef:de:ad") {
        pwnagotchiDetected = true;
        Serial.println(mood.getHappy() + " Pwnagotchi detected!");
        Serial.println(" ");
        Display::updateDisplay(mood.getHappy(), "Pwnagotchi detected!");
        // delay(Config::shortDelay);

        // extract the ESSID from the beacon frame
        String essid = "";

        // "borrowed" from ESP32 Marauder
        for (int i = 38; i < len; i++) {
          if (isAscii(snifferPacket->payload[i])) {
            essid.concat((char)snifferPacket->payload[i]);
          } else {
            essid.concat("?");
          }
        }

        // give it a sec
        // delay(Config::shortDelay);

        // network related info
        Serial.print(mood.getHappy() + " RSSI: ");
        Serial.println(snifferPacket->rx_ctrl.rssi);
        Serial.print(mood.getHappy() + " Channel: ");
        Serial.println(snifferPacket->rx_ctrl.channel);
        Serial.print(mood.getHappy() + " BSSID: ");
        Serial.println(addr);
        Serial.print(mood.getHappy() + " ESSID: ");
        Serial.println(essid);
        Serial.println(" ");

        // parse the ESSID as JSON
        DynamicJsonDocument jsonBuffer(2048);
        DeserializationError error = deserializeJson(jsonBuffer, essid);
        // delay(Config::shortDelay);

        // check if json parsing is successful
        if (error) {
          Serial.println(mood.getBroken() +
                         " Could not parse Pwnagotchi json: ");
          Serial.print(mood.getBroken() + " ");
          Serial.println(error.c_str());
          Display::updateDisplay(mood.getBroken(),
                                 "Could not parse Pwnagotchi json: " +
                                     (String)error.c_str());
          Serial.println(" ");
        } else {
          Serial.println(mood.getHappy() + " Successfully parsed json!");
          Serial.println(" ");
          Display::updateDisplay(mood.getHappy(), "Successfully parsed json!");
          // find minigotchi/palnagotchi
          bool pal = jsonBuffer["pal"].as<bool>();
          bool minigotchi = jsonBuffer["minigotchi"].as<bool>();

          // find out some stats
          String name = jsonBuffer["name"].as<String>();
          delay(Config::shortDelay);
          String pwndTot = jsonBuffer["pwnd_tot"].as<String>();
          delay(Config::shortDelay);

          if (name == "null") {
            name = "N/A";
          }

          if (pwndTot == "null") {
            pwndTot = "N/A";
          }

          String deviceType = "";

          // minigotchi or palnagotchi stuff
          if (minigotchi || pal) {
            if (minigotchi) {
              deviceType = "Minigotchi";
            }

            if (pal) {
              deviceType = "Palnagotchi";
            }

            // show corresponding type
            Serial.print(mood.getHappy() + " " + deviceType + " name: ");
            Serial.println(name);
            Serial.print(mood.getHappy() + " Pwned Networks: ");
            Serial.println(pwndTot);
            Serial.print(" ");
            Display::updateDisplay(mood.getHappy(),
                                   deviceType + " name: " + (String)name);
            delay(Config::shortDelay);
            Display::updateDisplay(mood.getHappy(),
                                   "Pwned Networks: " + (String)pwndTot);
            // reset
            deviceType = "";
          } else {
            // this should be a pwnagotchi
            Serial.print(mood.getHappy() + " Pwnagotchi name: ");
            Serial.println(name);
            Serial.print(mood.getHappy() + " Pwned Networks: ");
            Serial.println(pwndTot);
            Serial.print(" ");
            Display::updateDisplay(mood.getHappy(),
                                   "Pwnagotchi name: " + (String)name);
            delay(Config::shortDelay);
            Display::updateDisplay(mood.getHappy(),
                                   "Pwned Networks: " + (String)pwndTot);
          }

          // clear json buffer
          jsonBuffer.clear();

          delay(Config::shortDelay);
          Parasite::sendPwnagotchiStatus(FRIEND_FOUND, name.c_str());
        }
      }
    }
  }
}

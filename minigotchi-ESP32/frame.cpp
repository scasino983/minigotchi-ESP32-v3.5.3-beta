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
 * frame.cpp: handles the sending of "pwnagotchi" beacon frames
 */

#include "frame.h"
#include "wifi_interface.h"   // For WiFi interface functions
#include "channel_hopper.h"   // For stop_channel_hopping
#include "wifi_sniffer.h"     // For is_sniffer_running
#include "wifi_sniffer.h"
#include "channel_hopper.h"
#include "minigotchi.h"
#include "pwnagotchi.h"
#include "deauth.h"

// Channel hopper helpers
extern bool is_channel_hopping();
extern TaskHandle_t get_channel_hopper_task_handle();
// Pwnagotchi scan
extern void pwnagotchi_scan_stop();
// Deauth helpers
extern bool is_deauth_running();
extern void deauth_stop();

// Forward declaration for robust WiFi/FreeRTOS cleanup helper
static void stop_all_wifi_tasks_and_cleanup();

/** developer note:
 *
 * when it comes to detecting a pwnagotchi, this is done with pwngrid/opwngrid.
 * essentially pwngrid looks for the numbers 222-226 in payloads, and if they
 * aren't there, it ignores it. these need to be put into the frames!!!
 *
 * note that these frames aren't just normal beacon frames, rather a modified
 * one with data, additional ids, etc. frames are dynamically constructed,
 * headers are included like a normal frame. by far this is the most memory
 * heaviest part of the minigotchi, the reason is
 *
 */

// initializing
size_t Frame::payloadSize = 255; // by default
const size_t Frame::chunkSize = 0xFF;

// beacon stuff
size_t Frame::essidLength = 0;
uint8_t Frame::headerLength = 0;

// payload ID's according to pwngrid
const uint8_t Frame::IDWhisperPayload = 0xDE;
const uint8_t Frame::IDWhisperCompression = 0xDF;
const uint8_t Frame::IDWhisperIdentity = 0xE0;
const uint8_t Frame::IDWhisperSignature = 0xE1;
const uint8_t Frame::IDWhisperStreamHeader = 0xE2;

// other addresses
const uint8_t Frame::SignatureAddr[] = {0xde, 0xad, 0xbe, 0xef, 0xde, 0xad};
const uint8_t Frame::BroadcastAddr[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
const uint16_t Frame::wpaFlags = 0x0411;

/**
 * Gets first instance of mood class
 */
Mood &Frame::mood = Mood::getInstance();

// Don't even dare restyle!
const uint8_t Frame::header[]{
    0x80, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xde, 0xad,
    0xbe, 0xef, 0xde, 0xad, 0xde, 0xad, 0xbe, 0xef, 0xde, 0xad, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x64, 0x00, 0x11, 0x04,
};

// get header length
const int Frame::pwngridHeaderLength = sizeof(Frame::header);

/** developer note:
 *
 * frame structure based on how it was built here
 *
 * 1. header
 * 2. payload id's
 * 3. (chunked) pwnagotchi data
 *
 */

/** developer note:
 *
 * referenced the following for packing-related function:
 *
 * https://github.com/evilsocket/pwngrid/blob/master/wifi/pack.go
 *
 */

/**
 * Replicates pwngrid's pack() function from pack.go
 * https://github.com/evilsocket/pwngrid/blob/master/wifi/pack.go
 */
uint8_t *Frame::pack() {
  // make a json doc
  String jsonString = "";
  DynamicJsonDocument doc(2048);

  doc["epoch"] = Config::epoch;
  doc["face"] = Config::face;
  doc["identity"] = Config::identity;
  doc["name"] = Config::name;

  doc["policy"]["advertise"] = Config::advertise;
  doc["policy"]["ap_ttl"] = Config::ap_ttl;
  doc["policy"]["associate"] = Config::associate;
  doc["policy"]["bored_num_epochs"] = Config::bored_num_epochs;

  doc["policy"]["deauth"] = Config::deauth;
  doc["policy"]["excited_num_epochs"] = Config::excited_num_epochs;
  doc["policy"]["hop_recon_time"] = Config::hop_recon_time;
  doc["policy"]["max_inactive_scale"] = Config::max_inactive_scale;
  doc["policy"]["max_interactions"] = Config::max_interactions;
  doc["policy"]["max_misses_for_recon"] = Config::max_misses_for_recon;
  doc["policy"]["min_recon_time"] = Config::min_rssi;
  doc["policy"]["min_rssi"] = Config::min_rssi;
  doc["policy"]["recon_inactive_multiplier"] =
      Config::recon_inactive_multiplier;
  doc["policy"]["recon_time"] = Config::recon_time;
  doc["policy"]["sad_num_epochs"] = Config::sad_num_epochs;
  doc["policy"]["sta_ttl"] = Config::sta_ttl;

  doc["pwnd_run"] = Config::pwnd_run;
  doc["pwnd_tot"] = Config::pwnd_tot;
  doc["session_id"] = Config::session_id;
  doc["uptime"] = Config::uptime;
  doc["version"] = Config::version;

  // serialize then put into beacon frame
  serializeJson(doc, jsonString);
  Frame::essidLength = measureJson(doc);
  Frame::headerLength = 2 + ((uint8_t)(essidLength / 255) * 2);
  uint8_t *beaconFrame = new uint8_t[Frame::pwngridHeaderLength +
                                     Frame::essidLength + Frame::headerLength];
  memcpy(beaconFrame, Frame::header, Frame::pwngridHeaderLength);

  /** developer note:
   *
   * if you literally want to check the json everytime you send a packet(non
   * serialized ofc)
   *
   * Serial.println(jsonString);
   */

  int frameByte = pwngridHeaderLength;
  for (int i = 0; i < essidLength; i++) {
    if (i == 0 || i % 255 == 0) {
      beaconFrame[frameByte++] = Frame::IDWhisperPayload;
      uint8_t newPayloadLength = 255;
      if (essidLength - i < Frame::chunkSize) {
        newPayloadLength = essidLength - i;
      }
      beaconFrame[frameByte++] = newPayloadLength;
    }

    uint8_t nextByte = (uint8_t)'?';
    if (isAscii(jsonString[i])) {
      nextByte = (uint8_t)jsonString[i];
    }

    beaconFrame[frameByte++] = nextByte;
  }

  /* developer note: we can print the beacon frame like so...

  Serial.println("('-') Full Beacon Frame:");
  for (size_t i = 0; i < frameSize; ++i) {
    Serial.print(beaconFrame[i], HEX);
    Serial.print(" ");
  }

  Serial.println(" ");

  */

  return beaconFrame;
}

/**
 * Send a modified pwnagotchi packet,
 * except add minigotchi info as well.
 */
uint8_t *Frame::packModified() {
  // make a json doc
  String jsonString = "";
  DynamicJsonDocument doc(2048);

  doc["minigotchi"] = true;
  doc["epoch"] = Config::epoch;
  doc["face"] = Config::face;
  doc["identity"] = Config::identity;
  doc["name"] = Config::name;

  doc["policy"]["advertise"] = Config::advertise;
  doc["policy"]["ap_ttl"] = Config::ap_ttl;
  doc["policy"]["associate"] = Config::associate;
  doc["policy"]["bored_num_epochs"] = Config::bored_num_epochs;

  doc["policy"]["deauth"] = Config::deauth;
  doc["policy"]["excited_num_epochs"] = Config::excited_num_epochs;
  doc["policy"]["hop_recon_time"] = Config::hop_recon_time;
  doc["policy"]["max_inactive_scale"] = Config::max_inactive_scale;
  doc["policy"]["max_interactions"] = Config::max_interactions;
  doc["policy"]["max_misses_for_recon"] = Config::max_misses_for_recon;
  doc["policy"]["min_recon_time"] = Config::min_rssi;
  doc["policy"]["min_rssi"] = Config::min_rssi;
  doc["policy"]["recon_inactive_multiplier"] =
      Config::recon_inactive_multiplier;
  doc["policy"]["recon_time"] = Config::recon_time;
  doc["policy"]["sad_num_epochs"] = Config::sad_num_epochs;
  doc["policy"]["sta_ttl"] = Config::sta_ttl;

  doc["pwnd_run"] = Config::pwnd_run;
  doc["pwnd_tot"] = Config::pwnd_tot;
  doc["session_id"] = Config::session_id;
  doc["uptime"] = Config::uptime;
  doc["version"] = Config::version;

  // serialize then put into beacon frame
  serializeJson(doc, jsonString);
  Frame::essidLength = measureJson(doc);
  Frame::headerLength = 2 + ((uint8_t)(essidLength / 255) * 2);
  uint8_t *beaconFrame = new uint8_t[Frame::pwngridHeaderLength +
                                     Frame::essidLength + Frame::headerLength];
  memcpy(beaconFrame, Frame::header, Frame::pwngridHeaderLength);

  int frameByte = pwngridHeaderLength;
  for (int i = 0; i < essidLength; i++) {
    if (i == 0 || i % 255 == 0) {
      beaconFrame[frameByte++] = Frame::IDWhisperPayload;
      uint8_t newPayloadLength = 255;
      if (essidLength - i < Frame::chunkSize) {
        newPayloadLength = essidLength - i;
      }
      beaconFrame[frameByte++] = newPayloadLength;
    }
    uint8_t nextByte = (uint8_t)'?';
    if (isAscii(jsonString[i])) {
      nextByte = (uint8_t)jsonString[i];
    }

    beaconFrame[frameByte++] = nextByte;
  }

  return beaconFrame;
}

// Helper to ensure WiFi is initialized and started
static bool ensure_wifi_initialized() {
    wifi_mode_t mode;
    esp_err_t err = esp_wifi_get_mode(&mode);
    if (err == ESP_ERR_WIFI_NOT_INIT) {
        wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
        if (esp_wifi_init(&cfg) != ESP_OK) return false;
        if (esp_wifi_start() != ESP_OK) return false;
        return true;
    }
    return (err == ESP_OK);
}

// Robust WiFi mode switch helper (maximal recovery)
static bool reset_and_set_wifi_mode(wifi_mode_t mode) {
    // Try to stop WiFi until it is stopped or not initialized
    for (int i = 0; i < 5; ++i) {
        esp_err_t stop_err = esp_wifi_stop();
        if (stop_err == ESP_OK || stop_err == ESP_ERR_WIFI_NOT_INIT) break;
        Serial.printf("[WiFi] esp_wifi_stop() failed: %s\n", esp_err_to_name(stop_err));
        delay(100);
    }
    delay(100);
    // Try to deinit WiFi until it is deinitialized or not initialized
    for (int i = 0; i < 5; ++i) {
        esp_err_t deinit_err = esp_wifi_deinit();
        if (deinit_err == ESP_OK || deinit_err == ESP_ERR_WIFI_NOT_INIT) break;
        Serial.printf("[WiFi] esp_wifi_deinit() failed: %s\n", esp_err_to_name(deinit_err));
        delay(100);
    }
    delay(150);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t init_err = esp_wifi_init(&cfg);
    if (init_err != ESP_OK) {
        Serial.printf("[WiFi] esp_wifi_init() failed: %s\n", esp_err_to_name(init_err));
        return false;
    }
    delay(50);
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK) {
        Serial.printf("[WiFi] esp_wifi_start() failed: %s\n", esp_err_to_name(start_err));
        return false;
    }
    delay(100);
    esp_err_t mode_err = esp_wifi_set_mode(mode);
    if (mode_err != ESP_OK) {
        Serial.printf("[WiFi] esp_wifi_set_mode(%d) failed: %s\n", (int)mode, esp_err_to_name(mode_err));
        return false;
    }
    delay(100);
    return true;
}

/**
 * Sends a pwnagotchi packet in AP mode with improved stability
 */
bool Frame::send() {
  Serial.printf("Frame::send() - Entry. Free heap: %d\n", ESP.getFreeHeap());
  stop_all_wifi_tasks_and_cleanup();
  
  // First, check if WiFi is initialized
  wifi_mode_t currentMode;
  esp_err_t check_err = esp_wifi_get_mode(&currentMode);
  
  // If WiFi is not initialized, initialize it with better error handling
  if (check_err == ESP_ERR_WIFI_NOT_INIT) {
    Serial.println(mood.getIntense() + " WiFi not initialized, initializing now...");
    
    // Initialize WiFi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t init_err = esp_wifi_init(&cfg);
    if (init_err != ESP_OK) {
      Serial.printf("%s Failed to initialize WiFi: %s\n", 
                    mood.getBroken().c_str(), esp_err_to_name(init_err));
      return false;
    }
    
    // Start WiFi
    esp_err_t start_err = esp_wifi_start();
    if (start_err != ESP_OK) {
      Serial.printf("%s Failed to start WiFi: %s\n", 
                    mood.getBroken().c_str(), esp_err_to_name(start_err));
      return false;
    }
    
    // Set to STA mode initially (this helps with stability)
    esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (mode_err != ESP_OK) {
      Serial.printf("%s Failed to set initial WiFi mode: %s\n", 
                    mood.getBroken().c_str(), esp_err_to_name(mode_err));
      return false;
    }
    
    delay(150); // Give WiFi more time to initialize
    
    // Re-check mode after initialization
    check_err = esp_wifi_get_mode(&currentMode);
    if (check_err != ESP_OK) {
      Serial.printf("%s Failed to get WiFi mode after init: %s\n", 
                    mood.getBroken().c_str(), esp_err_to_name(check_err));
      return false;
    }
  } else if (check_err != ESP_OK) {
    // Some other error occurred checking the WiFi mode
    Serial.printf("%s Error checking WiFi mode: %s\n", 
                  mood.getBroken().c_str(), esp_err_to_name(check_err));
    return false;
  }
  
  // Save current WiFi state before changing it
  wifi_mode_t previousMode = currentMode; // Already got this above
  
  bool wasPromiscuous = false;
  esp_err_t promisc_err = esp_wifi_get_promiscuous(&wasPromiscuous);
  if (promisc_err != ESP_OK) {
    Serial.printf("%s Error checking promiscuous mode: %s\n", 
                  mood.getSad().c_str(), esp_err_to_name(promisc_err));
    // Continue anyway, assuming not promiscuous
    wasPromiscuous = false;
  }
  
  // If we were in promiscuous mode, turn it off properly
  if (wasPromiscuous) {
    esp_wifi_set_promiscuous_rx_cb(NULL); // Clear callback first
    esp_err_t promisc_off_err = esp_wifi_set_promiscuous(false);
    if (promisc_off_err != ESP_OK) {
      Serial.printf("%s Error disabling promiscuous mode: %s\n", 
                    mood.getSad().c_str(), esp_err_to_name(promisc_off_err));
      // Continue anyway
    }
    delay(100);  // Give WiFi state time to settle
  }
  // Clean up existing connections if any
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  delay(75); // A bit more time for disconnects

  // Ensure WiFi is initialized before any mode change
  if (!ensure_wifi_initialized()) {
    Serial.println("[Frame::send] Failed to ensure WiFi initialized before mode set!");
    return false;
  }

  // First, verify WiFi is initialized and in a good state
  wifi_mode_t current_mode;
  esp_err_t mode_check_err = esp_wifi_get_mode(&current_mode);
  
  if (mode_check_err != ESP_OK) {
    Serial.printf("%s WiFi not in good state before AP mode: %s\n", 
                mood.getBroken().c_str(), esp_err_to_name(mode_check_err));
    // Full cleanup and reinitialization
    esp_wifi_stop();
    delay(100);
    esp_wifi_deinit();
    delay(200);
    // Reinitialize WiFi with default config
    if (!ensure_wifi_initialized()) {
      Serial.printf("%s Failed to reinitialize WiFi\n", mood.getBroken().c_str());
      return false;
    }
    delay(150);
  }

  // Set to STA mode first as a transitional state with longer delay
  Serial.println(mood.getIntense() + " Setting WiFi to STA mode as transition...");
  if (!reset_and_set_wifi_mode(WIFI_MODE_STA)) {
    Serial.println("[Frame::send] Failed to robustly reset and set WiFi to STA mode!");
    return false;
  }
  // Set to AP mode for frame transmission with retry mechanism
  Serial.println(mood.getIntense() + " Setting WiFi to AP mode for transmission...");
  bool ap_mode_ok = false;
  for (int retry = 0; retry < 3; retry++) {
    if (!reset_and_set_wifi_mode(WIFI_MODE_AP)) {
      Serial.printf("[Frame::send] Failed to robustly reset and set WiFi to AP mode (attempt %d)!\n", retry+1);
      if (retry == 2) return false;
      delay(200 * (retry + 1));
    } else {
      ap_mode_ok = true;
      break;
    }
  }
  if (!ap_mode_ok) return false;
  delay(250);  // Longer delay to ensure AP mode is fully active

  // Create normal frame
  Serial.printf("Frame::send() - About to pack frame. Free heap: %d\n", ESP.getFreeHeap());
  uint8_t *frame = Frame::pack();
  size_t frameSize = 0; // Initialize frameSize
  if (frame != nullptr) {
    frameSize = Frame::pwngridHeaderLength + Frame::essidLength + Frame::headerLength;
    Serial.printf("Frame::send() - Frame packed. Frame size: %d. Free heap: %d\n", frameSize, ESP.getFreeHeap());
  } else {
    Serial.println("Frame::send() - Frame::pack() returned nullptr.");
    // Restore previous WiFi state
    esp_wifi_set_mode(previousMode);
    if (wasPromiscuous) {
      delay(30); // Adjusted delay
      esp_wifi_set_promiscuous(true);
    }
    Serial.printf("Frame::send() - Exit (pack failed). Free heap: %d\n", ESP.getFreeHeap());
    return false;
  }

  // Send frames
  esp_err_t err = ESP_FAIL;
  
  // Only try to send if we have valid memory and frame creation succeeded (already checked)
  // Check heap before sending
  if (ESP.getFreeHeap() < 10000) { // This check seems redundant now with heap checks in pack and before tx, but keeping as a safeguard.
    Serial.println(mood.getBroken() + " Low memory before sending, aborting");
    if (frame != nullptr) {
      delete[] frame;
      frame = nullptr;
    }
    
    // Restore previous WiFi state
    esp_wifi_set_mode(previousMode);
    if (wasPromiscuous) {
      delay(30); // Adjusted delay
      esp_wifi_set_promiscuous(true);
    }
    Serial.printf("Frame::send() - Exit (low memory). Free heap: %d\n", ESP.getFreeHeap());
    return false;
  }
    
  delay(75); // A small delay before transmission - increased for better stability
  Serial.printf("Frame::send() - Attempting to transmit first frame. WiFi Interface: WIFI_IF_AP. Free heap: %d\n", ESP.getFreeHeap());
  err = esp_wifi_80211_tx(WIFI_IF_AP, frame, frameSize, false);
  Serial.printf("Frame::send() - First frame tx result: %s\n", esp_err_to_name(err));
    
  if (err == ESP_OK) {
    if (frame != nullptr) {
      delete[] frame;
      frame = nullptr;  // Prevent use-after-free
    }

    // Try to send the modified frame
    Serial.printf("Frame::send() - About to pack modified frame. Free heap: %d\n", ESP.getFreeHeap());
    frame = Frame::packModified();
    if (frame != nullptr) {
      frameSize = Frame::pwngridHeaderLength + Frame::essidLength + Frame::headerLength;
      Serial.printf("Frame::send() - Modified frame packed. Frame size: %d. Free heap: %d\n", frameSize, ESP.getFreeHeap());
      delay(75);  // Small delay between transmissions - increased for better stability
      Serial.printf("Frame::send() - Attempting to transmit modified frame. WiFi Interface: WIFI_IF_AP. Free heap: %d\n", ESP.getFreeHeap());
      err = esp_wifi_80211_tx(WIFI_IF_AP, frame, frameSize, false);
      Serial.printf("Frame::send() - Modified frame tx result: %s\n", esp_err_to_name(err));
      if (frame != nullptr) {
        delete[] frame;
        frame = nullptr;  // Prevent use-after-free
      }
    } else {
      Serial.println("Frame::send() - Frame::packModified() returned nullptr.");
      err = ESP_FAIL;  // Memory allocation failed
      // frame is already nullptr here
    }
  } else {
    if (frame != nullptr) {
      delete[] frame;  // Clean up on error
      frame = nullptr;
    }
  }

  // Restore previous WiFi state
  delay(30);  // Give WiFi time to finish transmission
  esp_wifi_set_mode(previousMode);
  
  if (wasPromiscuous) {
    delay(30);  // Give WiFi time to change mode
    esp_wifi_set_promiscuous(true);
  }
  Serial.printf("Frame::send() - Exit. Free heap: %d\n", ESP.getFreeHeap());
  return (err == ESP_OK);
}

/**
 * Full usage of Pwnagotchi's advertisments on the Minigotchi.
 */
void Frame::advertise() {
  Serial.printf("Frame::advertise() - Entry. Free heap: %d\n", ESP.getFreeHeap());
  stop_all_wifi_tasks_and_cleanup();
  int packets = 0;
  unsigned long startTime = millis();
  if (!Config::advertise) {
    Serial.println(mood.getNeutral() + " Advertisement disabled in config.");
    Serial.printf("Frame::advertise() - Exit (disabled). Free heap: %d\n", ESP.getFreeHeap());
    return;  // Skip advertisement if disabled
  }
  // Stop the sniffer temporarily if it's running
  bool sniffer_was_running = is_sniffer_running();
  if (sniffer_was_running) {
    Serial.println(mood.getNeutral() + " Stopping sniffer before advertisement...");
    esp_err_t stop_err = wifi_sniffer_stop();
    if (stop_err != ESP_OK) {
      Serial.printf("%s Failed to stop sniffer properly: %s\n", 
                   mood.getBroken().c_str(), esp_err_to_name(stop_err));
      Serial.println(mood.getIntense() + " Performing more thorough WiFi cleanup...");
      esp_wifi_set_promiscuous_rx_cb(NULL);
      esp_wifi_set_promiscuous(false);
    }
    delay(150);  // Give more time for the sniffer to stop properly
  }
  // Ensure we're not in monitor mode
  bool is_promiscuous = false;
  esp_wifi_get_promiscuous(&is_promiscuous);
  if (is_promiscuous) {
    Serial.println(mood.getIntense() + " Still in promiscuous mode, disabling...");
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_set_promiscuous(false);
    delay(100);
  }
  Serial.println(mood.getIntense() + " Starting advertisement...");
  Display::updateDisplay(mood.getIntense(), "Starting advertisement...");
  Parasite::sendAdvertising();
  delay(Config::shortDelay);
  int availableHeap = ESP.getFreeHeap();
  int maxPackets = min(15, availableHeap / 10240);
  maxPackets = max(3, maxPackets);
  Serial.printf("%s Available heap: %d bytes, sending max %d packets\n", 
               mood.getNeutral().c_str(), availableHeap, maxPackets);
  Serial.printf("Frame::advertise() - Starting packet send loop. Max packets: %d. Free heap: %d\n", maxPackets, ESP.getFreeHeap());
  WiFi.disconnect(true);
  delay(100);
  // Ensure WiFi is initialized before any mode/config changes
  if (!ensure_wifi_initialized()) {
    Serial.println("[Frame::advertise] Failed to ensure WiFi initialized before advertisement!");
    Display::updateDisplay(mood.getBroken(), "WiFi init failed!");
    return;
  }
  // First, check if WiFi is in an initialized state
  wifi_mode_t current_mode;
  esp_err_t check_err = esp_wifi_get_mode(&current_mode);
  
  // Only perform complete WiFi reset if we determine it's necessary
  bool need_full_reset = false;
  
  if (check_err == ESP_OK) {
    // WiFi is initialized, check if we're in a clean state
    Serial.println(mood.getIntense() + " WiFi is initialized in mode: " + String(current_mode));
    
    // If we're not in STA mode, or there's a sniffer running, we need a reset
    if (current_mode != WIFI_MODE_STA || sniffer_was_running) {
      need_full_reset = true;
    } else {
      // WiFi is in STA mode and no sniffer, we can just use it directly
      Serial.println(mood.getNeutral() + " WiFi already in clean STA mode, proceeding with advertisement...");
      need_full_reset = false;
    }
  } else if (check_err == ESP_ERR_WIFI_NOT_INIT) {
    // WiFi is not initialized, so we need to initialize it
    Serial.println(mood.getIntense() + " WiFi is not initialized, will initialize for advertisement.");
    need_full_reset = true;
  } else {
    // Some other error, assume we need a reset
    Serial.printf("%s Unexpected WiFi state: %s. Forcing reset.\n", 
                mood.getBroken().c_str(), esp_err_to_name(check_err));
    need_full_reset = true;
  }
  
  // If we need a full reset, do it thoroughly but only once
  if (need_full_reset) {
    Serial.println(mood.getIntense() + " Performing WiFi reset for advertisement...");
    if (!reset_and_set_wifi_mode(WIFI_MODE_STA)) {
      Serial.println("[Frame::advertise] Failed to robustly reset and set WiFi to STA mode!");
      Display::updateDisplay(mood.getBroken(), "WiFi mode set failed!");
      return;
    }
    delay(150);
    if (!reset_and_set_wifi_mode(WIFI_MODE_AP)) {
      Serial.println("[Frame::advertise] Failed to robustly reset and set WiFi to AP mode!");
      Display::updateDisplay(mood.getBroken(), "WiFi mode set failed!");
      return;
    }
    delay(150);
  }
  Serial.printf("Frame::advertise() - About to send %d packets\n", maxPackets);
  for (packets = 0; packets < maxPackets; packets++) {
    Serial.printf("Frame::advertise() - Sending packet %d/%d\n", packets+1, maxPackets);
    if (!Frame::send()) {
      Serial.printf("%s Frame::send() failed during advertisement!\n", mood.getBroken().c_str());
      break;
    }
    delay(Config::shortDelay);
  }
  unsigned long endTime = millis();
  Serial.printf("Frame::advertise() - Sent %d packets in %lu ms. Free heap: %d\n", 
               packets, endTime - startTime, ESP.getFreeHeap());
  Serial.println(mood.getIntense() + " Advertisement complete.");
  Display::updateDisplay(mood.getIntense(), "Advertisement done!");
  delay(500);
  if (sniffer_was_running) {
    delay(300);
    Serial.println(mood.getIntense() + " Setting WiFi to STA mode before restarting sniffer...");
    bool sta_ok = false;
    for (int retry = 0; retry < 3; retry++) {
      if (!reset_and_set_wifi_mode(WIFI_MODE_STA)) {
        Serial.printf("[Frame::advertise] Failed to robustly reset and set WiFi to STA mode (attempt %d)!\n", retry+1);
        if (retry == 2) {
          Display::updateDisplay(mood.getBroken(), "WiFi mode set failed!");
          return;
        }
        delay(200 * (retry + 1));
      } else {
        sta_ok = true;
        break;
      }
    }
    if (!sta_ok) return;
    delay(200);
    Serial.println(mood.getIntense() + " Restarting sniffer...");
    wifi_sniffer_start();
  }
  Serial.printf("Frame::advertise() - Exit. Free heap: %d\n", ESP.getFreeHeap());
}

// Robust global WiFi/FreeRTOS cleanup helper
static void stop_all_wifi_tasks_and_cleanup() {
    Serial.println("[WiFi Cleanup] Stopping all WiFi-related tasks and callbacks...");
    // Stop sniffer if running
    if (is_sniffer_running()) {
        Serial.println("[WiFi Cleanup] Stopping sniffer...");
        wifi_sniffer_stop();
        delay(100);
    }
    // Stop channel hopper if running
    if (is_channel_hopping()) {
        Serial.println("[WiFi Cleanup] Stopping channel hopper...");
        stop_channel_hopping();
        // Wait for channel hopper task to be NULL (with timeout)
        int wait_ms = 0;
        while (get_channel_hopper_task_handle() != NULL && wait_ms < 1000) {
            delay(50);
            wait_ms += 50;
        }
        if (get_channel_hopper_task_handle() == NULL) {
            Serial.println("[WiFi Cleanup] Channel hopper stopped.");
        } else {
            Serial.println("[WiFi Cleanup] Channel hopper did not stop in time!");
        }
    }
    // Disable promiscuous callbacks and monitor mode
    esp_wifi_set_promiscuous_rx_cb(NULL);
    esp_wifi_set_promiscuous(false);
    delay(50);
    // Stop pwnagotchi scan callbacks if any
    pwnagotchi_scan_stop();
    delay(30);
    // Forcibly stop deauth if running
    if (is_deauth_running()) {
        Serial.println("[WiFi Cleanup] Stopping deauth...");
        deauth_stop();
        delay(50);
    }
    // Disconnect WiFi
    WiFi.softAPdisconnect(true);
    WiFi.disconnect(true);
    delay(50);
    Serial.println("[WiFi Cleanup] All WiFi-related tasks and callbacks stopped.");
}

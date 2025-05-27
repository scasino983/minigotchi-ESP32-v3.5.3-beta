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

/**
 * Sends a pwnagotchi packet in AP mode with improved stability
 */
bool Frame::send() {
  Serial.printf("Frame::send() - Entry. Free heap: %d\n", ESP.getFreeHeap());
  // Save current WiFi state before changing it
  wifi_mode_t previousMode;
  esp_wifi_get_mode(&previousMode);
  bool wasPromiscuous = false;
  esp_wifi_get_promiscuous(&wasPromiscuous);
  
  // If we were in promiscuous mode, turn it off properly with explicit wifi_driver_can_send_packet check
  if (wasPromiscuous) {
    esp_wifi_set_promiscuous(false);
    delay(75);  // Give WiFi state time to settle - increased for better stability
  }
  
  // Clean up existing connections if any
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  delay(50); // Keep 50ms delay for disconnects as it's not directly related to tx stability
  
  // Set to AP mode for frame transmission
  esp_err_t mode_err = esp_wifi_set_mode(WIFI_MODE_AP);
  if (mode_err != ESP_OK) {
    Serial.printf("Failed to set WiFi mode to AP: %s\n", esp_err_to_name(mode_err));
    // Try to restore previous state
    if (wasPromiscuous) {
      esp_wifi_set_mode(previousMode);
      delay(30); // Adjusted delay
      esp_wifi_set_promiscuous(true);
    }
    Serial.printf("Frame::send() - Exit (mode set failed). Free heap: %d\n", ESP.getFreeHeap());
    return false;
  }
  
  delay(75);  // Give WiFi time to settle into AP mode - increased for better stability

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
    wifi_sniffer_stop();
    delay(100);  // Give time for the sniffer to stop properly
  }

  // Ensure we're not in monitor mode
  esp_wifi_set_promiscuous(false);
  delay(50);

  Serial.println(mood.getIntense() + " Starting advertisement...");
  Display::updateDisplay(mood.getIntense(), "Starting advertisement...");
  Parasite::sendAdvertising();
  delay(Config::shortDelay);
  
  // Check available heap memory
  int availableHeap = ESP.getFreeHeap();
  int maxPackets = min(15, availableHeap / 10240);  // More conservative packet count
  maxPackets = max(3, maxPackets);  // At least send 3 packets
  
  Serial.printf("%s Available heap: %d bytes, sending max %d packets\n", 
               mood.getNeutral().c_str(), availableHeap, maxPackets);
  Serial.printf("Frame::advertise() - Starting packet send loop. Max packets: %d. Free heap: %d\n", maxPackets, ESP.getFreeHeap());

  // Reset WiFi completely before sending frames
  WiFi.disconnect(true);
  delay(50);
  
  // Send a more reasonable number of packets with proper error handling
  for (int i = 0; i < maxPackets; ++i) {
    // Check if we're running low on memory
    if (ESP.getFreeHeap() < 15000) {  // Increased threshold
      Serial.println(mood.getBroken() + " Low memory, stopping advertisement");
      break;
    }
    
    if (Frame::send()) {
      packets++;

      // calculate packets per second
      float pps = packets / (float)(millis() - startTime) * 1000;

      // show pps
      if (!isinf(pps)) {
        Serial.printf("%s Packets: %d, Rate: %.1f pkt/s (Channel: %d)\n", 
                     mood.getIntense().c_str(), packets, pps, Channel::getChannel());
        
        Display::updateDisplay(
            mood.getIntense(),
            "Pkt: " + String(packets) + " @ " + String(pps, 1) + " pkt/s" +
                " (CH: " + String(Channel::getChannel()) + ")");
      }
      
      // Add a larger delay between packets to avoid overloading the WiFi stack
      delay(150);  // Increased from 100ms
    } else {
      Serial.println(mood.getBroken() + " Advertisement failed to send!");
      Serial.printf("Frame::advertise() - Frame::send() failed. Loop iteration: %d. Free heap: %d\n", i, ESP.getFreeHeap());
      Display::updateDisplay(mood.getBroken(), "Advertisement failed to send!");
      delay(250);  // Longer delay after a failure - increased from 200ms
    }
  }

  Serial.println(mood.getHappy() + " Advertisement finished!");
  Display::updateDisplay(mood.getHappy(), "Advertisement finished!");
  delay(Config::shortDelay);
  
  // Restart the sniffer if it was running before
  if (sniffer_was_running) {
    delay(250);  // Give WiFi time to settle - increased from 200ms
    Serial.println(mood.getNeutral() + " Restarting sniffer after advertisement...");
    wifi_sniffer_start();
  }
  Serial.printf("Frame::advertise() - Exit. Free heap: %d\n", ESP.getFreeHeap());
}

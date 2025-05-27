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
 * channel.cpp: handles channel switching
 */

#include "channel.h"

/** developer note:
 *
 * i am using the ideal 2.4 GHz channels, which are 1, 6, and 11.
 * the reason for that is so we don't interfere with other devices on our
 * frequency. there are probably plenty more reasons but this is a good practice
 * for such iot devices.
 *
 */

/**
 * Gets first instance of mood class
 */
Mood &Channel::mood = Mood::getInstance();

/**
 * Channels to use, matching the config
 */
int Channel::channelList[13] = {
    Config::channels[0], Config::channels[1],  Config::channels[2],
    Config::channels[3], Config::channels[4],  Config::channels[5],
    Config::channels[6], Config::channels[7],  Config::channels[8],
    Config::channels[9], Config::channels[10], Config::channels[11],
    Config::channels[12]};

/**
 * Here, we choose the channel to initialize on
 * @param initChannel Channel to initialize on
 */
void Channel::init(int initChannel) {
  // start on user specified channel
  delay(Config::shortDelay);
  Serial.println(" ");
  Serial.print(mood.getSleeping() + " Initializing on channel ");
  Serial.println(initChannel);
  Serial.println(" ");
  Display::updateDisplay(mood.getSleeping(),
                         "Initializing on channel " + (String)initChannel);
  delay(Config::shortDelay);

  // switch channel
  Minigotchi::monStop();
  esp_err_t err = esp_wifi_set_channel(initChannel, WIFI_SECOND_CHAN_NONE);
  Minigotchi::monStart();

  if (err == ESP_OK && initChannel == getChannel()) {
    Serial.print(mood.getNeutral() + " Successfully initialized on channel ");
    Serial.println(getChannel());
    Display::updateDisplay(mood.getNeutral(),
                           "Successfully initialized on channel " +
                               (String)getChannel());
    delay(Config::shortDelay);
  } else {
    Serial.println(mood.getBroken() +
                   " Channel initialization failed, try again?");
    Display::updateDisplay(mood.getBroken(),
                           "Channel initialization failed, try again?");
    delay(Config::shortDelay);
  }
}

/**
 * Cycle channels with improved reliability
 */
void Channel::cycle() {
  static uint8_t lastSuccessfulChannel = 0;
  static uint8_t failedAttempts = 0;
  const uint8_t MAX_FAILED_ATTEMPTS = 3;
  
  // Get channels
  int numChannels = sizeof(channelList) / sizeof(channelList[0]);
  
  // Get current channel before switching
  int currentChannel = getChannel();
  
  // Select the next channel using a smarter strategy
  int newChannel;
  
  if (failedAttempts >= MAX_FAILED_ATTEMPTS) {
    // After multiple failures, try the last known good channel
    newChannel = (lastSuccessfulChannel > 0) ? lastSuccessfulChannel : Config::channel;
    Serial.println(Minigotchi::getMood().getIntense() + " Too many failed channel switches, reverting to known good channel: " + String(newChannel));
    failedAttempts = 0; // Reset counter
  } else {
    // Normal channel selection - prioritize 1, 6, and 11 which are non-overlapping channels
    int primaryChannels[] = {1, 6, 11};
    static int primaryIndex = 0;
    
    // 70% chance to use primary channels, 30% chance to use any other channel
    bool usePrimary = (random(10) < 7);
    
    if (usePrimary) {
      // Cycle through 1, 6, 11
      primaryIndex = (primaryIndex + 1) % 3;
      newChannel = primaryChannels[primaryIndex];
    } else {
      // Random channel selection, avoiding current channel
      do {
        int randomIndex = random(numChannels);
        newChannel = channelList[randomIndex];
      } while (newChannel == currentChannel && numChannels > 1);
    }
  }
  
  // Stop monitor mode completely before switching channel
  Serial.println(Minigotchi::getMood().getNeutral() + " Pausing monitor mode for channel switch...");
  Minigotchi::monStop();
  delay(150);  // Increased delay to ensure WiFi state settles
  
  // Save WiFi state
  wifi_mode_t previousMode;
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_get_mode(&previousMode));
  
  // Ensure in STA mode for channel switch
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(WIFI_MODE_STA));
  delay(50);
  
  // Switch channel
  esp_err_t err = esp_wifi_set_channel(newChannel, WIFI_SECOND_CHAN_NONE);
  
  if (err != ESP_OK) {
    Serial.printf("%s Channel switch failed. Error: %s (0x%x)\n", 
                  Minigotchi::getMood().getBroken().c_str(), 
                  esp_err_to_name(err), 
                  err);
    failedAttempts++;
  } else {
    // Verify switch was successful
    delay(50);
    int actualChannel = getChannel();
    if (actualChannel == newChannel) {
      Serial.printf("%s Successfully switched to channel %d\n", 
                    Minigotchi::getMood().getHappy().c_str(), 
                    newChannel);
      lastSuccessfulChannel = newChannel;
      failedAttempts = 0;
      
      // Update display with success
      Display::updateDisplay(Minigotchi::getMood().getNeutral(), 
                            "CH: " + String(newChannel));
    } else {
      Serial.printf("%s Channel verification failed. Requested: %d, Actual: %d\n", 
                    Minigotchi::getMood().getBroken().c_str(), 
                    newChannel, 
                    actualChannel);
      failedAttempts++;
    }
  }
  
  // Restore previous WiFi mode
  ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(previousMode));
  delay(50);
  
  // Restart monitor mode
  Serial.println(Minigotchi::getMood().getNeutral() + " Resuming monitor mode after channel switch...");
  Minigotchi::monStart();
}

/**
 * Switch to given channel with improved reliability
 * @param newChannel New channel to switch to
 */
void Channel::switchChannel(int newChannel) {
  if (!isValidChannel(newChannel)) {
    Serial.printf("%s Invalid channel %d requested. Using default channel %d instead.\n", 
                  mood.getBroken().c_str(), newChannel, Config::channel);
    newChannel = Config::channel;
  }

  Serial.printf("%s Switching to channel %d (was on channel %d)\n", 
                mood.getSleeping().c_str(), newChannel, getChannel());
  Display::updateDisplay(mood.getSleeping(),
                         "Switching to channel " + (String)newChannel);
  
  // Save current WiFi state
  wifi_mode_t current_mode;
  esp_wifi_get_mode(&current_mode);
  
  // Completely stop WiFi for a clean switch
  bool was_promiscuous = false;
  esp_wifi_get_promiscuous(&was_promiscuous);
  
  if (was_promiscuous) {
    esp_wifi_set_promiscuous(false);
  }
  
  // Make sure we're in STA mode for the channel switch
  esp_wifi_set_mode(WIFI_MODE_STA);
  
  // Set the channel - with multiple attempts if needed
  esp_err_t err = ESP_FAIL;
  int max_attempts = 3;
  
  for (int attempt = 1; attempt <= max_attempts; attempt++) {
    err = esp_wifi_set_channel(newChannel, WIFI_SECOND_CHAN_NONE);
    
    if (err == ESP_OK) {
      // Verify the switch actually worked
      delay(50);
      int actual_channel = getChannel();
      
      if (actual_channel == newChannel) {
        // Success!
        break;
      } else {
        Serial.printf("%s Channel verification failed on attempt %d. Requested: %d, Actual: %d\n", 
                     mood.getSad().c_str(), attempt, newChannel, actual_channel);
        err = ESP_FAIL;  // Force another attempt
      }
    }
    
    if (err != ESP_OK && attempt < max_attempts) {
      Serial.printf("%s Channel switch attempt %d failed. Error: %s. Retrying...\n", 
                   mood.getSad().c_str(), attempt, esp_err_to_name(err));
      delay(150 * attempt);  // Increasing backoff delay
    }
  }

  // Restore previous WiFi state
  esp_wifi_set_mode(current_mode);
  
  if (was_promiscuous) {
    esp_wifi_set_promiscuous(true);
  }

  // Check final result
  if (err == ESP_OK) {
    Serial.printf("%s Successfully switched to channel %d\n", 
                 mood.getNeutral().c_str(), getChannel());
    Display::updateDisplay(mood.getNeutral(),
                         "On channel " + (String)getChannel());
  } else {
    Serial.printf("%s Failed to switch to channel %d after %d attempts. Error: %s\n", 
                 mood.getBroken().c_str(), newChannel, max_attempts, esp_err_to_name(err));
    Display::updateDisplay(mood.getBroken(), 
                         "Failed switch to ch " + (String)newChannel);
  }
}

/**
 * Check if the channel switch was successful and display status
 * @param expected_channel Channel that should have been set
 * @return true if channel matches expected, false otherwise
 */
bool Channel::checkChannel(int expected_channel) {
  int currentChannel = Channel::getChannel();
  bool success = (expected_channel == currentChannel);
  
  if (success) {
    Serial.printf("%s Currently on channel %d (as expected)\n", 
                 mood.getNeutral().c_str(), currentChannel);
    Display::updateDisplay(mood.getNeutral(),
                         "On channel " + (String)currentChannel);
  } else {
    Serial.printf("%s Channel mismatch! Expected: %d, Actual: %d\n", 
                 mood.getBroken().c_str(), expected_channel, currentChannel);
    Display::updateDisplay(mood.getBroken(), 
                         "Ch mismatch! Exp:" + (String)expected_channel + 
                         " Act:" + (String)currentChannel);
  }
  
  return success;
}

/**
 * Checks whether or not channel is valid by indexing channel list
 * @param channel Channel to check
 */
bool Channel::isValidChannel(int channel) {
  bool isValidChannel = false;
  for (int i = 0; i < sizeof(channelList) / sizeof(channelList[0]); i++) {
    if (channelList[i] == channel) {
      isValidChannel = true;
      break;
    }
  }
  return isValidChannel;
}

/**
 * Returns current channel as an integer
 */
int Channel::getChannel() {
  uint8_t primary;
  wifi_second_chan_t second;
  esp_wifi_get_channel(&primary, &second);
  return primary;
}

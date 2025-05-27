/*
 * Minigotchi: An even smaller Pwnagotchi
 * Copyright (C) 2024 dj1ch
 * (Full license header as in your original file)
 */

#include "minigotchi.h"
#include "config.h"
#include "display.h"
#include "channel.h"
#include "frame.h"
#include "pwnagotchi.h"
#include "deauth.h"
#include "parasite.h"
// #include "ble.h" // BLE functionality removed
#include "webui.h"
#include "AXP192.h"

#include <SPI.h>
#include <SD.h>
#include "pcap_logger.h"
#include "handshake_logger.h" // Added to resolve missing declarations
#include "wifi_sniffer.h" // <-- NEW INCLUDE
#include <WiFi.h> // Include WiFi.h for WiFi.mode() calls


#ifndef SD_CS_PIN
  #define SD_CS_PIN 5
#endif

// Initializing static members
Mood &Minigotchi::mood = Mood::getInstance();
WebUI *Minigotchi::web = nullptr;
int Minigotchi::currentEpoch = 0;

void Minigotchi::WebUITask(void *pvParameters) {
  WebUI web_ui_obj;
  if (!WebUI::running) {
      Serial.println(Minigotchi::mood.getBroken() + " WebUI failed to initialize properly in constructor!");
      vTaskDelete(NULL);
      return;
  }
  Serial.println(Minigotchi::mood.getNeutral() + " WebUITask: WebUI object created/accessible, entering wait loop.");
  while (!Config::configured) {
    WebUI::processDNS();
    taskYIELD();
  }
  Serial.println(Minigotchi::mood.getHappy() + " WebUITask: Config::configured is true. Cleaning up WebUI.");
  // Destructor for web_ui_obj will be called when task exits.
  vTaskDelete(NULL);
}

void Minigotchi::waitForInput() {
  if (!Config::configured) {
    xTaskCreatePinnedToCore(WebUITask, "WebUI_Task", 8192, NULL, 1, NULL, 1);
  }
  while (!Config::configured) {
    delay(100);
  }
}

int Minigotchi::addEpoch() {
  Minigotchi::currentEpoch++;
  return Minigotchi::currentEpoch;
}

void Minigotchi::epoch() {
  Minigotchi::addEpoch();
  Parasite::readData();
  Serial.print(Minigotchi::mood.getNeutral() + " Current Epoch: ");
  Serial.println(Minigotchi::currentEpoch);
  Serial.println(" ");
  Display::updateDisplay(Minigotchi::mood.getNeutral(),
                         "Current Epoch: " + String(Minigotchi::currentEpoch));
}

void Minigotchi::boot() {
  Mood::init(Config::happy, Config::sad, Config::broken, Config::intense,
             Config::looking1, Config::looking2, Config::neutral,
             Config::sleeping);

  if (String(Config::screen.c_str()) == "M5STICKCP") {
    AXP192 axp192;
    axp192.begin();
    axp192.ScreenBreath(100);
  } else if (String(Config::screen.c_str()) == "M5STICKCP2") {
    pinMode(4, OUTPUT);
    digitalWrite(4, HIGH);
  }

  Display::startScreen();
  Serial.println(" ");
  Serial.println(Minigotchi::mood.getHappy() +
                 " Hi, I'm Minigotchi, your pwnagotchi's best friend!");
  Display::updateDisplay(Minigotchi::mood.getHappy(), "Hi, I'm Minigotchi");
  delay(Config::shortDelay);
  Serial.println(Minigotchi::mood.getNeutral() +
                 " You can edit my configuration parameters in config.cpp!");
  Display::updateDisplay(Minigotchi::mood.getNeutral(), "Edit config.cpp!");
  delay(Config::shortDelay);
  Serial.println(Minigotchi::mood.getIntense() + " Starting now...");
  Display::updateDisplay(Minigotchi::mood.getIntense(), "Starting now");
  delay(Config::shortDelay);
  Serial.println("################################################");
  Serial.println("#                BOOTUP PROCESS                #");
  Serial.println("################################################");
  Serial.println(" ");

  // Initialize NVS
  esp_err_t err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  Config::loadConfig(); // Load configuration (sets Config::configured)

  // SD Card Initialization
  Serial.println(Minigotchi::mood.getNeutral() + " Initializing SD card...");
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD card initialization failed!");
    Display::updateDisplay(getMood().getSad(), "SD Card Failed!");
    delay(Config::shortDelay);
  } else {
    Serial.println("SD card initialized successfully!");
    Display::updateDisplay(getMood().getHappy(), "SD Card OK!");
    delay(Config::shortDelay);
    // SD card test file creation
    File testFile = SD.open("/minigotchi_sd_test.txt", FILE_WRITE);
    if (testFile) {
      testFile.println("Minigotchi SD test successful at " + String(millis()));
      testFile.close();
      Serial.println("Successfully created/wrote to /minigotchi_sd_test.txt");
    } else {
      Serial.println("Failed to open /minigotchi_sd_test.txt for writing.");
    }    // PCAP logger test
    Serial.println("Initializing PCAP Logger for test...");
    if (pcap_logger_init() == ESP_OK) {
      Serial.println("PCAP Logger initialized.");
      if (pcap_logger_open_new_file() == ESP_OK) {
        Serial.println("New PCAP file opened for test.");
        const uint8_t dummy_packet[] = {
            0x80, 0x00, 0x00, 0x00, 0xff,0xff,0xff,0xff,0xff,0xff, 0x01,0x02,0x03,0x04,0x05,0x06, 
            0x01,0x02,0x03,0x04,0x05,0x06, 0x00, 0x00, 0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
            0x64,0x00,0x01,0x04,0x00,0x04,'T','E','S','T',0x01,0x08,0x82,0x84,0x8b,0x96,0x0c,0x12,0x18,0x24
        };
        size_t dummy_len = sizeof(dummy_packet);
        esp_err_t write_err = pcap_logger_write_packet(dummy_packet, dummy_len);
        if (write_err == ESP_OK) {
          Serial.println("Dummy packet written to PCAP buffer.");
        } else {
          Serial.println("Failed to write dummy packet. Error: " + String(esp_err_to_name(write_err)));
        }
        esp_err_t flush_err = pcap_logger_flush_buffer();
        if (flush_err == ESP_OK) {
            Serial.println("PCAP buffer flushed successfully for test.");
        } else {
            Serial.println("Failed to flush PCAP buffer for test. Error: " + String(esp_err_to_name(flush_err)));
        }
        pcap_logger_close_file(); 
        Serial.println("PCAP file closed after test write.");
      } else {
        Serial.println("Failed to open new PCAP file for testing.");
      }
    } else {
      Serial.println("Failed to initialize PCAP Logger for testing.");
    }
    
    // Handshake CSV logger test
    Serial.println("Initializing Handshake CSV Logger for test...");
    if (handshake_logger_init() == ESP_OK) {
      Serial.println("Handshake CSV Logger initialized.");
      if (handshake_logger_open_new_file() == ESP_OK) {
        Serial.println("New Handshake CSV file opened for test.");
        // Test entry with sample handshake data
        esp_err_t csv_err = handshake_logger_write_entry(
          "aa:bb:cc:dd:ee:ff", "11:22:33:44:55:66", "M1 (AP to STA)", 6, "Test_SSID");
        if (csv_err == ESP_OK) {
          Serial.println("Test handshake entry written to CSV file.");
        } else {
          Serial.println("Failed to write test handshake entry. Error: " + String(esp_err_to_name(csv_err)));
        }
        handshake_logger_close_file();
        Serial.println("Handshake CSV file closed after test write.");
      } else {
        Serial.println("Failed to open new Handshake CSV file for testing.");
      }
    } else {
      Serial.println("Failed to initialize Handshake CSV Logger for testing.");
    }
  }
  delay(Config::shortDelay);

  ESP_ERROR_CHECK(esp_wifi_init(&Config::wifiCfg));
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_country(&Config::ctryCfg));

  if (!Config::configured) {
    Serial.println(Minigotchi::mood.getNeutral() + " Device not configured. Starting WebUI for setup...");
    waitForInput();
    Serial.println(Minigotchi::mood.getHappy() + " WebUI configuration completed.");
    WiFi.mode(WIFI_OFF);
    Serial.println(Minigotchi::mood.getNeutral() + " WiFi turned OFF after WebUI config.");
  } else {
    Serial.println(Minigotchi::mood.getNeutral() + " Device already configured. Setting up WiFi in STA mode...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    Serial.println(Minigotchi::mood.getNeutral() + " WiFi STA mode enabled. (Connection attempt depends on saved credentials).");
  }

  Deauth::list();
  Channel::init(Config::channel);
  // Minigotchi::info(); // Called later in loop or by other functions

  // Start WiFi Sniffer for testing (30-second capture then stop)
  Serial.println(Minigotchi::mood.getNeutral() + " Attempting to start WiFi sniffer for testing...");
  esp_err_t sniffer_err = wifi_sniffer_start(); // This will also open the first PCAP file
  if (sniffer_err == ESP_OK) {
      Serial.println(Minigotchi::mood.getHappy() + " WiFi sniffer started for 30-second test from boot().");
      delay(30000); // Sniff for 30 seconds
      Serial.println(Minigotchi::mood.getNeutral() + " Stopping WiFi sniffer after 30s test.");
      wifi_sniffer_stop(); // This will close the PCAP file
      Serial.println(Minigotchi::mood.getNeutral() + " WiFi sniffer stopped after test.");
  } else {
      Serial.println(Minigotchi::mood.getBroken() + " Failed to start WiFi sniffer from boot(). Error: " + String(esp_err_to_name(sniffer_err)));
  }

  Minigotchi::finish();
}

void Minigotchi::info() {
  delay(Config::shortDelay);
  Serial.println(" ");
  Serial.println(Minigotchi::mood.getNeutral() + " Current Minigotchi Stats: ");
  Display::updateDisplay(Minigotchi::mood.getNeutral(), "Current Minigotchi Stats:");
  version();
  mem();
  cpu();
  Serial.println(" ");
  delay(Config::shortDelay);
}

void Minigotchi::finish() {
  Serial.println("################################################");
  Serial.println(" ");
  Serial.println(Minigotchi::mood.getHappy() + " Started successfully!");
  Display::updateDisplay(Minigotchi::mood.getHappy(), "Started successfully"); // Corrected typo
  delay(Config::shortDelay);
}

void Minigotchi::version() {
  Serial.print(Minigotchi::mood.getNeutral() + " Version: ");
  Serial.println(Config::version.c_str());
  Display::updateDisplay(Minigotchi::mood.getNeutral(),
                         "Version: " + String(Config::version.c_str()));
  delay(Config::shortDelay);
}

void Minigotchi::mem() {
  Serial.print(Minigotchi::mood.getNeutral() + " Heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println(" bytes");
  Display::updateDisplay(Minigotchi::mood.getNeutral(),
                         "Heap: " + String(ESP.getFreeHeap()) + " bytes");
  delay(Config::shortDelay);
}

void Minigotchi::cpu() {
  Serial.print(Minigotchi::mood.getNeutral() + " CPU Frequency: ");
  Serial.print(ESP.getCpuFreqMHz());
  Serial.println(" MHz");
  Display::updateDisplay(Minigotchi::mood.getNeutral(),
                         "CPU Frequency: " + String(ESP.getCpuFreqMHz()) + " MHz");
  delay(Config::shortDelay);
}

bool Minigotchi::monStart() {
  // First check if WiFi is initialized at all
  wifi_mode_t mode;
  esp_err_t mode_err = esp_wifi_get_mode(&mode);
  
  if (mode_err == ESP_ERR_WIFI_NOT_INIT) {
    Serial.println(Minigotchi::mood.getIntense() + " WiFi not initialized, performing initialization...");
    
    // Initialize WiFi with default configuration
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t init_err = esp_wifi_init(&cfg);
    
    if (init_err != ESP_OK) {
      Serial.printf("%s Failed to initialize WiFi: %s\n", 
                   Minigotchi::mood.getBroken().c_str(), esp_err_to_name(init_err));
      return false;
    }
    
    // Start WiFi with retry mechanism
    esp_err_t start_err = ESP_FAIL;
    for (int retry = 0; retry < 3; retry++) {
      start_err = esp_wifi_start();
      if (start_err == ESP_OK) {
        break;
      }
      
      Serial.printf("%s Failed to start WiFi (attempt %d): %s\n", 
                   Minigotchi::mood.getBroken().c_str(), retry+1, esp_err_to_name(start_err));
      
      if (retry < 2) {
        delay(100 * (retry + 1));
      }
    }
    
    if (start_err != ESP_OK) {
      Serial.printf("%s Failed to start WiFi after multiple attempts: %s\n", 
                   Minigotchi::mood.getBroken().c_str(), esp_err_to_name(start_err));
      esp_wifi_deinit(); // Clean up
      return false;
    }
    
    // Give WiFi time to initialize
    delay(150);
    
    // Re-check mode after initialization
    mode_err = esp_wifi_get_mode(&mode);
    if (mode_err != ESP_OK) {
      Serial.printf("%s Failed to get WiFi mode after init: %s\n", 
                   Minigotchi::mood.getBroken().c_str(), esp_err_to_name(mode_err));
      return false;
    }
  } else if (mode_err != ESP_OK) {
    Serial.printf("%s Error checking WiFi mode: %s\n", 
                 Minigotchi::mood.getBroken().c_str(), esp_err_to_name(mode_err));
    return false;
  }
  
  // Make sure any existing connections are closed
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  
  // Give a small delay to allow the disconnect to complete
  delay(100); // Increased for better stability
  
  // Set mode to STA for monitor mode with retry
  bool sta_mode_set = false;
  for (int retry = 0; retry < 3; retry++) {
    esp_err_t sta_err = esp_wifi_set_mode(WIFI_MODE_STA);
    if (sta_err == ESP_OK) {
      sta_mode_set = true;
      break;
    }
    
    Serial.printf("%s Failed to set WiFi to STA mode (attempt %d): %s\n", 
                 Minigotchi::mood.getBroken().c_str(), retry+1, esp_err_to_name(sta_err));
    
    if (retry < 2) {
      delay(100 * (retry + 1));
    }
  }
  
  if (!sta_mode_set) {
    Serial.println(Minigotchi::mood.getBroken() + " Failed to set WiFi to STA mode after multiple attempts.");
    return false;
  }
  
  // Another small delay before enabling promiscuous mode
  delay(100); // Increased for better stability
  
  // First check if we're already in promiscuous mode
  bool is_promiscuous = false;
  esp_wifi_get_promiscuous(&is_promiscuous);
  
  if (is_promiscuous) {
    Serial.println(Minigotchi::mood.getNeutral() + " Already in promiscuous mode.");
    return true;
  }
  
  // Set promiscuous mode with error handling and retry
  bool success = false;
  for (int attempt = 1; attempt <= 3; attempt++) {
    esp_err_t promisc_err = esp_wifi_set_promiscuous(true);
    
    if (promisc_err == ESP_OK) {
      success = true;
      break;
    } else {
      Serial.printf("%s Failed to start monitor mode (attempt %d): %s\n", 
                   Minigotchi::mood.getSad().c_str(), attempt, esp_err_to_name(promisc_err));
      
      if (attempt < 3) {
        // Retry with increasing delay
        delay(100 * attempt);
        
        // Try restarting WiFi before the next attempt
        WiFi.mode(WIFI_OFF);
        delay(100);
        WiFi.mode(WIFI_STA);
        delay(100);
      }
    }
  }
  
  if (success) {
    Serial.println(Minigotchi::mood.getHappy() + " Monitor mode started successfully.");
  } else {
    Serial.println(Minigotchi::mood.getBroken() + " Failed to start monitor mode after multiple attempts.");
    
    // One last desperate attempt with a full WiFi reset
    Serial.println(Minigotchi::mood.getIntense() + " Attempting full WiFi reset as last resort...");
    
    // Full cleanup
    esp_wifi_stop();
    delay(100);
    esp_wifi_deinit();
    delay(150);
    
    // Reinitialize
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK || esp_wifi_start() != ESP_OK) {
      Serial.println(Minigotchi::mood.getBroken() + " Last resort WiFi reset failed.");
      return false;
    }
    
    // Try STA mode again
    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) {
      Serial.println(Minigotchi::mood.getBroken() + " Failed to set STA mode after reset.");
      return false;
    }
    
    delay(150);
    
    // One final attempt to enable promiscuous mode
    esp_err_t final_attempt = esp_wifi_set_promiscuous(true);
    if (final_attempt == ESP_OK) {
      Serial.println(Minigotchi::mood.getHappy() + " Monitor mode started after last resort reset!");
      success = true;
    } else {
      Serial.printf("%s Final attempt to start monitor mode failed: %s\n", 
                   Minigotchi::mood.getBroken().c_str(), esp_err_to_name(final_attempt));
    }
  }
  
  // Verify we're actually in promiscuous mode
  esp_wifi_get_promiscuous(&is_promiscuous);
  if (!is_promiscuous && success) {
    Serial.println(Minigotchi::mood.getBroken() + " WARNING: Monitor mode state verification failed!");
    success = false;
  }
  
  return success;
}

void Minigotchi::monStop() {
  // First check if WiFi is initialized
  wifi_mode_t mode;
  esp_err_t mode_err = esp_wifi_get_mode(&mode);

  if (mode_err == ESP_ERR_WIFI_NOT_INIT) {
    Serial.println(mood.getBroken() + " WiFi not initialized, cannot stop monitor mode properly.");
    // Try to reinitialize WiFi for cleanup
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) == ESP_OK) {
      esp_wifi_start();
      delay(100);
      WiFi.mode(WIFI_STA);
      Serial.println(mood.getNeutral() + " WiFi reinitialized in STA mode.");
    } else {
      Serial.println(mood.getBroken() + " Failed to reinitialize WiFi for monStop.");
    }
    return;
  }

  // Check if promiscuous mode is actually active
  bool is_promiscuous = false;
  esp_err_t check_err = esp_wifi_get_promiscuous(&is_promiscuous);
  if (check_err != ESP_OK) {
    Serial.println(mood.getBroken() + " Error checking promiscuous mode: " + String(esp_err_to_name(check_err)));
  }

  // Always disable callback
  esp_wifi_set_promiscuous_rx_cb(NULL);

  // Try to disable promiscuous mode with retries
  bool success = false;
  for (int attempt = 1; attempt <= 3; attempt++) {
    esp_err_t promisc_off_err = esp_wifi_set_promiscuous(false);
    if (promisc_off_err == ESP_OK) {
      success = true;
      break;
    } else {
      Serial.println(mood.getSad() + " Failed to stop monitor mode on attempt " + String(attempt) + ". Error: " + String(esp_err_to_name(promisc_off_err)));
      delay(100 * attempt);
    }
  }

  if (success) {
    Serial.println(mood.getNeutral() + " Promiscuous mode stopped.");
  } else {
    Serial.println(mood.getBroken() + " Failed to stop monitor mode properly after multiple attempts.");
    // Force WiFi reset as a last resort
    esp_wifi_stop();
    delay(100);
    esp_wifi_deinit();
    delay(150);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) == ESP_OK && esp_wifi_start() == ESP_OK) {
      WiFi.mode(WIFI_STA);
      Serial.println(mood.getNeutral() + " WiFi reset and set to STA mode after failed monStop.");
    } else {
      Serial.println(mood.getBroken() + " Last resort WiFi reset failed in monStop.");
    }
  }
}

void Minigotchi::cycle() {
  Parasite::readData();
  Channel::cycle();
}

void Minigotchi::detect() {
  Parasite::readData();
  Pwnagotchi::detect(); // This is where Pwnagotchi scanning happens
}

void Minigotchi::deauth() {
  Parasite::readData();
  Deauth::deauth();
}

void Minigotchi::advertise() {
  Parasite::readData();
  Frame::advertise();
}

// void Minigotchi::spam() { // BLE functionality removed
//  Parasite::readData();
//  Ble::spam();
// }

void Minigotchi::displaySecurityEvaluation() {
  Serial.println(Minigotchi::mood.getNeutral() + " --- Security Evaluation ---");
  Display::updateDisplay(Minigotchi::mood.getNeutral(), "Security Stats:");
  delay(Config::shortDelay);

  // Display Live AP Count
  Serial.println(Minigotchi::mood.getLooking1() + " Scanning for APs...");
  Display::updateDisplay(Minigotchi::mood.getLooking1(), "Scanning APs...");
  int apCount = WiFi.scanNetworks(false, true); // Scan hidden SSIDs as well, don't block
  if (apCount < 0) {
    Serial.println(Minigotchi::mood.getBroken() + " WiFi scan error!");
    Display::updateDisplay(Minigotchi::mood.getBroken(), "AP Scan Error");
    apCount = 0;
  } else {
    Serial.println(Minigotchi::mood.getHappy() + " Found " + String(apCount) + " APs.");
    Display::updateDisplay(Minigotchi::mood.getHappy(), "APs Found: " + String(apCount));
  }
  delay(Config::longDelay); // Display for a bit
  // Placeholder for total handshakes - to be implemented next
  Serial.println(Minigotchi::mood.getNeutral() + " Total Handshakes: (counting...)");
  Display::updateDisplay(Minigotchi::mood.getNeutral(), "Handshakes: (counting...)");
  delay(Config::shortDelay);
  
  // Get and display the handshake count
  int totalHandshakes = 0;
  if (handshake_logger_get_total_handshakes(&totalHandshakes) == ESP_OK) {
    Serial.println(Minigotchi::mood.getHappy() + " Total Handshakes: " + String(totalHandshakes));
    Display::updateDisplay(Minigotchi::mood.getHappy(), "Handshakes: " + String(totalHandshakes));
  } else {
    Serial.println(Minigotchi::mood.getBroken() + " Error getting handshake count");
    Display::updateDisplay(Minigotchi::mood.getBroken(), "Error getting handshake count");
  }
  // Serial.println(Minigotchi::mood.getNeutral() + " Total Handshakes: " + String(totalHandshakes));
  // Display::updateDisplay(Minigotchi::mood.getNeutral(), "Handshakes: " + String(totalHandshakes));
  // delay(Config::longDelay);
}

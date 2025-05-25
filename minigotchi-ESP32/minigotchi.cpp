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
#include "ble.h"
#include "webui.h"
#include "AXP192.h"

#include <SPI.h>
#include <SD.h>
#include "pcap_logger.h"
#include "wifi_sniffer.h" // <-- NEW INCLUDE

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
    }
    // PCAP logger test
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
      pcap_logger_deinit();
      Serial.println("PCAP Logger de-initialized after test.");
    } else {
      Serial.println("PCAP Logger failed to initialize.");
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

void Minigotchi::monStart() {
  WiFi.softAPdisconnect(true);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_STA);
  esp_err_t promisc_err = esp_wifi_set_promiscuous(true);
  if (promisc_err != ESP_OK) {
      Serial.println(Minigotchi::mood.getBroken() + " Failed to start monitor mode. Error: " + String(esp_err_to_name(promisc_err)));
      return;
  }
  Serial.println(Minigotchi::mood.getIntense() + " Monitor mode started.");
}

void Minigotchi::monStop() {
  esp_err_t promisc_err = esp_wifi_set_promiscuous(false);
   if (promisc_err != ESP_OK && promisc_err != ESP_ERR_WIFI_NOT_STARTED ){
      Serial.println(Minigotchi::mood.getBroken() + " Failed to stop monitor mode properly. Error: " + String(esp_err_to_name(promisc_err)));
  } else if (promisc_err == ESP_OK) {
    Serial.println(Minigotchi::mood.getNeutral() + " Promiscuous mode stopped.");
  }
  // Revert to a known state, e.g., STA mode (or WIFI_OFF if preferred after sniffing)
  WiFi.mode(WIFI_STA);
  Serial.println(Minigotchi::mood.getNeutral() + " WiFi mode set to STA after stopping monitor mode.");
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

void Minigotchi::spam() {
  Parasite::readData();
  Ble::spam();
}

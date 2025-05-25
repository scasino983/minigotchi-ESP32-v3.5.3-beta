/*
 * Minigotchi: An even smaller Pwnagotchi
 * Copyright (C) 2024 dj1ch
 * (Full license header as in your original file)
 */

#include "minigotchi.h" // Should be minigotchi-ESP32/minigotchi.h
#include <SPI.h>
#include <SD.h>
#include "pcap_logger.h" // Include the PCAP logger header
#include "wifi_sniffer.h" // Include the WiFi sniffer header

// Define SD Card CS Pin globally for this file, or move to config.h if preferred
#define SD_CS_PIN 5

// Initializing static members
Mood &Minigotchi::mood = Mood::getInstance();
WebUI *Minigotchi::web = nullptr;
int Minigotchi::currentEpoch = 0;

// --- Minigotchi Class Method Implementations ---

void Minigotchi::WebUITask(void *pvParameters) {
  WebUI *web = new WebUI();

  if (!WebUI::running) {
      Serial.println(Minigotchi::mood.getBroken() + " WebUI failed to initialize properly in constructor!");
      vTaskDelete(NULL); 
      return;
  }
  Serial.println(Minigotchi::mood.getNeutral() + " WebUITask: WebUI object created, entering wait loop.");

  while (!Config::configured) {
    WebUI::processDNS(); 
    taskYIELD(); 
  }

  Serial.println(Minigotchi::mood.getHappy() + " WebUITask: Config::configured is true. Cleaning up WebUI.");
  delete web;
  web = nullptr; 

  vTaskDelete(NULL);
}

void Minigotchi::waitForInput() {
  if (!Config::configured) {
    xTaskCreatePinnedToCore(WebUITask, "WebUI Task", 8192, NULL, 1, NULL, 1); // Core 1 for WebUI
  }
  while (!Config::configured) {
    delay(10); // Small delay for main task polling
  }
}

int Minigotchi::addEpoch() {
  Minigotchi::currentEpoch++;
  return Minigotchi::currentEpoch;
}

void Minigotchi::epoch() {
  Minigotchi::addEpoch();
  Parasite::readData(); // Assuming Parasite class exists and is intended
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

  if (String(Config::screen.c_str()) == "M5STICKCP") { // Compare Arduino String with c_str()
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
  Serial.println(" ");
  Serial.println(Minigotchi::mood.getNeutral() +
                 " You can edit my configuration parameters in config.cpp!");
  Display::updateDisplay(Minigotchi::mood.getNeutral(), "Edit my config.cpp!");
  delay(Config::shortDelay);
  Serial.println(Minigotchi::mood.getIntense() + " Starting now...");
  Display::updateDisplay(Minigotchi::mood.getIntense(), "Starting  now");
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
    Serial.println(Minigotchi::mood.getBroken() + " SD Card Mount Failed or Card not present!");
  } else {
    Serial.println(Minigotchi::mood.getHappy() + " SD card initialized.");
    uint8_t cardType = SD.cardType();
    String cardTypeStr = "UNKNOWN";
    if (cardType == CARD_NONE) cardTypeStr = "None";
    else if (cardType == CARD_MMC) cardTypeStr = "MMC";
    else if (cardType == CARD_SD)  cardTypeStr = "SDSC";
    else if (cardType == CARD_SDHC) cardTypeStr = "SDHC";
    Serial.println(Minigotchi::mood.getNeutral() + " Card type: " + cardTypeStr);
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize); // Corrected format specifier

    File testFile = SD.open("/minigotchi_sd_test.txt", FILE_WRITE);
    if (testFile) {
      testFile.println("Minigotchi SD test successful at " + String(millis()));
      testFile.close();
      Serial.println(Minigotchi::mood.getHappy() + " Successfully created/wrote to /minigotchi_sd_test.txt");
    } else {
      Serial.println(Minigotchi::mood.getBroken() + " Failed to open /minigotchi_sd_test.txt for writing.");
    }

    // Initialize PCAP Logger (MUST be done for wifi_sniffer to work)
    if (pcap_logger_init() == ESP_OK) {
      Serial.println(Minigotchi::mood.getHappy() + " PCAP Logger initialized successfully for general use.");
    } else {
      Serial.println(Minigotchi::mood.getBroken() + " CRITICAL: PCAP Logger failed to initialize. Sniffer will not work.");
      // Potentially halt or indicate error strongly, as sniffing is a core feature.
    }
  } // This closes the "if (SD.begin())" block
  delay(Config::shortDelay);

  // Initialize WiFi system (esp_wifi_init is often called by WiFi.mode or softAP if not done)
  // If not using WebUI immediately, this STA setup might be desired.
  // If WebUI is launched (because Config::configured is false), it will switch to AP mode.
  ESP_ERROR_CHECK(esp_wifi_init(&Config::wifiCfg)); // Ensure WiFi stack is initialized
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_country(&Config::ctryCfg));

  if (!Config::configured) {
    waitForInput(); // This will start WebUI in AP mode
  } else {
    // If already configured, attempt to connect as STA
    // (Assuming WiFi credentials are saved in Config and loaded by Config::loadConfig())
    Serial.println(Minigotchi::mood.getNeutral() + " Device configured. Setting STA mode.");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    // TODO: Implement actual connection using WiFi.begin(stored_ssid, stored_password);
    // For now, it just enables STA mode.
    Serial.println(Minigotchi::mood.getNeutral() + " Attempting WiFi connection (if credentials available)...");
    // Example: WiFi.begin(Config::station_ssid.c_str(), Config::station_password.c_str());
  }

  Deauth::list();
  Channel::init(Config::channel);
  Minigotchi::info();
  Parasite::sendName(); // Assuming Parasite class

  // Attempt to start WiFi sniffer for testing (after other initializations)
  Serial.println(Minigotchi::mood.getNeutral() + " Attempting to start WiFi sniffer for testing...");
  esp_err_t sniffer_err = wifi_sniffer_start();
  if (sniffer_err == ESP_OK) {
      Serial.println(Minigotchi::mood.getHappy() + " WiFi sniffer started for 30-second test from boot().");
      delay(30000); // Sniff for 30 seconds
      Serial.println(Minigotchi::mood.getNeutral() + " Stopping WiFi sniffer after 30s test.");
      wifi_sniffer_stop();
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
  Display::updateDisplay(Minigotchi::mood.getHappy(), "Started sucessfully");
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
  WiFi.disconnect(true); // Disconnect if in STA mode
  WiFi.mode(WIFI_STA); // Promiscuous mode is an extension of STA mode
  esp_wifi_set_promiscuous(true);
  Serial.println(Minigotchi::mood.getIntense() + " Monitor mode started.");
}

void Minigotchi::monStop() {
  esp_wifi_set_promiscuous(false);
  WiFi.mode(WIFI_STA); // Or WIFI_AP if it should revert to AP, or WIFI_OFF
  Serial.println(Minigotchi::mood.getNeutral() + " Monitor mode stopped.");
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

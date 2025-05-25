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
  // Initialize Moods
  Mood::init(Config::happy, Config::sad, Config::broken, Config::intense,
             Config::looking1, Config::looking2, Config::neutral,
             Config::sleeping);

  // M5StickC/CPlus specific power management
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
  Serial.println(Minigotchi::getMood().getHappy() +
                 " Hi, I'm Minigotchi, your pwnagotchi's best friend!");
  Display::updateDisplay(Minigotchi::getMood().getHappy(), "Hi, I'm Minigotchi");
  delay(Config::shortDelay); 
  Serial.println(Minigotchi::getMood().getNeutral() +
                 " You can edit my configuration parameters in config.cpp!");
  Display::updateDisplay(Minigotchi::getMood().getNeutral(), "Edit config.cpp!");
  delay(Config::shortDelay);
  Serial.println(Minigotchi::getMood().getIntense() + " Starting now...");
  Display::updateDisplay(Minigotchi::getMood().getIntense(), "Starting now");
  delay(Config::shortDelay);
  Serial.println("################################################");
  Serial.println("#                BOOTUP PROCESS                #");
  Serial.println("################################################");
  Serial.println(" ");

  esp_err_t err_nvs = nvs_flash_init();
  if (err_nvs == ESP_ERR_NVS_NO_FREE_PAGES || err_nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err_nvs = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err_nvs);
  Serial.println(Minigotchi::getMood().getNeutral() + " NVS initialized.");

  Config::loadConfig(); 
  Serial.println(Minigotchi::getMood().getNeutral() + " Configuration loaded from NVS.");
  Serial.println(Minigotchi::getMood().getNeutral() + " Config::configured is currently: " + (Config::configured ? "true" : "false"));

  bool sd_card_ok = false; 
  Serial.println(Minigotchi::getMood().getNeutral() + " Initializing SD card...");
  if (!SD.begin(SD_CS_PIN)) { 
    Serial.println(Minigotchi::getMood().getBroken() + " SD Card Mount Failed or Card not present!");
  } else {
    sd_card_ok = true; 
    Serial.println(Minigotchi::getMood().getHappy() + " SD card initialized.");
    uint8_t cardType = SD.cardType();
    String cardTypeStr = "UNKNOWN";
    if (cardType == CARD_NONE) cardTypeStr = "None";
    else if (cardType == CARD_MMC) cardTypeStr = "MMC";
    else if (cardType == CARD_SD)  cardTypeStr = "SDSC";
    else if (cardType == CARD_SDHC) cardTypeStr = "SDHC";
    Serial.println(Minigotchi::getMood().getNeutral() + " Card type: " + cardTypeStr);
    uint64_t cardSize = SD.cardSize() / (1024 * 1024);
    Serial.printf("SD Card Size: %lluMB\n", cardSize);

    File testFile = SD.open("/minigotchi_sd_test.txt", FILE_WRITE);
    if (testFile) {
      testFile.println("Minigotchi SD test successful at " + String(millis()));
      testFile.close(); 
      Serial.println(Minigotchi::getMood().getHappy() + " Successfully created/wrote to /minigotchi_sd_test.txt");
    } else {
      Serial.println(Minigotchi::getMood().getBroken() + " Failed to open /minigotchi_sd_test.txt for writing.");
    }

    Serial.println(Minigotchi::getMood().getNeutral() + " Initializing PCAP Logger...");
    if (pcap_logger_init() == ESP_OK) {
      Serial.println(Minigotchi::getMood().getHappy() + " PCAP Logger initialized.");
    } else {
      Serial.println(Minigotchi::getMood().getBroken() + " PCAP Logger failed to initialize. Sniffing will be disabled.");
      sd_card_ok = false; 
    }
  }
  delay(Config::shortDelay); 

  ESP_ERROR_CHECK(esp_wifi_init(&Config::wifiCfg)); 
  ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
  ESP_ERROR_CHECK(esp_wifi_set_country(&Config::ctryCfg));

  if (!Config::configured) {
    Serial.println(Minigotchi::getMood().getNeutral() + " Device not configured. Starting WebUI for setup...");
    waitForInput(); 
    Serial.println(Minigotchi::getMood().getHappy() + " WebUI configuration completed.");
    WiFi.mode(WIFI_OFF); 
    Serial.println(Minigotchi::getMood().getNeutral() + " WiFi turned OFF after WebUI config.");
  } else {
    Serial.println(Minigotchi::getMood().getNeutral() + " Device already configured. Setting up WiFi in STA mode...");
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start()); 
    Serial.println(Minigotchi::getMood().getNeutral() + " WiFi STA mode enabled. (Connection attempt depends on saved credentials).");
  }
  
  Deauth::list();
  Channel::init(Config::channel);

  if (sd_card_ok) { 
    Serial.println(Minigotchi::getMood().getNeutral() + " Attempting to start WiFi sniffer for 30s test...");
    esp_err_t sniffer_err = wifi_sniffer_start(); 
    if (sniffer_err == ESP_OK) {
        Serial.println(Minigotchi::getMood().getHappy() + " WiFi sniffer started for 30-second test.");
        delay(30000); 
        Serial.println(Minigotchi::getMood().getNeutral() + " Stopping WiFi sniffer after 30s test.");
        wifi_sniffer_stop(); 
        Serial.println(Minigotchi::getMood().getNeutral() + " WiFi sniffer stopped after test.");
        pcap_logger_deinit();
        Serial.println(Minigotchi::getMood().getNeutral() + " PCAP Logger de-initialized after sniffer test.");
    } else {
        Serial.println(Minigotchi::getMood().getBroken() + " Failed to start WiFi sniffer. Error: " + String(esp_err_to_name(sniffer_err)));
        pcap_logger_deinit(); 
        Serial.println(Minigotchi::getMood().getNeutral() + " PCAP Logger de-initialized after failed sniffer start.");
    }
  } else {
    Serial.println(Minigotchi::getMood().getSad() + " Skipping WiFi sniffer test due to SD card or PCAP logger init failure.");
  }

  Parasite::sendName(); 
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

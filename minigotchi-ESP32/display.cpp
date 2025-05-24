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
 * display.cpp: handles display support
 */

#include "display.h"
#include "display_test.h"
#include "display_diagnostics.h"

#if disp
TFT_eSPI tft; // Define TFT_eSPI object

Adafruit_SSD1306 *Display::ssd1306_adafruit_display = nullptr;
Adafruit_SSD1305 *Display::ssd1305_adafruit_display = nullptr;
U8G2_SSD1306_128X64_NONAME_F_SW_I2C *Display::ssd1306_ideaspark_display =
    nullptr;
U8G2_SH1106_128X64_NONAME_F_SW_I2C *Display::sh1106_adafruit_display = nullptr;
TFT_eSPI *Display::tft_display = nullptr;
#endif

String Display::storedFace = "";
String Display::previousFace = "";

String Display::storedText = "";
String Display::previousText = "";

/**
 * Deletes any pointers if used
 */
Display::~Display() {
#if disp
  if (ssd1306_adafruit_display) {
    delete ssd1306_adafruit_display;
  }
  if (ssd1305_adafruit_display) {
    delete ssd1305_adafruit_display;
  }
  if (ssd1306_ideaspark_display) {
    delete ssd1306_ideaspark_display;
  }
  if (sh1106_adafruit_display) {
    delete sh1106_adafruit_display;
  }
  if (tft_display) {
    delete tft_display;
  }
#endif
}

/**
 * Function to initialize the screen ONLY.
 */
void Display::startScreen() {
#if disp
  if (Config::display) {
    if (Config::screen == "SSD1306") {
      ssd1306_adafruit_display =
          new Adafruit_SSD1306(SSD1306_SCREEN_WIDTH, SSD1306_SCREEN_HEIGHT,
                               &Wire, SSD1306_OLED_RESET);
      delay(100);
      ssd1306_adafruit_display->begin(SSD1306_SWITCHCAPVCC,
                                      0x3C); // for the 128x64 displays
      delay(100);
    } else if (Config::screen == "WEMOS_OLED_SHIELD") {
      ssd1306_adafruit_display =
          new Adafruit_SSD1306(WEMOS_OLED_SHIELD_OLED_RESET);
      delay(100);
      ssd1306_adafruit_display->begin(
          SSD1306_SWITCHCAPVCC,
          0x3C); // initialize with the I2C addr 0x3C (for the 64x48)
      delay(100);
    } else if (Config::screen == "SSD1305") {
      ssd1305_adafruit_display = new Adafruit_SSD1305(
          SSD1305_SCREEN_WIDTH, SSD1305_SCREEN_HEIGHT, &SPI, SSD1305_OLED_DC,
          SSD1305_OLED_RESET, SSD1305_OLED_CS, 7000000UL);
      ssd1305_adafruit_display->begin(SSD1305_I2C_ADDRESS,
                                      0x3c); // initialize with the
      // I2C addr 0x3C (for the 64x48)
      delay(100);
    } else if (Config::screen == "IDEASPARK_SSD1306") {
      ssd1306_ideaspark_display = new U8G2_SSD1306_128X64_NONAME_F_SW_I2C(
          U8G2_R0, IDEASPARK_SSD1306_SCL, IDEASPARK_SSD1306_SDA, U8X8_PIN_NONE);
      delay(100);
      ssd1306_ideaspark_display->begin();
      delay(100);
    } else if (Config::screen == "SH1106") {
      sh1106_adafruit_display = new U8G2_SH1106_128X64_NONAME_F_SW_I2C(
          U8G2_R0, SH1106_SCL, SH1106_SDA, U8X8_PIN_NONE);
      delay(100);
      sh1106_adafruit_display->begin();
      delay(100);    } else if (Config::screen ==
               "CYD") { // Check if the screen configuration is set to "CYD" and      // execute the corresponding code
      tft_display = &tft;
        Serial.println("==== Initializing CYD 2.8\" ILI9341 Display ====");
      
      // Power cycle the display with a long reset sequence
      Serial.println("Performing extended hardware reset sequence...");
      pinMode(TFT_RST, OUTPUT);
      digitalWrite(TFT_RST, HIGH);
      delay(200);  // Longer initial delay
      digitalWrite(TFT_RST, LOW);
      delay(200);  // Longer low pulse
      digitalWrite(TFT_RST, HIGH);
      delay(200);  // Longer stabilization time
      
      // Try to toggle CS line as well (sometimes helps)
      pinMode(TFT_CS, OUTPUT);
      digitalWrite(TFT_CS, HIGH);
      delay(100);
      digitalWrite(TFT_CS, LOW);
      delay(100);
      digitalWrite(TFT_CS, HIGH);
      delay(100);
      
      // Initialize the display with standard method
      Serial.println("Running tft.init()...");
      bool init_result = tft.init();
      if (init_result) {
        Serial.println("Standard TFT init successful!");
      } else {
        Serial.println("Standard TFT init failed! Continuing with manual initialization...");
      }
      
      // Manually send ILI9341 initialization commands from Ghost_ESP
      Serial.println("Sending Ghost_ESP ILI9341 initialization commands...");
      
      // Command sequence from Ghost_ESP ILI9341 driver
      uint8_t cmd_data[16];
      
      // Power control
      cmd_data[0] = 0x00; cmd_data[1] = 0x83; cmd_data[2] = 0x30;
      tft.writecommand(0xCF); for(int i=0; i<3; i++) tft.writedata(cmd_data[i]);
      
      cmd_data[0] = 0x64; cmd_data[1] = 0x03; cmd_data[2] = 0x12; cmd_data[3] = 0x81;
      tft.writecommand(0xED); for(int i=0; i<4; i++) tft.writedata(cmd_data[i]);
      
      cmd_data[0] = 0x85; cmd_data[1] = 0x01; cmd_data[2] = 0x79;
      tft.writecommand(0xE8); for(int i=0; i<3; i++) tft.writedata(cmd_data[i]);
      
      cmd_data[0] = 0x39; cmd_data[1] = 0x2C; cmd_data[2] = 0x00; cmd_data[3] = 0x34; cmd_data[4] = 0x02;
      tft.writecommand(0xCB); for(int i=0; i<5; i++) tft.writedata(cmd_data[i]);
      
      cmd_data[0] = 0x20;
      tft.writecommand(0xF7); tft.writedata(cmd_data[0]);
      
      cmd_data[0] = 0x00; cmd_data[1] = 0x00;
      tft.writecommand(0xEA); for(int i=0; i<2; i++) tft.writedata(cmd_data[i]);
      
      // Power control
      cmd_data[0] = 0x26;
      tft.writecommand(0xC0); tft.writedata(cmd_data[0]);
      
      cmd_data[0] = 0x11;
      tft.writecommand(0xC1); tft.writedata(cmd_data[0]);
      
      // VCOM control
      cmd_data[0] = 0x35; cmd_data[1] = 0x3E;
      tft.writecommand(0xC5); for(int i=0; i<2; i++) tft.writedata(cmd_data[i]);
        cmd_data[0] = 0xBE;
      tft.writecommand(0xC7); tft.writedata(cmd_data[0]);
      
      // Memory Access Control - CRITICAL FOR DISPLAY OPERATION
      // 0x48 - Common for CYD displays (MY=0, MX=1, MV=0, ML=0, BGR=1, MH=0)
      cmd_data[0] = 0x48;  // Try 0x48 for BGR mode instead of 0x28 (RGB mode)
      tft.writecommand(0x36); tft.writedata(cmd_data[0]);
      
      // Pixel Format Set
      cmd_data[0] = 0x55;
      tft.writecommand(0x3A); tft.writedata(cmd_data[0]);
      
      cmd_data[0] = 0x00; cmd_data[1] = 0x1B;
      tft.writecommand(0xB1); for(int i=0; i<2; i++) tft.writedata(cmd_data[i]);
      
      cmd_data[0] = 0x08;
      tft.writecommand(0xF2); tft.writedata(cmd_data[0]);
      
      cmd_data[0] = 0x01;
      tft.writecommand(0x26); tft.writedata(cmd_data[0]);
      
      // Set Gamma
      cmd_data[0] = 0x1F; cmd_data[1] = 0x1A; cmd_data[2] = 0x18; cmd_data[3] = 0x0A; cmd_data[4] = 0x0F;
      cmd_data[5] = 0x06; cmd_data[6] = 0x45; cmd_data[7] = 0x87; cmd_data[8] = 0x32; cmd_data[9] = 0x0A;
      cmd_data[10] = 0x07; cmd_data[11] = 0x02; cmd_data[12] = 0x07; cmd_data[13] = 0x05; cmd_data[14] = 0x00;
      tft.writecommand(0xE0); for(int i=0; i<15; i++) tft.writedata(cmd_data[i]);
      
      cmd_data[0] = 0x00; cmd_data[1] = 0x25; cmd_data[2] = 0x27; cmd_data[3] = 0x05; cmd_data[4] = 0x10;
      cmd_data[5] = 0x09; cmd_data[6] = 0x3A; cmd_data[7] = 0x78; cmd_data[8] = 0x4D; cmd_data[9] = 0x05;
      cmd_data[10] = 0x18; cmd_data[11] = 0x0D; cmd_data[12] = 0x38; cmd_data[13] = 0x3A; cmd_data[14] = 0x1F;
      tft.writecommand(0xE1); for(int i=0; i<15; i++) tft.writedata(cmd_data[i]);
      
      // Column Address Set
      cmd_data[0] = 0x00; cmd_data[1] = 0x00; cmd_data[2] = 0x00; cmd_data[3] = 0xEF;
      tft.writecommand(0x2A); for(int i=0; i<4; i++) tft.writedata(cmd_data[i]);
      
      // Page Address Set
      cmd_data[0] = 0x00; cmd_data[1] = 0x00; cmd_data[2] = 0x01; cmd_data[3] = 0x3F;
      tft.writecommand(0x2B); for(int i=0; i<4; i++) tft.writedata(cmd_data[i]);
      
      // Memory Write
      tft.writecommand(0x2C);
      
      cmd_data[0] = 0x07;
      tft.writecommand(0xB7); tft.writedata(cmd_data[0]);
      
      cmd_data[0] = 0x0A; cmd_data[1] = 0x82; cmd_data[2] = 0x27; cmd_data[3] = 0x00;
      tft.writecommand(0xB6); for(int i=0; i<4; i++) tft.writedata(cmd_data[i]);
      
      // Sleep Out
      tft.writecommand(0x11);
      delay(100);
      
      // Display ON
      tft.writecommand(0x29);
      delay(100);
      
      // Inversion OFF
      tft.writecommand(0x20);
      
      Serial.println("ILI9341 initialization sequence completed!");
      
      // Set up display parameters
      Serial.println("Setting rotation to 3...");
      tft.setRotation(3); // Landscape mode
      Serial.println("Setting text size and datum...");
      tft.setTextSize(1);
      tft.setTextDatum(TL_DATUM);
      
      // Clear display and set black background
      Serial.println("Filling screen with BLACK...");
      tft.fillScreen(TFT_BLACK);
      
      // Try different SPI speeds if needed
      Serial.printf("Current SPI frequency: %d MHz\n", SPI_FREQUENCY/1000000);
      
      // Try a basic screen test with different colors
      Serial.println("Performing basic color tests...");
      tft.fillScreen(TFT_RED);
      delay(500);
      tft.fillScreen(TFT_GREEN);
      delay(500);
      tft.fillScreen(TFT_BLUE);
      delay(500);
      tft.fillScreen(TFT_WHITE);
      delay(500);
      tft.fillScreen(TFT_BLACK);
      delay(500);
      
      // Set backlight PIN - Note: on CYD, backlight is typically on GPIO 32, not 21
      // Trying both common pins for CYD displays
      Serial.println("Setting backlight pins...");
      
      // Try pin 21 (as mentioned in your code)
      pinMode(21, OUTPUT);
      digitalWrite(21, HIGH);
      Serial.println("Backlight on pin 21 set to HIGH");
      
      // Also try pin 32 (common for many CYD displays)
      pinMode(32, OUTPUT);
      digitalWrite(32, HIGH);
      Serial.println("Backlight on pin 32 set to HIGH");
      
      delay(500);
      
      // Draw some basic text directly
      Serial.println("Drawing test text...");
      tft.setTextSize(3);
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(20, 20);
      tft.println("DISPLAY TEST");
      tft.setTextSize(2);
      tft.setCursor(20, 60);
      tft.println("If you can see this");
      tft.setCursor(20, 80);
      tft.println("text, display is");
      tft.setCursor(20, 100);
      tft.println("working!");
      delay(2000);
      
      // Run display tests
      Serial.println("Running display tests...");
      DisplayTest::init(&tft);
      DisplayTest::runDisplayTests();
      
      // Run comprehensive diagnostics
      Serial.println("Running comprehensive display diagnostics...");
      DisplayDiagnostics::init(&tft);
      DisplayDiagnostics::runComprehensiveDiagnostics();
      
      // Welcome message after tests
      tft.fillScreen(TFT_BLACK);
      tft.setTextDatum(MC_DATUM); 
      tft.setTextPadding(0);
      
      tft.setTextSize(3);
      tft.setTextColor(TFT_YELLOW);
      tft.drawString("Minigotchi", tft.width()/2, tft.height()/2 - 40);
      tft.setTextSize(2);
      tft.setTextColor(TFT_WHITE);
      tft.drawString("CYD 2.8\" Display", tft.width()/2, tft.height()/2);
      delay(2000);
      
      tft.fillScreen(TFT_BLACK);
    }else if (Config::screen == "T_DISPLAY_S3") {
      tft_display = &tft;
      tft.begin();
      tft.setRotation(1); // Set display rotation if needed
      delay(100);
    } else {
      ssd1306_adafruit_display =
          new Adafruit_SSD1306(WEMOS_OLED_SHIELD_OLED_RESET);
      delay(100);
      ssd1306_adafruit_display->begin(
          SSD1306_SWITCHCAPVCC,
          0x3C); // initialize with the I2C addr 0x3C (for the 64x48)
      delay(100);
    }

    // initialize w/ delays to prevent crash
    if (Config::screen == "SSD1306" && ssd1306_adafruit_display != nullptr) {
      ssd1306_adafruit_display->display();
      delay(100);
      ssd1306_adafruit_display->clearDisplay();
      delay(100);
      ssd1306_adafruit_display->setTextColor(WHITE);
      delay(100);
    } else if (Config::screen == "SSD1305" &&
               ssd1305_adafruit_display != nullptr) {
      ssd1305_adafruit_display->display();
      delay(100);
      ssd1305_adafruit_display->clearDisplay();
      delay(100);
      ssd1305_adafruit_display->setTextColor(WHITE);
      delay(100);
    } else if (Config::screen == "IDEASPARK_SSD1306" &&
               ssd1306_ideaspark_display != nullptr) {
      ssd1306_ideaspark_display->clearBuffer();
      delay(100);
    } else if (Config::screen == "SH1106" &&
               sh1106_adafruit_display != nullptr) {
      sh1106_adafruit_display->clearBuffer();
      delay(100);
    } else if (Config::screen == "M5STICKCP" ||
               Config::screen == "M5STICKCP2" ||
               Config::screen ==
                   "M5CARDPUTER") { // New condition for M5StickC Plus
      tft.setRotation(1);           // Set display rotation if needed
      tft.begin();                  // Initialize TFT_eSPI library
      delay(100);
      tft.setRotation(1); // Set display rotation if needed
      delay(100);
      tft.fillScreen(TFT_BLACK); // Fill screen with black color
      delay(100);
      tft.setTextColor(TFT_WHITE); // Set text color to white
      delay(100);
      tft.setTextSize(2); // Set text size)
      delay(100);
    }
  }
#endif
}

/** developer note:
 *
 * ssd1305 handling is a lot more different than ssd1306,
 * the screen height is half the expected ssd1306 size.
 *
 * source fork:
 * https://github.com/dkyazzentwatwa/minigotchi-ssd1305-neopixel/blob/main/minigotchi/display.cpp
 *
 */

/**
 * Updates the face ONLY
 * @param face Face to use
 */
void Display::updateDisplay(String face) {
#if disp
  Display::updateDisplay(face, "");
#endif
}

/**
 * Updates the display with both face and text
 * @param face Face to use
 * @param text Additional text under the face
 */
void Display::updateDisplay(String face, String text) {
#if disp
  if (Config::display) {
    if ((Config::screen == "SSD1306" ||
         Config::screen == "WEMOS_OLED_SHIELD") &&
        ssd1306_adafruit_display != nullptr) {
      ssd1306_adafruit_display->setCursor(0, 0);
      delay(5);
      ssd1306_adafruit_display->setTextSize(2);
      delay(5);
      ssd1306_adafruit_display->clearDisplay();
      delay(5);
      ssd1306_adafruit_display->println(face);
      delay(5);
      ssd1306_adafruit_display->setCursor(0, 20);
      delay(5);
      ssd1306_adafruit_display->setTextSize(1);
      delay(5);
      ssd1306_adafruit_display->println(text);
      delay(5);
      ssd1306_adafruit_display->display();
      delay(5);
    } else if (Config::screen == "SSD1305" &&
               ssd1305_adafruit_display != nullptr) {
      ssd1305_adafruit_display->setCursor(32, 0);
      delay(5);
      ssd1305_adafruit_display->setTextSize(2);
      delay(5);
      ssd1305_adafruit_display->clearDisplay();
      delay(5);
      ssd1305_adafruit_display->println(face);
      delay(5);
      ssd1305_adafruit_display->setCursor(0, 15);
      delay(5);
      ssd1305_adafruit_display->setTextSize(1);
      delay(5);
      ssd1305_adafruit_display->println(text);
      delay(5);
      ssd1305_adafruit_display->display();
      delay(5);
    } else if (Config::screen == "IDEASPARK_SSD1306" &&
               ssd1306_ideaspark_display != nullptr) {
      ssd1306_ideaspark_display->clearBuffer();
      delay(5);
      ssd1306_ideaspark_display->setDrawColor(2);
      delay(5);
      ssd1306_ideaspark_display->setFont(u8g2_font_10x20_tr);
      delay(5);
      ssd1306_ideaspark_display->drawStr(0, 15, face.c_str());
      delay(5);
      ssd1306_ideaspark_display->setDrawColor(1);
      delay(5);
      ssd1306_ideaspark_display->setFont(u8g2_font_6x10_tr);
      delay(5);
      Display::printU8G2Data(0, 32, text.c_str());
      delay(5);
      ssd1306_ideaspark_display->sendBuffer();
      delay(5);
    } else if (Config::screen == "SH1106") {
      sh1106_adafruit_display->clearBuffer();
      delay(5);
      sh1106_adafruit_display->setDrawColor(2);
      delay(5);
      sh1106_adafruit_display->setFont(u8g2_font_10x20_tr);
      delay(5);
      sh1106_adafruit_display->drawStr(0, 15, face.c_str());
      delay(5);
      sh1106_adafruit_display->setDrawColor(1);
      delay(5);
      sh1106_adafruit_display->setFont(u8g2_font_6x10_tr);
      delay(5);
      Display::printU8G2Data(0, 32, text.c_str());
      delay(5);
      sh1106_adafruit_display->sendBuffer();
      delay(5);

    } else if (Config::screen == "M5STICKCP" ||
               Config::screen == "M5STICKCP2" ||
               Config::screen ==
                   "M5CARDPUTER") { // New condition for M5 devices
      bool faceChanged = (face != Display::storedFace);
      bool textChanged = (text != Display::storedText);

      if (faceChanged) {
        tft.fillRect(0, 0, tft.width(), 50, TFT_BLACK); // Clear face area
        delay(5);
        tft.setTextColor(TFT_WHITE); // Set text color to white
        delay(5);
        tft.setCursor(0, 0); // Set cursor to start position
        delay(5);
        tft.setTextSize(6); // Set text size for face
        delay(5);
        tft.println(face); // Print face
        delay(5);
        Display::storedFace = face; // Store the new face
      }

      if (textChanged) {
        tft.fillRect(0, 50, tft.width(), tft.height() - 50,
                     TFT_BLACK); // Clear text area
        delay(5);
        tft.setTextColor(TFT_WHITE); // Set text color to white
        delay(5);
        tft.setCursor(0, 50); // Set cursor to start position
        delay(5);
        tft.setTextSize(2); // Set text size for text
        delay(5);
        tft.println(text); // Print text
        delay(5);
        Display::storedText = text; // Store the new text
      }    } else if ((Config::screen == "CYD" || Config::screen == "T_DISPLAY_S3") &&
               tft_display != nullptr) {
      bool faceChanged = (face != Display::storedFace);
      bool textChanged = (text != Display::storedText);      if (faceChanged) {
        int faceHeight = (Config::screen == "CYD") ? 100 : 50;
        tft.fillRect(0, 0, tft.width(), faceHeight,
                     TFT_BLACK); // Clear face area
        tft.setCursor(20, 20);  // Center the face horizontally a bit
        tft.setTextSize((Config::screen == "CYD") ? 8 : 6);  // Larger face for 2.8" display
        tft.setTextColor(TFT_RED); // Make it red for better visibility
        tft.println(face);
        Display::storedFace = face;
        Serial.println("Updated face: " + face);
      }

      if (textChanged) {
        int textY = (Config::screen == "CYD") ? 120 : 50;
        tft.fillRect(0, textY, tft.width(), tft.height() - textY,
                     TFT_BLACK); // Clear text area
        tft.setCursor(10, textY);  // Indent text slightly
        tft.setTextSize((Config::screen == "CYD") ? 2 : 2);
        tft.setTextColor(TFT_WHITE);
        
        // Word wrap for longer text
        int maxCharsPerLine = (Config::screen == "CYD") ? 20 : 15;
        int lineHeight = 20;
        int currentY = textY;
        
        // Break text into chunks of maxCharsPerLine
        for (int i = 0; i < text.length(); i += maxCharsPerLine) {
          String line = text.substring(i, min(i + maxCharsPerLine, (int)text.length()));
          tft.setCursor(10, currentY);
          tft.println(line);
          currentY += lineHeight;
        }
        
        Display::storedText = text;
      }
    }
  }
#endif
}

// If using the U8G2 library, it does not handle wrapping if text is too long to
// fit on the screen So will print text for screens using that library via this
// method to handle line-breaking

/**
 * Handles U8G2 screen formatting.
 * This will only be used if the UG82 related screens are used and applied
 * within the config
 * @param x X value to print data
 * @param y Y value to print data
 * @param data Text to print
 */
void Display::printU8G2Data(int x, int y, const char *data) {
#if disp
  if (Config::screen == "IDEASPARK_SSD1306") {
    auto *screen = static_cast<U8G2_SSD1306_128X64_NONAME_F_SW_I2C *>(
        ssd1306_ideaspark_display);

    int numCharPerLine = screen->getWidth() / screen->getMaxCharWidth();
    if (strlen(data) <= numCharPerLine &&
        screen->getStrWidth(data) <=
            screen->getWidth() - screen->getMaxCharWidth()) {
      screen->drawStr(x, y, data);
    } else {
      int lineNum = 0;
      char buf[numCharPerLine + 1];
      memset(buf, 0, sizeof(buf));
      for (int i = 0; i < strlen(data); ++i) {
        if (data[i] != '\n') {
          buf[strlen(buf)] = data[i];
        }
        if (data[i] == '\n' || strlen(buf) == numCharPerLine ||
            i == strlen(data) - 1 ||
            screen->getStrWidth(buf) >=
                screen->getWidth() - screen->getMaxCharWidth()) {
          buf[strlen(buf)] = '\0';
          screen->drawStr(x, y + (screen->getMaxCharHeight() * lineNum++) + 1,
                          buf);
          memset(buf, 0, sizeof(buf));
        }
      }
    }
  } else if (Config::screen == "SH1106") {
    auto *screen = static_cast<U8G2_SH1106_128X64_NONAME_F_SW_I2C *>(
        sh1106_adafruit_display);

    int numCharPerLine = screen->getWidth() / screen->getMaxCharWidth();
    if (strlen(data) <= numCharPerLine &&
        screen->getStrWidth(data) <=
            screen->getWidth() - screen->getMaxCharWidth()) {
      screen->drawStr(x, y, data);
    } else {
      int lineNum = 0;
      char buf[numCharPerLine + 1];
      memset(buf, 0, sizeof(buf));
      for (int i = 0; i < strlen(data); ++i) {
        if (data[i] != '\n') {
          buf[strlen(buf)] = data[i];
        }
        if (data[i] == '\n' || strlen(buf) == numCharPerLine ||
            i == strlen(data) - 1 ||
            screen->getStrWidth(buf) >=
                screen->getWidth() - screen->getMaxCharWidth()) {
          buf[strlen(buf)] = '\0';
          screen->drawStr(x, y + (screen->getMaxCharHeight() * lineNum++) + 1,
                          buf);
          memset(buf, 0, sizeof(buf));
        }
      }
    }
  } else {
    auto *screen = static_cast<U8G2_SSD1306_128X64_NONAME_F_SW_I2C *>(
        ssd1306_ideaspark_display);

    int numCharPerLine = screen->getWidth() / screen->getMaxCharWidth();
    if (strlen(data) <= numCharPerLine &&
        screen->getStrWidth(data) <=
            screen->getWidth() - screen->getMaxCharWidth()) {
      screen->drawStr(x, y, data);
    } else {
      int lineNum = 0;
      char buf[numCharPerLine + 1];
      memset(buf, 0, sizeof(buf));
      for (int i = 0; i < strlen(data); ++i) {
        if (data[i] != '\n') {
          buf[strlen(buf)] = data[i];
        }
        if (data[i] == '\n' || strlen(buf) == numCharPerLine ||
            i == strlen(data) - 1 ||
            screen->getStrWidth(buf) >=
                screen->getWidth() - screen->getMaxCharWidth()) {
          buf[strlen(buf)] = '\0';
          screen->drawStr(x, y + (screen->getMaxCharHeight() * lineNum++) + 1,
                          buf);
          memset(buf, 0, sizeof(buf));
        }
      }
    }
  }
#endif
}

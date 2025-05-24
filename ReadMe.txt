# Compile the project using the huge_app partition scheme
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app "g:\minigotchi-ESP32-v3.5.3-beta\minigotchi-ESP32"
cd 'g:\minigotchi-ESP32-v3.5.3-beta\minigotchi-ESP32' ; arduino-cli compile --fqbn esp32:esp32:esp32

# Upload to your ESP32 device (after making sure it's connected properly)
arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32:PartitionScheme=huge_app "g:\minigotchi-ESP32-v3.5.3-beta\minigotchi-ESP32"
cd 'g:\minigotchi-ESP32-v3.5.3-beta\minigotchi-ESP32' ; arduino-cli upload -p COM3 --fqbn esp32:esp32:esp32:PartitionScheme=huge_app --verbose

# Run the Seruial Monitor
arduino-cli monitor -p COM3 -b 115200 -v
$port = new-Object System.IO.Ports.SerialPort COM3,115200,None,8,one; $port.Open(); while($true) { if($port.BytesToRead) { $port.ReadExisting() } Start-Sleep -Milliseconds 100 }

# CYD 2.8" ESP32 Display Fix for Minigotchi
Date: May 24, 2025

## Problem Description
The CYD 2.8" ESP32 display with ILI9341 controller was experiencing issues where the backlight worked but no visible output appeared on screen. The display initialization was failing, resulting in a blank screen despite the backlight being operational.

## Root Causes Identified
1. Incorrect display driver configuration in TFT_eSPI library
2. Improper SPI settings for the ILI9341 controller
3. Missing pin assignments for display connections
4. Lack of diagnostic tools to identify issues

## Diagnostic Test Suite Created
Created a comprehensive test suite (`display_test.h`) to systematically diagnose and verify display functionality:
1. Color fill tests - Testing basic rendering with solid colors
2. Text rendering tests - Verifying font rendering capabilities
3. Shape drawing tests - Testing lines, rectangles, circles
4. Color gradient tests - Testing more complex rendering operations

## Configuration Steps to Fix

### 1. TFT_eSPI Library Configuration
Modified `User_Setup.h` in the TFT_eSPI library with the correct parameters:

```cpp
// Select the correct display driver
#define ILI9341_DRIVER       // Generic driver for common displays

// Pin configuration for CYD 2.8" ESP32 display
#define TFT_MISO 19
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   22  // Chip select control pin
#define TFT_DC    5  // Data Command control pin
#define TFT_RST  25  // Reset pin
#define TOUCH_CS 21  // Touch screen chip select

// Display settings
#define TFT_WIDTH  240     // ILI9341 display width
#define TFT_HEIGHT 320     // ILI9341 display height
#define TFT_INVERSION_OFF  // No display inversion
#define TFT_RGB_ORDER TFT_RGB  // Correct color order

// SPI settings
#define SPI_FREQUENCY  40000000  // 40MHz SPI clock
#define SPI_READ_FREQUENCY  20000000  // 20MHz for read operations
#define SPI_TOUCH_FREQUENCY  2500000  // 2.5MHz for touch screen
```

### 2. Added Display Diagnostic Testing
Implemented a test class in `display_test.h` that includes:

```cpp
class DisplayTest {
private:
    static TFT_eSPI* _tft;

public:
    static void init(TFT_eSPI* tft) {
        _tft = tft;
    }

    static void runDisplayTests() {
        _tft->fillScreen(TFT_BLACK);
        delay(500);
        testColorFill();
        testText();
        testShapes();
        testColorGradient();
    }

    // Test methods for different display features
    static void testColorFill() { /* Test code */ }
    static void testText() { /* Test code */ }
    static void testShapes() { /* Test code */ }
    static void testColorGradient() { /* Test code */ }
};
```

### 3. Modified Display Initialization
Updated `display.cpp` to include display tests during initialization:

```cpp
void Display::startScreen() {
    // Initialize with hardware parameters for CYD MicroUSB
    tft.init();
    delay(100);

    // Set up display parameters
    tft.setRotation(3);
    tft.invertDisplay(false);
    delay(100);

    // Clear display and set black background
    tft.fillScreen(TFT_BLACK);
    delay(100);

    // Set up backlight specifically for CYD MicroUSB (pin 21)
    Serial.println("Initializing CYD MicroUSB backlight on pin 21");
    pinMode(21, OUTPUT);
    digitalWrite(21, HIGH);
    Serial.println("CYD MicroUSB backlight set to HIGH");
    delay(100);
    
    // Run display tests
    Serial.println("Running display tests...");
    DisplayTest::init(&tft);
    DisplayTest::runDisplayTests();
    
    // Welcome message after tests
    tft.fillScreen(TFT_BLACK);
    tft.setTextDatum(MC_DATUM); 
    tft.setTextPadding(0);
    
    tft.setTextSize(3);
    tft.setTextColor(TFT_YELLOW);
    tft.drawString("Minigotchi", tft.width()/2, tft.height()/2 - 40);
}
```

### 4. Compilation Challenges
Encountered and resolved compilation issues:
1. Fixed SPI mode configuration issues
2. Addressed TFT_SPI_MODE definition errors
3. Used the "huge_app" partition scheme for the larger firmware size:
   ```
   arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=huge_app
   ```

## Results
The display now works correctly with:
- Proper initialization of the ILI9341 controller
- Correct SPI communication (40MHz frequency)
- Accurate pin configuration for the CYD 2.8" ESP32 display
- Diagnostic tests to verify functionality

The display now shows:
1. Color fill tests (RED, GREEN, BLUE, BLACK)
2. Text rendering with proper fonts
3. Basic shapes (rectangles, circles)
4. Color gradients

Serial output confirms the correct initialization sequence and successful tests.

## Lessons Learned
1. TFT_eSPI library requires careful configuration specific to the display hardware
2. Diagnostic test suites are essential for troubleshooting display issues
3. SPI settings (frequency, mode) must match the display controller specifications
4. For ESP32 with extensive code, larger partition schemes may be necessary
5. Backlight control is separate from display initialization

## Future Recommendations
1. Always include diagnostic tests when implementing display support
2. Document pin connections for easier troubleshooting
3. Keep a backup of working display configurations
4. When upgrading TFT_eSPI or other libraries, verify configurations are still valid
5. For CYD displays, ensure the backlight pin (21) is properly initialized

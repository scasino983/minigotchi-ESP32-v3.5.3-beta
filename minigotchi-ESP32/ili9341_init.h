#ifndef ILI9341_INIT_H
#define ILI9341_INIT_H

#include <Arduino.h>
#include <TFT_eSPI.h>

// Command structure for initialization
typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t databytes; // Number of data bytes; bit 7 = delay after set; 0xFF = end of commands
} lcd_init_cmd_t;

class ILI9341_Init {
public:
    // Initialize the ILI9341 display with recommended initialization sequence
    static bool initialize(TFT_eSPI* tft, int8_t rst_pin = -1) {
        if (!tft) return false;
        
        Serial.println("Starting ILI9341 initialization sequence from Ghost_ESP");
        
        // Define initialization commands
        lcd_init_cmd_t ili_init_cmds[] = {
            // Command, Data array, Length
            {0xCF, {0x00, 0x83, 0X30}, 3},
            {0xED, {0x64, 0x03, 0X12, 0X81}, 4},
            {0xE8, {0x85, 0x01, 0x79}, 3},
            {0xCB, {0x39, 0x2C, 0x00, 0x34, 0x02}, 5},
            {0xF7, {0x20}, 1},
            {0xEA, {0x00, 0x00}, 2},
            {0xC0, {0x26}, 1},          // Power control
            {0xC1, {0x11}, 1},          // Power control
            {0xC5, {0x35, 0x3E}, 2},    // VCOM control
            {0xC7, {0xBE}, 1},          // VCOM control
            {0x36, {0x28}, 1},          // Memory Access Control (orientation)
            {0x3A, {0x55}, 1},          // Pixel Format Set - 16bit pixels
            {0xB1, {0x00, 0x1B}, 2},    // Frame Rate Control
            {0xF2, {0x08}, 1},          // 3Gamma Function Disable
            {0x26, {0x01}, 1},          // Gamma curve selected
            {0xE0, {0x1F, 0x1A, 0x18, 0x0A, 0x0F, 0x06, 0x45, 0X87, 0x32, 0x0A, 0x07, 0x02, 0x07, 0x05, 0x00}, 15}, // Set Gamma
            {0XE1, {0x00, 0x25, 0x27, 0x05, 0x10, 0x09, 0x3A, 0x78, 0x4D, 0x05, 0x18, 0x0D, 0x38, 0x3A, 0x1F}, 15}, // Set Gamma
            {0x2A, {0x00, 0x00, 0x00, 0xEF}, 4}, // Column Address Set (0 to 239)
            {0x2B, {0x00, 0x00, 0x01, 0x3f}, 4}, // Page Address Set (0 to 319)
            {0x2C, {0}, 0},                      // Memory Write
            {0xB7, {0x07}, 1},                  // Entry Mode Set
            {0xB6, {0x0A, 0x82, 0x27, 0x00}, 4}, // Display Function Control
            {0x11, {0}, 0x80},                  // Sleep Out + delay
            {0x29, {0}, 0x80},                  // Display ON + delay
            {0, {0}, 0xff},                     // End of commands
        };
        
        // Hardware reset if reset pin is provided
        if (rst_pin >= 0) {
            Serial.printf("Performing hardware reset on pin %d\n", rst_pin);
            pinMode(rst_pin, OUTPUT);
            digitalWrite(rst_pin, HIGH);
            delay(50);
            digitalWrite(rst_pin, LOW);
            delay(100);
            digitalWrite(rst_pin, HIGH);
            delay(100);
        }
        
        // Call regular TFT_eSPI init first for SPI setup
        bool init_result = tft->init();
        if (!init_result) {
            Serial.println("Warning: Standard TFT init failed, continuing with manual initialization");
        } else {
            Serial.println("Standard TFT init succeeded, enhancing with manual commands");
        }
        
        // Send all initialization commands manually
        Serial.println("Sending ILI9341 initialization commands...");
        uint16_t cmd = 0;
        while (ili_init_cmds[cmd].databytes != 0xff) {
            sendCommand(tft, ili_init_cmds[cmd].cmd);
            if (ili_init_cmds[cmd].databytes & 0x1F) {
                sendData(tft, ili_init_cmds[cmd].data, ili_init_cmds[cmd].databytes & 0x1F);
            }
            if (ili_init_cmds[cmd].databytes & 0x80) {
                delay(100);
            }
            cmd++;
        }
        
        Serial.println("ILI9341 initialization sequence completed");
        
        // Turn off display inversion
        sendCommand(tft, 0x20); // INVOFF - Display Inversion Off
        
        // Set rotation, etc. through standard TFT_eSPI methods
        tft->setRotation(3); // Landscape mode
        
        // Clear display
        tft->fillScreen(TFT_BLACK);
        
        return true;
    }
    
private:
    // Send command to display
    static void sendCommand(TFT_eSPI* tft, uint8_t cmd) {
        tft->writecommand(cmd);
    }
    
    // Send data to display
    static void sendData(TFT_eSPI* tft, const uint8_t* data, int len) {
        for (int i = 0; i < len; i++) {
            tft->writedata(data[i]);
        }
    }
};

#endif // ILI9341_INIT_H

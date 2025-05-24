#ifndef DISPLAY_DIAGNOSTICS_H
#define DISPLAY_DIAGNOSTICS_H

#include <TFT_eSPI.h>

// Extended diagnostic class specifically for troubleshooting display issues
class DisplayDiagnostics {
private:
    static TFT_eSPI* _tft;
    
public:
    static void init(TFT_eSPI* tft) {
        _tft = tft;
    }
    
    // Run comprehensive diagnostics
    static void runComprehensiveDiagnostics() {
        // Clear the display
        _tft->fillScreen(TFT_BLACK);
        delay(500);
        
        // Log the display information
        Serial.println("\n==== Display Diagnostics Information ====");
        Serial.printf("Display Width: %d\n", _tft->width());
        Serial.printf("Display Height: %d\n", _tft->height());
        Serial.printf("Display Rotation: %d\n", _tft->getRotation());
        
        // Test pixel-by-pixel direct writing
        testPixelByPixel();
        
        // Test basic colors with patterns
        testColorPatterns();
        
        // Test different SPI frequencies
        testSPIFrequencies();
        
        // Test with different RGB orders
        testRGBOrders();
        
        // Test with different rotation settings
        testRotations();
    }
    
    // Test pixel-by-pixel writing (very basic operation)
    static void testPixelByPixel() {
        Serial.println("Testing pixel-by-pixel writing...");
        _tft->fillScreen(TFT_BLACK);
        
        // Draw a checkerboard pattern (10x10 pixel squares)
        for (int y = 0; y < _tft->height(); y += 10) {
            for (int x = 0; x < _tft->width(); x += 10) {
                uint16_t color = ((x/10 + y/10) % 2) ? TFT_WHITE : TFT_RED;
                _tft->fillRect(x, y, 10, 10, color);
            }
        }
        
        delay(2000);
        _tft->fillScreen(TFT_BLACK);
        
        // Draw pixel-by-pixel gradient
        for (int y = 0; y < _tft->height(); y += 2) {
            for (int x = 0; x < _tft->width(); x += 2) {
                _tft->drawPixel(x, y, _tft->color565(x % 256, y % 256, (x+y) % 256));
            }
        }
        
        delay(2000);
    }
    
    // Test basic color patterns
    static void testColorPatterns() {
        Serial.println("Testing color patterns...");
        _tft->fillScreen(TFT_BLACK);
        
        // Color bars
        const uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_YELLOW, TFT_MAGENTA, TFT_CYAN, TFT_WHITE};
        int barWidth = _tft->width() / 7;
        
        for (int i = 0; i < 7; i++) {
            _tft->fillRect(i * barWidth, 0, barWidth, _tft->height()/2, colors[i]);
            
            // Add color name text
            _tft->setTextColor(TFT_BLACK);
            _tft->setCursor(i * barWidth + 5, _tft->height()/4);
            switch(i) {
                case 0: _tft->print("RED"); break;
                case 1: _tft->print("GREEN"); break;
                case 2: _tft->print("BLUE"); break;
                case 3: _tft->print("YELLOW"); break;
                case 4: _tft->print("MAGENTA"); break;
                case 5: _tft->print("CYAN"); break;
                case 6: _tft->print("WHITE"); break;
            }
        }
        
        // Grayscale bars
        int grayBarWidth = _tft->width() / 8;
        for (int i = 0; i < 8; i++) {
            uint8_t gray = i * 32; // 0, 32, 64, 96, 128, 160, 192, 224
            _tft->fillRect(i * grayBarWidth, _tft->height()/2, grayBarWidth, _tft->height()/2, _tft->color565(gray, gray, gray));
        }
        
        delay(3000);
    }
    
    // Test with different SPI frequencies
    static void testSPIFrequencies() {
        Serial.println("This is a simulation of testing different SPI frequencies");
        Serial.println("Actual SPI frequency changes require recompilation with different settings");
        
        _tft->fillScreen(TFT_BLACK);
        _tft->setTextColor(TFT_WHITE);
        _tft->setCursor(10, 10);
        _tft->setTextSize(2);
        _tft->println("SPI Frequency Test");
        _tft->setTextSize(1);
        _tft->setCursor(10, 50);
        _tft->println("Current SPI config:");
        _tft->setCursor(10, 70);
        _tft->printf("Main: %d MHz", SPI_FREQUENCY/1000000);
        _tft->setCursor(10, 90);
        _tft->printf("Read: %d MHz", SPI_READ_FREQUENCY/1000000);
        
        delay(3000);
    }
    
    // Test with different RGB orders
    static void testRGBOrders() {
        Serial.println("Testing RGB color patterns to check for correct color order");
        
        _tft->fillScreen(TFT_BLACK);
        _tft->setTextSize(2);
        
        // Pure Red
        _tft->fillRect(0, 0, _tft->width(), 40, TFT_RED);
        _tft->setTextColor(TFT_WHITE);
        _tft->setCursor(10, 10);
        _tft->print("RED");
        
        // Pure Green
        _tft->fillRect(0, 40, _tft->width(), 40, TFT_GREEN);
        _tft->setTextColor(TFT_BLACK);
        _tft->setCursor(10, 50);
        _tft->print("GREEN");
        
        // Pure Blue
        _tft->fillRect(0, 80, _tft->width(), 40, TFT_BLUE);
        _tft->setTextColor(TFT_WHITE);
        _tft->setCursor(10, 90);
        _tft->print("BLUE");
        
        // RGB Test Pattern
        int barWidth = _tft->width() / 3;
        _tft->fillRect(0, 120, barWidth, 40, TFT_RED);
        _tft->fillRect(barWidth, 120, barWidth, 40, TFT_GREEN);
        _tft->fillRect(barWidth*2, 120, barWidth, 40, TFT_BLUE);
        
        delay(3000);
    }
    
    // Test with different rotation settings
    static void testRotations() {
        Serial.println("Testing different rotation settings");
        
        for (int r = 0; r < 4; r++) {
            _tft->fillScreen(TFT_BLACK);
            _tft->setRotation(r);
            
            _tft->setTextSize(3);
            _tft->setTextColor(TFT_WHITE);
            _tft->setCursor(10, 10);
            _tft->printf("Rotation: %d", r);
            
            _tft->setTextSize(2);
            _tft->setCursor(10, 50);
            _tft->printf("Width: %d", _tft->width());
            _tft->setCursor(10, 70);
            _tft->printf("Height: %d", _tft->height());
            
            // Draw a rectangle near the edges to show orientation
            _tft->drawRect(0, 0, _tft->width(), _tft->height(), TFT_RED);
            _tft->fillTriangle(_tft->width()-30, 30, _tft->width()-10, 10, _tft->width()-10, 30, TFT_GREEN);
            
            delay(2000);
        }
        
        // Reset to default rotation
        _tft->setRotation(3);
    }
};

TFT_eSPI* DisplayDiagnostics::_tft = nullptr;

#endif // DISPLAY_DIAGNOSTICS_H

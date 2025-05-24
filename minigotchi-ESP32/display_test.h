#ifndef DISPLAY_TEST_H
#define DISPLAY_TEST_H

#include <TFT_eSPI.h>

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

private:
    static void testColorFill() {
        // Test basic color fills
        _tft->fillScreen(TFT_RED);
        delay(500);
        _tft->fillScreen(TFT_GREEN);
        delay(500);
        _tft->fillScreen(TFT_BLUE);
        delay(500);
        _tft->fillScreen(TFT_BLACK);
        delay(500);
    }

    static void testText() {
        _tft->fillScreen(TFT_BLACK);
        _tft->setTextColor(TFT_WHITE);
        _tft->setTextSize(2);
        _tft->setCursor(10, 10);
        _tft->println("Display Test");
        _tft->setTextSize(1);
        _tft->setCursor(10, 40);
        _tft->println("Testing text rendering...");
        delay(1000);
    }

    static void testShapes() {
        _tft->fillScreen(TFT_BLACK);
        // Draw some shapes
        _tft->drawRect(20, 20, 50, 50, TFT_RED);
        _tft->fillRect(100, 20, 50, 50, TFT_GREEN);
        _tft->drawCircle(45, 100, 25, TFT_BLUE);
        _tft->fillCircle(125, 100, 25, TFT_YELLOW);
        delay(1000);
    }

    static void testColorGradient() {
        _tft->fillScreen(TFT_BLACK);
        // Draw a simple gradient
        for(int i = 0; i < 240; i++) {
            _tft->drawFastVLine(i, 0, 320, _tft->color565(i, 255-i, i%255));
        }
        delay(1000);
    }
};

TFT_eSPI* DisplayTest::_tft = nullptr;

#endif // DISPLAY_TEST_H

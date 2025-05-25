// User_Setup.h for Minigotchi on ESP32-2432S028

#define USER_SETUP_INFO "Minigotchi_ESP32-2432S028_Setup"

// Driver (ILI9341_2_DRIVER is a good start, or ILI9341_DRIVER)
#define ILI9341_2_DRIVER

#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// Pins from YOUR ESP32-2432S028 board's diagram
#define TFT_MISO -1
#define TFT_MOSI 13
#define TFT_SCLK 14
#define TFT_CS   15
#define TFT_DC    2
#define TFT_RST  -1    // Display reset tied to ESP32 EN or 3.3V
#define TFT_BL   21    // Backlight control

// Byte swapping - often needed for CYDs
#define TFT_SWAP_BYTES 1

// Fonts (standard set)
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT

// SPI Settings
#define SPI_FREQUENCY  40000000 // Or 20000000 if 40MHz is unstable
#define SPI_READ_FREQUENCY  20000000
#define SPI_TOUCH_FREQUENCY  2500000 // For touch, if used later

#define TFT_RGB_ORDER TFT_RGB
#define SUPPORT_TRANSACTIONS
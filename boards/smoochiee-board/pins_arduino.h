#ifndef Pins_Arduino_h
#define Pins_Arduino_h

#include "soc/soc_caps.h"
#include <stdint.h>

// ============================================================================
// SERIAL
// ============================================================================
static const uint8_t TX = 1;
static const uint8_t RX = 2;

#define SERIAL_RX 2
#define SERIAL_TX 1
#define USB_as_HID 1

// ============================================================================
// I2C (Grove / PMU)
// ============================================================================
static const uint8_t SDA = 47;
static const uint8_t SCL = 48;

#define GROVE_SDA 47
#define GROVE_SCL 48

// ============================================================================
// LEGACY ARDUINO SPI CONSTANTS (required by arduinoespressif32 core)
// ============================================================================
static const uint8_t SS   = 3;
static const uint8_t MOSI = 12;
static const uint8_t MISO = 11;
static const uint8_t SCK  = 13;

// ============================================================================
// KY-040 ROTARY ENCODER
// ============================================================================
#define BTN_ALIAS "\"OK\""
#define HAS_ENCODER

#define ENC_CLK 17
#define ENC_DT  18
#define ENC_SW  8

#define R_BTN   ENC_CLK
#define L_BTN   ENC_DT
#define SEL_BTN ENC_SW
#define UP_BTN  ENC_DT
#define DW_BTN  ENC_CLK
#define BTN_ACT LOW

// ============================================================================
// STATUS LEDS
// ============================================================================
#define RXLED 4
#define TXLED 5
#define LED_ON HIGH
#define LED_OFF LOW

// ============================================================================
// ST7789 DISPLAY SPI BUS
// ============================================================================
#define HAS_SCREEN 1
#define ROTATION 1
#define MINBRIGHT (uint8_t)1

#define USER_SETUP_LOADED 1
#define ST7789_DRIVER 1
#define TFT_RGB_ORDER 0
#define TFT_WIDTH 170
#define TFT_HEIGHT 320
#define TFT_BACKLIGHT_ON 1

#define TFT_BL  6
#define TFT_RST 16
#define TFT_DC  15
#define TFT_MISO 11
#define TFT_MOSI 12
#define TFT_SCLK 13
#define TFT_CS  7

#define TOUCH_CS -1
#define SMOOTH_FONT 1

#define SPI_FREQUENCY 20000000
#define SPI_READ_FREQUENCY 20000000
#define SPI_TOUCH_FREQUENCY 2500000

// ============================================================================
// MICROSD CARD SPI BUS (Dedicated - separate from display)
// ============================================================================
#define SDCARD_CS   3
#define SDCARD_SCK  40
#define SDCARD_MISO 42
#define SDCARD_MOSI 41

// ============================================================================
// SPI ALIASES (Display bus - for libs that need them)
// ============================================================================
#define SPI_SCK_PIN  13
#define SPI_MOSI_PIN 12
#define SPI_MISO_PIN 11
#define SPI_SS_PIN   43

// ============================================================================
// RF MODULES (disabled but defined for compile compat)
// ============================================================================
#define USE_CC1101_VIA_SPI
#define CC1101_GDO0_PIN 9
#define CC1101_GDO2_PIN 10
#define CC1101_SS_PIN 46
#define CC1101_MOSI_PIN SPI_MOSI_PIN
#define CC1101_SCK_PIN SPI_SCK_PIN
#define CC1101_MISO_PIN SPI_MISO_PIN

#define USE_NRF24_VIA_SPI
#define NRF24_CE_PIN 21
#define NRF24_SS_PIN 14
#define NRF24_MOSI_PIN SPI_MOSI_PIN
#define NRF24_SCK_PIN SPI_SCK_PIN
#define NRF24_MISO_PIN SPI_MISO_PIN

// ============================================================================
// MISC
// ============================================================================
#define FP 1
#define FM 2
#define FG 3

// ============================================================================
// RGB LED
// ============================================================================
#define HAS_RGB_LED 1
#define RGB_LED 45
#define LED_TYPE WS2812B
#define LED_ORDER GRB
#define LED_TYPE_IS_RGBW 0
#define LED_COUNT 16
#define LED_COLOR_STEP 15

// ============================================================================
// PMIC (Battery)
// ============================================================================
#define XPOWERS_CHIP_BQ25896
#define USE_BOOST

// ============================================================================
// MIC
// ============================================================================
#define PIN_CLK 1
#define PIN_DATA 10
#define PIN_WS 2

// ============================================================================
// IO EXPANDER
// ============================================================================
#define USE_IO_EXPANDER
#define IO_EXPANDER_AW9523
#define IO_EXP_GPS 13
#define IO_EXP_MIC 4
#define IO_EXP_VIBRO 2
#define IO_EXP_CC_RX 7
#define IO_EXP_CC_TX 12

#endif /* Pins_Arduino_h */

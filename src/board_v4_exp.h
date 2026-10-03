#pragma once
// Heltec WiFi LoRa 32 V4 on the TFT expansion kit (ST7789 320x240 + CHSC6X
// touch). LoRa and battery pins are the plain V4's (board_v4.h); TFT,
// backlight and touch values are camillia-mt's src/hal/hw_heltec_v4.h
// (camillia-mt 32fea4f). No OLED: the kit uses GPIO17/18/21 for the TFT.

// LoRa — SX1262 behind a front-end module (FEM).
#define LORA_SPI_SCK           9
#define LORA_SPI_MISO         11
#define LORA_SPI_MOSI         10
#define LORA_CS                8
#define LORA_DIO1             14
#define LORA_RST              12
#define LORA_BUSY             13
#define LORA_FEM_POWER_PIN     7   // also the module power hold (BOARD_POWERON)
#define LORA_FEM_ENABLE_PIN    2
#define LORA_FEM_TX_MODE_PIN  46
#define MESH_TCXO_V         1.8f

// Battery — divider on GPIO1, switched by a sense-enable line.
#define BATT_ADC_PIN           1
#define BATT_DIV         5.1205f
#define BATT_SENSE_PIN        37
#define BATT_SENSE_ON        LOW

// VEXT: driven LOW once, as the first thing setup() does (main.cpp), and never
// touched again — displayBegin() does not drive it. camillia-mt's notes follow
// word for word.
// Switched peripheral rail. Drive LOW to enable. Do not change this.
//
// Verified twice on hardware, the second time with no other change in the build:
// driving GPIO36 HIGH makes the touch controller's I2C fail continuously —
//
//     [E][Wire.cpp:499] requestFrom(): i2cWriteReadNonStop returned Error 263
//
// once per second, each timeout blocking lv_timer_handler() for ~1 s (the loop
// probe measured lvgl at 1082 ms x14 in one 15 s window). LOW restores it.
//
// wadamesh's non-R8 Heltec env does set PIN_VEXT_EN=36 / PIN_VEXT_EN_ACTIVE=HIGH
// on the same hardware, which made this look like an unresolved contradiction
// for a long time. It was a red herring: LOW powers this rail correctly, and the
// environment sensors were never unpowered.
//
// The sensors were missing because ENV_SDA/ENV_SCL were swapped (see below), not
// because of this pin. With 4/3 the BME280 answers in ~119 ms with the rail
// driven LOW exactly as it is here. Do not revisit GPIO36 to chase a sensor
// problem; on this unit HIGH only ever broke touch.
#define BOARD_VEXT_ENABLE         36
#define BOARD_VEXT_ON_LEVEL       LOW
// Deliberately NOT using BOARD_VEXT_RAIL_ON_AT_DISPLAY here. Parking the rail
// off and raising it at lcd.init() — matching wadamesh's claim()-then-init
// order — stopped this board booting at all: no serial, no network, no display.
// Every attempt to make GPIO36 behave the way that port describes has made this
// unit worse. LOW from the top of setup() is the only configuration observed to
// boot and keep touch working.
#define VEXT_PIN              BOARD_VEXT_ENABLE
#define VEXT_ON_LEVEL         BOARD_VEXT_ON_LEVEL

// TFT — ST7789, 240x320 native, driven landscape 320x240 (rotation 3).
#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_SCK               17
#define TFT_SPI_MISO              -1   // Write-only (3-wire SPI)
#define TFT_SPI_MOSI              33
#define TFT_SPI_3WIRE           true
#define TFT_SPI_WRITE_HZ     40000000
#define TFT_SPI_READ_HZ       4000000
#define TFT_CS                    15
#define TFT_DC                    16
#define TFT_RST                   18
#define TFT_PANEL_WIDTH          240
#define TFT_PANEL_HEIGHT         320
#define TFT_PANEL_OFFSET_X         0
#define TFT_PANEL_OFFSET_Y         0
#define TFT_INVERT              true
#define TFT_RGB_ORDER          false
#define TFT_ROTATION_LANDSCAPE     3
#define TFT_LANDSCAPE_W          320
#define TFT_LANDSCAPE_H          240

// Backlight — LEDC PWM.
#define TFT_BL                    21
#define TFT_BL_INVERT          false
#define TFT_BL_FREQ            44100
#define TFT_BL_PWM_CH              7
#define TFT_BRIGHTNESS_DEFAULT   160

// Capacitive touch — CHSC6X on Wire1, polled (no INT line).
#define TOUCH_SDA                 47
#define TOUCH_SCL                 48
#define TOUCH_ADDR              0x2E
#define TOUCH_INT                 -1
#define TOUCH_RST                 44
#define TOUCH_I2C_PORT             1   // Wire1

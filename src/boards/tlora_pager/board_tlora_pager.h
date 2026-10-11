#pragma once
// LilyGo T-LoRa Pager (tlora-pager-tft). Values are camillia-mt's
// src/hal/hw_tlora_pager.h (camillia-mt 3777e74), trimmed to what this build
// uses; rotation from that tree's src/hal/board.h (TFT_ROTATION_DEFAULT 3 for
// DEVICE_TLORA_PAGER_TFT).
//
// LoRa, TFT and micro-SD share one SPI bus (SCK 35 / MISO 33 / MOSI 34) with
// separate chip selects, so board_init_tlora_pager.cpp parks every CS high
// before the display starts clocking it. The radio's power rail (and GPS, SD,
// keyboard) sits behind the XL9555 expander on the KB_SDA/KB_SCL bus; the
// vendored mesh_radio.cpp arms it (pagerPrimeLoRaRail) just before the SX1262
// probe, exactly as camillia-mt does. The panel and backlight are not gated.
//
// UNTESTED ON HARDWARE — build-verified only.

// No GPIO power rail; everything switched is on the XL9555.
#define BOARD_POWERON             -1

// LoRa — SX1262 (the LR1121 SKU is not built): TCXO on DIO3, no FEM.
#define LORA_SPI_SCK              35
#define LORA_SPI_MISO             33
#define LORA_SPI_MOSI             34
#define LORA_CS                   36
#define LORA_DIO1                 14
#define LORA_RST                  47
#define LORA_BUSY                 48
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1
#ifndef PAGER_LORA_USE_LR1121
#define PAGER_LORA_USE_LR1121      0
#endif
#define MESH_TCXO_V             3.0f

// micro-SD on the shared bus; unused, deselected at boot.
#define SD_CS                     21

// Main I2C bus: XL9555 expander, BQ25896 charger, TCA8418 keyboard, ES8311.
// camillia-mt names it after the keyboard; mesh_radio.cpp uses these names.
#define KB_SDA                     3
#define KB_SCL                     2
#define KB_BL                     46   // keyboard backlight, active HIGH; held off here

// TFT — ST7796, 222x480 native, driven landscape 480x222. SPI2 is the same
// host Arduino's SPI (FSPI) gives the radio; LovyanGFX's use_lock serialises.
#define TFT_SPI_HOST          SPI2_HOST
#define TFT_SPI_SCK      LORA_SPI_SCK
#define TFT_SPI_MISO    LORA_SPI_MISO
#define TFT_SPI_MOSI    LORA_SPI_MOSI
#define TFT_SPI_3WIRE          false
#define TFT_SPI_WRITE_HZ    40000000
#define TFT_SPI_READ_HZ     16000000
#define TFT_CS                    38
#define TFT_DC                    37
#define TFT_RST                   -1
#define TFT_PANEL_WIDTH          222
#define TFT_PANEL_HEIGHT         480
#define TFT_PANEL_OFFSET_X        49
#define TFT_PANEL_OFFSET_Y         0
#define TFT_INVERT              true
#define TFT_RGB_ORDER          false
#define TFT_ROTATION_LANDSCAPE     3
#define TFT_LANDSCAPE_W          480
#define TFT_LANDSCAPE_H          222

// Backlight — LEDC PWM.
#define TFT_BL                    42
#define TFT_BL_INVERT          false
#define TFT_BL_FREQ            12000
#define TFT_BL_PWM_CH              7
#define TFT_BRIGHTNESS_DEFAULT   130

// Rotary wheel press, active LOW: the Wake button (camillia-mt's TBALL_CLICK,
// one of the pager's screen-wake lines). No touch panel on this board.
#define WAKE_BUTTON_PIN            7

// Battery — BQ25896 charger ADC over I2C (KB_SDA/KB_SCL); no ADC pin.
#define BQ25896_ADDR            0x6B

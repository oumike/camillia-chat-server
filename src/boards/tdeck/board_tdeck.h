#pragma once
// LilyGo T-Deck. Values are camillia-mt's src/hal/hw_tdeck.h (camillia-mt
// 3777e74), trimmed to what this build uses; rotation from that tree's
// src/hal/board.h (TFT_ROTATION_DEFAULT 1 for DEVICE_TDECK).
//
// LoRa, TFT and micro-SD share one SPI bus (SCK 40 / MISO 38 / MOSI 41) with
// separate chip selects, so board_init_tdeck.cpp parks every CS high before
// the display starts clocking it. Nothing on the board (radio, panel, touch,
// keyboard) is powered until BOARD_POWERON is driven HIGH.
//
// UNTESTED ON HARDWARE — build-verified only.

// Peripheral power rail, held HIGH from the first statement of setup().
#define BOARD_POWERON             10

// LoRa — SX1262: TCXO on DIO3, DIO2 as the RF switch, no FEM.
#define LORA_SPI_SCK              40
#define LORA_SPI_MISO             38
#define LORA_SPI_MOSI             41
#define LORA_CS                    9
#define LORA_DIO1                 45
#define LORA_RST                  17
#define LORA_BUSY                 13
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1
#define MESH_TCXO_V             1.6f

// micro-SD on the shared bus; unused, deselected at boot.
#define SD_CS                     39

// TFT — ST7789, 240x320 native, driven landscape 320x240. SPI2 is the same
// host Arduino's SPI (FSPI) gives the radio; LovyanGFX's use_lock serialises.
#define TFT_SPI_HOST          SPI2_HOST
#define TFT_SPI_SCK      LORA_SPI_SCK
#define TFT_SPI_MISO    LORA_SPI_MISO
#define TFT_SPI_MOSI    LORA_SPI_MOSI
#define TFT_SPI_3WIRE          false
#define TFT_SPI_WRITE_HZ    40000000
#define TFT_SPI_READ_HZ      1000000
#define TFT_CS                    12
#define TFT_DC                    11
#define TFT_RST                   -1
#define TFT_PANEL_WIDTH          240
#define TFT_PANEL_HEIGHT         320
#define TFT_PANEL_OFFSET_X         0
#define TFT_PANEL_OFFSET_Y         0
#define TFT_INVERT              true
#define TFT_RGB_ORDER          false
#define TFT_ROTATION_LANDSCAPE     1
#define TFT_LANDSCAPE_W          320
#define TFT_LANDSCAPE_H          240

// Backlight — LEDC PWM.
#define TFT_BL                    42
#define TFT_BL_INVERT          false
#define TFT_BL_FREQ            12000
#define TFT_BL_PWM_CH              0
#define TFT_BRIGHTNESS_DEFAULT   128

// Capacitive touch — GT911 on Wire (shared with the keyboard at 0x55). No
// reset line; INT on GPIO16.
#define TOUCH_SDA                 18
#define TOUCH_SCL                  8
#define TOUCH_ADDR              0x5D
#define TOUCH_INT                 16
#define TOUCH_RST                 -1
#define TOUCH_I2C_PORT             0   // Wire
#define TOUCH_I2C_FREQ        400000

// Trackball centre click, active LOW: the Wake button (camillia-mt's only
// T-Deck wake gesture).
#define WAKE_BUTTON_PIN            0

// Battery — fixed 100k/100k divider on GPIO4, no sense switch. camillia-mt's
// BATT_CAL (1.03) compensates the divider's high source impedance; it is
// folded into BATT_DIV for battery_adc.cpp.
#define BATT_ADC_PIN               4
#define BATT_DIV        (2.0f * 1.03f)
#define BATT_SENSE_PIN            -1
#define BATT_SENSE_ON            LOW

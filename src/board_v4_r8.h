#pragma once
// Heltec WiFi LoRa 32 V4-R8 on the Expansion Kit V2 (ST7789 320x240 + CHSC6X
// touch). Values are camillia-mt's src/hal/hw_heltec_r8.h, which sources them
// from wadamesh's heltec_v4_r8 env, Meshtastic's variant and Heltec's
// schematics; see that file for per-pin provenance.
//
// Not the V4 pin map with a flag: the R8's octal PSRAM takes GPIO33-37 (so
// VEXT, the battery sense line and the v1 kit's TFT MOSI all move or vanish),
// and the V2 kit rewires display and touch.
//
// UNTESTED ON HARDWARE — build-verified only.

// LoRa — SX1262, wired as on the V4.
#define LORA_SPI_SCK           9
#define LORA_SPI_MISO         11
#define LORA_SPI_MOSI         10
#define LORA_CS                8
#define LORA_DIO1             14
#define LORA_RST              12
#define LORA_BUSY             13
// FEM: POWER is sourced; ENABLE is inherited from the V4; TX_MODE is derived
// (the V4's GPIO46 is the R8's LED; Heltec lists PA_CTX on GPIO5). If TX or RX
// is dead on a first unit, check these two first. -1 leaves a line undriven.
#define LORA_FEM_POWER_PIN     7   // also the module power hold
#define LORA_FEM_ENABLE_PIN    2
#define LORA_FEM_TX_MODE_PIN   5
#define MESH_TCXO_V         1.8f

// Battery — the R8 has no ADC_Ctrl: the divider is always connected.
#define BATT_ADC_PIN           1
#define BATT_DIV           5.07f
#define BATT_SENSE_PIN        -1
#define BATT_SENSE_ON        LOW

// Switched peripheral rail, active LOW as on the V4; moved 36 -> 40 because 36
// is inside the octal PSRAM range.
#define VEXT_PIN              40
#define VEXT_ON_LEVEL        LOW

// micro-SD chip select on the display SPI bus. GPIO3 is a strapping pin; it is
// parked HIGH before the display starts so the (unused) card stays off the bus.
#define SD_CS                  3

// TFT — ST7789, 240x320 native, driven landscape 320x240. Four-wire: the V2
// kit brings MISO out on GPIO45.
#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_SCK               16
#define TFT_SPI_MISO              45
#define TFT_SPI_MOSI              15
#define TFT_SPI_3WIRE          false
#define TFT_SPI_WRITE_HZ     40000000   // wadamesh: fall back to 26.6 MHz if bands tear
#define TFT_SPI_READ_HZ       4000000
#define TFT_CS                    47
#define TFT_DC                    48
#define TFT_RST                   21   // also resets the touch controller
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
#define TFT_BL                    44
#define TFT_BL_INVERT          false
#define TFT_BL_FREQ            44100
#define TFT_BL_PWM_CH              7
#define TFT_BRIGHTNESS_DEFAULT   160

// Capacitive touch — CHSC6X on Wire (GPIO17/18, the kit's unified I2C bus).
// TOUCH_RST is -1 on purpose: its reset is GPIO21, shared with the TFT, and a
// touch driver pulsing it would wipe the panel's init sequence.
#define TOUCH_SDA                 17
#define TOUCH_SCL                 18
#define TOUCH_ADDR              0x2E
#define TOUCH_INT                 43
#define TOUCH_RST                 -1
#define TOUCH_I2C_PORT             0   // Wire

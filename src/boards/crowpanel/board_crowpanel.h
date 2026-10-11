#pragma once
// Elecrow CrowPanel Advance 3.5" (crowpanel-35, DEVICE_CROWPANEL_35) and 5.0"
// (crowpanel-50, DEVICE_CROWPANEL_50) HMI boards with the SX1262 module in the
// J8/J9 wireless slot. Both are ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB octal
// PSRAM). Values are camillia-mt's src/hal/hw_crowpanel_35.h and
// src/hal/hw_crowpanel_50.h (camillia-mt 3777e74), trimmed to what this build
// uses; rotations from that tree's src/hal/board.h (TFT_ROTATION_LANDSCAPE).
//
// Neither board has battery sensing (battery_none.cpp) or a free user button:
// BOOT (GPIO0) is the radio's NSS on the 3.5" and on the 5.0" V1.0.
//
// UNTESTED ON HARDWARE — build-verified only.
#include <stdint.h>

#if defined(DEVICE_CROWPANEL_35) && defined(DEVICE_CROWPANEL_50)
#error "define only one of DEVICE_CROWPANEL_35 / DEVICE_CROWPANEL_50"
#endif

// Both: SX1262 with TCXO on DIO3 at 3.3 V, DIO2 as the RF switch, no FEM.
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1
#define MESH_TCXO_V             3.3f

// Both: GT911 capacitive touch on I2C port 0, SDA 15 / SCL 16.
#define TOUCH_SDA                 15
#define TOUCH_SCL                 16
#define TOUCH_ADDR              0x5D
#define TOUCH_I2C_PORT             0
#define TOUCH_I2C_FREQ        400000

#if defined(DEVICE_CROWPANEL_35)
// ── 3.5" ─────────────────────────────────────────────────────────────────────

// LoRa — expansion slot. IO45 selects the slot: LOW routes it to the radio,
// HIGH to the microphone (mesh_radio.cpp drives it before SPI.begin()).
#define LORA_SPI_SCK              10
#define LORA_SPI_MISO              9
#define LORA_SPI_MOSI              3
#define LORA_CS                    0
#define LORA_DIO1                  1
#define LORA_RST                   2
#define LORA_BUSY                 46
#define LORA_POWER_ENABLE_PIN     45
#define LORA_POWER_ENABLE_LEVEL  LOW

// microSD chip select (soft-SPI card on 5/4/6); unused, deselected at boot.
#define SD_CS                      7

// TFT — ILI9488, 320x480 native portrait, run as 480x320 landscape. SPI3,
// because the radio has Arduino's SPI (SPI2/FSPI). Write-only bus.
#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_SCK               42
#define TFT_SPI_MISO              -1
#define TFT_SPI_MOSI              39
#define TFT_SPI_3WIRE          false
#define TFT_SPI_WRITE_HZ    40000000   // Meshtastic runs 60 MHz; camillia-mt starts conservative
#define TFT_SPI_READ_HZ     16000000
#define TFT_CS                    40
#define TFT_DC                    41
#define TFT_RST                   -1
#define TFT_PANEL_WIDTH          320
#define TFT_PANEL_HEIGHT         480
#define TFT_PANEL_OFFSET_X         0
#define TFT_PANEL_OFFSET_Y         0
#define TFT_INVERT              true
#define TFT_RGB_ORDER          false
// 3 is landscape the right way up on hardware (1 was upside down, per
// camillia-mt); LovyanGFX rotates the GT911 with it.
#define TFT_ROTATION_LANDSCAPE     3
#define TFT_LANDSCAPE_W          480
#define TFT_LANDSCAPE_H          320

// Backlight — LEDC PWM.
#define TFT_BL                    38
#define TFT_BL_INVERT          false
#define TFT_BL_FREQ            44000
#define TFT_BL_PWM_CH              7
#define TFT_BRIGHTNESS_DEFAULT   160

// Touch INT and reset are wired to GPIOs on this size.
#define TOUCH_INT                 47
#define TOUCH_RST                 48

#elif defined(DEVICE_CROWPANEL_50)
// ── 5.0" ─────────────────────────────────────────────────────────────────────

// Hardware revision. V1.0 has the radio's NSS on GPIO0 and a PCA9557 (0x18)
// for backlight and touch reset; from V1.1 a board MCU at 0x30 does those, and
// from V1.2 NSS is GPIO8. The display side detects which controller answers;
// the radio cannot, so -DCROWPANEL_50_HW_V10=1 builds for V1.0.
#ifndef CROWPANEL_50_HW_V10
#define CROWPANEL_50_HW_V10        0
#endif

// LoRa — wireless-module slot. GPIO 4/5/6 and 19/20/2 also reach the SD card
// and the audio parts through CH486F switches set by the DIP switch on the
// back: it must be in the wireless-module position. GPIO19/20 are the S3's
// native USB pins, which is why the console is UART0 (CH340K) on this board.
#define LORA_SPI_SCK               5
#define LORA_SPI_MISO              4
#define LORA_SPI_MOSI              6
#if CROWPANEL_50_HW_V10
#define LORA_CS                    0
#else
#define LORA_CS                    8
#endif
#define LORA_DIO1                 20
#define LORA_RST                  19
#define LORA_BUSY                  2
#define LORA_POWER_ENABLE_PIN     -1   // the DIP switch selects the slot, not a GPIO
#define LORA_POWER_ENABLE_LEVEL  LOW

// TFT — 800x480 ST7262 on the 16-bit RGB bus, scanned out of a PSRAM
// framebuffer. Bus pins and timings are in crowpanel_50_display.h.
#define TFT_PANEL_WIDTH          800
#define TFT_PANEL_HEIGHT         480
#define TFT_ROTATION_LANDSCAPE     0
#define TFT_LANDSCAPE_W          800
#define TFT_LANDSCAPE_H          480
// Full on: from V1.1 the board MCU dims by PWM and anything less makes the
// backlight driver whine.
#define TFT_BRIGHTNESS_DEFAULT   255
// Pixel clock. Elecrow's demo runs 21 MHz, Meshtastic and camillia-mt 14 MHz:
// the lower clock leaves PSRAM bandwidth for WiFi and the radio while the
// panel scans out of the same memory, which is what stops RGB drift/tearing.
#define CROWPANEL_PCLK_HZ   14000000

// Board MCU (V1.1+), single-byte commands at 0x30 (V1.2/V1.3 values):
// 0 brightest .. 244 dimmest, 245 backlight off, 250 wakes the touch panel.
#define CROWPANEL_MCU_ADDR      0x30
#define CROWPANEL_MCU_BL_MAX       0
#define CROWPANEL_MCU_BL_MIN     244
#define CROWPANEL_MCU_BL_OFF     245
#define CROWPANEL_MCU_TOUCH_ON   250

// PCA9557 expander (V1.0).
#define CROWPANEL_IOEX_ADDR     0x18
#define CROWPANEL_IOEX_BL          1   // backlight driver enable, HIGH = on
#define CROWPANEL_IOEX_TP_RST      2   // GT911 reset, LOW = in reset
#define CROWPANEL_IOEX_AMP_MUTE    3   // speaker amplifier, held as Elecrow leaves it
#define CROWPANEL_IOEX_AMP_SHUT    4

// Touch: INT (GPIO1) is only the GT911 address strap, the touch is polled;
// reset belongs to the expander / board MCU.
#define TOUCH_INT                  1
#define TOUCH_RST                 -1

#else
#error "board_crowpanel.h needs DEVICE_CROWPANEL_35 or DEVICE_CROWPANEL_50"
#endif

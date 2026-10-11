#pragma once
// Elecrow ThinkNode M9. Values are camillia-mt's src/hal/hw_m9.h (camillia-mt
// 3777e74), trimmed to what this build uses; see that file for per-pin
// provenance (schematic, cross-checked against a hardware-verified MeshCore
// port). Rotation from that tree's src/hal/board.h (TFT_ROTATION_DEFAULT 3 for
// DEVICE_M9, hardware-verified: keyboard below the screen).
//
// LoRa (LR1110), TFT and micro-SD share one SPI bus (SCK 40 / MISO 38 /
// MOSI 47) with separate chip selects, so board_init_m9.cpp parks every CS
// high before the display starts clocking it.
//
// The console is an external UART bridge, not native USB-CDC (envs/m9.ini
// builds with ARDUINO_USB_CDC_ON_BOOT=0).
//
// UNTESTED ON HARDWARE — build-verified only.

// Peripheral rail (LCD / GPS / sensors): P-MOS, active LOW.
#define BOARD_VEXT_ENABLE         18
#define BOARD_VEXT_ON_LEVEL      LOW

// Passive buzzer; parked low so it stays silent.
#define BOARD_BUZZER               9

// GPS (CC1167Q): unused here, so its enable is held off. Enable is active LOW;
// reset goes through an NPN, so LOW releases it.
#define GPS_ENABLE_PIN            11
#define GPS_ENABLE_ON_LEVEL      LOW
#define GPS_RESET_PIN              5
#define GPS_RESET_ACTIVE_LEVEL  HIGH

// LoRa — LR1110. TCXO 3.3 V (hardware-confirmed in camillia-mt). The RF switch
// is driven from the chip's own DIO5/DIO6; MESH_LR11XX_RFSWITCH_DIO56 makes
// mesh_radio.cpp hand RadioLib that table.
#define LORA_SPI_SCK              40
#define LORA_SPI_MISO             38
#define LORA_SPI_MOSI             47
#define LORA_CS                   39
#define LORA_DIO1                 42
#define LORA_RST                  45
#define LORA_BUSY                 41
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1
#define MESH_LR11XX_RFSWITCH_DIO56 1
#define MESH_LORA_LR1110           1
#define MESH_TCXO_V             3.3f

// micro-SD on the shared bus; unused, deselected at boot.
#define SD_CS                     48

// TFT — ST7789, 240x320 native, driven landscape 320x240.
#define TFT_SPI_HOST          SPI2_HOST
#define TFT_SPI_SCK      LORA_SPI_SCK
#define TFT_SPI_MISO    LORA_SPI_MISO
#define TFT_SPI_MOSI    LORA_SPI_MOSI
#define TFT_SPI_3WIRE          false
#define TFT_SPI_WRITE_HZ    40000000
#define TFT_SPI_READ_HZ      1000000
#define TFT_CS                    16
#define TFT_DC                    15
#define TFT_RST                   14
#define TFT_PANEL_WIDTH          240
#define TFT_PANEL_HEIGHT         320
#define TFT_PANEL_OFFSET_X         0
#define TFT_PANEL_OFFSET_Y         0
#define TFT_INVERT              true
#define TFT_RGB_ORDER          false
#define TFT_ROTATION_LANDSCAPE     3
#define TFT_LANDSCAPE_W          320
#define TFT_LANDSCAPE_H          240

// Backlight — LEDC PWM through a PNP, so active LOW (TFT_BL_INVERT). Channel 7
// as in camillia-mt, away from the low channels Arduino hands out first.
#define TFT_BL                    17
#define TFT_BL_INVERT           true
#define TFT_BL_FREQ            12000
#define TFT_BL_PWM_CH              7
#define TFT_BRIGHTNESS_DEFAULT   128

// No touch panel. The keyboard (I2C companion MCU at 0x6c on GPIO20/21) is not
// used; the BOOT button is the Wake button, active LOW.
#define WAKE_BUTTON_PIN            0

// Battery — plain 1:2 divider on GPIO13, no sense switch.
#define BATT_ADC_PIN              13
#define BATT_DIV                2.0f
#define BATT_SENSE_PIN            -1
#define BATT_SENSE_ON            LOW

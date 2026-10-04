#pragma once
// Seeed Wio Tracker L2. Values are camillia-mt's src/hal/hw_wio_tracker_l2.h
// (camillia-mt ec81772), trimmed to what this build uses. A PCA9555 expander
// gates the LCD, touch, GNSS, SD, audio and battery-sense rails, so nothing
// on the board answers until board_init_wio_l2.cpp has brought it up.
#include <stdint.h>

// LoRa — SX1262: TCXO on DIO3 at 1.8 V, DIO2 as the RF switch, no FEM.
#define LORA_SPI_SCK               4
#define LORA_SPI_MISO              5
#define LORA_SPI_MOSI              6
#define LORA_CS                   21
#define LORA_RST                   7
#define LORA_DIO1                  9
#define LORA_BUSY                  8
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1
#define MESH_TCXO_V             1.8f

// Shared I2C (Wire): expander, GT911, LP5814 and ADS1115.
#define BOARD_I2C_SDA             47
#define BOARD_I2C_SCL             48
#define BOARD_I2C_PORT             0
#define BOARD_I2C_FREQ        100000

// I/O expander — PCA9555. Inputs are marked; the rest are outputs.
#define EXPANDER_ADDR           0x21
#define EXPANDER_INT              45   // active-low, shared interrupt
#define EXP_BIT_WAKE_BUTTON        0   // in, active-low top Wake button
#define EXP_BIT_I2C_INT            1   // in
#define EXP_BIT_SD_DETECT          2   // in
#define EXP_BIT_TOUCH_INT          3   // driven low; touch is polled
#define EXP_BIT_LCD_CS             4   // expander-side LCD control, held high
#define EXP_BIT_LCD_POWER          5
#define EXP_BIT_LCD_RST            6
#define EXP_BIT_GROVE_POWER        7
#define EXP_BIT_TOUCH_RST          8
#define EXP_BIT_GNSS_RST           9   // active-high
#define EXP_BIT_USER_LED          10
#define EXP_BIT_USB_OTG_EN        11
#define EXP_BIT_AUDIO_PA_POWER    12
#define EXP_BIT_GNSS_POWER        13
#define EXP_BIT_SD_POWER          14
#define EXP_BIT_BATT_SENSE_EN     15   // active-high

// TFT — NV3031B 240x320 on QUAD-SPI, no DC line, SPI mode 3. Panel reset is
// expander bit 6. Panel_NV3031B first appears in LovyanGFX 1.2.27.
#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_SCK               42
#define TFT_QSPI_IO0              41
#define TFT_QSPI_IO1              40
#define TFT_QSPI_IO2              39
#define TFT_QSPI_IO3              38
#define TFT_CS                    46
#define TFT_RST                   -1
#define TFT_SPI_MODE               3
#define TFT_SPI_WRITE_HZ    75000000
#define TFT_SPI_READ_HZ     16000000
#define TFT_PANEL_WIDTH          240
#define TFT_PANEL_HEIGHT         320
#define TFT_PANEL_OFFSET_X         0
#define TFT_PANEL_OFFSET_Y         0
#define TFT_INVERT              true
#define TFT_RGB_ORDER           true
// LovyanGFX adds the panel's offset_rotation (1) to the logical rotation, so
// logical 0 is internal rotation 1: landscape 320x240.
#define TFT_PANEL_OFFSET_ROTATION  1
#define TFT_ROTATION_LANDSCAPE     0
#define TFT_LANDSCAPE_W          320
#define TFT_LANDSCAPE_H          240
#define TFT_BRIGHTNESS_DEFAULT   160

// Backlight — LP5814 I2C LED driver, not a PWM pin. Initialise it before the
// GT911, and reset Wire after GT911 init: a NACK in the GT911 probe can leave
// the I2C peripheral busy, and the next backlight write then times out.
#define LP5814_ADDR             0x2c
#define LP5814_REG_DEVICE_CONFIG0 0x00
#define LP5814_REG_MAX_CURRENT    0x01
#define LP5814_REG_ENABLE_CONTROL 0x02
#define LP5814_REG_DIM_MODE       0x04
#define LP5814_REG_ENGINE_MODE    0x05
#define LP5814_REG_UPDATE         0x0F
#define LP5814_REG_LED0_DC        0x14
#define LP5814_REG_LED0_PWM       0x18
#define LP5814_LED_DC_VALUE        200

// Capacitive touch — GT911 on the shared bus. INT is expander bit 3 and reset
// expander bit 8, so LovyanGFX gets neither.
#define TOUCH_SDA         BOARD_I2C_SDA
#define TOUCH_SCL         BOARD_I2C_SCL
#define TOUCH_ADDR              0x5D
#define TOUCH_INT                 -1
#define TOUCH_I2C_PORT    BOARD_I2C_PORT
#define TOUCH_I2C_FREQ    BOARD_I2C_FREQ
#define TOUCH_OFFSET_ROTATION      2

// Battery — ADS1115 AIN0 sees battery/2 while expander bit 15 is on.
#define ADS1115_ADDR            0x48
#define ADS1115_BATT_CHANNEL       0
#define BATT_DIV                2.0f

// Expander access, defined in board_init_wio_l2.cpp. All are no-ops (Read
// returns false) when the expander did not come up.
bool boardExpanderOk();
bool boardExpanderRead(uint16_t &in);           // input ports, bit n = expander pin n
void boardExpanderSet(uint8_t bit, bool on);    // drive one output bit

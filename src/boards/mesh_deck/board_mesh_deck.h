#pragma once
// Attaky Mesh Deck 1.0 (ESP32 core + LoRa/GPS expand module + Standard Cell).
// Values are camillia-mt's src/hal/hw_mesh_deck.h (camillia-mt 3777e74),
// trimmed to what this build uses; rotation from that tree's src/hal/board.h
// (TFT_ROTATION_DEFAULT 3 for DEVICE_MESH_DECK). See that file for how each
// pin was established (wadamesh attaky_mesh_series variant + Attaky's frame
// connector docs).
//
// The frame keeps every rail on: there is no power latch or VEXT. What does
// need bringing up is the FT6636 touch controller, whose reset is AW9523
// expander 0x59 P13 (see board_init_mesh_deck.cpp).

// LoRa — Ra-01SH (SX1262) on its own SPI bus. Plain 32 MHz crystal, not a
// TCXO, so DIO3 must not be driven as a TCXO supply (lib/camillia_mesh/config.h
// sets MESH_RADIO_HAS_TCXO 0 for DEVICE_MESH_DECK). DIO2 drives the RF switch.
#define LORA_SPI_SCK               5
#define LORA_SPI_MISO              7
#define LORA_SPI_MOSI              6
#define LORA_CS                    8
#define LORA_DIO1                 21
#define LORA_RST                   9
#define LORA_BUSY                 16
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1
#define MESH_TCXO_V             0.0f

// Main I2C bus: touch, AW9523 expanders, MAX17048 fuel gauge, IMU, ALS, keyboard.
#define I2C_SDA                    2
#define I2C_SCL                    1
#define I2C_FREQ              100000   // camillia-mt: Wire.begin(I2C_SDA, I2C_SCL, 100000)

// AW9523 at 0x59: front buttons on P00..P07, RGB LED P10..P12, FT6636 reset P13.
#define BTN_EXPANDER_ADDR       0x59
#define TOUCH_RST_EXPANDER_BIT     3   // P13 = port 1 bit 3, active-low
#define BTN_R2_BIT                 6   // P06, camillia-mt's screen sleep/wake key
// The one real GPIO button (IO38, Attaky's "Boot"); camillia-mt's USER_BUTTON_PIN.
#define BOOT_BUTTON_PIN           38

// Battery — MAX17048 fuel gauge; no ADC divider.
#define BATT_FUEL_GAUGE_ADDR    0x36

// TFT — ST7789 240x320 native, driven landscape 320x240 at rotation 3 (the
// panel is mounted 180 degrees from the usual landscape). Write-only: no MISO.
// SPI3, not SPI2: Arduino's SPI (SPI2/FSPI) is re-pointed at the radio pins by
// mesh_radio.cpp's SPI.begin(), and this board's radio is on separate pins.
#define TFT_SPI_HOST          SPI3_HOST
#define TFT_SPI_SCK               11
#define TFT_SPI_MOSI              13
#define TFT_SPI_MISO              -1
#define TFT_SPI_3WIRE          false
#define TFT_SPI_WRITE_HZ    80000000
#define TFT_SPI_READ_HZ      1000000
#define TFT_CS                    45
#define TFT_DC                    12
#define TFT_RST                   10
#define TFT_PANEL_WIDTH          240
#define TFT_PANEL_HEIGHT         320
#define TFT_PANEL_OFFSET_X         0
#define TFT_PANEL_OFFSET_Y         0
#define TFT_INVERT              true
#define TFT_RGB_ORDER          false
#define TFT_ROTATION_LANDSCAPE     3
#define TFT_LANDSCAPE_W          320
#define TFT_LANDSCAPE_H          240

// Backlight — LEDC PWM on LEDA, active HIGH.
#define TFT_BL                    14
#define TFT_BL_INVERT          false
#define TFT_BL_FREQ            12000
#define TFT_BL_PWM_CH              7
#define TFT_BRIGHTNESS_DEFAULT   150

// Touch — FT6636 on the main I2C bus, polled (its INT is expander 0x58 P04).
#define TOUCH_ADDR              0x38

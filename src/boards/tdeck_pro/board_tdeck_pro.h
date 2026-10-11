#pragma once
// LilyGo T-Deck Pro (e-paper). Values are camillia-mt's src/hal/hw_tdeck_pro.h
// (camillia-mt 3777e74), trimmed to what this build uses; panel class, SPI
// clock and rotation from that tree's src/hal/tdeck_pro_display.h and
// src/hal/board.h (TFT_ROTATION_DEFAULT 0 for DEVICE_TDECK_PRO).
//
// E-paper, SX1262 and micro-SD share one SPI bus (SCK 36 / MISO 47 / MOSI 33)
// with separate chip selects; all three are parked high in boardEarlyInit()
// and again before every panel refresh. GPIO46 powers the whole LoRa module
// (mesh_radio.cpp drives LORA_POWER_ENABLE_PIN before SPI.begin).
//
// UNTESTED ON HARDWARE — build-verified only.

// LoRa — SX1262: TCXO on DIO3 (2.4 V), DIO2 as the RF switch, no FEM.
#define LORA_SPI_SCK              36
#define LORA_SPI_MISO             47
#define LORA_SPI_MOSI             33
#define LORA_CS                    3
#define LORA_DIO1                  5
#define LORA_RST                   4
#define LORA_BUSY                  6
#define LORA_POWER_ENABLE_PIN     46
#define LORA_POWER_ENABLE_LEVEL HIGH
#define LORA_FEM_POWER_PIN        -1
#define LORA_FEM_ENABLE_PIN       -1
#define LORA_FEM_TX_MODE_PIN      -1
#define MESH_TCXO_V             2.4f

// micro-SD on the shared bus; unused, deselected at boot.
#define SD_CS                     48

// E-paper — GDEQ031T10 (UC8253), 240x320 portrait, driven with GxEPD2.
#define EINK_SPI_SCK    LORA_SPI_SCK
#define EINK_SPI_MISO  LORA_SPI_MISO
#define EINK_SPI_MOSI  LORA_SPI_MOSI
#define EINK_SPI_HZ          2000000
#define EINK_CS                   34
#define EINK_DC                   35
#define EINK_RST                  16
#define EINK_BUSY                 37
#define EINK_ROTATION              0

// Frontlight (camillia-mt's TFT_BL) — kept off; a status page needs no light.
#define FRONTLIGHT_PIN            45

// Keyboard backlight — kept off; the keyboard is not used.
#define KB_BL                     42

// I2C bus shared by keyboard, touch and the BQ25896 charger.
#define I2C_SDA                   13
#define I2C_SCL                   14

// u-blox GNSS power switch — held off; this firmware has no GPS code.
#define GPS_ENABLE_PIN            39
#define GPS_ENABLE_ON_LEVEL     HIGH

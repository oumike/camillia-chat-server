#pragma once
// Heltec WiFi LoRa 32 V4 (plain board, built-in OLED). Radio pins match
// camillia-mt's working hw_heltec_v4.h; OLED pins are the stock V4 values.

// LoRa — SX1262 behind a front-end module (FEM).
#define LORA_SPI_SCK           9
#define LORA_SPI_MISO         11
#define LORA_SPI_MOSI         10
#define LORA_CS                8
#define LORA_DIO1             14
#define LORA_RST              12
#define LORA_BUSY             13
#define LORA_FEM_POWER_PIN     7
#define LORA_FEM_ENABLE_PIN    2
#define LORA_FEM_TX_MODE_PIN  46
#define MESH_TCXO_V         1.8f

// OLED — SSD1306 128x64 on I2C, powered from the switched Vext rail.
#define OLED_SDA              17
#define OLED_SCL              18
#define OLED_RST              21
#define OLED_ADDR           0x3C
#define VEXT_PIN              36
#define VEXT_ON_LEVEL        LOW

// Battery — divider on GPIO1, switched by a sense-enable line (camillia-mt's
// hw_heltec_v4.h values) so the divider does not drain the cell between reads.
#define BATT_ADC_PIN           1
#define BATT_DIV         5.1205f
#define BATT_SENSE_PIN        37
#define BATT_SENSE_ON        LOW

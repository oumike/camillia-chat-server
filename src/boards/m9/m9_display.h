#pragma once
// LovyanGFX device for the Elecrow ThinkNode M9: ST7789 on the shared SPI2
// bus, active-low PWM backlight, no touch. Configuration is camillia-mt's
// src/hal/display.h LGFX_TDeck as compiled for DEVICE_M9 (camillia-mt 3777e74).
#include <LovyanGFX.hpp>
#include "board.h"

class LGFX_M9Board : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;

public:
    LGFX_M9Board() {
        {
            auto cfg       = _bus.config();
            cfg.spi_host   = TFT_SPI_HOST;
            cfg.freq_write = TFT_SPI_WRITE_HZ;
            cfg.freq_read  = TFT_SPI_READ_HZ;
            cfg.use_lock   = true;   // the radio shares this bus
            cfg.pin_sclk   = TFT_SPI_SCK;
            cfg.spi_mode   = 0;
            cfg.spi_3wire  = TFT_SPI_3WIRE;
            cfg.pin_miso   = TFT_SPI_MISO;
            cfg.pin_mosi   = TFT_SPI_MOSI;
            cfg.pin_dc     = TFT_DC;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg         = _panel.config();
            cfg.pin_cs       = TFT_CS;
            cfg.pin_rst      = TFT_RST;
            cfg.panel_width  = TFT_PANEL_WIDTH;
            cfg.panel_height = TFT_PANEL_HEIGHT;
            cfg.offset_x     = TFT_PANEL_OFFSET_X;
            cfg.offset_y     = TFT_PANEL_OFFSET_Y;
            cfg.invert       = TFT_INVERT;
            cfg.rgb_order    = TFT_RGB_ORDER;
            cfg.readable     = true;
            cfg.bus_shared   = true;
            _panel.config(cfg);
        }
        {
            auto cfg        = _light.config();
            cfg.pin_bl      = TFT_BL;
            cfg.invert      = TFT_BL_INVERT;
            cfg.freq        = TFT_BL_FREQ;
            cfg.pwm_channel = TFT_BL_PWM_CH;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        setPanel(&_panel);
    }
};

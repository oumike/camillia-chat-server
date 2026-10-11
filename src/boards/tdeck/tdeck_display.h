#pragma once
// LovyanGFX device for the LilyGo T-Deck: ST7789 on the shared SPI2 bus, PWM
// backlight, GT911 touch. Configuration is camillia-mt's src/hal/display.h
// LGFX_TDeck as compiled for DEVICE_TDECK (camillia-mt 3777e74).
#include <LovyanGFX.hpp>
#include "board.h"

class LGFX_TDeckBoard : public lgfx::LGFX_Device {
    lgfx::Panel_ST7789 _panel;
    lgfx::Bus_SPI      _bus;
    lgfx::Light_PWM    _light;
    lgfx::Touch_GT911  _touch;

public:
    LGFX_TDeckBoard() {
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
            auto cfg              = _panel.config();
            cfg.pin_cs            = TFT_CS;
            cfg.pin_rst           = TFT_RST;
            cfg.panel_width       = TFT_PANEL_WIDTH;
            cfg.panel_height      = TFT_PANEL_HEIGHT;
            cfg.offset_x          = TFT_PANEL_OFFSET_X;
            cfg.offset_y          = TFT_PANEL_OFFSET_Y;
            cfg.invert            = TFT_INVERT;
            cfg.rgb_order         = TFT_RGB_ORDER;
            cfg.readable          = true;
            cfg.dummy_read_pixel  = 16;
            cfg.end_read_delay_us = 8;
            cfg.bus_shared        = true;
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
        {
            auto cfg            = _touch.config();
            cfg.x_min           = 0;
            cfg.x_max           = TFT_PANEL_WIDTH - 1;
            cfg.y_min           = 0;
            cfg.y_max           = TFT_PANEL_HEIGHT - 1;
            cfg.pin_int         = TOUCH_INT;
            cfg.pin_rst         = TOUCH_RST;
            cfg.bus_shared      = false;
            cfg.offset_rotation = 0;
            cfg.i2c_port        = TOUCH_I2C_PORT;
            cfg.i2c_addr        = TOUCH_ADDR;
            cfg.pin_sda         = TOUCH_SDA;
            cfg.pin_scl         = TOUCH_SCL;
            cfg.freq            = TOUCH_I2C_FREQ;
            _touch.config(cfg);
            _panel.setTouch(&_touch);
        }
        setPanel(&_panel);
    }
};

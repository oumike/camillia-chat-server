// Adapted from camillia-mt 252952d: src/hal/display.h (DEVICE_WIO_TRACKER_L2 branches).
#pragma once
// LGFX_WioL2 is camillia-mt's LGFX_TDeck reduced to the Wio Tracker L2
// branches: NV3031B on quad-SPI (SPI3), LP5814 I2C backlight, GT911 touch on
// Wire's port, with the values from board_wio_l2.h. One change from mt: the
// LP5814 light remembers whether its init succeeded and skips register writes
// when it did not, so a missing driver logs once instead of on every dim.
#include <LovyanGFX.hpp>
#include <lgfx/v1/panel/Panel_NV3031B.hpp>   // LovyanGFX 1.2.27+
#include <Wire.h>
#include "board.h"

class Light_WioTrackerL2LP5814 : public lgfx::v1::ILight {
public:
    struct config_t {
        uint8_t brightness = TFT_BRIGHTNESS_DEFAULT;
    };

    const config_t &config() const { return _cfg; }
    void config(const config_t &cfg) { _cfg = cfg; }
    bool ok() const { return _ok; }

    bool init(uint8_t brightness) override {
        Wire.beginTransmission(LP5814_ADDR);
        if (Wire.endTransmission() != 0) {
            if (!_reported) Serial.printf("[cs] display: LP5814 backlight not found at 0x%02X\n", LP5814_ADDR);
            _reported = true;
            _ok = false;
            return false;
        }

        bool ok = true;
        ok = writeReg(LP5814_REG_DEVICE_CONFIG0, 0x01) && ok;
        ok = writeReg(LP5814_REG_MAX_CURRENT, 0x01) && ok;
        ok = writeReg(LP5814_REG_ENABLE_CONTROL, 0x00) && ok;
        ok = writeReg(LP5814_REG_DIM_MODE, 0x4E) && ok;
        ok = writeReg(LP5814_REG_ENGINE_MODE, 0xF0) && ok;
        for (uint8_t channel = 0; channel < 4; channel++) {
            ok = writeReg(LP5814_REG_LED0_DC + channel, LP5814_LED_DC_VALUE) && ok;
        }
        ok = writeReg(LP5814_REG_ENABLE_CONTROL, 0x0F) && ok;
        ok = writeReg(LP5814_REG_UPDATE, 0x55) && ok;
        delay(5);
        _ok = true;   // ACKed: brightness writes are worth trying even if a setup write failed
        setBrightness(brightness);
        return ok;
    }

    void setBrightness(uint8_t brightness) override {
        _cfg.brightness = brightness;
        if (!_ok) return;
        for (uint8_t channel = 0; channel < 4; channel++) {
            (void)writeReg(LP5814_REG_LED0_PWM + channel, brightness);
        }
    }

    uint8_t getBrightness() const { return _cfg.brightness; }

private:
    bool writeReg(uint8_t reg, uint8_t value) {
        Wire.beginTransmission(LP5814_ADDR);
        Wire.write(reg);
        Wire.write(value);
        const uint8_t error = Wire.endTransmission();
        if (error != 0) {
            Serial.printf("[cs] display: LP5814 write reg 0x%02X failed: %u\n", reg, (unsigned)error);
            return false;
        }
        return true;
    }

    config_t _cfg;
    bool     _ok = false;
    bool     _reported = false;
};

class LGFX_WioL2 : public lgfx::LGFX_Device {
    lgfx::Panel_NV3031B      _panel;
    lgfx::Bus_SPI            _bus;
    Light_WioTrackerL2LP5814 _light;
    lgfx::Touch_GT911        _touch;

public:
    bool lightOk() const { return _light.ok(); }

    // LP5814 before the GT911 probe, then reset Wire: a NACK in the GT911 probe
    // can leave the I2C peripheral busy, and the next backlight write then times out.
    bool init_impl(bool use_reset, bool use_clear) override {
        (void)_light.init(_light.config().brightness);
        const bool result = LGFX_Device::init_impl(use_reset, use_clear);
        Wire.end();
        Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL);
        Wire.setClock(BOARD_I2C_FREQ);
        return result;
    }

    LGFX_WioL2() {
        {
            // Quad-SPI: setting pin_io0..pin_io3 is what selects it in 1.2.27.
            // No DC line on a QSPI panel, so pin_dc stays -1.
            auto cfg       = _bus.config();
            cfg.spi_host   = TFT_SPI_HOST;
            cfg.freq_write = TFT_SPI_WRITE_HZ;
            cfg.freq_read  = TFT_SPI_READ_HZ;
            cfg.use_lock   = true;
            cfg.pin_sclk   = TFT_SPI_SCK;
            cfg.spi_mode   = TFT_SPI_MODE;
            cfg.spi_3wire  = false;
            cfg.pin_miso   = -1;
            cfg.pin_mosi   = -1;
            cfg.pin_dc     = -1;
            cfg.pin_io0    = TFT_QSPI_IO0;
            cfg.pin_io1    = TFT_QSPI_IO1;
            cfg.pin_io2    = TFT_QSPI_IO2;
            cfg.pin_io3    = TFT_QSPI_IO3;
            _bus.config(cfg);
            _panel.setBus(&_bus);
        }
        {
            auto cfg            = _panel.config();
            cfg.pin_cs          = TFT_CS;
            cfg.pin_rst         = TFT_RST;
            cfg.pin_busy        = -1;
            cfg.panel_width     = TFT_PANEL_WIDTH;
            cfg.panel_height    = TFT_PANEL_HEIGHT;
            cfg.memory_width    = TFT_PANEL_WIDTH;
            cfg.memory_height   = TFT_PANEL_HEIGHT;
            cfg.offset_x        = TFT_PANEL_OFFSET_X;
            cfg.offset_y        = TFT_PANEL_OFFSET_Y;
            cfg.offset_rotation = TFT_PANEL_OFFSET_ROTATION;
            cfg.invert          = TFT_INVERT;
            cfg.rgb_order       = TFT_RGB_ORDER;
            cfg.dlen_16bit      = false;
            cfg.bus_shared      = false;
            cfg.readable        = false;
            _panel.config(cfg);
        }
        {
            auto cfg       = _light.config();
            cfg.brightness = TFT_BRIGHTNESS_DEFAULT;
            _light.config(cfg);
            _panel.setLight(&_light);
        }
        {
            auto cfg = _touch.config();
            cfg.x_min           = 0;
            cfg.x_max           = TFT_PANEL_WIDTH - 1;
            cfg.y_min           = 0;
            cfg.y_max           = TFT_PANEL_HEIGHT - 1;
            cfg.pin_int         = TOUCH_INT;
            cfg.bus_shared      = false;
            cfg.offset_rotation = TOUCH_OFFSET_ROTATION;
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

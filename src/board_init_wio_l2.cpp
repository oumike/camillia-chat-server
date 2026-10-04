// Seeed Wio Tracker L2 (wio-tracker-l2): I2C, then the PCA9555 expander that
// gates every rail. The sequence and delays are camillia-mt's
// wioTrackerL2IoBegin() (src/hal/wio_tracker_l2_io.cpp), except that GNSS
// power stays off and GNSS reset stays asserted: the chat server has no GNSS.
#include "board_init.h"
#include <Arduino.h>
#include <Wire.h>
#include "board.h"
#include "xl9555.h"

namespace {

bool        s_ok = false;
bool        s_found = false;       // the expander answered the first read
const char *s_failStep = nullptr;  // the write that failed after that
uint8_t     s_out0 = 0xFF, s_out1 = 0xFF, s_cfg0 = 0xFF, s_cfg1 = 0xFF;

void stage(uint8_t bit, bool high) { xl9555SetOutput(bit, high, s_out0, s_out1, s_cfg0, s_cfg1); }

// xl9555WriteAll writes the output latches before the directions, so a bit
// that turns into an output starts at its staged level.
bool commit(const char *step) {
    if (xl9555WriteAll(EXPANDER_ADDR, s_out0, s_out1, s_cfg0, s_cfg1)) return true;
    s_failStep = step;
    return false;
}

}  // namespace

void boardEarlyInit() {
    pinMode(EXPANDER_INT, INPUT_PULLUP);
    Wire.begin(BOARD_I2C_SDA, BOARD_I2C_SCL, BOARD_I2C_FREQ);
    if (!xl9555ReadAll(EXPANDER_ADDR, s_out0, s_out1, s_cfg0, s_cfg1)) return;
    s_found = true;

    xl9555SetInput(EXP_BIT_WAKE_BUTTON, s_cfg0, s_cfg1);
    xl9555SetInput(EXP_BIT_I2C_INT, s_cfg0, s_cfg1);
    xl9555SetInput(EXP_BIT_SD_DETECT, s_cfg0, s_cfg1);
    xl9555SetInput(EXP_BIT_LCD_CS, s_cfg0, s_cfg1);

    stage(EXP_BIT_TOUCH_INT, false);
    stage(EXP_BIT_LCD_POWER, true);
    stage(EXP_BIT_LCD_RST, true);
    stage(EXP_BIT_GROVE_POWER, false);
    stage(EXP_BIT_TOUCH_RST, false);
    stage(EXP_BIT_GNSS_RST, true);       // asserted, and left so
    stage(EXP_BIT_USER_LED, false);
    stage(EXP_BIT_USB_OTG_EN, false);
    stage(EXP_BIT_AUDIO_PA_POWER, false);
    stage(EXP_BIT_GNSS_POWER, false);    // mt: on
    stage(EXP_BIT_SD_POWER, false);
    stage(EXP_BIT_BATT_SENSE_EN, false); // battery_ads1115.cpp raises it only around a read
    if (!commit("safe state")) return;

    delay(50);   // mt: 10 ms, GNSS reset release, 40 ms
    stage(EXP_BIT_LCD_RST, false);
    if (!commit("LCD reset")) return;
    delay(10);
    stage(EXP_BIT_LCD_RST, true);
    if (!commit("LCD reset release")) return;
    delay(500);
    stage(EXP_BIT_LCD_CS, true);
    if (!commit("LCD CS")) return;
    delay(10);
    stage(EXP_BIT_TOUCH_RST, true);
    if (!commit("touch reset release")) return;
    delay(60);
    s_ok = true;
}

void boardReport() {
    if (s_ok) {
        Serial.printf("[cs] board: expander 0x%02x ok, out=%04x dir=%04x\n", EXPANDER_ADDR,
                      (unsigned)(s_out1 << 8 | s_out0), (unsigned)(s_cfg1 << 8 | s_cfg0));
    } else if (!s_found) {
        Serial.printf("[cs] board: expander not found at 0x%02x\n", EXPANDER_ADDR);
    } else {
        Serial.printf("[cs] board: expander 0x%02x write failed (%s)\n", EXPANDER_ADDR, s_failStep);
    }
}

bool boardExpanderOk() { return s_ok; }

bool boardExpanderRead(uint16_t &in) {
    if (!s_ok) return false;
    uint8_t in0 = 0xFF, in1 = 0xFF;
    if (!xl9555ReadReg(EXPANDER_ADDR, XL9555_REG_IN0, in0)) return false;
    if (!xl9555ReadReg(EXPANDER_ADDR, XL9555_REG_IN1, in1)) return false;
    in = (uint16_t)(in1 << 8 | in0);
    return true;
}

void boardExpanderSet(uint8_t bit, bool on) {
    if (!s_ok) return;
    const uint8_t out0 = s_out0, out1 = s_out1, cfg0 = s_cfg0, cfg1 = s_cfg1;
    stage(bit, on);
    if (xl9555WriteAll(EXPANDER_ADDR, s_out0, s_out1, s_cfg0, s_cfg1)) return;
    s_out0 = out0;   // keep the shadows matching the chip
    s_out1 = out1;
    s_cfg0 = cfg0;
    s_cfg1 = cfg1;
}

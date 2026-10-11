// LilyGo T-Display P4 (p4-amoled-sx1262, p4-amoled-lr2021). Power-up order
// follows camillia-mt's setup() (camillia-mt 3777e74: src/main_lvgl.cpp): the
// XL9535 comes up first, then the ESP32-C6 is reset so ESP-Hosted finds it
// fresh when WiFi starts.
#include "board_init.h"
#include <Arduino.h>
#include "board.h"
#include "hal/tdisplay_p4_io.h"

namespace {
bool s_ioOk = false;
bool s_c6Ok = false;
bool s_fullRestart = false;
}  // namespace

void boardEarlyInit() {
    // First, so its shutdown handler runs after every other one (see the .cpp).
    tdisplayP4InstallFullRestart();
    s_fullRestart = tdisplayP4ConsumeFullRestartMarker();
    pinMode(USER_BUTTON_PIN, INPUT_PULLUP);
    s_ioOk = tdisplayP4IoBegin();
    if (s_ioOk) s_c6Ok = tdisplayP4IoResetEsp32C6();
}

void boardReport() {
    if (s_fullRestart) Serial.printf("[cs] board: woke from a software restart (full system reset)\n");
    if (!s_ioOk) Serial.printf("[cs] board: XL9535 expander not answering; radio, panel and C6 are unpowered\n");
    else if (!s_c6Ok) Serial.printf("[cs] board: ESP32-C6 reset failed; WiFi may be unavailable\n");
}

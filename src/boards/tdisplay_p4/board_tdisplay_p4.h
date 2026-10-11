#pragma once
// LilyGo T-Display P4, 4.1" AMOLED variant (p4-amoled-sx1262, p4-amoled-lr2021).
// ESP32-P4 with Wi-Fi through an ESP32-C6 over ESP-Hosted SDIO, RM69A10
// 568x1232 MIPI-DSI AMOLED, GT9895 touch, BQ27220 fuel gauge, and an SX1262 or
// LR2021 whose reset and DIO1 sit on the XL9535 expander. Every pin is
// camillia-mt's src/hal/hw_tdisplay_p4.h, vendored verbatim in hal/.
//
// UNTESTED ON HARDWARE — build-verified only.
#include "hal/hw_tdisplay_p4.h"

// Landscape, turned the way camillia-mt turns it (src/hal/board.h:
// TFT_ROTATION_LANDSCAPE 3); LovyanGFX applies the same turn to touch.
#define TFT_ROTATION_LANDSCAPE     3

// The panel is ~330 DPI, so the UI is drawn at half resolution and every LVGL
// pixel goes out as a 2x2 block (camillia-mt's TDISPLAY_P4_UI_SCALE, default 2):
// text then comes out about the physical size it is on the 320x240 boards.
// Build with -DTDISPLAY_P4_UI_SCALE=1 for the native 1232x568.
#ifndef TDISPLAY_P4_UI_SCALE
#define TDISPLAY_P4_UI_SCALE       2
#endif

// The AMOLED's corners are rounded, so LVGL's area stops this many panel
// pixels short of each short edge (camillia-mt's UI_CORNER_SAFE_X is 10 LVGL
// pixels at scale 2; an estimate there too). The strips stay black.
#define TDISPLAY_P4_INSET_X       16

#define TFT_LANDSCAPE_W  ((TFT_PANEL_HEIGHT - 2 * TDISPLAY_P4_INSET_X) / TDISPLAY_P4_UI_SCALE)
#define TFT_LANDSCAPE_H  (TFT_PANEL_WIDTH / TDISPLAY_P4_UI_SCALE)

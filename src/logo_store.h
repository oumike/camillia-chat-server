#pragma once
// The boot logo the flasher sends with `cfg logo` (cfg_cli.h): a PNG of at most
// CFG_LOGO_MAX bytes in NVS namespace cs_logo, drawn by the LVGL splash in the
// camellia's place.
#include <stddef.h>
#include <stdint.h>
#include <vector>

bool logoLoad(std::vector<uint8_t> &png);   // false if none is stored
bool logoSave(const uint8_t *png, size_t len);
void logoErase();

// Decodes `png` onto a w x h card of colour `bg` (RGB565), fitted inside it and
// centred. Returns a PSRAM buffer of w*h native-endian RGB565 pixels (free with
// heap_caps_free), or nullptr. Builds without LovyanGFX always return nullptr.
uint16_t *logoRender565(const uint8_t *png, size_t len, int w, int h, uint16_t bg);

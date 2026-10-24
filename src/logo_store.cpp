#include "logo_store.h"
#include <Preferences.h>
#include <esp_heap_caps.h>
#include "cfg_cli.h"

// Only the LVGL builds carry LovyanGFX; the OLED and e-paper builds have no logo.
#if __has_include(<LovyanGFX.hpp>)
#include <LovyanGFX.hpp>
#define CS_LOGO_RENDER 1
#endif

static const char *kNs = "cs_logo";
static const char *kKey = "png";

bool logoLoad(std::vector<uint8_t> &png) {
    Preferences p;
    if (!p.begin(kNs, true)) return false;
    const size_t n = p.getBytesLength(kKey);
    bool ok = n > 0 && n <= CFG_LOGO_MAX;
    if (ok) {
        png.resize(n);
        ok = p.getBytes(kKey, png.data(), n) == n;
    }
    p.end();
    if (!ok) png.clear();
    return ok;
}

bool logoSave(const uint8_t *png, size_t len) {
    Preferences p;
    if (!p.begin(kNs, false)) return false;
    const bool ok = p.putBytes(kKey, png, len) == len;
    if (!ok) p.remove(kKey);
    p.end();
    return ok;
}

void logoErase() {
    Preferences p;
    if (!p.begin(kNs, false)) return;
    p.remove(kKey);
    p.end();
}

uint16_t *logoRender565(const uint8_t *png, size_t len, int w, int h, uint16_t bg) {
#ifdef CS_LOGO_RENDER
    if (!png || !len || w <= 0 || h <= 0) return nullptr;
    LGFX_Sprite spr;
    spr.setPsram(true);
    spr.setColorDepth(16);
    if (!spr.createSprite(w, h)) return nullptr;
    spr.fillScreen(bg);
    // drawPng scales down to fit maxW x maxH; centre what it draws.
    const bool drawn = spr.drawPng(png, len, 0, 0, w, h, 0, 0, 0.0f, 0.0f, lgfx::middle_center);
    uint16_t *out = drawn ? (uint16_t *)heap_caps_malloc((size_t)w * h * 2, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
                          : nullptr;
    if (out) {
        // The sprite holds byte-swapped RGB565 (lgfx::swap565_t); LVGL wants native.
        const uint16_t *src = (const uint16_t *)spr.getBuffer();
        for (size_t i = 0, n = (size_t)w * h; i < n; i++) out[i] = (uint16_t)(src[i] << 8 | src[i] >> 8);
    }
    spr.deleteSprite();
    return out;
#else
    (void)png; (void)len; (void)w; (void)h; (void)bg;
    return nullptr;
#endif
}

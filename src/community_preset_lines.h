#pragma once
// A community preset (community_presets.h) as the cfg CLI lines that apply it,
// without the leading "cfg ": region, radio preset and slot, the whole channel
// plan, the broker if the community has one, then commit. Pure C++.

// Most channels in a profile (10) plus region, preset, slot, clear, mqtt,
// oktomqtt and commit.
constexpr int CFG_PRESET_MAX_LINES = 32;

struct CfgPresetLine {
    char text[256];
};

// Fills `lines` for preset `idx`; returns how many, or 0 for a bad index or if
// they do not fit.
int communityPresetLines(int idx, CfgPresetLine *lines, int max);

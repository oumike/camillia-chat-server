#pragma once
// Shared between board_init_mesh_deck.cpp and display_hal_mesh_deck.cpp: the
// one handle on AW9523 0x59. Aw9523::begin() soft-resets the chip, which would
// put the FT6636 back into reset (P13), so it is opened exactly once, in
// boardEarlyInit() — as in camillia-mt's s_mdButtons.
#include "aw9523.h"

Aw9523 &meshDeckButtons();
bool meshDeckButtonsReady();

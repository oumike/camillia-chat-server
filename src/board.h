#pragma once
// Board selector: the expansion env defines CS_BOARD_V4_EXPANSION (platformio.ini).
#if defined(CS_BOARD_V4_EXPANSION)
#include "board_v4_exp.h"
#else
#include "board_v4.h"
#endif

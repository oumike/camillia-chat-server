#pragma once
// Board selector: the expansion env defines CS_BOARD_V4_EXPANSION and the Wio
// env CS_BOARD_WIO_L2 (platformio.ini); neither means the plain Heltec V4.
#if defined(CS_BOARD_V4_EXPANSION)
#include "board_v4_exp.h"
#elif defined(CS_BOARD_WIO_L2)
#include "board_wio_l2.h"
#else
#include "board_v4.h"
#endif

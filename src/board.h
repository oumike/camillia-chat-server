#pragma once
// Board selector. Boards added from envs/*.ini name their pin header directly
// with -DCS_BOARD_HEADER='"board_x.h"'. Of the older envs, the expansion env
// defines CS_BOARD_V4_EXPANSION, the V4-R8 env CS_BOARD_V4_R8 and the Wio env
// CS_BOARD_WIO_L2 (platformio.ini); none of them means the plain Heltec V4.
#if defined(CS_BOARD_HEADER)
#include CS_BOARD_HEADER
#elif defined(CS_BOARD_V4_EXPANSION)
#include "board_v4_exp.h"
#elif defined(CS_BOARD_V4_R8)
#include "board_v4_r8.h"
#elif defined(CS_BOARD_WIO_L2)
#include "board_wio_l2.h"
#else
#include "board_v4.h"
#endif

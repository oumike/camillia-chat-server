#pragma once
// Early board power-up. One implementation per env, chosen by build_src_filter
// in platformio.ini: board_init_v4.cpp, board_init_v4_exp.cpp, board_init_wio_l2.cpp.

void boardEarlyInit();   // first statement of setup(), before Serial
void boardReport();      // after Serial.begin; logs what boardEarlyInit found (may print nothing)

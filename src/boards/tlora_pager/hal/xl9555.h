#pragma once
// lib/camillia_mesh/mesh_radio.cpp includes "hal/xl9555.h" for DEVICE_TLORA_PAGER_TFT
// (camillia-mt's layout); -Isrc/boards/tlora_pager resolves it here, to the copy
// already vendored in src/.
#include "../../../xl9555.h"

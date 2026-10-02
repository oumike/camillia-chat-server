#pragma once
// Shim replacing camillia-mt's src/config.h for the borrowed mesh layer.
// Only the constants mesh_proto / mesh_radio / mesh_channel_plan use.
#include "../../src/board.h"

#define MESH_FREQ       906.875f  // MHz (US LongFast default slot)
#define MESH_BW         250.0f    // kHz
#define MESH_SF         11
#define MESH_CR         5
#define MESH_SYNC       0x2B      // Meshtastic sync word
#define MESH_PREAMBLE   16
#define MESH_POWER      22        // dBm
#define MESH_HOP_LIMIT   7
#define MY_LORA_RX_BOOST 1

#define MESH_RADIO_HAS_TCXO 1
#define LORA_BW_CODE_MIN   16

#define MESH_HW_MODEL_HELTEC_V4  110
#define MY_HW_MODEL MESH_HW_MODEL_HELTEC_V4

// 3 store channels + the camillia-cs discovery channel (always slot 3).
#define MESH_CHANNELS     4
#define MAX_CHANNELS      MESH_CHANNELS
#define MESH_TEXT_MAX_LEN 200

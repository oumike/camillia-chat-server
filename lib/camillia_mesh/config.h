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

// camillia-mt src/config.h: crystal-clocked boards (MESH_TCXO_V 0) are named
// here; the LR1121 pager and the LR1110 M9 cannot go below 62.5 / 31.25 kHz.
#if defined(DEVICE_MESH_DECK)
#  define MESH_RADIO_HAS_TCXO 0
#else
#  define MESH_RADIO_HAS_TCXO 1
#endif

#if defined(DEVICE_TLORA_PAGER_TFT) && defined(PAGER_LORA_USE_LR1121) && (PAGER_LORA_USE_LR1121)
#  define LORA_BW_CODE_MIN 62
#elif defined(DEVICE_M9) || !MESH_RADIO_HAS_TCXO
#  define LORA_BW_CODE_MIN 31
#else
#  define LORA_BW_CODE_MIN 16
#endif

// Boards from envs/*.ini pass their Meshtastic HardwareModel (camillia-mt's
// src/config.h values) as -DCS_HW_MODEL=N.
#if defined(CS_HW_MODEL)
#define MY_HW_MODEL CS_HW_MODEL
#elif defined(CS_BOARD_WIO_L2)
#define MESH_HW_MODEL_SEEED_WIO_TRACKER_L2  137
#define MY_HW_MODEL MESH_HW_MODEL_SEEED_WIO_TRACKER_L2
#else
#define MESH_HW_MODEL_HELTEC_V4  110
#define MY_HW_MODEL MESH_HW_MODEL_HELTEC_V4
#endif

// 20 store channels + the camillia-cs discovery channel (always slot 20).
#define MESH_CHANNELS     21
#define MAX_CHANNELS      MESH_CHANNELS
#define MESH_TEXT_MAX_LEN 200

#pragma once
// LoRa glue: channel table from settings, decrypted receive, encrypted send.
#include <Arduino.h>
#include "mesh_proto.h"
#include "settings.h"

constexpr int DISCOVERY_SLOT = 3;   // CHANNEL_KEYS index of the camillia-cs channel
constexpr uint32_t PORT_CHAT_SERVER = 256;   // Meshtastic PRIVATE_APP

bool radioBegin(const Settings &s, uint32_t myNodeId);
// Decrypted packets only.
bool radioPoll(MeshPacket &pkt);
// chanSlot -1 = discovery channel. hopLimit is both hop_limit and hop_start.
bool radioSend(uint32_t to, int chanSlot, uint8_t hopLimit, uint32_t portnum,
               const uint8_t *payload, size_t len);
// Send an already-encoded Meshtastic Data message.
bool radioSendData(uint32_t to, int chanSlot, uint8_t hopLimit, const uint8_t *data, size_t len);
uint8_t hopsTravelled(const MeshHdr &hdr);
// Store slot for a CHANNEL_KEYS index: 0..chanCount-1, -1 for discovery, -2 if neither.
int slotForChanIdx(int chanIdx);

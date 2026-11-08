#pragma once
// Whether a channel message's sender is identified enough to store it, at a
// Settings::senderFilter level (SenderFilter). Pure C++.
#include <stdint.h>
#include "node_names.h"
#include "peer_keys.h"

bool senderAllowed(uint8_t level, const NodeNames &names, const PeerKeys &keys, uint32_t from);

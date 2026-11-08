#pragma once
// LittleFS persistence of the node tables learned from NODEINFO: names and
// public keys. Kept across restarts so the sender filter (Settings::senderFilter)
// and PKI DMs work straight after boot. Device only; call after persistBegin().
#include <stddef.h>
#include <stdint.h>
#include "node_names.h"
#include "peer_keys.h"

// Flash both saved files can take, plus a .tmp while one is saved.
constexpr size_t NODE_PERSIST_FLASH_BYTES =
    NodeNames::BLOB_MAX + PeerKeys::BLOB_MAX +
    (NodeNames::BLOB_MAX > PeerKeys::BLOB_MAX ? NodeNames::BLOB_MAX : PeerKeys::BLOB_MAX);

// Loads the saved tables (a missing or corrupt file leaves its table empty).
void nodePersistBegin(NodeNames &names, PeerKeys &keys);
// Saves a table that changed, at most once per 5 minutes each.
void nodePersistMaybeSave(const NodeNames &names, const PeerKeys &keys, uint32_t nowMs);
// Saves whatever changed now (before a restart).
void nodePersistSaveNow(const NodeNames &names, const PeerKeys &keys);

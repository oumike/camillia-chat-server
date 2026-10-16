#pragma once
// Node ID -> Curve25519 public key, learned from NODEINFO. A node's key is what
// decrypts the DMs it sends the server (PKI) and encrypts the replies. Fixed
// capacity, least-recently-updated entry evicted. Pure C++, no Arduino.
#include <stddef.h>
#include <stdint.h>

constexpr int PEER_KEYS_CAP = 200;

class PeerKeys {
public:
    using AllocFn = void *(*)(size_t);

    // Allocates PEER_KEYS_CAP entries once; false if the allocation fails.
    bool begin(AllocFn alloc);

    // Records or replaces id's key. An all-zero key is ignored.
    void update(uint32_t id, const uint8_t key[32], uint32_t nowMs);

    // id's key, or nullptr if none is known. Valid until the next update().
    const uint8_t *find(uint32_t id) const;

    int count() const { return _count; }

private:
    struct Entry {
        uint32_t id;
        uint32_t lastMs;
        uint8_t  key[32];
    };

    Entry *_e = nullptr;
    int    _count = 0;
};

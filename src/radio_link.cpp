#include "radio_link.h"
#include <string.h>
#include "cs_proto.h"
#include "mesh_channel_plan.h"
#include "mesh_radio.h"

static uint32_t  s_myNodeId = 0;
static int       s_chanCount = 0;
static PeerKeyFn s_keyFor = nullptr;
static NoKeyFn   s_onNoKey = nullptr;

static void setSlot(int i, const char *name, const uint8_t *key, uint8_t keyLen, uint8_t role) {
    ChannelKey &ck = CHANNEL_KEYS[i];
    memset(&ck, 0, sizeof(ck));
    strncpy(ck.name_buf, name, sizeof(ck.name_buf) - 1);
    ck.name = ck.name_buf;
    memcpy(ck.key, key, keyLen);
    ck.keyLen = keyLen;
    ck.hash = computeChannelHash(ck.name_buf, ck.key, ck.keyLen);
    ck.role = role;
}

bool radioBegin(const Settings &s, uint32_t myNodeId) {
    s_myNodeId = myNodeId;
    s_chanCount = s.chanCount;
    // Unused store slots get a random 32-byte key so they never "decrypt" anything.
    for (int i = 0; i < SETTINGS_MAX_CHANNELS; i++) {
        if (i < s.chanCount) {
            setSlot(i, s.ch[i].name, s.ch[i].key, s.ch[i].keyLen, i == 0 ? 0 : 1);
        } else {
            uint8_t junk[32];
            esp_fill_random(junk, sizeof junk);
            setSlot(i, "", junk, sizeof junk, 2);
        }
    }
    setSlot(DISCOVERY_SLOT, csp::DISCOVERY_CHANNEL_NAME, csp::DISCOVERY_KEY, 16, 1);

    uint8_t preset = s.modemPreset < PRESET_COUNT ? s.modemPreset : PRESET_LONG_FAST;
    const PresetParams &p = kPresets[preset];
    float freq = s.freqSlot ? regionSlotFreqNum(s.region, p.bw, s.freqSlot - 1)
                            : regionSlotFreq(s.region, p.bw, p.channelName);
    meshSetHopLimit(7);
    bool ok = Radio.init() && Radio.reconfigure(freq, p.bw, p.sf, p.cr, regionPower(s.region));
    Serial.printf("[cs] radio %s: %s %s %.3f MHz, %d channel(s) + discovery\n", ok ? "ok" : "FAILED",
                  s.region, p.name, freq, s.chanCount);
    return ok;
}

void radioSetPeerKeys(PeerKeyFn keyFor, NoKeyFn onNoKey) {
    s_keyFor = keyFor;
    s_onNoKey = onNoKey;
}

// The mesh layer leaves header-channel-0 packets for the caller (camillia-mt's
// handleRx does the same): only one addressed here, from a node whose key we
// hold, can be ours to open.
static bool decryptPkiForUs(MeshPacket &pkt) {
    if (pkt.hdr.channel != 0 || pkt.hdr.to != s_myNodeId || pkt.rawLen <= 12 || !s_keyFor) return false;
    const uint8_t *key = s_keyFor(pkt.hdr.from);
    if (!key) {
        Serial.printf("[cs] PKI packet from !%08x: no key for it yet\n", (unsigned)pkt.hdr.from);
        if (s_onNoKey) s_onNoKey(pkt.hdr.from);
        return false;
    }
    uint8_t plain[256];
    size_t plainLen = 0;
    if (!decryptPki(pkt.hdr, pkt.rawCipher, pkt.rawLen, key, plain, plainLen)) {
        Serial.printf("[cs] PKI packet from !%08x: decrypt failed\n", (unsigned)pkt.hdr.from);
        return false;
    }
    const uint8_t *payPtr = nullptr;
    size_t payLen = 0;
    if (!decodeData(plain, plainLen, pkt.portnum, payPtr, payLen, pkt.requestId, pkt.wantResponse,
                    &pkt.dataDest, &pkt.hasDataDest, &pkt.dataSource, &pkt.hasDataSource) ||
        !payPtr || payLen > sizeof(pkt.payload)) {
        return false;
    }
    memcpy(pkt.payload, payPtr, payLen);
    pkt.payloadLen = payLen;
    pkt.chanIdx = -2;
    pkt.decrypted = true;
    return true;
}

bool radioPoll(MeshPacket &pkt) {
    while (Radio.pollRx(pkt)) {
        if (pkt.decrypted || decryptPkiForUs(pkt)) return true;
    }
    return false;
}

int slotForPacket(const MeshPacket &pkt) {
    const int idx = pkt.chanIdx;
    if (idx < 0 || idx >= MAX_CHANNELS || pkt.hdr.channel != CHANNEL_KEYS[idx].hash) return -2;
    if (idx < s_chanCount) return idx;
    if (idx == DISCOVERY_SLOT) return -1;
    return -2;
}

// Meshtastic's duty-cycle limited regions. EU_866 is set to the 1% ETSI default
// for that band rather than guessed higher.
uint8_t regionDutyPct(const char *region) {
    static const struct { const char *code; uint8_t pct; } kDuty[] = {
        {"EU_433", 10}, {"EU_868", 10}, {"EU_N_868", 10}, {"UA_433", 10}, {"UA_868", 1}, {"EU_866", 1},
    };
    for (const auto &d : kDuty)
        if (!strcmp(d.code, region)) return d.pct;
    return 100;
}

bool regionKnown(const char *region) {
    for (uint8_t i = 0; i < kRegionCount; i++)
        if (!strcmp(kRegions[i].code, region)) return true;
    return false;
}

bool settingsValidateDevice(const Settings &s, char *err, size_t cap) {
    if (!settingsValidate(s, err, cap)) return false;
    if (!regionKnown(s.region)) {
        snprintf(err, cap, "Unknown region \"%s\"", s.region);
        return false;
    }
    if (s.modemPreset >= PRESET_COUNT || !presetUsableOnThisRadio(s.modemPreset)) {
        snprintf(err, cap, "That preset is not available on this radio");
        return false;
    }
    return true;
}

uint8_t hopsTravelled(const MeshHdr &hdr) {
    uint8_t start = (hdr.flags >> 5) & 0x07, limit = hdr.flags & 0x07;
    return (start == 0 || limit > start) ? 0 : (uint8_t)(start - limit);
}

bool radioSendData(uint32_t to, int chanSlot, uint8_t hopLimit, const uint8_t *data, size_t len) {
    int idx = chanSlot < 0 ? DISCOVERY_SLOT : chanSlot;
    const ChannelKey &ck = CHANNEL_KEYS[idx];
    uint8_t frame[sizeof(MeshHdr) + 256];
    if (len > 256 - sizeof(MeshHdr)) return false;

    MeshHdr hdr = {};
    hdr.to = to;
    hdr.from = s_myNodeId;
    hdr.id = nextMeshPacketId();
    hdr.channel = ck.hash;
    uint8_t h = hopLimit & 0x07;
    hdr.flags = (uint8_t)(h | (h << 5));
    hdr.relay_node = (uint8_t)(s_myNodeId & 0xFF);
    if (!encryptPayload(hdr.id, s_myNodeId, ck.key, ck.keyLen, data, frame + sizeof(MeshHdr), len))
        return false;
    memcpy(frame, &hdr, sizeof(hdr));
    return Radio.transmit(frame, sizeof(hdr) + len);
}

bool radioSendDataPki(uint32_t to, const uint8_t toKey[32], uint8_t hopLimit, const uint8_t *data,
                      size_t len) {
    // Ciphertext plus the 8-byte tag and 4-byte extra nonce encryptPki appends.
    uint8_t frame[sizeof(MeshHdr) + 256];
    if (!toKey || len + 12 > 256 - sizeof(MeshHdr)) return false;

    MeshHdr hdr = {};
    hdr.to = to;
    hdr.from = s_myNodeId;
    hdr.id = nextMeshPacketId();
    hdr.channel = 0;   // PKI marker
    uint8_t h = hopLimit & 0x07;
    hdr.flags = (uint8_t)(h | (h << 5));
    hdr.relay_node = (uint8_t)(s_myNodeId & 0xFF);
    if (!encryptPki(hdr.id, s_myNodeId, toKey, data, len, frame + sizeof(MeshHdr))) return false;
    memcpy(frame, &hdr, sizeof(hdr));
    return Radio.transmit(frame, sizeof(hdr) + len + 12);
}

bool radioSend(uint32_t to, int chanSlot, uint8_t hopLimit, uint32_t portnum,
               const uint8_t *payload, size_t len) {
    // Data { portnum (1, varint), payload (2, bytes), bitfield (9, varint) = 0 }.
    // bitfield is always written: 2.8 nodes ignore Data without it.
    uint8_t data[256];
    size_t n = 0;
    auto varint = [&](uint32_t v) { do { uint8_t b = v & 0x7F; v >>= 7; data[n++] = b | (v ? 0x80 : 0); } while (v); };
    if (len > csp::MAX_PAYLOAD) return false;
    data[n++] = (1 << 3) | 0; varint(portnum);
    data[n++] = (2 << 3) | 2; varint((uint32_t)len);
    memcpy(data + n, payload, len); n += len;
    data[n++] = (9 << 3) | 0; data[n++] = 0;
    return radioSendData(to, chanSlot, hopLimit, data, n);
}

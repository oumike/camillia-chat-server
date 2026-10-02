#include "radio_link.h"
#include <string.h>
#include "cs_proto.h"
#include "mesh_channel_plan.h"
#include "mesh_radio.h"

static uint32_t s_myNodeId = 0;
static int      s_chanCount = 0;

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
    for (int i = 0; i < 3; i++) {
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

bool radioPoll(MeshPacket &pkt) {
    while (Radio.pollRx(pkt)) {
        if (pkt.decrypted) return true;
    }
    return false;
}

int slotForChanIdx(int chanIdx) {
    if (chanIdx >= 0 && chanIdx < s_chanCount) return chanIdx;
    if (chanIdx == DISCOVERY_SLOT) return -1;
    return -2;
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

bool radioSend(uint32_t to, int chanSlot, uint8_t hopLimit, uint32_t portnum,
               const uint8_t *payload, size_t len) {
    // Data { portnum (1, varint), payload (2, bytes), bitfield (9, varint) = 0 }.
    // bitfield is always written: 2.8 nodes ignore Data without it.
    uint8_t data[256];
    size_t n = 0;
    auto varint = [&](uint32_t v) { do { uint8_t b = v & 0x7F; v >>= 7; data[n++] = b | (v ? 0x80 : 0); } while (v); };
    if (len > 240) return false;
    data[n++] = (1 << 3) | 0; varint(portnum);
    data[n++] = (2 << 3) | 2; varint((uint32_t)len);
    memcpy(data + n, payload, len); n += len;
    data[n++] = (9 << 3) | 0; data[n++] = 0;
    return radioSendData(to, chanSlot, hopLimit, data, n);
}

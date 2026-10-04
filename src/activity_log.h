#pragma once
// In-RAM ring of recent sync events (Heltec V4 expansion display).
// Fixed capacity, oldest entry overwritten. Pure C++, no Arduino.
#include <stddef.h>
#include <stdint.h>

enum ActivityKind : uint8_t { ACT_DISCOVER, ACT_ANNOUNCE, ACT_REQUEST, ACT_BATCH, ACT_HELD, ACT_TX_FAIL };

struct ActivityEntry {
    uint32_t uptimeSec, unixTime;   // unixTime 0 = clock unset
    uint8_t  kind;
    uint32_t node;              // 0 for ACT_HELD
    int8_t   chanSlot;          // -1 discovery / none
    uint32_t a, b;              // REQUEST: a=cursor; BATCH: a=packets, b=messages
    uint8_t  flags;             // BATCH: 1 = MORE
};

constexpr int ACTIVITY_CAP = 50;

class ActivityLog {
public:
    void add(const ActivityEntry &e);
    // Newest first; returns the number copied (<= max, <= count()).
    int  copyNewest(ActivityEntry *out, int max) const;
    int  count() const { return count_; }

private:
    ActivityEntry ring_[ACTIVITY_CAP];
    int head_ = 0;   // next write slot
    int count_ = 0;
};

// Detail text, e.g. "REQUEST #LongFast from seq 41", "sent 3 packets (24 msgs), MORE".
// chanName may be null/empty (rendered "#?"). Always NUL-terminated within cap.
void activityDetail(const ActivityEntry &e, const char *chanName, char *out, size_t cap);

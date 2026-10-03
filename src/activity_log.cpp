#include "activity_log.h"
#include <stdio.h>

void ActivityLog::add(const ActivityEntry &e) {
    ring_[head_] = e;
    head_ = (head_ + 1) % ACTIVITY_CAP;
    if (count_ < ACTIVITY_CAP) count_++;
}

int ActivityLog::copyNewest(ActivityEntry *out, int max) const {
    int n = max < count_ ? max : count_;
    for (int i = 0; i < n; i++) {
        int idx = (head_ - 1 - i + 2 * ACTIVITY_CAP) % ACTIVITY_CAP;
        out[i] = ring_[idx];
    }
    return n > 0 ? n : 0;
}

void activityDetail(const ActivityEntry &e, const char *chanName, char *out, size_t cap) {
    if (!out || cap == 0) return;
    switch (e.kind) {
    case ACT_DISCOVER:
        snprintf(out, cap, "DISCOVER");
        break;
    case ACT_ANNOUNCE:
        snprintf(out, cap, "ANNOUNCE sent");
        break;
    case ACT_REQUEST: {
        const char *ch = (chanName && *chanName) ? chanName : "?";
        if (e.a == 0) snprintf(out, cap, "REQUEST #%s from start", ch);
        else snprintf(out, cap, "REQUEST #%s from seq %lu", ch, (unsigned long)e.a);
        break;
    }
    case ACT_BATCH:
        snprintf(out, cap, "sent %lu packet%s (%lu msg%s)%s", (unsigned long)e.a, e.a == 1 ? "" : "s",
                 (unsigned long)e.b, e.b == 1 ? "" : "s", (e.flags & 1) ? ", MORE" : "");
        break;
    case ACT_HELD:
        snprintf(out, cap, "held: airtime limit");
        break;
    case ACT_TX_FAIL:
        snprintf(out, cap, "send FAILED");
        break;
    default:
        snprintf(out, cap, "?");
        break;
    }
    out[cap - 1] = '\0';
}

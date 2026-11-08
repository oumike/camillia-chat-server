#include "sender_filter.h"
#include "settings.h"

bool senderAllowed(uint8_t level, const NodeNames &names, const PeerKeys &keys, uint32_t from) {
    const int parts = names.nameParts(from);
    switch (level) {
    case SENDER_FILTER_OFF:       return true;
    case SENDER_FILTER_NAME:      return parts != 0;
    case SENDER_FILTER_FULL_NAME: return parts == (NodeNames::LONG_NAME | NodeNames::SHORT_NAME);
    case SENDER_FILTER_NAME_KEY:  return parts != 0 && keys.find(from) != nullptr;
    default:                      return false;
    }
}

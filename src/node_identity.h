#pragma once
// NODEINFO broadcasts so nodes list the server by name (spec §5.4).
#include <stdint.h>
#include "settings.h"

void identityBegin(const Settings &s, uint32_t myNodeId);
void identityLoop(uint32_t nowMs);

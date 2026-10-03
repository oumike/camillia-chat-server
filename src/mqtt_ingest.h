#pragma once
// Receive-only MQTT (spec §5.7): subscribe to the monitored channels' topics,
// decode + decrypt each ServiceEnvelope, hand decrypted packets to the caller.
// This module never publishes.
#include <functional>
#include "mesh_proto.h"
#include "settings.h"

void mqttBegin(const Settings &s, uint32_t myNodeId, std::function<void(const MeshPacket &)> onPacket);
void mqttLoop(uint32_t nowMs);
const char *mqttState();   // "off", "no wifi", "connecting", "connected"
// Envelopes received / decrypted with a monitored channel key, since boot.
#include <Arduino.h>
String mqttDiagJson();   // why envelopes were dropped
void mqttCounters(uint32_t &received, uint32_t &decrypted);

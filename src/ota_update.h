#pragma once
// Over-the-air updates, ported from camillia-mt's ota_update.cpp (stable channel
// only). Releases come from the camillia OTA proxy over plain HTTP:
//   GET  http://ota.camillia.sumat.org/chat-server/latest            GitHub release JSON (tag_name)
//   GET  .../chat-server/<tag>/camillia-chat-server-<env>-<tag>-ota.bin      app image
//   GET  .../chat-server/<tag>/camillia-chat-server-<env>-<tag>-ota.bin.sig  DER ECDSA P-256 over SHA-256
// The image streams into the idle OTA slot and is only committed after its
// signature verifies against the baked-in public key (ota_signing_pubkey.h).
//
// Network work runs on its own FreeRTOS task so the radio and web server keep
// going. The device checks once after joining WiFi and then daily; with
// Settings::otaAutoInstall it installs a newer release by itself.
#include <Arduino.h>
#include "settings.h"

void otaBegin(const Settings *settings);
// Starts the boot / daily checks and any auto-install. Call every loop.
void otaLoop(uint32_t nowMs);
// False when a check or install is already running (or WiFi is down for install).
bool otaRequestCheck();
bool otaRequestInstall();
// {"state":"idle|checking|downloading|verifying|installed|error","current":"0.5.1",
//  "latest":"v0.6.0"|null,"updateAvailable":bool,"checkedAgoSec":n|null,
//  "written":n,"total":n,"error":"...","autoInstall":bool,"supported":bool}
String otaStatusJson();
// True once an image is committed; main restarts to boot it.
bool otaRestartPending();

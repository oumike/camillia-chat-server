#include "pki_identity.h"
#include <Arduino.h>
#include <Curve25519.h>
#include <Preferences.h>
#include <string.h>
#include "mesh_proto.h"

void pkiBegin() {
    Preferences p;
    if (!p.begin("cs-pki", false)) {
        // Left all zero: NODEINFO then advertises no key, rather than one that
        // would change on every boot.
        Serial.println("[cs] pki: NVS unavailable, PKI DMs disabled");
        return;
    }
    bool valid = false;
    if (p.getBytes("priv", myPrivKey, 32) == 32 && p.getBytes("pub", myPubKey, 32) == 32) {
        uint8_t derived[32];
        valid = Curve25519::eval(derived, myPrivKey, nullptr) && !memcmp(derived, myPubKey, 32);
    }
    if (!valid) {
        Curve25519::dh1(myPubKey, myPrivKey);
        p.putBytes("priv", myPrivKey, 32);
        p.putBytes("pub", myPubKey, 32);
        Serial.println("[cs] pki: generated a new key pair");
    }
    p.end();
    Serial.printf("[cs] pki: public key %02x%02x%02x%02x...\n", myPubKey[0], myPubKey[1], myPubKey[2],
                  myPubKey[3]);
}

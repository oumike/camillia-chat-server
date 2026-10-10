// Key copied from camillia-mt src/ota_signing_pubkey.h: both projects sign
// with the same key (the OTA_SIGNING_KEY secret). release.sh checks they match.
// ECDSA P-256 public key used to verify signed OTA images (see ota_update.cpp).
#pragma once

static const char OTA_SIGNING_PUBKEY_PEM[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MFkwEwYHKoZIzj0CAQYIKoZIzj0DAQcDQgAEiRPIGApb6inoZNZNsmnDydQELhdx\n"
    "lPplsHQ8jOmqk9wyMdAt5u/JosEcnpZhrAcH0xmEY/JfrI2+8d3kyyu1tw==\n"
    "-----END PUBLIC KEY-----\n"
    ;

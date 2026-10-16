#pragma once
// The server's Curve25519 key pair (mesh_proto's myPubKey/myPrivKey): made on
// first boot and kept in NVS, so the public key nodes learned from its NODEINFO
// stays valid across restarts. With it, NODEINFO advertises the key and nodes
// can send the server PKI DMs, which camillia-mt requires.
void pkiBegin();

### Fixed
- DMs from camillia-mt nodes (`help`, `status`) now reach the server. camillia-mt only sends DMs encrypted to the recipient's public key, and the server had none, so every DM failed on the node with "TX failed". The server now has its own key pair, made on first boot and kept across restarts, advertises it in its NODEINFO, and answers encrypted DMs the same way. The first DM from a node that has not yet heard the server's key still fails once; that attempt fetches the key and the next one goes through.

### Changed
- The server answers a NODEINFO request addressed to it right away, so a node that needs its key no longer waits for the three-hourly broadcast.
- DMs to the server are acknowledged, so they show as delivered on the sender.

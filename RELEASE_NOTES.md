### New
- New "Store messages from" setting in the web UI (`senderFilter` in YAML config) limits stored channel messages to identified senders. The options are Everyone, nodes with a name, nodes with both a long and short name, or nodes with a name and public key. It defaults to Everyone, and messages already stored are kept.

### Changed
- Node names and public keys heard from NODEINFO are now saved to flash and survive a restart, so the sender filter and PKI DMs work straight after boot. Changes are saved at most every 5 minutes and again before a restart.
- The storage capacity check now reserves flash for the saved node tables, which slightly lowers the largest messages-per-channel setting that fits.

### New
- Messages kept per channel can now be set from 10 to 1000 (default 250) in a new Storage card in the web UI, or under `storage: messagesPerChannel` in exported and imported settings.
- Saving or importing settings is refused, with an explanation, if the chosen channel count and messages per channel would not fit in the device's PSRAM or flash.
- Setting "Dimmed brightness" to 0 now turns the screen fully off when it dims, and it wakes on the current page.

### Changed
- Lowering messages per channel drops each channel's oldest messages beyond the new limit at the next restart.
- The on-device health page and the web storage view now show the configured per-channel capacity instead of a fixed 250.
- Existing settings carry over on upgrade, and messages per channel starts at 250.


### Update (v0.5.13)
### New
- The web config's firmware section now has a "Release notes" link next to the installed version, which opens that version's release page on GitHub.


### Update (v0.5.14)
### New
- Community Preset in the web config's Radio card sets the server up for a local mesh such as ChiMesh, MichMesh or Freq51, with about 40 to choose from. It sets the region, radio preset, frequency slot, channel list and the community's MQTT broker if it has one.
- Before a community preset is applied, the web config shows what will change and the community's own notes. Applying one replaces every channel, clears all stored message history and restarts the server.
- Community presets that this board's radio can't run, or whose MQTT broker needs TLS, are greyed out with the reason shown.
- The web flasher's Flash and Customize now works with the chat server. Right after flashing it sets the radio, up to 20 channels, node names, home Wi-Fi, a community's MQTT broker and a boot logo over USB, or starts from a community profile.
- TFT builds can show a custom boot logo in place of the camellia on the splash screen. OLED and e-paper builds don't show a logo.
- The USB serial console accepts camillia-mt's `cfg` commands for staging and committing settings, channels, MQTT and the boot logo. Committing clears every channel's stored history and restarts.

### Changed
- The serial console rejects lines over 256 bytes with an error, and names the command when it doesn't recognise one.
- MQTT brokers that need TLS are refused when set from the flasher or a community preset, since the server can't connect to them.

### Fixed
- CrowPanel builds, including the 5.0", no longer crash when the display's SPI DMA memory runs short. They now skip a frame instead.

### New
- Messages kept per channel can now be set from 10 to 1000 (default 250) in a new Storage card in the web UI, or under `storage: messagesPerChannel` in exported and imported settings.
- Saving or importing settings is refused, with an explanation, if the chosen channel count and messages per channel would not fit in the device's PSRAM or flash.
- Setting "Dimmed brightness" to 0 now turns the screen fully off when it dims, and it wakes on the current page.

### Changed
- Lowering messages per channel drops each channel's oldest messages beyond the new limit at the next restart.
- The on-device health page and the web storage view now show the configured per-channel capacity instead of a fixed 250.
- Existing settings carry over on upgrade, and messages per channel starts at 250.

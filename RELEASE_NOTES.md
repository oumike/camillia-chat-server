### New
- Firmware images for nine more boards: LilyGo T-Deck, T-Deck Pro (e-paper), T-LoRa Pager, Attaky Mesh Deck, Elecrow ThinkNode M9, Elecrow CrowPanel Advance 3.5" and 5.0", and LilyGo T-Display P4 with an SX1262 or an LR2021 radio. All except the Mesh Deck are untested on hardware.
- On larger screens the status pages scale up, with bigger text and more rows of messages, activity and channels.
- The T-Deck Pro shows a single plain status page on its e-paper screen and redraws it at most once a minute.
- On boards without touch, a button does the job of a tap: the trackball on the T-Deck, the wheel on the Pager, and BOOT on the M9 and T-Display P4.
- The T-Display P4 needs ESP-Hosted firmware on its ESP32-C6 for WiFi. camillia-mt's `flash-tdisplay-p4-c6.sh` installs the matching version.
- The CrowPanel 5.0" build is for board revisions V1.1 and later. A V1.0 board needs its own build made with `-DCROWPANEL_50_HW_V10=1`. The DIP switch on the back must be set to the wireless module.
- The CrowPanel boards have no battery sensing, so they show no battery level.
- `scripts/build-upload-monitor.sh` asks which board to build when none is given, and takes `--env`/`-e` to name any board directly.

### Changed
- `scripts/flash.sh` detects the chip itself, so it flashes the T-Display P4 (ESP32-P4) as well as the ESP32-S3 boards.

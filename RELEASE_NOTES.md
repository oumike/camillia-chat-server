### New
- Release builds now include `camillia-chat-server-wio-tracker-l2-<tag>.bin` for the Seeed Wio Tracker L2.
- The Wio Tracker L2 screen now works, with touch and adjustable backlight. Previously the server ran on this board without a display.
- On the Wio Tracker L2, pressing the Wake button acts as a tap on the screen.
- `flash.sh` can now flash Wio Tracker L2 firmware files.

### Changed
- On the Wio Tracker L2, if the touch controller doesn't respond at startup or keeps failing, touch is turned off and the server keeps running with the display only.

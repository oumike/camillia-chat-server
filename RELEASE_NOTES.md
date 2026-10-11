### New
- Over-the-air updates: a new **Firmware** card in the web config shows the installed and latest release, checks for updates and installs them with a progress bar.
- Every update image is checked against a built-in signing key before it is installed. Images that fail the check are rejected and the running firmware stays in place.
- New **Install updates automatically** option (`ota.autoInstall` in the YAML backup). When it is on, the server checks for updates 20 seconds after joining WiFi and then once a day, installs newer stable releases and restarts. It is off by default.
- Releases now include a signed OTA image for each board, next to the factory image. Flash the factory image over USB once; after that, updates can come over the air.
- The screen footer now shows the device's IP address on every page, on both the OLED and the colour touchscreen builds.
- The web status page now reports battery percentage and whether the device is on USB power, on battery or has no battery.

### Changed
- The `camillia-cs-<short name>` setup access point now turns off once the home WiFi network is joined. The web config is then at the IP shown in the screen footer. The access point comes back if the home network is down for 30 seconds.
- The web config has been redesigned. It has a header showing network, MQTT, battery and firmware version status, a card layout, and light and dark themes.
- In the channel editor, channels can be added and removed, each key is labelled with its type (default, AES-128, AES-256 or invalid), and a random AES-256 key can be generated with one click.
- A save bar at the bottom of the config page shows unsaved changes, and the page warns before leaving with changes that haven't been saved.
- The Messages tab is now **System Monitor**. It shows live uptime, network, MQTT and battery, along with storage use and stored messages.
- The battery indicator is now always on screen and shows a dash when no battery is connected.

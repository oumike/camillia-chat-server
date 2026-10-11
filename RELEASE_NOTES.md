### New
- Store messages for up to 20 channels, up from 10. The web settings page and YAML import both accept up to 20 channels.
- Each camillia-mt node now receives only the channels it has in common with the server, including channels past the 10th. Nodes running older firmware still receive the first 10 channels, as before.
- When more than 10 channels are configured, the status display lists the 9 busiest channels and adds a "+N more" line showing how many other channels there are and their total message count.

### Changed
- Settings from earlier versions, including channels, radio, MQTT, Wi-Fi, display and OTA options, are kept when upgrading to this release.

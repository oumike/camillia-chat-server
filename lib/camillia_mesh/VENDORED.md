# Vendored from camillia-mt

Source: `camillia-mt` commit `9796a7a` (v5.6.3). Copied by hand; carry upstream fixes over the same way.

| File | Origin | Local edits |
|---|---|---|
| `mesh_proto.{h,cpp}` | `src/mesh_proto.*` | `CHANNEL_KEYS[]` initializer replaced with 11 disabled slots (10 store + discovery), filled at runtime by `radio_link` |
| `mesh_radio.{h,cpp}` | `src/mesh_radio.*` | none (other boards' `#if DEVICE_*` branches compile out) |
| `xeddsa.{h,cpp}` | `src/xeddsa.*` | none (needed only because `mesh_proto` links it) |
| `mesh_channel_plan.h` | `src/mesh_channel_plan.h` | none |
| `mesh_channel_plan.cpp` | `src/config_io.cpp` lines 19–349 (channel plan tables and slot math) | new file header; includes `config.h` and `mesh_proto.h` |
| `config.h` | shim for `src/config.h` | radio/region constants only; pins come from `src/board.h` |
| `debug_flags.{h,cpp}` | shim for `src/debug_flags.*` | message logs go to `Serial`; ack/GPS logs dropped |

`myPubKey`, `myPrivKey` and `myDeviceRole` (owned by camillia-mt's UI layer) are defined in `src/main.cpp`; PKI is unused here.

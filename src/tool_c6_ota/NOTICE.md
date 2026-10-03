# Third-party notice for `src/tool_c6_ota`

`main/main.c` is MIT (repository `LICENSE`). It follows the call sequence of Espressif's
`host_performs_slave_ota` example (Apache-2.0, `examples/host_performs_slave_ota/main/main.c` in
`src/bedside_clock/components/espressif__esp_hosted/`); the example's SPDX header is Apache-2.0
and the example is credited in the file header.

The tool links the `esp_hosted` host library from `src/bedside_clock/components/` (Apache-2.0,
see that folder's `LICENSE`) and embeds a C6 slave image at build time. **No slave image is
committed**: `devices/*/firmware/*.bin` is gitignored. The image used on 2026-10-03,
`network_adapter_esp32c6.bin` 2.12.12, comes from
https://github.com/esphome/esp-hosted-firmware (Apache-2.0, built from Espressif's `esp_hosted`
slave); its SHA-256 is recorded in `README.md`.

The full attribution list is the repository's [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

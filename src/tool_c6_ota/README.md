# tool_c6_ota: reflash the ESP32-C6 esp_hosted slave over SDIO

One-shot ESP32-P4 firmware that streams an esp_hosted **slave** image to the C6 through the
existing SDIO link (`esp_hosted_slave_ota_*`, no wires). Written for the Guition
JC8012P4A1C 10.1", whose C6 ships with slave 2.3.0 while `bedside_clock` links host 2.12.12.

## Use

1. Put the slave image at `devices/esp32-c6/firmware/network_adapter_esp32c6-2.12.12.bin`
   (from https://esphome.github.io/esp-hosted-firmware/v2.12.12/network_adapter_esp32c6.bin,
   SHA-256 `bad97ce81e7fcf5f3365898f633b80941ae863db9c754d87a78f89c8f61f2e94`), or pass
   `-D C6_IMAGE=<path>`.
2. `idf.py set-target esp32p4 && idf.py -p COM4 flash monitor`
3. Read the log: the slave version before, the transfer, `ota_end -> ESP_OK`, and after the
   automatic restart the new version line. A second boot of the tool says "already runs
   2.12.12: nothing to do".
4. Flash the real firmware back (`bedside_clock`).

If `ota_begin`/`ota_end` fail the C6 keeps its current firmware; the UART route via the CN5
header is documented in `devices/guition-jc8012p4a1c/docs/c6-firmware.md`.

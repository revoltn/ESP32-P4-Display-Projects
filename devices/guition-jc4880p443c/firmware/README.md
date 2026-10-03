# Firmware images for the JC4880P443C

This folder is tracked but holds no binaries: `devices/*/firmware/*.bin` and `*.zip` are gitignored.
It documents (1) the factory images Guition ships in the vendor package, with hashes so a download
can be checked, (2) how to restore a bricked board, (3) the ESP32-C6 co-processor firmware, and
(4) how firmware built from this repo is packaged and published.

## 1. Factory images (vendor package, local only)

Location: `devices/guition-jc4880p443c/vendor/package/8-Burn operation/Burn files/` (gitignored;
provenance in [../vendor/README.md](../vendor/README.md)). Sizes from `ls -l`, SHA256 computed
2026-10-02, structure read with `esptool image_info` and by parsing the partition table at 0x8000.

| File | Bytes | SHA256 (first 16) | What it is |
|---|---|---|---|
| `P4-JC4880P443C_I_W_V2.2.bin` | 13,762,560 | `f7afdfd68ea0d57b` | ESP32-P4 full-flash image for offset **0x0**: 0xFF padding, bootloader at 0x2000, partition table at 0x8000 with `nvs` 0xb000 (24 KB), `phy_init` 0x11000, `factory` 7 MB at 0x20000, `storage` 6 MB SPIFFS at 0x720000. The esp-brookesia phone launcher demo (camera, music, video player). Ends exactly at 0xD20000. |
| `P4-JC4880P443C_I_W-V2.1.bin` | 13,762,560 | `9e61c84bd9c31541` | Same layout, previous revision of the same demo. |
| `JC4880P443C_I_W_xiaozhi2.0.4.bin` | 9,981,049 | `b12e316fe8edebba` | ESP32-P4 full-flash image for 0x0: `nvs`, `otadata`, `phy_init`, `ota_0` and `ota_1` 3.94 MB each at 0x20000 / 0x410000, `assets` 8 MB at 0x800000. xiaozhi-esp32 voice assistant 2.0.4 built on IDF v5.5.1; it fetches OTA from `api.tenclass.net`. |
| `C6-JC4880P443-2.1.10.bin` | 1,428,880 | `7b68c4cff6e3cf28` | ESP32-C6 full-flash image for 0x0 (bootloader at 0x0, table at 0x8000: `nvs`, `otadata`, `phy_init`, `ota_0` 1.5 MB at 0x10000, `ota_1` 1.5 MB at 0x190000). esp_hosted co-processor firmware; the binary names IDF v5.4.2. |
| `JC-C6-slave_v2.3.2.bin` | 1,196,480 | `f9ec1205c5fa1e49` | ESP32-C6 full-flash image, same layout; esp_hosted slave v2.3.2. |

Full hashes:

```
7b68c4cff6e3cf288896a89b4a09379396f62c6bfbfbdc3984b9e142cfb4db0c  C6-JC4880P443-2.1.10.bin
f9ec1205c5fa1e497032dedcee33d2ab9560bd2768fc1ea06acb6d8e2773f80c  JC-C6-slave_v2.3.2.bin
b12e316fe8edebba91322f8664b50515caa84baaa0c04bd9901574ecad3db0d8  JC4880P443C_I_W_xiaozhi2.0.4.bin
9e61c84bd9c3154143695d66d94b354d958b803188e018d480b133bbaf6e34b9  P4-JC4880P443C_I_W-V2.1.bin
f7afdfd68ea0d57b391b2eba40531d419c4a5e969a3ecdcb4262b7deae9bca3d  P4-JC4880P443C_I_W_V2.2.bin
```

Read these before trusting the images:

- Neither C6 image matches what the author's unit shipped with (esp_hosted slave **2.12.12**, read
  from the handshake log). Guition batches differ; boards with slave 2.3.0 are documented in the
  community ([docs/05 §7](../docs/05-troubleshooting.md)). Flashing one of these C6 images onto a
  2.12.12 board is a downgrade and breaks a host pinned to `~2.12.0`.
- The two P4 demo images cover 0x0 to 0xD20000 of a 16 MiB flash. Writing one at 0x0 leaves whatever
  sits above 0xD20000 untouched; run `erase_flash` first if you want a true factory state.
- The vendor tool's `flash_download_tool/dl_temp/` cache also holds `JC4848P443_v1.0.bin_rep` and
  `JC4880P443-V1.2.bin_rep`: another product and an older build. Not for this board.
- `flash_download_tool/logs/*.txt` are the vendor's own flashing-session logs and contain the MAC
  addresses of their test units.

Where the package comes from: `https://pan.jczn1688.com/directlink/1/HMI%20display/JC4880P443C_I_W.zip`.
That URL is not a file; it serves a JavaScript single-page downloader that fetches a manifest and
SHA256-named shards. The recipe (manifest endpoint, shard concatenation, per-shard verification) is
in [../../guition-jc8012p4a1c/vendor/README.md](../../guition-jc8012p4a1c/vendor/README.md); the
10.1" board's `vendor/fetch_vendor_zip.sh` is the working script. Our local copy is the
309,232,242-byte zip downloaded 2026-08-29 (SHA256 in [../vendor/README.md](../vendor/README.md)).
On 2026-10-02 the vendor manifest advertised a 477,630,572-byte version in 10 shards, so a newer
package with possibly different images exists upstream.

Why the images are not hosted here: Guition publishes no licence for them, the C6 images are
Espressif esp_hosted builds whose redistribution we have not cleared, and the xiaozhi image talks to
a third-party cloud by default. Hashes let you verify your own download; that is the limit.

## 2. Unbrick / factory restore

Vendor procedure (from `8-Burn operation/` and [docs/01](../docs/01-hardware.md),
[docs/05](../docs/05-troubleshooting.md)):

1. Power: at least **600 mA** available on the USB port (the vendor's first instruction; a weak hub
   browns out mid-write).
2. Cable into the **USB2** socket, which is the USB-Serial/JTAG one: the socket that gives you a COM
   port (COM3 on the author's PC). The high-speed socket does not expose the ROM loader as a COM port.
3. `flash_download_tool_3.9.9` (in the package; Espressif freeware, 3.9.9 added "ESP32-P4 USB
   download"): chip ESP32-P4, LoadMode USB, add the merged `.bin` at address **0x0**, keep the tool's
   SPI defaults, START. The three screenshots in `Burn operation instructions/` show the dialog.
4. If the board does not enter download mode by itself (an app that broke USB): hold BOOT (GPIO35)
   while pressing RST, then retry.

esptool equivalent (any OS; use the IDF Python environment, esptool 4.12 was used here):

```bash
# 0. back up what is there first (16 MiB; a few minutes at 921600)
esptool.py --chip esp32p4 --port COM3 --baud 921600 read_flash 0x0 0x1000000 backup-YYYYMMDD.bin

# 1. optional, true factory state
esptool.py --chip esp32p4 --port COM3 erase_flash

# 2. write the merged image at 0x0; flash mode/size/frequency stay "keep" and come from the image header
esptool.py --chip esp32p4 --port COM3 --baud 921600 write_flash 0x0 "P4-JC4880P443C_I_W_V2.2.bin"
```

Then `esptool.py --chip esp32p4 --port COM3 flash_id` must report `Chip is ESP32-P4 (revision v1.3)`
(or whatever your unit is) and the boot log should show the esp-brookesia launcher. The vendor images
are built for rev v1.x silicon; anything you build yourself must be too ([docs/05 §2](../docs/05-troubleshooting.md)).

## 3. ESP32-C6 co-processor firmware

The C6 runs the esp_hosted "network adapter" (slave) from its own flash; its partition table spans
3.1 MB, so the module carries at least 4 MB there. The P4 only talks to it over SDIO.

- This unit: esp_hosted slave **2.12.12**, capabilities WLAN + HCI over SDIO, BLE only. The host
  component must match the slave's major.minor (`espressif/esp_hosted: "~2.12.0"`); WiFi tolerates a
  mismatch with a warning, Bluetooth does not ([docs/04](../docs/04-bluetooth.md)).
- esp_hosted 3.x on the host needs a 3.x slave. Deferred in this repo
  ([docs/03](../docs/03-working-components.md)); it is a C6 reflash plus host patches, not a version bump.
- Update paths. (a) Wire-free: `esp_hosted_slave_ota_*` over SDIO with the official images from
  <https://esphome.github.io/esp-hosted-firmware/>, as EspControl's `c6_recovery` component and
  ESPHome's `esp32_hosted` update platform do; untested here
  ([docs/07](../docs/07-espcontrol-findings.md), [docs/08](../docs/08-esphome-findings.md)).
  (b) UART through the JP1 header (`C6_U0RXD`, `C6_U0TXD`, `C6_IO9`, `C6_CHIP_PU`), community-verified,
  park the P4 in download mode first or it keeps resetting the C6 mid-write
  ([docs/05 §7](../docs/05-troubleshooting.md)). The official `network_adapter_esp32c6.bin` is an app
  image for `ota_0` at 0x10000; the two vendor C6 files above are full-flash images for 0x0.
- The C6 keeps its firmware when the P4 is reflashed; the two are independent.
- Cross-board notes on the C6 live in [../../esp32-c6/README.md](../../esp32-c6/README.md).

## 4. Firmware built from this repo

| App | CMake project | Partition table (`partitions.csv`) |
|---|---|---|
| `src/bedside_clock` | `bedside_v01` | `nvs` 0xb000 24 KB, `phy_init` 0x11000, `factory` 8 MB at 0x20000, `model` 4 MB SPIFFS at 0x820000 (esp-sr models, label must be `model`), `coredump` 128 KB at 0xC20000 |
| `src/sample_hello_world` | `hello_world` | `nvs` 0xb000 24 KB, `phy_init` 0x11000, `factory` 4 MB at 0x20000 |
| `src/sample_wakeword_bench` | `mww_test` | same table as bedside_clock so `app-flash` can swap apps without moving `model` |

Produce one image that restores a board the same way the factory image does:

```bash
idf.py build
idf.py merge-bin                      # IDF 5.4 and later; writes build/merged-binary.bin for offset 0x0
# explicit form, from the build directory, if you want to name the output or pick the format:
esptool.py --chip esp32p4 merge_bin -o bedside_clock-merged.bin @flash_args
```

Check `build/flash_args` lists every image you expect. For bedside_clock that includes the esp-sr
`srmodels.bin` for the `model` partition at 0x820000; it is written by a full `idf.py flash`, never
by `app-flash`, and a merged image without it boots to a voice pipeline that finds no models. Flash a
merged image exactly like the factory one: `write_flash 0x0 <file>`.

Naming convention for anything that leaves the build directory:

```
<app>-jc4880p443c-v<app version>-idf<idf version>-<git short sha>-<merged|app>.bin
bedside_clock-jc4880p443c-v0.11-idf5.5.5-1a2b3c4-merged.bin
bedside_clock-jc4880p443c-v0.11-idf5.5.5-1a2b3c4-merged.bin.sha256
```

`merged` is the 0x0 image, `app` is the bare application for `write_flash 0x20000` on a board whose
table and bootloader already match. The app version is the project's `PROJECT_VER` if set, else the
build date as `YYYYMMDD`.

Publishing: attach the files to a GitHub Release, one release per build that was flashed and
verified on the board. Never commit binaries; `devices/*/firmware/*.bin` is gitignored on purpose and
git history is the wrong place for 13 MB blobs. Release notes must state the IDF tag, the
`dependencies.lock` it was built with, sdkconfig deltas from `sdkconfig.defaults`, the C6 slave
version it was tested against, and the P4 silicon revision.

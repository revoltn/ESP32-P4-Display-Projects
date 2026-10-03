# Firmware images for the JC8012P4A1C_I_W_Y (not hosted here)

This folder documents the vendor's factory and co-processor images and how to use them. The
binaries themselves are **not** in the repository: `.gitignore` excludes
`devices/*/firmware/*.bin`, and there is no redistribution licence from Guition for the P4
images (which bundle vendor code without a licence, media files of unknown rights and
Espressif libraries) nor source for the vendor's `-dirty` C6 builds. Get them from the
vendor package (see [../vendor/README.md](../vendor/README.md)); the SHA256s below let you
check what you downloaded. All image facts come from
[../vendor/INVENTORY.md](../vendor/INVENTORY.md) section 5 (headers and strings read from
the files in the GitHub mirror).

## 1. Inventory

Package path: `vendor/package/8-Burn operation/Burn files/`. All four are complete flash
images (bootloader + partition table + app) meant for offset **0x0**.

| File | Bytes | SHA256 | Target chip | What it is | Build info |
|---|---|---|---|---|---|
| `JC8012P4A1_P4-V2.8.bin` | 13,697,024 | `ef972908b06889aa1cf43194c613fa848804f17a444b847b954615f05a59c4b8` | **ESP32-P4** (bootloader at 0x2000, app at 0x10000) | Factory demo: `esp_brookesia_demo` 1.0.1 (esp-brookesia phone UI with camera, music, 2048, settings, WiFi via esp_hosted host) including the SPIFFS media | IDF `v5.5-dev-3618-g7f6e7f4506-dirty`, built Jun 20 2025 18:02:02; partitions nvs 0x9000/0x6000, phy_init 0xf000, factory 0x10000/0x900000, storage (spiffs) 0x910000/0x400000 |
| `JC8012P4A1C_I_W_Y_xiaozhi2.0.4.bin` | 11,240,285 | `7bc321ff9c27aaa8b678e968ec12e7ec681242352caa7431fdb1d9651b25ae2e` | **ESP32-P4** (bootloader at 0x2000, app at 0x20000) | xiaozhi-esp32 voice assistant 2.0.4 for board id `guition-jc8012p4a1` (source: github.com/78/xiaozhi-esp32, MIT) | IDF `v5.5.1-dirty`, built Dec 25 2025 17:25:06; partitions nvs, otadata 0xd000, phy_init, ota_0 0x20000/0x3f0000, ota_1 0x410000/0x3f0000, assets 0x800000/0x800000 |
| `JC-C6-slave_v2.3.2.bin` | 1,196,480 | `f9ec1205c5fa1e497032dedcee33d2ab9560bd2768fc1ea06acb6d8e2773f80c` | **ESP32-C6** (image chip id 0x000d; bootloader at 0x0, app at 0x10000) | ESP-Hosted-MCU slave (`network_adapter`), SDIO slave + BLE HCI; the file name says v2.3.2, the unit reports 2.3.0 at runtime | version string `f0a63f7-dirty`, IDF `v5.5`, built Aug 26 2025 11:55:53; partitions nvs 0x9000/0x4000, otadata 0xd000/0x2000, phy_init 0xf000, ota_0 0x10000/0x180000, ota_1 0x190000/0x180000 |
| `JC8012P4A1_C6.bin` | 1,146,768 | `8b16a042713bca67d64a1d7af6a296432c1c5f8116fe015ae24acfca3a66fec0` | **ESP32-C6** (same layout) | Older ESP-Hosted-MCU slave build | version string `54aafd0-dirty`, IDF `v5.4-dev-3951-g9106c43acc-dirty`, built Dec 26 2024 18:41:41 |

Neither C6 commit hash (`f0a63f7`, `54aafd0`) exists in `espressif/esp-hosted-mcu`
(INVENTORY 5.1): these are vendor-local trees. Which of the two the factory installs is not
documented; this unit reports slave 2.3.0 (boot log), consistent with the newer file's name.

The image **currently on this unit is not in the package**: the boot log shows
`esp_brookesia_demo 1.0.1`, `ESP-IDF v5.5.4-dirty`, compiled **Jul 3 2026 19:41:38**, ELF
SHA256 prefix `b8d6d6c61`, whereas the packaged P4 image was built Jun 20 2025 with IDF
v5.5-dev. The live vendor zip (shards dated 2026-08-31) may contain the newer build; the
mirror does not. Hence step 2.

## 2. Back up the factory image first (not yet done on this unit)

```
esptool.py --chip esp32p4 -p COMx -b 1152000 read_flash 0 0x1000000 jc8012p4a1c-factory-backup-YYYY-MM-DD.bin
sha256sum jc8012p4a1c-factory-backup-YYYY-MM-DD.bin
```

16,777,216 bytes. At 1.152 Mbps the transfer alone is about two minutes, in practice several
minutes (esptool reads in 4 KB blocks with per-block overhead; the vendor's own tool logs in
the package record a 16 MB operation). Use the native USB-Serial/JTAG port (COM4 on the
owner's machine) or the CH340 port; both reach the P4 bootloader. Keep the backup outside
the repository (binaries are gitignored anyway). Restoring it is `write_flash 0x0 <backup>`.

The backup also captures NVS (the demo's stored WiFi settings) and, for a xiaozhi image,
device credentials; treat it as private.

## 3. Vendor flashing recipe (P4)

From `8-Burn operation/烧录说明：Burning instructions.txt` and the three screenshots in
`Burn operation instructions/`:

1. Install the CH340 driver (`7-Character&Picture_Molding_Tool/USB-SERIAL CH340.rar`).
2. Supply more than 600 mA.
3. Use the **USB1** socket (the CH340C bridge on the schematic, sheet 6).
4. `flash_download_tool_3.9.7` (in the package; 3.9.7 is the first release with ESP32-P4
   support per its `doc/release_note.txt`): ChipType ESP32-P4, WorkMode Develop, LoadMode
   UART; SPIDownload tab, one file at **`0x0000`**, SPI SPEED **80MHz**, SPI MODE **QIO**,
   DoNotChgBin checked, BAUD **1152000**, START.

The tool's `configure/` and `dl_temp/` folders contain the vendor PC's own history (other
products' firmware, COM port numbers, employee paths, device MACs in `logs/`): irrelevant,
do not redistribute (INVENTORY 2.9).

## 4. esptool equivalent

```
esptool.py --chip esp32p4 -p COMx -b 1152000 write_flash 0x0 JC8012P4A1_P4-V2.8.bin
```

Notes:

- The merged image already contains the bootloader at its P4 offset (0x2000), the partition
  table at 0x8000 and the app; writing at 0x0 is correct, do not add `--flash_mode`/`--flash_freq`
  (the vendor ticks DoNotChgBin for the same reason).
- `-b 1152000` is the vendor's rate; drop to 460800 if the CH340 path shows checksum errors.
- Over the native USB-Serial/JTAG port esptool auto-resets; over the CH340 port the DTR/RTS
  circuit (sheet 6, Q2/Q3 to BOOTMODE and CHIP_PU) does it. Manual fallback: hold BOOT (SW3)
  while tapping RST (SW2).
- `--after hard_reset` is the default; the demo boots straight into the phone UI.
- A P4 image built for rev 3.x silicon is refused on this rev v1.3 chip; the vendor images
  are rev 1.x builds (`Min chip rev v1.0, Max chip rev v1.99` in the boot log).

## 5. C6 images

The two `*C6*.bin` files are **ESP32-C6** images. They go to the C6 over its own UART header
(CN5, schematic sheet 5) with `esptool.py --chip esp32c6 ... write_flash 0x0 <file>`, or the
app portion goes over SDIO via slave OTA; both paths are described, untested, in
[../docs/c6-firmware.md](../docs/c6-firmware.md).

**Never flash a C6 image onto the P4.** It would overwrite the P4 bootloader with an image
for a different chip; the P4 then does nothing until a correct image is written at 0x0
again (recoverable, but pointless). esptool checks the image header's chip id for images
that carry one and refuses with a chip-mismatch error unless `--force` is given; do not use
`--force` [tool behaviour, confirm on your esptool version]. The P4 and the C6 never share a
serial port on this board: the P4 is reached through USB1/USB3, the C6 only through CN5.

Restoring `JC-C6-slave_v2.3.2.bin` puts the C6 back into the mismatched factory state
(slave 2.3.0 against a 2.11/2.12 host); it is a rollback target, not an upgrade.

## 6. Where to download

- Vendor package via the GUITION-DISK downloader
  (`https://pan.jczn1688.com/directlink/1/HMI%20display/JC8012P4A1C_I_W_Y.zip`, 421,427,604
  bytes, sharded; see [../vendor/README.md](../vendor/README.md) for `fetch_vendor_zip.sh`).
- GitHub mirror of the package (same four files, SHA256s above):
  `https://github.com/DevinWatson/10.1-inch-ESP32P4-Xiaozhi-ESP32-C6-JC8012P4A1C_I_W_Y`
  under `8-Burn operation/Burn files/`.
- Upstream esp_hosted slave images (Apache-2.0, versioned, SHA256 in the manifest):
  `https://esphome.github.io/esp-hosted-firmware/manifest/esp32c6.json`.
- xiaozhi source for the voice-assistant image: `https://github.com/78/xiaozhi-esp32`
  (board folder `guition-jc8012p4a1`).

Why not host them here: no licence to redistribute (vendor code, Silead touch firmware
inside the P4 images, media files named after commercial recordings, closed-source Espressif
flash tool), and the C6 builds are modified Apache-2.0 code without the corresponding
source. Links plus hashes are the honest alternative. The same reasoning applies to this
repo's own 10.1" builds: they are source-only (see
[../docs/touch-gsl3680.md](../docs/touch-gsl3680.md), section 5).

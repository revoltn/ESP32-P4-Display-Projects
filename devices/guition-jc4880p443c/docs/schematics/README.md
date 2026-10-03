# Schematic sheets (local only)

Five sheets of the JC4880P443 schematic, re-encoded as webp from the PNG sheets in the vendor
package. **The image files are gitignored** (`devices/*/docs/schematics/*.webp` in the root
`.gitignore`): they are Guition's copyright and the package carries no redistribution licence, so a
fresh clone contains only this README. Regenerate them from the vendor package as described below,
or open the PNG sheets there directly.

The webp files are pixel-for-pixel re-encodes of the vendor PNGs (sizes checked 2026-10-02), so the
mapping is exact.

| File (webp) | Pixels | Vendor source sheet | What it shows |
|---|---|---|---|
| `01-audio-es8311_sd-card_wifi-antenna.webp` | 1753x601 | `5-Schematic/6_CODEC&TFCARD.png` | ES8311 codec wiring (I2S on GPIO 9/10/12/13/48, I2C 7/8), the TF-card slot on SDMMC 39-44 with `TF_VCC` fed from LDO output VO4 through an always-on P-FET (R13 holds the gate low, the GPIO45 enable is not populated), R11/R12 data-line pull-ups, the C6 WiFi antenna |
| `02-speaker_power-3v3_uart-input_battery.webp` | 697x1123 | `5-Schematic/1_PWR.png` | NS4150 speaker amplifier with PA enable GPIO11 and the MX1.25 speaker header, TLV62569 3V3 buck, UART0 input header CN2 (GPIO37/38, VIN through an AO3401 P-FET), IP5306 charger/boost, battery divider to GPIO53 |
| `03-expand-io_usb-fs-hs_lcd-backlight.webp` | 1356x608 | `5-Schematic/4_USB&IO.png` | JP1 2x13 expansion header including `C6_U0RXD`, `C6_U0TXD`, `C6_IO9`, `C6_CHIP_PU`; both USB-C sockets (full-speed `USB1P1` = USB-Serial/JTAG, high-speed `ESP_USB`) with 5.1 k CC pull-downs on the shared `USB5V_IN` rail; MP3202 backlight boost on GPIO23 |
| `04-rs485-max485.webp` | 898x715 | `5-Schematic/5_485.png` | MAX485 transceiver on UART1 (GPIO26/27) with the 74LVC1G132 + S8550 automatic direction circuit, J4 RS-485 and J5 TTL headers, TVS diodes and fuses |
| `05-adc-mic.webp` | 872x293 | `5-Schematic/7_OTHER.png` (873x293) | MSM381A3729H9CP analog microphone into the ES8311 ADC input |

Not converted, open the PNGs in the package: `2_LCD&CSI.png` (770x987: the ST7701 DSI panel FPC,
LCD reset GPIO5, the OV02C10 CSI FPC) and `3_ESP32-P4.png` (1962x1019: the module pinout, strapping
pins, SDIO 14-19 and the C6 reset on GPIO54).

`docs/01-hardware.md` and `docs/interface-types.md` cite "sheet 01" to "sheet 05" using the webp
numbering in the first column.

## Where the sources are

`devices/guition-jc4880p443c/vendor/package/5-Schematic/` (gitignored, from `JC4880P443C_I_W.zip`;
provenance in [../../vendor/README.md](../../vendor/README.md)):

- `1_PWR.png` to `7_OTHER.png`: seven raster sheets dated 2025-07-24.
- `JC4880P443_V1.0.pdf`: the full schematic as one vector page (1.0 MB, dated 2025-09-27). It zooms
  without loss and is text-searchable for net names such as `USB5V_IN`, `TF_VCC`, `C6_CHIP_PU`.

## Regenerating the webp files

From the PNGs (exact pixel match, which is how the current files were made). Save this as
`regen.py` in this folder and run `python regen.py` after `pip install pillow`:

```python
from PIL import Image
src = "../../vendor/package/5-Schematic"
pairs = {
    "01-audio-es8311_sd-card_wifi-antenna.webp": "6_CODEC&TFCARD.png",
    "02-speaker_power-3v3_uart-input_battery.webp": "1_PWR.png",
    "03-expand-io_usb-fs-hs_lcd-backlight.webp": "4_USB&IO.png",
    "04-rs485-max485.webp": "5_485.png",
    "05-adc-mic.webp": "7_OTHER.png",
}
for out, png in pairs.items():
    Image.open(f"{src}/{png}").convert("RGB").save(out, "WEBP", quality=90, method=6)
```

From the PDF, when a different crop or a higher resolution is wanted: render the page with Poppler
(`pdftoppm -r 200 -png JC4880P443_V1.0.pdf sheet`) or ImageMagick
(`magick -density 200 JC4880P443_V1.0.pdf sheet.png`), crop the block you need in any image editor,
and save it as webp with the same file name so the links in `01-hardware.md` keep working.

Do not commit the results: the ignore rule exists so that nobody has to decide per file whether a
schematic crop is fair use.

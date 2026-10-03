# Archived material, inventory of 2026-08-30

Written on 2026-08-30 as `_archived/README.md` when superseded or redundant samples were moved out of
the working trees per the coverage verdict in
[devices/guition-jc4880p443c/docs/08-esphome-findings.md](../devices/guition-jc4880p443c/docs/08-esphome-findings.md).
On 2026-10-02 the repository was reorganised and `_archived/` dissolved into the device and reference
folders; this file keeps the record with the paths rewritten to the current layout. Everything listed
is gitignored content; only the READMEs around it are tracked.

| Item (path on 2026-08-30) | Why archived | Where it is now |
|---|---|---|
| `demo_docs_specs/1-Demo/idf_examples/ESP-IDF_5.5.4/lvgl_demo_v8`, `lvgl_demo_v9`, `lvgl_sw_rotation`, `esp_draw_bits` | Minimal LVGL bootstraps, superseded by `src/sample_hello_world` (then `src/hello_world`) and ESPHome's P4 LVGL fixtures; `lvgl_demo_v9` has a known build bug (missing semicolon) | `devices/guition-jc4880p443c/vendor/package-archived-demos/1-Demo/idf_examples/ESP-IDF_5.5.4/` |
| `demo_docs_specs/1-Demo/arduino_examples/` | Arduino-framework variants of covered functionality; the battery-ADC thresholds were extracted into [docs/01-hardware.md](../devices/guition-jc4880p443c/docs/01-hardware.md) | `devices/guition-jc4880p443c/vendor/package-archived-demos/1-Demo/arduino_examples/` |
| `demos-docs-samples-2/examples/01-08, 11` | Display, touch, WiFi, SD, reset and MP3 examples covered better by the vendor demo, ESPHome fixtures or `src/sample_hello_world`. **`07_bluetooth` is outdated**: it predates esp_hosted 2.5.2's controller-disabled-by-default change and would hit endless HCI timeouts on current C6 firmware; see [docs/04-bluetooth.md](../devices/guition-jc4880p443c/docs/04-bluetooth.md) for the working recipe | `devices/guition-jc4880p443c/community/bigbag-JC4880P443C-examples/archived-examples/examples/` (the kept examples `09`, `10`, `12` are in `.../trimmed/examples/`) |
| `ESPControl/` | A single `.url` shortcut to the EspControl installer page | No longer kept. The installer page is <https://jtenniswood.github.io/espcontrol/> and the source copy is `reference/espcontrol/upstream/` ([README](espcontrol/README.md)) |
| `*.zip` | Original archives of the trees extracted elsewhere in the repo | `devices/guition-jc4880p443c/vendor/JC4880P443C_I_W.zip` (vendor package, 295 MB), `devices/guition-jc4880p443c/community/seobaeksol-JC4880P443C_I_W/JC4880P443C_I_W-main.zip`, `devices/guition-jc4880p443c/community/bigbag-JC4880P443C-examples/JC4880P443C-examples-main.zip`, `reference/esphome/esphome-dev.zip`, `reference/espcontrol/espcontrol-main.zip`. Sizes and SHA256 in the README next to each |

Nothing listed here is needed to build anything in `src/` (the BSP components the apps use were
copied into each project's `components/`). The **factory restore images** were never archived; they
remain in `devices/guition-jc4880p443c/vendor/package/8-Burn operation/Burn files/` and are
inventoried with hashes in
[devices/guition-jc4880p443c/firmware/README.md](../devices/guition-jc4880p443c/firmware/README.md).

Name changes of 2026-10-02 that affect the rows above: `demo_docs_specs/` became
`devices/guition-jc4880p443c/vendor/package/` for the vendor files, while its `01_*.md` to `10_*.md`,
`90_*.md` and `README.md` (a copy of github.com/seobaeksol/JC4880P443C_I_W, not vendor material) went
to `devices/guition-jc4880p443c/community/seobaeksol-JC4880P443C_I_W/`; `demos-docs-samples-2/`
(github.com/bigbag/JC4880P443C-examples) became
`devices/guition-jc4880p443c/community/bigbag-JC4880P443C-examples/trimmed/`;
`demo-espcontrol-main/` became `reference/espcontrol/upstream/`; `demo-esphome-dev/` became
`reference/esphome/upstream/`.

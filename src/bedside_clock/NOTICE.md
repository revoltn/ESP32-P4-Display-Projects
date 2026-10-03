# Third-party notice for `src/bedside_clock`

The application code in `main/`, `tools/`, `www/`, the generated font tables in `main/fonts/`
and the wake-word models in `main/models/` are MIT (repository `LICENSE`). The full
attribution list with versions, commits and licences is the repository's
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md); this file lists only what this project
vendors or modifies.

## Vendored components (`components/`), all Apache-2.0

| Component | Version | Modified here |
|---|---|---|
| `bsp_extra` (Espressif, via the Guition package) | 0.0.2 | yes: `include/bsp_board_extra.h`, `src/bsp_board_extra.c` (mono downmix, `bsp_extra_sink_t` USB output table, `bsp_extra_i2s_write_codec`, `bsp_extra_codec_get_fs`, unchanged-format `set_fs` early-out) |
| `chmorgan__esp-audio-player` (Espressif, Chris Morgan) | 1.0.7 | yes: `audio_player.cpp` (ID3v2 skip, write pacing, PAUSE state), `audio_mp3.{h,cpp}` (give up after 500 consecutive decode errors) |
| `esp_cam_sensor` (Espressif) | 2.1.0 | no (Guition added the OV02C10 driver) |
| `espressif__esp32_p4_function_ev_board` (Espressif) | 5.2.3 | yes (Guition wired the ST7701 panel; 2026-10-03 bedside_clock added the 10.1" JC8012P4A1C branch): `Kconfig` (panel revision and touch orientation options), `esp32_p4_function_ev_board.c`, `idf_component.yml`, `include/bsp/display.h`, `include/bsp/esp32_p4_function_ev_board.h`, new `priv_include/jd9365_init_jc8012p4a1_v2.h` (table from ESPHome, MIT, see its header) |
| `espressif__esp_hosted` (Espressif; bundles protobuf-c, BSD-2-Clause) | 2.12.12 | yes: `host/port/esp/freertos/src/port_esp_hosted_host_os.c`, `.../port_esp_hosted_host_sdio.c` (L2 cache-line alignment of PSRAM DMA buffers, libc realloc) |
| `espressif__esp_lcd_st7701` (Espressif) | 1.1.3 | no (Guition patched the init table) |
| `espressif__usb_host_uac` (Espressif) | 1.5.0 | yes: `uac_host.c`, `include/usb/uac_host.h` (descriptor walk fix, `uac_host_get_device_alt_ep_info`) |
| `espressif__esp_lvgl_port` (Espressif) | 2.9.0 | yes: `src/common/ppa/lcd_ppa.c`, `src/lvgl9/esp_lvgl_port_disp.c` (bounded PPA wait, CPU-rotation fallback); `LOCAL PATCH` markers in the files; Apache-2.0 `license.txt` kept |
| `espressif__esp_codec_dev` (Espressif) | 1.5.11 | yes: `esp_codec_dev.c` (`esp_codec_dev_open()` fails when `set_fmt()` fails); Apache-2.0 `LICENSE` kept; the `device/zl38063/` subtree is unmodified and carries its own `LICENSE.txt` |

## Patch provenance (`patches/`)

Since 2026-10-03 the patched `esp_lvgl_port` and `esp_codec_dev` live in `components/` (table
above). `patches/` keeps the unified diffs against the pristine registry archives as
provenance; see `patches/README.md`.

## Fonts (`tools/fonts/`, `main/fonts/`)

Dangrek (OFL 1.1), Montserrat Medium (OFL 1.1), Font Awesome 6 Free Solid (icons CC BY 4.0,
font OFL 1.1); Anton and Archivo Black (OFL 1.1) are present but unused. Licence texts sit next
to the `.ttf` files.

## Models (`main/models/`)

`hey_mr_clock` and `hi_esp` were trained by the repository author and are MIT. The training
recipe in `tools/wakeword_train/` follows OHF-Voice/micro-wake-word (Apache-2.0) and drives
Piper sample generator and Chatterbox (MIT); none of those projects or any training data is in
this tree.

## Derived code

`components/espressif__esp32_p4_function_ev_board/priv_include/jd9365_init_jc8012p4a1_v2.h` is a register
table copied from ESPHome's `mipi_dsi/models/guition.py` (MIT, Copyright (c) 2019 ESPHome; the full
notice is in the file header).

`main/app_video.{c,h}` is ported from Espressif's CC0 `video_lcd_display` example.
`main/wake_mww.cpp` is an independent implementation of the microWakeWord streaming recipe; no
ESPHome (GPLv3) source was copied.

Build-time dependencies fetched by `idf.py` and **not redistributed here**: `espressif/esp_lcd_jd9365`
(Apache-2.0) and `mangoo1/esp_lcd_touch_gsl3680` (**GPL-2.0-or-later**, used by 10.1" builds only;
10.1" binaries must therefore not be distributed, see `../../docs/licensing.md`). Also fetched
(LVGL, esp-sr, esp-tflite-micro,
esp-micro-speech-features, esp_wifi_remote, esp_codec_dev, libhelix-mp3 and the rest) are not
redistributed here; their licences are listed in section 3 of THIRD_PARTY_NOTICES.md.

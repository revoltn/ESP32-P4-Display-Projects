# Third-party notices

This is the single authoritative list of third-party material that this repository
redistributes, modifies, depends on, or merely keeps on disk for reference. The repository's
own code and documentation are MIT (see [LICENSE](LICENSE)); the scope paragraph at the end of
that file says what the MIT grant covers. Everything below keeps its own licence.

Facts in this file were checked on 2026-10-02 against the files in the tree: component
manifests (`idf_component.yml`), the registry's `CHECKSUMS.json` where present (used to decide
"modified by this repo"), licence files, and the two vendor packages. Where a fact could not be
verified locally it says so. The narrative (why things are where they are, the decision rules,
the compliance verdicts) is in [docs/licensing.md](docs/licensing.md).

Project names used here: `src/bedside_clock` (the alarm clock), `src/sample_hello_world` (the
bring-up sample), `src/sample_wakeword_bench` (the microWakeWord benchmark). Older documents
and code comments call them `bedside_v01`, `hello_world` and `mww_test`.

## 1. Vendored components (`src/bedside_clock/components`, `src/sample_hello_world/components`)

Both projects carry the same seven directories. They are local overrides: the component manager
resolves them from `components/` instead of the registry (`dependencies.lock` records
`type: local`). Four of them originate in Guition's board package
(`1-Demo/idf_examples/ESP-IDF_5.5.4/common_components/` of `JC4880P443C_I_W.zip`), where
Guition had already patched Espressif's code for this panel; three were copied from the
registry and patched here. "Modified by this repo" was decided by hashing every file against
the component's `CHECKSUMS.json` (where the vendor stripped that file, by diffing against the
vendor package copy).

| Component | Version | Licence (SPDX) | Copyright holder | Upstream (from `idf_component.yml`) | Modified by this repo? |
|---|---|---|---|---|---|
| `bsp_extra` | 0.0.2 | Apache-2.0 (`LICENSE`) | Espressif Systems (Shanghai) CO LTD, 2015-2024 | Not a registry component. Copied from the Guition package (`common_components/bsp_extra`, itself from Espressif's `esp_brookesia_phone` example). | **Yes, both projects.** `src/bsp_board_extra.c`: stereo-to-mono downmix for the mono ES8311 (both projects). `bedside_clock` additionally changes `include/bsp_board_extra.h` and `src/bsp_board_extra.c`: `bsp_extra_sink_t` alternative-output table and `bsp_extra_sink_register()` (USB speaker takeover with unconditional fallback to the codec), `bsp_extra_i2s_write_codec()`, `bsp_extra_codec_get_fs()`, skip of the close/reopen when `set_fs` is called with an unchanged format, `dev_lock()` wrapper, headroom logging. |
| `chmorgan__esp-audio-player` | 1.0.7 | Apache-2.0 (`LICENSE`) | Copyright 2021 Espressif Systems; Copyright 2022 Chris Morgan | https://github.com/chmorgan/esp-audio-player (registry `chmorgan/esp-audio-player`; the manifest carries no commit) | **Yes, both projects** (3 of 17 files differ from `CHECKSUMS.json`). `audio_player.cpp`: skip an ID3v2 tag by its syncsafe size before decoding; `audio_mp3.h` / `audio_mp3.cpp`: `consec_errors` counter, decode gives up after 500 consecutive bad frames instead of spinning. `bedside_clock` additionally in `audio_player.cpp`: 10 ms pacing after a failed write (marked `LOCAL PATCH`) and `AUDIO_PLAYER_STATE_PAUSE` handled in the idle transitions. |
| `esp_cam_sensor` | 2.1.0 | Apache-2.0 (`LICENSE`) | Espressif Systems | https://github.com/espressif/esp-video-components/tree/master/esp_cam_sensor, commit `70d2ce41c5ae6a5a9f4603ddd1ab93f8c8dcaa71` | **No.** Guition-patched relative to the registry release: adds the whole `sensors/ov02c10/` driver (11 files that are not in the registry 2.1.0 `CHECKSUMS.json`) and the hooks for it in `CMakeLists.txt`, `Kconfig`, `project_include.cmake`. Byte-identical to the vendor package copy. |
| `espressif__esp32_p4_function_ev_board` | 5.2.3 | Apache-2.0 (`LICENSE`) | Espressif Systems (Shanghai) CO LTD, 2024-2026 | https://github.com/espressif/esp-bsp/tree/master/bsp/esp32_p4_function_ev_board, commit `4b3f542031dfe2a0b955517f804a99734f2cf8bb` | **No.** Guition-patched relative to esp-bsp: `esp32_p4_function_ev_board.c` drives the 480x800 ST7701 (`ST7701_480_360_PANEL_60HZ_DPI_CONFIG`, `esp_lcd_new_panel_st7701`) under the `CONFIG_BSP_LCD_TYPE_1024_600` branch and references `esp_lcd_st7701` by relative path. No `CHECKSUMS.json` in the vendor copy. Byte-identical to the vendor package copy. |
| `espressif__esp_hosted` | 2.12.12 | Apache-2.0 (`LICENSE`); bundles protobuf-c, BSD-2-Clause (`common/protobuf-c/LICENSE`) | Espressif Systems (Shanghai) CO LTD, 2015-2026; protobuf-c: Dave Benson and the protobuf-c authors | https://github.com/espressif/esp-hosted-mcu, commit `098525357e19c81099f2c3769938bd877190a8f5` | **Yes, both projects, identical** (2 of 664 files differ from `CHECKSUMS.json`). `host/port/esp/freertos/src/port_esp_hosted_host_os.c`: `hosted_realloc()` uses libc `realloc` (the upstream copy over-read the old block), `hosted_malloc_align()` raises PSRAM DMA alignment to `CONFIG_CACHE_L2_CACHE_LINE_SIZE` (128 B on the P4). `host/port/esp/freertos/src/port_esp_hosted_host_sdio.c`: TX buffers that are not L2-cache-line aligned are bounced through an internal DMA-safe buffer; the sdmmc aligned buffer is freed on deinit. The directory also ships upstream's `slave/`, `examples/` and docs unchanged. |
| `espressif__esp_lcd_st7701` | 1.1.3 | Apache-2.0 (`license.txt`) | Espressif Systems, 2023-2024 | https://github.com/espressif/esp-iot-solution/tree/master/components/display/lcd/esp_lcd_st7701, commit `61935c3499f63781d1392c3bfa8be4a46eaf0bd1` | **No.** Guition-patched init command table for the JC4880P443C panel. No `CHECKSUMS.json` in the vendor copy. Byte-identical to the vendor package copy. |
| `espressif__usb_host_uac` | 1.5.0 | Apache-2.0 (`LICENSE`) | Espressif Systems | https://github.com/espressif/esp-usb/tree/master/host/class/uac/usb_host_uac, commit `6d24137e14a4f6c8138662a7a074be16a899c2c6` | **Yes, both projects, identical** (2 of 42 files differ from `CHECKSUMS.json`). `uac_host.c`: the alternate-setting descriptor walk no longer stops at the class-specific endpoint descriptor (devices such as Jabra speakerphones place the standard endpoint descriptor after it, which left `ep_mps = 0` and made every stream start fail), only the first data endpoint is kept, diagnostics raised to INFO; `include/usb/uac_host.h`: new `uac_host_get_device_alt_ep_info()`. Marked `local override` in the source. |

Apache-2.0 section 4(b) requires every modified file to carry a prominent notice that it was
changed. The modified files listed above carry one (added 2026-10-02 where it was missing).
Guition's modifications to the four vendor-origin components carry no such notice from Guition;
this table is the repository's record of them.

## 2. Patched registry components (`src/bedside_clock/patches/`)

These two components started as registry packages and carry local patches. Since 2026-10-03 the
patched copies are committed under `src/bedside_clock/components/` with their Apache-2.0 licence
files and `LOCAL PATCH` markers (Apache-2.0 section 4(b)); the unified diffs against the pristine
registry archives stay in `src/bedside_clock/patches/` as provenance, with notes in
`src/bedside_clock/patches/README.md`. Only `bedside_clock` uses the patched copies.

| Component | Version | Licence | Upstream | What the patch changes |
|---|---|---|---|---|
| `espressif/esp_lvgl_port` | 2.9.0 | Apache-2.0 (`license.txt`) | https://github.com/espressif/esp-bsp/tree/master/components/esp_lvgl_port, commit `f0ef9497efce684997ce391edd19733483e250a5` | `src/common/ppa/lcd_ppa.c`: the PPA rotate is issued non-blocking and waited for with a 500 ms timeout (`PPA_LCD_TIMEOUT_MS`) instead of `portMAX_DELAY`; a timeout returns `ESP_ERR_TIMEOUT`. `src/lvgl9/esp_lvgl_port_disp.c`: `ppa_failed` flag, the CPU rotation buffer is allocated even when the PPA is available, and after a failed PPA rotate the display falls back to CPU rotation for the rest of the boot (upstream flushed the unrotated buffer). Reason: the PPA on ESP32-P4 rev v1.3 can hang forever (JTAG-proven 2026-09-06), freezing the LVGL task. |
| `espressif/esp_codec_dev` | 1.5.11 | Apache-2.0 (`LICENSE`) | https://github.com/espressif/esp-adf/tree/master/components/esp_codec_dev, commit `73befa9ebffdd6e5065b7145329f115910e13ab5` | `esp_codec_dev.c`: `esp_codec_dev_open()` checks the return of `data_if->set_fmt()` and fails with `ESP_CODEC_DEV_DRV_ERR` instead of ignoring it (an I2S slot reconfigure failure used to leave the device half-open). |

## 3. Registry dependencies pulled by `idf.py` (not redistributed here)

Everything in this section is downloaded by the ESP-IDF component manager at build time from
https://components.espressif.com/ and lands in the gitignored `managed_components/`. Licences
are as declared by the component manager and as read from the licence file inside each
component at lock time; see each project's `dependencies.lock` for the exact versions and
hashes. ESP-IDF itself (5.5.5, Apache-2.0) is not in the repository either.

Direct dependencies (`main/idf_component.yml` of each project):

| Component | Version (locked) | Licence | Used by | Notes |
|---|---|---|---|---|
| `lvgl/lvgl` | 9.5.0 | MIT (LVGL Kft) | clock, hello | Also provides the built-in `lv_font_montserrat_*` glyph tables (Montserrat is OFL 1.1, see section 4). |
| `espressif/esp_lvgl_port` | 2.9.0 | Apache-2.0 | clock, hello | Patched in `bedside_clock`, section 2. |
| `espressif/esp_wifi_remote` | 1.6.4 | Apache-2.0 | clock, hello | |
| `espressif/esp_hosted` | 2.12.12 | Apache-2.0 | clock, hello | Requirement `~2.12.0`, satisfied by the vendored copy in section 1. |
| `espressif/usb_host_uac` | 1.5.0 | Apache-2.0 | clock, hello | Requirement `^1`, satisfied by the vendored copy in section 1. |
| `espressif/esp-sr` | 2.5.2 | Espressif MIT ("for use on all ESPRESSIF SYSTEMS products") | clock | Ships closed binary libraries (`lib/`) and the WakeNet9 / MultiNet7 / VAD model files (`model/`) under the same licence file; the models are flashed to the `model` partition, never committed. The licence's field-of-use clause is satisfied on the ESP32-P4. |
| `espressif/esp-tflite-micro` | 1.4.1 | Apache-2.0 | clock, bench | `third_party/`: flatbuffers (Apache-2.0), gemmlowp (Apache-2.0), ruy (Apache-2.0), kissfft (BSD-3-Clause). Linked into `bedside_clock` only with `CONFIG_BEDSIDE_WAKE_MWW`. |
| `esphome/esp-micro-speech-features` | 1.2.3 | Apache-2.0 | clock, bench | TensorFlow microfrontend fork (the feature generator every microWakeWord model is trained against). |
| `espressif/esp_lcd_jd9365` | 1.0.4 | Apache-2.0 | clock (10.1" only) | JD9365 MIPI-DSI panel driver, requested by the BSP fork's manifest. |
| `mangoo1/esp_lcd_touch_gsl3680` | 1.0.0 | **GPL-2.0-or-later** | clock (10.1" only) | Silead GSL3680 touch driver, requested by the BSP fork's manifest and fetched at build time. 10.1" binaries cannot be distributed (`docs/licensing.md` section 4). |

Transitive dependencies (resolved by the component manager):

| Component | Version (locked) | Licence | Used by | Notes |
|---|---|---|---|---|
| `chmorgan/esp-file-iterator` | 1.0.0 | No licence file in the component package; source header "Copyright 2022 Chris Morgan". Upstream licence not verified offline. | clock, hello | via `bsp_extra` |
| `chmorgan/esp-libhelix-mp3` | 1.0.3 | Component wrapper: Apache-2.0 (`LICENSE`). **Bundled decoder `libhelix-mp3/`: RealNetworks Public Source License 1.0 / RealNetworks Community Source License 1.0** (`libhelix-mp3/LICENSE.txt`, `RPSL.txt`, `RCSL.txt`; Copyright 1995-2004 RealNetworks, Inc.) | clock, hello | via `esp-audio-player`. Linked, unmodified, into every firmware image of both projects. Not redistributed in this repository; see docs/licensing.md for what the RPSL asks of anyone who distributes binaries. |
| `espressif/esp-nn` | 1.4.1 (bench: 1.4.0) | Apache-2.0 | clock, bench | PIE kernels for tflite-micro |
| `espressif/esp-dl` | 3.3.12 | MIT (Espressif, 2021) | clock | via esp-sr |
| `espressif/esp-dsp` | 1.8.0 | Apache-2.0 | clock | via esp-sr |
| `espressif/dl_fft` | 0.7.0 | No licence file in the component package; source headers Apache-2.0 (Espressif) | clock | via esp-sr / esp-dl |
| `espressif/cjson` | 1.7.19~2 | MIT (Dave Gamble and cJSON contributors) | clock | |
| `espressif/esp_codec_dev` | 1.5.11 | Apache-2.0 | clock, hello | Patched in `bedside_clock`, section 2. |
| `espressif/esp_video` | 2.0.1 | Espressif MIT | clock, hello | V4L2 camera stack; `esp_cam_sensor` 2.0.x it asks for is overridden by the vendored 2.1.0 |
| `espressif/esp_ipa` | 1.3.1 | Espressif MIT | clock, hello | ISP pipeline (AE/AWB) |
| `espressif/esp_new_jpeg` | 1.0.2 | Espressif MIT | clock | via esp-dl |
| `espressif/esp_h264` | 1.0.4 | Apache-2.0 | clock, hello | via esp_video |
| `espressif/esp_sccb_intf` | 0.0.9 | Apache-2.0 | clock, hello | |
| `espressif/esp_lcd_touch` | 1.2.1 | Apache-2.0 | clock, hello | |
| `espressif/esp_lcd_touch_gt911` | 1.2.1 | Apache-2.0 | clock, hello | the 4.3" touch controller |
| `espressif/esp_lcd_ek79007` | 2.0.2~1 | Apache-2.0 | clock, hello | BSP dependency, unused panel |
| `espressif/esp_lcd_ili9881c` | 1.1.0 | Apache-2.0 | clock, hello | BSP dependency, unused panel |
| `espressif/esp_lcd_lt8912b` | 0.2.1 (hello: 0.2.0) | Apache-2.0 | clock, hello | BSP dependency, unused |
| `espressif/usb_host_uvc` | 2.4.2 | Apache-2.0 | clock, hello | via esp_video |
| `espressif/cmake_utilities` | 0.5.3 | Apache-2.0 | all | |
| `espressif/eppp_link` | 1.1.6 | Apache-2.0 | clock, hello | via esp_wifi_remote |
| `espressif/esp_serial_slave_link` | 1.1.2 | Apache-2.0 | clock, hello | via eppp_link |
| `espressif/wifi_remote_over_eppp` | 0.3.3 | No licence file in the component package (Espressif) | clock, hello | via esp_wifi_remote |

"Espressif MIT" is the MIT text with the permission granted "for use on all ESPRESSIF SYSTEMS
products". It is not plain MIT; it is listed separately in docs/licensing.md because a
field-of-use clause matters for GPL compatibility (section 7, GSL3680).

## 4. Fonts

Font files live in `src/bedside_clock/tools/fonts/` (sources) and `src/bedside_clock/main/fonts/`
(LVGL glyph tables generated from them by `tools/fonts/gen_fonts.sh` with `lv_font_conv`).
Under the SIL Open Font License the generated `.c` tables are Modified Versions of the font
software, so the OFL text travels with them. Every generated file records its source font,
ranges and size in its `Opts:` header.

| Font | Licence | Copyright | Source file(s) | Generated table(s) | Status |
|---|---|---|---|---|---|
| Dangrek | SIL OFL 1.1 (`tools/fonts/OFL-Dangrek.txt`; a duplicate copy with `OFL.txt` in `src/bedside_clock/fonts/`) | Copyright 2020 The Dangrek Project Authors (https://github.com/danhhong/Dangrek) | `Dangrek-Regular.ttf` (Google Fonts) | `main/fonts/dangrek_226.c`, `dangrek_478.c` (digits, `-`, `:` only) | used: Giant clock face |
| Montserrat Medium | SIL OFL 1.1 (`tools/fonts/OFL-Montserrat.txt`) | Copyright 2011 The Montserrat Project Authors (https://github.com/JulietaUla/Montserrat) | `Montserrat-Medium.ttf`, a copy of `lvgl/scripts/built_in_font/Montserrat-Medium.ttf` | `main/fonts/montserrat_ext_14.c`, `_16.c`, `_20.c`, `_28.c` (Latin-1 Supplement, Latin Extended-A, Cyrillic) | used: fallback fonts for server-supplied text (`main/fonts/app_fonts.c`). LVGL's built-in `lv_font_montserrat_*` tables (enabled via `CONFIG_LV_FONT_MONTSERRAT_*`) come from the same face. |
| Font Awesome 6 Free, Solid | Font Awesome Free License (`tools/fonts/LICENSE-FontAwesome.txt`): icons CC BY 4.0, font files SIL OFL 1.1, code MIT | Fonticons, Inc. | `fa-solid-900.ttf` | `main/fonts/app_icons_28.c` (U+F130 microphone, U+F2DB microchip) | used: service indicators. Attribution also in `main/fonts/app_icons.h`. |
| Anton | SIL OFL 1.1 (`tools/fonts/OFL-Anton.txt`) | Copyright 2020 The Anton Project Authors (https://github.com/googlefonts/AntonFont.git) | `Anton-Regular.ttf` | none | unused (kept with its licence) |
| Archivo Black | SIL OFL 1.1 (`tools/fonts/OFL-ArchivoBlack.txt`) | Copyright 2017 The Archivo Black Project Authors (https://github.com/Omnibus-Type/ArchivoBlack) | `ArchivoBlack-Regular.ttf` | none | unused (kept with its licence) |

## 5. Wake-word models and the training recipe

| File(s) | Author / origin | Licence | Notes |
|---|---|---|---|
| `src/bedside_clock/main/models/hey_mr_clock.tflite` + `.json` | Trained by the repository author (model `mrclock_v1d`, 2026-09-20) with the microWakeWord recipe in `tools/wakeword_train/` | MIT, with the repository | Positives: the author's own device recordings, Chatterbox clones of the author's voice, Piper TTS; negatives: the author's room/TV recordings plus public negative sets. The training data is not in the repository. The model encodes the author's voice; this is a privacy choice, not a licence matter. |
| `src/bedside_clock/main/models/hi_esp.tflite` + `.json` | Trained by the repository author (`hi_esp_v0`, `tools/wakeword_train/run_v0.sh`, Piper-only positives) | MIT, with the repository | The phrase "Hi ESP" is also the phrase of Espressif's WakeNet9 `wn9_hiesp` model; no Espressif code, data or weights were used. |
| `src/sample_wakeword_bench/main/assets/okay_nabu.tflite` + `.json` | Kevin Ahrendt (https://www.kevinahrendt.com/), stock model from https://github.com/esphome/micro-wake-word-models | Apache-2.0 (the repository's licence; the model files themselves carry no licence field). Not verified offline beyond the manifest's `author` field. | Used unmodified as the benchmark reference. Attribution recorded here and in `src/sample_wakeword_bench/NOTICE.md`; the licence text is at https://www.apache.org/licenses/LICENSE-2.0. |
| `src/sample_wakeword_bench/main/assets/okay_nabu_tts.wav`, `okay_nabu_tts2.wav`, `negative_tts.wav` | Generated by the repository author with Windows text-to-speech | MIT, with the repository | |
| `src/bedside_clock/tools/wakeword_train/*` (scripts, YAML, `wsl_scripts/`) | Repository author | MIT, with the repository | `simple_clips.py`, `make_features.py` and `training_parameters_*.yaml` follow the reference notebook of https://github.com/OHF-Voice/micro-wake-word (Apache-2.0; `simple_clips.py` is a drop-in replacement for its `microwakeword.audio.clips.Clips`). The scripts invoke, but do not include: microWakeWord (Apache-2.0), https://github.com/rhasspy/piper-sample-generator with its `en_US-libritts_r-medium` voice (MIT, as stated by that project), Chatterbox TTS (MIT, as stated by that project), pymicro-features, ai-edge-litert, PyTorch. Licences of the external projects were taken from the training README and were not re-verified offline. |
| Training datasets referenced by path only | Hugging Face `kahrendt/microwakeword` negative feature sets, MIT RIR survey, Free Music Archive, AudioSet | per dataset | None of this data is in the repository. Dataset terms attach to the data; the trained weights above are released by their author under MIT. |

## 6. Code derived from examples

| File(s) | Derived from | Licence of the origin | What was kept / changed |
|---|---|---|---|
| `src/bedside_clock/main/app_video.{c,h}`, `src/sample_hello_world/main/app_video.{c,h}` | Espressif esp-video example `video_lcd_display` (`main/app_video.c`, SPDX `CC0-1.0`, as shipped in the Guition package) | CC0-1.0 | V4L2 open/format/buffer/stream code kept; changes listed in each file header (I2C bus handle reuse, event-group stop handshake taken from the esp-brookesia camera app pattern, error handling). |
| `src/bedside_clock/main/wake_mww.cpp`, `src/sample_wakeword_bench/main/main.cpp` | The microWakeWord streaming recipe as published by the microWakeWord project: TF microfrontend settings, feature quantisation `(v*256+333)/666-128`, `[1, stride, 40]` int8 input, sliding-window mean over the last `sliding_window_size` probabilities, 100-slice cool-off after reset | Apache-2.0 (microWakeWord). ESPHome's `micro_wake_word` component (GPL-3.0) implements the same recipe and was used for behavioural comparison; **no ESPHome source was copied**. | Independent implementation on `esp-tflite-micro`, validated clip-for-clip against the reference in `src/sample_wakeword_bench`. The ESPHome v2 model manifest format (`wake_word`, `micro.probability_cutoff`, ...) is a data format, parsed with cJSON. |
| `src/bedside_clock/components/espressif__esp32_p4_function_ev_board/priv_include/jd9365_init_jc8012p4a1_v2.h` | ESPHome `esphome/components/mipi_dsi/models/guition.py`, model `JC8012P4A1-V2` (https://github.com/esphome/esphome) | MIT for ESPHome's Python sources (Copyright (c) 2019 ESPHome) | The 200-entry JD9365 register sequence, reformatted as a C table; the full MIT notice is in the file header. Sleep-out and display-on entries added here. |
| `src/tool_c6_ota/main/main.c` | Espressif `esp_hosted` example `examples/host_performs_slave_ota` (`main/main.c`, Apache-2.0, in `src/bedside_clock/components/espressif__esp_hosted/`) | Apache-2.0 | OTA call sequence (`begin/write/end/activate`, version check, restart) kept; the embedded-image source, version comparison and logging are new. The C6 slave image it flashes (`network_adapter_esp32c6.bin` from https://github.com/esphome/esp-hosted-firmware, Apache-2.0) is **not committed** (gitignored under `devices/*/firmware/`). |
| `src/bedside_clock/main/{usb_audio.c, wifi_mgr.c, app_common.h, feature_specs.c}` | The author's own `src/sample_hello_world` | MIT (this repository) | Ports within the repository. |
| `src/*/main/app_main.c` crash-loop breaker, `boot_seq.c` ordering, watchdog and memory rules | Patterns distilled in `docs/reliability-checklist.md` from EspControl, ESPHome and the vendor demos | Ideas only | No code was copied from EspControl (PolyForm NC) or ESPHome's C++ (GPLv3); a grep for their distinctive identifiers in `src/` finds nothing. |

## 7. Reference material kept locally but NOT redistributed

These trees exist on the maintainer's disk under `devices/*/vendor/package/`,
`devices/*/community/`, `reference/*/upstream/` and the gitignored binary folders. Only the
repository's own notes about them are published. The `.gitignore` rules that keep them out are
listed in docs/licensing.md's publishing checklist.

| Item | Local path | Origin | Licence | Why it is not hosted |
|---|---|---|---|---|
| Guition JC4880P443C_I_W board package (4.3") | `devices/guition-jc4880p443c/vendor/JC4880P443C_I_W.zip` and `vendor/package/` | http://www.jczn1688.com/zlxz (sharded download from `pan.jczn1688.com`) | No licence from Guition. Inside: Espressif Apache-2.0 BSP/demos (the four components in section 1), `xiaozhi-esp32` (MIT, Shenzhen Xinzhi Future Technology), `esp-brookesia` (Apache-2.0), Espressif `human_face_detect` / `pedestrian_detect` (MIT), ESP-IDF examples (Unlicense OR CC0-1.0), five factory `.bin` images (no licence), Espressif `flash_download_tool_3.9.9` (freeware, no redistribution grant), Espressif ESP32-P4 and OmniVision OV02C10 datasheets, Guition schematics and `.dwg` CAD, two sample `.avi` files of unknown copyright, and in `7-Character&Picture_Molding_Tool/` **WinHex 15.1 SR-8 (commercial, X-Ways Software)**, a CH340 driver, sscom32, NetAssist and a JPG compressor from a download portal. | No redistribution grant; several items cannot be redistributed by anyone (WinHex, OmniVision datasheet, factory images); 295 MB. Facts extracted from it are in `devices/guition-jc4880p443c/docs/`. Factory image SHA256 (for the unbrick procedure): `C6-JC4880P443-2.1.10.bin` 7b68c4cff6e3cf288896a89b4a09379396f62c6bfbfbdc3984b9e142cfb4db0c; `JC-C6-slave_v2.3.2.bin` f9ec1205c5fa1e497032dedcee33d2ab9560bd2768fc1ea06acb6d8e2773f80c; `JC4880P443C_I_W_xiaozhi2.0.4.bin` b12e316fe8edebba91322f8664b50515caa84baaa0c04bd9901574ecad3db0d8; `P4-JC4880P443C_I_W-V2.1.bin` 9e61c84bd9c3154143695d66d94b354d958b803188e018d480b133bbaf6e34b9; `P4-JC4880P443C_I_W_V2.2.bin` f7afdfd68ea0d57b391b2eba40531d419c4a5e969a3ecdcb4262b7deae9bca3d. |
| Guition JC8012P4A1C_I_W_Y board package (10.1") | `devices/guition-jc8012p4a1c/vendor/package/` (plus `manifest.json`, `fetch_vendor_zip.sh`, `INVENTORY.md`) | Official: `https://pan.jczn1688.com/directlink/1/HMI%20display/JC8012P4A1C_I_W_Y.zip` (sharded; 421 MB). The local copy came from the DevinWatson mirror below. | No licence from Guition. Same classes of content as the 4.3" package: Espressif BSP 4.1.1 / `esp_lcd_jd9365` / `esp_video` (Apache-2.0 and Espressif MIT), LVGL 9.2.2 copy (MIT), `flash_download_tool_3.9.7` with 191 MB of other products' firmware in its `dl_temp/` cache and vendor flashing logs, four factory `.bin` images, Espressif and OmniVision datasheets, schematics, WinHex and the same Windows tools, demo MP3s with commercial song titles, and the GSL3680 touch driver (next rows). | As above. Factory image SHA256: `JC-C6-slave_v2.3.2.bin` f9ec1205c5fa1e497032dedcee33d2ab9560bd2768fc1ea06acb6d8e2773f80c; `JC8012P4A1_C6.bin` 8b16a042713bca67d64a1d7af6a296432c1c5f8116fe015ae24acfca3a66fec0; `JC8012P4A1_P4-V2.8.bin` ef972908b06889aa1cf43194c613fa848804f17a444b847b954615f05a59c4b8; `JC8012P4A1C_I_W_Y_xiaozhi2.0.4.bin` 7bc321ff9c27aaa8b678e968ec12e7ec681242352caa7431fdb1d9651b25ae2e. |
| Silead `gsl_point_id.c` (GSL3680 point-ID algorithm) | six copies inside the 10.1" package demos | Silead Inc., from the MediaTek Android kernel tree (`drivers/input/touchscreen/mediatek/gslX680/`) | **GPL-2.0-or-later** (verbatim notice in the file, "2010 - 2016 silead inc.") | GPL code is not vendored into this MIT tree. See docs/licensing.md, "The 10.1 inch touch driver". |
| Silead GSLX680 controller firmware (`GSLX680_FW[]`, 4587 words) and `gsl_config_data_id[]` | `esp_lcd_touch_gsl3680.h` / `.c` in the same demos | Silead / Guition | **No licence statement at all** | Uploaded to the controller over I2C on every boot; nobody in the chain grants redistribution. |
| `mangoo1/esp_lcd_touch_gsl3680` | fetched at build time into the gitignored `managed_components/`, not committed | ESP component registry (https://components.espressif.com/components/mangoo1/esp_lcd_touch_gsl3680), version 1.0.0 | **GPL-2.0-or-later** (as listed by the registry; recorded from the registry page, not re-verified offline) | Declared in the BSP fork's `idf_component.yml` since 2026-10-03 (called only by the 10.1" `bedside_clock` build), never vendored. Consequence for binaries: docs/licensing.md. |
| DevinWatson/10.1-inch-ESP32P4-Xiaozhi-ESP32-C6-JC8012P4A1C_I_W_Y | content of `devices/guition-jc8012p4a1c/vendor/package/` | https://github.com/DevinWatson/10.1-inch-ESP32P4-Xiaozhi-ESP32-C6-JC8012P4A1C_I_W_Y (commit `84e7c969`) | No licence file; a re-upload of the vendor zip | Mirror of unlicensed vendor material. |
| ESPHome | `reference/esphome/upstream/`, `reference/esphome/esphome-dev.zip` | https://github.com/esphome/esphome (dev branch) | ESPHome License: MIT for Python and everything else, **GPLv3 for the C++ runtime** (`.c`, `.cpp`, `.h`, `.hpp`, `.tcc`, `.ino`) | Redistribution would be allowed with the licence, but 31 MB of someone else's tree adds nothing; the findings are in `devices/guition-jc4880p443c/docs/08-esphome-findings.md`. Keeping it out also keeps GPL code out of the published tree. |
| EspControl | `reference/espcontrol/upstream/`, `reference/espcontrol/espcontrol-main.zip` | https://github.com/jtenniswood/espcontrol | **PolyForm Noncommercial 1.0.0**; `NOTICE`: "Required Notice: Copyright (c) 2026 jtenniswood" | Redistribution is permitted only for noncommercial purposes and only with the licence and the Required Notice; hosting it would attach those terms to this repository. Patterns learned from it are in `docs/reliability-checklist.md` and `devices/guition-jc4880p443c/docs/07-espcontrol-findings.md` (ideas, no code). |
| seobaeksol/JC4880P443C_I_W | `devices/guition-jc4880p443c/community/seobaeksol-JC4880P443C_I_W/` | https://github.com/seobaeksol/JC4880P443C_I_W (commit `d469820`) | **No licence** (all rights reserved by default) | Korean bring-up notes `01..90`; linked from the hardware notes as an independent pin-map cross-check. |
| bigbag/JC4880P443C-examples | `devices/guition-jc4880p443c/community/bigbag-JC4880P443C-examples/` | https://github.com/bigbag/JC4880P443C-examples | **No licence** | PlatformIO examples (sleep/wake, battery ADC, RS485 were the useful ones). Link only. |
| p1ngb4ck/unofficial_guition_esp32p4_repo | `devices/guition-jc8012p4a1c/community/p1ngb4ck-unofficial_guition_esp32p4_repo/` | https://github.com/p1ngb4ck/unofficial_guition_esp32p4_repo (commit `89b928ee`) | No licence file. Its README states the Guition files are posted "with kind allowance" from Guition; that permission is theirs, not ours. | Schematics, spec PDF and an ESPHome YAML for the 10.1" board. Link only. |
| sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6 | not on disk | https://github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6 | No licence (as recorded by the maintainer; not inspected for this file) | Link only. |
| BETTA-HA-PANEL | not on disk | (project page as recorded by the maintainer) | Custom licence; SPDX `NOASSERTION` (not inspected for this file) | Link only. |
| Espressif Flash Download Tool 3.9.7 / 3.9.9 | inside both vendor packages | https://www.espressif.com/en/support/download/other-tools | Espressif freeware, closed source, no redistribution grant in the package | Link to Espressif. |
| Espressif ESP32-P4 datasheet (CN v0.4.1), OmniVision OV02C10 datasheet | inside both vendor packages | Espressif; OmniVision | All rights reserved; the OmniVision document is a distributor/NDA-class datasheet | Link to Espressif; the OmniVision file is not linked anywhere. |
| Guition schematic sheets, structure drawings, CAD | `devices/*/docs/schematics/*.webp|png|pdf` (gitignored), vendor packages | Guition | No licence | Pin tables derived from them (facts) are in the device docs; the images themselves are not published. |

## 8. How to add an entry

1. Decide the category: vendored (copied into `src/*/components` or `main/`), patched registry
   component (`patches/`), build-time dependency (only `idf_component.yml` changes), font, model,
   derived code, or local-only reference. Local-only material goes under `devices/*/vendor/`,
   `devices/*/community/` or `reference/*/upstream/` so the existing `.gitignore` rules cover it.
2. Record: name, exact version or commit, licence as SPDX identifier (write "Espressif MIT" or
   "No licence" when that is the truth), copyright holder from the licence or file headers,
   upstream URL, and whether this repository modified it (list the files and the change).
3. Keep the upstream licence file inside the copied directory. For Apache-2.0, add a one-line
   "modified by this repository" notice to every file you change (section 4(b)). For OFL fonts,
   copy the OFL text next to the font and keep it when you regenerate glyph tables. For CC BY,
   name the author and the licence where the asset is used.
4. Never copy GPL, PolyForm or unlicensed code into `src/`. A GPL component may be a build-time
   dependency, in which case write down in docs/licensing.md what that does to distributed
   binaries.
5. Add the per-project line to `src/<project>/NOTICE.md` if the project vendors or modifies it.

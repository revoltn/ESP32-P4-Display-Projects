# Board profiles: one `bedside_clock`, many boards

**Status:** design approved 2026-10-02; reviewed and re-sequenced 2026-10-03 (section 0
below): the 10.1" port is done first with the smallest possible change set, the `-D BOARD=`
plumbing and the generated profile follow. Target: build and run `src/bedside_clock` on both
Guition panels from one tree, with a seam that keeps an ESP32-S3 board possible later.
Estimated effort for the two P4 boards: 45 to 80 hours (breakdown in section 6); the
re-sequenced path reaches a working 10.1" in about 10 to 16 of those.

## 0. Review 2026-10-03 and the re-sequenced order

Three verification passes over the tree (build system, `main/` coupling, the 10.1" device
folder and vendor package) confirmed the design and corrected it in two places.

**One BSP fork, not two.** The 4.3" fork `components/espressif__esp32_p4_function_ev_board`
(5.2.3) already contains a `CONFIG_BSP_LCD_TYPE_1280_800` branch at 800x1280
(`include/bsp/display.h`, the ILI9881C `#else` in the `.c`, an `#else` pin block in the pin
header). The 10.1" is that branch with the ILI9881C swapped for `espressif/esp_lcd_jd9365`
(registry, Apache-2.0), the GSL3680 touch from `mangoo1/esp_lcd_touch_gsl3680` (registry,
GPL, see `docs/licensing.md` section 4), the 10.1" pins (LCD reset 27, touch RST 22 / INT 21,
PA 20, I2S DIN 11) and a 1.5 Gbps lane rate. The vendor 4.1.1 fork proposed in section 3.3
would have brought an exact `esp_codec_dev: 1.3.2` pin (clashing with the patched 1.5.11),
an older `esp_lvgl_port`, and the GPL driver as a path dependency. Section 3.3's
`boards/<id>/components/` layout is therefore not needed for the P4 boards; it stays the
answer for an ESP32-S3 board with a different BSP.

**Panel revision is a build option.** Guition ships V1 and V2 glass under one part number.
The BSP `Kconfig` gained `choice BSP_JD9365_PANEL_REV` (`_V1`: DPI 60 MHz, vsync 4/8/20,
driver default table; `_V2`: DPI 70 MHz, vsync 4/10/20, own table from ESPHome's MIT Python
model data). The board overlay selects it; `board.json` `display.panel_rev` records it.

**Lock behaviour.** Section 3.3's claim that dropping `bsp_extra`'s path dependency makes
the lock entry disappear is wrong: the BSP's own manifest keeps requesting its drivers. With
one fork the manifest is identical for both boards and one `dependencies.lock` is stable.
Should it ever diverge, ESP-IDF supports a per-build lock via
`idf_build_set_property(DEPENDENCIES_LOCK dependencies.lock.<board>)`. Phase 0 stayed a hard
prerequisite: the component manager validates `.component_hash` whenever the manifest set
changes, which the 10.1" branch does.

**Citation fixes.** `main/idf_component.yml` pinned esp_hosted at line 5, not 4; only
`esp_lvgl_port` was listed there, `esp_codec_dev` arrived via the BSP manifest;
`esp_lvgl_port` carries four `LOCAL PATCH` markers in two files, not three; there is no
`CONFIG_LV_DRAW_SW_*` line at `sdkconfig.defaults:93` (a comment); the hosted patch note
pointed at `managed_components`, the patch lives in `components/espressif__esp_hosted`;
`feature_specs.c` "no camera module fitted" was already wrong on the 4.3". The 10.1"
`board.json` and README disagreed on `mic_count` (2 vs 1), speaker connector and amp part.

**Re-sequenced order (what was executed from 2026-10-03):**

| Step | Content | Maps to |
|---|---|---|
| 0 | patched `esp_lvgl_port` 2.9.0 and `esp_codec_dev` 1.5.11 moved from `managed_components/` into `components/` (4.3" image 40 bytes smaller: shorter `__FILE__` strings) | phase 0 |
| 1 | 10.1" branch in the one BSP fork; `sdkconfig.jc8012p4a1c.defaults` overlay; `BSP_JD9365_PANEL_REV`; Specs page height `lv_pct(100)`; `feature_specs.c` board text conditional; flash + smoke test on the 10.1"; C6 slave OTA over SDIO with `src/tool_c6_ota` | phase 3 (bring-up), parts of phase 2 |
| 2 | `choice BEDSIDE_BOARD` in Kconfig, `-D BOARD=`/`-D VARIANT=` CMake sugar composing the defaults layers, `sdkconfig.defaults` split, `board_profile` generated from `board.json` with the BSP pin cross-check, `UI_PX()` and the 10.1" font set, settings v12 `board_id` | phases 1 and 2 |

Build commands today (step 1): `docs/howto/build-and-flash.md`. Bring-up results:
`devices/guition-jc8012p4a1c/docs/bring-up-notes.md`.

Related: the coupling inventory this plan is based on lives in the session notes;
every claim below about today's code carries a `file:line` reference into
`src/bedside_clock`.

## 1. Goals and non-goals

Goals

1. A board is selected at configure time with one switch (`-D BOARD=<id>`) and nothing
   else. Build directories, sdkconfig layers, vendored BSP components and fonts follow.
2. Resolution, orientation semantics, UI scale, fonts, pins, features and tuning defaults
   come from **one machine-readable file per board**, `devices/<board>/board.json`,
   which is also the human-facing board card data. Documentation and firmware cannot drift.
3. `main/` contains no board literals: no `BSP_LCD_H_RES`, no GPIO numbers, no
   "4.3 inch" strings, no "external speaker" assumptions.
4. The 4.3" build stays byte-for-byte functionally identical through phases 0 to 2
   (regression-free refactor before any 10.1" work).
5. A future ESP32-S3 board must only need: a new `boards/<id>/`, a HAL file for its BSP,
   and target-conditional dependencies. No `#if CONFIG_IDF_TARGET_*` sprinkled in `main/`.

Non-goals

- Supporting a board at runtime (one binary, many boards). Flash budget (8 MB app slot
  already holds esp-sr, fonts and models) and the vendor BSP forks make this a per-build
  choice.
- Rewriting the UI layout per board. The layout is flex and percent based; it scales.
- Replacing the vendor-patched BSP forks with hand-rolled drivers. They are what works.

## 2. What is board-specific today

Every pin, LDO channel, panel timing and touch driver lives in the vendored BSP fork and
in `sdkconfig.defaults`; `main/` has zero GPIO literals. The app-side coupling is small
but spread out:

| Area | Where | Today (4.3") | 10.1" |
|---|---|---|---|
| Panel geometry | BSP `include/bsp/display.h:45-58`, `app_main.c:342` (`BSP_LCD_H_RES * 160` draw buffer) | 480x800 portrait ST7701, rotated 90 to 800x480 | 800x1280 portrait JD9365, rotated to 1280x800 |
| DSI timing | BSP `esp32_p4_function_ev_board.c:460` (`lane_bit_rate_mbps = 750`), `esp_lcd_st7701.h:177` (DPI 28 MHz) | ST7701 table | JD9365: 1.5 Gbps, DPI 60 MHz, h 20/20/40, v 4/8/20 (vendor BSP v4.1.1) |
| Orientation map | `app_main.c:302-311`, `settings.h:189-191` | assumes portrait-native panel: "landscape" = rotate 90 | same assumption holds; an S3 landscape-native RGB panel inverts it |
| UI literals | `ui_settings.c` 45, `ui_play.c` 23, `ui_clock.c` 8 + 8 defines, `ui_common.c` 5, `alarm.c` 6, `voice.c` 4 | heights and pads in px, widths in `lv_pct` | x1.6 (w) / x1.667 (h) |
| True screen-size literals | `ui_settings.c:1509` (`lv_obj_set_height(cont, 480)`), `ui_clock.c:269-292` Large-face offsets | hard-coded | must become percent / scaled |
| Fonts | `ui_clock.c:49-52` (`dangrek_478`, `dangrek_226`), `app_fonts.c:23-26` (14/16/20/28 chain), `sdkconfig.defaults:87-92` | one font set linked | a second set (about 780 px hours, 360 px minutes) must be linked **instead of** the first |
| Touch | BSP `.c:563-588` GT911 only, polled (INT = NC) | GT911 at 0x5D | GSL3680 at 0x40, firmware upload at init, INT on GPIO21 |
| Audio | `bsp_extra` mono downmix, `voice.c:66-67` gains, `voice.c:1251-1275` AFE `"M"`, AEC off, `DET_MODE_95`, loopback self-test thresholds, `app_common.h:25-27` | quiet single analog mic, external speaker | built-in speaker, "2 mics" on one ES8311 ADC, PA GPIO20, I2S DIN 11 |
| Camera | `sdkconfig.defaults:158-167` (OV02C10 1288x728), `app_video.c:134-144` HFLIP | OV02C10 | same sensor; HFLIP and PSRAM budget to re-check |
| Board text | `feature_specs.c:92-161` | prose with "4.3", "GT911", "external speaker", "no camera" (stale) | must come from the profile |
| Build | root `CMakeLists.txt` (no board switch), one `components/`, one `sdkconfig.defaults`, `bsp_extra/idf_component.yml` path-dep on the 4.3" BSP by name | | two BSP forks share the component name `espressif__esp32_p4_function_ev_board` and cannot coexist in one `components/` |
| Settings | `settings.c:62-79` defaults (`orientation`, `bright_min/max`, `volume`, `alarm_volume`, `voice_mic_gain`) | tuned for the 4.3" | per-board defaults; NVS blob must not carry 4.3" gains onto a 10.1" |
| C6 | `main/idf_component.yml:4` pins `esp_hosted ~2.12.0` | slave 2.12.12 | slave ships 2.3.0; reflash required |
| Patched registry components | `managed_components/espressif__esp_lvgl_port` (3 sites), `espressif__esp_codec_dev` (1 site) | regenerated on lock refresh, patches lost | blocks any multi-build-dir scheme |

## 3. Design

### 3.1 Single source of truth: `devices/<board>/board.json`

`devices/guition-jc4880p443c/board.json` and `devices/guition-jc8012p4a1c/board.json`
exist from today. They hold the board card data (SoC, memory, display geometry and
timing, touch, I2C, audio, camera, SD, battery, RTC, USB, console pins, UI scale and
font roles, feature list, revision notes, sources). Schema in section 8.

At configure time a small generator turns the selected board's JSON into two artefacts
inside the build tree:

```
build_<board>/gen/board_defs.h        compile-time constants and feature macros
build_<board>/gen/board_profile_data.c   the const board_profile_t instance
```

Generator: `tools/gen_board_profile.py <board.json> <out_dir>` (Python 3, no third-party
modules, run via `add_custom_command` from the `board_profile` component, re-run when
the JSON changes). Pins in the JSON are **documentation plus cross-check**: the vendor
BSP headers remain the pin authority for the P4 boards, and a `--check-bsp
<bsp_header_dir>` mode of the generator fails the build if a JSON pin disagrees with
the BSP macro (`BSP_LCD_RST`, `BSP_POWER_AMP_IO`, `BSP_I2S_DSIN`, ...). That turns the
two mistakes we already found between boards (PA 11 vs 20, DIN 48 vs 11) into build
errors instead of silent audio.

### 3.2 Build selection

Root `src/bedside_clock/CMakeLists.txt` (before `include(project.cmake)`):

```cmake
if(NOT DEFINED BOARD)
    set(BOARD jc4880p443c)            # the board this project grew up on
endif()
set(BOARD_DIR ${CMAKE_SOURCE_DIR}/boards/${BOARD})
if(NOT EXISTS ${BOARD_DIR}/board.cmake)
    message(FATAL_ERROR "Unknown BOARD '${BOARD}'. Available: jc4880p443c, jc8012p4a1c")
endif()
include(${BOARD_DIR}/board.cmake)     # sets BOARD_DEVICE_DIR, BOARD_FONT_SRCS, BOARD_EXTRA_DEFAULTS
set(EXTRA_COMPONENT_DIRS ${BOARD_DIR}/components)
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults;${BOARD_DIR}/sdkconfig.board.defaults${BOARD_EXTRA_DEFAULTS}")
set(SDKCONFIG ${CMAKE_SOURCE_DIR}/sdkconfig.${BOARD})
```

Developer commands become:

```powershell
idf.py -B build_jc4880p443c -D BOARD=jc4880p443c build            # 4.3", WakeNet
idf.py -B build_jc4880p443c_mww -D BOARD=jc4880p443c -D VARIANT=mww build
idf.py -B build_jc8012p4a1c -D BOARD=jc8012p4a1c build            # 10.1"
```

`VARIANT=mww` appends `sdkconfig.mww.defaults` exactly as today's manual recipe in
`main/Kconfig.projbuild:13-15`, so the existing microWakeWord variant composes with the
board switch (2 boards x 2 variants). Generated `sdkconfig.<board>[.mww]` files are
gitignored; `sdkconfig.*.defaults` are committed.

Kconfig mirror: `main/Kconfig.projbuild` gains `choice BEDSIDE_BOARD` with one symbol per
board. Each `boards/<id>/sdkconfig.board.defaults` sets its own symbol, so menuconfig
shows the selection and components can `depends on BEDSIDE_BOARD_JC8012P4A1C` where a
compile-time gate is unavoidable (touch driver dependency, below). CMake remains the
single entry point; the Kconfig symbol follows it.

### 3.3 Component layout

```
src/bedside_clock/
  boards/
    jc4880p443c/
      board.cmake                  BOARD_DEVICE_DIR=../../devices/guition-jc4880p443c, font list
      sdkconfig.board.defaults     target, rev <3, flash, PSRAM, BSP Kconfig, hosted SDIO, camera, LVGL fonts
      components/
        espressif__esp32_p4_function_ev_board/   vendor fork 5.2.3 (ST7701 under the "1024_600" Kconfig)
        espressif__esp_lcd_st7701/               vendor-patched 1.1.3
    jc8012p4a1c/
      board.cmake
      sdkconfig.board.defaults
      components/
        espressif__esp32_p4_function_ev_board/   vendor fork 4.1.1 (JD9365 under the ILI9881C branch), reset 27, PA 20
        espressif__esp_lcd_jd9365/               vendor copy 1.0.2 (or registry 1.0.x if identical)
    common/
      p4_guition_hal.c             the HAL for both P4 Guition boards (shared source, see 3.4)
  components/                      shared, board-agnostic
    board_profile/                 struct, accessor, generator hook (3.1)
    bsp_extra/                     audio helpers; manifest path-dep replaced by `REQUIRES esp32_p4_function_ev_board`
    espressif__esp_hosted/         2.12.12 + local patch (unchanged)
    esp_cam_sensor/, espressif__usb_host_uac/, chmorgan__esp-audio-player/
    espressif__esp_lvgl_port/      moved out of managed_components WITH the PPA patches (3.3.1)
    espressif__esp_codec_dev/      moved out of managed_components WITH the set_fmt patch
```

Only one `boards/<id>/components` is on `EXTRA_COMPONENT_DIRS` per build, so the two
forks that share the component name no longer collide. `bsp_extra` today declares the
BSP as a manifest **path** dependency (`components/bsp_extra/idf_component.yml`), which
is what locks an absolute board-specific path into `dependencies.lock:189-192`; it
becomes a plain CMake `REQUIRES` and the lock entry disappears.

3.3.1 Patched registry components. `src/bedside_clock/patches/` now carries the diffs,
but a patch directory is a stop-gap: copy `espressif__esp_lvgl_port` 2.9.0 and
`espressif__esp_codec_dev` 1.5.11 from `managed_components/` into `components/` (local
components override managed ones, the component manager warns and proceeds), drop the
two lines from `main/idf_component.yml`, keep the `MODIFIED` notice at the top of each
patched file. This is phase 0 because every later phase creates new build directories.

### 3.4 The `board_profile` component

```c
// components/board_profile/include/board_profile.h
typedef enum { BOARD_ORIENT_LANDSCAPE, BOARD_ORIENT_LANDSCAPE_FLIP, BOARD_ORIENT_PORTRAIT, BOARD_ORIENT_COUNT } board_orient_t;

typedef struct {
    const char *id;            /* "jc8012p4a1c" */
    const char *name;          /* "Guition JC8012P4A1C_I_W_Y 10.1\"" */
    struct { uint16_t native_w, native_h; bool portrait_native; const char *controller, *interface; } panel;
    lv_display_rotation_t rotation_for[BOARD_ORIENT_COUNT];
    struct { uint16_t scale_q8; uint16_t draw_buf_lines;
             const lv_font_t *body, *label, *title, *icon, *hh, *mm; } ui;
    struct { bool builtin_speaker; uint8_t mic_count; const char *afe_input_fmt;
             int8_t mic_gain_db; uint8_t digital_gain; uint16_t selftest_min_rms;
             uint8_t volume_default, alarm_volume_default; const char *codec, *amp; } audio;
    struct { bool present; bool hflip; const char *sensor; } camera;
    struct { const char *controller; bool int_wired; } touch;
    struct { int8_t adc_gpio; uint16_t divider_x100, empty_mv, full_mv; } battery;
    struct { bool present; const char *expected_slave; } coproc;
    uint32_t features;         /* BOARD_FEAT_CAMERA | BOARD_FEAT_SD | BOARD_FEAT_USB_HOST | ... */
} board_profile_t;

const board_profile_t *board(void);
#define UI_PX(n)  ((int32_t)(((n) * (int32_t)board()->ui.scale_q8) >> 8))
#define BOARD_HAS(f) ((board()->features & (f)) != 0)
```

`board_defs.h` (generated) additionally provides `BOARD_NATIVE_W`, `BOARD_NATIVE_H`,
`BOARD_FEAT_*` masks and `BOARD_ID_STR` for places that need constants in static
initialisers or `#if`.

HAL seam (`board_hal.h`, implemented by `boards/common/p4_guition_hal.c` for both P4
boards): `board_hal_display_start(cfg)`, `board_hal_display_lock/unlock`,
`board_hal_brightness_set`, `board_hal_i2c_handle`, `board_hal_audio_*` (thin pass-through
to `bsp_extra` today). `main/` stops including `bsp/display.h`; `bsp/esp-bsp.h` and
`bsp_board_extra.h` are included only by the HAL. The HAL is deliberately thin: it is the
list of functions an ESP32-S3 BSP will have to provide, nothing more.

### 3.5 UI scaling and fonts

- Apply `UI_PX()` to the roughly 85 height/pad/offset literals in `ui_settings.c`,
  `ui_play.c`, `ui_common.c`, `alarm.c`, `voice.c` and the Large-face offsets in
  `ui_clock.c:269-292`. Widths already use `lv_pct`.
- `ui_settings.c:1509` `lv_obj_set_height(cont, 480)` becomes `lv_pct(100)`.
- Fonts come from `board()->ui.*`. The Giant clock face is already resolution-aware
  (`ui_clock.c:181-182` reads the display size and lays out by glyph ink); it only needs
  the taller Dangrek assets. Generate `dangrek_780.c` / `dangrek_360.c` with the existing
  `tools/fonts/gen_fonts.sh` (digits, colon, dash only) and `montserrat_ext_{20,24,32,44}.c`
  for the 10.1" body sizes.
- `main/CMakeLists.txt` takes `BOARD_FONT_SRCS` from `board.cmake` so **only one** giant
  font set is linked. `dangrek_478.c` is already the largest object in the 8 MB app;
  linking both sets would not fit next to esp-sr.
- LVGL glyph draw buffer: `CONFIG_LV_DRAW_SW_...` sizing in `sdkconfig.defaults:93` scales
  with the largest glyph (about 2.5x at 780 px); move that line to the per-board defaults.

### 3.6 Orientation semantics

`ORIENT_LANDSCAPE` means "rotate the native panel by 90 degrees" today
(`app_main.c:302-311`). Keep the user-facing setting values (NVS compatibility) but
resolve them through `board()->rotation_for[]`, filled by the generator from
`panel.portrait_native`. A landscape-native S3 panel maps `ORIENT_LANDSCAPE` to rotation
0 without touching `main/`.

### 3.7 Audio profile

`bsp_extra` stays the engine; the profile supplies: speaker type (for `feature_specs`,
the USB-audio fallback wording and the loopback self-test threshold), mic count and AFE
input format string (`"M"` for both boards until a second ADC path is proven; the 10.1"
"2 mics" are analog into the single ES8311 ADC, inference from the boot log showing only
ES8311), mic gain and digital gain defaults, volume defaults. `voice.c:238-335` self-test
reads its threshold from the profile. PA pin and I2S pins stay in the BSP (cross-checked
by the generator).

### 3.8 Touch

- 4.3": GT911 via the BSP, polled (INT unwired). Unchanged.
- 10.1": GSL3680 at 0x40 with an 800 ms firmware upload, INT on GPIO21, RST on GPIO22,
  `mirror_y` in the vendor BSP. Every available driver is GPL-2.0-or-later (Silead
  `gsl_point_id.c`) and the controller firmware array carries no licence. Decision:
  the 10.1" BSP fork depends on the registry component
  `mangoo1/esp_lcd_touch_gsl3680 ^1.0.0` (declared in
  `boards/jc8012p4a1c/components/espressif__esp32_p4_function_ev_board/idf_component.yml`
  so it is only resolved for that board). Nothing GPL enters this repository. Consequence
  (see `docs/licensing.md`): a 10.1" `bedside_clock` image links GPL code together with
  esp-sr's closed libraries and Espressif's field-of-use "MIT" components, so a
  GPL-compliant **binary** distribution of the 10.1" firmware is not possible; the 10.1"
  build is source-only (users build it themselves). The 4.3" build is unaffected. Ways
  out, if binary releases matter later: a clean-room GSL3680 driver with the Silead
  firmware fetched at build time (ESPHome's approach), or a 10.1" variant without esp-sr.
  With INT wired, `esp_lvgl_port` uses its ISR path automatically; the 4.3" polling
  behaviour is untouched.
- `ui_watch.c` touch trace and all of `main/` stay driver-agnostic (`lv_indev_*` only).

### 3.9 Camera, SD, battery, RTC

Feature flags from `board.json` gate initialisation: `BOARD_HAS(BOARD_FEAT_CAMERA)` around
`ambient.c` start, `BOARD_FEAT_BATTERY_ADC` for a future battery indicator (GPIO 53 vs 52
and divider live in the profile), `BOARD_FEAT_RTC` for a future external-RTC time seed.
Camera HFLIP comes from `board()->camera.hflip`. The OV02C10 1288x728 mode stays in the
shared defaults (same sensor on both boards); the PSRAM budget on the 10.1" is 4.1 MB
LCD frame buffers + 3.6 MB camera + 256 KB draw buffer, comfortably inside 32 MB but
the internal-RAM reserve (`CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL`) must be re-measured.

### 3.10 Settings schema v12

Add `uint8_t board_id` to the settings blob (`settings.c:18` version 11 -> 12). On load,
if `board_id != board()->id_num`, re-apply the board-sensitive defaults (`orientation`,
`bright_min/max`, `volume`, `alarm_volume`, `voice_mic_gain`) and reset the learned
ambient calibration (`luma_floor/ceil`), then stamp the new id. A blob carried between
boards can no longer leave a 10.1" with the 4.3" mic gain.

### 3.11 API and web UI

`GET /api/v1/status` gains a `board` object: `{id, name, panel:{w,h,controller},
features:[...]}`. `feature_specs.c` prints from the profile instead of prose
(`feature_specs.c:92-161` has five stale or board-specific lines today, including
"no camera module fitted" which is wrong on both boards). `www/index.html` reads
`board.features` to hide controls that do not apply (battery, USB audio).

### 3.12 C6 co-processor strategy

The host library is pinned to `esp_hosted ~2.12.0` (`main/idf_component.yml:4`) because
the 4.3" slave runs 2.12.12 and vHCI fails across a mismatch. The 10.1" ships slave
2.3.0 and its factory firmware already logs `Version mismatch ... Upgrade co-proc`.
Decision: do not carry two host libraries. Reflash the 10.1" C6 to a 2.12.x slave image
(procedure and options in `devices/guition-jc8012p4a1c/docs/c6-firmware.md`) as part of
phase 3, and record the expected slave version in `board.json` so `boot_seq.c` can warn
when the handshake reports something else.

### 3.13 Keeping ESP32-S3 possible

- `main/idf_component.yml`: wrap `esp_hosted`, `esp_wifi_remote`, `usb_host_uac` and the
  esp-sr model selection in `rules: - if: "target == esp32p4"` blocks; an S3 build pulls
  native WiFi and its own esp-sr libraries.
- `wifi_mgr.c` is the only file that includes `esp_hosted.h`; it gets a
  `#if CONFIG_IDF_TARGET_ESP32P4` around the hosted-specific calls and nothing else in
  `main/` may test the target.
- `usb_audio.c` and `ambient.c` compile only when `BOARD_FEAT_USB_HOST` /
  `BOARD_FEAT_CAMERA` are set (CMake `if()` on the generated feature list).
- The S3 board directory provides its own BSP (RGB or SPI panel, no DSI, no PPA), a HAL
  file, `portrait_native=false` and smaller PSRAM; `esp_lvgl_port` software rotation on an
  RGB panel takes the avoid-tearing path, which is why the HAL owns `display_start`.

## 4. Repository conventions for boards

```
devices/<board>/                 hardware truth (README, board.json, docs, firmware, vendor, community)
src/<project>/boards/<id>/       firmware-side selection: board.cmake, sdkconfig.board.defaults, components/
```

`board.cmake` points at the device folder, so one `board.json` serves every firmware
project (`sample_hello_world` can adopt the same mechanism later; it is 4.3"-only today).

## 5. Migration phases

Each phase ends with the 4.3" board flashed and the acceptance list in section 7 green.

Phase 0, safety (2 to 4 h)
1. Move the patched `esp_lvgl_port` and `esp_codec_dev` from `managed_components/` to
   `components/`; drop their manifest lines; build; confirm the `LOCAL PATCH` markers are
   compiled (grep the build's `compile_commands.json`).
2. Add `tools/gen_board_profile.py` with the 4.3" JSON; emit `board_defs.h` only; no
   consumers yet.

Phase 1, plumbing (4 to 8 h)
1. Create `boards/jc4880p443c/` and move the two vendor components into it; rewrite the
   `bsp_extra` dependency; add `-D BOARD` handling, `choice BEDSIDE_BOARD`,
   `sdkconfig.board.defaults` split out of `sdkconfig.defaults` (lines 1-13, 19-27, 44-45,
   74-84, 97-120, 158-167, 173-174 are board; the rest stays app).
2. Add the `board_profile` component and the HAL; `app_main.c:342` uses
   `board()->panel.native_w * board()->ui.draw_buf_lines`; `app_apply_orientation` uses
   `rotation_for[]`.
3. Verify `build_jc4880p443c` and `build_jc4880p443c_mww` produce working firmware; update
   `Kconfig.projbuild` help and `README.md` with the new commands.

Phase 2, de-literal `main/` (6 to 10 h)
1. `UI_PX()` pass; `ui_settings.c:1509`; Large-face offsets; font roles from the profile.
2. `feature_specs.c` from the profile; `board` object in `/api/v1/status`; web UI reads it.
3. Settings v12 `board_id`; `test_alarm_sched.py` unaffected; add
   `tools/test_board_profile.py` (schema validation + BSP cross-check in CI).

Phase 3, 10.1" bring-up (14 to 26 h)
1. `boards/jc8012p4a1c/`: vendor BSP fork 4.1.1 from
   `devices/guition-jc8012p4a1c/vendor/package/1-Demo/idf-examples/common_components/`,
   JD9365 driver, `mangoo1/esp_lcd_touch_gsl3680` dependency, `sdkconfig.board.defaults`
   (same rev <3 flags, DPI buffers, hosted pins).
2. A throwaway `BOARD=jc8012p4a1c` build of `sample_hello_world`-style display + touch
   smoke test (menu, backlight slider, touch trace) to validate: panel timing on **this**
   unit (V1 vs V2 init table), rotation cost at 1280x800 with and without PPA, PSRAM and
   internal-RAM headroom, GSL3680 coordinate transform after rotation.
3. Reflash the C6 to esp_hosted slave 2.12.x; confirm the handshake log shows matching
   versions and WiFi connects.
4. Generate the 10.1" font set; first `bedside_clock` boot on the 10.1".

Phase 4, product parity on the 10.1" (8 to 16 h)
1. Audio: built-in speaker loudness, self-test threshold, mic gains, wake-word recall
   with the new mic placement (re-run the `tools/wakeword_train` evaluation clips).
2. Camera HFLIP and ambient calibration; battery ADC indicator (new feature, optional).
3. Soak: 24 h with the event journal and `ui_watch` (PPA fallback counters must stay 0 or
   be explained).

Phase 5, polish (3 to 6 h)
1. `tools/build_matrix.ps1` building all four variants; size table per board in the
   README; CHANGELOG entry; device READMEs updated with "runs bedside_clock since <date>".

## 6. Risks, ranked

| # | Risk | Mitigation |
|---|---|---|
| 1 | JD9365 timing/init differs between V1 and V2 panels; wrong table = black or torn screen | Read the rear label; start from the vendor BSP values that the factory firmware proves on this unit; keep both ESPHome tables in the board folder |
| 2 | PPA rotate at 1280x800 (2.7x the pixels) on rev v1.3 silicon | The bounded-wait + CPU fallback patch already exists; measure CPU rotation cost; consider rendering the UI landscape-native with LVGL rotation only on the draw buffer |
| 3 | GSL3680 driver quality (coordinate drift reports in the HA thread) and the GPL + closed-library mix making 10.1" binaries undistributable | Registry dependency, not vendored; coordinate transform validated with the touch trace overlay; source-only distribution for the 10.1" documented |
| 4 | Flash budget: second font set + esp-sr + models in the 8 MB slot | Per-board font linking; measure with `idf.py size-components`; the partition table has 3.75 MB unallocated as a last resort |
| 5 | "2 mics" wiring unknown; AFE format may need `"MR"` or a mixer | Treat as `"M"`; verify with the loopback self-test and recordings |
| 6 | C6 slave reflash on the 10.1" bricks the C6 | Back up first; UART recovery pins documented; EspControl's C6 recovery flow as reference |
| 7 | Settings blob migration v12 on units in the field (one unit today) | Append-only field; defaults re-applied only on board mismatch |
| 8 | `dependencies.lock` churn when the BSP path dependency is removed | Commit the regenerated lock once per board; the lock is machine-independent after the change |

## 7. Acceptance checklist per board

- `idf.py -B build_<board> -D BOARD=<board> build` succeeds from a clean clone with only
  the documented toolchain; `size-components` recorded.
- Boot log shows the profile banner (`board: <id> <name> <w>x<h>`), no `LOCAL PATCH`
  missing warning from `patches/apply_patches.sh`, LDO ch3 acquired, touch controller
  identified, C6 handshake with matching versions.
- Clock face, settings pages and play page render without clipping at the board's
  landscape size; touch targets land where drawn (touch trace overlay).
- Alarm fires, song plays, voice wake works, API `status` reports the right `board`.
- 24 h soak without PPA fallback or watchdog events in the journal.

## 8. `board.json` schema (v1)

Top-level keys: `schema`, `id`, `name`, `vendor`, `sku`, `soc`, `coprocessor`, `display`,
`touch`, `i2c`, `audio`, `camera`, `sdcard`, `battery`, `rtc`, `usb`, `console_uart`,
`ui`, `features`, `revisions`, `status`, `sources`. See the two instances under
`devices/*/board.json`; the generator validates required keys and value ranges and
rejects unknown top-level keys so typos fail loudly. `ui.scale_q8` is the fixed-point
multiplier applied by `UI_PX()` (256 = 1.0). `ui.fonts` names the LVGL font symbols per
role; the generator emits `LV_FONT_DECLARE` lines for them.

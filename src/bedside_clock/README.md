# bedside_clock

ESP-IDF 5.5.5 firmware for a bedside alarm clock on the Guition **JC4880P443C** (ESP32-P4
rev v1.3 + ESP32-C6, 4.3" 480x800 MIPI-DSI panel, GT911 touch, ES8311 codec, OV02C10 camera).
The panel is driven in landscape through LVGL 9 software rotation. A port to the 10.1"
JC8012P4A1C is planned as a second board profile; see
[../../docs/architecture/board-profiles-plan.md](../../docs/architecture/board-profiles-plan.md).

Older documents and code comments call this project `bedside_v01`. Licence: MIT for the code
here; third-party parts are listed in [NOTICE.md](NOTICE.md) and the repository's
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md).

## What it does

- **Alarms**: three slots, each with a time, a weekday mask (bit0 Sunday .. bit6 Saturday) and
  a ring-once flag; shared sound mode (chime, or a Navidrome playlist played in random order),
  alarm volume and an auto-stop time (`max_alarm_seconds`, 0 = ring until dismissed). Snooze
  is 9 minutes. A ring that stopped by itself leaves a "missed" marker on the clock face. An
  alarm that was due during a reboot is caught up within a 5 minute window.
- **Music**: Subsonic REST client for a Navidrome server (`navidrome_client.c`), songs cached
  in PSRAM while they download (`song_cache.c`), MP3 decoding through `esp-audio-player`.
  Play by song query, playlist query, random, or by exact id from the web/device pickers.
- **Voice control**: always-on microphone front end from `esp-sr` (AFE with WebRTC VAD).
  Wake word is either Espressif's WakeNet9 "Hi ESP" (default build) or an embedded
  microWakeWord model on `esp-tflite-micro` ("Hey Mr Clock" and "Hi ESP", the `build_mww`
  variant). MultiNet7 then matches one of about 60 English grammar phrases on-device
  (`voice_actions.c`). Free-text requests ("play song ...") are recorded, endpointed by the
  VAD and sent to an OpenAI-compatible server (LocalAI: whisper for transcription, a chat
  model to pick the best library match). User guide: [docs/VOICE.md](docs/VOICE.md);
  internals: [docs/VOICE_AND_AI_PIPELINE.md](docs/VOICE_AND_AI_PIPELINE.md).
- **Ambient dimming**: the camera is used as a light sensor (1288x728 RGB565 frames, mean
  luma, sampled every `ambient_period_s` seconds with the sensor asleep in between) to move
  the backlight between a night floor and a day ceiling; self-calibrating.
- **USB audio**: a USB Audio Class speaker on the OTG port takes over from the built-in codec
  while present and falls back to it on any failure (`usb_audio.c` + `bsp_extra_sink_t`).
- **HTTP control API** on port 80 (`api_server.c`), off by default, enabled in Settings with a
  shared key sent as `X-Api-Key` or `Authorization: Bearer`. JSON endpoints for status,
  settings (PATCH applies live and persists), playback, controls, playlists/song search, the
  event journal, the RAM log, the USB survey and wake-word clips.
- **Web UI**: `main/www/index.html` (no external assets) served at `/`; clock, playback,
  settings, song/playlist pickers with search-as-you-type, events panel.
- **Persistent event journal** (`evlog.c`): a ring of one-line events (boot, time, WiFi,
  every alarm decision, UI stalls) in noinit RAM with an NVS mirror, readable on the device
  (Settings > Specifications), via `GET /api/v1/events` and in the web UI.
- **Post-mortem**: ELF core dumps to a `coredump` partition, a crash-loop breaker in NVS, a
  safe mode after three abnormal resets (camera, USB host and voice stay off), an LVGL
  heartbeat watchdog that logs per-task state when the UI stalls (`ui_watch.c`).
- Settings are one versioned NVS blob (`settings.c`, version 11; older blobs migrate).
  Text from the music server renders in Latin, Latin Extended-A and Cyrillic through
  generated Montserrat supplements (`main/fonts/`).

## Hardware prerequisites

| Item | Notes |
|---|---|
| Guition JC4880P443C_I_W with ESP32-P4 **rev v1.3** | `sdkconfig.defaults` selects `CONFIG_ESP32P4_SELECTS_REV_LESS_V3` + `REV_MIN_100`; a rev 3.x board needs that changed, and the esp-sr libraries then differ. |
| ESP32-C6 running ESP-Hosted slave firmware **2.12.x** | The host pins `espressif/esp_hosted ~2.12.0`; a mismatched slave gives RPC timeouts. The factory C6 image on the 4.3" board was 2.12.x; the 10.1" board ships 2.3.0 and must be reflashed first. Bluetooth is compiled out (`CONFIG_BT_ENABLED=n`). |
| Speaker on the MX1.25 2-pin connector | The board has no speaker. Without one there is no chime and no self-test tone. |
| OV02C10 camera module | Needed for ambient dimming only; the firmware runs with manual brightness when it is absent or fails to start. |
| Navidrome (or any Subsonic-API server) reachable by HTTP or HTTPS | Required for playlist alarms and music; the chime alarm works without it. |
| Optional: an OpenAI-compatible server (LocalAI) with a whisper model and a chat model | Only for free-text voice queries and the wake-word verifier. No default URL or key is baked in. |
| Optional: USB Audio Class speaker | Plug it into the USB socket that does **not** give you the serial console; the console socket belongs to the USB-Serial/JTAG peripheral and the host controller cannot see devices there. Bus power is present on both sockets when the board is USB-powered. |
| Host PC: ESP-IDF **5.5.5** with the ESP32-P4 toolchain | `idf_component.yml` requires `idf >=5.5.5` (esp_lvgl_port 2.9 needs the DPI frame-buffer callback added there). Node.js only if you regenerate fonts (`tools/fonts/gen_fonts.sh`). |

## Build

The default build is the **WakeNet** variant. Run every `idf.py` command from this directory;
`-C <dir>` with a relative `-B` creates the build directory relative to the current directory,
not the project.

```sh
idf.py set-target esp32p4                 # applies sdkconfig.defaults
idf.py build                              # -> build/
```

The **microWakeWord** variant (`CONFIG_BEDSIDE_WAKE_MWW=y`, the firmware that runs on the
author's clock) is a second build directory with its own sdkconfig. The exact lines, from
`main/Kconfig.projbuild`:

```sh
idf.py -B build_mww -D SDKCONFIG=sdkconfig.mww \
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.mww.defaults" build
```

| | default (`build/`) | `build_mww` |
|---|---|---|
| Wake word engine | esp-sr WakeNet9 `wn9_hiesp` ("Hi ESP") | `main/wake_mww.cpp` on esp-tflite-micro, models from `main/models/` (`BEDSIDE_WAKE_MODELS` in `main/CMakeLists.txt`), switchable live in Settings > Voice |
| Command grammar, VAD, AI stages | identical | identical |
| Extra flash / RAM | - | about 0.4 MB of text for tflite-micro + ESP-NN + microfrontend, 60 KB flash per model, 22 KB PSRAM arena; WakeNet's ~324 KB PSRAM is not allocated |

Two registry components must be patched after the component manager has fetched them
(`managed_components/` is gitignored, so this is per checkout): `esp_lvgl_port` 2.9.0 (bounded
PPA rotate wait and CPU-rotation fallback; without it a PPA hang freezes the UI) and
`esp_codec_dev` 1.5.11. Patches and the procedure are in [patches/README.md](patches/README.md).

Flash and monitor (replace `COMx`; the first flash must be a full `flash`, because the esp-sr
models live in the `model` partition and `app-flash` does not write it):

```sh
idf.py -p COMx flash monitor              # default variant; Ctrl-] leaves the monitor
idf.py -B build_mww -p COMx flash monitor # microWakeWord variant
idf.py -B build_mww -p COMx app-flash     # later app-only updates
idf.py -p COMx coredump-info              # read a stored core dump after a crash
```

Partition table (`partitions.csv`, 16 MB flash): `nvs` 24 KB, `phy_init`, `factory` 8 MB
(esp-dl kernels alone are 1.6 MB of text), `model` 4 MB SPIFFS (WakeNet + MultiNet, label
must be `model`), `coredump` 128 KB.

Notable `sdkconfig.defaults` choices, each explained in the file: software AES
(`CONFIG_MBEDTLS_HARDWARE_AES=n`, hardware AES corrupted TLS records in PSRAM on this chip),
SDIO at 20 MHz, hosted mempool and task stacks in PSRAM (needs the `esp_hosted` alignment
patch in `components/`), `CONFIG_LVGL_PORT_ENABLE_PPA=y`, task watchdog 60 s with panic,
L2 cache 256 KB / 128 B lines, lwIP window 32 KB.

## First boot and configuration

Nothing is preconfigured: no WiFi, no server, no API key.

1. **WiFi**: on the device, cog > WiFi, scan, pick a network, type the password, Connect. The
   C6 is 2.4 GHz only. Credentials go to NVS (`settings.c`).
2. **Timezone**: cog > Timezone. SNTP (`pool.ntp.org`) starts as soon as WiFi is up; the RTC
   carries time across warm resets.
3. **Music API** (Navidrome): URL (`https://host[:port]`, no trailing slash), user, password,
   then "Test server".
4. **Alarms** and **Alarm sound**: times, days, once-flag; chime or a playlist picked from the
   server list; alarm volume; auto-stop.
5. **API** (optional): set a key and enable. The clock's IP is on the clock face and in
   Settings > WiFi. Then `http://<ip>/` is the web UI and, for example:

   ```sh
   curl -H "X-Api-Key: <key>" http://<ip>/api/v1/status
   curl -H "X-Api-Key: <key>" -X PATCH http://<ip>/api/v1/settings \
        -d '{"alarm1_enabled":true,"alarm1_hh":6,"alarm1_mm":45,"alarm1_days":62}'
   curl -H "X-Api-Key: <key>" -X POST http://<ip>/api/v1/playback -d '{"action":"play_random"}'
   ```

   The API stays down after three abnormal resets until re-enabled in Settings. The settings
   it accepts and the `alarmN_*` keys are listed in the header comment of
   `main/api_server.c`.
6. **AI server** (optional): Settings > AI (URL without trailing slash, key, chat model
   name), or `PATCH /api/v1/settings` with `ai_url`, `ai_key`, `ai_model`. Without it, grammar commands still work; "play song <name>"
   and the wake-word verifier report "AI server not configured". The transcription model
   name is compile-time (`AI_STT_MODEL` in `localai_client.c`, default `whisper-1`).
7. **Voice**: mic gain, wake threshold (percent; on the microWakeWord build 78 maps to the
   cutoff the shipped "Hey Mr Clock" model was measured at), wake word, verifier on/off,
   clip collection. Phrases: [docs/VOICE.md](docs/VOICE.md).

Boot order after the display is up (`boot_seq.c`): codec and alarm scheduler, ambient and USB
host, then API registration and WiFi, then SNTP and server probes, then voice last. The alarm
arms whether or not the network ever comes.

## Directory map

`main/`

| File | Purpose |
|---|---|
| `app_main.c` | NVS, settings, display + LVGL sw rotation, clock UI, worker task and `app_run_async()`, lazy codec init under `app_init_lock()`, crash-loop breaker, core-dump check, async media volume |
| `boot_seq.{c,h}` | Ordered service start-up in stages, safe mode, periodic server probes and the "seen recently" freshness window for the indicators |
| `settings.{c,h}` | Versioned NVS settings blob, defaults, sanitiser, migration, `settings_lock()` |
| `wifi_mgr.{c,h}` | esp_hosted + esp_wifi_remote bring-up, scan, connect, autoconnect from stored credentials, RSSI |
| `clock_sync.{c,h}` | Timezone, SNTP, `localtime_r`/`mktime` guarded against concurrent `tzset()`, time-valid logic |
| `alarm.{c,h}` | 1 s scheduler task over the three slots, snooze, ring (chime loop or playlist), auto-stop, missed marker, catch-up window, voice snooze/dismiss entry points |
| `ambient.{c,h}` | Camera-as-light-sensor task, calibration bounds, brightness slew, pauses while settings or an alarm are on screen |
| `app_video.{c,h}` | V4L2 wrapper for the OV02C10 (ported from Espressif's `video_lcd_display` example) |
| `navidrome_client.{c,h}` | Subsonic REST client (JSON via cJSON, MD5 token auth, retries, range-resume streaming) |
| `song_cache.{c,h}` | PSRAM song cache with a single downloader task, `FILE*` streams that block until bytes arrive, LRU eviction |
| `music_player.{c,h}` | Background music engine: resolve (search, optional AI pick), play, playlist loop with journaled fallback reasons |
| `localai_client.{c,h}` | OpenAI-compatible client: `/v1/models` probe, chat completion pick, multipart whisper transcription |
| `voice.{c,h}` | Mic front end and self-test, feed/detect tasks, wake and command loop, green frame and badge, free-speech capture, wake pre-roll clip store, whisper verifier, recording requests |
| `voice_actions.{c,h}` | Phrase table and command registry; dispatch to widgets under the LVGL lock |
| `wake_mww.{cpp,h}` | microWakeWord engine and model registry (compiled in with `CONFIG_BEDSIDE_WAKE_MWW`) |
| `usb_audio.{c,h}` | USB host + UAC: detection, format survey, resampling stream, `bsp_extra_sink_t` registration |
| `api_server.{c,h}` | HTTP control API and the embedded web page |
| `evlog.{c,h}` | Persistent event journal |
| `ui_clock.{c,h}` | Clock face (Large / Giant), date, next-alarm line, service indicators, dim tint |
| `ui_settings.{c,h}` | Settings overlay: tile menu and the WiFi, Music API, AI, API, Alarms, Alarm sound, Timezone, Volume, Brightness, Clock Face, Voice and Specifications pages |
| `ui_play.{c,h}` | Play screen: Song / Playlist / Random tiles, search list, now-playing bar |
| `ui_common.{c,h}` | Shared tile grid and sidebar widgets |
| `ui_watch.{c,h}` | LVGL heartbeat watch and touch trace |
| `feature_specs.c` | Device specification sheet (live values, journal, log) |
| `app_common.h` | `svc_state_t`, audio constants, worker and init-lock prototypes |
| `utf8_util.h` | Repair of UTF-8 strings cut at a byte boundary |
| `fonts/` | `app_fonts.{c,h}` fallback chain, `app_icons.h`; generated tables `dangrek_226/478.c`, `montserrat_ext_{14,16,20,28}.c`, `app_icons_28.c` |
| `models/` | microWakeWord models and manifests, [models/README.md](main/models/README.md) |
| `www/index.html` | The web UI, embedded with `EMBED_TXTFILES` |
| `Kconfig.projbuild` | `CONFIG_BEDSIDE_WAKE_MWW` |

`tools/`

| File | Purpose |
|---|---|
| `wake_clips.py` | PC side of the wake-clip API: pull clips, guided recording sessions, clear (`BEDSIDE_API_KEY` env or `--key`) |
| `wakeword_train/` | The microWakeWord training recipe and the WSL scripts that produced `hey_mr_clock`; [README](tools/wakeword_train/README.md) |
| `fonts/` | Source `.ttf` files with their licences and `gen_fonts.sh` |
| `test_alarm_sched.py` | Host-side mirror of the scheduler rules in `alarm.c` (there is no host C toolchain here) |
| `test_usb_resample.py` | Host-side mirror of the USB resampler arithmetic |
| `test_ai_matching.py` | Replays the song-matching flow against a real Navidrome and AI server (reads credentials from the environment) |
| `readcom.py` | Non-interactive serial capture with optional reset |

`patches/`: unified diffs for `esp_lvgl_port` and `esp_codec_dev` plus a README.
`components/`: the seven local component overrides (see [NOTICE.md](NOTICE.md)).

## Documents

- [docs/VOICE.md](docs/VOICE.md): what to say, tuning, troubleshooting.
- [docs/VOICE_AND_AI_PIPELINE.md](docs/VOICE_AND_AI_PIPELINE.md): models, tasks, both
  pipelines, configuration reference, failure modes, training overview.
- [main/models/README.md](main/models/README.md): adding a wake-word model.
- [tools/wakeword_train/README.md](tools/wakeword_train/README.md): the training recipe,
  model history and lessons.
- [patches/README.md](patches/README.md): the two registry patches.
- [../../devices/guition-jc4880p443c/docs/10-bedside-v01-review.md](../../devices/guition-jc4880p443c/docs/10-bedside-v01-review.md):
  the running code review (fixes by date, open defects, checklist scorecard, API assessment).
- [../../docs/reliability-checklist.md](../../docs/reliability-checklist.md): the rules this
  firmware is held to.
- Board facts, pins, toolchain, troubleshooting: `../../devices/guition-jc4880p443c/docs/`.

## Status and known issues (2026-10-02)

- **PPA hang (mitigated).** On this P4 revision the PPA rotate used by `esp_lvgl_port` can
  never complete, which froze the LVGL task for good. The `patches/` bound the wait to 500 ms
  and fall back to CPU rotation for the rest of the boot; `ui_watch` logs a stall and aborts
  into a core dump after 30 s if the UI still stops. Root cause in the silicon or driver is
  not understood.
- **Codec lock not bounded (open).** `bsp_extra` takes its play/record mutex with
  `portMAX_DELAY`; a stored core dump showed the alarm task tripping the task watchdog while
  waiting on it. A bounded `dev_lock` with a fallback path is the open follow-up.
- **USB audio.** Music playback through a UAC speaker is verified by ear (44.1 kHz stereo
  resampled to the device's 48 kHz). Not yet tested on hardware: the chime alarm over USB
  (16 kHz mono upconverted) and pulling the cable mid-alarm (must fall back to the codec).
- **Bluetooth is off** in this build (`CONFIG_BT_ENABLED=n`); nothing in `main/` uses it. The
  working BLE-over-C6 recipe is in `devices/guition-jc4880p443c/docs/04-bluetooth.md`.
- **Hardware AES is off** because of TLS record corruption under load on this chip
  (`devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md`); re-test on
  each IDF upgrade.
- **"Hey Mr Clock" model**: 0 false accepts per hour on TV audio at the shipped cutoff, 93 %
  recall in a quiet room, poor recall when the phrase is spoken over a loud TV. The WakeNet
  "Hi ESP" model is the fallback pick. Retraining notes in `tools/wakeword_train/README.md`.
- **Stop by voice** works only between songs: while a song plays the player owns the codec and
  the mic is paused (the MIC indicator turns grey).
- **Review items**: the open defect list in section 2 of
  `10-bedside-v01-review.md` (D1 to D13) has not been re-audited for this README; some have
  since been addressed (for example the worker-task fallback in `boot_seq.c`), see the dated
  sections 1b to 1f of that document.
- **10.1" port** (Guition JC8012P4A1C): boots and runs since 2026-10-03 through the BSP's
  `CONFIG_BSP_LCD_TYPE_1280_800` branch and `sdkconfig.jc8012p4a1c.defaults` (build command in
  `../../docs/howto/build-and-flash.md`; panel revision V1/V2 is a menuconfig choice). Still
  open: UI scaling and a 10.1" font set (the clock digits are sized for the 4.3"), the
  `-D BOARD=` switch and board profile, mic and speaker tuning, WiFi and soak verification
  (`../../docs/architecture/board-profiles-plan.md` section 0). The GSL3680 touch driver is
  GPL, so 10.1" builds are source-only (`../../docs/licensing.md`).
- `add_compile_options(-Wno-format -Wno-unused-variable)` in `CMakeLists.txt` hides printf
  format warnings; flagged in the review, not yet removed.

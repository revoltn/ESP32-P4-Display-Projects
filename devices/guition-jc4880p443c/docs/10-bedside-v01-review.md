# bedside_v01 code review — findings and open items (2026-09-02)

Scope: `src/bedside_clock` (the alarm-clock app; named `src/bedside_v01` at the time, and the CMake
project name still is), reviewed against
[docs/reliability-checklist.md](../../../docs/reliability-checklist.md) (then `MUST-IMPLEMENT.md`),
the ESP-IDF 5.5.5 sources, and the project docs.
Trigger: a field device logged `Navidrome: stream read error` on the Device Specifications page
and never retried the song.

Everything below is an **open** item unless it is listed in §1. Line numbers are as of the
review; they drift as files change — search for the quoted identifier if a line has moved.

---

## 1. Fixed in this pass

| Area | Change |
|---|---|
| `navidrome_client.c` | All three HTTP paths share `nav_read_retry` / `nav_fetch_headers_retry` (ride out a silent-server `-ESP_ERR_HTTP_EAGAIN` once; the alarm path zero times) and `nav_diag_log` (one on-screen line + one `ESP_LOGW` with classified cause, errno, TLS error, bytes, elapsed ms, attempt, WiFi RSSI, free internal heap). `esp_http_client_fetch_headers` return is no longer ignored. Reused handles get `esp_http_client_clear_response_buffer` + `Range` header deletion before every open (both were latent bugs). |
| `navidrome_client.c` | `nav_stream_song` reconnects on a dropped/truncated connection with `Range: bytes=<got>-` (2 attempts, 1 s / 3 s backoff, 45 s budget from first failure, abort flag polled). HTTP 206 appends; HTTP 200 asks the sink's new `on_restart` whether it may start over. Chunked (transcoded) bodies are checked with `esp_http_client_is_complete_data_received`. |
| `navidrome_client.c` | `nav_http_get_json` retries the whole (idempotent) API request once. |
| `song_cache.c` | `dl_on_start` now resets `len` (a retry used to append duplicate audio); new `dl_on_restart`; readers track `max_read_pos`; reader stall budget 30 s → 90 s while the downloader is busy, 5 s when nobody is filling the stream. |
| `music_player.c` | `PLAY_FAIL` now updates the status line ("Could not load: … - see Specifications log") instead of leaving a stale "Loading:"; a download that dies mid-song is reported rather than treated as "finished"; random play survives up to 3 consecutive fetch failures. Stale header comment fixed. |
| `alarm.c` | `alarm_init` checks `xEventGroupCreate`/`xTaskCreate`; overlay callbacks guard `s_ev`. |
| `voice.c` | `esp_afe_handle_from_config` and `esp_mn_handle_from_name` results NULL-checked (stale `model` partition no longer panics). |
| `ui_settings.c`, `ui_play.c`, `api_server.c`, `app_main.c` | `settings_save()` and `ambient_reset_calibration()` moved off the LVGL task onto the worker (generation-checked UI update). New non-blocking `app_audio_is_ready()` replaces `app_audio_ensure_init()` in LVGL callbacks and in the HTTP server's `apply_volume`, removing a `portMAX_DELAY` mutex wait from both. |
| `sdkconfig.defaults`, `sdkconfig` | `CONFIG_ESP_TASK_WDT_TIMEOUT_S` 5 → 60 (MUST §2). |
| `managed_components/espressif__esp_codec_dev/esp_codec_dev.c` | **Local patch**: `esp_codec_dev_open` now fails when the data interface's `set_fmt` fails instead of enabling an I2S channel whose DMA descriptors were never allocated (panic in `i2s_rx_channel_start`, see §1a). Keep this patch if the component is refreshed. |
| `components/bsp_extra/src/bsp_board_extra.c` | `bsp_extra_codec_set_fs` logs internal/DMA free + largest block before every reconfigure and dumps `heap_caps_print_heap_info` on failure. |
| `api_server.c` | `GET /api/v1/status` heap block gained `internal_largest`, `dma_free`, `dma_largest`. |
| `navidrome_client.c` | A raw file the cache cannot hold (`on_start` refused) now falls back to the 128 kbps transcode instead of failing the song. |
| `sdkconfig.defaults`, `sdkconfig` | `CONFIG_MBEDTLS_HARDWARE_AES=n` (root cause, §1a), `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` 40000 → 20000 (MUST §4), `CONFIG_LWIP_L2_TO_L3_COPY=y` (+35 KB internal RAM during playback). |

### 1b. Resilience pass, 2026-09-03 (long-running / core processes)

Built, flashed (full flash: new `coredump` partition), boot verified clean, `tools/test_ai_matching.py`
ALL PASS.

| Area | Change |
|---|---|
| `bsp_board_extra.c` | Codec probe failure returns `ESP_FAIL` instead of `assert()` (was a boot loop on a flaky ES8311); initial `set_fs` result propagated; `volume_set` under `_play_mtx`. |
| `clock_sync.c` | Wall clock trusted from the RTC after any non-power-on reset (`> 2025-01-01`) so the alarm fires with WiFi down; TZ applied in `early_init` before the alarm task starts; `clock_sync_is_synced()`. |
| `wifi_mgr.c` | Stored-credential autoconnect arms the 30 s background reconnect on every failure (the old failure path cancelled it → no WiFi after a power cut until reboot); reconnect job retries `wifi_mgr_init()` for a late C6; disconnect reason logged; `wifi_mgr_ip()` copies under a mux (D10). |
| `app_main.c` | `bsp_display_start` + worker creation checked (`app_display_ready()` guards every LVGL helper; headless boot still rings); boot order codec → alarm → ambient → API → WiFi → SNTP → voice; worker queue 16; crash-stable timer periodic until the NVS clear succeeds; safe mode at ≥3 crashes (voice + camera off); reset reason named; `heap_caps_register_failed_alloc_callback`; core dump detection. D1 closed. |
| `music_player.c` | Lazy init checks every handle and returns bool (a NULL queue used to `configASSERT` from LVGL/httpd/voice); `set_status` NULL guard; music task stack in PSRAM (never writes flash). |
| `settings.c` | `settings_sanitize()` (ranges + NUL termination) after load; a longer stored blob (downgrade) loads as a prefix instead of factory-resetting; `settings_save()` returns bool. |
| `alarm.c` | Task WDT subscribed (`CONFIG_ESP_TASK_WDT_PANIC=y`), waits in 5 s fed slices, unsubscribed around the playlist network path; overlay re-shown from the ring loops if the first show lost the lock; `set_fs` failure in the chime backs off; delay-free retries removed. |
| `ambient.c`, `app_video.c` | Camera stall detection (5 frameless samples → log, panel to `bright_max`, bounded stream restart); callback NULL guard (D2), `MAP_FAILED` (D3), driver frame size checked against the allocation (D4); task creation checked; WDT subscribed. |
| `voice.c` | Task creation checked with full teardown (was: `s_running` stuck true, voice dead silently); STT on its own PSRAM-stack task (was 45 s on the shared worker); phrase-table failures detected; self-test task checked. |
| `ui_settings.c` | Inline `settings_save()` / calibration-reset fallbacks removed (they ran NVS on the 7 KB LVGL stack or, via voice, on a PSRAM stack → assert); settings-lock on nav/AI/playlist writers (D5); voice gain + TZ apply posted to the worker; dropped jobs reported. |
| `localai_client.c` | Credentials snapshotted under `settings_lock` (D6); cJSON allocations checked (D8); `out` validated (D13); header lengths clamped. |
| `navidrome_client.c` | `s_api_mtx` take bounded at 10 s (the alarm used to wait up to ~60 s behind a browser search); 8 KB stream chunk moved to PSRAM. |
| `audio_player.cpp` (local patch) | 10 ms delay on a failed I2S write so a closed codec cannot spin-starve the idle task under the now-fatal WDT. |
| `sdkconfig(.defaults)`, `partitions.csv` | `ESP_TASK_WDT_PANIC=y`, `SPIRAM_ALLOW_BSS_SEG_EXTERNAL_MEMORY=y`, core dump to flash (ELF, CRC32) into a new 128 K `coredump` partition at 0xC20000 (appended; no existing offset moved). Kconfig also raised `FREERTOS_ISR_STACKSIZE` from 1536 to its new minimum for core dumps. |

Not yet exercised on hardware (needs an AP power-cycle or the touch UI): background reconnect after an
outage, a full alarm ring under the WDT, the camera restart path, safe mode.

### 1c. Three alarms, weekday masks, auto-stop, 2026-09-19

Settings blob **v9** (append-only): `alarm_slot_t alarms[3]` (`enabled`, `hh`, `mm`, `days` = tm_wday bits,
bit0 Sunday, `0x80` = ring once then switch off; an empty weekday set is normalised to every day) and
`max_alarm_seconds` (0 = ring until dismissed, else 30..7200, default **600** — an upgraded device changes
from "ring forever" to 10 min). Migration copies the old single alarm into Alarm 1 (every day); the legacy
`alarm_enabled/hh/mm` fields are kept as a mirror of Alarm 1 on every save so a downgrade still has it.
Sound mode, wake playlist and alarm volume stay global.

| Area | Change |
|---|---|
| `alarm.c` | Per-slot scheduler state (fired-at persisted as NVS blob `alarm_fired3`, seeded once from the old `alarm_fired`), weekday test on the candidate's own day, earliest due slot rings and slots sharing the instant are marked together; per-slot snooze; once-mode auto-off + settings save on the worker (retried every tick when the queue is full); ring timeout = one `esp_timer` whose callback acts exactly like Dismiss (`s_timed_out` distinguishes it), monotonic deadline, generation guard against a late callback; lock-free `alarm_next()` / `alarm_last_missed()` snapshots for the UI and API; journal lines carry `A<n>`. |
| `api_server.c` | `/settings`: `alarm1_*..alarm3_*`, `max_alarm_seconds`; legacy names deprecated (GET mirror of Alarm 1, PATCH alias). `/status.alarm`: `ringing_index`, `remaining_s`, `next{}`, `missed{}`, `snooze_until`, `max_alarm_seconds`. |
| `www/index.html` | Three alarm groups (time + Mo..Su toggles + presets), "Stop ringing after" select, status line from `next`/`missed`, banner names the alarm and counts down. |
| `ui_settings.c` | Alarms page = accordion of three cards with one shared editor (rollers + day matrix + presets), 1 s repaint when the slots change underneath; new **Alarm sound** tile (mode, playlist, stop-after). |
| `ui_clock.c` | Indicator shows the next ring (`07:00` / `Mon 07:00`), amber while snoozing, red `missed 07:00` after an auto-stop until the face is tapped; date falls back to `%a %d %b %Y` when the full form no longer fits. |
| `voice_actions.c` | "first/second/third alarm", "every day/weekdays/weekends/just once", "open alarm sound"; `bind()` no longer stacks delete hooks on rebinding. |
| `app_main.c` | **LVGL task stack 7168 → 12288.** The first Alarms page build panicked with a *Stack protection fault* in `lv_draw_label_iterate_characters`: LVGL 9 renders recursively (one frame per nesting level plus the draw pipeline) and the port default left ~150 B of headroom. The page was also flattened (day matrix and preset row are direct children of the card, no wrapper column). Rule: keep settings pages shallow; a page nested deeper than ~6 objects below `s_page` needs a stack check. Three consecutive panics trip the crash-loop breaker (API auto-start suppressed, voice/camera skipped) until a 30 s stable boot - that is why "API and Voice greyed out" accompanied the crash. |
| `api_server.c` | Test hook `POST /api/v1/controls {"action":"open_settings_page","page":N}` opens Settings on page N on the LVGL task (`lv_async_call`), exactly like a tap - used to reproduce UI crashes with the serial log attached. |

### 1d. Playlist alarm fell back to the chime silently, 2026-09-26

Field report: alarm in playlist mode rang the two-tone chime. The journal had `A2 ringing 08:44 (playlist)`
then `A2 timed out after 2 min` and nothing in between - every early exit inside `playlist_loop()` was
either silent or `ui_log`-only (16 KB RAM, unreadable after the fact). Any single failed download with
nothing played yet also dropped straight to the chime.

| Area | Change |
|---|---|
| `alarm.c` | `playlist_loop(idx)` journals every non-button exit: `A<n> playlist: wifi down / no playlist chosen / music server not set / player init failed`, `playlist fetch failed`, `playlist is empty`, `dl fail <k>: <reason>`, `fmemopen failed`, `player start failed`, and `A<n> playing <title>` when a song starts; `alarm_fire` adds `A<n> playlist gave up - chime`. Retry policy: up to 3 consecutive failures inside a 30 s budget (`ALARM_PLAYLIST_BUDGET_MS`, restarted after each song that played) instead of "first failure = chime". `fetch_playlist_songs()` re-resolves the playlist id by its stored name once when the server answered Subsonic error 70 "not found" (`nav_last_api_error()`; an unreachable server is not retried, that would only add silence), stores + saves a changed id (`A<n> playlist id refreshed`) or journals `playlist gone: <name>`. **Bench-confirmed root cause 2026-09-26**: `Navidrome err 70: playlist not found` - the `category_dance` playlist had been recreated server-side under a new id; with the refresh a song played 9 s after the ring started. |
| `navidrome_client.c/.h` | `nav_download_song(..., char *why, size_t why_cap)` returns a short reason (`timeout 0KB/15s`, `TLS 512KB/3s`, `closed early`, `HTTP 404`, `no PSRAM`, `truncated`, `too short`); `nav_diag_log` gained the optional output. Subsonic API errors (`error 70 not found`, `40 wrong credentials`) are journaled once as `Navidrome err <code>: <msg>`. |
| `api_server.c`, `ui_settings.c`, `app_common.h` | **`GET /api/v1/log`** (auth as usual) returns the in-RAM ui_log as `text/plain` via new `ui_log_copy()` (tail when >16 KB). `/events` says *what* happened, `/log` has the detailed lines around it - until the next reboot. |
| `www/index.html` | Events panel gained a **Device log** button (raw fetch, newest lines last). |
| `alarm.c` | **The alarm task runs at priority 3 for the `playlist_loop` phase** (`vTaskPrioritySet(NULL, 3)`, back to 5 after), the same level as the other bulk-TLS tasks. At priority 5 the whole-file download with software AES starved taskLVGL (4) - the bench capture showed `ui_watch: LVGL task stalled 6000 ms, state=R` right through the transfer, i.e. Snooze/Dismiss was untouchable for the whole download. After the change: **0 stall warnings**, 3.6 MB fetched in 7.8 s, a touch at +85.167 s dismissed the ring at +85.271 s (104 ms). |

Device-verified 2026-09-26 (API-armed Alarm 3, serial attached, both paths): playlist mode reached
`A3 playing Chase the Sun` 8 s after the ring with the UI responsive; with `nav_url` pointed at an
unreachable host the ring fell back to the chime exactly 30 s in (`A3 playlist fetch failed` →
`A3 playlist gave up - chime`). Bench gotcha: closing/opening `tools/readcom.py` on COM3 around a test
can reset the board and kill an in-flight HTTP request, and the httpd 2-socket cap shows up as
`httpd_accept_conn: error in accept (113)` when a test script polls while the serial port cycles.

### 1e. USB audio output, phase 1 (detection), 2026-09-26

The ES8311 is a mono codec, so `bsp_extra_i2s_write` averages L and R and the stereo image is
discarded. `espressif/usb_host_uac` had been a declared dependency since the "Jabra speaker"
note in `main/idf_component.yml`, with hub support enabled in `sdkconfig.defaults`, but no code
ever called it: zero `uac_host_*` symbols in the linker map. Goal is that **the built-in mono
speaker is used unless a USB speaker is plugged in, and then that one takes over automatically**.

Shipped in phase 1 - detection only, no audio is routed anywhere yet:

| Area | Change |
|---|---|
| `main/usb_audio.{c,h}` (new) | USB host + UAC driver install, `usb_lib` daemon task, connect/disconnect tracking, and a **silent survey** on its own `usb_audio` task: opens the device, records identity and every alternate setting, then *trial-starts* each advertised 16-bit format until one succeeds and immediately stops it. A format can be advertised and still refuse to start when it exceeds the endpoint max packet size, so asking is the only way to know. Ported from `src/sample_hello_world/main/feature_usb_audio.c`. |
| `main/api_server.c` | **`GET /api/v1/usb`** returns the survey as text/plain, and `GET /api/v1/status` gains a `usb_audio` object `{state, present, product, vid, pid, rate, channels}`. The text endpoint exists because `uac_host_printf_device_param()` only reaches the serial console, and a USB speaker occupies the board's USB port. |
| `main/boot_seq.c` | `usb_audio_init()` in stage 1 before the codec, **gated on `!safe_mode`** like the camera: three consecutive panics disable it and the clock still boots. It never blocks and never waits for a device. |

**Measured on the device 2026-09-26 22:59 (no speaker attached):** host stack installs cleanly and
costs **6,404 bytes of internal RAM** (171 KB still free), boot is clean, alarms arm normally,
`test_alarm_sched.py` passes, and `status.usb_audio.state` reads 1 (`SVC_WAIT`, stack up / no
device). **The serial console is completely unaffected by the host install**, which says the OTG
host controller is a different peripheral from the USB-Serial/JTAG that provides COM3.

Phase 2 (automatic routing) and phase 3 (indicator, settings) are planned but not written. The
routing design is a `bsp_extra_sink_t` function table registered into `bsp_extra_i2s_write`, tried
*before* the in-place mono downmix and falling through to the codec on any failure, so a USB
problem can never silence the alarm.

### 1f. USB audio phase 2 (routing) + the host-port dead end, 2026-09-26 night

Routing is written and on the board. `bsp_extra_sink_t` is a function table registered by
`main/usb_audio.c` into the BSP; `bsp_extra_i2s_write` tries it **before** the in-place mono
downmix (so a USB sink sees untouched stereo) and falls through to the ES8311 on any failure,
which is what stops a USB fault from ever silencing the alarm. `set_fs` and `volume_set` notify
the sink outside the codec lock, so no lock-order inversion is possible. The USB side keeps the
stream open while a device is present, converts 44.1 kHz stereo and 16 kHz mono to whatever the
device negotiated (linear interpolation, phase carried across buffers), uses only bounded locks,
caps the decoder's `portMAX_DELAY` write timeout at 1 s, and rebuilds the stream up to 3 times
after a failed write. `voice.c`'s self-test tone was moved to the new
`bsp_extra_i2s_write_codec()` because it is measured back through the on-board microphone.
Host-side test: `tools/test_usb_resample.py` (frame counts, pitch, and boundary continuity -
the last one is what catches a phase that does not carry over and clicks once per buffer).

**The blocker is the hardware host path, not the firmware.** A Philips SPA3700 enumerated exactly
once (2026-09-26 23:00, phase 1) and never again. Everything below was then tried, and the USB
host reported **zero devices on the bus every single time**:

| Tried | Result |
|---|---|
| Driver rescan when a device is on the bus but unclaimed | never triggered: nothing was ever on the bus |
| `root_port_unpowered` + explicit power-on after the driver is listening | no change |
| Root port power cycling, 3x per boot, for a device attached before boot | no change |
| `peripheral_map = BIT0\|BIT1` (this board has TWO USB sockets on two different peripherals; the default is the high-speed one only, so a device in the other socket was previously invisible) | installed fine, reports 0x3, **serial console unaffected**, still zero devices |
| Both physical sockets, four power-on boots | zero devices each time |

So nothing electrically reaches the host controller. The remaining candidates are all off-board:
the port does not source VBUS (a USB device only pulls up D+ once it sees VBUS, even a
self-powered one), the USB-C receptacle is wired as a device (CC pulldowns) and cannot present as
a host, or the splitter in use is not a host-capable adapter. Note that the original intent
recorded in `sdkconfig.defaults` was **"the Jabra on a powered hub off the HS Type-C port"**, and
`CONFIG_USB_HOST_HUBS_SUPPORTED=y` was enabled for exactly that - a powered hub supplies VBUS
itself, which is the configuration most likely to work here.

**CORRECTION 2026-09-27 - the "board cannot host" verdict that stood here was WRONG.** The user pointed out hello_world drove this same speaker on this board. Both sockets' VBUS pins share the net `USB5V_IN`, so bus power is present on the second socket whenever the board is USB-powered. The real causes of the zero-device readings were: (1) the speaker was in the COM3 socket, whose PHY belongs to USB-Serial/JTAG, so the high-speed host could not see it; and (2) self-inflicted - `peripheral_map = BIT0|BIT1` does not mean both sockets: IDF 5.5 `usb_host_install` supports one root port and picks bit 1 when set (`usb_host.c`: "Right now we support only one peripheral"), and on the P4 bit 1 is the FULL-SPEED controller (`USB_DWC_LL_GET_HW(num)`: 1 = FS, else HS), whose pins reach neither socket. Fix: host config reverted to exactly hello_world's (default peripheral, port powered at install), port-power cycling removed, and the speaker goes in the socket that does NOT give COM3. Lesson: when a working reference exists, diff against it before inventing hardware theories. **Verified 2026-09-27 00:25:** with the PC in the COM3 socket and the Philips SPA3700 in the other one at the same time, the speaker enumerated at boot (`usb: Philips SPA370 48000Hz 2ch ON`), a random song played through it - confirmed by ear - with no USB write failures, 44.1 kHz stereo resampled to the device's 48 kHz. Still to test on hardware: a beep-mode alarm (16 kHz chime upconverted) and pulling the cable mid-alarm (must fall back to the built-in speaker).

Diagnostics left in place, all readable over WiFi because a speaker occupies the port that
provides the serial console: `GET /api/v1/usb` (identity, every advertised format, which one
actually started, plus a live bus census, the peripheral map, and the retry counters) and
`usb_audio` in `GET /api/v1/status`. The journal records `usb: periph map`, `usb: host up`,
`usb: port cycle N/3`, `usb: rescan N/3` and the device line, and survives reboots.

### 1a. What the new diagnostics found on the device (2026-09-02 evening)

With the API key the retry path was exercised against the real server. Every field failure
turned out to be the same thing:

```
Navidrome stream <id>: TLS error after 565KB/302ms try2 fmt=raw rc=-1 errno=0 rssi=-72
navidrome: ... tls_last=ESP_ERR_MBEDTLS_SSL_READ_FAILED tls_code=29056 (SSL - Verification of the message MAC failed)
```

mbedTLS rejected records although lwIP's TCP checksum (always verified in IDF's lwIP; the
`CONFIG_LWIP_CHECKSUM_CHECK_TCP` Kconfig does not exist) had accepted every segment, so the
bytes were intact on arrival and wrong by the time they were decrypted. Bisection on hardware,
one variable per build, random play with 3-5 songs per run:

| Build | TLS MAC errors | Verdict |
|---|---|---|
| baseline (SDIO 40 MHz, HW AES) | every 0.15-0.6 MB | field bug reproduced |
| SDIO 20 MHz | 5 in ~25 MB | rate ~10x lower, not fixed |
| + `LWIP_L2_TO_L3_COPY` | 12 in ~30 MB | not the cause (kept for the RAM gain) |
| + PPA rotation off | 10 in ~25 MB | not the cause (reverted) |
| + `CONFIG_MBEDTLS_HARDWARE_AES=n` | **0 in 26 MB, 5 songs, all first attempt** | **root cause** |

Root cause: the ESP32-P4 AES-DMA path decrypting mbedTLS's record buffer, which lives in
PSRAM because of `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`, returns wrong plaintext under sustained
load. IDF v5.5.5 (tagged 2026-07-16) already contains Espressif's related fixes on the 5.5
branch ("Ensure cache coherency when DMA writes to cacheable PSRAM buffers" and "Cache
invalidate the output buffer before the AES-DMA operation", both 2026-01-16,
`components/mbedtls/port/aes/dma/esp_aes_dma_core.c`); the only later change there
(2026-08-25) is ESP32-S2-specific. So this is an open upstream bug worth reporting with this
reproduction. Moving mbedTLS buffers to internal RAM is not an option (~40 KB per connection,
two connections, ~80 KB free at idle). Software AES costs ~0.6 MB/s TLS throughput, forty times
what 128 kbps playback needs. Re-test with hardware AES after each IDF upgrade.

Verified live in the same sessions: reconnect + `Range` resume (HTTP 206 at 144 KB, 565 KB,
2144 KB ...), server-cannot-resume restart from 0 (transcode → 200), the silent-server path
(`timeout (server silent) after 1024KB/33146ms`, a stalled Navidrome transcode), the
raw-too-big → transcode fallback, and the mid-song failure status on the Play screen.

Second finding, a crash: at song start `bsp_extra_codec_set_fs` reconfigures I2S to the
song's rate; the I2S driver frees its DMA descriptors and re-allocates ~6 x 1 KB in
DMA-capable internal RAM. With only ~41 KB internal free during playback (before the L2 copy
change) that allocation failed once, and `esp_codec_dev_open` ignored the failure and enabled
the channel → `Load access fault` in `i2s_rx_channel_start` → reboot ("Recovered from crash").
Patched (fail the open) and instrumented; the underlying internal-RAM pressure remains an open
item (§3).

---

## 2. Remaining defects (crash or data-corruption class)

| # | Where | Problem | Suggested fix |
|---|---|---|---|
| D1 | `ambient.c:254`, `app_main.c:238`, `voice.c:242`, `voice.c:705`, `voice.c:707` | `xTaskCreate*` results discarded. If the **worker** (`app_main.c:238`) fails, every async job silently drops after 8 queue slots: no WiFi, SNTP, alarm, ambient, voice, API, and no diagnosis. | Check `pdPASS`; `ui_log` + degrade. For the worker, fall back to running `boot_job` inline. |
| D2 | `app_video.c:247` | `user_camera_video_frame_operation_cb` invoked without a NULL check; registration is optional per the header. | Guard, or make registration mandatory in `app_video_stream_task_start`. |
| D3 | `app_video.c:191` | `mmap` failure tested against `NULL`; `mmap` returns `MAP_FAILED`. Unreachable today (userptr path) but wrong. | Compare to `MAP_FAILED`. |
| D4 | `app_video.c:222` vs `app_video.c:205` / `ambient.c:220` | Buffer size negotiated twice: `ambient_init` allocates `hes*ves*2`, then `app_video_set_bufs` overwrites `camera_buf_size` with the driver's `buf.length`, which is what `frame_cb` iterates over. A padded stride would read past the PSRAM allocation. | Allocate from `VIDIOC_QUERYBUF` length, or assert equality. |
| D5 | `ui_settings.c:529`, `:589`, `:776` | `nav_url`/`nav_pass`/`ai_*`/`alarm_playlist_*` strings written without `settings_lock()`; `settings.h:75-79` mandates it and `api_commit` (`:647`) / `wifi_connect_job` (`:314`) obey. `settings_save` on the ambient task can persist a half-copied credential. | Wrap in `settings_lock()/unlock()`. |
| D6 | `localai_client.c:36`, `:52` | `ai_key`/`ai_url` read unlocked on the worker while the LVGL task may be `strlcpy`-ing them → torn string into an HTTP header. | Snapshot under `settings_lock()`. |
| D7 | `api_server.c:790`, `:814` (`handle_controls`), `:394` (`apply_volume`) | Settings mutated from the httpd task without the lock (single bytes, so not torn, but contradicts the module's own contract at `:15-17`). | Route through the locked staging used by `handle_settings_patch`. |
| D8 | `localai_client.c:284-300` | `cJSON_CreateObject` / `cJSON_AddArrayToObject` unchecked; `cJSON_AddItemToArray(msgs, m)` with `msgs == NULL` leaks `m`. | Check allocations; single cleanup path. |
| D9 | `ui_settings.c:767` / `:256` | `new_page()` NULLs `s_pl_list` but not `s_pl_job`; leaving the Alarm page after "Choose playlist…" strands the whole `nav_playlist_list_t` until the overlay closes. | Free `s_pl_job` in `new_page()`. |
| D10 | `wifi_mgr.c:32`, `:94`, `:292` | `s_ip` rewritten by the event task and handed out by pointer to LVGL (`ui_settings.c:699`), worker, httpd (`api_server.c:387`). Torn read possible. | `wifi_mgr_ip(char *out, size_t cap)` copying under a critical section. |
| D11 | `ui_settings.c:362` | SSID recovered by `strstr(txt, "  (")` on the row label text; an SSID containing `"  ("` truncates, a non-label last child is UB. | Carry the scan index in the row's user data (as `alarm_pl_pick_cb` does). |
| D12 | `wifi_mgr.c:204-207` | Driver AP list not cleared when `esp_wifi_scan_get_ap_records` fails (`esp_wifi_clear_ap_list` only on the zero-result path). | Clear on every exit. |
| D13 | `localai_client.c:120`, `:208` | `out[0] = '\0'` and `snprintf(out, out_cap, …)` before validating `out`/`out_cap`. Callers are correct today. | Early-return on `out == NULL || out_cap == 0`. |

---

## 3. MUST-IMPLEMENT scorecard for bedside_v01 (the checklist is now `docs/reliability-checklist.md`)

The checklist asks every project to carry this table. Status after this pass:

| Rule | Status | Evidence / gap |
|---|---|---|
| rev v1.x build flags | ✅ | `sdkconfig.defaults` |
| Crash-loop breaker | ✅ | `app_main.c:112-159`; gates the API at ≥3 (`api_server.c:180`) |
| Boot counting / safe mode | ⚠️ | Counter yes; **no minimal recovery state** — at crash count 5 the firmware still brings up LVGL, camera, esp-sr, music |
| NVS commit before reboot | ✅ | vacuous — no `esp_restart()` in the app |
| NVS erase-retry | ✅ | `app_main.c:199-203` |
| Task WDT sizing | ✅ | 60 s (fixed this pass) |
| No feature work in LVGL callbacks | ⚠️ | Flash writes, codec init and (2026-09-06) the codec **volume write** fixed - `bsp_extra_codec_volume_set` waits on the decoder's `_play_mtx`, which is held ~95 % of the time during playback; a slider drag on the LVGL task froze touch and redraws for the whole drag. All three volume entry points now go through `app_audio_set_volume_async()` (worker). Still on the LVGL task: `voice_gain_cb` → `voice_apply_mic_gain()` (blocking I2C write, `ui_settings.c:1153`), `tz_roller_cb` → `clock_sync_start()` (`:958`, mutex + `setenv`/`tzset`), and `tick_cb` → `clock_sync_localtime()` → `xSemaphoreTake(s_tz_mtx, portMAX_DELAY)` (`ui_clock.c:92` → `clock_sync.c:33`), a mutex also held by worker/alarm/httpd |
| Display lock with finite timeout | ✅ | 500/1000 ms everywhere; the single `lock(0)` is pre-task in `app_main.c:230` |
| LDO ch3 before DSI | ✅ | BSP |
| Backlight 0 → init → on after first frame | ⚠️ | `bsp_display_brightness_set(bright_max)` at `app_main.c:228` runs before `ui_clock_create()` and any flush. Also: 80 % cap is only the default; slider and `PATCH /settings` accept 100; BSP PWM is 5 kHz not the recommended 100 Hz |
| LVGL buffers in PSRAM, no DMA | ✅ | `app_main.c:217-226` |
| esp_hosted version logged **and mismatch warned** | ⚠️ | Logged at INFO only (`wifi_mgr.c:169-174`), never compared to the pinned `~2.12.0`; `feature_specs.c:157` hard-codes "2.12.12" — the exact stale-string trap the rule exists for |
| Hosted mempool/tasks from SPIRAM | ✅ | `sdkconfig.defaults` |
| No `ESP_ERROR_CHECK` on wifi init | ✅ | `wifi_mgr.c:136-167` |
| Bounded WiFi retries + background reconnect | ✅ | `wifi_mgr.c:19-20`, `:55-71` |
| Post-connect staging | ❌ | `boot_job` (`app_main.c:182-193`) runs codec → API → blocking WiFi → SNTP → alarm → ambient → voice with no yield; `got_ip_handler` starts httpd the instant GOT_IP fires |
| TLS stack ≥ 8192 | ✅ | worker 16 KB, main 8 KB, httpd 8 KB, `MBEDTLS_EXTERNAL_MEM_ALLOC=y` |
| One I2C driver | ✅ | `app_video.c:67-71` reuses `bsp_i2c_get_handle()` |
| 200 ms codec settle | ✅ | `app_main.c:100` |
| One sample rate / full-duplex caution | ⚠️ | 16 kHz pinned; full duplex is now relied on (`voice.c:15-16` claims hardware verification — record the evidence in docs) |
| Large buffers in PSRAM | ⚠️ | `static char s_log_buf[16384]` in internal `.bss` (`ui_settings.c:72`) — display-only data |
| Dedicated NVS namespace | ⚠️ | App config **and** crash counter both use `"bedside"` (`settings.c:13`, `app_main.c:109`) |
| Atomic config writes (two-slot + checksum) | ❌ | Single `nvs_set_blob` with magic+version, no checksum/second slot/generation counter, three concurrent writers (LVGL Save, ambient calibration, HTTP PATCH). A downgrade (larger stored blob) silently factory-resets (`settings.c:87`, `:127`) |
| Internal-RAM protection set, MEMTEST off, `CMAKE_OBJECT_PATH_MAX` | ✅ | `sdkconfig.defaults`, `CMakeLists.txt:3` |
| `dependencies.lock` committed + pins | ⚠️ | Lock present; `espressif/esp_wifi_remote: "*"` unpinned; `esp_lvgl_port ~2.9.0` deviates from the checklist's `~2.7.2` (deviation is intentional and commented — update the checklist) |
| OTA (§8) | N/A | factory-only `partitions.csv` |
| Bluetooth (§5) | N/A at app level | NimBLE **enabled in sdkconfig** with no BT code in `main/` — dead weight costing RAM/flash (see §6) |

---

## 4. HTTP control API (`api_server.c`)

**Well done:** single auth choke point before any parsing (`:840`); constant-time key compare
(`:264`); empty stored key = hard lockout (`:304`); off by default (`settings.c:66`); crash-loop
suppression at ≥3 abnormal resets (`:180`); start/stop only on the worker via an idempotent
reconcile with a 60 s self-healing timer (`:132-161`); 2 KB body cap into PSRAM (`:319-323`);
range/length-validated staging before any write (`:475-514`); one locked write batch (`:594-621`);
priority 3 on core 1, 2 sockets, LRU purge (`:99-103`). No reboot/OTA/factory-reset endpoint. No
unauthenticated path was found that can crash the device.

**Gaps, in order of importance:**

1. **Plaintext HTTP on port 80, bound to all interfaces** (`:98`). `PATCH /settings` accepts
   `wifi_pass`, `nav_pass`, `ai_key`, `api_key` (`:612-620`) and the response echoes settings
   including `wifi_ssid`/`nav_url`/`nav_user` (`:671`). Everything crosses the LAN in the clear.
   Mitigation options: document as LAN-only; bind to the STA address; or move to HTTPS with a
   self-signed cert (esp_https_server) and accept the RAM cost.
2. **No auth-failure rate limit or lockout**; each rejection does `getpeername` + `inet_ntop` +
   `ESP_LOGW` (`:218-237`). Add a failure counter with exponential delay.
3. `ESP_FAIL` used as a control-flow sentinel (`:709`, `:771`) alongside real `esp_err_t` from
   `send_err`; a genuine failure could emit a second response on the same request. Use a
   dedicated bool.
4. Cross-task flags `s_httpd` / `s_suppressed` not `volatile` (`:70-74`).
5. `handle_controls` silently ignores `brightness` when `volume` is also present (`:772-779`).
6. `send_err` interpolates `msg` into JSON without escaping (`:243`) — safe only because every
   caller passes a literal.

---

## 5. Logging quality

- **Level discipline.** `ui_log` is the only user-visible channel and always emits at INFO, so
  hard failures ("WiFi init FAILED", "Audio codec init FAILED", "API server start failed",
  "audio player init failed", "Ambient light: camera init failed") are indistinguishable from
  chatter under `ESP_LOG_LEVEL` filtering. Add `ui_log_warn`/`ui_log_err` (or a level
  parameter) that also picks `ESP_LOGW/E`.
- `ui_log_bulk` sends its serial copy through raw `printf` (`ui_settings.c:1465`): no tag,
  timestamp or level.
- Missing context: `wifi_mgr.c:83` drops `wifi_event_sta_disconnected_t.reason` (the one field
  that separates wrong-password from AP-not-found); every `ioctl` failure in `app_video.c`
  (`:97`, `:111`, `:129`, `:176`, `:187`, `:208`, `:233`, `:260`, `:273`, `:286`) logs no
  `errno` and `:92` no device path; `localai_client.c:65/:91/:102/:176` log no return
  value/byte counts/host; `app_main.c:130` logs the reset reason as a bare integer although
  `reset_reason_str()` exists in `feature_specs.c:23`.
- Worth keeping as patterns: the heap delta around voice bring-up (`voice.c:712-715`), the
  AFE/mic heartbeats (`voice.c:348-352`, `:528-531`), the ambient decision trace
  (`ambient.c:162-164`), and `settings.c:128` (logs both `err` and length).

---

## 6. Documentation drift

| Claim / location | Reality |
|---|---|
| `src/bedside_v01/specs.md` (since removed from `src/bedside_clock`) | **Byte-identical to `src/sample_hello_world/specs.md`** and describes the hello_world acclimatisation app (BLE listing, Toccata synth). bedside_v01 has no spec document anywhere. Highest-value doc fix: write one. |
| No `.md` mentions bedside_v01 | `docs/`, `README.md` "Our codebase", `docs/06` baseline all describe hello_world only. The HTTP API (`/api/v1/*`), song cache, alarm subsystem, ambient dimming, orientation setting and the 11 settings pages are documented only in C comments. |
| `README.md:22`, `:39`; `VOICE_AND_AI_PIPELINE.md:8` | Still say ESP-IDF 5.5.**4**; toolchain is 5.5.5 (`docs/02:7`, `idf_component.yml`). |
| `VOICE_AND_AI_PIPELINE.md:83` | Says settings **v5**; code is `SETTINGS_VERSION 6` (`settings.c:16`), v6 added `api_enabled`/`api_key`. |
| `VOICE_AND_AI_PIPELINE.md:263`, `:290` | Describe "download → play"; the music path now streams via `song_cache` with a 64 KB prebuffer. |
| `docs/02-toolchain-and-build.md:89`, `docs/05:180` | Prescribed `readcom.py` for serial capture but the script did not exist. **Fixed 2026-09-02**: added `tools/readcom.py` (now `src/bedside_clock/tools/readcom.py`), docs/02 now points at it. |
| `docs/02:79-82` | Documents only hello_world's partition table; bedside adds the 4 MB `model` SPIFFS at 0x820000 (`partitions.csv:8`). |
| `ui_settings.c:1-3` | Header lists 8 pages; 11 are built (`:1268-1278`). |
| `feature_specs.c:143-144` | Spec sheet still prints "no camera module fitted" — the OV02C10 is fitted and used for ambient dimming. |
| `VOICE_AND_AI_PIPELINE.md:205-208` | "nine open-page commands" is correct, but the AI and API pages have no voice phrase and no `builders[]` slot — unreachable by voice. |
| `tools/test_ai_matching.py:26-31`, `settings.c:19-21` | **Live Navidrome and LocalAI credentials in tracked source.** Move to a local, ignored config or env vars. |
| `main/idf_component.yml:9-10` | `usb_host_uac: "^1"` pinned "for the Jabra speaker" — zero code references. |
| `sdkconfig.defaults` "M5 Bluetooth attempt" block | NimBLE + hosted BT enabled; no BT code in `main/`. Costs internal RAM and flash. |
| `voice_actions.h:44` | `VA_SET_TIME` has no phrase and no binding (dead enum entry). |
| `tools/test_ai_matching.py:38` | Harness `TIMEOUT_S 120` vs device `AI_HTTP_TIMEOUT_MS 45000` — a query the device would abort at 45 s still passes the harness. |
| `tools/__pycache__/` | Build artefact in the tree. |

---

## 7. Hygiene

- **Duplicated helpers:** `ui_settings.c:78-92 sidebar_btn_enable` ≡ `ui_common.c:51-65 ui_sidebar_btn_enable`; `kb_event_cb` + `ta_focus_cb` verbatim in `ui_play.c:160-179` and `ui_settings.c:185-204`; `new_page` and the `COL_*` palette in both `ui_play.c` and `ui_settings.c` vs `UI_COL_*` in `ui_common.h`; `ui_play.c:52 label_set_if_changed` vs `ui_common.c:73`.
- **Dead code:** `ui_play_is_open()` (`ui_play.c:285`); `app_video_stream_task_restart/stop/wait_stop` and the event-group stop handshake (`app_video.c:30-31`, `:308-315`, `:343-366`) — the camera is never stopped.
- **Three-way enum-order coupling:** `voice_actions.c:310` routes on `cmd <= VA_STOP_MUSIC`, `:261` computes a page id as `cmd - VA_OPEN_VOLUME`, and `ui_settings.c:1387-1397 builders[]` must be in the same order; nothing enforces it. Add a `_Static_assert` on the counts and a table keyed by enum.
- **Long functions:** `api_server.c:516-683 handle_settings_patch` ≈ 168 lines; `voice.c:609-717` ≈ 108.
- **Magic numbers:** `ui_clock.c:59-60` (fixed-point 256 scale, `+10`); `ambient.c:59` luma weights `77/150/29` with shifts; four different sizes for the same 49-byte API key (`api_server.c:242`, `:266`, `:287`, `:290`).
- **`add_compile_options(-Wno-format -Wno-unused-variable)`** (`CMakeLists.txt:5`) disables the main compile-time check on a printf-heavy codebase where `size_t`/`time_t` widths already required hand casts (`app_main.c:131`, `feature_specs.c:96-121`, `voice.c:713`). Re-enable and fix the handful of warnings.
- `ui_settings.c:893-904`, `:966-972`: static roller-option caches built with unbounded `sprintf`; `lv_roller_set_options(..., LV_ROLLER_MODE_NORMAL)` copies the string, so the caches buy nothing.
- `ambient.c:98-112`: lock-free access to `luma_floor/ceil` is correct only because `ambient_reset_calibration()` is reachable solely from the Brightness page while the ambient task skips when settings are open. Document the invariant at both ends or take the settings lock.

---

## 8. Suggested order of attack

1. §6 first row: write a real `src/bedside_clock/specs.md` (and a `docs/` page for the HTTP API).
2. D1 (worker task creation), D5/D6 (settings lock omissions), D10 (`s_ip`).
3. §3: post-connect staging, esp_hosted version comparison, backlight-after-first-frame.
4. §4 items 1–2 (API transport and lockout) once the deployment model is decided.
5. §5 log levels — small change, large diagnostic payoff.
6. Remove the BT and `usb_host_uac` dead weight; move credentials out of source.

# Reliability checklist (MUST-IMPLEMENT)

Formerly `MUST-IMPLEMENT.md` at the repository root; moved to `docs/` in the 2026-10-02
restructure. Links point into the 4.3" board folder (`devices/guition-jc4880p443c/docs/`)
and the renamed projects (`src/sample_hello_world`, `src/bedside_clock`). The older names
`hello_world` and `bedside_v01` in the text below refer to the same code.

Every item here prevents a **specific crash, hang, brick, or instability** that either bit us
during bring-up or that another shipping codebase (EspControl in production, ESPHome core,
the vendor demos) learned the hard way. Any firmware written for these boards implements these
unless an item is explicitly waived with a reason.

Format per item: **MUST** (the rule) / *Why* (the failure it prevents) / *Proven by* (source).

---

## 1. Boot & crash resilience

- [ ] **MUST build for rev v1.x silicon**: `CONFIG_ESP32P4_SELECTS_REV_LESS_V3=y` +
  `CONFIG_ESP32P4_REV_MIN_100=y` (ESPHome: `engineering_sample: true`).
  *Why*: default rev-3 build refuses to flash — or worse, a forced image doesn't boot; the
  two silicon generations are binary-incompatible. *Proven by*: our flash refusal
  ([docs/05](../devices/guition-jc4880p443c/docs/05-troubleshooting.md#2-flash-refused-chip-revision)); ESPHome validates
  this exact trap.

- [ ] **MUST implement a boot-time crash-loop breaker**: on boot, if crash data / a panic
  reason is present → log it once, clear it, mark boot successful, flush NVS, then continue
  (or single clean reboot). Never let a stored crash report be replayed into logic that can
  re-trigger the crash.
  *Why*: turns one crash into a crash *loop*. *Proven by*: EspControl runs exactly this on
  our board (`device.yaml:83-104`).

- [ ] **MUST flush NVS before any intentional reboot** (`nvs_commit()` / preferences sync,
  then restart).
  *Why*: batched writes are silently lost across `esp_restart()` — settings "randomly" revert.
  *Proven by*: EspControl syncs before every reboot path and documents why.

- [ ] **MUST use the NVS init erase-retry pattern**: on `ESP_ERR_NVS_NO_FREE_PAGES` /
  `NEW_VERSION_FOUND` → erase → retry init.
  *Why*: firmware updates that change NVS layout otherwise boot-fail forever. *Proven by*:
  vendor demo, ESPHome, our app_main.c.

- [ ] **MUST count boot attempts / support a safe mode** for any firmware with OTA or config
  that can wedge the UI: N failed boots → minimal recovery state (WiFi + OTA only).
  *Why*: a bad config or asset must never require USB access to a deployed device.
  *Proven by*: ESPHome `safe_mode` component; EspControl ships dedicated `.recovery` builds.

## 2. Watchdogs & long operations

- [ ] **MUST size the task watchdog for the slowest legitimate operation** — 60 s during OTA
  (partition erase blocks the loop task) and around C6 transport bring-up; restore afterwards.
  *Why*: WDT reset mid-OTA-erase = corrupted app slot; WDT reset during C6 connect = boot loop
  on slow panels. *Proven by*: EspControl sets `CONFIG_ESP_TASK_WDT_TIMEOUT_S=60` for our
  board; ESPHome's `ble.cpp` raises WDT to 60 s specifically around
  `esp_hosted_connect_to_slave()`.

- [ ] **MUST NOT do feature work in UI callbacks**: LVGL event handlers only post to a queue;
  a worker task executes. All `lv_*` calls from other tasks go through the display lock
  **with a finite timeout** (we use 1000 ms) and a skip-on-failure path — never lock(0)
  blocking forever from a worker.
  *Why*: blocked LVGL task = frozen touch + missed watchdog feeds; deadlock if two lockers
  wait forever. *Proven by*: our architecture (works); the archived community BLE sample
  violates it (100 ms stalls in callbacks) — don't copy that.

## 3. Display & backlight

- [ ] **MUST enable LDO channel 3 @ 2500 mV before any DSI/panel init** (the vendored BSP does
  this; any hand-rolled init must too).
  *Why*: the #1 "board looks bricked" cause — permanent black screen. *Proven by*: vendor
  blueprint triage table; ESPHome hard-depends `mipi_dsi` on `esp_ldo`.

- [ ] **MUST init backlight at 0% and turn it on only after the panel is initialized** (and
  first frame ready).
  *Why*: white-flash/garbage frame at boot; drawing before DPI is stable. *Proven by*: vendor
  init flow (backlight duty 0 → init → on).

- [ ] **MUST blank/dim the backlight during OTA flash writes**, restore after.
  *Why*: cuts peak power draw during flash-busy periods on a ≥600 mA-hungry board and avoids
  rendering from a starved flash cache. *Proven by*: EspControl `firmware_update.yaml`
  ("reduces power and avoids drawing while flash is busy").

- [ ] **SHOULD cap backlight at ~80% duty, 100 Hz PWM** unless full brightness is required.
  *Why*: field-proven thermal/longevity values for this exact panel; the duty→luminance curve
  above 80% is uncharacterized. *Proven by*: EspControl production config (`max_power: 0.8`,
  100 Hz).

- [ ] **MUST keep LVGL buffers in PSRAM** on this board (`buff_spiram`, no DMA flag), per the
  proven display config.
  *Why*: internal RAM is needed for WiFi/BT/audio; internal-RAM framebuffers starve them.
  *Proven by*: factory demo config; our firmware.

## 4. C6 co-processor link (esp_hosted over SDIO)

- [ ] **MUST match the host esp_hosted component to the C6 slave firmware version**
  (currently slave = 2.12.12 → host `~2.12.0`), and **MUST log
  `esp_hosted_get_coprocessor_fwversion()` at runtime** and surface a mismatch warning.
  *Why*: WiFi tolerates a mismatch; **Bluetooth silently times out on every HCI command**.
  *Proven by*: our two days → [docs/04](../devices/guition-jc4880p443c/docs/04-bluetooth.md).

- [ ] **MUST treat 40 MHz SDIO as provisional: on any C6 link unreliability, drop
  `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ` to 20000 first.**
  *Why*: 40 MHz works on our unit; EspControl ships 20 MHz "for reliable communication
  **across production panels**" — unit-to-unit variance is real. *Proven by*: EspControl
  production choice on this exact board.

- [ ] **MUST reset the C6 on every host boot** (reset GPIO54 active-high,
  `SLAVE_RESET_ON_EVERY_HOST_BOOTUP`) and tolerate the ~1.5-2 s link bring-up.
  *Why*: stale slave state after host-only resets causes undefined transport behavior.
  *Proven by*: every working config (vendor, EspControl, ESPHome, ours).

- [ ] **MUST configure hosted memory for P4+display builds**: either
  `CONFIG_ESP_HOSTED_USE_MEMPOOL=n` or mempool-prefer-SPIRAM, plus
  `CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM=y`.
  *Why*: boot-time `HS_MP: mempool create failed: no mem` / `sdio_mempool_create` assert when
  a large LVGL UI has eaten internal RAM. *Proven by*: EspControl comment for our board;
  ESPHome's `use_psram` comment names "P4 + large LVGL UI" explicitly.

- [ ] **MUST NOT assume the C6 is present**: `esp_wifi_init()` (and BLE bring-up) failure is a
  runtime condition with a user-visible message, never `ESP_ERROR_CHECK` → abort.
  *Why*: a dead/mismatched C6 must degrade to "wireless unavailable", not a boot loop.
  *Proven by*: brookesia `Setting.cpp` defensive pattern; ESPHome's explicit error strings.

- [ ] **SHOULD keep a sha256-pinned known-good C6 firmware + the OTA-over-SDIO update path**
  (`esp_hosted_slave_ota_*`; official images at esphome.github.io/esp-hosted-firmware).
  *Why*: wire-free recovery from a bricked/mismatched C6; the JP1 UART fallback needs
  soldering. *Proven by*: EspControl `c6_recovery` component; ESPHome `esp32_hosted/update`.

## 5. Bluetooth (BLE via C6 vHCI)

- [ ] **MUST follow the exact bring-up order**:
  `esp_hosted_connect_to_slave()` → (log fw version) → `esp_hosted_bt_controller_init()` →
  `esp_hosted_bt_controller_enable()` → short settle → start host stack (NimBLE/Bluedroid).
  *Why*: since esp_hosted 2.5.2 the C6's BT controller is **disabled by default**; any other
  order = endless `BLE_HS_ETIMEOUT_HCI`. *Proven by*: our fix
  ([docs/04](../devices/guition-jc4880p443c/docs/04-bluetooth.md)); ESPHome `ble.cpp` implements the identical sequence.

- [ ] **MUST bound BLE startup with a sync timeout (~5 s) and degrade gracefully** with an
  on-screen/logged explanation naming the likely cause (C6 firmware lacks BT / version
  mismatch) — and compile-gate BT so the rest of the firmware ships without it.
  *Why*: BT availability depends on C6 firmware the user may change; the product must not
  hang or crash on it. *Proven by*: our feature_bt.c; ESPHome's user-facing error messages.

- [ ] **SHOULD move the BT host stack to PSRAM** when combined with the display
  (NimBLE/Bluedroid allocs; ESPHome's `use_psram` saves ~40 KB internal).
  *Why*: same internal-RAM pressure as §4. *Proven by*: ESPHome option + comment.

## 6. WiFi & network behavior

- [ ] **MUST bound every connect attempt** (finite retries + timeout, e.g. 3×/20 s), then move
  to fallback (next SSID / AP mode / clear failure state). Never infinite silent retry.
  *Proven by*: our feature_wifi.c; ESPHome wifi behavior.

- [ ] **MUST debounce fallback-AP activation** (~90 s of failure before opening the hotspot).
  *Why*: a router reboot otherwise flips deployed panels into setup mode. *Proven by*:
  EspControl `ap_timeout: 90s` with comment.

- [ ] **MUST stage post-connect work**: after (re)connect or API client attach, yield ~1 s
  before heavy fetches; spread refreshes (their pattern: 2/8/20/25 s).
  *Why*: an immediate burst starves the connection handshake and drops it again — reconnect
  storms. *Proven by*: EspControl `on_client_connected` staging.

- [ ] **MUST size TLS for the P4** when any HTTPS is used: main/task stack ≥8192 for the
  calling task, `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`.
  *Why*: mbedTLS blows the default stack on P4 → overflow crash on first HTTPS request.
  *Proven by*: EspControl sdkconfig comment for our board.

- [ ] **MUST use software AES (`CONFIG_MBEDTLS_HARDWARE_AES=n`) while mbedTLS buffers live in
  PSRAM**, and re-test hardware AES only after an IDF upgrade, with ≥25 MB of sustained HTTPS
  download and zero `-0x7180` errors as the pass criterion.
  *Why*: on the P4 the hardware AES-DMA path decrypting a PSRAM-resident record buffer
  returns wrong plaintext under load → `SSL - Verification of the message MAC failed` every
  few hundred KB; TCP checksums pass, so nothing below TLS notices. IDF v5.5.5 already has
  Espressif's Jan-2026 cache fixes and still fails. *Proven by*: hardware bisection 2026-09-02,
  [docs/11](../devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md).

- [ ] **MUST log the read-once HTTP/TLS error details at every stream/read failure**
  (`esp_http_client_get_errno`, `esp_http_client_get_and_clear_last_tls_error` +
  `mbedtls_strerror`, `esp_http_client_is_complete_data_received`, bytes, elapsed, attempt),
  once per failure, to the user-visible log; **MUST check `esp_http_client_fetch_headers`'s
  return** (it can return `-ESP_ERR_HTTP_EAGAIN`); and **MUST treat `-ESP_ERR_HTTP_EAGAIN`
  as "server silent, connection valid"** (re-read), not as a hard error.
  *Why*: a bare "read error" cost a full investigation to attribute; the IDF client's own
  diagnostics are `ESP_LOGD` and compiled out at the default log level. *Proven by*:
  bedside_v01 `navidrome_client.c` (docs/10 §1, docs/11 §2).

- [ ] **MUST make long downloads resumable and bounded**: reconnect with `Range: bytes=N-`
  (accept 206; on 200 restart only if nothing downstream consumed bytes yet), a retry count
  AND a wall-clock budget, backoff that polls the abort flag, and — on reused
  `esp_http_client` handles — `esp_http_client_clear_response_buffer()` and
  `esp_http_client_delete_header(c, "Range")` before every open (both persist across
  requests). *Proven by*: bedside_v01 `nav_stream_song`, live-verified 206 resumes.

## 7. Audio (ES8311)

- [ ] **MUST use exactly one I2C master driver** on GPIO7/8 shared by touch (0x5D), codec
  (0x18), camera (0x36).
  *Why*: a second driver on the same pins is a documented failure mode (bus contention,
  intermittent NACKs). *Proven by*: vendor docs; BSP design.

- [ ] **MUST allow a settle delay (~200 ms) after codec init** before the first fs/volume
  calls.
  *Why*: early register writes intermittently no-op → "silent codec" mysteries. *Proven by*:
  vendor demo audio.c comment + our working firmware.

- [ ] **MUST pin one sample rate per app** (we use 16 kHz/16-bit mono) and **treat full-duplex
  I2S as unproven** — record fully, then play, until simultaneous rec+play is explicitly
  validated on hardware.
  *Proven by*: vendor doc lists simultaneous play+record as a regression test to earn, not
  assume; conflicting rates across vendor examples.

- [ ] **SHOULD manage the PA (GPIO11) explicitly** — off when idle, on for playback.
  *Why*: amp idle hiss/pops and wasted power on an externally-amped speaker. *Proven by*:
  BSP exposes it for a reason; ESPHome models it as a switchable output.

- [ ] **MUST treat a codec sample-rate change as an allocation that can fail**: the I2S
  reconfigure re-allocates ~6 × 1 KB DMA descriptors per direction from DMA-capable internal
  RAM. Check `esp_codec_dev_open()`'s result (with the local esp_codec_dev patch that makes
  it fail instead of enabling a descriptor-less channel), log internal/DMA free + largest
  block before each reconfigure, and keep ≥ ~12 KB of DMA-capable internal RAM free at song
  start (measure via `/api/v1/status` `dma_largest`).
  *Why*: with ~40 KB internal free the allocation failed and upstream `esp_codec_dev_open`
  enabled the channel anyway → NULL load in `i2s_rx_channel_start` → reboot on every play.
  *Proven by*: bedside_v01 2026-09-02 (docs/05 §10, docs/11 §8).

## 8. OTA & release hygiene

- [ ] **MUST show OTA progress and persist an "update complete" flag** rendered on the *next*
  boot (the flashing boot can't reliably render it).
  *Proven by*: EspControl `firmware_update_complete_pending` pattern.

- [ ] **MUST never mix factory merged-image offsets (0x0) with the dev partition layout**
  (bootloader 0x2000 / table 0x8000 / app 0x20000).
  *Why*: half-overwritten layouts produce boards that look bricked. *Proven by*: vendor
  flashing doc warning.

- [ ] **MUST verify release binaries contain the intended version string** (and no
  `dev`/`0.0.0` placeholder) — a strings-scan of the .bin in the release script.
  *Proven by*: EspControl `assert_binary_version()`.

- [ ] **MUST commit `dependencies.lock`** after a known-good build and pin exact component
  versions (`esp_hosted ~2.12.0`, `esp_lvgl_port ~2.7.2`, `lvgl 9.5.0`).
  *Why*: registry drift broke our build once already (esp_lvgl_port 2.9.0 vs IDF 5.5.4).
  *Proven by*: [docs/05 §1](../devices/guition-jc4880p443c/docs/05-troubleshooting.md).

## 9. Configuration & persistence

- [ ] **MUST write config atomically**: two-slot store — write payload first, publish a
  checksummed header last; keep a monotonic generation counter if multiple writers (browser +
  device) exist.
  *Why*: power loss mid-write otherwise corrupts config → boot loop or factory reset.
  *Proven by*: EspControl `configuration_store.*` (tested, portable C++).

- [ ] **MUST use a dedicated NVS namespace** for app config (not shared with framework/BSP
  keys).
  *Why*: collision-free migrations; no partition-layout changes needed. *Proven by*:
  EspControl `"espcontrol_cfg"` rationale.

## 10. Memory & sdkconfig baseline (P4-specific)

- [ ] **MUST ship the internal-RAM protection set**:
  `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096`, `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536`,
  `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y`.
  *Why*: without them, WiFi/BT/DMA allocations fail unpredictably once LVGL+audio load PSRAM
  and heap pressure rises. *Proven by*: xiaozhi/EspControl production sdkconfigs.

- [ ] **MUST put large, non-DMA buffers in PSRAM explicitly**
  (`heap_caps_malloc(..., MALLOC_CAP_SPIRAM)`) — audio recordings, images, framebuffers.
  *Proven by*: our echo feature; every demo.

- [ ] **MUST expose internal-RAM headroom at runtime** — free + largest block for
  `MALLOC_CAP_INTERNAL` and `MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA` — in the status/diagnostics
  channel, and log it before every runtime DMA (re)allocation. **SHOULD set
  `CONFIG_LWIP_L2_TO_L3_COPY=y`** with esp_hosted WiFi.
  *Why*: a full UI + voice + HTTP server + streaming leaves this board at ~80 KB internal
  free at idle and ~40 KB while streaming; failures are DMA allocations, which only the
  `|DMA` figure predicts. The L2→L3 copy released hosted RX buffers early and raised the
  streaming figure to ~76 KB. *Proven by*: bedside_v01 measurements 2026-09-02 (docs/11).

- [ ] **SHOULD keep `CONFIG_SPIRAM_MEMTEST=n`** for production boot speed (=y when chasing
  memory corruption).

- [ ] **Windows builds: MUST keep `set(CMAKE_OBJECT_PATH_MAX 512)`** in the top CMakeLists.
  *Why*: long-path object failures deep in managed components. *Proven by*: vendor demo +
  our build.

---

## Per-project sign-off

When starting a new firmware for this board, copy this table into the project README and mark
each section: **Implemented / Waived (reason) / N-A**.

| Section | Status |
|---|---|
| 1 Boot & crash resilience | |
| 2 Watchdogs & long ops | |
| 3 Display & backlight | |
| 4 C6 link | |
| 5 Bluetooth | |
| 6 WiFi/network | |
| 7 Audio | |
| 8 OTA & release | |
| 9 Config persistence | |
| 10 Memory & sdkconfig | |

Current status of `src/sample_hello_world` (updated 2026-08-30, hardware-verified):
- §1 **Implemented** — crash-loop breaker (NVS consecutive-crash counter, abnormal-reset
  detection, on-screen banner, cleared after 30 s stable uptime); rev flag; NVS erase-retry.
  Safe mode waived: no OTA/persisted config to recover from yet.
- §2 **Implemented** — queue/worker, finite display-lock timeouts. OTA WDT N-A.
- §3 **Implemented** — LDO ch3 via BSP; backlight on only after panel init, capped at 80%;
  PSRAM LVGL buffers. OTA blanking N-A.
- §4 **Implemented** — version-matched host (~2.12.0), C6 fw version logged in both WiFi and
  BT paths, mempool-prefer-SPIRAM + hosted tasks from SPIRAM, no ESP_ERROR_CHECK on wifi
  init, reset-on-boot. 40 MHz retained with the 20 MHz fallback documented.
- §5 **Implemented** — full bring-up order, 5 s sync timeout + graceful message, compile
  gate, NimBLE allocations in PSRAM.
- §6 **Implemented** — bounded retries + fallback SSID; TLS sizing (`MBEDTLS_EXTERNAL_MEM_ALLOC`,
  8 KB main stack). AP fallback / reconnect staging waived: no provisioning UI or HA client yet.
- §7 **Implemented** — one I2C driver, 200 ms codec settle, one sample rate, sequential
  record-then-play. Explicit PA idle management deferred to BSP.
- §8 N-A (no OTA, not a git repo — `dependencies.lock` exists and is preserved).
- §9 N-A (no persisted app config; crash counter uses its own NVS namespace per the rule).
- §10 **Implemented** — ALWAYSINTERNAL 4096, RESERVE_INTERNAL 65536, TRY_ALLOCATE_WIFI_LWIP,
  PSRAM for large buffers, MEMTEST off, CMAKE_OBJECT_PATH_MAX 512.

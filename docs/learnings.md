# Learnings

Hard-won lessons from bringing up two Guition ESP32-P4 boards and shipping an alarm clock on
one of them. Each entry has the lesson, the story in a few sentences, and where the evidence
lives. They are ordered roughly by how early they bite.

1. **Build for the silicon you actually have; P4 rev v1.x and rev v3.x are binary-incompatible.**
   The first flash was refused with `requires chip revision in range [v3.1 - v3.99] (this chip
   is revision v1.3)`. IDF 5.5 defaults to the newer silicon, and setting `REV_MIN_100` alone
   does nothing because the choice is hidden behind `SELECTS_REV_LESS_V3`. Other toolchains
   fail later and worse (an "Illegal instruction" in the bootloader).
   Evidence: [05 section 2](../devices/guition-jc4880p443c/docs/05-troubleshooting.md), every `sdkconfig.defaults`.

2. **Reuse the vendor-patched BSP verbatim before writing a single driver line.**
   The stock Espressif `esp32_p4_function_ev_board` BSP targets a 1024x600 EK79007 panel. The
   copy inside the factory demo is patched for the 480x800 ST7701, the right pins and the LDO
   sequence, and even re-uses the Kconfig name `CONFIG_BSP_LCD_TYPE_1024_600` to mean this
   panel. Copying it, with its sibling `esp_lcd_st7701`, turned a multi-day display bring-up
   into an afternoon. Do not edit its manifest: its unused dependencies must still resolve.
   Evidence: [03-working-components.md](../devices/guition-jc4880p443c/docs/03-working-components.md).

3. **Two on-chip LDO channels are prerequisites, not options: ch3 for the DSI PHY, ch4 for the SD card.**
   A permanently black screen with a healthy serial log is almost always a missing LDO ch3 at
   2500 mV. The SD slot has no GPIO power switch at all; `TF_VCC` is LDO ch4 through an
   always-on FET, so without it `send_op_cond returned 0x107`. The 10.1" factory firmware asks
   the LDO for 0 V and fails exactly that way. And an SD driver that grabs its own channel
   collides with the display's.
   Evidence: [01-hardware.md](../devices/guition-jc4880p443c/docs/01-hardware.md), the [10.1" factory log](../devices/guition-jc8012p4a1c/logs/factory-boot-2026-10-02.txt).

4. **The esp_hosted host component and the C6 slave firmware are version coupled; WiFi hides it, BLE does not.**
   The vendor manifests pin host 2.11 while the C6 runs 2.12.12. WiFi connected with a
   warning, so nobody looked, and Bluetooth timed out on every HCI command for two days. Pin
   the host to the slave (`~2.12.0`), log `esp_hosted_get_coprocessor_fwversion()` at
   runtime, and read the `transport:` handshake block before debugging anything wireless.
   The 10.1" ships slave 2.3.0 and loops on association until updated.
   Evidence: [04-bluetooth.md](../devices/guition-jc4880p443c/docs/04-bluetooth.md), [howto/update-c6-firmware.md](howto/update-c6-firmware.md).

5. **Since esp_hosted 2.5.2 the C6's Bluetooth controller starts disabled; the host must switch it on, in order.**
   This was the third of three stacked causes behind the same `BLE_HS_ETIMEOUT_HCI` symptom,
   and it is documented only inside the component's own `docs/bluetooth_design.md`. The
   sequence is connect to slave, controller init, controller enable, settle, then the host
   stack. ESPHome's `ble.cpp` does exactly this, which was the independent confirmation.
   Evidence: [04-bluetooth.md](../devices/guition-jc4880p443c/docs/04-bluetooth.md), [reliability-checklist.md](reliability-checklist.md) section 5.

6. **Pin component versions and commit `dependencies.lock`; the registry will drift under you.**
   One day the registry resolved `esp_lvgl_port` 2.9.0, which needs the `on_frame_buf_complete`
   DPI callback that only exists from IDF 5.5.5, and the 5.5.4 build broke with no source
   change. The fix was first a pin, then a coordinated IDF upgrade. The manifests now require
   `idf: ">=5.5.5"` so a stale toolchain fails at the dependency step instead of deep in a
   compile.
   Evidence: [05 section 1](../devices/guition-jc4880p443c/docs/05-troubleshooting.md), `src/*/main/idf_component.yml`.

7. **Hardware AES decrypting a TLS record buffer in PSRAM corrupts data under load; use software AES.**
   Songs died every 0.15 to 0.6 MB with `SSL - Verification of the message MAC failed` while
   lwIP's TCP checksums had accepted every segment, which localised the fault to decryption.
   Four builds, one Kconfig each, with a scripted API-driven load and a 100 s serial capture
   separated the AES-DMA cache-coherency bug from three plausible transport theories. Only a
   zero counts; theories that "explain everything" each changed the rate, not the outcome.
   IDF v5.5.5 already contains Espressif's cache fixes and still fails.
   Evidence: [11-tls-stream-corruption-investigation.md](../devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md), the comment block in `src/bedside_clock/sdkconfig.defaults`.

8. **The PPA hardware rotate can wait forever on rev v1.3; never take a DMA engine's completion on faith.**
   The screen froze with music still playing. Touch was dead because it is polled by the same
   LVGL task, which a JTAG backtrace showed blocked in `ppa_do_scale_rotate_mirror` with
   `portMAX_DELAY`, waiting for an interrupt that never came while PSRAM was busy with MP3,
   TLS and the song cache. The local `esp_lvgl_port` patch bounds the wait and falls back to
   CPU rotation for the rest of the boot; `ui_watch` aborts to a core dump after 30 s so that
   a dead clock at least reboots into a working one. Same family as item 7: DMA plus PSRAM
   under load is suspect on this silicon.
   Evidence: `LOCAL PATCH` hunks in `managed_components/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_disp.c`, `src/bedside_clock/main/ui_watch.c`.

9. **Touch is polled by the LVGL task; a blocked LVGL task means no touch, so LVGL callbacks never do feature work.**
   The GT911 interrupt line is not wired, so every input depends on the LVGL task staying
   responsive. Callbacks post to a queue; one worker executes; `lv_*` calls from other tasks
   go through the display lock with a finite timeout and a skip path. The archived community
   BLE sample stalled 100 ms inside callbacks and is the counter-example.
   Evidence: [reliability-checklist.md](reliability-checklist.md) section 2, [03-working-components.md](../devices/guition-jc4880p443c/docs/03-working-components.md) "Architecture pattern".

10. **Tear avoidance and software rotation are mutually exclusive in esp_lvgl_port; decide per app.**
    The sample runs `AVOID_TEAR` plus `DIRECT_MODE` because its camera preview tears
    otherwise, and that configuration owns both DSI framebuffers. The clock needs landscape on
    a portrait panel, which only software rotation provides, so it runs without tear
    avoidance and accepts it (there is no video on a clock). The camera preview itself must
    then draw through an LVGL canvas under the display lock, not to the panel.
    Evidence: comments in both `sdkconfig.defaults`, [09-camera.md](../devices/guition-jc4880p443c/docs/09-camera.md).

11. **Internal RAM is the scarce resource; budget it, expose it, and treat DMA allocations as fallible.**
    A full UI plus voice plus HTTP server plus streaming leaves 80 to 115 KB internal free at
    idle and about 40 KB while streaming. A song's sample-rate change re-allocates I2S DMA
    descriptors from that pool; when it failed, upstream `esp_codec_dev_open()` enabled a
    channel with a NULL descriptor list and the clock rebooted at every song start. The fix is
    layered: the `esp_codec_dev` patch, `CONFIG_LWIP_L2_TO_L3_COPY=y` (plus 35 KB during
    playback), PSRAM for every large buffer and for mbedTLS, and `dma_largest` in
    `/api/v1/status` so the next failure is predicted rather than discovered.
    Evidence: [11 section 8](../devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md), [reliability-checklist.md](reliability-checklist.md) section 10.

12. **Size the task watchdog for the slowest legitimate operation, and make it panic.**
    The IDF default of 5 s can fire while blocking esp_hosted RPCs bring up the C6 or during
    an OTA erase; EspControl and ESPHome both raise it to 60 s around exactly those paths.
    With `CONFIG_ESP_TASK_WDT_PANIC=y` a trip becomes a counted `ESP_RST_TASK_WDT` reboot with
    a core dump instead of a log line nobody reads. One stored dump showed a Task WDT on the
    alarm task during a long download, which is why that download now runs at a lower priority.
    Evidence: `src/bedside_clock/sdkconfig.defaults` comments, [reliability-checklist.md](reliability-checklist.md) section 2.

13. **Log the read-once HTTP and TLS error details at the failure site; a bare "read error" costs an investigation.**
    `esp_http_client_get_errno()` and `esp_http_client_get_and_clear_last_tls_error()` clear on
    read, and the IDF client's own diagnostics are `ESP_LOGD`, compiled out at the default
    level. Logging them once per failure turned "stream read error" into "MAC failed, errno
    0", which excluded the network in one line. Treat `-ESP_ERR_HTTP_EAGAIN` as "server
    silent, connection valid", check the return of `esp_http_client_fetch_headers`, and clear
    the `Range` header and the response buffer before reusing a handle.
    Evidence: [10-bedside-v01-review.md](../devices/guition-jc4880p443c/docs/10-bedside-v01-review.md) section 1, `src/bedside_clock/main/navidrome_client.c`.

14. **Stored server ids go stale; resolve by name on "not found" and journal every fallback.**
    The alarm beeped instead of playing because Navidrome had re-created the playlist under a
    new id (Subsonic error 70) and the clock kept asking for the old one. The fix distinguishes
    "the id is stale" (`nav_last_api_error() == 70`, re-resolve by name once) from "the server
    is unreachable", returns a short `why` from the download, and writes each fallback reason to
    the event journal so the next 06:30 failure explains itself.
    Evidence: `src/bedside_clock/main/alarm.c` (sound selection and `A%d dl fail` journal lines), `navidrome_client.c`.

15. **A persistent event journal answers "why didn't the alarm ring" without a serial cable.**
    A noinit ring in internal RAM survives every reset except power-on at zero flash cost, and
    an NVS mirror written about 2 s after each append survives power cuts. Forty one-line
    events (boots with reason, time sync, WiFi edges, every alarm decision, UI stalls) are
    readable on the Specs page and over `GET /api/v1/events`. It also fixed a real bug: an
    alarm that fired, then rebooted inside its catch-up window, used to fire again.
    Evidence: `src/bedside_clock/main/evlog.h`, [howto/serial-capture-and-logs.md](howto/serial-capture-and-logs.md).

16. **Version the settings blob, append-only, with explicit migrations and a legacy mirror for downgrades.**
    The clock's settings struct is at version 11. Each bump appends fields and adds one
    `if (stored.version < N)` block that seeds sensible values (not zeros: version 3 had to
    seed "uncalibrated" markers because a zero floor read as "calibrated from pitch black").
    Out-of-range values are repaired and counted, and the pre-v9 single alarm is kept as a
    mirror of Alarm 1 so an older firmware still finds it.
    Evidence: `src/bedside_clock/main/settings.h` and the migration chain in `settings.c`.

17. **Evaluate a wake-word model on device audio, as a stream; training-set metrics lie.**
    The first model reported 6.5 percent false rejects and zero false accepts on its own
    splits and fired on every file join in practice. Tightly cut, peak-normalised utterances
    had taught it "a step in the noise floor means wake". The cure was cutting with real room
    audio around each utterance, keeping levels, and a stream evaluator that mimics the device.
    Evidence: [tools/wakeword_train/README.md](../src/bedside_clock/tools/wakeword_train/README.md) lessons 1 and 2.

18. **Negatives recorded through the same microphone in the same room are what remove false accepts.**
    Adding 24 minutes of TV through the device mic took false accepts from 116 per hour to 0
    at the shipped cutoff. One speaker through one channel also overfits fast: let synthetic
    multi-speaker positives carry the phrase, keep device positives at low weight, and feed
    full-length negatives (the augmenter silently keeps only the last 3.2 s of a clip
    otherwise). Each of these cost one 45-minute training run to learn.
    Evidence: [tools/wakeword_train/README.md](../src/bedside_clock/tools/wakeword_train/README.md) lessons 3 to 7, [main/models/README.md](../src/bedside_clock/main/models/README.md).

19. **USB host works only on the high-speed socket, and `peripheral_map` is not a bitmask of sockets.**
    The socket that gives COM3 belongs to USB-Serial/JTAG; a speaker plugged in there is
    invisible to the host. Setting `peripheral_map = BIT0|BIT1` to "cover both" moved the host
    onto the full-speed controller whose pins reach neither socket, while serial kept working,
    which was the tell. The lesson behind the lesson: with a working reference in the repo
    (the sample's USB audio feature), diff against it before theorising about hardware.
    Evidence: [01-hardware.md](../devices/guition-jc4880p443c/docs/01-hardware.md) USB bullet, `src/bedside_clock/main/usb_audio.c` header comment.

20. **Non-ASCII titles need a font fallback chain and UTF-8-safe truncation, not a bigger font.**
    LVGL's built-in Montserrat covers ASCII. Cyrillic and accented song titles rendered as
    boxes until `app_fonts.c` chained generated `montserrat_ext_*` supplements behind the
    built-ins (plus 158 KB of flash). Two rules followed: labels that display server text use
    `&app_font_NN`, and byte-bounded copies are trimmed at a code-point boundary
    (`utf8_trim_tail`) so a cut title does not end in a broken sequence.
    Evidence: `src/bedside_clock/main/fonts/app_fonts.c`, `main/utf8_util.h`.

21. **Keep the camera stack at the vendor's version; the sensor driver is vendor-custom and the vendor's own defaults file is wrong.**
    Managed `esp_cam_sensor` 2.0.1 has no OV02C10 driver at all, and the driver does not exist
    upstream either, so upgrading `esp_video` drops the camera. The vendor demo's
    `sdkconfig.defaults` names a nonexistent mode symbol; Kconfig silently fell back to the
    1-lane FHD default, which is what their shipped `sdkconfig` actually contains. Trust the
    generated configuration, not the defaults file, and remember the ISP pipeline controller is
    functionally required (frames are dark and green without it).
    Evidence: [09-camera.md](../devices/guition-jc4880p443c/docs/09-camera.md), [03-working-components.md](../devices/guition-jc4880p443c/docs/03-working-components.md) "Deferred".

22. **Read the licence before copying, because the useful code is the encumbered code.**
    EspControl is PolyForm Noncommercial (patterns yes, code no). The 10.1" touch stack
    contains Silead's `gsl_point_id.c` under GPL-2.0-or-later plus a firmware blob with no
    licence at all, linked into the vendor's Apache and MIT demos. Several community mirrors
    carry no licence, which means no grant. Apache-2.0 section 4(b) requires a notice on
    every modified file, which covers all three local patches in managed components. The vendor
    package also ships a commercial hex editor that must not be republished.
    Evidence: [licensing.md](licensing.md), [vendor/INVENTORY.md](../devices/guition-jc8012p4a1c/vendor/INVENTORY.md), [07-espcontrol-findings.md](../devices/guition-jc4880p443c/docs/07-espcontrol-findings.md).

23. **Back up the factory image and capture its boot log before the first flash.**
    The 10.1" factory log answered the panel controller, touch controller, C6 slave version,
    camera presence and SD power questions in 190 lines, before any port work. The factory
    image is the only known-good state, the vendor's restore path needs their Windows tool,
    and neither is redistributed here.
    Evidence: [devices/guition-jc8012p4a1c/logs/](../devices/guition-jc8012p4a1c/logs/factory-boot-2026-10-02.txt), [getting-started.md](getting-started.md) section 5.

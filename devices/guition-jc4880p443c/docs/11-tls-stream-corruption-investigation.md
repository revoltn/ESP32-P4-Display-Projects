# Case study: "Navidrome: stream read error" → ESP32-P4 hardware-AES corruption

Date: 2026-09-02. App: `src/bedside_clock` (then `src/bedside_v01`). Board: Guition JC4880P443C (ESP32-P4 rev v1.3 +
ESP32-C6 over SDIO). Toolchain: ESP-IDF v5.5.5 (tag 2026-07-16). All measurements below were
taken on the actual device over COM3; nothing here is inferred from documentation alone.

**One-line summary:** every failed song was mbedTLS rejecting a TLS record
(`-0x7180 Verification of the message MAC failed`) after lwIP had accepted the same bytes with
a valid TCP checksum. The P4's hardware AES-DMA engine, decrypting mbedTLS's PSRAM-resident
record buffer under sustained load, produces wrong plaintext. Software AES
(`CONFIG_MBEDTLS_HARDWARE_AES=n`) eliminated it completely. Along the way a reboot-on-play
crash (I2S DMA allocation failure) was found and fixed.

---

## 1. Symptom as reported

A song was requested, the Device Specifications page showed one line,
`Navidrome: stream read error`, and nothing else happened. The Play screen stayed on
"Loading: <title>". The server was reachable from a PC.

## 2. Why the log was useless, and what changed

`nav_stream_song()` treated every negative return from `esp_http_client_read()` as fatal and
logged one fixed string. The IDF client actually distinguishes (verified in
`components/esp_http_client/esp_http_client.c`, v5.5.5):

| Return | Meaning | Right reaction |
|---|---|---|
| `-ESP_ERR_HTTP_EAGAIN` (-28679) | 15 s poll with zero bytes; connection and parser intact | read again |
| `ESP_FAIL` (-1) | transport/TLS error, or FIN with body incomplete | close, reconnect |
| `0` | body framing complete | done |

The client now (a) rides out one silent-server timeout, (b) reconnects with
`Range: bytes=<got>-` up to twice inside a 45 s budget, and (c) on any hard failure logs
**once** the classified cause plus `esp_http_client_get_errno()`,
`esp_http_client_get_and_clear_last_tls_error()` (rendered with `mbedtls_strerror`), body
completeness, Content-Length/chunked, bytes, elapsed ms, attempt, format, host (never the URL,
which carries credentials), WiFi RSSI and free internal heap. Both diagnostics functions are
read-once (they clear), so exactly one call per failure. Details of the retry design and its
IDF gotchas (the `Range` header persisting across requests on a reused handle; body bytes
cached during header fetch surviving `esp_http_client_close()`; `fetch_headers` also
returning EAGAIN) are in [10-bedside-v01-review.md](10-bedside-v01-review.md) §1.

## 3. What the first instrumented run showed

```
I ui_settings: Navidrome stream haPvsw2Yqzu0XbGQRC1335: TLS error after 144KB/171ms try1 fmt=raw rc=-1 errno=0 rssi=-68
W navidrome: stream haPvsw2Yqzu0XbGQRC1335: host=https://music.example.net tls_last=ESP_ERR_MBEDTLS_SSL_READ_FAILED
   tls_code=29056 (SSL - Verification of the message MAC failed) tls_flags=0x0 errno=0 (-) body_complete=0 chunked=0
   content_len=8576399 bytes=147456 elapsed=171ms free_internal=41255
I ui_settings: Navidrome: reconnecting for haPvsw2Yqzu0XbGQRC1335 at 144 KB in 1000 ms (retry 1/2)
I ui_settings: Navidrome: resumed haPvsw2Yqzu0XbGQRC1335 at 144 KB (HTTP 206)
I ui_settings: Navidrome stream haPvsw2Yqzu0XbGQRC1335: TLS error after 565KB/302ms try2 fmt=raw rc=-1 errno=0 rssi=-72
```

Three facts fall out of this:

1. The failure is a **TLS record MAC mismatch**, not a socket error (`errno=0`), not a
   timeout, not a server close.
2. It happens every 0.15–0.6 MB at the raw-file transfer rate (2–3 MB/s), i.e. every
   150–400 ms of full-speed streaming.
3. The new reconnect + `Range` resume path works: HTTP 206 resumes at exactly the byte count
   reached.

## 4. Eliminating the transport

lwIP verifies TCP checksums on receive in IDF (lwIP's own default `CHECKSUM_CHECK_TCP 1`;
IDF's `lwipopts.h` maps only the IP/UDP/ICMP Kconfigs, and a `CONFIG_LWIP_CHECKSUM_CHECK_TCP`
Kconfig does not exist). A corrupted segment from the C6→P4 link would therefore be dropped
and retransmitted, never delivered. Since mbedTLS received a full, checksum-valid record and
still failed the MAC, the data was intact on arrival and wrong at decryption time.

The esp_hosted SDIO layer has its own optional checksum (`CONFIG_ESP_HOSTED_SDIO_CHECKSUM`,
off); it was not needed once the transport was cleared.

## 5. Bisection on hardware

Random play via the HTTP API (`POST /api/v1/playback {"action":"play_random"}` plus
`next` every 22 s), 3–5 songs per run, serial captured for 100 s, one variable per build:

| # | Build | TLS MAC errors | Conclusion |
|---|---|---|---|
| 0 | baseline: SDIO 40 MHz, hardware AES, PPA on | every 0.15–0.6 MB | field bug reproduced |
| 1 | SDIO 20 MHz | 5 in ~25 MB | ~10× fewer, **not fixed** |
| 2 | + `CONFIG_LWIP_L2_TO_L3_COPY=y` (copy frames out of the hosted buffer on arrival) | 12 in ~30 MB | not the cause; **kept** (see §7) |
| 3 | + `CONFIG_LVGL_PORT_ENABLE_PPA=n` (software rotation, no PPA DMA on PSRAM) | 10 in ~25 MB | not the cause; reverted |
| 4 | + `CONFIG_MBEDTLS_HARDWARE_AES=n` (PPA back on) | **0 in 26 MB, 5 songs, every one on the first attempt** | **root cause** |

Ruled out along the way: camera frame buffers (128-byte aligned, whole-cache-line sizes),
the hosted SDIO stream buffer (128-byte aligned in PSRAM, 512-multiple DMA lengths, driver
does C2M/M2C for exactly the transfer length), hosted RX buffer lifetime (build 2), PPA
(build 3).

## 6. Root cause

With `CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y` (required here: internal RAM is ~80–115 KB free at
idle) mbedTLS's 16 KB input record buffer lives in PSRAM. TLS record decryption is done in
place by the AES-DMA port (`components/mbedtls/port/aes/dma/esp_aes_dma_core.c`), which on the
P4 must keep the L1/L2 caches coherent around the DMA. Under sustained load the decrypted
output is wrong often enough to fail one record in every few thousand.

Upstream state (checked 2026-09-02 on GitHub, `release/v5.5` history of that file):

| Date | Commit | Title |
|---|---|---|
| 2024-11-14 | `f62bb46` | Fix external memory corruption caused due to unaligned length cache sync |
| 2026-01-16 | `525ef3a` | Ensure cache coherency when DMA writes to cacheable PSRAM buffers |
| 2026-01-16 | `0742f3f` | Cache invalidate the output buffer before the AES-DMA operation |
| 2026-08-25 | `c4075d6` | fix ESP32-S2 Crypto DMA stall on PSRAM output with partial blocks |

Our v5.5.5 (2026-07-16) contains the first three and still fails; the fourth is S2-only. So
as of this writing there is **no upstream fix to adopt**. This is not a third-party library
that could be swapped (wolfSSL as an IDF component would use the same hardware AES port).

## 7. Decisions that stay in `sdkconfig.defaults`

| Setting | Why |
|---|---|
| `CONFIG_MBEDTLS_HARDWARE_AES=n` | The fix. Software AES gives ~0.6 MB/s TLS throughput on this core, ~40× what 128 kbps playback needs; raw-file downloads take proportionally longer (a 10 MB file ~17 s). `HARDWARE_GCM` follows automatically. Re-test with hardware AES after every IDF upgrade. |
| `CONFIG_ESP_HOSTED_SDIO_CLOCK_FREQ_KHZ=20000` | MUST-IMPLEMENT §4 default (formerly `MUST-IMPLEMENT.md`, now [docs/reliability-checklist.md](../../../docs/reliability-checklist.md); EspControl ships 20 MHz on this board). Not the cause, but it lowered the AES error rate ~10× (bus contention) and streaming throughput was the same at both clocks. |
| `CONFIG_LWIP_L2_TO_L3_COPY=y` | Releases esp_hosted RX buffers immediately. Measured free internal RAM during playback rose from ~41 KB to ~76 KB, which is the headroom the I2S reconfigure needs (§8). |
| `CONFIG_LVGL_PORT_ENABLE_PPA=y` | Unchanged; ruled out. |

## 8. Second finding: reboot at song start

First test run, before any config change:

```
I bsp_extra_board: rate = 48000bits_cfg = 16
E i2s_common: i2s_alloc_dma_desc(541): allocate DMA buffer failed
E i2s_std: i2s_std_set_slot(138): allocate memory for dma descriptor failed
E I2S_IF: Failed to reconfig STD slot, ret: 257
Guru Meditation Error: Core  0 panic'ed (Load access fault).
  i2s_rx_channel_start (i2s_common.c:152) <- i2s_channel_enable <- _i2s_drv_enable
  <- esp_codec_dev_open (esp_codec_dev.c:184) <- bsp_extra_codec_set_fs (bsp_board_extra.c:196) <- audio_task
```

Mechanism: the player asks for the song's sample rate; `bsp_extra_codec_set_fs` closes and
reopens both codec devices; the I2S driver frees its DMA descriptors and re-allocates six
~1 KB buffers per direction from **DMA-capable internal RAM**. With ~41 KB free (and a
fragmented largest block) the allocation failed. `esp_codec_dev_open()` in the managed
component ignores the `set_fmt` return and enables the channel anyway; `i2s_rx_channel_start`
then dereferences the NULL descriptor list.

Fixes:
- Local patch in `managed_components/espressif__esp_codec_dev/esp_codec_dev.c`:
  `esp_codec_dev_open` returns `ESP_CODEC_DEV_DRV_ERR` when `set_fmt` fails (marked
  `LOCAL PATCH`; keep it if the component is refreshed). The player then ends the song
  cleanly instead of rebooting.
- `bsp_extra_codec_set_fs` logs `internal free / largest, dma free / largest` before every
  reconfigure and prints `heap_caps_print_heap_info()` on failure.
- `GET /api/v1/status` reports `internal_largest`, `dma_free`, `dma_largest`.
- The L2→L3 copy (§7) added ~35 KB of headroom during playback.

Still open: internal RAM is the board's scarce resource (voice pipeline ~48 KB, API server
16 KB stack, `s_log_buf` 16 KB, LVGL, hosted). Idle free varies 82–117 KB between boots. See
[10-bedside-v01-review.md](10-bedside-v01-review.md) §3 for the candidates.

## 9. Behaviour verified live (same sessions)

| Path | Evidence |
|---|---|
| Reconnect + Range resume | `resumed … at 144 KB (HTTP 206)`, `… at 565 KB`, `… at 2144 KB`, then `streamed 8206070 bytes (raw, 3 attempts)` |
| Server cannot resume (live transcode) | `server cannot resume - restarting … from 0` accepted because no decoder had consumed bytes yet; later attempt got 206 from Navidrome's transcode cache |
| Silent server | a Navidrome transcode stalled: `timeout (server silent) after 1024KB/33146ms try2 fmt=mp3 rc=-28679` (2 × 15 s, as designed) |
| Raw file too big for the cache | `raw … too big for the cache (13550329 bytes) - trying transcode` → `streamed 9011927 bytes (transcode, 2 attempts)` |
| Song that cannot fit even transcoded (3-hour rain track) | `failed (ESP_ERR_NO_MEM)` → Play screen "Could not load: … - see Specifications log" |
| Mid-song death | `Stream of How Deep Is Your Love failed mid-song - see Specifications log` then next song |
| Give-up bound | `giving up on … after 3 attempts / 5894 ms (1145 KB received)` |

## 10. How to re-test (after an IDF upgrade, or on another unit)

1. Build with `CONFIG_MBEDTLS_HARDWARE_AES=y` temporarily.
2. Start a serial capture (`tools/readcom.py COM3 100` from the IDF Python env), then:
   ```powershell
   $h = @{ "X-Api-Key" = "<key>"; "Content-Type" = "application/json" }
   Invoke-RestMethod -Uri http://<ip>/api/v1/playback -Method Post -Headers $h -Body '{"action":"play_random"}'
   # every ~20 s: -Body '{"action":"next"}'
   ```
3. Count `0x7180` in the capture. Baseline expectation with hardware AES on v5.5.5: several
   per 25 MB. Pass criterion: zero across ≥ 25 MB of raw streaming (5 songs).
4. Watch `bsp_extra_board: rate = … | internal free … dma free … (largest …)` at each song
   start; `dma largest` below ~12 KB means the I2S reconfigure is one bad boot away from
   failing.

## 11. Suggested upstream report (ESP-IDF)

Title: *ESP32-P4: hardware AES-DMA decrypt corrupts TLS records in PSRAM-resident mbedTLS
buffers under sustained load (v5.5.5, includes 525ef3a/0742f3f)*. Include: chip rev v1.3,
`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y`, `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`, L2 cache 256 KB /
128 B lines, esp_hosted 2.12.12 SDIO WiFi, the log excerpt in §3, the bisection table in §5,
and the observation that the error rate scales with SDIO clock (bus contention). Reproduction
needs only an HTTPS download of a few MB at ≥ 2 MB/s with `esp_http_client`.

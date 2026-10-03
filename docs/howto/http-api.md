# The bedside_clock HTTP API

A small JSON API served by the clock itself (`esp_http_server`, port 80, plain HTTP) for
controlling playback, alarms and settings from a PC, a phone or a script, and for pulling
diagnostics without a serial cable. Everything below is read from
`src/bedside_clock/main/api_server.c` and the embedded page `main/www/index.html`.

## 1. Enabling it

1. On the device: Settings > API, enter a key, switch the API on. Both are stored in NVS.
2. The server starts when WiFi has an IP and stops when it loses it; a slow periodic
   reconcile heals missed events, so there is nothing to restart by hand.
3. The clock face shows the IP address.

Safety properties built into the server, so you know what to expect:

| Property | Value |
|---|---|
| Transport | HTTP on port 80, no TLS. Treat the key as LAN-only and never expose the port to the internet |
| Clients | 2 concurrent sockets, idle keep-alive connections are purged LRU |
| Request body | JSON, at most 2 KB |
| Priority | Below LVGL, the music task and the worker; a busy API cannot stall the UI |
| Crash-loop breaker | After 3 consecutive abnormal resets the server stays down until you re-enable it in Settings |
| Empty key | The server never runs with an empty key, even if it is switched on |

## 2. Authentication

Every `/api/` request needs the key in one of two headers. Without it you get
`401 Unauthorized` with `WWW-Authenticate: Bearer`, and the attempt is logged on the device.

```
X-Api-Key: <your-api-key>
Authorization: Bearer <your-api-key>
```

The embedded page at `GET /` needs no key to load; it asks for the key once and keeps it in
the browser's localStorage. A 401 makes it ask again.

## 3. Endpoints

| Method and path | Purpose | Body or query |
|---|---|---|
| `GET /` | Embedded control page (now playing, controls, alarm banner, settings, events, log) | none, no auth |
| `GET /api/v1/status` | Snapshot: time, WiFi, playback, alarm, volume, USB audio, heap | none |
| `GET /api/v1/settings` | All settings; secrets appear only as `*_set` booleans | none |
| `PATCH /api/v1/settings` | Partial update, applied live and saved to NVS | JSON object with any keys from section 5 |
| `GET /api/v1/events` | The persistent event journal (evlog) | none |
| `GET /api/v1/log` | The in-RAM ui_log (text/plain) | none |
| `GET /api/v1/usb` | USB audio descriptor survey (text/plain) | none |
| `GET /api/v1/songs` | Navidrome song search | `?q=<text>[&limit=N]`, `q` required |
| `GET /api/v1/playlists` | The server's playlists | none |
| `POST /api/v1/playback` | Start, stop, skip | `{"action": "...", "query": "...", "id": "..."}` |
| `POST /api/v1/controls` | Alarm and device controls | `{"action": "..."}` or `{"volume": 0-100}` or `{"brightness": 1-100}` |
| `GET /api/v1/voice/clips` | Wake-word and recording clips held in RAM, with the verifier's verdict | none |
| `GET /api/v1/voice/clips/<id>` | One clip as `audio/wav` (16 kHz mono) | none |
| `POST /api/v1/voice/clips/clear` | Forget all clips | none |
| `POST /api/v1/voice/record` | Record through the real mic, gain and AFE; reply is `audio/wav`. Blocks for the duration; `409` while the mic is paused by music playback | `{"seconds": 1-8}` |

Playback actions: `play` (needs `query`, matched on the server and optionally by the AI
backend), `play_playlist` (needs `query`), `play_random`, `play_song_id` and
`play_playlist_id` (need `id`; `title`/`name` and `artist` may be supplied for the display),
`stop`, `pause`, `next`, `prev`.

Control actions: `snooze`, `dismiss` (both `409 Conflict` when no alarm is ringing),
`volume_up`, `volume_down`, `brightness_up`, `brightness_down`, and the test hook
`open_settings_page` with `"page": 0-9` (4 = Alarms, 9 = Alarm sound; `503` if the display
lock is busy).

## 4. Examples

PowerShell (5.1 or 7). The clock's IP is on the clock face; replace the placeholders.

```powershell
$ip  = "192.168.1.50"
$key = "<your-api-key>"
$h   = @{ "X-Api-Key" = $key }

Invoke-RestMethod -Uri "http://$ip/api/v1/status" -Headers $h
Invoke-RestMethod -Uri "http://$ip/api/v1/events" -Headers $h | Format-Table
Invoke-RestMethod -Uri "http://$ip/api/v1/log"    -Headers $h          # plain text
Invoke-RestMethod -Uri "http://$ip/api/v1/usb"    -Headers $h          # plain text

Invoke-RestMethod -Uri "http://$ip/api/v1/playback" -Method Post -Headers $h `
    -ContentType "application/json" -Body '{"action":"play_random"}'
Invoke-RestMethod -Uri "http://$ip/api/v1/playback" -Method Post -Headers $h `
    -ContentType "application/json" -Body '{"action":"play","query":"gymnopedie"}'
Invoke-RestMethod -Uri "http://$ip/api/v1/playback" -Method Post -Headers $h `
    -ContentType "application/json" -Body '{"action":"stop"}'

Invoke-RestMethod -Uri "http://$ip/api/v1/controls" -Method Post -Headers $h `
    -ContentType "application/json" -Body '{"volume":35}'
Invoke-RestMethod -Uri "http://$ip/api/v1/controls" -Method Post -Headers $h `
    -ContentType "application/json" -Body '{"action":"snooze"}'

# Alarm 1 at 06:30 on weekdays (days = weekday bits, bit0 Sunday .. bit6 Saturday)
Invoke-RestMethod -Uri "http://$ip/api/v1/settings" -Method Patch -Headers $h `
    -ContentType "application/json" `
    -Body '{"alarm1_enabled":1,"alarm1_hh":6,"alarm1_mm":30,"alarm1_days":62,"max_alarm_seconds":600}'

# A 3 s microphone recording to a file
Invoke-WebRequest -Uri "http://$ip/api/v1/voice/record" -Method Post -Headers $h `
    -ContentType "application/json" -Body '{"seconds":3}' -OutFile mic.wav
```

curl (bash; on Windows PowerShell use `curl.exe`, because `curl` is an alias for
`Invoke-WebRequest`):

```bash
IP=192.168.1.50
KEY='<your-api-key>'

curl -s -H "X-Api-Key: $KEY" http://$IP/api/v1/status | python -m json.tool
curl -s -H "X-Api-Key: $KEY" http://$IP/api/v1/events
curl -s -H "X-Api-Key: $KEY" http://$IP/api/v1/log
curl -s -X POST -H "X-Api-Key: $KEY" -H "Content-Type: application/json" \
     -d '{"action":"play_random"}' http://$IP/api/v1/playback
curl -s -X POST -H "X-Api-Key: $KEY" -H "Content-Type: application/json" \
     -d '{"action":"next"}' http://$IP/api/v1/playback
curl -s -X PATCH -H "X-Api-Key: $KEY" -H "Content-Type: application/json" \
     -d '{"volume":40,"bright_max":70}' http://$IP/api/v1/settings
curl -s -H "X-Api-Key: $KEY" "http://$IP/api/v1/songs?q=bach&limit=5"
```

## 5. Settings keys accepted by PATCH

Integers and booleans (range checked, `400 Bad Request` with `invalid value for <key>`
otherwise):

| Key | Range | Notes |
|---|---|---|
| `volume`, `alarm_volume` | 0-100 | |
| `bright_min`, `bright_max` | 1-100 | ambient-dimming floor and ceiling |
| `alarm1_enabled` .. `alarm3_enabled` | 0-1 | three alarm slots |
| `alarm1_hh` .. `alarm3_hh`, `alarm1_mm` .. `alarm3_mm` | 0-23, 0-59 | |
| `alarm1_days` .. `alarm3_days` | weekday bits | bit0 Sunday .. bit6 Saturday; `+128` = ring once, then switch the slot off; an empty weekday set is stored as every day |
| `max_alarm_seconds` | 0 or 30-7200 | 0 = ring until dismissed |
| `alarm_enabled`, `alarm_hh`, `alarm_mm` | | deprecated aliases of alarm 1; `alarm1_*` wins if both are given |
| `alarm_mode` | 0-1 | |
| `clock_face` | 0-1 | |
| `orientation` | 0-2 | |
| `tz_offset_min` | -720..840 | minutes east of UTC |
| `voice_enabled` | 0-1 | |
| `voice_mic_gain` | 0-42 | dB, ES8311 ADC |
| `voice_wake_thr` | 40-99 | percent |
| `voice_wake_verify`, `voice_wake_collect` | 0-1 | |
| `api_enabled` | 0-1 | |
| `svc_poll_s` | 0 or a bounded interval | 0 = no periodic server probes |
| `ambient_period_s` | bounded interval | camera light sampling period |

Strings:

| Key | Notes |
|---|---|
| `wifi_ssid`, `wifi_pass` | reconnect happens through the normal WiFi manager |
| `nav_url`, `nav_user`, `nav_pass` | Navidrome; `nav_url` is `https://host[:port]` without trailing slash |
| `ai_url`, `ai_key`, `ai_model` | OpenAI-compatible server |
| `api_key` | changing it takes effect for the next request |
| `voice_wake_word` | must be one of the ids listed in `GET /api/v1/settings` under `voice_wake_words` (`[{id,name}]`), else `400` |

`GET /api/v1/settings` returns the secrets as `wifi_pass_set`, `nav_pass_set`, `ai_key_set`
and `api_key_set` booleans only.

Known quirk (documented in the source): a PATCH issued while the on-device Settings screen is
sitting on a modified, unsaved page can be overwritten by a later on-device Save.

## 6. Shape of GET /api/v1/status

Abbreviated; numbers are examples.

```json
{
  "time":     { "epoch": 1790000000, "local": "2026-10-02 21:14:05", "valid": true },
  "wifi":     { "connected": true, "ip": "192.168.1.50", "ssid": "<ssid>" },
  "playback": { "state": "playing", "title": "...", "artist": "...", "status": "..." },
  "alarm":    { "enabled": true, "ringing": false, "ringing_index": 0, "remaining_s": 0,
                "max_alarm_seconds": 600, "snooze_until": 0,
                "next": { "index": 1, "epoch": 1790030600, "in_s": 30595, "hh": 6, "mm": 30, "wday": 5, "snooze": false },
                "missed": null },
  "volume":   35,
  "usb_audio": { "state": 1, "present": false, "product": null, "rate": null, "channels": null },
  "heap":     { "internal_free": 98000, "internal_largest": 61000,
                "dma_free": 90000, "dma_largest": 57000, "psram_free": 20000000 }
}
```

`playback.state` is one of `idle`, `busy`, `playing`, `paused`. `alarm.ringing_index` is 1..3
or 0. `heap.dma_largest` is the number to watch before reporting audio problems (see
[serial-capture-and-logs.md](serial-capture-and-logs.md) section 3).

## 7. Error responses

| Status | When |
|---|---|
| `400 Bad Request` | missing body, invalid JSON object, missing or unknown `action`, missing `query`/`id`/`q`, value out of range (`invalid value for <key>`), `page must be 0..9` |
| `401 Unauthorized` | missing or wrong key |
| `404 Not Found` | unknown endpoint under `/api/` |
| `408 Request Timeout` | body not received in time |
| `409 Conflict` | `snooze`/`dismiss` with no alarm ringing; `voice/record` while the mic is paused |
| `413 Content Too Large` | body over 2 KB |
| `503 Service Unavailable` | display busy (`open_settings_page`), music server not configured or unreachable for `songs`/`playlists` |
| `500 Internal Server Error` | out of memory |

Error bodies are JSON with a short message.

## 8. Using the API for development

- Scripted test loads: the TLS corruption bisection drove `play_random` plus `next` every
  20 s while `readcom.py` captured serial; see
  [11-tls-stream-corruption-investigation.md](../../devices/guition-jc4880p443c/docs/11-tls-stream-corruption-investigation.md) section 10.
- Wake-word data collection: `tools/wake_clips.py` wraps `voice/record` and `voice/clips`
  ([add-a-wake-word.md](add-a-wake-word.md)).
- Alarm scheduling tests: `tools/test_alarm_sched.py` mirrors the on-device scheduler so a
  PATCH can be checked against expected `alarm.next` values.

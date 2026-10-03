# Voice & AI Pipeline — architecture, models, and data flow

Developer documentation for the voice-control and AI subsystem of `bedside_clock`.
The companion document [VOICE.md](VOICE.md) is the *user-facing* guide (what to
say, tuning, troubleshooting); this one documents how it all works: the models,
the libraries, the tasks, and the two pipelines that turn sound into actions.

Verified on hardware 2026-09-02 (ESP32-P4 rev v1.3, ESP-IDF 5.5.4).

---

## 1. Big picture

There are **two distinct pipelines**, both starting from the same always-on
microphone front end:

- **Pipeline A — grammar commands (fully on-device, no network).**
  "Hi ESP" (WakeNet9 wake word) opens a ~6 s command window in which
  MultiNet7-EN matches one of **62 pre-registered phrases** (`voice_actions.c`
  table) and `voice_actions_dispatch()` executes it — snooze, open a settings
  page, nudge a slider, etc. Latency is a few hundred ms; works offline.

- **Pipeline B — free-speech music queries (on-device capture + server AI).**
  Song and playlist *names* cannot be in a fixed grammar, so the grammar
  commands "play song" / "playlist" switch into capture mode: the next
  utterance is VAD-endpointed into a 16 kHz WAV, sent to a LocalAI server for
  **Whisper transcription**, matched against the Navidrome library with a
  local fuzzy scorer, disambiguated (only when needed) by a **Qwen chat
  model**, and played.

```
                        ┌────────────────────────────────────────────────┐
  ES8311 analog mic     │ ON-DEVICE (esp-sr)                             │
  ───────────────────►  │ AFE: VAD(WebRTC) → WakeNet9 "Hi ESP" → AGC     │
  16 kHz mono, +24 dB   │        │ wake                                  │
  analog, ×12 digital   │        ▼                                      │
                        │ MultiNet7-EN grammar (62 phrases, 6 s window) │
                        └──────┬──────────────────────────┬──────────────┘
                               │ any grammar command      │ "play song"/"playlist"
                               ▼                          ▼
                    voice_actions_dispatch()      VAD-endpointed WAV capture
                    (synthesizes LVGL events              │ (≤8 s, PSRAM)
                     on the real widgets)                 ▼
                                              ┌────────────────────────────┐
                                              │ LOCALAI SERVER (HTTP)      │
                                              │ /v1/audio/transcriptions   │
                                              │   model: whisper-1         │
                                              │ /v1/chat/completions       │
                                              │   model: qwen_qwen3-4b-…   │
                                              └──────────┬─────────────────┘
                                                         ▼
                                    music_player.c: Navidrome search3 →
                                    fuzzy shortlist (≤25) → dominance
                                    shortcut or AI referee → download → play
```

---

## 2. Models

### On-device (esp-sr, flashed to the `model` partition)

| Model | What | Details |
|---|---|---|
| **WakeNet9 `wn9_hiesp`** | Wake word "Hi ESP" | Quantized `wakeNet9_v1h24_Hi,ESP_3_0.63_0.635`, trigger v4. Detection threshold comes from the setting **`voice_wake_thr`** (percent, default 50; the model default 0.63 was too strict for a non-American accent on this quiet analog mic; 0.4 is a hair trigger) via `s_afe->set_wakenet_threshold(data, 1, thr)`, re-applied live by `voice_apply_wake_threshold()`. Closed, US-trained: cannot be tuned to a voice, which is why the microWakeWord engine below exists. |
| **microWakeWord** (optional, `CONFIG_BEDSIDE_WAKE_MWW`; **the build on the board since 2026-09-20**) | Replacement wake-word engine | `main/wake_mww.cpp`: embedded streaming int8 models (`main/models/<id>.tflite` + ESPHome-v2 `.json` manifest, registry from `BEDSIDE_WAKE_MODELS`, live-switchable via `voice_wake_word`) on **esp-tflite-micro + ESP-NN PIE kernels**, features from the TF microfrontend (`esphome/esp-micro-speech-features`). Fed with the AFE output in the detect loop; WakeNet is then not initialised. Measured on this P4: 0.55 ms per 30 ms invoke, 22 KB arena in PSRAM (`src/sample_wakeword_bench/README.md`). Embedded: `hey_mr_clock` (mrclock_v1d, active) and `hi_esp` (v0); see `main/models/README.md` and §12. `voice_wake_thr` maps onto the probability cutoff (slider 78 = 0.95). |
| **Wake verifier** (whisper, optional) | Second opinion on every wake | `voice_wake_verify` (default on): the last 2 s of AFE output before the wake (`WAKE_CLIP_MS`, from a 2.5 s PSRAM pre-roll ring) is POSTed to `/v1/audio/transcriptions`. The command window opens immediately, but a recognised command waits ≤ `VERIFY_WAIT_MS` (2.5 s) for the verdict; a transcript without the phrase (letters-only match against `k_wake_accept[]`) closes the window with a grey "Not for me" badge. Fail-open when the server is slow/down/busy. |
| **MultiNet7 `mn7_en`** | English command grammar | Created with a **6000 ms** command window (`VOICE_MN_TIMEOUT_MS`). Phrases are registered at bring-up via `esp_mn_commands_add(cmd_id, phrase)` from the `voice_actions.c` table; g2p (grapheme→phoneme) runs on-device, so phrases are plain lowercase text. Recognized text in logs looks like phoneme soup (`" bPcN SfTglZ"`) — normal. |
| **AFE (audio front end)** | Preprocessing chain | `afe_config_init("M", models, AFE_TYPE_SR, AFE_MODE_LOW_COST)`, mono ("M"). Pipeline as reported at boot: `[input] -> VAD(WebRTC) -> WakeNet(wn9_hiesp) -> AGC(WakeNet) -> [output]`. `vad_init=true` (endpointing for Pipeline B), `agc_init=true`, `aec_init=false` (single mic, no reference channel), `memory_alloc_mode=AFE_MEMORY_ALLOC_MORE_PSRAM`, `wakenet_mode=DET_MODE_95` (aggressive — the quiet mic needs it). |

Selected in `sdkconfig.defaults` (esp-sr Kconfig): WakeNet9 *hiesp* + MultiNet7
English. The models are packed into `build/srmodels/srmodels.bin` (~2.9 MB) and
flashed to the dedicated **`model` partition: 4 MB SPIFFS at 0x820000**
(`partitions.csv`). ⚠️ `idf.py app-flash` does **not** update this partition —
after changing model config, run a full `idf.py flash`, or `voice_start()`
fails with *"no models in 'model' partition"*.

### Server-side (LocalAI, OpenAI-compatible API)

| Model | Endpoint | Used for |
|---|---|---|
| **`whisper-1`** | `POST {ai_url}/v1/audio/transcriptions` | Speech-to-text of the captured WAV. Model name is compile-time `AI_STT_MODEL` in `localai_client.c` (the one AI parameter that is *not* a runtime setting). |
| **`qwen_qwen3-4b-instruct-2507`** | `POST {ai_url}/v1/chat/completions` | Picking the best match from a numbered candidate list (`ai_pick_best`). `temperature: 0`, `max_tokens: 8`, answers in ~9 s. |

Server settings live in NVS (`settings.h` v5 fields): `ai_provider`
(LocalAI only), `ai_url`, `ai_key` (Bearer), `ai_model` — editable on the
Settings → AI page or over the HTTP API. There is **no default endpoint**:
`ai_url` ships empty, which every AI entry point treats as "not
configured" (no request is made, the service indicator shows off).
Model-name gotchas learned the hard way on a LocalAI box: there was no
plain `"qwen"` model (settings_init rewrites a stored `"qwen"` to the
full name), and `qwen3-1.7b` was broken server-side.

---

## 3. Libraries / components

| Component | Role |
|---|---|
| `espressif__esp-sr` (managed) | AFE, WakeNet9, MultiNet7, `esp_mn_commands_*` grammar registration, `esp_srmodel_*` model loading from the `model` partition |
| `espressif__esp-tflite-micro` + `espressif__esp-nn` (managed) | TensorFlow Lite Micro interpreter with P4 PIE kernels for `wake_mww.cpp`; only linked in with `CONFIG_BEDSIDE_WAKE_MWW` (the default binary does not grow) |
| `esphome__esp-micro-speech-features` (managed, Apache-2.0) | TF microfrontend (40 log-mel features / 10 ms) that every microWakeWord model is trained against; the frontend was dropped from esp-tflite-micro ≥ 1.4, hence the separate component |
| `bsp_extra` (local, vendor-derived) | ES8311 codec glue: `bsp_extra_i2s_read/write`, `bsp_extra_codec_set_fs`, `bsp_extra_codec_in_gain_set`, volume |
| `chmorgan__esp-audio-player` (local, **patched**) | Music/alarm playback. Local patch in `audio_player.cpp` (~:438,:465): treat PAUSE like PLAYING for the IDLE transition, else stop-while-paused wedges the player and the mic never resumes. Keep the patch if the component is refreshed. |
| `esp_http_client` + `esp_crt_bundle` | HTTP(S) to LocalAI and Navidrome |
| `cJSON` | Request/response bodies |
| LVGL 9 (`esp_lvgl_port`) | Badge/frame UI, and the widgets that voice commands actuate |

---

## 4. Audio front end (shared microphone, one codec)

The board has **one** ES8311 codec that is both the music DAC and the mic ADC
— codec ownership is the central constraint of the whole design.

- **Capture format**: 16 kHz / 16-bit / mono (`AUDIO_SAMPLE_RATE/BITS/SLOT_MODE`
  in `app_common.h`).
- **Gain staging** (`voice.c`): analog ES8311 ADC gain from the Voice settings
  page (`voice_mic_gain`, default **24 dB**, steps of 6), then a software
  pre-gain of **×12** (`VOICE_DIGITAL_GAIN`, ≈ +21 dB, with saturation) applied
  in the feed task *before* the AFE — the analog mic peaks around −23 dBFS
  even point-blank, far below what WakeNet was trained on.
- **Codec sharing**: every song makes `audio_player` call
  `bsp_extra_codec_set_fs(44100, …)`, which closes/reopens the record device.
  The feed task therefore polls `audio_player_get_state()`: while the player
  is not IDLE (or voice is disabled) capture is **paused** — it feeds the AFE
  *silence* at real-time pace so an open command window times out normally
  instead of wedging with WakeNet disabled. On resume it waits 300 ms, calls
  `voice_codec_reclaim()` (re-asserts 16 kHz + mic gain) and
  `s_afe->reset_buffer()`.
- **Duplex exception**: the alarm's generated chime runs at 16 kHz directly
  through `bsp_extra_i2s_write`, so **"Hi ESP → snooze/dismiss" works over the
  chime** (full duplex verified on this board). It does *not* work while an
  MP3 song is playing — the player owns the codec and the mic is paused.
- **Boot self-test** (`voice_selftest`): plays a 1 kHz tone (own 700 ms task)
  while reading the mic, then a **Goertzel** check compares power at 1 kHz vs
  an off-bin control (1466 Hz). Healthy path ≳ +15 dB and rms > 100 →
  `Voice self-test: … [OK]` in the log; broken sample framing smears it to
  ~0 dB. This catches a dead mic path at boot, before any silent failure.

---

## 5. Tasks and shared state

| Task | Core | Prio | Stack | Runs |
|---|---|---|---|---|
| `voice_feed` | 1 | 6 | 8 KB **internal** (calls the I2S driver) | mic read → ×12 gain → `afe->feed()`; pause/resume logic; `mic alive: rms=…` heartbeat every ~8 s |
| `voice_detect` | 1 | 5 | 16 KB **PSRAM** (`xTaskCreatePinnedToCoreWithCaps`) | `afe->fetch()` loop: wake detection, MultiNet inference, dispatch, Pipeline-B capture |
| `voice_tone` | 1 | 5 | 3 KB | one-shot self-test tone, self-deletes |
| `worker` (app_main) | any | 5 | 16 KB | Pipeline B's STT HTTP call (`voice_query_job`), `voice_start()` bring-up, voice enable/disable toggles |
| `music` (music_player) | any | 3 | 16 KB **PSRAM** | Navidrome + `ai_pick_best` chat call, playback control (the decoder is the BSP's prio-5 `Audio Task`) |
| `song_dl` (song_cache) | any | 3 | 16 KB **PSRAM** | streams the current song (and prefetches the next) into the PSRAM cache over TLS |
| `voice_stt` (voice.c) | any | 3 | 16 KB **PSRAM** | Pipeline B's whisper upload + `music_play_*_query()`; also the wake verifier's whisper round-trip (`VQ_VERIFY` jobs, one at a time - a busy queue means "accept unverified") |
| `httpd` (api_server.c) | 1 | 3 | 16 KB | `POST /api/v1/voice/record` blocks here while `voice_detect` fills the clip (≤ 8 s + an open command window) |
| `taskLVGL` (esp_lvgl_port) | any | 4 | 7 KB | UI, **and touch polling** (the GT911 INT pin is unwired, so touch only works while this task runs) - it is deliberately the only prio-4 task: bulk TLS at the same priority time-sliced with it |

Core 1 keeps voice inference away from LVGL/esp_hosted on core 0. The
`voice_detect` PSRAM stack works because `CONFIG_SPIRAM_XIP_FROM_PSRAM=y`
keeps PSRAM addressable during flash writes (it can reach `settings_save()`
via dispatched commands) — an invisible dependency: don't turn XIP off.

Measured footprint at bring-up (boot log): `voice up: 62 phrases, feed=512,
internal heap 217731 -> 156283 (delta 61448)` — ~60 KB internal RAM; the
AFE/model tensors live in PSRAM. One AFE feed chunk = 512 samples = 32 ms.

Bring-up (`voice_start()`, runs on the worker, serialized by
`app_init_lock()`): codec init → self-test → `esp_srmodel_init("model")` →
AFE create → wake threshold → MultiNet create → grammar registration →
feed buffer → spawn the two tasks. **Every failure path calls
`voice_pipeline_teardown()`** (destroys MN/AFE, frees the feed buffer,
`esp_srmodel_deinit`) — without it each failed retry leaked megabytes of
PSRAM, since `voice_set_enabled(true)` re-runs the whole bring-up.

---

## 6. Pipeline A — grammar commands (on-device)

```
mic ──► voice_feed ──► AFE ──► voice_detect:
                                 res->data → pre-roll ring (2.5 s)  [+ wake_mww_feed() with MWW]
                                 recording requested? → record_request_serve() (no wake handling)
                                 woke? (WAKENET_DETECTED, or wake_mww_feed() == true)
                                   ├─ disable_wakenet() (WakeNet only), green frame, blip
                                   ├─ wake_clip_begin(): snapshot last 2 s → clip store,
                                   │     copy → voice_stt (whisper) if verify is on
                                   └─ loop: verdict REJECT? → "Not for me", close
                                        mn->detect(res->data)
                                        ├─ DETECTING  (≤10 s hard watchdog)
                                        ├─ DETECTED → verify_wait_ok() (≤2.5 s, AFE kept draining)
                                        │     ├─ REJECT → command dropped
                                        │     ├─ VA_PLAY_SONG/PLAYLIST → Pipeline B
                                        │     └─ else voice_actions_dispatch()
                                        └─ TIMEOUT → "?" badge
                                 mn->clean(), enable_wakenet() / wake_mww_reset(), frame off
```

Details worth knowing:

- The 10 s watchdog on `ESP_MN_STATE_DETECTING` is belt-and-braces on top of
  MultiNet's own 6 s timeout: silence-fed spans (player owning the codec) can
  stall MultiNet's internal clock.
- The **wake blip** (60 ms 1 kHz tone) is skipped when the alarm is active or
  the player isn't IDLE — never fight for the speaker.
- UI feedback: a **status badge** on `lv_layer_sys()` (above both the settings
  and alarm overlays, which live on `lv_layer_top()`) and a **green 8 px
  border** around the screen while the command window is open. Both are only
  touched under `bsp_display_lock()`; the frame is non-clickable so touches
  fall through.

### Command registry and dispatch (`voice_actions.c`)

One table maps **62 phrases → ~50 command ids** (synonyms share an id:
"open wifi" / "open network"). To add a command: add the enum to
`voice_actions.h`, the phrase(s) to `s_phrases[]`, and either a case in
`dispatch_fn_locked()` (navigation-style) or a `voice_actions_bind*()` call
where the widget is created. Grammar re-registration happens only at
`voice_start()`.

Dispatch runs **on the voice_detect task** and has three tiers:

1. **Alarm tier (no LVGL)**: `VA_SNOOZE`/`VA_DISMISS` call
   `alarm_voice_snooze()/_dismiss()` which only set an abort flag + event-group
   bits — safe even mid-render, which matters because the alarm is exactly
   when the UI is busiest.
2. **Function tier** (under `bsp_display_lock`): navigation
   (`open settings`, `go back`, `save`, page up/down, the nine `open <page>`
   commands which map `cmd - VA_OPEN_VOLUME` onto the page-builder table in
   `ui_settings.c` — keep enum order and builder array in sync), plus
   play-random / stop-music.
3. **Widget tier** (under `bsp_display_lock`): bound widgets. The binding
   system synthesizes **LVGL events on the real widgets**
   (`lv_obj_send_event(obj, LV_EVENT_CLICKED/VALUE_CHANGED)`), so the touch
   callbacks remain the single source of truth — voice can never drift from
   touch behavior. Kinds: `K_CLICK`, `K_SLIDER` (nudge ±step, clamped),
   `K_SWITCH` (set + VALUE_CHANGED), `K_DROPDOWN` (select option),
   `K_ROLLER` (nudge with wrap-around). Widgets **auto-unbind via an
   `LV_EVENT_DELETE` hook**, so page teardown needs no bookkeeping; a command
   whose widget isn't on screen answers "Not on this screen".
   `voice_actions_set_option_list()` designates the current list widget so
   "option one…ten" can click the Nth *clickable* row (WiFi scan results,
   playlist picker).

Every dispatch returns a short feedback string shown in the badge (green =
executed, orange = heard but rejected) and logged with the model's confidence.

---

## 7. Pipeline B — free-speech music queries (capture → STT → match → play)

Triggered from Pipeline A by `VA_PLAY_SONG` or `VA_PLAY_PLAYLIST`
(`capture_query()` in `voice.c`, still on the voice_detect task, wakenet still
disabled, border still up):

```
"play song" matched
  │  requires ai_is_configured(), else badge "AI server not configured"
  ▼
open Play screen; badge "Say the song name..."
  ▼
CAPTURE (voice.c capture_query)                     limits:
  afe->fetch() loop, VAD-endpointed                  CAPTURE_MAX_MS     8000
  - wait for first VAD_SPEECH                        CAPTURE_WAIT_MS    5000
  - prepend res->vad_cache (pre-speech audio,        CAPTURE_SILENCE_MS 1000
    so the first syllable isn't clipped)             CAPTURE_FETCH_MS   32
  - accumulate AFE-processed 16 kHz mono PCM
  - endpoint: ≥1 s silence AFTER ≥400 ms of real speech
    (noise blips reset the buffer instead of ending it)
  - reject: no speech / < 0.25 s captured → "no name heard"
  ▼
44-byte WAV header + PCM in PSRAM (≤ ~256 KB)
  ▼ app_run_async → worker task (frees the WAV either way; a full job
  ▼ queue is reported as "Busy - try again", buffer freed)
STT (localai_client.c ai_transcribe_wav)
  multipart/form-data POST /v1/audio/transcriptions, model whisper-1,
  45 s timeout; response {"text": ...} trimmed of whitespace/trailing '.'
  ▼  badge shows the transcript
music_play_song_query(text)  /  music_play_playlist_query(text)
  ▼ (music task — same engine as the touch Play screen)
MATCHING (music_player.c, next section) → download → audio_player
```

The transcript path and the touch path converge at
`music_play_*_query()` — voice is just another way to type the query.

---

## 8. AI matching pipeline (music_player.c + localai_client.c)

The problem: a small 4B chat model **times out (>120 s) on a 343-song
prompt** and is nondeterministic on near-ties. The pipeline therefore
never shows the model more than **25 options**, and skips the model entirely
when the local scorer has a runaway winner.

```
query text
  ▼
Navidrome search3 (max 40)          ── nav_search_songs()
  │ empty result?
  │   └► whole library (search3 "" max 400) → MUST fuzzy-shortlist
  ▼
fuzzy_score() every candidate       ── local, no network
  ▼
shortlist ≤ SHORTLIST_MAX (25)      ── shortlist_songs(), keeps scores
  ▼
top_is_dominant()?                  ── score[0] ≥ 8 AND ≥ 2× best
  │ yes → play items[0], NO AI CALL    non-duplicate rival (duplicate =
  │ no                                  same folded artist + title containment)
  ▼
ai_pick_best(query, "title - artist" list ≤25)
  POST /v1/chat/completions, temperature 0, max_tokens 8
  prompt: numbered list, "may be misspelled or written how it sounds",
  "list is ordered most-likely-first, prefer the earlier one when torn",
  "reply ONLY the number, 0 if nothing shares words or sounds"
  ▼
  n≥1 → play that item;  0 → "AI found no match";  HTTP/parse error →
  fall back to the top local hit ("AI unavailable - playing top search hit")
```

**`fuzzy_score`** (typo- and sound-alike-tolerant, pure C, no allocations):

- `squash()`: lowercase, alnum only, spaces dropped — "red gum" hits "Redgum".
- `phonetic_fold()`: a mini-Metaphone — drop `h`, soft-c (`c`→`s` before
  e/i/y else `k`), `q`→`k`, `z`→`s`, all vowels+`y`→`a`, collapse repeats.
  "toccata"/"tocata"/"tokata" all fold to "takata". (Double Metaphone is the
  drop-in upgrade if this hits limits.)
- Scoring per query token: exact substring in squashed haystack (strong,
  2×len) → Levenshtein ≥80% similarity between *phonetic folds* of words →
  phonetic substring (medium) → bigram coverage fallbacks (≥75% of bigrams
  present) → whole-joined-query bonuses ("ram stein" → "rammstein").

**Playlist queries** skip the fuzzy stage (playlist lists are short): all
names go straight to `ai_pick_best`.

**Testing mandate** (from development): run
`python src/bedside_clock/tools/test_ai_matching.py` **before and after any
change** to the matching logic or prompts. It replicates this exact pipeline
(search3 → fuzzy shortlist → numbered-list prompt) against the real library
and real LocalAI; its 13 standard queries (including typo/phonetic cases:
*tocata, tokata, gost riders, tenashus, tome, Ram stein*) must all pass.
`fuzzy_score` in C and in the python harness are deliberate mirrors — keep
them in sync.

---

## 9. Configuration reference

### Runtime (NVS settings, Settings UI)

| Setting | Field | Default | Where used |
|---|---|---|---|
| Voice enabled | `voice_enabled` (v4) | on | gates wake handling + capture; toggling on re-runs `voice_start()` if needed |
| Mic gain | `voice_mic_gain` (v4) | 24 dB | ES8311 analog ADC gain, steps of 6 |
| Wake threshold | `voice_wake_thr` (v10) | 50 % | WakeNet threshold (0.40-0.99) or the microWakeWord cutoff; live via `voice_apply_wake_threshold()` |
| Verify wakes | `voice_wake_verify` (v10) | on | whisper second opinion on every wake (fail-open) |
| Keep clips | `voice_wake_collect` (v10) | on | last 16 wake/recording WAVs in PSRAM for `/api/v1/voice/clips` |
| AI server | `ai_url` / `ai_key` / `ai_model` (v5) | dev LocalAI box | both LocalAI endpoints |
| Navidrome | `nav_url` / `nav_user` / `nav_pass` | — | library search + song download |

### Compile-time (`voice.c` / `localai_client.c` / `music_player.c`)

| Constant | Value | Meaning |
|---|---|---|
| `VOICE_DIGITAL_GAIN` | 12 | software pre-gain before the AFE |
| `WAKE_PREROLL_MS` / `WAKE_CLIP_MS` | 2500 / 2000 | pre-roll ring and wake snapshot length |
| `VOICE_CLIPS_MAX` / `VERIFY_WAIT_MS` | 16 / 2500 | clip store depth; how long a command waits for the verdict |
| `CONFIG_BEDSIDE_WAKE_MWW` | n | Kconfig: microWakeWord engine instead of WakeNet (`sdkconfig.mww.defaults`) |
| `VOICE_MN_TIMEOUT_MS` | 6000 | MultiNet command window |
| `CAPTURE_MAX/WAIT/SILENCE_MS` | 8000/5000/1000 | Pipeline B endpointing |
| `AI_STT_MODEL` | "whisper-1" | STT model name (only compile-time AI knob) |
| `AI_HTTP_TIMEOUT_MS` | 45000 | LLM inference can be slow |
| `SHORTLIST_MAX` / `DOMINANT_MIN` | 25 / 8 | model list cap / dominance floor |

### sdkconfig / partition prerequisites

- esp-sr: WakeNet9 *hiesp* + MultiNet7 English selected; models land in the
  4 MB `model` partition at 0x820000 (full `idf.py flash` required).
- `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` — required for the PSRAM-stacked
  voice_detect task to survive flash writes (see §5).

---

## 10. Failure modes and how the design absorbs them

| Failure | Behavior |
|---|---|
| Models not flashed | `voice_start` logs *"no models in 'model' partition (full idf.py flash needed?)"*, tears down cleanly; rest of the app unaffected |
| Any bring-up step fails | `voice_pipeline_teardown()` frees AFE/MN/models/buffers; retry via the Voice toggle does not leak |
| Player owns the codec | capture pauses, AFE fed silence at real-time pace; command window times out normally; resume re-reclaims codec + resets AFE buffer |
| Mic path broken | boot Goertzel self-test flags `[SUSPECT]` / read failure in the log and Specs page |
| AI server down / misconfigured | Pipeline B refuses at capture start ("AI server not configured") or badge "Didn't catch that"; song matching falls back to the top local search hit when only `ai_pick_best` fails |
| Whisper hears nothing | empty transcript → `ESP_ERR_NOT_FOUND` → orange badge, no music action |
| Worker queue full | capture WAV freed, "Busy - try again" badge (no leak) |
| Command mid-render / alarm ringing | snooze/dismiss tier never takes the LVGL lock; widget tier fails soft with "UI busy" after a 1 s lock timeout |

---

## 11. File map

| File | Contents |
|---|---|
| `main/voice.c` | mic front end, self-test, feed/detect tasks, wake/command loop, badge + green frame, Pipeline B capture, WAV, STT hand-off, wake pre-roll + clip store + whisper verifier, recording requests |
| `main/wake_mww.{cpp,h}` | microWakeWord streaming engine (tflite-micro, microfrontend, sliding-window decision); stubs unless `CONFIG_BEDSIDE_WAKE_MWW` |
| `main/models/` | embedded wake model + manifest (placeholder until the trained one lands) |
| `main/api_server.c` (`/api/v1/voice/...`) | clip list / WAV download / clear, on-demand recording |
| `tools/wake_clips.py` | PC side of the flywheel: pull clips, guided positive-recording sessions |
| `tools/wakeword_train/` | training pipeline (Chatterbox voice-cloned positives, Piper generic positives, feature generation, microWakeWord training config/runner; see §12) |
| `src/sample_wakeword_bench/` | standalone P4 bring-up test of the engine, with measured numbers in its README |
| `main/voice_actions.{c,h}` | command enum, 62-phrase grammar table, widget binding (auto-unbind on delete), 3-tier dispatch |
| `main/localai_client.{c,h}` | `ai_transcribe_wav` (whisper, multipart), `ai_pick_best` (qwen, numbered list), `ai_is_configured`, shared POST helper |
| `main/music_player.{c,h}` | query resolution (fuzzy scorer, shortlist, dominance, AI referee), playback engine |
| `main/navidrome_client.{c,h}` | Subsonic REST: search3, playlists, random, stream download |
| `main/ui_settings.c` (Voice page) | enable toggle, mic-gain slider |
| `tools/test_ai_matching.py` | offline harness mirroring the matching pipeline — run before/after matching changes |
| `VOICE.md` | user-facing guide: full phrase list, tuning, troubleshooting |

---

## 12. Training a wake-word model (tools/wakeword_train, WSL `~/mww`)

Why: WakeNet is closed and American-trained; lowering its threshold for an
Australian accent buys recall with false wakes. A microWakeWord model trained
on the user's own voice fixes both ends. Hardware: the RTX 5070 is used for
sample generation (PyTorch cu128); the wake model itself is ~30 k parameters
and trains on the CPU (TensorFlow, no sm_120 wheel yet).

Data (all 16 kHz mono):

1. **Real positives through the device** — `tools/wake_clips.py record`
   (`POST /api/v1/voice/record`): mic + gain + AFE, i.e. exactly what the
   model will hear. Aim for 150-300 across distances, moods, with music.
2. **Voice-cloned positives** — `gen_chatterbox.py`: Chatterbox (MIT) zero-shot
   clones of the user's headset references (`record_ref.py`), Australian male
   by construction; sweeps exaggeration / cfg / temperature. Also used for
   **confusables** in the same voice ("hi there", "high speed", ...).
3. **Generic positives** — Piper LibriTTS-R (US, 900 speakers,
   `python -m piper_sample_generator` from its repo root; needs the model's
   `.json` next to the `.pt`), low sampling weight only.
4. **Hard negatives from the device** — `tools/wake_clips.py pull` collects
   every wake clip with the verifier's verdict; the REJECTs are gold.
5. **Generic negatives / backgrounds** — `kahrendt/microwakeword` HF feature
   sets (speech, dinner_party, no_speech, dinner_party_eval), MIT RIRs
   (`16khz/*.wav`), AudioSet (`data/bal_train/NN.parquet`, flac bytes), FMA.

Pipeline: `make_features.py` (SimpleClips replaces microwakeword's Clips —
`datasets` 5.x needs torchcodec + system FFmpeg — then the reference
notebook's Augmentation and `RaggedMmap` splits) → `training_parameters_*.yaml`
→ `python -m microwakeword.model_train_eval ... mixednet ...` →
`trained_models/<name>/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite`
+ a v2 manifest → copy to `main/models/<id>.{tflite,json}` and list `<id>` in
`BEDSIDE_WAKE_MODELS` (`main/CMakeLists.txt`; several models can be embedded and
the user picks one in Settings > Voice, setting `voice_wake_word`) → build with
`sdkconfig.mww.defaults` → `idf.py app-flash`. **Evaluate with `eval_stream.py`
on held-out device utterances and held-out device room/TV audio before
flashing**: training-set metrics and per-clip evaluation with interpreter resets
were both misleading (the model emits p≈1 for ~1 s after a reset, which the
device's cool-off hides).

Outcome 2026-09-20 (model history and the seven lessons in
`tools/wakeword_train/README.md`): the clock runs `hey_mr_clock` = `mrclock_v1d`
(88 device utterances cut with 0.5 s room context and no normalisation, 554
voice clones, 12 k Piper, plus 24 min of TV / 2 min of own speech / music
through the device mic as full-length negatives): quiet-room recall 93 %, 0 FA/h
on held-out TV + room + speech at cutoff 0.95 (slider 78), weak only on the
phrase spoken over a loud TV. `hi_esp` (v0, Piper-only) stays in the picker as
the fallback. Retrain trigger and recipe: the README's "Next model" section.

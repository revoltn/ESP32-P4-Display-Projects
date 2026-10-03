# Voice Control — how it works and what to say

> Developer documentation (models, tasks, pipelines, AI matching internals)
> lives in [VOICE_AND_AI_PIPELINE.md](VOICE_AND_AI_PIPELINE.md).

## The sequence

```
you:    "Hi ESP"                     (the wake word - always being listened for;
                                      pick another in Settings > Voice > Wake word)
device: short beep + GREEN BORDER around the screen
you:    ONE command from the list below, within ~6 seconds
device: border drops, badge flashes the result:
          green  "✓ ..."            command executed
          orange "..."              heard, but not valid right now
          grey   "?"                nothing recognized (timeout)
```

Rules that trip people up:

- **Exact phrases only.** The recognizer matches a fixed grammar, *not* the text
  on the buttons. "Timezone" does nothing — the phrase is **"open time zone"**.
  The full list is below; anything not on it is ignored.
- **One command per green window.** After a command executes (or times out) the
  device goes back to waiting for the wake word. To issue another command, say
  "Hi ESP" again. "Hi ESP volume up volume up" does not work.
- **Wait for the border** before speaking the command. Speaking during the beep
  can clip the start of the command.
- Only the **selected wake word** wakes it ("Hi ESP" by default; "Hey ESP" is
  not in the model). Firmware built with the microWakeWord engine lists every
  embedded phrase in Settings > Voice > Wake word (e.g. "Hey Mr Clock" once its
  model is added - see `main/models/README.md`); the pages and examples below
  say "Hi ESP" for short.
- Commands are context-sensitive: a widget command only works while its page is
  on screen (otherwise the badge says "Not on this screen"). The navigation
  commands work from anywhere.

## Commands

### Anywhere
| Say | Does |
|---|---|
| "open settings" | opens the settings overlay |
| "open volume" / "open wifi" (or "open network") / "open time zone" / "open music" / "open alarm" / "open alarm sound" / "open clock face" / "open brightness" / "open specifications" (or "device info") / "open voice control" | opens settings straight onto that page, from any screen |

### Music (anywhere)
| Say | Does |
|---|---|
| "play song" (or "play a song") | opens the Play screen and a **second capture window**: the border stays green and the badge asks for the song name — say it ("toccata"), and it is transcribed by the AI server, matched against the library, and played |
| "playlist" (or "play playlist") | same, but asks for and plays a playlist |
| "play random" (or "play random music") | random songs from the whole library |
| "stop music" (or "stop playing") | stops playback — **but only between songs**: while a song is actually playing the player owns the audio codec and the mic is paused, so use the touch Stop |

While the mic is paused the **MIC icon on the clock face turns dim grey**; it goes
green again once the song stops and listening has actually resumed. Grey mic =
"Hi ESP" will not be heard.

The name capture waits up to 5 s for you to start speaking, records until ~1 s
of silence (8 s max), then the badge shows what was heard. Song-name
transcription needs the AI server (Settings → shows "AI server not
configured" otherwise); the whisper model name is `AI_STT_MODEL` in
`main/localai_client.c` (default "whisper-1").

### While the alarm is ringing
| Say | Does |
|---|---|
| "snooze" or "snooze alarm" | snoozes 9 minutes |
| "stop alarm" or "dismiss alarm" | dismisses the alarm |

(These work over the beep-chime. While a playlist *song* is playing the mic is
paused — the player owns the audio codec — so use the touch buttons there. The
clock-face MIC icon is grey whenever that is the case.)

### While settings is open (any page)
| Say | Does |
|---|---|
| "go back" | back to the menu, or closes settings from the menu |
| "save settings" | same as the Save button |
| "page up" / "page down" | scrolls the current page |

### On specific settings pages (the widget must be on screen)
| Say | Page | Does |
|---|---|---|
| "scan networks" | WiFi | starts a scan |
| "option one" … "option ten" | WiFi / Alarm sound | picks the Nth row of the network / playlist list |
| "connect now" | WiFi | same as the Connect button |
| "test server" | Music | saves and tests the Navidrome server |
| "first alarm" / "second alarm" / "third alarm" | Alarms | expands that alarm's card (the editor and the commands below then target it) |
| "alarm on" / "alarm off" (or "turn alarm on/off") | Alarms | flips the expanded alarm's switch |
| "hour up" / "hour down" / "minute up" / "minute down" | Alarms | nudges the expanded alarm's time rollers (wraps around) |
| "every day" / "weekdays" / "weekends" / "just once" | Alarms | day presets for the expanded alarm ("just once" toggles the ring-once flag) |
| "choose playlist" | Alarm sound | fetches the playlist list |
| "volume up" / "volume down" | Volume | ±10 on the volume slider |
| "alarm volume up" / "alarm volume down" | Volume | ±10 on the alarm-volume slider |
| "brightness up" / "brightness down" | Brightness | ±10 on max (day) brightness |
| "night brightness up" / "night brightness down" | Brightness | ±10 on min (night) brightness |
| "giant clock" / "large clock" | Clock Face | picks the clock face |
| "reset calibration" | Brightness | resets the ambient-light calibration |
| "refresh page" | Specifications | refreshes the device specs |

## Tuning & troubleshooting

- **Mic gain**: Settings → Voice → slider (analog ES8311 gain, steps of 6 dB,
  default 24). Raise it if wake misses at a distance; lower if you get false
  wakes.
- **Wake threshold**: Settings → Voice → slider, 40–99 % (also
  `voice_wake_thr` in the web panel / API); lower = wakes more easily *and*
  more falsely. Applied live. On the WakeNet build it is the model's detection
  threshold (default 50; the model's own 63 was too strict for a non-American
  accent). On the microWakeWord build the slider maps onto that model's
  0.88–0.99 cutoff (50 → 0.90, 70 → 0.94, **78 → 0.95, the setting the
  "Hey Mr Clock" model was measured at**, 90 → 0.97).
- **Wake word** (microWakeWord build): Settings → Voice → Wake word picks one
  of the embedded models, live. As of 2026-09-20 the clock runs **"Hey Mr
  Clock"** (trained on the owner's voice through this very mic: hears him 14
  times in 15 in a quiet room, silent through hours of TV, but misses when the
  phrase is spoken *over* a loud TV) with "Hi ESP" as the fallback pick.
  Retraining: `tools/wakeword_train/README.md`.
- **AI server confirms each wake** (Settings → Voice switch, `voice_wake_verify`,
  default on): the 2 s of audio around every "Hi ESP" is sent to the AI
  server's whisper. The green window opens immediately, but a command is only
  carried out once the transcript is in, and if it did not contain the phrase
  the window closes with a grey "Not for me" badge. This kills most false
  wakes from TV/music/speech. Fail-open: if the AI server is unreachable or
  slower than ~2.5 s the command goes through unverified (logged as such).
- **Wake clips**: the last 16 wake snapshots and recordings are kept in RAM
  (`voice_wake_collect`) and served by the API — `GET /api/v1/voice/clips`
  lists them with the verifier's verdict and transcript,
  `GET /api/v1/voice/clips/<id>` is the WAV, `POST /api/v1/voice/record`
  `{"seconds":3}` records through the real mic path. `tools/wake_clips.py`
  pulls them to the PC (`pull`) or runs a guided recording session
  (`record`); rejected wakes are the hard negatives a better wake model gets
  trained on.
- One more knob lives in `main/voice.c` (reflash to change):
  `VOICE_DIGITAL_GAIN` (software boost before the recognizer, currently 12).
- **Event history**: Settings → Specifications shows every wake, recognized
  command (with confidence), and timeout. The recognized-phrase text looks like
  phoneme soup (`" bPcN SfTglZ"` = "open settings") — that is normal.
- **Serial log** (the USB-Serial/JTAG COM port) additionally prints `mic alive: rms=…` and
  `afe alive: … vad=…` every 8 s: rms is what the recognizer hears (ambient
  ~300, close speech 1500+), vad=1 means speech energy is being detected.
- On every boot a short 1 kHz **self-test beep** plays; the log line
  `Voice self-test: … [OK]` proves the mic path is healthy end-to-end.

## How "play song Toccata" works under the hood

Free-form words (song names) cannot be matched by the on-device grammar —
MultiNet only knows pre-registered phrases — so "play song" switches the
pipeline into capture mode:

1. "Hi ESP" wakes as usual; "play song" is matched by the grammar.
2. voice.c keeps the border up and records your next utterance: the AFE's
   voice-activity detector endpoints it (including a pre-speech cache so the
   first syllable isn't clipped) into a 16 kHz mono WAV in PSRAM.
3. The worker task POSTs the WAV to the AI server
   (`/v1/audio/transcriptions`, whisper) and hands the transcript to
   `music_play_song_query()` — the same engine behind the Play screen's Song
   tile (Navidrome search + qwen `ai_pick_best` disambiguation + playback).

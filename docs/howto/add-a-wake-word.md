# Add a wake word

`bedside_clock` has two wake-word engines. The default build uses esp-sr WakeNet9 and
understands only "Hi ESP". The `build_mww` variant runs microWakeWord models on tflite-micro
and offers every embedded model in Settings > Voice > Wake word, switchable live. Adding a
phrase means training a model and embedding it; both halves are documented in detail
elsewhere, this page is the map.

## Embedding a model you already have

Source of truth: [main/models/README.md](../../src/bedside_clock/main/models/README.md).

1. Copy the pair into `src/bedside_clock/main/models/` as `<id>.tflite` and `<id>.json`
   (`<id>` is lowercase snake_case; the `wake_word` field in the JSON manifest is the name
   shown in the picker).
2. Append `<id>` to `set(BEDSIDE_WAKE_MODELS ...)` in `src/bedside_clock/main/CMakeLists.txt`.
   The list generates an X-macro header and the EMBED_FILES entries.
3. If the phrase is new, extend the verifier's accept list `k_wake_accept[]` in `main/voice.c`
   (the optional whisper check compares transcripts against it).
4. Build and flash the variant; the model partition is not involved, `app-flash` suffices:

```powershell
cd src\bedside_clock
idf.py -B build_mww -D SDKCONFIG=sdkconfig.mww -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.mww.defaults" build
idf.py -B build_mww -p COM3 app-flash
```

5. Pick it in Settings > Voice > Wake word, or `PATCH /api/v1/settings` with
   `{"voice_wake_word": "<id>"}` ([http-api.md](http-api.md)).

Manifest format (ESPHome v2): `probability_cutoff`, `feature_step_size`, `sliding_window_size`
and `tensor_arena_size` under `micro`. The on-device slider maps to the cutoff (78 = 0.95 for
the shipped "Hey Mr Clock").

## Training a model

Source of truth: [tools/wakeword_train/README.md](../../src/bedside_clock/tools/wakeword_train/README.md),
which has the full recipe, the WSL environment, the model history and seven lessons that each
cost a training run. The short version:

1. Record positives and negatives **through the clock's own microphone** using
   `tools/wake_clips.py` against the HTTP API (`session` mode, 8 s clips).
2. Cut utterances with real room audio around them (`--pad-s 0.5 --pad-after-s 0.4
   --no-normalize`); tight, normalised cuts teach the model that any onset is the wake word.
3. Add synthetic positives (Piper, Chatterbox voice clones, time-compressed to the speaker's
   tempo) so the model learns the phrase rather than one voice through one microphone.
4. Record negatives in the real environment (TV, own speech) and feed them at full length
   (`--aug-duration none`); TV through the same mic is what removed false accepts.
5. Train with `run_train.sh`, then **evaluate as a stream on held-out device audio**
   (`eval_stream.py`). Training-set metrics and per-clip evaluation were both misleading.
6. Write the manifest with `eval_model.py --manifest-out` and embed as above.

Budget: about 1.5 hours per model, mostly unattended, on a CPU-only TensorFlow install.
The engine's cost on the P4 is small (0.55 ms per 30 ms of audio, about 5 percent of one
core, measured in [sample_wakeword_bench](../../src/sample_wakeword_bench/README.md)).

## Related

- How detection fits the voice pipeline:
  [VOICE_AND_AI_PIPELINE.md](../../src/bedside_clock/docs/VOICE_AND_AI_PIPELINE.md) section 12.
- What the user says after the wake word: [VOICE.md](../../src/bedside_clock/docs/VOICE.md).

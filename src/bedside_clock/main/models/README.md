# Embedded wake-word models (microWakeWord)

Every model here is a pair `<id>.tflite` + `<id>.json` (ESPHome v2 manifest).
The pairs listed in `BEDSIDE_WAKE_MODELS` in `main/CMakeLists.txt` are embedded
into the app (EMBED_FILES) and offered in **Settings > Voice > Wake word** and
through the HTTP API (`voice_wake_word`, options in `voice_wake_words`). They
are used by `main/wake_mww.cpp` only when `CONFIG_BEDSIDE_WAKE_MWW=y`
(`main/Kconfig.projbuild`, `sdkconfig.mww.defaults`, the `build_mww` variant);
the default WakeNet build offers just "Hi ESP" (`wn9_hiesp`).

The manifest:

```json
{"wake_word": "Hi ESP", "model": "hi_esp.tflite",
 "micro": {"probability_cutoff": 0.95, "feature_step_size": 10,
           "sliding_window_size": 5, "tensor_arena_size": 26080}}
```

`wake_word` is the name shown in the picker; `<id>` (lowercase snake_case, the
file stem) is what settings and the API store. Switching models is live: the
detect task builds the new interpreter before freeing the old one.

## Adding a model ("Hey Mr Clock" etc.)

1. Copy the trained files in as `models/<id>.tflite` + `models/<id>.json`
   (`tools/wakeword_train/`: `trained_models/<name>/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite`
   + the manifest written by `eval_model.py --manifest-out`).
2. Append `<id>` to `set(BEDSIDE_WAKE_MODELS ...)` in `main/CMakeLists.txt`.
3. Rebuild the `build_mww` variant and `idf.py -B build_mww -p COMx app-flash`
   (the model partition is not involved).

The verifier's accept list (`k_wake_accept[]` in `main/voice.c`) is a union of
every phrase's spellings; extend it when a new phrase is added.

## Current models

- **`hey_mr_clock`** = `mrclock_v1d` (2026-09-20 16:20): the user's phrase.
  Positives: 88 device recordings (re-cut with 0.5 s of real room audio around
  each utterance, no normalisation - tight normalised cuts taught v1a/v1b to
  fire on any onset), 554 Chatterbox clones of the user's own voice (tempo
  1.25-1.9x), 12 k Piper US positives. Negatives: 24 min of TV through the
  device mic, 2 min device speech, 195 cloned confusables, FMA + AudioSet audio
  (all full length, `--aug-duration none`), plus the HF sets. Stream eval on 22
  held-out device utterances vs 5 min held-out TV + room + speech: **0 FA/h at
  cutoff >= 0.95** (v1c: 116/h), music 0/h, recall 64 % (the noisy-pass
  utterances are the misses). Use slider 78 (= cutoff 0.95). v1e (positives
  augmented with the TV recordings as backgrounds) is meant to recover recall.

- **`hi_esp`** = `hi_esp_v0` (2026-09-20): "Hi ESP" trained by
  `tools/wakeword_train/run_v0.sh` on 15.2 k Piper LibriTTS-R (US, multi-speaker)
  positives only, 6000 steps; a pipeline-validation model, not yet the
  Australian-male one. Training-set numbers (dinner_party_eval ambient):
  cutoff 0.95 -> 12 % false rejects, 0.19 false accepts/h; 0.99 -> 20 % / 0.

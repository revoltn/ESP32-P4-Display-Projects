# Training a wake word for bedside_clock (microWakeWord)

How the "Hey Mr Clock" model on the clock was made, what went wrong on the way,
and how to make the next one. The engine side (tflite-micro on the P4, model
registry, picker) is documented in `../../main/models/README.md` and
`../../VOICE_AND_AI_PIPELINE.md` §12; this file is the data + training side.

## TL;DR recipe (one more model, ~1.5 h, mostly unattended)

```
# 1. record through the CLOCK (API key in Settings > API; device IP from the clock face)
python tools/wake_clips.py --host <device-ip> --key <api-key> --out <wake_data dir> \
       session --minutes 4 --seconds 8 --label <phrase>_pos        # say the phrase every ~3 s
python tools/wake_clips.py ... session --minutes 25 --label tv_neg  # TV on, nobody talks to it
python tools/wake_clips.py ... session --minutes 2  --label speech_neg   # talk, no phrase

# 2. in WSL (~/mww, see "Environment"): cut utterances, build features, train, evaluate
python tools/split_utterances.py --in wake_data/<phrase>_pos --out data/device/<phrase>_utt \
       --pad-s 0.5 --pad-after-s 0.4 --no-normalize          # hold ~20 % out for eval
python tools/make_features.py --positives <utt dir(s)> --out work/<run>/features/device_positive \
       --train-repeat 8 --rirs data/mit_rirs --backgrounds data/fma_16k data/audioset_16k
python tools/make_features.py --positives data/device/tv_neg --out work/<run>/features/device_tv_neg \
       --train-repeat 2 --aug-duration none --rirs ... --backgrounds ...   # negatives: FULL length
bash tools/run_train.sh <run> tools/training_parameters_<run>.yaml
python tools/eval_stream.py --model work/<run>/trained_models/<run>/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite \
       --positives <held-out utterances> --negatives <held-out TV> data/device/room_ambient_1 \
       --ambience data/device/room_ambient_1
python tools/eval_model.py --model <same> --wake-word "Hey Mr Clock" --cutoff 0.95 --manifest-out hey_mr_clock.json

# 3. ship: copy .tflite + .json to main/models/<id>.*, make sure <id> is in BEDSIDE_WAKE_MODELS
#    (main/CMakeLists.txt), rebuild the build_mww variant, idf.py -B build_mww -p COMx app-flash
```

The worked examples are the orchestration scripts in `wsl_scripts/` (run inside WSL:
`wsl_setup.sh` builds the environment, `wsl_data2.sh` fetches RIRs/backgrounds,
`wsl_gen_mrclock.sh` generates Piper + Chatterbox data, `wsl_train_mrclock_c/d/e.sh`
are the v1c/v1d/v1e runs end to end, `wsl_eval_stream.sh` / `wsl_split_recall.sh`
the evaluations). They stage files from a Windows-side folder given in the `SP`
environment variable (`SP=/mnt/c/... bash wsl_scripts/<script>`); they refuse to run
without it. The `training_parameters_*.yaml` files assume the WSL layout `~/mww`
and spell it out as `/home/USER/mww/...`: replace `USER` with your login (or edit
every `features_dir`) before training. `training_parameters_mrclock_v1d.yaml` is the
recipe of the model on the device.

## Files

| File | Purpose |
|---|---|
| `../wake_clips.py` | PC side of the device API: `session` (back-to-back 8 s recordings), `record` (guided single clips), `pull` (wake clips with the verifier's verdict = hard negatives), `status`, `clear` |
| `split_utterances.py` | energy-based cutting of session recordings into one-utterance WAVs. **Use `--pad-s 0.5 --pad-after-s 0.4 --no-normalize`** (see lessons) |
| `record_ref.py` | headset recording for voice-clone references (not needed if device utterances exist: `gen_mrclock.sh` built references by concatenating 12 device utterances) |
| `gen_chatterbox.py` | Chatterbox (MIT) zero-shot clones of the reference voice saying the phrase / confusables, GPU. Renders slowly (~2 s per phrase) - compress with `make_features.py --tempo 1.25 1.9` |
| `simple_clips.py` | soundfile-based replacement for microwakeword's `Clips` (HF `datasets` 5.x needs torchcodec + FFmpeg, absent in the sandboxed WSL) |
| `make_features.py` | augmentation (RIR, backgrounds at -5..10 dB SNR, EQ/pitch/distortion/gain) + spectrogram RaggedMmaps for training/validation/testing. Options: `--train-repeat`, `--tempo MIN MAX`, `--aug-duration 3.2|none` |
| `training_parameters_*.yaml` | microWakeWord training configs; the `features:` list carries the sampling/penalty weights per set |
| `run_train.sh <name> <yaml>` | trains `mixednet` (the reference notebook's architecture), resumes from checkpoint, exports the int8 streaming tflite |
| `eval_stream.py` | **the evaluation that matters**: device-like continuous stream, recall on held-out device utterances, FA/h on held-out device audio, ignoring file joins |
| `eval_model.py` | per-clip evaluation (misleading for recall, see lessons) + manifest writer |
| `clip_stats.py` | level/duration sanity check of a folder of WAVs |

## Environment (WSL Ubuntu-24.04, `~/mww`)

Created 2026-09-20 with `uv` (no sudo, no C compiler in the distro):

- `venv-tf`: Python 3.11, TensorFlow 2.21 (**CPU** - no official wheel for the RTX 5070's sm_120;
  the tiny ~30 k-parameter model trains in ~45 min), `micro-wake-word` (editable clone),
  pymicro-features, ai-edge-litert, tensorboard, librosa, soundfile.
- `venv-tts`: PyTorch 2.11+cu128 (works on the 5070), chatterbox-tts, piper-sample-generator
  (run from its repo root: `cd piper-sample-generator && python -m piper_sample_generator ...`;
  needs the model's `.json` next to the `.pt`, copy it from `piper-sample-generator/models/`;
  install with `webrtcvad-wheels` first).
- `data/`: HF `kahrendt/microwakeword` negative feature sets (speech, dinner_party, no_speech,
  dinner_party_eval; 25 GB), `mit_rirs` (270), `fma_16k` (210), `audioset_16k` (500),
  `generated/` (Piper + Chatterbox output), `device/` (everything recorded through the clock,
  mirrored from the Windows-side `wake_data` folder that `wake_clips.py --out` writes).

## Model history ("Hey Mr Clock", 2026-09-20)

Evaluation = `eval_stream.py`, 22 held-out device utterances (15 quiet, 7 with TV on) vs
5 min held-out TV + 3 min room + 2 min own speech, all through the device mic.

| run | positives | negatives added | quiet recall | TV-on recall | FA/h on TV+room+speech |
|---|---|---|---|---|---|
| v1a | 88 device (tight cut, normalised) ×3 + 12 k Piper | 2 min device speech | (eval artefacts) | | fires on every onset |
| v1b | + 404 clones, device weight 1 | same | 93 % | ~70 % | ~300 (onsets + TV) |
| v1c | device re-cut with 0.5 s context, 554 clones, 195 cloned confusables | same | 93 % | 43-71 % | 116 |
| **v1d (on device)** | as v1c | **+ 24 min device TV, + music/AudioSet, all full length** | **93 %** | 0-14 % | **0 at cutoff ≥ 0.95** |
| v1e | v1d positives re-augmented with TV backgrounds | as v1d | 93 % | 14-29 % | 49 |

`hi_esp` (v0) is Piper-only (15 k US clips), never trained on the user; it worked for the user
in practice and is kept in the picker as the fallback.

## Lessons (each cost a training run)

1. **Evaluate on device audio in a stream, never trust training-set metrics.** v1a reported 6.5 %
   FRR / 0 FA on its own splits and was unusable. `eval_model.py` per-clip evaluation with
   interpreter resets is also misleading: many models emit p=1.0 for the first ~1.2 s after a
   reset, which the device's 100-slice cool-off hides. `eval_stream.py` mimics the device.
2. **Tightly cut, peak-normalised utterances teach the model "step in the noise floor => wake".**
   v1a/v1b fired on every file join and clip start. Cut with real room audio around the
   utterance (`--pad-s 0.5 --pad-after-s 0.4`) and keep the original level (`--no-normalize`).
3. **The augmenter keeps only the LAST 3.2 s of a longer clip.** Negatives recorded as 8 s
   sessions lost 60 % of their audio until `--aug-duration none`; the trainer's
   `truncation_strategy: random` then samples 1.5 s windows across the whole clip.
4. **One speaker + one channel overfits fast.** Device positives at weight 3 with few device
   negatives gave a model that recognised "the author through this mic" rather than the phrase. Let
   the multi-speaker sets (Piper, clones) carry the phrase at weight 2 and keep device positives
   at 1-1.5; give device-channel negatives (TV, own speech) weights 6-8.
5. **What kills TV false accepts is TV through the same mic as negatives** (v1c -> v1d: 116 -> 0
   FA/h). What it costs is recall over TV; mixing TV under the positives (v1e) did not buy it
   back. The remaining lever is real "phrase over TV" recordings (only 26 exist).
6. Chatterbox renders the phrase in ~2 s versus the user's 0.9 s: time-compress clones
   (`--tempo 1.25 1.9`). Piper's LibriTTS-R clips are already ~0.9 s.
7. The whisper verifier (`voice_wake_verify`) is a cheap backstop while a model is immature; the
   user turned it off once v1d proved quiet.

## Next model (when the TV-on gap matters)

Record a 5-min TV-on phrase session (`session --label hey_mr_clock_tv_pos`), cut it with the
rule above, add it as a positive set at weight 2-3 alongside v1d's sets, train `mrclock_v1f`
(`training_parameters_mrclock_v1d.yaml` + the new set), and compare with `eval_stream.py` on
the same held-out data. A larger `mixednet` (e.g. `--pointwise_filters "96,96,96,96"`) is
the second lever; the P4 has headroom (0.55 ms per invoke today).

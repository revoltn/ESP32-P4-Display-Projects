#!/usr/bin/env bash
# v1c: re-cut device utterances (0.5 s real context, no normalisation), full clone + confusable sets.
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd ~/mww
PY=~/mww/venv-tf/bin/python
W=~/mww/work/mrclock_v1
# Windows-side staging folder (tools/ and clips copied from the repo), e.g.
#   SP=/mnt/c/Users/<you>/bedside_staging bash wsl_scripts/<this script>
SP="${SP:?set SP to the folder holding the staged tools/clips (see README)}"
F='cudart\|absl\|XNNPACK\|cpu_feature\|AVX2\|cuda_executor\|cuDNN'
log() { echo "[$(date +%H:%M:%S)] $*"; }

# stop the auto-queued v1 run (it would use the old tight-cut device features) and any generation leftovers
pkill -f train_mrclock.sh; pkill -f 'run_train.sh mrclock_v1 '; sleep 1
while pgrep -f gen_mrclock.sh >/dev/null; do sleep 20; done
log "clones $(ls data/generated/mrclock_clone | wc -l), confusables $(ls data/generated/mrclock_confusables | wc -l)"

# re-cut from the raw 8 s session recordings
for s in hey_mr_clock_device_1 hey_mr_clock_device_noisy_1; do cp -r /mnt/u/Dev/ESP32-P4/wake_data/$s data/device/ 2>/dev/null; done
rm -rf data/device/pos_b data/device/pos_noisy_b data/device/train_pos_b data/device/holdout_pos_b
$PY tools/split_utterances.py --in data/device/hey_mr_clock_device_1 --out data/device/pos_b --pad-s 0.5 --pad-after-s 0.4 --no-normalize 2>&1 | tail -1
$PY tools/split_utterances.py --in data/device/hey_mr_clock_device_noisy_1 --out data/device/pos_noisy_b --pad-s 0.5 --pad-after-s 0.4 --no-normalize --margin-db 9 2>&1 | tail -1
mkdir -p data/device/train_pos_b data/device/holdout_pos_b
i=0
for f in data/device/pos_b/*.wav data/device/pos_noisy_b/*.wav; do
  i=$((i+1)); if [ $((i % 5)) -eq 0 ]; then cp "$f" data/device/holdout_pos_b/; else cp "$f" data/device/train_pos_b/; fi
done
log "device positives (b): train $(ls data/device/train_pos_b | wc -l), holdout $(ls data/device/holdout_pos_b | wc -l)"

feat() { local name=$1 rep=$2; shift 2
  local extra=(); while [ "$1" != "--" ]; do extra+=("$1"); shift; done; shift
  if [ -d "$W/features/$name/training" ]; then log "features/$name exists"; return; fi
  log "features/$name ..."
  $PY tools/make_features.py --positives "$@" --out "$W/features/$name" --train-repeat "$rep" "${extra[@]}" \
      --rirs data/mit_rirs --backgrounds data/fma_16k data/audioset_16k 2>&1 | grep -v "$F"
}
feat device_positive_b 8 -- data/device/train_pos_b
feat clone_positive    2 --tempo 1.25 1.9 -- data/generated/mrclock_clone
feat clone_confusables 2 --tempo 1.25 1.9 -- data/generated/mrclock_confusables

log "training v1c"
bash tools/run_train.sh mrclock_v1c tools/training_parameters_mrclock_v1c.yaml 2>&1 | grep -a 'training exit\|tflite\|Cutoff\|AUC\|Error\|Traceback'

M=~/mww/work/mrclock_v1c/trained_models/mrclock_v1c/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite
if [ -f "$M" ]; then
  log "stream evaluation: held-out device clips (b) vs device room + speech"
  $PY tools/eval_stream.py --model "$M" --positives data/device/holdout_pos_b \
      --negatives data/device/room_ambient_1 data/device/random_speech_neg_1 --ambience data/device/room_ambient_1 2>&1 | grep -v "$F"
  log "stream evaluation: old tight-cut holdout (comparable to v1a/v1b numbers)"
  $PY tools/eval_stream.py --model "$M" --positives data/device/holdout_pos \
      --negatives data/device/room_ambient_1 --ambience data/device/room_ambient_1 --cutoffs 0.9 0.95 0.97 2>&1 | grep -v "$F" | grep -v '^model'
  log "stream evaluation: music/AudioSet (optimistic)"
  $PY tools/eval_stream.py --model "$M" --negatives data/fma_16k data/audioset_16k --max-neg-files 250 \
      --ambience data/device/room_ambient_1 --cutoffs 0.85 0.9 0.95 0.97 0.99 2>&1 | grep -v "$F" | grep -v '^model'
  mkdir -p "$SP/mrclock_v1c"; cp "$M" "$SP/mrclock_v1c/hey_mr_clock.tflite"
  $PY tools/eval_model.py --model "$M" --wake-word "Hey Mr Clock" --cutoff 0.95 --manifest-out "$SP/mrclock_v1c/hey_mr_clock.json" 2>&1 | grep manifest
  sed -i 's/"wake_word.tflite"/"hey_mr_clock.tflite"/' "$SP/mrclock_v1c/hey_mr_clock.json"
fi
log "V1C DONE"

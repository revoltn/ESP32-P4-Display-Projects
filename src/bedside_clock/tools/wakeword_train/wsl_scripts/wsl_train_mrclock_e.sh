#!/usr/bin/env bash
# v1e: positives re-augmented with the device TV recordings as backgrounds (+ music/AudioSet).
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
BG="data/fma_16k data/audioset_16k data/device/tv_train"

feat() { local name=$1 rep=$2; shift 2
  local extra=(); while [ "$1" != "--" ]; do extra+=("$1"); shift; done; shift
  if [ -d "$W/features/$name/training" ]; then log "features/$name exists"; return; fi
  log "features/$name ..."
  $PY tools/make_features.py --positives "$@" --out "$W/features/$name" --train-repeat "$rep" "${extra[@]}" \
      --rirs data/mit_rirs --backgrounds $BG 2>&1 | grep -v "$F"
}
feat device_positive_tv 8 -- data/device/train_pos_b
feat clone_positive_tv  2 --tempo 1.25 1.9 -- data/generated/mrclock_clone
feat piper_positive_tv  1 -- data/generated/mrclock_piper

log "training v1e"
bash tools/run_train.sh mrclock_v1e tools/training_parameters_mrclock_v1e.yaml 2>&1 | grep -a 'training exit\|tflite\|Cutoff\|AUC\|Error\|Traceback'

M=~/mww/work/mrclock_v1e/trained_models/mrclock_v1e/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite
if [ -f "$M" ]; then
  log "v1e: held-out device positives vs HELD-OUT TV + room + speech"
  $PY tools/eval_stream.py --model "$M" --positives data/device/holdout_pos_b \
      --negatives data/device/tv_holdout data/device/room_ambient_1 data/device/random_speech_neg_1 \
      --ambience data/device/room_ambient_1 --cutoffs 0.8 0.9 0.93 0.95 0.97 0.99 2>&1 | grep -v "$F"
  log "v1e: music/AudioSet"
  $PY tools/eval_stream.py --model "$M" --negatives data/fma_16k data/audioset_16k --max-neg-files 250 \
      --ambience data/device/room_ambient_1 --cutoffs 0.9 0.95 0.97 2>&1 | grep -v "$F" | grep -v '^model'
  mkdir -p "$SP/mrclock_v1e"; cp "$M" "$SP/mrclock_v1e/hey_mr_clock.tflite"
  $PY tools/eval_model.py --model "$M" --wake-word "Hey Mr Clock" --cutoff 0.95 --manifest-out "$SP/mrclock_v1e/hey_mr_clock.json" 2>&1 | grep manifest
  sed -i 's/"wake_word.tflite"/"hey_mr_clock.tflite"/' "$SP/mrclock_v1e/hey_mr_clock.json"
fi
log "V1E DONE"

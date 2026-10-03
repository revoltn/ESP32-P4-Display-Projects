#!/usr/bin/env bash
# v1d: v1c + device-channel negatives (TV session, optional near-miss session) + music_neg.
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

# device negatives from the Windows side; the LAST 5 min (37 clips) of TV are held out for eval
rm -rf data/device/tv_neg_1 data/device/tv_train data/device/tv_holdout
cp -r /mnt/u/Dev/ESP32-P4/wake_data/tv_neg_1 data/device/
mkdir -p data/device/tv_train data/device/tv_holdout
n=$(ls data/device/tv_neg_1/*.wav | wc -l); k=$((n - 37)); [ $k -lt 10 ] && k=$((n * 2 / 3))
ls data/device/tv_neg_1/*.wav | head -n $k | xargs -I{} cp {} data/device/tv_train/
ls data/device/tv_neg_1/*.wav | tail -n +$((k + 1)) | xargs -I{} cp {} data/device/tv_holdout/
log "TV clips: $n total, train $(ls data/device/tv_train | wc -l), holdout $(ls data/device/tv_holdout | wc -l)"
HAVE_NM=0
if [ -d /mnt/u/Dev/ESP32-P4/wake_data/near_miss_neg_1 ]; then
  rm -rf data/device/near_miss_neg_1; cp -r /mnt/u/Dev/ESP32-P4/wake_data/near_miss_neg_1 data/device/; HAVE_NM=1
  log "near-miss clips: $(ls data/device/near_miss_neg_1 | wc -l)"
fi

feat() { local name=$1 rep=$2; shift 2
  local extra=(); while [ "$1" != "--" ]; do extra+=("$1"); shift; done; shift
  if [ -d "$W/features/$name/training" ]; then log "features/$name exists"; return; fi
  log "features/$name ..."
  $PY tools/make_features.py --positives "$@" --out "$W/features/$name" --train-repeat "$rep" "${extra[@]}" \
      --rirs data/mit_rirs --backgrounds data/fma_16k data/audioset_16k 2>&1 | grep -v "$F"
}
# negatives keep their FULL length (--aug-duration none): the augmenter would otherwise keep
# only the last 3.2 s of each 8 s recording; the trainer's random 1.5 s windows cover the rest
rm -rf "$W/features/device_tv_neg" "$W/features/device_speech_neg" "$W/features/music_neg"
feat device_tv_neg     2 --aug-duration none -- data/device/tv_train
feat device_speech_neg 4 --aug-duration none -- data/device/random_speech_neg_1
feat music_neg         1 --aug-duration none -- data/fma_16k data/audioset_16k
YAML=tools/training_parameters_mrclock_v1d.yaml
cp $YAML /tmp/v1d.yaml
if [ $HAVE_NM -eq 1 ]; then
  rm -rf "$W/features/device_confusables"; feat device_confusables 6 --aug-duration none -- data/device/near_miss_neg_1
else
  # drop the device_confusables block (6 lines) from the yaml
  $PY - <<'EOF'
import re
y = open("/tmp/v1d.yaml").read()
y = re.sub(r"  - features_dir: /home/USER/mww/work/mrclock_v1/features/device_confusables\n(?:    .*\n){5}", "", y)
open("/tmp/v1d.yaml", "w").write(y)
print("device_confusables entry removed (no near-miss session yet)")
EOF
fi

log "training v1d"
bash tools/run_train.sh mrclock_v1d /tmp/v1d.yaml 2>&1 | grep -a 'training exit\|tflite\|Cutoff\|AUC\|Error\|Traceback'

M=~/mww/work/mrclock_v1d/trained_models/mrclock_v1d/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite
if [ -f "$M" ]; then
  for name in mrclock_v1d mrclock_v1c; do
    MM=~/mww/work/$name/trained_models/$name/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite
    log "$name: held-out device positives vs HELD-OUT TV (5 min) + room + speech"
    $PY tools/eval_stream.py --model "$MM" --positives data/device/holdout_pos_b \
        --negatives data/device/tv_holdout data/device/room_ambient_1 data/device/random_speech_neg_1 \
        --ambience data/device/room_ambient_1 --cutoffs 0.8 0.9 0.93 0.95 0.97 0.99 2>&1 | grep -v "$F"
  done
  log "v1d: music/AudioSet (now trained as negatives)"
  $PY tools/eval_stream.py --model "$M" --negatives data/fma_16k data/audioset_16k --max-neg-files 250 \
      --ambience data/device/room_ambient_1 --cutoffs 0.9 0.95 0.97 2>&1 | grep -v "$F" | grep -v '^model'
  mkdir -p "$SP/mrclock_v1d"; cp "$M" "$SP/mrclock_v1d/hey_mr_clock.tflite"
  $PY tools/eval_model.py --model "$M" --wake-word "Hey Mr Clock" --cutoff 0.95 --manifest-out "$SP/mrclock_v1d/hey_mr_clock.json" 2>&1 | grep manifest
  sed -i 's/"wake_word.tflite"/"hey_mr_clock.tflite"/' "$SP/mrclock_v1d/hey_mr_clock.json"
fi
log "V1D DONE"

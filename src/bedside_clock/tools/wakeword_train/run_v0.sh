#!/usr/bin/env bash
# v0 pipeline validation: Piper positives -> features -> mixednet -> streaming
# int8 tflite. Waits for the Piper generation to finish first.
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
TOOLS="$(cd "$(dirname "$0")" && pwd)"
WORK=~/mww/work/hi_esp_v0
PY=~/mww/venv-tf/bin/python
mkdir -p "$WORK/features" && cd "$WORK"
log() { echo "[$(date +%H:%M:%S)] $*"; }

while pgrep -f gen_piper.sh >/dev/null; do sleep 30; done
log "piper done: $(find ~/mww/data/generated/hi_esp_piper -name '*.wav' | wc -l) clips"

if [ ! -d features/hi_esp_piper/training ]; then
  log "features..."
  $PY "$TOOLS/make_features.py" --positives ~/mww/data/generated/hi_esp_piper \
      --out features/hi_esp_piper --rirs ~/mww/data/mit_rirs \
      --backgrounds ~/mww/data/fma_16k ~/mww/data/audioset_16k 2>&1 | grep -v 'cudart_stub\|cpu_feature_guard\|absl::\|AVX2 FMA'
fi
cp "$TOOLS/training_parameters_v0.yaml" training_parameters.yaml

log "training..."
cd "$WORK" && PYTHONPATH=~/mww/micro-wake-word $PY -m microwakeword.model_train_eval \
  --training_config training_parameters.yaml \
  --train 1 --restore_checkpoint 1 \
  --test_tf_nonstreaming 0 --test_tflite_nonstreaming 0 --test_tflite_nonstreaming_quantized 0 \
  --test_tflite_streaming 0 --test_tflite_streaming_quantized 1 \
  --use_weights best_weights \
  mixednet --pointwise_filters "64,64,64,64" --repeat_in_block "1,1,1,1" \
  --mixconv_kernel_sizes "[5], [7,11], [9,15], [23]" --residual_connection "0,0,0,0" \
  --first_conv_filters 32 --first_conv_kernel_size 5 --stride 3 \
  2>&1 | grep -v 'cudart_stub\|cpu_feature_guard\|absl::\|AVX2 FMA'
log "training exit: $?"
find trained_models -name '*.tflite' -exec ls -la {} \;

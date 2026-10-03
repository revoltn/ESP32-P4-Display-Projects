#!/usr/bin/env bash
# Generic microWakeWord training run.
#   run_train.sh <name> <training_parameters.yaml> [--steps N]
# Work dir: ~/mww/work/<name>; the yaml's features_dir entries are resolved
# relative to it (or absolute). Produces
#   ~/mww/work/<name>/trained_models/<train_dir>/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite
# Feature sets must already exist (make_features.py). Resumes from checkpoint.
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
NAME=${1:?name}; YAML=$(realpath "${2:?yaml}")
WORK=~/mww/work/$NAME
PY=~/mww/venv-tf/bin/python
mkdir -p "$WORK" && cd "$WORK"
cp "$YAML" training_parameters.yaml
log() { echo "[$(date +%H:%M:%S)] $*"; }
log "training $NAME in $WORK"
PYTHONPATH=~/mww/micro-wake-word $PY -m microwakeword.model_train_eval \
  --training_config training_parameters.yaml \
  --train 1 --restore_checkpoint 1 \
  --test_tf_nonstreaming 0 --test_tflite_nonstreaming 0 --test_tflite_nonstreaming_quantized 0 \
  --test_tflite_streaming 0 --test_tflite_streaming_quantized 1 \
  --use_weights best_weights \
  mixednet --pointwise_filters "64,64,64,64" --repeat_in_block "1,1,1,1" \
  --mixconv_kernel_sizes "[5], [7,11], [9,15], [23]" --residual_connection "0,0,0,0" \
  --first_conv_filters 32 --first_conv_kernel_size 5 --stride 3 \
  2>&1 | grep -v 'cudart_stub\|cpu_feature_guard\|absl::\|AVX2 FMA\|cuda_executor\|cuDNN'
log "training exit: ${PIPESTATUS[0]}"
find trained_models -name '*.tflite' -exec ls -la {} \;

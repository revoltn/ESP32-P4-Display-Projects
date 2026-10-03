#!/usr/bin/env bash
cd ~/mww
# Windows-side staging folder (tools/ and clips copied from the repo), e.g.
#   SP=/mnt/c/Users/<you>/bedside_staging bash wsl_scripts/<this script>
SP="${SP:?set SP to the folder holding the staged tools/clips (see README)}"
sed 's/\r$//' "$SP/wakeword_train/eval_stream.py" > tools/eval_stream.py
F='cudart\|absl\|XNNPACK\|cpu_feature\|AVX2\|cuda_executor\|cuDNN'
PY=venv-tf/bin/python
for name in mrclock_v1b mrclock_v1a; do
  M=work/$name/trained_models/$name/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite
  echo "=== $name: held-out device positives / device room+speech negatives ==="
  $PY tools/eval_stream.py --model $M --positives data/device/holdout_pos \
      --negatives data/device/room_ambient_1 data/device/random_speech_neg_1 --ambience data/device/room_ambient_1 2>&1 | grep -v "$F"
  echo "=== $name: music + AudioSet backgrounds (seen in augmentation -> optimistic) ==="
  $PY tools/eval_stream.py --model $M --negatives data/fma_16k data/audioset_16k --max-neg-files 250 \
      --ambience data/device/room_ambient_1 --cutoffs 0.85 0.9 0.95 0.97 0.99 2>&1 | grep -v "$F" | grep -v '^model'
done
echo "=== control hi_esp_v0 on the same device negatives (and mrclock positives: expect ~0 recall) ==="
$PY tools/eval_stream.py --model work/hi_esp_v0/trained_models/hi_esp_v0/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite \
    --positives data/device/holdout_pos --negatives data/device/room_ambient_1 data/device/random_speech_neg_1 \
    --ambience data/device/room_ambient_1 --cutoffs 0.9 0.95 2>&1 | grep -v "$F"

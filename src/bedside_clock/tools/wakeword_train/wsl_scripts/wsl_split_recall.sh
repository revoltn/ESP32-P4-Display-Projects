#!/usr/bin/env bash
cd ~/mww
F='cudart\|absl\|XNNPACK\|cpu_feature\|AVX2\|cuda_executor\|cuDNN'
mkdir -p /tmp/ho_clean /tmp/ho_noisy; rm -f /tmp/ho_clean/* /tmp/ho_noisy/*
for f in data/device/holdout_pos_b/*.wav; do
  b=$(basename "$f"); ts=${b:9:6}
  # noisy pass ran 12:44:00-12:47:00 (clips 124400-124700); clean pass 12:36-12:40
  if [ "$ts" -ge 124400 ]; then cp "$f" /tmp/ho_noisy/; else cp "$f" /tmp/ho_clean/; fi
done
echo "holdout clean: $(ls /tmp/ho_clean | wc -l), noisy: $(ls /tmp/ho_noisy | wc -l)"
for name in mrclock_v1d mrclock_v1c mrclock_v1e; do
  M=work/$name/trained_models/$name/tflite_stream_state_internal_quant/stream_state_internal_quant.tflite
  for set in clean noisy; do
    echo "=== $name / $set ==="
    venv-tf/bin/python tools/eval_stream.py --model $M --positives /tmp/ho_$set --negatives data/device/room_ambient_1 \
        --ambience data/device/room_ambient_1 --cutoffs 0.9 0.95 2>&1 | grep -E '^ +0\.9'
  done
done

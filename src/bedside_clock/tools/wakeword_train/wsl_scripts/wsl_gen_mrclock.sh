#!/usr/bin/env bash
# "Hey Mr Clock" synthetic positives, run sequentially on the GPU:
#   1. Piper LibriTTS-R (US multi-speaker, robustness filler)        -> data/generated/mrclock_piper
#   2. Chatterbox clones of the user's OWN voice, references built by
#      concatenating clean device utterances                          -> data/generated/mrclock_clone
#   3. Chatterbox confusables in the same voice (hard negatives)     -> data/generated/mrclock_confusables
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd ~/mww
PY=~/mww/venv-tts/bin/python
log() { echo "[$(date +%H:%M:%S)] $*"; }

# --- copy device data ---
mkdir -p data/device
cp -r /mnt/u/Dev/ESP32-P4/wake_data/hey_mr_clock_pos_1 data/device/ 2>/dev/null
cp -r /mnt/u/Dev/ESP32-P4/wake_data/hey_mr_clock_pos_noisy_1 data/device/ 2>/dev/null
cp -r /mnt/u/Dev/ESP32-P4/wake_data/random_speech_neg_1 data/device/ 2>/dev/null
log "device data: $(ls data/device/hey_mr_clock_pos_1 | wc -l) clean, $(ls data/device/hey_mr_clock_pos_noisy_1 | wc -l) noisy, $(ls data/device/random_speech_neg_1 | wc -l) neg"

# --- references for cloning: 3 x ~12 s from the clean utterances ---
mkdir -p data/refs/device_voice
$PY - <<'EOF'
import glob, numpy as np, soundfile as sf
fs = sorted(glob.glob("data/device/hey_mr_clock_pos_1/*.wav"))
gap = np.zeros(int(0.35 * 16000), dtype=np.float32)
for r in range(3):
    parts = []
    for f in fs[r::3][:12]:
        y, sr = sf.read(f, dtype="float32"); parts += [y, gap]
    y = np.concatenate(parts)[: 14 * 16000]
    sf.write(f"data/refs/device_voice/ref{r+1}.wav", (y / (np.abs(y).max() + 1e-6) * 0.8 * 32767).astype(np.int16), 16000)
    print(f"ref{r+1}: {len(y)/16000:.1f} s")
EOF

# --- 1. Piper ---
OUT=data/generated/mrclock_piper; mkdir -p "$OUT"
if [ "$(find $OUT -name '*.wav' | wc -l)" -lt 5000 ]; then
  cd piper-sample-generator
  i=0
  for text in "Hey Mister Clock." "Hey, Mister Clock!" "Hey Mister Clock?"; do
    i=$((i+1)); log "piper [$i/3] '$text'"
    $PY -m piper_sample_generator "$text" --model ~/mww/data/en_US-libritts_r-medium.pt \
        --max-samples 4000 --batch-size 100 --max-speakers 900 \
        --length-scales 0.8 0.9 1.0 1.1 1.2 --noise-scales 0.333 0.667 --noise-scale-ws 0.6 0.8 1.0 \
        --output-dir ~/mww/$OUT/v$i 2>&1 | tail -n 2
  done
  cd ~/mww
fi
log "piper: $(find $OUT -name '*.wav' | wc -l) wavs"

# --- 2./3. Chatterbox ---
log "chatterbox positives"
$PY tools/gen_chatterbox.py --refs data/refs/device_voice \
    --phrases "Hey Mister Clock." "Hey, Mr. Clock!" "Hey Mister Clock?" "hey mister clock" \
    --per-ref 500 --out data/generated/mrclock_clone 2>&1 | grep -v Warning | tail -n 4
log "chatterbox confusables"
$PY tools/gen_chatterbox.py --refs data/refs/device_voice \
    --phrases "Hey Mister Clark." "Hey Mister Cook." "What's the clock?" "It's six o'clock." "Hey mate." "Mister." "Hey, hey." "Hey Mister Cloak." \
    --per-ref 160 --out data/generated/mrclock_confusables 2>&1 | grep -v Warning | tail -n 4
log "done: clones $(ls data/generated/mrclock_clone | wc -l), confusables $(ls data/generated/mrclock_confusables | wc -l)"

#!/usr/bin/env bash
# Training environment for the bedside_clock wake word (microWakeWord + Chatterbox)
# Runs as the normal user inside WSL Ubuntu-24.04 (no sudo): uv provides Python 3.11.
#   ~/mww/venv-tf   TensorFlow >= 2.18, microwakeword (editable), pymicro-features, datasets
#   ~/mww/venv-tts  PyTorch cu128 (Blackwell/sm_120), chatterbox-tts, piper-sample-generator
#   ~/mww/data      piper checkpoint, HF negative features, MIT RIRs, AudioSet + FMA backgrounds
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
mkdir -p ~/mww/data ~/mww/logs
cd ~/mww
log() { echo "[$(date +%H:%M:%S)] $*"; }

phase_tools() {
  if ! command -v uv >/dev/null 2>&1; then
    log "installing uv"; curl -LsSf https://astral.sh/uv/install.sh | sh
  fi
  export PATH="$HOME/.local/bin:$PATH"
  uv --version
  uv python install 3.11
  [ -d micro-wake-word ] || git clone --depth 1 https://github.com/OHF-Voice/micro-wake-word.git
  [ -d piper-sample-generator ] || git clone --depth 1 https://github.com/rhasspy/piper-sample-generator.git
}

phase_tf() {
  export PATH="$HOME/.local/bin:$PATH"
  [ -d venv-tf ] || uv venv --python 3.11 venv-tf
  uv pip install --python venv-tf/bin/python -e ./micro-wake-word
  uv pip install --python venv-tf/bin/python huggingface_hub soundfile librosa ipykernel
  venv-tf/bin/python - <<'EOF'
import tensorflow as tf, microwakeword, pymicro_features, numpy
print("TF", tf.__version__, "numpy", numpy.__version__, "GPUs", tf.config.list_physical_devices("GPU"))
EOF
}

phase_tts() {
  export PATH="$HOME/.local/bin:$PATH"
  [ -d venv-tts ] || uv venv --python 3.11 venv-tts
  # chatterbox pins an older torch; install it first, then force the cu128 wheels that know sm_120
  uv pip install --python venv-tts/bin/python chatterbox-tts
  uv pip install --python venv-tts/bin/python --upgrade torch torchaudio --index-url https://download.pytorch.org/whl/cu128
  uv pip install --python venv-tts/bin/python -e ./piper-sample-generator || \
    uv pip install --python venv-tts/bin/python piper-sample-generator
  uv pip install --python venv-tts/bin/python soundfile
  venv-tts/bin/python - <<'EOF'
import torch
print("torch", torch.__version__, "cuda", torch.version.cuda, "available", torch.cuda.is_available())
if torch.cuda.is_available():
    p = torch.cuda.get_device_properties(0); print(p.name, "sm", p.major, p.minor, f"{p.total_memory/2**30:.1f} GB")
    x = torch.randn(1024, 1024, device="cuda"); print("matmul ok", (x @ x).sum().item() != 0)
EOF
}

phase_data() {
  export PATH="$HOME/.local/bin:$PATH"
  cd ~/mww/data
  [ -f en_US-libritts_r-medium.pt ] || curl -L -o en_US-libritts_r-medium.pt \
    https://github.com/rhasspy/piper-sample-generator/releases/download/v2.0.0/en_US-libritts_r-medium.pt
  ~/mww/venv-tf/bin/python - <<'EOF'
from huggingface_hub import hf_hub_download
import zipfile, os
os.makedirs("negative_datasets", exist_ok=True)
for f in ["dinner_party.zip", "dinner_party_eval.zip", "no_speech.zip", "speech.zip"]:
    p = hf_hub_download(repo_id="kahrendt/microwakeword", filename=f, repo_type="dataset", local_dir="hf")
    tgt = "negative_datasets/" + f[:-4]
    if not os.path.isdir(tgt):
        print("unzip", f); zipfile.ZipFile(p).extractall("negative_datasets")
print(os.listdir("negative_datasets"))
EOF
  ~/mww/venv-tf/bin/python - <<'EOF'
# MIT room impulse responses -> mit_rirs/*.wav (16 kHz, 16-bit)
import os, numpy as np, soundfile as sf
from datasets import load_dataset, Audio
if not os.path.isdir("mit_rirs"):
    os.makedirs("mit_rirs")
    ds = load_dataset("davidscripka/MIT_environmental_impulse_responses", split="train", streaming=True)
    ds = ds.cast_column("audio", Audio(sampling_rate=16000))
    n = 0
    for row in ds:
        a = row["audio"]; name = os.path.basename(a.get("path") or f"rir_{n}.wav")
        sf.write(f"mit_rirs/{name}", (np.asarray(a["array"]) * 32767).astype(np.int16), 16000)
        n += 1
    print("rirs", n)
EOF
  ~/mww/venv-tf/bin/python - <<'EOF'
# Background audio: AudioSet balanced-train shard + FMA xsmall, converted to 16 kHz 16-bit wav
import os, glob, tarfile, zipfile, numpy as np, soundfile as sf, librosa
from huggingface_hub import hf_hub_download
def convert(src_glob, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    files = glob.glob(src_glob, recursive=True); n = 0
    for f in files:
        try:
            y, _ = librosa.load(f, sr=16000, mono=True)
        except Exception as e:
            continue
        if len(y) < 16000: continue
        sf.write(os.path.join(out_dir, os.path.splitext(os.path.basename(f))[0] + ".wav"), (np.clip(y, -1, 1) * 32767).astype(np.int16), 16000); n += 1
    print(out_dir, n, "files from", len(files))
if not os.path.isdir("audioset_16k"):
    p = hf_hub_download(repo_id="agkphysics/AudioSet", filename="data/bal_train09.tar", repo_type="dataset", local_dir="hf")
    os.makedirs("audioset_raw", exist_ok=True); tarfile.open(p).extractall("audioset_raw")
    convert("audioset_raw/**/*.flac", "audioset_16k")
if not os.path.isdir("fma_16k"):
    p = hf_hub_download(repo_id="mchl914/fma_xsmall", filename="fma_xs.zip", repo_type="dataset", local_dir="hf")
    os.makedirs("fma_raw", exist_ok=True); zipfile.ZipFile(p).extractall("fma_raw")
    convert("fma_raw/**/*.mp3", "fma_16k")
EOF
}

for ph in "$@"; do
  log "=== phase $ph start ==="
  if "phase_$ph" > ~/mww/logs/$ph.log 2>&1; then log "=== phase $ph OK ==="; else log "=== phase $ph FAILED (see ~/mww/logs/$ph.log) ==="; fi
done
log "all requested phases finished"

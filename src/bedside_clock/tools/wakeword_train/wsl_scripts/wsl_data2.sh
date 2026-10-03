#!/usr/bin/env bash
# Background/RIR audio for microWakeWord augmentation (second attempt):
#   mit_rirs/      271 room impulse responses, plain 16 kHz WAVs on HF
#   audioset_16k/  one balanced-train parquet shard (~690 MB, ~2 k clips) -> 16 kHz wav
#   fma_16k/       FMA x-small (8 k x 30 s mp3) -> 16 kHz wav
set -uo pipefail
export PATH="$HOME/.local/bin:$PATH"
cd ~/mww/data
PY=~/mww/venv-tf/bin/python
uv pip install --python $PY pyarrow 2>&1 | tail -1

$PY - <<'EOF'
import os, glob, shutil
from huggingface_hub import snapshot_download
if len(glob.glob("mit_rirs/*.wav")) < 200:
    p = snapshot_download(repo_id="davidscripka/MIT_environmental_impulse_responses", repo_type="dataset",
                          allow_patterns=["16khz/*.wav"], local_dir="hf/mit_rirs")
    os.makedirs("mit_rirs", exist_ok=True)
    n = 0
    for f in glob.glob(os.path.join(p, "16khz", "*.wav")):
        shutil.copy(f, "mit_rirs/"); n += 1
    print("mit_rirs", n)
else:
    print("mit_rirs already", len(glob.glob("mit_rirs/*.wav")))
EOF

$PY - <<'EOF'
import os, io, glob, zipfile, numpy as np, soundfile as sf, librosa
from huggingface_hub import hf_hub_download

def to16k(y, sr):
    if y.ndim > 1: y = y.mean(axis=1)
    if sr != 16000: y = librosa.resample(y.astype(np.float32), orig_sr=sr, target_sr=16000)
    return (np.clip(y, -1, 1) * 32767).astype(np.int16)

# --- AudioSet: one parquet shard, audio column holds flac bytes ---
if len(glob.glob("audioset_16k/*.wav")) < 500:
    import pyarrow.parquet as pq
    p = hf_hub_download(repo_id="agkphysics/AudioSet", filename="data/bal_train/00.parquet", repo_type="dataset", local_dir="hf")
    os.makedirs("audioset_16k", exist_ok=True)
    pf = pq.ParquetFile(p)
    print("audioset columns", pf.schema.names)
    n = bad = 0
    for batch in pf.iter_batches(batch_size=64):
        d = batch.to_pydict()
        col = "audio" if "audio" in d else [k for k in d if "audio" in k.lower()][0]
        ids = d.get("video_id") or d.get("id") or [None] * len(d[col])
        for a, vid in zip(d[col], ids):
            try:
                b = a["bytes"] if isinstance(a, dict) else a
                y, sr = sf.read(io.BytesIO(b), dtype="float32")
                pcm = to16k(y, sr)
                if len(pcm) < 16000: continue
                name = vid or f"clip{n}"
                sf.write(f"audioset_16k/{name}.wav", pcm, 16000); n += 1
            except Exception as e:
                bad += 1
    print("audioset_16k", n, "clips,", bad, "failed")
else:
    print("audioset_16k already", len(glob.glob("audioset_16k/*.wav")))

# --- FMA x-small ---
if len(glob.glob("fma_16k/*.wav")) < 500:
    p = hf_hub_download(repo_id="mchl914/fma_xsmall", filename="fma_xs.zip", repo_type="dataset", local_dir="hf")
    os.makedirs("fma_16k", exist_ok=True)
    n = bad = 0
    with zipfile.ZipFile(p) as z:
        names = [x for x in z.namelist() if x.lower().endswith(".mp3")]
        print("fma mp3 files in zip:", len(names))
        for x in names:
            try:
                y, sr = sf.read(io.BytesIO(z.read(x)), dtype="float32")
                pcm = to16k(y, sr)
                if len(pcm) < 16000: continue
                sf.write(f"fma_16k/{os.path.splitext(os.path.basename(x))[0]}.wav", pcm, 16000); n += 1
            except Exception as e:
                bad += 1
                if bad <= 3: print("fma decode failed:", x, type(e).__name__, str(e)[:100])
    print("fma_16k", n, "clips,", bad, "failed")
else:
    print("fma_16k already", len(glob.glob("fma_16k/*.wav")))
EOF
echo "=== data2 done ==="; du -sh mit_rirs audioset_16k fma_16k 2>/dev/null

#!/usr/bin/env python3
"""Cut long recordings (e.g. 8 s device session clips with the wake phrase said
every few seconds) into one-utterance WAVs for training.

Energy-based endpointing on 20 ms frames: an utterance is a run of frames above
an adaptive threshold (noise floor + margin) lasting 0.35-2.0 s; each is saved
with 150 ms of context either side. Utterances touching a clip edge are dropped.

  python split_utterances.py --in wake_clips/hey_mr_clock_session --out data/device/hey_mr_clock_pos
Review a few of the outputs; delete the ones that are not the phrase.
"""
import argparse, glob, os
import numpy as np
import soundfile as sf

SR = 16000
FRAME = SR // 50           # 20 ms

def split(path, out_dir, min_s, max_s, pad_s, margin_db, pad_after_s=None, normalize=True):
    if pad_after_s is None:
        pad_after_s = pad_s
    y, sr = sf.read(path, dtype="float32", always_2d=False)
    if y.ndim > 1:
        y = y.mean(axis=1)
    if sr != SR:
        import librosa
        y = librosa.resample(y, orig_sr=sr, target_sr=SR)
    n = len(y) // FRAME
    if n < 10:
        return 0
    frames = y[: n * FRAME].reshape(n, FRAME)
    rms_db = 20 * np.log10(np.sqrt((frames ** 2).mean(axis=1)) + 1e-6)
    floor = np.percentile(rms_db, 20)
    thr = floor + margin_db
    active = rms_db > thr
    # close gaps < 200 ms so a phrase with a short pause stays one utterance
    gap = int(0.2 * SR / FRAME)
    i = 0
    while i < n:
        if not active[i]:
            j = i
            while j < n and not active[j]:
                j += 1
            if 0 < i and j < n and j - i <= gap:
                active[i:j] = True
            i = j
        else:
            i += 1
    saved = 0
    i = 0
    base = os.path.splitext(os.path.basename(path))[0]
    while i < n:
        if not active[i]:
            i += 1
            continue
        j = i
        while j < n and active[j]:
            j += 1
        dur = (j - i) * FRAME / SR
        if min_s <= dur <= max_s and i > 0 and j < n:
            a = max(0, i * FRAME - int(pad_s * SR)); b = min(len(y), j * FRAME + int(pad_after_s * SR))
            seg = y[a:b]
            if normalize:
                seg = seg / (np.abs(seg).max() + 1e-6) * 0.8
            sf.write(os.path.join(out_dir, f"{base}_{saved:02d}.wav"), (np.clip(seg, -1, 1) * 32767).astype(np.int16), SR)
            saved += 1
        i = j
    return saved

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--in", dest="inp", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--min-s", type=float, default=0.35)
    ap.add_argument("--max-s", type=float, default=2.0)
    ap.add_argument("--pad-s", type=float, default=0.15, help="context kept BEFORE the utterance (s)")
    ap.add_argument("--pad-after-s", type=float, default=None, help="context kept AFTER (default = --pad-s)")
    ap.add_argument("--margin-db", type=float, default=12.0)
    ap.add_argument("--no-normalize", action="store_true",
                    help="keep the original level (recommended for training data: a peak-normalised, "
                         "tightly cut clip starts with a step in the noise floor that the model learns as a cue)")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    total = 0
    files = sorted(glob.glob(os.path.join(a.inp, "**", "*.wav"), recursive=True))
    for f in files:
        k = split(f, a.out, a.min_s, a.max_s, a.pad_s, a.margin_db, a.pad_after_s, not a.no_normalize)
        total += k
    print(f"{total} utterances from {len(files)} recordings -> {a.out}")

if __name__ == "__main__":
    main()

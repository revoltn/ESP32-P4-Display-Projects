#!/usr/bin/env python3
"""Quick level / duration statistics for a folder of WAVs: python clip_stats.py DIR [DIR ...]"""
import glob, os, sys
import numpy as np
import soundfile as sf

for d in sys.argv[1:]:
    files = sorted(glob.glob(os.path.join(d, "**", "*.wav"), recursive=True))
    if not files:
        print(d, ": no wavs"); continue
    peaks, rmss, durs = [], [], []
    for f in files:
        y, sr = sf.read(f, dtype="float32", always_2d=False)
        if y.ndim > 1: y = y.mean(axis=1)
        peaks.append(20 * np.log10(np.abs(y).max() + 1e-6))
        rmss.append(20 * np.log10(np.sqrt((y ** 2).mean()) + 1e-6))
        durs.append(len(y) / sr)
    print(f"{d}: {len(files)} files | duration median {np.median(durs):.2f} s (min {min(durs):.2f}, max {max(durs):.2f}) "
          f"| peak dBFS median {np.median(peaks):.1f} max {max(peaks):.1f} | rms dBFS median {np.median(rmss):.1f}")

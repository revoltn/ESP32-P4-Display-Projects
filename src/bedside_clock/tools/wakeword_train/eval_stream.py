#!/usr/bin/env python3
"""Device-like streaming evaluation of a microWakeWord model.

The device never resets the model: audio streams continuously and the 100-slice
cool-off only follows a detection. Per-clip evaluation with resets produces a
start-of-stream transient (the model can sit at 1.0 for the first second) that
corrupts both recall and false-accept counts. So this builds ONE stream:

  positives:  [ambience 2 s] clip1 [ambience ~1.5 s] clip2 ... [ambience 1.5 s]
              a hit = >= 1 detection inside [clip start, clip end + 0.7 s];
              detections elsewhere in this stream count as false accepts too
  negatives:  all negative files concatenated in order (they are consecutive
              8 s device recordings), first 2 s ignored

Ambience slices come from --ambience (device room recordings); they are also
negatives, so keep --negatives and --ambience the same folder if you like.

  eval_stream.py --model M.tflite --positives DIR [DIR..] --negatives DIR [DIR..] --ambience DIR
"""
import argparse, glob, os, random
import numpy as np
from eval_model import Model, features, load16k, SR

COOL_OFF = 100

def stream_detections(m, pcm, cutoff255, window):
    """Run the model once over pcm; return detection times (s) for this cutoff.
    Probabilities are computed once and reused across cutoffs by the caller."""
    raise NotImplementedError

def detect_times(ps, cutoff255, window, stride):
    times = []
    recent = np.zeros(window, dtype=np.int64); k = 0; ignore = -COOL_OFF
    for i, p in enumerate(ps):
        k = (k + 1) % window
        recent[k] = p
        if p < cutoff255:
            ignore = min(ignore + stride, 0)
        if ignore < 0:
            continue
        if recent.sum() > cutoff255 * window:
            times.append((i + 1) * stride * 0.010)
            recent[:] = 0; ignore = -COOL_OFF
    return times

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", required=True)
    ap.add_argument("--positives", nargs="*", default=[])
    ap.add_argument("--negatives", nargs="*", default=[])
    ap.add_argument("--ambience", nargs="*", default=[])
    ap.add_argument("--window", type=int, default=5)
    ap.add_argument("--gap", type=float, default=1.5)
    ap.add_argument("--cutoffs", nargs="+", type=float, default=[0.5, 0.6, 0.7, 0.8, 0.85, 0.9, 0.93, 0.95, 0.97, 0.99])
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--max-neg-files", type=int, default=100000)
    a = ap.parse_args()
    random.seed(a.seed)
    m = Model(a.model)

    def wavs(dirs, cap=100000):
        out = []
        for d in dirs:
            out += sorted(glob.glob(os.path.join(d, "**", "*.wav"), recursive=True))
        return out[:cap]

    amb_pcm = [load16k(f) for f in wavs(a.ambience)]
    def ambience(seconds):
        n = int(seconds * SR)
        if not amb_pcm:
            return np.zeros(n, dtype=np.int16)
        src = random.choice(amb_pcm)
        if len(src) <= n:
            return np.resize(src, n)
        s = random.randrange(0, len(src) - n)
        return src[s:s + n]

    # ---- positive stream ----
    pos = wavs(a.positives)
    parts, spans = [ambience(2.0)], []
    t = 2.0
    for f in pos:
        clip = load16k(f)
        parts.append(clip)
        spans.append((t, t + len(clip) / SR))
        t += len(clip) / SR
        g = ambience(a.gap + random.uniform(0, 0.5))
        parts.append(g); t += len(g) / SR
    pos_stream = np.concatenate(parts) if pos else np.zeros(0, dtype=np.int16)
    pos_ps = m.probabilities(features(pos_stream)) if pos else np.zeros(0, dtype=np.int32)

    # ---- negative stream ----
    neg = wavs(a.negatives, a.max_neg_files)
    neg_parts = [load16k(f) for f in neg]
    # file joins are discontinuities the real stream never has: ignore 0.5 s after each
    joins, tt = [], 0.0
    for p in neg_parts[:-1]:
        tt += len(p) / SR; joins.append(tt)
    neg_stream = np.concatenate(neg_parts) if neg else np.zeros(0, dtype=np.int16)
    neg_ps = m.probabilities(features(neg_stream)) if neg else np.zeros(0, dtype=np.int32)
    neg_hours = max(len(neg_stream) / SR - 2.0, 0) / 3600
    def near_join(x):
        return any(j <= x <= j + 0.5 for j in joins)

    print(f"model {os.path.basename(os.path.dirname(os.path.dirname(a.model)))}: {len(pos)} positives in a "
          f"{len(pos_stream)/SR:.0f} s stream, negatives {len(neg_stream)/SR/60:.1f} min")
    print(f"{'cutoff':>7} {'recall':>7} {'FA/h(neg)':>10} {'at joins':>9} {'FA(pos stream)':>15}")
    for c in a.cutoffs:
        c255 = int(round(c * 255))
        det = detect_times(pos_ps, c255, a.window, m.stride)
        hits = 0; stray = 0
        used = set()
        for tm in det:
            hit = None
            for i, (s, e) in enumerate(spans):
                if s <= tm <= e + 0.7:
                    hit = i; break
            if hit is None:
                stray += 1
            else:
                used.add(hit)
        hits = len(used)
        all_fa = [x for x in detect_times(neg_ps, c255, a.window, m.stride) if x > 2.0]
        fa = [x for x in all_fa if not near_join(x)]
        recall = hits / len(spans) if spans else float("nan")
        fah = len(fa) / neg_hours if neg_hours else float("nan")
        print(f"{c:7.2f} {recall:7.3f} {fah:10.1f} {len(all_fa) - len(fa):9d} {stray:15d}")
    if spans:
        peaks = []
        for (s, e) in spans:
            i0 = int(s / (0.010 * m.stride)); i1 = int((e + 0.7) / (0.010 * m.stride)) + 1
            peaks.append(pos_ps[i0:i1].max() / 255.0 if i1 > i0 else 0)
        peaks = np.array(peaks)
        print(f"peak inside utterance windows: median {np.median(peaks):.2f}, 10th pct {np.percentile(peaks, 10):.2f}, min {peaks.min():.2f}")
        missed = [os.path.basename(pos[i]) for i in range(len(spans)) if peaks[i] < 0.9]
        if missed:
            print("weak (<0.9 peak):", ", ".join(missed[:10]))

if __name__ == "__main__":
    main()

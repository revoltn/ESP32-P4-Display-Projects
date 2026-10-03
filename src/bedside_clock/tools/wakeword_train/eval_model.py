#!/usr/bin/env python3
"""Evaluate a microWakeWord streaming model on raw WAVs, the way the device runs it.

Replays each clip through the TF microfrontend (pymicro-features) and the int8
streaming tflite (stride slices per invoke), applies the sliding-window rule
(mean of the last W probabilities > cutoff, 100-slice cool-off) and reports

  * recall on --positives (one wake expected per clip)
  * false accepts per hour on --negatives (any length; long room recordings,
    music, TV, the device's REJECT clips...)
  * both for a sweep of cutoffs, so you can pick probability_cutoff

and writes the ESPHome-v2 manifest for the chosen cutoff (--manifest-out).

  ~/mww/venv-tf/bin/python eval_model.py --model .../stream_state_internal_quant.tflite \
      --positives ~/mww/data/device/rec_positive ~/mww/data/generated/hi_esp_au_male/test \
      --negatives ~/mww/data/device/room_ambient ~/mww/data/fma_16k \
      --wake-word "Hi ESP" --manifest-out wake_word.json --cutoff 0.97
"""
import argparse, glob, json, os, sys
import numpy as np
import soundfile as sf
import librosa
from pymicro_features import MicroFrontend
from ai_edge_litert.interpreter import Interpreter, OpResolverType

SR = 16000
COOL_OFF = 100

def load16k(path):
    y, sr = sf.read(path, dtype="float32", always_2d=False)
    if y.ndim > 1:
        y = y.mean(axis=1)
    if sr != SR:
        y = librosa.resample(y, orig_sr=sr, target_sr=SR)
    return np.clip(y * 32767, -32768, 32767).astype(np.int16)

def features(pcm):
    fe = MicroFrontend()
    raw = pcm.tobytes()
    out, idx = [], 0
    while idx + 320 <= len(raw):
        r = fe.process_samples(raw[idx: idx + 320])
        idx += r.samples_read * 2
        if r.features:
            out.append(r.features)
    f = np.array(out, dtype=np.float32)
    return np.clip(np.floor(f * 256.0 / 26.0 + 0.5) - 128, -128, 127).astype(np.int8)

class Model:
    def __init__(self, path):
        self.it = Interpreter(model_path=path, experimental_op_resolver_type=OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES)
        self.it.allocate_tensors()
        self.inp = self.it.get_input_details()[0]; self.out = self.it.get_output_details()[0]
        self.stride = int(self.inp["shape"][1])

    def probabilities(self, q):
        """per-invoke uint8 probabilities for one clip (state reset first)"""
        self.it.reset_all_variables()
        ps = []
        for i in range(0, len(q) - self.stride + 1, self.stride):
            self.it.set_tensor(self.inp["index"], q[i:i + self.stride][None])
            self.it.invoke()
            ps.append(int(self.it.get_tensor(self.out["index"])[0][0]))
        return np.array(ps, dtype=np.int32)

def detections(ps, cutoff255, window, stride=3):
    """ESPHome rule: number of detections in a probability sequence. The
    cool-off counts feature SLICES (stride of them per invoke) whose latest
    probability is below the cutoff, so 100 slices = 1 s of quiet."""
    n = 0; recent = np.zeros(window, dtype=np.int64); k = 0; ignore = -COOL_OFF
    for p in ps:
        k = (k + 1) % window
        recent[k] = p
        if p < cutoff255:
            ignore = min(ignore + stride, 0)
        if ignore < 0:
            continue
        if recent.sum() > cutoff255 * window:
            n += 1; recent[:] = 0; ignore = -COOL_OFF
    return n

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--model", required=True)
    ap.add_argument("--positives", nargs="*", default=[])
    ap.add_argument("--negatives", nargs="*", default=[])
    ap.add_argument("--window", type=int, default=5)
    ap.add_argument("--cutoffs", nargs="+", type=float, default=[0.5, 0.6, 0.7, 0.8, 0.9, 0.95, 0.97, 0.99])
    ap.add_argument("--cutoff", type=float, default=0.97, help="cutoff written to the manifest")
    ap.add_argument("--wake-word", default="Hi ESP")
    ap.add_argument("--manifest-out", default=None)
    ap.add_argument("--arena", type=int, default=26080)
    ap.add_argument("--max-files", type=int, default=100000)
    a = ap.parse_args()

    m = Model(a.model)
    print(f"model {a.model}: stride {m.stride}, window {a.window}")

    def wavs(dirs):
        out = []
        for d in dirs:
            out += sorted(glob.glob(os.path.join(d, "**", "*.wav"), recursive=True))
        return out[: a.max_files]

    pos = wavs(a.positives); neg = wavs(a.negatives)
    # positives: prepend 1.3 s of silence so the cool-off expires before speech
    pad = np.zeros(int(1.3 * SR), dtype=np.int16)
    pos_ps = []
    for f in pos:
        pos_ps.append(m.probabilities(features(np.concatenate([pad, load16k(f)]))))
    neg_ps, neg_seconds = [], 0.0
    for f in neg:
        pcm = load16k(f); neg_seconds += len(pcm) / SR
        neg_ps.append(m.probabilities(features(pcm)))
    print(f"{len(pos)} positive clips, {len(neg)} negative files = {neg_seconds / 3600:.2f} h")

    print(f"{'cutoff':>7} {'recall':>7} {'FA/h':>8} {'FA':>5}")
    for c in a.cutoffs:
        c255 = int(round(c * 255))
        hits = sum(1 for ps in pos_ps if detections(ps, c255, a.window, m.stride) > 0)
        fa = sum(detections(ps, c255, a.window, m.stride) for ps in neg_ps)
        recall = hits / len(pos_ps) if pos_ps else float("nan")
        fah = fa / (neg_seconds / 3600) if neg_seconds else float("nan")
        print(f"{c:7.2f} {recall:7.3f} {fah:8.2f} {fa:5d}")
    if pos_ps:
        peaks = np.array([ps.max() / 255.0 if len(ps) else 0 for ps in pos_ps])
        print(f"positive peak probability: median {np.median(peaks):.2f}, 10th pct {np.percentile(peaks, 10):.2f}, min {peaks.min():.2f}")

    if a.manifest_out:
        mf = {"type": "micro", "wake_word": a.wake_word, "author": "bedside_clock tools/wakeword_train",
              "model": "wake_word.tflite", "trained_languages": ["en"], "version": 2,
              "micro": {"probability_cutoff": a.cutoff, "feature_step_size": 10,
                        "sliding_window_size": a.window, "tensor_arena_size": a.arena,
                        "minimum_esphome_version": "2024.7.0"}}
        with open(a.manifest_out, "w") as f:
            json.dump(mf, f, indent=2)
        print("manifest written:", a.manifest_out)

if __name__ == "__main__":
    main()

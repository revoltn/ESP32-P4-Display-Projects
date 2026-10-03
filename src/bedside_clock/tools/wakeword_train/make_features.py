#!/usr/bin/env python3
"""Augment positive clips and write microWakeWord spectrogram features
(training / validation / testing RaggedMmaps), following the reference
notebook's recipe: 3.2 s augmentation window, RIR + background mixing at
-5..10 dB SNR, EQ / distortion / pitch / band-stop at 10 %, slide_frames=10 for
train/validation, 1 for testing.

  make_features.py --positives DIR [DIR ...] --out generated_augmented_features \
      --rirs ~/mww/data/mit_rirs --backgrounds ~/mww/data/fma_16k ~/mww/data/audioset_16k \
      [--weight-dirs ...]   # not needed: weights live in the training yaml
Several --positives dirs are pooled (e.g. Chatterbox AU-male clones + device
recordings); use separate runs + separate features dirs when they need
different sampling weights.
"""
import argparse, os, sys, time
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from simple_clips import SimpleClips

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--positives", nargs="+", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--rirs", nargs="+", required=True)
    ap.add_argument("--backgrounds", nargs="+", required=True)
    ap.add_argument("--train-repeat", type=int, default=2)
    ap.add_argument("--split", type=float, default=0.1)
    ap.add_argument("--max-clip-s", type=float, default=None)
    ap.add_argument("--tempo", nargs=2, type=float, default=None, metavar=("MIN", "MAX"),
                    help="random time-stretch factor per clip (>1 = faster); e.g. 1.25 1.9 to bring "
                         "slow TTS clones down to the speaker's natural rate")
    ap.add_argument("--aug-duration", default="3.2",
                    help="fixed clip length in s for augmentation (the augmenter keeps the LAST N seconds "
                         "of a longer clip); 'none' keeps whole clips - use that for long NEGATIVE "
                         "recordings so the trainer's random 1.5 s windows cover all of them")
    ap.add_argument("--mww", default=os.path.expanduser("~/mww/micro-wake-word"))
    a = ap.parse_args()
    aug_dur = None if str(a.aug_duration).lower() == "none" else float(a.aug_duration)
    import random as _random
    import librosa as _librosa
    sys.path.insert(0, a.mww)
    from microwakeword.audio.augmentation import Augmentation
    from microwakeword.audio.spectrograms import SpectrogramGeneration
    from mmap_ninja.ragged import RaggedMmap

    class Pool(SimpleClips):
        """SimpleClips over several directories."""
        def __init__(self, dirs, **kw):
            import glob
            paths = []
            for d in dirs:
                paths += sorted(glob.glob(os.path.join(d, "**", "*.wav"), recursive=True))
            if not paths:
                raise SystemExit(f"no wavs under {dirs}")
            tmpdir = None
            # reuse SimpleClips split logic by faking a directory listing
            self.clips = paths
            import random
            rng = random.Random(kw.get("random_split_seed", 10)); shuffled = list(paths); rng.shuffle(shuffled)
            sc = kw.get("split_count", 0.1)
            k = int(len(shuffled) * sc) if sc < 1 else int(sc)
            k = max(1, min(k, len(shuffled) // 3))
            self.split_clips = {"test": shuffled[:k], "validation": shuffled[k:2 * k], "train": shuffled[2 * k:]}
            self.repeat_clip_min_duration_s = None; self.trim_zeros = False
            self.trimmed_clip_duration_s = a.max_clip_s; self.remove_silence = False

        def _load(self, path):
            y = SimpleClips._load(self, path)
            if a.tempo:
                rate = _random.uniform(a.tempo[0], a.tempo[1])
                y = _librosa.effects.time_stretch(y, rate=rate).astype("float32")
            return y

    clips = Pool(a.positives, random_split_seed=10, split_count=a.split)
    print(f"{len(clips)} positive clips: train {len(clips.split_clips['train'])}, "
          f"validation {len(clips.split_clips['validation'])}, test {len(clips.split_clips['test'])}")

    augmenter = Augmentation(
        augmentation_duration_s=aug_dur,
        augmentation_probabilities={
            "SevenBandParametricEQ": 0.1, "TanhDistortion": 0.1, "PitchShift": 0.1,
            "BandStopFilter": 0.1, "AddColorNoise": 0.1, "AddBackgroundNoise": 0.75,
            "Gain": 1.0, "RIR": 0.5},
        impulse_paths=a.rirs, background_paths=a.backgrounds,
        background_min_snr_db=-5, background_max_snr_db=10,
        min_jitter_s=0.195 if aug_dur else 0.0, max_jitter_s=0.205 if aug_dur else 0.0)

    os.makedirs(a.out, exist_ok=True)
    for split, name, repeat, slide in [("training", "train", a.train_repeat, 10),
                                       ("validation", "validation", 1, 10),
                                       ("testing", "test", 1, 1)]:
        out_dir = os.path.join(a.out, split)
        os.makedirs(out_dir, exist_ok=True)
        gen = SpectrogramGeneration(clips=clips, augmenter=augmenter, slide_frames=slide, step_ms=10)
        t0 = time.time()
        RaggedMmap.from_generator(out_dir=os.path.join(out_dir, "wakeword_mmap"),
                                  sample_generator=gen.spectrogram_generator(split=name, repeat=repeat),
                                  batch_size=100, verbose=False)
        print(f"{split}: done in {time.time() - t0:.0f} s")

if __name__ == "__main__":
    main()

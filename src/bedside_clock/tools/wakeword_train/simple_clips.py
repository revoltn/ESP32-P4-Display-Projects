"""Drop-in replacement for microwakeword.audio.clips.Clips that decodes with
soundfile + librosa instead of Hugging Face `datasets` (which needs torchcodec
and a system FFmpeg since datasets 4.x). Same interface as far as
SpectrogramGeneration / Augmentation.augment_generator care:
audio_generator(split, repeat), get_random_clip(), random_audio_generator().
Yields float32 mono arrays at 16 kHz in [-1, 1].
"""
import glob, math, os, random
import numpy as np
import soundfile as sf
import librosa

SR = 16000

class SimpleClips:
    def __init__(self, input_directory, file_pattern="**/*.wav", random_split_seed=10,
                 split_count=0.1, min_clip_duration_s=None, max_clip_duration_s=None,
                 repeat_clip_min_duration_s=None, trim_zeros=False, trimmed_clip_duration_s=None):
        paths = sorted(glob.glob(os.path.join(input_directory, file_pattern), recursive=True))
        if min_clip_duration_s or max_clip_duration_s:
            keep = []
            for p in paths:
                try:
                    d = sf.info(p).duration
                except Exception:
                    continue
                if min_clip_duration_s and d < min_clip_duration_s:
                    continue
                if max_clip_duration_s and d > max_clip_duration_s:
                    continue
                keep.append(p)
            paths = keep
        if not paths:
            raise ValueError(f"no clips matching {file_pattern} under {input_directory}")
        self.clips = list(paths)
        rng = random.Random(random_split_seed)
        shuffled = list(paths)
        rng.shuffle(shuffled)
        k = int(len(shuffled) * split_count) if split_count < 1 else int(split_count)
        k = max(1, min(k, len(shuffled) // 3)) if len(shuffled) >= 3 else 0
        self.split_clips = {"test": shuffled[:k], "validation": shuffled[k:2 * k], "train": shuffled[2 * k:]}
        self.repeat_clip_min_duration_s = repeat_clip_min_duration_s
        self.trim_zeros = trim_zeros
        self.trimmed_clip_duration_s = trimmed_clip_duration_s
        self.remove_silence = False

    def __len__(self):
        return len(self.clips)

    def _load(self, path):
        y, sr = sf.read(path, dtype="float32", always_2d=False)
        if y.ndim > 1:
            y = y.mean(axis=1)
        if sr != SR:
            y = librosa.resample(y, orig_sr=sr, target_sr=SR)
        if self.trim_zeros:
            y = np.trim_zeros(y)
        if self.trimmed_clip_duration_s:
            y = y[: int(self.trimmed_clip_duration_s * SR)]
        if self.repeat_clip_min_duration_s and len(y) < self.repeat_clip_min_duration_s * SR:
            reps = int(math.ceil(self.repeat_clip_min_duration_s * SR / max(len(y), 1)))
            y = np.tile(y, reps)
        return y.astype(np.float32)

    def audio_generator(self, split=None, repeat=1):
        clip_list = self.clips if split is None else self.split_clips[split]
        for _ in range(repeat):
            for p in clip_list:
                yield self._load(p)

    def get_random_clip(self):
        return self._load(random.choice(self.clips))

    def random_audio_generator(self, max_clips=math.inf):
        while max_clips > 0:
            max_clips -= 1
            yield self.get_random_clip()

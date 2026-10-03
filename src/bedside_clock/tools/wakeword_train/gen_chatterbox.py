#!/usr/bin/env python3
"""Voice-cloned wake-word samples with Chatterbox (Resemble AI, MIT) on the GPU.

Every reference clip in --refs (10-20 s of ONE speaker, clean, 16-48 kHz WAV)
is cloned saying each phrase in --phrases, sweeping exaggeration / cfg_weight /
temperature so the set covers fast, slow, flat and animated deliveries. The
accent and gender come from the reference clips: give it Australian male
references and you get Australian male samples. Output is 16 kHz mono int16
WAV, silence-trimmed, ready for microWakeWord's Clips loader.

  ~/mww/venv-tts/bin/python gen_chatterbox.py \
      --refs ~/mww/data/refs/au_male --phrases "Hi E S P." "Hi, E.S.P!" "hi esp." \
      --per-ref 600 --out ~/mww/data/generated/hi_esp_au_male

Confusables (hard negatives in the same voices) are just another run:
  ... --phrases "hi there" "I see" "high speed" "yes please" "E S P" "hi Steve" \
      --per-ref 150 --out ~/mww/data/generated/confusables_au_male

First run downloads the Chatterbox weights from Hugging Face (~2 GB).
"""
import argparse, glob, os, random, sys, time
import numpy as np
import torch, torchaudio
import soundfile as sf

def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--refs", required=True, help="directory of reference WAVs (one speaker per file)")
    ap.add_argument("--phrases", nargs="+", required=True)
    ap.add_argument("--per-ref", type=int, default=300, help="samples per reference clip (split across phrases)")
    ap.add_argument("--out", required=True)
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--turbo", action="store_true", help="use the Turbo model (faster, English only)")
    ap.add_argument("--exaggeration", nargs="+", type=float, default=[0.3, 0.45, 0.6, 0.75])
    ap.add_argument("--cfg", nargs="+", type=float, default=[0.2, 0.35, 0.5, 0.65])
    ap.add_argument("--temperature", nargs="+", type=float, default=[0.6, 0.8, 1.0])
    a = ap.parse_args()

    refs = sorted(glob.glob(os.path.join(a.refs, "*.wav")))
    if not refs:
        sys.exit(f"no reference wavs in {a.refs}")
    os.makedirs(a.out, exist_ok=True)
    random.seed(a.seed); torch.manual_seed(a.seed)
    dev = "cuda" if torch.cuda.is_available() else "cpu"
    print("device", dev, "| refs", len(refs), "| phrases", a.phrases)

    if a.turbo:
        from chatterbox.tts_turbo import ChatterboxTurboTTS as TTS
    else:
        from chatterbox.tts import ChatterboxTTS as TTS
    model = TTS.from_pretrained(device=dev)
    sr_out = 16000
    resampler = torchaudio.transforms.Resample(model.sr, sr_out).to(dev)

    total = 0
    t0 = time.time()
    for ref in refs:
        tag = os.path.splitext(os.path.basename(ref))[0]
        n_done = len(glob.glob(os.path.join(a.out, f"{tag}_*.wav")))
        for k in range(n_done, a.per_ref):
            phrase = a.phrases[k % len(a.phrases)]
            ex = random.choice(a.exaggeration); cfg = random.choice(a.cfg); temp = random.choice(a.temperature)
            try:
                with torch.inference_mode():
                    wav = model.generate(phrase, audio_prompt_path=ref, exaggeration=ex,
                                         cfg_weight=cfg, temperature=temp)
                    wav = resampler(wav.to(dev)).squeeze(0).cpu().numpy()
            except Exception as e:
                print("generate failed:", type(e).__name__, str(e)[:120]); continue
            # trim to the spoken part (+120 ms pads); drop silent/garbage outputs
            thr = 0.02 * (np.abs(wav).max() + 1e-6)
            idx = np.where(np.abs(wav) > thr)[0]
            if len(idx) < sr_out // 10:
                continue
            lo = max(0, idx[0] - int(0.12 * sr_out)); hi = min(len(wav), idx[-1] + int(0.12 * sr_out))
            wav = wav[lo:hi]
            if len(wav) > 3 * sr_out:  # runaway generation
                continue
            pcm = (np.clip(wav / (np.abs(wav).max() + 1e-6) * 0.8, -1, 1) * 32767).astype(np.int16)
            fn = os.path.join(a.out, f"{tag}_{k:04d}_p{a.phrases.index(phrase)}_e{ex:.2f}_c{cfg:.2f}_t{temp:.1f}.wav")
            sf.write(fn, pcm, sr_out)
            total += 1
            if total % 25 == 0:
                print(f"{total} clips, {time.time() - t0:.0f} s, last {len(wav) / sr_out:.2f} s '{phrase}' from {tag}")
    print(f"done: {total} new clips in {a.out} ({time.time() - t0:.0f} s)")

if __name__ == "__main__":
    main()

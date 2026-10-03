#!/usr/bin/env python3
"""Record a clean voice-cloning reference on the PC (headset / USB mic).

  python record_ref.py 20 refs/ref_01.wav          # 20 s, default input device
  python record_ref.py --list                        # show input devices
  python record_ref.py 20 refs/ref_02.wav --device 3

Read naturally for the whole time, e.g.:
  "Hi ESP. Hi ESP, what's the time. Hi ESP, open the alarm. It's a warm
   morning and the kettle's on. Hi ESP, volume down. Hi ESP, play a song."
Several takes at different distances / energies give Chatterbox more to
work with; keep the room quiet. Needs: pip install sounddevice soundfile
"""
import argparse, sys
import numpy as np
import sounddevice as sd
import soundfile as sf

ap = argparse.ArgumentParser()
ap.add_argument("seconds", type=float, nargs="?", default=20)
ap.add_argument("out", nargs="?", default="ref.wav")
ap.add_argument("--device", type=int, default=None)
ap.add_argument("--rate", type=int, default=24000)
ap.add_argument("--list", action="store_true")
a = ap.parse_args()
if a.list:
    print(sd.query_devices()); sys.exit(0)
print(f"recording {a.seconds:.0f} s at {a.rate} Hz from device {a.device if a.device is not None else 'default'} ... speak now")
x = sd.rec(int(a.seconds * a.rate), samplerate=a.rate, channels=1, dtype="float32", device=a.device)
sd.wait()
x = x[:, 0]
peak = float(np.abs(x).max())
print(f"peak {peak:.3f} ({20 * np.log10(peak + 1e-9):.1f} dBFS)" + ("  <- too quiet, raise the mic level" if peak < 0.1 else ""))
sf.write(a.out, (np.clip(x / max(peak, 1e-6) * 0.9, -1, 1) * 32767).astype(np.int16), a.rate)
print("saved", a.out)

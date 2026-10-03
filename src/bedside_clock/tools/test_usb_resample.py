"""Host mirror of stream_write_locked() in main/usb_audio.c.

Each device test costs a physical cable swap, so the resampler arithmetic gets
checked here first: output frame count, pitch, and above all continuity across
buffer boundaries (a phase that does not carry over clicks once per buffer).
"""
import math


class Resampler:
    def __init__(self, src_rate, src_ch, dev_rate, dev_ch):
        self.src_rate, self.src_ch = src_rate, src_ch
        self.dev_rate, self.dev_ch = dev_rate, dev_ch
        self.reset()

    def reset(self):
        self.pos_q16 = 0
        self.prev = [0, 0]
        self.have_prev = False

    def _src_frame(self, buf, i):
        if self.src_ch >= 2:
            return buf[i * 2], buf[i * 2 + 1]
        return buf[i], buf[i]

    def write(self, buf):
        """buf = flat list of int16, interleaved. Returns list of out frames."""
        frames = len(buf) // self.src_ch
        if frames == 0:
            return []
        step = ((self.src_rate << 16) // self.dev_rate)
        if not self.have_prev:
            self.prev = list(self._src_frame(buf, 0))
            self.have_prev = True
            self.pos_q16 = 0
        out = []
        while True:
            ip = self.pos_q16 >> 16
            if ip >= frames - 1:
                break
            fr = self.pos_q16 & 0xFFFF
            if ip < 0:
                al, ar = self.prev
            else:
                al, ar = self._src_frame(buf, ip)
            bl, br = self._src_frame(buf, ip + 1)
            l = al + (((bl - al) * fr) >> 16)
            r = ar + (((br - ar) * fr) >> 16)
            out.append((l, r) if self.dev_ch >= 2 else ((l + r) // 2,))
            self.pos_q16 += step
        self.pos_q16 -= frames << 16
        self.prev = list(self._src_frame(buf, frames - 1))
        return out


def sine(rate, freq, n, ch, amp=8000, phase0=0.0):
    out = []
    for i in range(n):
        v = int(amp * math.sin(2 * math.pi * freq * (i + phase0) / rate))
        out.extend([v] * ch)
    return out


def check(name, cond, detail=""):
    print(("PASS " if cond else "FAIL ") + name + (("  " + detail) if detail else ""))
    return cond


ok = True

# 1. 16 kHz mono -> 48 kHz stereo, the alarm chime case (exact 3x)
r = Resampler(16000, 1, 48000, 2)
total_out, total_in = 0, 0
for _ in range(10):
    blk = sine(16000, 1000, 512, 1, phase0=total_in)
    total_in += 512
    total_out += len(r.write(blk))
exp = total_in * 3
ok &= check("16k mono -> 48k stereo frame count",
            abs(total_out - exp) <= 3, f"got {total_out}, expected ~{exp}")

# 2. 44.1 kHz stereo -> 48 kHz stereo, the music case
r = Resampler(44100, 2, 48000, 2)
total_out, total_in = 0, 0
for _ in range(20):
    blk = sine(44100, 440, 1024, 2, phase0=total_in)
    total_in += 1024
    total_out += len(r.write(blk))
exp = int(total_in * 48000 / 44100)
ok &= check("44.1k stereo -> 48k stereo frame count",
            abs(total_out - exp) <= 3, f"got {total_out}, expected ~{exp}")

# 3. Continuity: no discontinuity at block boundaries. Resample a continuous
#    sine in blocks and confirm no sample-to-sample jump larger than the
#    largest jump the waveform itself can produce at the output rate.
r = Resampler(44100, 2, 48000, 2)
allout = []
for b in range(12):
    blk = sine(44100, 440, 1024, 2, phase0=b * 1024)
    allout.extend(r.write(blk))
left = [f[0] for f in allout]
max_step = max(abs(left[i + 1] - left[i]) for i in range(len(left) - 1))
# 440 Hz, amp 8000, at 48 kHz: max slope = 2*pi*440*8000/48000 ~= 461
ok &= check("44.1k->48k continuity (no boundary clicks)",
            max_step < 700, f"max sample step {max_step}, slope limit ~461")

# 4. Pitch preserved: zero crossings per second must match the source tone.
zc = sum(1 for i in range(len(left) - 1) if (left[i] < 0) != (left[i + 1] < 0))
secs = len(left) / 48000
freq = zc / 2 / secs
ok &= check("44.1k->48k pitch preserved",
            abs(freq - 440) < 8, f"measured {freq:.1f} Hz, expected 440")

# 5. Chime continuity at 16k->48k
r = Resampler(16000, 1, 48000, 2)
allout = []
for b in range(12):
    allout.extend(r.write(sine(16000, 1000, 512, 1, phase0=b * 512)))
left = [f[0] for f in allout]
max_step = max(abs(left[i + 1] - left[i]) for i in range(len(left) - 1))
# 1000 Hz, amp 8000, at 48 kHz: max slope = 2*pi*1000*8000/48000 ~= 1047
ok &= check("16k->48k continuity (chime)",
            max_step < 1400, f"max sample step {max_step}, slope limit ~1047")
zc = sum(1 for i in range(len(left) - 1) if (left[i] < 0) != (left[i + 1] < 0))
freq = zc / 2 / (len(left) / 48000)
ok &= check("16k->48k pitch preserved (chime)",
            abs(freq - 1000) < 20, f"measured {freq:.1f} Hz, expected 1000")

# 6. Mono device fallback path
r = Resampler(44100, 2, 48000, 1)
out = r.write(sine(44100, 440, 1024, 2))
ok &= check("stereo source -> mono device", all(len(f) == 1 for f in out),
            f"{len(out)} frames")

# 7. Degenerate inputs must not crash or emit garbage
r = Resampler(16000, 1, 48000, 2)
ok &= check("single-frame buffer is safe", r.write([123]) == [])
ok &= check("empty buffer is safe", r.write([]) == [])

print("\nALL PASS" if ok else "\nFAILURES PRESENT")

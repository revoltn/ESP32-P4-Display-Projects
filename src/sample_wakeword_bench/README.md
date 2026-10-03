# sample_wakeword_bench: microWakeWord on the ESP32-P4 rev v1.3, step-0 de-risk

Throwaway firmware that proves a microWakeWord streaming model runs correctly and cheaply on
the JC4880P443C (ESP32-P4 **rev v1.3**) with `esp-tflite-micro` + `esp-nn` PIE kernels,
before it was wired into `src/bedside_clock` (`main/wake_mww.cpp`). No display, codec or
network; it feeds embedded 16 kHz WAV clips (Windows TTS "Okay Nabu" and a negative sentence)
through the TF microfrontend and the stock `okay_nabu.tflite` (ESPHome v2 manifest) and
reports arena size, per-invoke time, detections, heap and stack use. Older documents call it
`mww_test`.

Same partition table as `bedside_clock`, so `idf.py -p COMx app-flash` only replaces the app
and leaves the `model`, `nvs` and `coredump` partitions alone; restore the clock afterwards
with `idf.py -p COMx app-flash` from `src/bedside_clock` (`-B build_mww` for that variant).

Licence: `main/main.cpp` and the `.wav` clips are MIT; `okay_nabu.tflite` is Kevin Ahrendt's
Apache-2.0 model from esphome/micro-wake-word-models, see [NOTICE.md](NOTICE.md).

## Build

```sh
idf.py set-target esp32p4
idf.py build                                   # esp-nn PIE kernels (CONFIG_NN_OPTIMIZED=y)
idf.py -p COMx app-flash monitor
```

Reference-kernel variant (plain ANSI C kernels, for numeric cross-checks and the no-ESP-NN
cost):

```sh
idf.py -B build_ref -D SDKCONFIG=sdkconfig.ref \
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.ref.defaults" build
```

`sdkconfig.defaults` selects rev v1.3 (`CONFIG_ESP32P4_SELECTS_REV_LESS_V3`), 360 MHz, PSRAM at
200 MHz with XIP, the 256 KB / 128 B-line L2 cache and the shared `partitions.csv`.

## Results (2026-09-20, IDF v5.5.5, 360 MHz, 512-sample chunks = the AFE feed size)

| build | invoke (3 slices = 30 ms audio) | frontend per slice | CPU incl. frontend | arena used |
|---|---|---|---|---|
| esp-nn **PIE** (`CONFIG_NN_OPTIMIZED=y`), arena internal | 0.55 ms avg, 0.86 ms max | ~0.26 ms | **4.7 %** of one core | 21.7 KB |
| esp-nn PIE, arena **PSRAM** | 0.55 ms avg (same) | ~0.26 ms | 4.6 % | 21.7 KB |
| ANSI-C reference (`CONFIG_NN_ANSI_C=y`) | 1.01 ms avg | ~0.26 ms | 6.2 % | 18.4 KB |

- **No illegal-instruction fault, no panic, no leak** over two 60 s noise soaks on the rev v1.3
  silicon; the task ran on core 1 with a 16 KB **PSRAM** stack (high-water ~13.4 KB free, i.e.
  ~3 KB used including logging; ESPHome's 8 KB P4 minimum is comfortably met).
- **Numerically correct**: the per-invoke probability trace from the PIE build, the ANSI-C
  build and a PC run of the same model (pymicro-features + LiteRT without XNNPACK; the PC
  script is not part of the repository) agree to within float-versus-int feature rounding
  (peaks 0.96 / 0.85 at the same 30 ms timestamps).
- Memory: arena in PSRAM costs ~1 KB internal + 27 KB PSRAM; the model is 60 KB in flash;
  tflite-micro + esp-nn + frontend add ~0.4 MB of `.text` (the app binary is 0.8 MB including
  0.35 MB of embedded assets).
- The TTS clips peaked at 0.96 (cutoff 0.97), so the stock model did not "wake" on them; the
  PC reference gives the identical value, so this is the synthetic voice's "Nabu", not the
  device. The negative sentence and 60 s of noise produced 0.00 throughout.
- Caveat: the ANSI-C run rebooted twice between suites with no panic output; the PIE run (the
  one that matters) completed both suites cleanly in one boot. Not investigated.

## Notes carried into `bedside_clock`

- The feature generator comes from `esphome/esp-micro-speech-features` (Apache-2.0), **not**
  from esp-tflite-micro >= 1.4 (the microfrontend was dropped there). Include `frontend.h` /
  `frontend_util.h`.
- Model input `[1, 3, 40]` int8, output `[1, 1]` uint8; features quantised as
  `(v*256 + 333)/666 - 128`; detection = mean of the last 5 probabilities > cutoff, with a
  100-slice cool-off after reset (`MIN_SLICES_BEFORE_DETECTION`).
- Ops needed: CallOnce, VarHandle, Reshape, ReadVariable, StridedSlice, Concatenation,
  AssignVariable, Conv2D, Mul, Add, Mean, FullyConnected, Logistic, Quantize,
  DepthwiseConv2D, AveragePool2D, MaxPool2D, Pad, Pack, SplitV.
- Resource variables need a `MicroResourceVariables` (var arena 1 KB) passed to the
  interpreter; recreate it whenever an interpreter is thrown away.
- `main.cpp` is an independent implementation of the recipe ESPHome's `micro_wake_word`
  component (GPLv3) also follows; no ESPHome source was copied.

## Files

| Path | Purpose |
|---|---|
| `main/main.cpp` | The benchmark: WAV walker, microfrontend setup, streaming model wrapper, timing and memory report, noise soak |
| `main/assets/okay_nabu.tflite`, `okay_nabu.json` | Stock model and manifest (third party, see NOTICE.md) |
| `main/assets/okay_nabu_tts.wav`, `okay_nabu_tts2.wav`, `negative_tts.wav` | Author-generated 16 kHz mono test clips |
| `sdkconfig.defaults`, `sdkconfig.ref.defaults` | PIE and ANSI-C variants |
| `partitions.csv` | Copy of `bedside_clock`'s table |

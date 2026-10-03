# Third-party notice for `src/sample_wakeword_bench`

`main/main.cpp` and the three `.wav` clips in `main/assets/` are MIT (repository `LICENSE`).
The full attribution list is the repository's
[THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md). This project vendors no components
and patches nothing in `managed_components/`.

## Third-party file in this project

- `main/assets/okay_nabu.tflite` and `okay_nabu.json`: the stock "Okay Nabu" microWakeWord
  model by Kevin Ahrendt (https://www.kevinahrendt.com/), from
  https://github.com/esphome/micro-wake-word-models, Apache-2.0
  (https://www.apache.org/licenses/LICENSE-2.0). Unmodified. The manifest is the ESPHome v2
  model manifest format.

## Derived code

`main/main.cpp` implements the microWakeWord streaming recipe (TF microfrontend features,
int8 quantisation, sliding-window mean, 100-slice cool-off) independently on `esp-tflite-micro`
and `esphome/esp-micro-speech-features` (both Apache-2.0, fetched by `idf.py`). ESPHome's
`micro_wake_word` component (GPLv3) was the behavioural reference; no ESPHome source was
copied.

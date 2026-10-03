# Local patches to registry components

**Status 2026-10-03: the patched components now live in-tree.**
`components/espressif__esp_lvgl_port` (2.9.0) and `components/espressif__esp_codec_dev`
(1.5.11) are committed copies with the patches below already applied (markers:
`LOCAL PATCH`). Local components override the same-named registry components the BSP
manifest requests, so nothing has to be re-applied after `idf.py reconfigure` and the
`.component_hash` validation problem is gone. The `.patch` files and `apply_patches.sh`
stay as provenance (the exact diff against the pristine registry archives) and for anyone
who prefers to re-create the copies from the registry.

## Original notes (pre 2026-10-03)

`managed_components/` is not committed: `idf.py` regenerates it from
`dependencies.lock`. Two of the components it downloads need source changes for
this firmware to behave on the board, and those changes would be lost on every
clean checkout. They live here as unified diffs and are re-applied by
`apply_patches.sh`.

| Patch | Component | Files |
|---|---|---|
| `0001-esp_lvgl_port-2.9.0-bounded-ppa-wait-cpu-fallback.patch` | `espressif/esp_lvgl_port` 2.9.0 | `src/common/ppa/lcd_ppa.c`, `src/lvgl9/esp_lvgl_port_disp.c` |
| `0002-esp_codec_dev-1.5.11-fail-open-when-set_fmt-fails.patch` | `espressif/esp_codec_dev` 1.5.11 | `esp_codec_dev.c` |

Both were generated against the pristine registry archives of exactly those
versions (`espressif__esp_lvgl_port-v2.9.0.zip`, `espressif__esp_codec_dev-v1.5.11.zip`)
and verified with `patch --dry-run` in both directions, so applying them
reproduces the sources this firmware was developed and tested with.

## Why

### 0001: PPA rotate can hang forever on ESP32-P4 rev v1.3

The display is driven in landscape through LVGL's software-rotation path, which
`esp_lvgl_port` accelerates with the P4's PPA (pixel processing accelerator).
Upstream issues the scale-rotate-mirror operation in blocking mode, i.e. it waits
on the transaction-done interrupt with `portMAX_DELAY`. On this board's rev v1.3
silicon that interrupt was seen never arriving: a JTAG backtrace showed the LVGL
task parked in `ppa_do_operation` for over seven minutes while a song streamed.
Because touch is polled from the same task (the GT911 INT line is not wired), the
whole UI froze; nothing short of a reset recovered it.

The patch:

- `lcd_ppa.c`: issues the rotate non-blocking, waits on a binary semaphore given
  from the PPA done ISR with a **500 ms bound**, and returns `ESP_ERR_TIMEOUT`
  instead of hanging. A full 480x800 RGB565 rotate takes a few milliseconds, so
  the bound never triggers in normal operation.
- `esp_lvgl_port_disp.c`: allocates the CPU rotation buffer even when the PPA is
  in use; when a PPA rotate fails or times out it flushes a CPU-rotated frame
  (upstream flushed the *unrotated* buffer) and sets `ppa_failed`, so the PPA is
  not touched again until reboot (a timed-out transaction may still be pending
  in the engine). The display degrades to CPU rotation instead of dying.

### 0002: `esp_codec_dev_open()` must fail when the I2S slot reconfig fails

`esp_codec_dev_open()` calls the data interface's `set_fmt()` (the I2S slot
reconfiguration) and ignores its return value. When that fails, typically
`ESP_ERR_NO_MEM` while allocating DMA descriptors in internal RAM, the channel
is left with `dma.desc == NULL` and the following `enable()` panics in
`i2s_rx_channel_start` with a NULL load. The patch checks the return value,
clears the opened flags and returns `ESP_CODEC_DEV_DRV_ERR`, so the caller
(`bsp_extra_codec_set_fs`) can report "codec busy / out of memory" and retry.

## How to apply

```sh
cd src/bedside_clock
idf.py reconfigure              # populates managed_components from dependencies.lock
bash patches/apply_patches.sh   # idempotent; prints OK / APPLIED / FAIL per component
idf.py build
```

`apply_patches.sh` dry-runs each patch first and skips a component whose patched
file already carries the `LOCAL PATCH` marker, so it is safe to run on every
build. The equivalent manual commands are

```sh
patch -p1 -d managed_components/espressif__esp_lvgl_port < patches/0001-esp_lvgl_port-2.9.0-bounded-ppa-wait-cpu-fallback.patch
patch -p1 -d managed_components/espressif__esp_codec_dev < patches/0002-esp_codec_dev-1.5.11-fail-open-when-set_fmt-fails.patch
```

If the IDF component manager later complains that a managed component was
modified (it validates `managed_components/<name>/.component_hash`), either let it
re-download a pristine copy (`IDF_COMPONENT_OVERWRITE_MANAGED_COMPONENTS=1 idf.py
reconfigure`) and run `apply_patches.sh` again, or move to the override described
under "Long term".

## How to verify

- `grep -n "LOCAL PATCH" managed_components/espressif__esp_lvgl_port/src/common/ppa/lcd_ppa.c
  managed_components/espressif__esp_codec_dev/esp_codec_dev.c` shows the markers.
- The serial log of a boot with the patch in place prints nothing new; the
  patch only speaks when it saves the day: `PPA: SRM rotate did not complete
  within 500 ms` followed by `LVGL: PPA rotation failed (ESP_ERR_TIMEOUT) - CPU
  rotation for the rest of this boot`, or `Adev_Codec: Data interface set_fmt
  failed (...) - not enabling channel`.
- Without 0001, the failure mode is a frozen screen and touch while audio keeps
  playing; `idf.py coredump-info` after the UI watchdog fires shows the LVGL
  task inside `ppa_do_operation`.

## Versions

The patches match the component versions pinned in `dependencies.lock`. If you
bump either component, re-create the diff against the new pristine source (the
registry serves archives at
`https://components.espressif.com/api/components/espressif/<name>` -> `versions[].url`)
or, better, check whether upstream has fixed the issue.

## Licensing

Both components are Apache-2.0. The patched files carry a `LOCAL PATCH` comment at
each change, which is the "prominent notice" Apache-2.0 section 4(b) requires on
modified files; these patches are offered under the same Apache-2.0 terms. See
`THIRD_PARTY_NOTICES.md` at the repository root.

## Long term

Patching `managed_components/` is a stop-gap. The clean options are a
`components/espressif__esp_lvgl_port` / `components/espressif__esp_codec_dev`
override (a full copy of the patched component, which takes precedence over the
registry version) or an upstream pull request: the bounded PPA wait is a
robustness fix any P4 rev v1.3 user would want, and checking `set_fmt()`'s return
value is a plain bug fix.

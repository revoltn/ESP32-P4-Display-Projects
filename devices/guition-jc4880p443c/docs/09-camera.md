# Camera: OV02C10 over MIPI-CSI — verified working

Live camera preview brought up 2026-08-30 in `src/sample_hello_world` (the **Camera** menu button).
User-confirmed on the actual board: live full-screen preview, correct orientation, tap-to-exit,
re-entry works, and the shared I2C peripherals (touch, codec) unharmed afterwards.

## The hardware (as verified)

| Item | Finding |
|---|---|
| Sensor | **OmniVision OV02C10** (2 MP, 1920x1080), on the CSI FPC connector — **a module IS fitted on this board** (an earlier session note claiming otherwise was wrong) |
| Control bus | SCCB = the **shared board I2C bus** (GPIO8 SCL / GPIO7 SDA, port 0), sensor at **0x36** (7-bit). Same bus as GT911 touch (0x5D) and ES8311 codec (0x18) |
| Reset / powerdown | Not GPIO-driven — pass **-1** for both (`CSI_IO0/1` on the connector are not confirmed usable) |
| CSI lanes | 2 lanes physically wired, but the **proven mode is 1-lane** RAW10 1920x1080 @ 30 fps |
| Mounting orientation | The module is mounted so the sensor's native 1920x1080 frame is **already upright relative to the portrait screen** — no PPA rotation needed (a 90° rotate produced a sideways image; rotation 0 confirmed correct by user) |

## Critical: the component version trap

`espressif/esp_video 2.0.1` (pulled transitively by the BSP) pins `esp_cam_sensor: 2.0.*`,
and **esp_cam_sensor 2.0.1 has no OV02C10 driver at all**. A registry bump cannot fix it.

The vendor ships **esp_cam_sensor 2.1.0** (which adds ov02c10) vendored inside
`devices/guition-jc4880p443c/vendor/package/1-Demo/idf_examples/ESP-IDF_5.5.4/video_lcd_display/components/esp_cam_sensor/`
(local only, see [../vendor/README.md](../vendor/README.md)).
Their `idf_examples/README.md` explicitly instructs replacing the managed copy with it.

The fix (same local-override trick as `espressif__esp_lcd_st7701`):

1. Copy that directory to `src/sample_hello_world/components/esp_cam_sensor/` (vendor's dir name is fine).
2. Delete `managed_components/espressif__esp_cam_sensor/` and `dependencies.lock`.
3. Reconfigure — the log must show `Using component placed at ...components\esp_cam_sensor`.
   The 2.0.* vs 2.1.0 version-range warning for a local override is expected and harmless.

## Required sdkconfig block (now in `src/sample_hello_world/sdkconfig.defaults`)

```ini
CONFIG_CAMERA_OV02C10=y
CONFIG_CAMERA_OV02C10_MIPI_RAW10_1920X1080_30FPS_1_LANE=y
CONFIG_CAMERA_OV02C10_MIPI_DEFAULT_FMT_RAW10_1920X1080_30FPS_1_LANE=y
CONFIG_ESP_VIDEO_ENABLE_ISP_PIPELINE_CONTROLLER=y
```

- The ISP pipeline controller line is **functionally required**, not optional: it spawns the
  `isp_task` that runs auto-exposure/auto-white-balance. Without it the RAW10→RGB565
  conversion produces dark/green frames.
- Beware: the vendor demo's own `sdkconfig.defaults` names a **nonexistent** symbol
  (`..._1920x1080_2LAN_30FPS`); Kconfig silently fell back to the 1-lane FHD default, which is
  what their shipped `sdkconfig` actually contains. Trust the generated sdkconfig, not their
  defaults file. Valid mode symbols live in
  `components/esp_cam_sensor/sensors/ov02c10/Kconfig.ov02c10`.
- New `CONFIG_CAMERA_*` symbols only appear after the local component is in the build **and**
  sdkconfig is regenerated (delete `sdkconfig`, it rebuilds from `sdkconfig.defaults`).

## Pipeline architecture (what actually works with our display setup)

```
OV02C10 (RAW10, 30fps) → MIPI-CSI → P4 ISP (AE/AWB/demosaic → RGB565)
  → V4L2 capture (2 user-pointer PSRAM buffers)
  → stream task (core 1): DQBUF → frame callback → QBUF
       frame callback: bsp_display_lock(100) [skip frame on timeout]
         → PPA scale/crop into a dedicated PSRAM canvas buffer
         → lv_obj_invalidate(canvas)
       → unlock
  → LVGL renders the canvas (full-screen lv_canvas on lv_layer_top())
```

Key constraints discovered:

- **Cannot draw to the panel directly.** `sample_hello_world` runs LVGL with
  `BSP_DISPLAY_LVGL_AVOID_TEAR` + `DIRECT_MODE`, so lvgl_port owns the two DSI framebuffers.
  The vendor `video_lcd_display` demo's `esp_lcd_dpi_panel_get_frame_buffer` +
  `esp_lcd_panel_draw_bitmap` approach would fight the LVGL renderer. Everything must go
  through an LVGL object (canvas) under `bsp_display_lock`.
- **Reuse the existing I2C bus handle** for SCCB: `init_sccb = false`,
  `i2c_handle = bsp_i2c_get_handle()`. Letting esp_video create a second driver on GPIO7/8
  would clash with touch + codec.
- `lv_obj_invalidate`, not `lv_refr_now` — the lvgl_port task owns refresh timing in
  direct mode.
- Doing the PPA blit *inside* the display lock serializes writer vs renderer (no tearing);
  at 30 fps it holds the lock ~1-3 ms per frame. On lock contention the frame is dropped —
  cannot deadlock.

## PPA findings (P4 2D accelerator)

- Scale factors are quantized to **1/16 steps** (`ppa_srm.c`, FRAG_MAX=16). 0.5 and 0.75 are
  exact; arbitrary ratios like 800/1080 are not representable.
- For 90°/270° rotation the placed output block is `w = scale_y*block_h, h = scale_x*block_w`;
  the driver validates it fits `out.pic_w/h`.
- Output buffer address **and** `buffer_size` must be cache-line aligned (128 B here);
  allocate with `heap_caps_aligned_calloc(alignment, …, MALLOC_CAP_SPIRAM)`.
- `esp_cache_get_alignment()` is declared in **`esp_private/esp_cache_private.h`**, not
  `esp_cache.h`.
- Working geometry (fills 480x800 portrait, no rotation): centered **640x1066 crop**
  (offsets 640, 7) at **scale 0.75** → 480x799 output (last canvas row stays black — the
  buffer is zeroed at alloc). The 90°-rotate alternative (1600x960 crop @ 0.5 → exact
  480x800) works mechanically but shows the scene sideways on this module.

## Memory budget (32 MB PSRAM — comfortable)

| Buffer | Size |
|---|---|
| 2 camera frame buffers (1920·1080·2 B) | 8.3 MB |
| 1 canvas buffer (480·800·2 B) | 0.77 MB |
| **Total** | **≈9.1 MB**, allocated once on first use, never freed (avoids fragmentation) |

## Lifecycle model that proved reliable

- `esp_video_init` + device `open()` + buffer allocation happen **once, lazily, on first
  button press**; failures don't latch (a re-tap re-probes, with a friendly "is the camera
  module plugged in?" log if absent).
- Per entry/exit only STREAMON/STREAMOFF cycle: start = `VIDIOC_STREAMON` + spawn stream task;
  stop = event-group handshake (set DELETE bit → task finishes its DQBUF/QBUF cycle,
  STREAMOFF, sets DELETE_DONE, self-deletes; caller blocks on DELETE_DONE). Re-entry re-queues
  the same buffers (`REQBUFS`/`QBUF` again) — verified working repeatedly.
- Donor-code fixes worth keeping: the vendor stream task takes the fd via a **stack pointer**
  from the parent (race), and `ESP_ERROR_CHECK`s DQBUF (panics on transient errors). Our port
  (`src/sample_hello_world/main/app_video.c`) stores the fd in the module struct and exits the task
  cleanly on stream errors.

## Where the code is

| File | Role |
|---|---|
| `src/sample_hello_world/main/feature_camera.c` | Feature logic: lazy init, PPA geometry, canvas UI, tap-to-exit |
| `src/sample_hello_world/main/app_video.c/.h` | V4L2 wrapper (ported from vendor `video_lcd_display`, stop handshake from Brookesia camera app) |
| `src/sample_hello_world/components/esp_cam_sensor/` | Local esp_cam_sensor **2.1.0** override (has the ov02c10 driver) |
| `devices/guition-jc4880p443c/vendor/package/1-Demo/idf_examples/ESP-IDF_5.5.4/video_lcd_display/` | Vendor donor demo (LVGL-free; direct panel draw — pattern reference only) |
| `devices/guition-jc4880p443c/community/seobaeksol-JC4880P443C_I_W/05_camera_csi.md` | Third-party pre-bring-up analysis of the vendor material ([seobaeksol/JC4880P443C_I_W](https://github.com/seobaeksol/JC4880P443C_I_W), Korean, no licence; community notes, not vendor documentation) |

## Verified boot/runtime log markers

- Tap Camera (first time): `app_video: driver: MIPI-CSI` … `width=1920 height=1080` …
  `Video Stream Start`, live preview appears full-screen.
- Tap preview: `Video Stream Stop`, menu returns, status "Ready".
- ISP pipeline: ov02c10 detected on SCCB 0x36 during `esp_video_init` (only on first Camera
  tap — init is lazy, nothing camera-related appears in the boot log).

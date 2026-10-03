# Third-party notice for `src/sample_hello_world`

The application code in `main/` is MIT (repository `LICENSE`). The full attribution list is the
repository's [THIRD_PARTY_NOTICES.md](../../THIRD_PARTY_NOTICES.md); this file lists only what
this project vendors or modifies.

## Vendored components (`components/`), all Apache-2.0

| Component | Version | Modified here |
|---|---|---|
| `bsp_extra` (Espressif, via the Guition package) | 0.0.2 | yes: `src/bsp_board_extra.c` (stereo-to-mono downmix for the mono ES8311) |
| `chmorgan__esp-audio-player` (Espressif, Chris Morgan) | 1.0.7 | yes: `audio_player.cpp` (ID3v2 tag skip), `audio_mp3.{h,cpp}` (give up after 500 consecutive decode errors) |
| `esp_cam_sensor` (Espressif) | 2.1.0 | no (Guition added the OV02C10 driver) |
| `espressif__esp32_p4_function_ev_board` (Espressif) | 5.2.3 | no (Guition wired the ST7701 panel) |
| `espressif__esp_hosted` (Espressif; bundles protobuf-c, BSD-2-Clause) | 2.12.12 | yes: `host/port/esp/freertos/src/port_esp_hosted_host_os.c`, `.../port_esp_hosted_host_sdio.c` (L2 cache-line alignment of PSRAM DMA buffers, libc realloc) |
| `espressif__esp_lcd_st7701` (Espressif) | 1.1.3 | no (Guition patched the init table) |
| `espressif__usb_host_uac` (Espressif) | 1.5.0 | yes: `uac_host.c`, `include/usb/uac_host.h` (descriptor walk fix, `uac_host_get_device_alt_ep_info`) |

The same seven directories, with further edits to `bsp_extra` and `esp-audio-player`, are in
`src/bedside_clock/components`.

## Derived code

`main/app_video.{c,h}` is ported from Espressif's CC0 `video_lcd_display` example. The
Toccata note table in `main/music_score.h` is the author's transcription of the opening of
J.S. Bach, BWV 565 (public domain work).

This project applies no patches to `managed_components/`. Build-time dependencies fetched by
`idf.py` (LVGL, esp_lvgl_port, esp_wifi_remote, esp_codec_dev, esp_video, libhelix-mp3 and the
rest) are not redistributed here; their licences are listed in section 3 of
THIRD_PARTY_NOTICES.md.

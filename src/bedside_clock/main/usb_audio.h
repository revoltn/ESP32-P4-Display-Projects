#pragma once
/* USB Audio Class (UAC) speaker support.
 *
 * PHASE 1 - DETECTION ONLY. This module brings up the USB host stack, notices
 * a USB audio output device arriving or leaving, surveys what formats it
 * advertises, and proves which of them will actually start a stream. It does
 * NOT yet carry any audio: every sound still goes to the ES8311 codec and the
 * built-in mono speaker. Routing arrives in phase 2, at which point this
 * module registers a bsp_extra_sink_t and bsp_extra_i2s_write() prefers it.
 *
 * Why the survey matters: the board's USB port is the same one the PC uses
 * for flashing and the serial console, so a USB speaker can only be attached
 * when the PC is not. There is no serial log during a real test. Everything
 * this module learns therefore has to be readable over WiFi instead:
 * GET /api/v1/usb (the full report), the usb_audio object in
 * GET /api/v1/status, and evlog lines that survive a reboot.
 *
 * Threading: usb_audio_init() installs the stack and returns immediately;
 * devices arrive asynchronously on the UAC driver's own task, which only sets
 * a flag and signals the module's worker task. Every accessor below is
 * lock-free or bounded, because the 1 s LVGL tick and the httpd task call
 * them. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "app_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Install the USB host + UAC driver and start watching for devices. Safe to
 * call twice. Never blocks waiting for a device. Called from boot_seq stage 1
 * (local hardware), skipped in safe mode like the camera is. */
void usb_audio_init(void);

/* True while a USB audio output device is enumerated. Lock-free. */
bool usb_audio_present(void);

/* Indicator state for the clock face (phase 3) and the API.
 *   SVC_OFF   host stack not installed (disabled, or install failed early)
 *   SVC_WAIT  stack up, no device attached
 *   SVC_UP    device attached and a stream format was proven to start
 *   SVC_FAIL  device attached but no advertised format would start */
svc_state_t usb_audio_service_state(void);

/* Snapshot of the attached device for GET /api/v1/status. Returns false when
 * nothing is attached (or the info lock was busy), in which case the outputs
 * are untouched. `product` may be empty if the device has no product string. */
bool usb_audio_device_info(char *product, size_t product_cap,
                           uint16_t *vid, uint16_t *pid,
                           uint32_t *rate, int *channels);

/* The human-readable descriptor survey served by GET /api/v1/usb: identity,
 * every alternate setting with channels/bit depth/rates/endpoint size, and
 * which format actually started. Copies into dst, NUL-terminated, and returns
 * the length. This replaces uac_host_printf_device_param(), which can only
 * reach the serial console and so is useless for this board's test setup. */
size_t usb_audio_report(char *dst, size_t cap);

#ifdef __cplusplus
}
#endif

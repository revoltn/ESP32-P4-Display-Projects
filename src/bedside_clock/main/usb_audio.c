/* USB Audio Class speaker - phase 1: detect, survey, prove. See usb_audio.h.
 *
 * Ported from src/sample_hello_world/main/feature_usb_audio.c, which plays a melody
 * on demand. The differences that matter here:
 *   - nothing is ever played: this phase only learns what the device can do;
 *   - the descriptor dump goes into a PSRAM string served over HTTP instead
 *     of to the serial console, which is unavailable while a speaker occupies
 *     the board's only USB port;
 *   - the stack never blocks boot and never waits for a device to appear.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_attr.h"
#include "esp_err.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "bsp_board_extra.h"
#include "usb/usb_host.h"
#include "usb/uac_host.h"

#include "app_common.h"
#include "evlog.h"
#include "usb_audio.h"

static const char *TAG = "usbaudio";

#define BIT_CONNECT    BIT0
#define BIT_DISCONNECT BIT1
#define BIT_RETRY      BIT2   /* a write failed: rebuild the stream */

/* A transient write failure should not mean mono until the speaker is
 * unplugged and back in, but an endlessly failing device must not be
 * retried forever either. Reset on every fresh connect. */
#define STREAM_RETRY_MAX 3

/* The report is display-only text, so it lives in PSRAM - the same reasoning
 * as ui_settings' log buffer: internal DMA-capable RAM is what the I2S
 * reconfigure at song start needs, and the USB host's own transfer buffers
 * already come out of that pool. */
#define REPORT_CAP 2048
EXT_RAM_BSS_ATTR static char s_report[REPORT_CAP];
static size_t s_report_len;

/* Guards s_report and the identity strings against a concurrent httpd read.
 * Bounded takes only: the httpd task must never park on this. */
static SemaphoreHandle_t s_info_mtx;

static EventGroupHandle_t s_ev;
static bool s_host_started;

/* Written by the UAC driver callback (on its own task), read everywhere else.
 * Lock-free by design - see the contract in app_common.h. */
static volatile bool    s_present;
static volatile uint8_t s_tx_addr;
static volatile uint8_t s_tx_iface;

/* Result of the trial stream start, written only by usb_audio_task. */
static volatile bool     s_format_ok;
static volatile uint32_t s_rate;
static volatile uint8_t  s_channels;
static char     s_product[UAC_STR_DESC_MAX_LENGTH + 1];
static uint16_t s_vid, s_pid;

/* ---------- live stream (phase 2: routing) ----------
 *
 * While a device is attached the stream stays OPEN, unlike the hello_world
 * reference which opens and closes around each melody. A decoder writing
 * continuously cannot afford an open/close per buffer.
 *
 * s_stream_mtx guards s_dev and the resampler state against the hot-unplug
 * path closing the device underneath a write. Every take is BOUNDED: a
 * timeout returns an error, which bsp_extra_i2s_write turns into a fallback
 * to the built-in speaker. Nothing here may ever block the alarm task. */
static SemaphoreHandle_t s_stream_mtx;
static uac_host_device_handle_t s_dev;      /* non-NULL = open */
static volatile bool s_stream_up;           /* open AND started */
static int  s_volume = 60;
static int  s_retries;        /* consecutive stream rebuilds, see STREAM_RETRY_MAX */

/* Raw USB bus census, independent of the UAC driver. When a speaker is
 * attached but never announces itself, this separates the two very different
 * causes: nothing on the bus at all (no VBUS, cable, or the device never
 * enumerated) versus a device that enumerated but whose audio interface the
 * UAC driver did not claim. Without a serial console - the speaker occupies
 * the port that provides it - this is the only way to tell them apart. */
static volatile int s_bus_devices = -1;   /* -1 = never scanned */
static int bus_scan(void);

/* A device that is already plugged in when the host starts is a different
 * case from one plugged into a running host: the audio driver registers its
 * client during boot, and a device that enumerated around the same moment can
 * end up on the bus without the driver ever announcing it. When the bus has a
 * device but nothing has been claimed, reinstall the driver so it takes a
 * fresh look. Bounded, because a device it simply cannot use must not loop. */
#define CLAIM_GRACE_TICKS 2      /* idle ticks (5 s each) before reinstalling */
#define CLAIM_RETRY_MAX   3
static int s_unclaimed_ticks;
static int s_claim_retries;

/* WHICH SOCKET. This board has two USB-C sockets. The HIGH-SPEED one
 * (schematic net ESP_USB) is the only one the host can use. The FULL-SPEED one
 * (net USB1P1) is wired to the PHY that USB-Serial/JTAG owns - it is the serial-console
 * socket - so a speaker plugged in there is invisible to the host. The speaker
 * goes in the socket that does NOT give the serial console; the PC can stay in the other.
 *
 * The host is installed exactly as the working hello_world does it: default
 * peripheral_map (= BIT0 = the high-speed controller on the P4) and the root
 * port powered by usb_host_install itself. Two things NOT to do, both tried and
 * both harmful:
 *   - peripheral_map = BIT0|BIT1 does not mean "both sockets": the library
 *     supports one root port, and when bit 1 is set it wins, moving the host
 *     onto the full-speed controller, whose pins reach neither socket;
 *   - root_port_unpowered + manual power cycling is unnecessary, and
 *     hub_root_stop() asserts internally. */
static bool uac_driver_install(void);

/* Negotiated device format. */
static uint32_t s_dev_rate;
static uint8_t  s_dev_ch;

/* Source format last announced by bsp_extra_codec_set_fs: 44100/stereo for
 * music, 16000/mono for the chime and the voice blip. */
static uint32_t s_src_rate = 44100;
static uint8_t  s_src_ch = 2;

/* Resampler carry-over. Linear interpolation with a Q16 phase that persists
 * across calls, plus the previous block's last frame, so buffer boundaries do
 * not click. Zero-order hold (what the reference uses) is fine for a melody
 * but audible on music. */
static int32_t s_pos_q16;
static int16_t s_prev[2];
static bool    s_have_prev;

/* Conversion scratch. PSRAM: uac_host_device_write copies into its own ring
 * buffer, and internal RAM is the scarce resource on this board. */
#define OUT_FRAMES 512
EXT_RAM_BSS_ATTR static int16_t s_out[OUT_FRAMES * 2];

static bool stream_lock(uint32_t ms)
{
    return s_stream_mtx != NULL &&
           xSemaphoreTake(s_stream_mtx, pdMS_TO_TICKS(ms)) == pdTRUE;
}

static void stream_unlock(void)
{
    if (s_stream_mtx != NULL) {
        xSemaphoreGive(s_stream_mtx);
    }
}

/* ---------- report building ---------- */

static void report_reset(void)
{
    s_report_len = 0;
    s_report[0] = '\0';
}

static void report_add(const char *fmt, ...)
{
    if (s_report_len + 1 >= REPORT_CAP) {
        return; /* full: keep what we have rather than wrap */
    }
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(s_report + s_report_len, REPORT_CAP - s_report_len, fmt, ap);
    va_end(ap);
    if (n > 0) {
        s_report_len += (size_t)n;
        if (s_report_len >= REPORT_CAP) {
            s_report_len = REPORT_CAP - 1; /* vsnprintf truncated */
        }
    }
}

static bool info_lock(uint32_t ms)
{
    return s_info_mtx != NULL &&
           xSemaphoreTake(s_info_mtx, pdMS_TO_TICKS(ms)) == pdTRUE;
}

static void info_unlock(void)
{
    if (s_info_mtx != NULL) {
        xSemaphoreGive(s_info_mtx);
    }
}

/* UAC string descriptors are wchar_t; keep only printable ASCII so the text is
 * safe for JSON, the serial log and an LVGL label alike. */
static void narrow_str(char *dst, size_t cap, const wchar_t *src)
{
    size_t i = 0;
    while (i + 1 < cap && src[i] != L'\0') {
        dst[i] = (src[i] >= 0x20 && src[i] < 0x7F) ? (char)src[i] : '.';
        i++;
    }
    dst[i] = '\0';
}

/* ---------- USB host plumbing ---------- */

/* Standard USB host library daemon: dispatches events, frees devices. */
static void usb_lib_task(void *arg)
{
    for (;;) {
        uint32_t flags = 0;
        usb_host_lib_handle_events(portMAX_DELAY, &flags);
        if (flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) {
            usb_host_device_free_all();
        }
    }
}

/* Runs on the UAC driver background task: record and signal, nothing more.
 * Opening the device from inside its own driver callback invites deadlock, so
 * the survey happens on usb_audio_task. */
static void uac_driver_cb(uint8_t addr, uint8_t iface_num,
                          const uac_host_driver_event_t event, void *arg)
{
    if (event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
        s_tx_addr = addr;
        s_tx_iface = iface_num;
        s_present = true;
        xEventGroupSetBits(s_ev, BIT_CONNECT);
    }
}

static void uac_device_cb(uac_host_device_handle_t handle,
                          const uac_host_device_event_t event, void *arg)
{
    if (event == UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
        s_present = false;
        xEventGroupSetBits(s_ev, BIT_DISCONNECT);
    }
}

/* ---------- format conversion + the sink ----------
 *
 * The attached speaker dictates the format: the Philips SPA3700 this was
 * built against offers exactly one, 48 kHz stereo 16-bit. Sources are
 * 44.1 kHz stereo (music) and 16 kHz mono (chime, voice blip), so something
 * always has to convert. Rate is linear-interpolated with a phase that
 * persists across calls; channels are duplicated or averaged.
 */

static void resampler_reset(void)
{
    s_pos_q16 = 0;
    s_prev[0] = 0;
    s_prev[1] = 0;
    s_have_prev = false;
}

/* One source frame as left/right, whatever the source channel count. */
static inline void src_frame(const int16_t *f, int16_t *l, int16_t *r)
{
    if (s_src_ch >= 2) {
        *l = f[0];
        *r = f[1];
    } else {
        *l = f[0];
        *r = f[0];
    }
}

/* Push one converted frame into the scratch buffer. */
static inline void out_frame(size_t idx, int16_t l, int16_t r)
{
    if (s_dev_ch >= 2) {
        s_out[idx * 2] = l;
        s_out[idx * 2 + 1] = r;
    } else {
        s_out[idx] = (int16_t)(((int32_t)l + r) / 2);
    }
}

/* Convert and write one caller buffer. Caller holds s_stream_mtx. */
static esp_err_t stream_write_locked(const int16_t *in, size_t frames,
                                     uint32_t timeout_ms)
{
    if (s_dev == NULL || s_src_rate == 0 || s_dev_rate == 0 || frames == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    /* Q16 source samples consumed per output sample. */
    const int32_t step = (int32_t)(((uint64_t)s_src_rate << 16) / s_dev_rate);
    const size_t out_bytes_per_frame = (s_dev_ch >= 2) ? 4 : 2;

    if (!s_have_prev) {
        /* First block of a stream: start on the first real frame rather than
         * interpolating against silence. */
        src_frame(in, &s_prev[0], &s_prev[1]);
        s_have_prev = true;
        s_pos_q16 = 0;
    }

    size_t n_out = 0;
    for (;;) {
        int32_t ip = s_pos_q16 >> 16;
        if (ip >= (int32_t)frames - 1) {
            break; /* need frame ip+1 to interpolate towards */
        }
        uint32_t fr = (uint32_t)(s_pos_q16 & 0xFFFF);
        int16_t al, ar, bl, br;
        if (ip < 0) {
            al = s_prev[0];
            ar = s_prev[1];
        } else {
            src_frame(in + (size_t)ip * s_src_ch, &al, &ar);
        }
        src_frame(in + (size_t)(ip + 1) * s_src_ch, &bl, &br);
        int16_t l = (int16_t)(al + (((int32_t)bl - al) * (int32_t)fr >> 16));
        int16_t r = (int16_t)(ar + (((int32_t)br - ar) * (int32_t)fr >> 16));
        out_frame(n_out++, l, r);
        s_pos_q16 += step;

        if (n_out == OUT_FRAMES) {
            esp_err_t err = uac_host_device_write(s_dev, (uint8_t *)s_out,
                                                  n_out * out_bytes_per_frame,
                                                  pdMS_TO_TICKS(timeout_ms));
            if (err != ESP_OK) {
                return err;
            }
            n_out = 0;
        }
    }

    if (n_out > 0) {
        esp_err_t err = uac_host_device_write(s_dev, (uint8_t *)s_out,
                                              n_out * out_bytes_per_frame,
                                              pdMS_TO_TICKS(timeout_ms));
        if (err != ESP_OK) {
            return err;
        }
    }

    /* Rebase the phase onto the next block and remember this block last
     * frame as its virtual index -1. */
    s_pos_q16 -= (int32_t)((uint32_t)frames << 16);
    src_frame(in + (size_t)(frames - 1) * s_src_ch, &s_prev[0], &s_prev[1]);
    return ESP_OK;
}

/* ---------- bsp_extra_sink_t callbacks ---------- */

static bool sink_active(void)
{
    return s_stream_up && s_present;
}

static esp_err_t sink_write(const void *buf, size_t len, uint32_t timeout_ms)
{
    if (!s_stream_up || buf == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    /* The decoder passes portMAX_DELAY. Cap it: the alarm task walks this
     * path too, and an unbounded wait here is exactly the shape of bug that
     * once took the alarm down with a task watchdog panic. */
    if (timeout_ms > 1000) {
        timeout_ms = 1000;
    }
    /* Short bound: if the hot-unplug path holds the lock, falling back to the
     * built-in speaker beats waiting. */
    if (!stream_lock(50)) {
        return ESP_ERR_TIMEOUT;
    }
    /* s_src_ch is read under the lock: sink_set_fs can change it between a
     * song and the chime, and a stale value here would misframe the buffer. */
    size_t frames = len / ((size_t)s_src_ch * sizeof(int16_t));
    esp_err_t err = stream_write_locked((const int16_t *)buf, frames, timeout_ms);
    if (err != ESP_OK && s_stream_up) {
        /* One line per failure run, not per buffer. Drop to the built-in
         * speaker immediately, then ask the task to rebuild the stream: a
         * one-off underrun should not cost stereo until the next replug. */
        s_stream_up = false;
        ui_log("USB audio: write failed (%s) - built-in speaker for now",
               esp_err_to_name(err));
        evlog(EV_ERR, "usb: write fail %.18s", esp_err_to_name(err));
        if (s_present && s_retries < STREAM_RETRY_MAX) {
            xEventGroupSetBits(s_ev, BIT_RETRY);
        }
    }
    stream_unlock();
    return err;
}

/* The source format changed: a song started, or the chime took over. */
static esp_err_t sink_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    if (bits_cfg != 16) {
        return ESP_ERR_NOT_SUPPORTED; /* the whole app is 16-bit */
    }
    if (!stream_lock(200)) {
        return ESP_ERR_TIMEOUT;
    }
    uint8_t src_ch = (ch == I2S_SLOT_MODE_MONO) ? 1 : 2;
    if (rate != s_src_rate || src_ch != s_src_ch) {
        s_src_rate = rate;
        s_src_ch = src_ch;
        resampler_reset();
        ESP_LOGI(TAG, "source now %lu Hz / %u ch -> device %lu Hz / %u ch",
                 (unsigned long)rate, src_ch,
                 (unsigned long)s_dev_rate, s_dev_ch);
    }
    stream_unlock();
    return ESP_OK;
}

static esp_err_t sink_volume(int volume)
{
    s_volume = volume;
    if (!s_stream_up || !stream_lock(100)) {
        return ESP_ERR_TIMEOUT;
    }
    esp_err_t err = ESP_ERR_INVALID_STATE;
    if (s_dev != NULL) {
        /* Many devices have no host-settable volume; that is not a failure,
         * they just use their own knob. */
        err = uac_host_device_set_volume(s_dev, volume);
    }
    stream_unlock();
    return err;
}

static const bsp_extra_sink_t s_sink = {
    .active = sink_active,
    .write  = sink_write,
    .set_fs = sink_set_fs,
    .volume = sink_volume,
};

/* Stop and release the stream. Safe to call when nothing is open. */
static void stream_teardown(void)
{
    if (!stream_lock(2000)) {
        /* Cannot safely close under a concurrent write; leave it to the next
         * connect, which reopens from scratch. */
        s_stream_up = false;
        return;
    }
    s_stream_up = false;
    if (s_dev != NULL) {
        uac_host_device_stop(s_dev);
        uac_host_device_close(s_dev);
        s_dev = NULL;
    }
    resampler_reset();
    stream_unlock();
}

/* ---------- the survey ---------- */

typedef struct { uint32_t rate; uint8_t ch; } cand_t;

/* Collect every 16-bit PCM (rate, channels) the device advertises, writing
 * each alternate setting into the report as it goes. Caller holds s_info_mtx
 * when it wants the report text to be consistent. */
static int collect_candidates(uac_host_device_handle_t dev, uint8_t alt_count,
                              cand_t *cands, int cap)
{
    int n = 0;
    for (uint8_t alt = 1; alt <= alt_count; alt++) {
        uac_host_dev_alt_param_t p;
        if (uac_host_get_device_alt_param(dev, alt, &p) != ESP_OK) {
            report_add("  alt %u: unreadable\n", alt);
            continue;
        }
        uint16_t ep_mps = 0;
        uint8_t ep_attr = 0, ep_addr = 0;
        uac_host_get_device_alt_ep_info(dev, alt, &ep_mps, &ep_attr, &ep_addr);

        if (p.sample_freq_type == 0) {
            report_add("  alt %u: %u ch %u-bit %lu-%lu Hz cont, ep 0x%02x mps %u\n",
                       alt, p.channels, p.bit_resolution,
                       (unsigned long)p.sample_freq_lower,
                       (unsigned long)p.sample_freq_upper, ep_addr, ep_mps);
        } else {
            report_add("  alt %u: %u ch %u-bit, ep 0x%02x mps %u, rates",
                       alt, p.channels, p.bit_resolution, ep_addr, ep_mps);
            for (int i = 0; i < p.sample_freq_type && i < UAC_FREQ_NUM_MAX; i++) {
                report_add(" %lu", (unsigned long)p.sample_freq[i]);
            }
            if (p.sample_freq_type > UAC_FREQ_NUM_MAX) {
                report_add(" (+%d not captured)", p.sample_freq_type - UAC_FREQ_NUM_MAX);
            }
            report_add("\n");
        }

        if (p.bit_resolution != 16) {
            continue; /* the whole app is 16-bit PCM */
        }
        if (p.sample_freq_type == 0) {
            /* continuous range: 48 kHz if it falls inside, else the near end */
            uint32_t r = 48000;
            if (r < p.sample_freq_lower) {
                r = p.sample_freq_lower;
            }
            if (r > p.sample_freq_upper) {
                r = p.sample_freq_upper;
            }
            if (n < cap) {
                cands[n++] = (cand_t){ r, p.channels };
            }
        } else {
            for (int i = 0; i < p.sample_freq_type && i < UAC_FREQ_NUM_MAX; i++) {
                bool dup = false;
                for (int j = 0; j < n; j++) {
                    if (cands[j].rate == p.sample_freq[i] &&
                        cands[j].ch == p.channels) {
                        dup = true;
                    }
                }
                if (!dup && n < cap) {
                    cands[n++] = (cand_t){ p.sample_freq[i], p.channels };
                }
            }
        }
    }
    return n;
}

/* Preference order: 48 kHz first (the rate nearly every device does best),
 * then descending. Phase 2 will additionally prefer the source rate, so a
 * 44.1 kHz song needs no resampling on a device that offers it. */
static void sort_candidates(cand_t *c, int n)
{
    for (int i = 1; i < n; i++) {
        cand_t tmp = c[i];
        int j = i - 1;
        while (j >= 0) {
            uint32_t a = (c[j].rate == 48000) ? UINT32_MAX : c[j].rate;
            uint32_t b = (tmp.rate == 48000) ? UINT32_MAX : tmp.rate;
            if (a >= b) {
                break;
            }
            c[j + 1] = c[j];
            j--;
        }
        c[j + 1] = tmp;
    }
}

/* Open the device, write the survey, and try each advertised format until one
 * starts. Nothing is ever written to the stream, so this is silent. The device
 * is closed again: phase 1 holds no resources between probes. */
static void survey_device(void)
{
    uac_host_device_config_t dev_config = {
        .addr = s_tx_addr,
        .iface_num = s_tx_iface,
        .buffer_size = 19200,
        .buffer_threshold = 4800,
        .callback = uac_device_cb,
        .callback_arg = NULL,
    };
    uac_host_device_handle_t dev = NULL;
    esp_err_t err = uac_host_device_open(&dev_config, &dev);
    if (err != ESP_OK) {
        ui_log("USB audio: device open failed: %s", esp_err_to_name(err));
        evlog(EV_ERR, "usb: open failed %.20s", esp_err_to_name(err));
        if (info_lock(1000)) {
            report_reset();
            report_add("device open failed: %s\n", esp_err_to_name(err));
            info_unlock();
        }
        return;
    }

    char product[sizeof s_product] = "";
    char manu[UAC_STR_DESC_MAX_LENGTH + 1] = "";
    uac_host_dev_info_t info;
    uint8_t alt_count = 0;
    uint16_t vid = 0, pid = 0;
    if (uac_host_get_device_info(dev, &info) == ESP_OK) {
        narrow_str(product, sizeof product, info.iProduct);
        narrow_str(manu, sizeof manu, info.iManufacturer);
        alt_count = info.iface_alt_num;
        vid = info.VID;
        pid = info.PID;
    }

    cand_t cands[16];
    int n_cand = 0;

    if (info_lock(2000)) {
        report_reset();
        report_add("USB audio device attached\n");
        report_add("  product      : %s\n", product[0] ? product : "(none)");
        report_add("  manufacturer : %s\n", manu[0] ? manu : "(none)");
        report_add("  VID:PID      : 0x%04x:0x%04x\n", vid, pid);
        report_add("  address      : %u, interface %u, %u alt setting(s)\n",
                   s_tx_addr, s_tx_iface, alt_count);
        report_add("advertised output formats:\n");
        n_cand = collect_candidates(dev, alt_count, cands, 16);
        strlcpy(s_product, product, sizeof s_product);
        s_vid = vid;
        s_pid = pid;
        info_unlock();
    } else {
        n_cand = collect_candidates(dev, alt_count, cands, 16);
    }

    if (n_cand == 0) {
        ui_log("USB audio: %s offers no 16-bit PCM output",
               product[0] ? product : "device");
        evlog(EV_ERR, "usb: no 16-bit format");
        if (info_lock(1000)) {
            report_add("\nNO 16-bit PCM output format advertised - "
                       "this device cannot be used.\n");
            info_unlock();
        }
        uac_host_device_close(dev);
        s_format_ok = false;
        return;
    }

    sort_candidates(cands, n_cand);

    /* Trial start. A format can be advertised and still refuse to start when
     * the combination exceeds the endpoint max packet size
     * (ESP_ERR_INVALID_SIZE), so the only way to know is to ask. */
    if (info_lock(1000)) {
        report_add("trial start (silent):\n");
        info_unlock();
    }
    uint32_t rate = 0;
    uint8_t channels = 0;
    for (int i = 0; i < n_cand; i++) {
        uac_host_stream_config_t stream = {
            .channels = cands[i].ch,
            .bit_resolution = 16,
            .sample_freq = cands[i].rate,
            .flags = 0,
        };
        err = uac_host_device_start(dev, &stream);
        if (info_lock(1000)) {
            report_add("  %lu Hz / %u ch -> %s\n",
                       (unsigned long)cands[i].rate, cands[i].ch,
                       err == ESP_OK ? "OK" : esp_err_to_name(err));
            info_unlock();
        }
        if (err == ESP_OK) {
            rate = cands[i].rate;
            channels = cands[i].ch;
            break; /* phase 2: leave it running - this is now the live stream */
        }
    }

    s_rate = rate;
    s_channels = channels;
    s_format_ok = (rate != 0);

    if (info_lock(1000)) {
        if (rate != 0) {
            report_add("\nUSABLE: %lu Hz, %u channel(s), 16-bit.\n",
                       (unsigned long)rate, channels);
            report_add("Source formats to convert: 44100/stereo (music), "
                       "16000/mono (chime, voice).\n");
        } else {
            report_add("\nNo advertised format would start.\n");
        }
        info_unlock();
    }

    if (rate == 0) {
        uac_host_device_close(dev);
        ui_log("USB audio: %s - no format would start",
               product[0] ? product : "device");
        evlog(EV_ERR, "usb: no format starts");
        return;
    }

    /* Adopt it as the live output. From the moment s_stream_up goes true,
     * bsp_extra_i2s_write prefers this device over the built-in speaker. */
    if (!stream_lock(2000)) {
        uac_host_device_close(dev);
        ui_log("USB audio: stream lock busy, staying on the built-in speaker");
        evlog(EV_ERR, "usb: stream lock busy");
        return;
    }
    s_dev = dev;
    s_dev_rate = rate;
    s_dev_ch = channels;
    resampler_reset();
    /* The decoder only calls set_fs when the format CHANGES, so a speaker
     * plugged in mid-song would otherwise never learn the source format. */
    uint32_t cur_rate = 0, cur_bits = 0;
    i2s_slot_mode_t cur_ch = I2S_SLOT_MODE_STEREO;
    if (bsp_extra_codec_get_fs(&cur_rate, &cur_bits, &cur_ch) && cur_bits == 16) {
        s_src_rate = cur_rate;
        s_src_ch = (cur_ch == I2S_SLOT_MODE_MONO) ? 1 : 2;
    }
    uac_host_device_set_volume(s_dev, s_volume);
    s_stream_up = true;
    stream_unlock();

    ui_log("USB audio: %s ready - %lu Hz, %u ch (now the active output)",
           product[0] ? product : "device", (unsigned long)rate, channels);
    evlog(EV_BOOT, "usb: %.14s %luHz %uch ON", product,
          (unsigned long)rate, channels);
}

/* Own task, so neither the UAC driver callback task nor the shared worker is
 * held up by device enumeration (control transfers, tens of ms). */
static void usb_audio_task(void *arg)
{
    for (;;) {
        EventBits_t bits = xEventGroupWaitBits(s_ev,
                                               BIT_CONNECT | BIT_DISCONNECT | BIT_RETRY,
                                               pdTRUE, pdFALSE, pdMS_TO_TICKS(5000));
        if (bits == 0) {
            /* Idle tick: census the bus. Journalled only on a change, so the
             * ring is not flooded, but a speaker that enumerates without ever
             * reaching the UAC driver still leaves a trace. */
            int n = bus_scan();
            if (n != s_bus_devices) {
                int was = s_bus_devices;
                s_bus_devices = n;
                if (was >= 0 || n > 0) {
                    ui_log("USB bus: %d device(s) enumerated (was %d), "
                           "audio device %s", n, was,
                           s_present ? "claimed" : "NOT claimed");
                    evlog(EV_BOOT, "usb bus: %d dev, audio %s", n,
                          s_present ? "ok" : "unclaimed");
                }
            }

            /* A device sitting on the bus that nothing has claimed is the
             * signature of a speaker that was already plugged in when the
             * host came up. Give the driver a grace period, then reinstall it
             * so it re-examines every device currently attached. */
            if (n > 0 && !s_present) {
                if (++s_unclaimed_ticks >= CLAIM_GRACE_TICKS &&
                    s_claim_retries < CLAIM_RETRY_MAX) {
                    s_unclaimed_ticks = 0;
                    s_claim_retries++;
                    ui_log("USB audio: %d device(s) on the bus but no audio "
                           "claimed - reinstalling the driver (%d/%d)",
                           n, s_claim_retries, CLAIM_RETRY_MAX);
                    evlog(EV_BOOT, "usb: rescan %d/%d", s_claim_retries,
                          CLAIM_RETRY_MAX);
                    if (uac_host_uninstall() == ESP_OK) {
                        vTaskDelay(pdMS_TO_TICKS(300));
                        if (!uac_driver_install()) {
                            ui_log("USB audio: driver reinstall failed - "
                                   "staying on the built-in speaker");
                        }
                    } else {
                        ui_log("USB audio: driver uninstall refused "
                               "(a device is still open)");
                    }
                }
            } else {
                s_unclaimed_ticks = 0;
            }
            continue;
        }
        if (bits & BIT_CONNECT) {
            s_retries = 0;
            s_claim_retries = 0;
            s_unclaimed_ticks = 0;
            survey_device();
        }
        if ((bits & BIT_RETRY) && !(bits & BIT_DISCONNECT) && s_present) {
            /* Rebuild after a failed write. Bounded by STREAM_RETRY_MAX so a
             * device that cannot hold a stream ends up on the built-in
             * speaker for good rather than thrashing. */
            s_retries++;
            ui_log("USB audio: rebuilding the stream (attempt %d/%d)",
                   s_retries, STREAM_RETRY_MAX);
            stream_teardown();
            vTaskDelay(pdMS_TO_TICKS(200));
            if (s_present) {
                survey_device();
            }
        }
        if (bits & BIT_DISCONNECT) {
            /* Close the stream before anything else: from here on every write
             * falls through to the built-in speaker, which is what keeps a
             * ringing alarm audible when the speaker is yanked mid-song. */
            stream_teardown();
            s_format_ok = false;
            s_rate = 0;
            s_channels = 0;
            ui_log("USB audio: device disconnected");
            evlog(EV_BOOT, "usb: disconnected");
            if (info_lock(1000)) {
                report_reset();
                report_add("No USB audio device attached "
                           "(the last one disconnected).\n");
                s_product[0] = '\0';
                s_vid = 0;
                s_pid = 0;
                info_unlock();
            }
        }
    }
}

/* Install the UAC driver. Also used by the rescan below, so the two paths
 * cannot drift apart.
 *
 * Priority 5 matches the audio decoder and the hello_world reference: the UAC
 * background task completes isochronous transfers, so it has to sit above the
 * LVGL task (4) or playback glitches while the UI redraws. Watch ui_watch for
 * stalls now that phase 2 streams through it. */
static bool uac_driver_install(void)
{
    uac_host_driver_config_t uac_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = tskNO_AFFINITY,
        .callback = uac_driver_cb,
        .callback_arg = NULL,
    };
    esp_err_t err = uac_host_install(&uac_config);
    if (err != ESP_OK) {
        ui_log("USB audio: uac_host_install failed: %s", esp_err_to_name(err));
        evlog(EV_ERR, "usb: uac install %.20s", esp_err_to_name(err));
        if (info_lock(1000)) {
            report_reset();
            report_add("uac_host_install failed: %s\n", esp_err_to_name(err));
            info_unlock();
        }
        return false;
    }
    return true;
}

/* Count what the USB host has enumerated, whatever driver owns it. */
static int bus_scan(void)
{
    uint8_t addrs[16];
    int num = 0;
    if (usb_host_device_addr_list_fill(sizeof(addrs), addrs, &num) != ESP_OK) {
        return -1;
    }
    return num;
}

/* ---------- public API ---------- */

void usb_audio_init(void)
{
    if (s_host_started) {
        return;
    }
    /* USB host transfer buffers come from internal RAM
     * (CONFIG_USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM is off) and internal RAM is
     * this board's scarce resource, so record what the stack costs. */
    unsigned before = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    s_ev = xEventGroupCreate();
    s_info_mtx = xSemaphoreCreateMutex();
    s_stream_mtx = xSemaphoreCreateMutex();
    if (s_ev == NULL || s_info_mtx == NULL || s_stream_mtx == NULL) {
        ui_log("USB audio: out of memory, disabled");
        evlog(EV_ERR, "usb: no memory for init");
        return;
    }
    report_reset();
    report_add("No USB audio device attached.\n");

    /* Identical to the hello_world host that drove this speaker on this board:
     * default peripheral (high-speed controller), port powered at install.
     * See the WHICH SOCKET note at the top of the file before changing it. */
    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ui_log("USB audio: usb_host_install failed: %s", esp_err_to_name(err));
        evlog(EV_ERR, "usb: host install %.20s", esp_err_to_name(err));
        report_reset();
        report_add("usb_host_install failed: %s\n", esp_err_to_name(err));
        return;
    }
    if (xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 2, NULL) != pdPASS) {
        ui_log("USB audio: usb_lib task create failed");
        evlog(EV_ERR, "usb: lib task create failed");
        usb_host_uninstall();
        return;
    }
    if (xTaskCreate(usb_audio_task, "usb_audio", 4096, NULL, 3, NULL) != pdPASS) {
        ui_log("USB audio: task create failed");
        evlog(EV_ERR, "usb: task create failed");
        return;
    }

    if (!uac_driver_install()) {
        return;
    }

    /* From here the BSP prefers this sink whenever sink_active() says a
     * stream is running; until then every write goes to the codec as before. */
    bsp_extra_sink_register(&s_sink);

    s_host_started = true;
    unsigned after = (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    unsigned cost = before > after ? before - after : 0;
    ui_log("USB audio: host up, internal RAM %u -> %u (-%u B)",
           before, after, cost);
    ESP_LOGI(TAG, "USB host + UAC driver installed");
    evlog(EV_BOOT, "usb: host up, -%u B", cost);
}

bool usb_audio_present(void)
{
    return s_present;
}

svc_state_t usb_audio_service_state(void)
{
    if (!s_host_started) {
        return SVC_OFF;
    }
    if (!s_present) {
        return SVC_WAIT;
    }
    return s_format_ok ? SVC_UP : SVC_FAIL;
}

bool usb_audio_device_info(char *product, size_t product_cap,
                           uint16_t *vid, uint16_t *pid,
                           uint32_t *rate, int *channels)
{
    if (!s_present || !info_lock(200)) {
        return false;
    }
    if (product != NULL && product_cap > 0) {
        strlcpy(product, s_product, product_cap);
    }
    if (vid != NULL) {
        *vid = s_vid;
    }
    if (pid != NULL) {
        *pid = s_pid;
    }
    info_unlock();
    if (rate != NULL) {
        *rate = s_rate;
    }
    if (channels != NULL) {
        *channels = s_channels;
    }
    return true;
}

size_t usb_audio_report(char *dst, size_t cap)
{
    if (dst == NULL || cap == 0) {
        return 0;
    }
    dst[0] = '\0';
    if (!s_host_started) {
        strlcpy(dst, "USB host stack is not running "
                     "(safe mode, or the install failed - check /api/v1/events).\n",
                cap);
        return strlen(dst);
    }
    if (!info_lock(500)) {
        strlcpy(dst, "USB audio report busy, try again.\n", cap);
        return strlen(dst);
    }
    strlcpy(dst, s_report, cap);
    info_unlock();

    /* Live bus census appended, so a report that says "no device attached"
     * still distinguishes an empty bus from one the UAC driver ignored. */
    int n = bus_scan();
    size_t len = strlen(dst);
    snprintf(dst + len, cap - len,
             "\n-- USB bus --\n"
             "devices enumerated : %d%s\n"
             "audio interface    : %s\n"
             "host socket        : high-speed (the one that is NOT the serial console)\n"
             "driver rescans     : %d of %d\n"
             "%s",
             n, n < 0 ? " (scan failed)" : "",
             s_present ? "claimed by the UAC driver" : "not claimed",
             s_claim_retries, CLAIM_RETRY_MAX,
             n == 0 ? "Nothing on the bus. Check the speaker is in the\n"
                      "high-speed socket - the serial-console socket cannot host.\n"
                    : (n > 0 && !s_present
                       ? "A device enumerated but its audio interface was not\n"
                         "claimed: wrong class/subclass, or it needs more power.\n"
                       : ""));
    return strlen(dst);
}

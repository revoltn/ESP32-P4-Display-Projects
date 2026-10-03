/* USB Audio: detect a USB Audio Class device (e.g. a Jabra speakerphone or
 * a Jabra Link dongle) on the P4's USB-OTG host port and play audio through
 * it. The device does all the Bluetooth (if any) itself — the board just
 * sees a USB sound card.
 *
 * Besides the menu feature (plays a melody), this file exposes
 * usb_audio_ready() / usb_audio_play_mono16() so other features (e.g.
 * Record & Playback) can route their audio to the USB speaker. */
#include <math.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"
#include "usb/uac_host.h"

#include "app_common.h"

static const char *TAG = "usbaudio";

#define DEV_WAIT_MS   15000
#define BIT_TX_CONN   BIT0
#define BIT_TX_GONE   BIT1

#define TWO_PI 6.28318530718f

static EventGroupHandle_t s_ev;
static bool s_host_started;
static volatile uint8_t s_tx_addr;
static volatile uint8_t s_tx_iface;
static volatile bool s_tx_present;

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

/* Runs on the UAC driver background task. */
static void uac_driver_cb(uint8_t addr, uint8_t iface_num,
                          const uac_host_driver_event_t event, void *arg)
{
    if (event == UAC_HOST_DRIVER_EVENT_TX_CONNECTED) {
        s_tx_addr = addr;
        s_tx_iface = iface_num;
        s_tx_present = true;
        xEventGroupSetBits(s_ev, BIT_TX_CONN);
    }
}

static void uac_device_cb(uac_host_device_handle_t handle,
                          const uac_host_device_event_t event, void *arg)
{
    if (event == UAC_HOST_DRIVER_EVENT_DISCONNECTED) {
        s_tx_present = false;
        xEventGroupSetBits(s_ev, BIT_TX_GONE);
    }
}

static bool usb_host_ensure_started(void)
{
    if (s_host_started) {
        return true;
    }
    s_ev = xEventGroupCreate();

    usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };
    esp_err_t err = usb_host_install(&host_config);
    if (err != ESP_OK) {
        ui_log("usb_host_install failed: %s", esp_err_to_name(err));
        return false;
    }
    if (xTaskCreate(usb_lib_task, "usb_lib", 4096, NULL, 2, NULL) != pdTRUE) {
        ui_log("usb lib task create failed");
        return false;
    }

    uac_host_driver_config_t uac_config = {
        .create_background_task = true,
        .task_priority = 5,
        .stack_size = 4096,
        .core_id = tskNO_AFFINITY,
        .callback = uac_driver_cb,
        .callback_arg = NULL,
    };
    err = uac_host_install(&uac_config);
    if (err != ESP_OK) {
        ui_log("uac_host_install failed: %s", esp_err_to_name(err));
        return false;
    }
    s_host_started = true;
    return true;
}

bool usb_audio_ready(int wait_ms)
{
    if (!usb_host_ensure_started()) {
        return false;
    }
    if (s_tx_present) {
        return true;
    }
    if (wait_ms > 0) {
        xEventGroupWaitBits(s_ev, BIT_TX_CONN, pdTRUE, pdFALSE,
                            pdMS_TO_TICKS(wait_ms));
    }
    return s_tx_present;
}

static void narrow_str(char *dst, size_t cap, const wchar_t *src)
{
    size_t i = 0;
    while (i + 1 < cap && src[i] != L'\0') {
        dst[i] = (src[i] >= 0x20 && src[i] < 0x7F) ? (char)src[i] : '.';
        i++;
    }
    dst[i] = '\0';
}

/* Open the connected TX device and start a 16-bit stream. Candidate formats
 * come from the device's own descriptors; a combination can still exceed the
 * endpoint's max packet size (ESP_ERR_INVALID_SIZE), so try candidates until
 * one starts: 48 kHz first, then descending rate. */
static bool usb_stream_open(uac_host_device_handle_t *out_dev,
                            uint32_t *out_rate, int *out_ch,
                            char *prod, size_t prod_cap, bool verbose)
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
        ui_log("uac_host_device_open failed: %s", esp_err_to_name(err));
        return false;
    }

    uac_host_dev_info_t info;
    uint8_t alt_count = 0;
    if (prod_cap > 0) {
        prod[0] = '\0';
    }
    if (uac_host_get_device_info(dev, &info) == ESP_OK) {
        narrow_str(prod, prod_cap, info.iProduct);
        alt_count = info.iface_alt_num;
        if (verbose) {
            static char manu[33];
            narrow_str(manu, sizeof manu, info.iManufacturer);
            ui_log("USB audio device: %s %s (VID 0x%04x PID 0x%04x)",
                   manu, prod, info.VID, info.PID);
            uac_host_printf_device_param(dev); /* serial-only dump */
        }
    }

    typedef struct { uint32_t rate; uint8_t ch; } cand_t;
    cand_t cands[16];
    int n_cand = 0;
    for (uint8_t alt = 1; alt <= alt_count; alt++) {
        uac_host_dev_alt_param_t p;
        if (uac_host_get_device_alt_param(dev, alt, &p) != ESP_OK) {
            continue;
        }
        if (verbose) {
            uint16_t ep_mps = 0;
            uint8_t ep_attr = 0, ep_addr = 0;
            uac_host_get_device_alt_ep_info(dev, alt, &ep_mps, &ep_attr, &ep_addr);
            ui_log("  alt %u: %u ch, %u-bit, %lu Hz (+%d more), ep 0x%02x mps %u",
                   alt, p.channels, p.bit_resolution,
                   (unsigned long)(p.sample_freq_type ? p.sample_freq[0]
                                                      : p.sample_freq_lower),
                   p.sample_freq_type ? p.sample_freq_type - 1 : 0,
                   ep_addr, ep_mps);
        }
        if (p.bit_resolution != 16) {
            continue;
        }
        if (p.sample_freq_type == 0) {
            uint32_t r = 48000;
            if (r < p.sample_freq_lower) r = p.sample_freq_lower;
            if (r > p.sample_freq_upper) r = p.sample_freq_upper;
            if (n_cand < 16) {
                cands[n_cand++] = (cand_t){ r, p.channels };
            }
        } else {
            for (int i = 0; i < p.sample_freq_type && i < UAC_FREQ_NUM_MAX; i++) {
                bool dup = false;
                for (int j = 0; j < n_cand; j++) {
                    if (cands[j].rate == p.sample_freq[i] &&
                        cands[j].ch == p.channels) {
                        dup = true;
                    }
                }
                if (!dup && n_cand < 16) {
                    cands[n_cand++] = (cand_t){ p.sample_freq[i], p.channels };
                }
            }
        }
    }
    if (n_cand == 0) {
        ui_log("No 16-bit PCM output format found - cannot play.");
        uac_host_device_close(dev);
        return false;
    }

    /* Order: 48 kHz first, then higher-rate-first. */
    for (int i = 1; i < n_cand; i++) {
        cand_t tmp = cands[i];
        int j = i - 1;
        while (j >= 0) {
            uint32_t a = (cands[j].rate == 48000) ? UINT32_MAX : cands[j].rate;
            uint32_t b = (tmp.rate == 48000) ? UINT32_MAX : tmp.rate;
            if (a >= b) {
                break;
            }
            cands[j + 1] = cands[j];
            j--;
        }
        cands[j + 1] = tmp;
    }

    uint32_t rate = 0;
    int channels = 0;
    for (int i = 0; i < n_cand; i++) {
        uac_host_stream_config_t stream = {
            .channels = cands[i].ch,
            .bit_resolution = 16,
            .sample_freq = cands[i].rate,
            .flags = 0,
        };
        err = uac_host_device_start(dev, &stream);
        if (err == ESP_OK) {
            rate = cands[i].rate;
            channels = cands[i].ch;
            break;
        }
        ui_log("  start %lu Hz / %u ch failed: %s",
               (unsigned long)cands[i].rate, cands[i].ch, esp_err_to_name(err));
    }
    if (rate == 0) {
        ui_log("Every advertised format failed to start.");
        uac_host_device_close(dev);
        return false;
    }

    if (uac_host_device_set_volume(dev, 60) != ESP_OK && verbose) {
        ui_log("(device has no host volume control - using its own)");
    }

    *out_dev = dev;
    *out_rate = rate;
    *out_ch = channels;
    return true;
}

static void usb_stream_close(uac_host_device_handle_t dev)
{
    /* Let the ring buffer drain before tearing the stream down. */
    vTaskDelay(pdMS_TO_TICKS(300));
    uac_host_device_stop(dev);
    uac_host_device_close(dev);
}

/* Play a mono 16-bit PCM buffer through the USB speaker, converting to the
 * negotiated rate/channels (zero-order-hold resampling — fine for voice). */
bool usb_audio_play_mono16(const int16_t *pcm, size_t frames, uint32_t src_rate)
{
    if (!s_tx_present || pcm == NULL || frames == 0 || src_rate == 0) {
        return false;
    }
    uac_host_device_handle_t dev;
    uint32_t rate;
    int channels;
    static char prod[33];
    if (!usb_stream_open(&dev, &rate, &channels, prod, sizeof prod, false)) {
        return false;
    }
    ui_log("USB playback on %s @ %lu Hz, %d ch",
           prod[0] ? prod : "USB device", (unsigned long)rate, channels);

    static int16_t out[1024];
    const int max_frames = (int)(sizeof out / sizeof out[0]) / channels;
    const size_t out_total = (size_t)((uint64_t)frames * rate / src_rate);
    size_t done = 0;
    bool ok = true;

    while (done < out_total && ok) {
        int n = (int)(out_total - done);
        if (n > max_frames) {
            n = max_frames;
        }
        for (int i = 0; i < n; i++) {
            size_t src = (size_t)((uint64_t)(done + i) * src_rate / rate);
            if (src >= frames) {
                src = frames - 1;
            }
            for (int c = 0; c < channels; c++) {
                out[i * channels + c] = pcm[src];
            }
        }
        esp_err_t err = uac_host_device_write(dev, (uint8_t *)out,
                                              n * channels * sizeof(int16_t),
                                              pdMS_TO_TICKS(1000));
        if (err != ESP_OK || !s_tx_present) {
            ui_log("USB write stopped: %s", esp_err_to_name(err));
            ok = false;
        }
        done += n;
    }
    usb_stream_close(dev);
    return ok;
}

/* Additive organ voice, same character as the Toccata feature. */
static inline float organ(float phase)
{
    return sinf(phase) + 0.5f * sinf(2.0f * phase)
         + 0.25f * sinf(3.0f * phase) + 0.15f * sinf(4.0f * phase);
}

/* Toccata in D minor, opening phrase. f = 0 is a rest. */
typedef struct { float f; int ms; } tone_t;
static const tone_t MELODY[] = {
    { 880.00f, 250 }, { 783.99f, 150 }, { 880.00f, 550 }, { 0, 200 },
    { 783.99f, 130 }, { 698.46f, 130 }, { 659.26f, 130 }, { 587.33f, 130 },
    { 554.37f, 450 }, { 587.33f, 900 },
};

static bool play_melody(uac_host_device_handle_t dev,
                        uint32_t rate, int channels)
{
    static int16_t buf[1024];
    const int max_frames = (int)(sizeof buf / sizeof buf[0]) / channels;

    for (size_t ni = 0; ni < sizeof MELODY / sizeof MELODY[0]; ni++) {
        const float w = TWO_PI * MELODY[ni].f / rate;
        const int total = (int)(rate / 1000 * MELODY[ni].ms);
        const int attack = (int)(rate * 8 / 1000);
        const int release = (int)(rate * 25 / 1000);

        for (int done = 0; done < total; ) {
            int n = total - done;
            if (n > max_frames) {
                n = max_frames;
            }
            for (int i = 0; i < n; i++) {
                int16_t s = 0;
                if (MELODY[ni].f > 0.0f) {
                    const int t = done + i;
                    float env = 1.0f;
                    if (t < attack) {
                        env = (float)t / attack;
                    } else if (t >= total - release) {
                        env = (float)(total - t) / release;
                    }
                    s = (int16_t)(organ(w * t) * 6000.0f * env);
                }
                for (int c = 0; c < channels; c++) {
                    buf[i * channels + c] = s;
                }
            }
            esp_err_t err = uac_host_device_write(dev, (uint8_t *)buf,
                                                  n * channels * sizeof(int16_t),
                                                  pdMS_TO_TICKS(1000));
            if (err != ESP_OK || !s_tx_present) {
                ui_log("USB write stopped: %s", esp_err_to_name(err));
                return false;
            }
            done += n;
        }
    }
    return true;
}

void feature_usb_audio_run(void)
{
    if (!usb_host_ensure_started()) {
        ui_set_status("USB audio: host init failed");
        return;
    }

    if (!s_tx_present) {
        ui_log("Waiting for a USB audio device (up to %d s)...", DEV_WAIT_MS / 1000);
        ui_log("Plug the Jabra (or its dongle) into the USB host port.");
        ui_set_status("USB audio: waiting for device...");
        if (!usb_audio_ready(DEV_WAIT_MS)) {
            ui_log("No USB audio device detected.");
            ui_log("If it is plugged in, the port may not be powering it (VBUS).");
            ui_set_status("USB audio: no device found");
            return;
        }
    }

    uac_host_device_handle_t dev;
    uint32_t rate;
    int channels;
    static char prod[33];
    if (!usb_stream_open(&dev, &rate, &channels, prod, sizeof prod, true)) {
        ui_set_status("USB audio: stream failed");
        return;
    }

    ui_log("Playing Toccata opening @ %lu Hz, %d ch...",
           (unsigned long)rate, channels);
    ui_set_status("USB audio: playing on %s", prod);
    bool ok = play_melody(dev, rate, channels);
    usb_stream_close(dev);

    if (ok) {
        ui_log("USB audio playback done.");
        ui_set_status("USB audio: played on %s", prod);
    } else {
        ui_set_status("USB audio: playback interrupted");
    }
}

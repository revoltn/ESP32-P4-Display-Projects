/* voice.c - on-device voice control via esp-sr.
 *
 * Pipeline: voice_feed task (core 1, prio 6, internal stack) reads 16 kHz
 * mono from the ES8311 mic and feeds the AFE; voice_detect task (core 1,
 * prio 5, PSRAM stack) fetches AFE results, watches for the "Hi ESP" wake
 * word (WakeNet9), then runs a ~6 s MultiNet7-EN command window and hands
 * the winning command id to voice_actions_dispatch().  All AFE/model
 * buffers are forced to PSRAM - internal RAM is the scarce resource on
 * this board (esp_hosted needs the headroom).
 *
 * Since 2026-09-20 every wake also snapshots the last 2 s of audio into a
 * RAM clip store (served by the HTTP API for training data) and, when the
 * AI server is configured, has whisper confirm the phrase before a command
 * is dispatched - see "wake clips" below and VOICE.md "Tuning".
 *
 * Capture pauses while the audio player is active: each song calls
 * bsp_extra_codec_set_fs(44100,...) which closes/reopens the record device
 * under us.  We poll audio_player_get_state() (safe before player init -
 * static instance, IDLE==0) and re-assert 16 kHz + mic gain on resume.
 * The beep-chime alarm runs at 16 kHz directly, so "Hi ESP -> snooze"
 * works over the chime (full duplex verified on this board 2026-09-02).
 *
 * The status badge lives on lv_layer_sys(), above the settings and alarm
 * overlays (both on lv_layer_top()), and is only touched under
 * bsp_display_lock().
 */
#include <ctype.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "audio_player.h"
#include "lvgl.h"
#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

#include "esp_afe_config.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_mn_iface.h"
#include "esp_mn_models.h"
#include "esp_mn_speech_commands.h"
#include "model_path.h"

#include "app_common.h"
#include "settings.h"
#include "wifi_mgr.h"
#include "alarm.h"
#include "ui_play.h"
#include "music_player.h"
#include "localai_client.h"
#include "voice_actions.h"
#include "voice.h"
#include "wake_mww.h"
#include "fonts/app_fonts.h"

static const char *TAG = "voice";

#define VOICE_MIC_GAIN_DB     24      /* echo-demo-proven default */
#define VOICE_DIGITAL_GAIN    12      /* ~+21 dB software boost before the AFE:
                                       * the analog mic peaks ~-23 dBFS even
                                       * point-blank (self-test), far below
                                       * what WakeNet was trained on */
/* WakeNet threshold: settings voice_wake_thr (percent; model default 0.63
 * was too strict for a non-American accent on this quiet mic, 0.50 is the
 * hand-tuned value, 0.40 = hair trigger). Applied in voice_start() and live
 * via voice_apply_wake_threshold(). */
#define WAKE_PREROLL_MS       2500    /* AFE output kept for wake snapshots */
#define WAKE_CLIP_MS          2000    /* snapshot length; WakeNet fires ~0.2-
                                       * 0.3 s after the phrase ends */
#define VOICE_CLIPS_MAX       16      /* PSRAM clip store (~64 KB each) */
#define VERIFY_WAIT_MS        2500    /* a recognised command waits this long
                                       * for the verifier's verdict, then it
                                       * is accepted anyway (fail-open) */
#define VOICE_FEED_STACK      8192    /* internal: calls into I2S driver */
#define VOICE_DETECT_STACK    16384   /* PSRAM: inference bookkeeping */
#define VOICE_CORE            1       /* keep clear of LVGL/hosted on core 0 */
#define VOICE_MN_TIMEOUT_MS   6000    /* command window after wake */

static srmodel_list_t           *s_models;
static const esp_afe_sr_iface_t *s_afe;
static esp_afe_sr_data_t        *s_afe_data;
static const esp_mn_iface_t     *s_mn;
static model_iface_data_t       *s_mn_data;
static int16_t                  *s_feed_buf;   /* internal RAM */
static int                       s_feed_samples;
static volatile bool             s_running;    /* pipeline created */
static volatile bool             s_enabled;    /* user toggle (settings) */
static volatile bool             s_start_failed; /* last voice_start() gave up */
static bool                      s_use_mww;     /* wake word via wake_mww (tflite-
                                                 * micro) instead of WakeNet; decided
                                                 * once in voice_start() */
static volatile bool             s_capture_paused; /* feed task is not reading the
                                                    * mic (player owns the codec);
                                                    * written by voice_feed only,
                                                    * read lock-free by the 1 s
                                                    * clock tick for the MIC icon */

/* ---------- wake clips: pre-roll ring, clip store, verifier ----------
 * While waiting for the wake word every AFE output chunk goes into a PSRAM
 * ring (WAKE_PREROLL_MS). When WakeNet fires, the last WAKE_CLIP_MS of it -
 * which ends ~0.2-0.3 s after the phrase - is snapshotted as a WAV and
 *   - kept in the clip store (VOICE_CLIPS_MAX, oldest evicted) for the PC
 *     to pull via /api/v1/voice/clips: real false wakes are the best hard
 *     negatives a wake-word model can be trained on, and
 *   - if settings voice_wake_verify is on and the AI server is reachable,
 *     POSTed to whisper. The command window opens immediately (optimistic),
 *     but a recognised command is only dispatched once the verdict is in,
 *     and a REJECT closes the window. No verdict within VERIFY_WAIT_MS =
 *     fail-open, so a slow or dead LocalAI never mutes the clock.
 * POST /api/v1/voice/record captures N s through the same path (mic + gain
 * + AFE), which is exactly what a replacement wake model will hear. */

typedef struct {
    uint32_t id;              /* 0 = empty slot */
    voice_clip_kind_t kind;
    voice_verdict_t verdict;
    time_t   epoch;
    uint32_t uptime_s;
    uint16_t ms;
    char     text[48];
    uint8_t *wav;             /* PSRAM: 44-byte header + PCM */
    size_t   len;
} voice_clip_t;

#define WAKE_PREROLL_SAMPLES ((size_t)WAKE_PREROLL_MS * AUDIO_SAMPLE_RATE / 1000)

static voice_clip_t      s_clips[VOICE_CLIPS_MAX];
static uint32_t          s_clip_next_id = 1;
static SemaphoreHandle_t s_clip_mtx;

static int16_t  *s_preroll;         /* PSRAM ring of AFE output */
static size_t    s_preroll_pos;     /* next write index */
static size_t    s_preroll_fill;    /* valid samples (saturates at ring size) */

static volatile voice_verdict_t s_verify_state = VOICE_VERDICT_NONE;
static volatile uint32_t        s_verify_seq;  /* wake the verdict belongs to */

/* recording request handed from the API task to voice_detect */
static volatile int       s_rec_want_ms;   /* > 0: please record this much */
static volatile uint32_t  s_rec_clip_id;   /* result, 0 = failed */
static SemaphoreHandle_t  s_rec_done;

/* ---------- status badge (lv_layer_sys, all screens) ---------- */

static lv_obj_t   *s_badge;
static lv_obj_t   *s_badge_label;
static lv_timer_t *s_badge_timer;

static void badge_hide_timer_cb(lv_timer_t *t)
{
    if (s_badge != NULL) {
        lv_obj_add_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    }
    lv_timer_pause(t);
}

/* Show the badge; hide_ms > 0 auto-hides, 0 keeps it up. Any-task safe. */
static void badge_show(const char *text, uint32_t color, uint32_t hide_ms)
{
    if (!app_display_ready() || !bsp_display_lock(500)) {
        return;
    }
    if (s_badge == NULL) {
        s_badge = lv_obj_create(lv_layer_sys());
        lv_obj_set_size(s_badge, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_style_bg_color(s_badge, lv_color_hex(0x101418), 0);
        lv_obj_set_style_bg_opa(s_badge, LV_OPA_90, 0);
        lv_obj_set_style_border_width(s_badge, 2, 0);
        lv_obj_set_style_radius(s_badge, 14, 0);
        lv_obj_set_style_pad_hor(s_badge, 18, 0);
        lv_obj_set_style_pad_ver(s_badge, 10, 0);
        lv_obj_align(s_badge, LV_ALIGN_TOP_MID, 0, 8);
        lv_obj_clear_flag(s_badge, LV_OBJ_FLAG_SCROLLABLE);
        s_badge_label = lv_label_create(s_badge);
        lv_obj_set_style_text_font(s_badge_label, &app_font_20, 0);
        lv_obj_center(s_badge_label);
        s_badge_timer = lv_timer_create(badge_hide_timer_cb, 1500, NULL);
        lv_timer_pause(s_badge_timer);
    }
    lv_label_set_text(s_badge_label, text);
    lv_obj_set_style_text_color(s_badge_label, lv_color_hex(color), 0);
    lv_obj_set_style_border_color(s_badge, lv_color_hex(color), 0);
    lv_obj_remove_flag(s_badge, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_badge);
    if (hide_ms > 0) {
        lv_timer_set_period(s_badge_timer, hide_ms);
        lv_timer_reset(s_badge_timer);
        lv_timer_resume(s_badge_timer);
    } else {
        lv_timer_pause(s_badge_timer);
    }
    bsp_display_unlock();
}

/* Green frame around the whole screen while the command window is open. */
static lv_obj_t *s_frame;

static void listen_frame_show(bool on)
{
    if (!app_display_ready() || !bsp_display_lock(500)) {
        return;
    }
    if (s_frame == NULL) {
        s_frame = lv_obj_create(lv_layer_sys());
        lv_obj_set_size(s_frame, lv_pct(100), lv_pct(100));
        lv_obj_center(s_frame);
        lv_obj_set_style_bg_opa(s_frame, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_color(s_frame, lv_color_hex(0x2ECC40), 0);
        lv_obj_set_style_border_width(s_frame, 8, 0);
        lv_obj_set_style_border_opa(s_frame, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(s_frame, 0, 0);
        lv_obj_set_style_pad_all(s_frame, 0, 0);
        /* touches must fall through to whatever is underneath */
        lv_obj_remove_flag(s_frame, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(s_frame, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(s_frame, LV_OBJ_FLAG_HIDDEN);
    }
    if (on) {
        lv_obj_remove_flag(s_frame, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_frame);
    } else {
        lv_obj_add_flag(s_frame, LV_OBJ_FLAG_HIDDEN);
    }
    bsp_display_unlock();
}

/* Short "I'm listening" blip; only when nothing else owns the speaker. */
static void wake_blip(void)
{
    if (alarm_is_active() ||
        audio_player_get_state() != AUDIO_PLAYER_STATE_IDLE) {
        return;
    }
    static int16_t buf[960]; /* 60 ms @ 16 kHz */
    const float w = 2.0f * (float)M_PI * 1000.0f / AUDIO_SAMPLE_RATE;
    for (int i = 0; i < 960; i++) {
        float env = i < 80 ? i / 80.0f : (i > 880 ? (960 - i) / 80.0f : 1.0f);
        buf[i] = (int16_t)(sinf(w * i) * 6000.0f * env);
    }
    size_t written;
    bsp_extra_i2s_write(buf, sizeof(buf), &written, 200);
}

/* ---------- capture / detection ---------- */

/* Re-open the codec for voice capture (after boot or after the player
 * repointed it at 44.1 kHz). */
static bool voice_codec_reclaim(void)
{
    esp_err_t err = bsp_extra_codec_set_fs(AUDIO_SAMPLE_RATE, AUDIO_BITS,
                                           AUDIO_SLOT_MODE);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "set_fs failed: %s", esp_err_to_name(err));
        return false;
    }
    int gain = settings_get()->voice_mic_gain;
    if (gain <= 0) {
        gain = VOICE_MIC_GAIN_DB;
    }
    bsp_extra_codec_in_gain_set(gain);
    ESP_LOGI(TAG, "mic reclaimed: 16k mono, gain %d dB", gain);
    return true;
}

/* ---------- loopback self-test ----------
 * Plays a 1 kHz tone through the speaker while reading the mic, then checks
 * (Goertzel) that the captured audio peaks at exactly 1 kHz vs an off-bin
 * control frequency.  A healthy path shows the tone >15 dB above control;
 * broken sample framing/rate smears it to ~0 dB.  Runs once at init. */

static float goertzel_power(const int16_t *x, int n, float freq)
{
    float w = 2.0f * (float)M_PI * freq / AUDIO_SAMPLE_RATE;
    float c = 2.0f * cosf(w);
    float s1 = 0, s2 = 0;
    for (int i = 0; i < n; i++) {
        float s0 = (float)x[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return s1 * s1 + s2 * s2 - c * s1 * s2;
}

static void selftest_tone_task(void *arg)
{
    /* 1 kHz @ 16 kHz = 16 samples/cycle; 512 is a whole number of cycles,
     * so looping the buffer stays phase-continuous */
    static int16_t tone[512];
    const float w = 2.0f * (float)M_PI * 1000.0f / AUDIO_SAMPLE_RATE;
    TickType_t end = xTaskGetTickCount() + pdMS_TO_TICKS(700);
    while (xTaskGetTickCount() < end) {
        for (int i = 0; i < 512; i++) {
            tone[i] = (int16_t)(sinf(w * i) * 8000.0f);
        }
        size_t written;
        /* Codec-only on purpose: this tone is measured back through the
         * ES8311 microphone, so it must come out of the on-board speaker
         * even when a USB speaker has taken over everything else. */
        bsp_extra_i2s_write_codec(tone, sizeof(tone), &written, 200);
    }
    vTaskDelete(NULL);
}

static void voice_selftest(void)
{
    const int n = 4096; /* 256 ms @ 16 kHz */
    int16_t *rec = heap_caps_malloc((size_t)n * sizeof(int16_t),
                                    MALLOC_CAP_SPIRAM);
    if (rec == NULL) {
        return;
    }
    if (xTaskCreatePinnedToCore(selftest_tone_task, "voice_tone", 3072, NULL, 5,
                                NULL, VOICE_CORE) != pdPASS) {
        ui_log("Voice self-test skipped (tone task create failed)");
        heap_caps_free(rec);
        return;
    }
    size_t got = 0;
    bsp_extra_i2s_read(rec, 3200, &got, 500);           /* skip first 100 ms */
    esp_err_t err = bsp_extra_i2s_read(rec, (size_t)n * sizeof(int16_t),
                                       &got, 1000);
    vTaskDelay(pdMS_TO_TICKS(400)); /* let the tone task finish */
    if (err == ESP_OK && got == (size_t)n * sizeof(int16_t)) {
        int64_t sq = 0;
        for (int i = 0; i < n; i++) {
            sq += (int32_t)rec[i] * rec[i];
        }
        int rms = (int)sqrtf((float)(sq / n));
        float p_sig = goertzel_power(rec, n, 1000.0f);
        float p_ref = goertzel_power(rec, n, 1466.0f);
        float db = 10.0f * log10f((p_sig + 1.0f) / (p_ref + 1.0f));
        ui_log("Voice self-test: 1kHz tone %+.1f dB vs control, mic rms %d %s",
               db, rms, (db > 15.0f && rms > 100) ? "[OK]" : "[SUSPECT]");
    } else {
        ui_log("Voice self-test: mic read failed (%s)", esp_err_to_name(err));
    }
    heap_caps_free(rec);
}

static void voice_feed_task(void *arg)
{
    const size_t want = (size_t)s_feed_samples * sizeof(int16_t);
    bool paused = false;
    int err_streak = 0;
    uint32_t pause_ticks = 0;
    uint32_t feeds = 0;
    int64_t sq_acc = 0;
    size_t sq_n = 0;

    while (s_running) {
        audio_player_state_t pst = audio_player_get_state();
        /* Hold off for the WHOLE music session, not just while the decoder
         * runs: between tracks the player reports IDLE for the entire
         * multi-second download, and reclaiming the codec to 16 kHz in that
         * window races the next song's 44.1 kHz reconfigure (the decoder
         * only re-applies the format when it CHANGES, so a late reclaim
         * used to leave a whole track playing at the wrong rate). The
         * chime-mode alarm keeps voice alive: it runs at 16 kHz with the
         * player idle and the music engine stopped. */
        bool music_busy = (music_state() != MUSIC_STATE_IDLE);
        if (!s_enabled || pst != AUDIO_PLAYER_STATE_IDLE || music_busy) {
            if (!paused) {
                paused = true;
                s_capture_paused = true;
                pause_ticks = 0;
                ESP_LOGI(TAG, "capture paused (%s, player state %d)",
                         s_enabled ? "player/music active" : "voice disabled",
                         (int)pst);
            }
            if (++pause_ticks % 300 == 0) { /* every ~10 s */
                ESP_LOGW(TAG, "still paused (player state %d, enabled %d)",
                         (int)pst, (int)s_enabled);
            }
            /* keep the AFE pipeline moving with silence so an open command
             * window times out normally instead of wedging with WakeNet
             * disabled (mic can't be read: the player owns the codec) */
            memset(s_feed_buf, 0, want);
            s_afe->feed(s_afe_data, s_feed_buf);
            vTaskDelay(pdMS_TO_TICKS(32)); /* one chunk of real time */
            continue;
        }
        if (paused) {
            vTaskDelay(pdMS_TO_TICKS(300)); /* let the player's teardown settle */
            if (!voice_codec_reclaim()) {
                vTaskDelay(pdMS_TO_TICKS(1000));
                continue;
            }
            s_afe->reset_buffer(s_afe_data);
            paused = false;
            s_capture_paused = false; /* only once the mic is really back */
            ESP_LOGI(TAG, "capture resumed");
        }

        size_t got = 0;
        esp_err_t err = bsp_extra_i2s_read(s_feed_buf, want, &got, 1000);
        if (err != ESP_OK || got != want) {
            if (err_streak++ == 0) {
                ESP_LOGE(TAG, "i2s read err: %s (got %u/%u)",
                         esp_err_to_name(err), (unsigned)got, (unsigned)want);
            }
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        err_streak = 0;

        /* digital pre-gain with saturation (see VOICE_DIGITAL_GAIN) */
        for (int i = 0; i < s_feed_samples; i++) {
            int32_t v = (int32_t)s_feed_buf[i] * VOICE_DIGITAL_GAIN;
            if (v > 32767) {
                v = 32767;
            } else if (v < -32768) {
                v = -32768;
            }
            s_feed_buf[i] = (int16_t)v;
        }
        s_afe->feed(s_afe_data, s_feed_buf);

        /* mic-health heartbeat: RMS of everything fed in the last ~8 s
         * (post-gain, i.e. what the wake model actually sees) */
        for (int i = 0; i < s_feed_samples; i += 8) {
            sq_acc += (int32_t)s_feed_buf[i] * s_feed_buf[i];
            sq_n++;
        }
        if (++feeds % 256 == 0) {
            int rms = sq_n ? (int)sqrtf((float)(sq_acc / (int64_t)sq_n)) : 0;
            ESP_LOGI(TAG, "mic alive: rms=%d feeds=%u", rms, (unsigned)feeds);
            sq_acc = 0;
            sq_n = 0;
        }
    }
    vTaskDelete(NULL);
}

/* ---------- free-speech capture ("play song <name>") ----------
 * After the "play song"/"playlist" grammar command, keep the border up and
 * record the user's next utterance (VAD-endpointed, AFE-processed 16 kHz
 * mono) into a WAV, then hand it to the worker: LocalAI whisper transcribes
 * it and music_player resolves & plays it. */

#define CAPTURE_MAX_MS        8000   /* longest utterance we keep */
#define CAPTURE_WAIT_MS       5000   /* give up if no speech starts */
#define CAPTURE_SILENCE_MS    600    /* end of utterance: every ms here is
                                      * user-perceived latency on top of the
                                      * STT round-trip; 600 still rides out
                                      * mid-title pauses */
#define CAPTURE_FETCH_MS      32     /* one AFE fetch = 512 samples @16k */

typedef enum {
    VQ_SONG = 0,
    VQ_PLAYLIST,
    VQ_VERIFY,      /* wake clip -> whisper -> accept/reject the wake */
} vq_kind_t;

typedef struct {
    uint8_t *wav;       /* owned by the job (freed after transcription) */
    size_t len;
    vq_kind_t kind;
    uint32_t clip_id;   /* VQ_VERIFY: clip to annotate (0 = not stored) */
    uint32_t seq;       /* VQ_VERIFY: s_verify_seq at submission */
} voice_query_t;

static void wav_header(uint8_t *p, uint32_t pcm_bytes)
{
    uint32_t rate = AUDIO_SAMPLE_RATE, byterate = rate * 2, riff = 36 + pcm_bytes;
    memcpy(p, "RIFF", 4);       memcpy(p + 4, &riff, 4);
    memcpy(p + 8, "WAVEfmt ", 8);
    uint32_t fmtlen = 16;       memcpy(p + 16, &fmtlen, 4);
    uint16_t fmt = 1, ch = 1, align = 2, bits = 16;
    memcpy(p + 20, &fmt, 2);    memcpy(p + 22, &ch, 2);
    memcpy(p + 24, &rate, 4);   memcpy(p + 28, &byterate, 4);
    memcpy(p + 32, &align, 2);  memcpy(p + 34, &bits, 2);
    memcpy(p + 36, "data", 4);  memcpy(p + 40, &pcm_bytes, 4);
}

/* ---------- clip store ---------- */

static void clips_init(void)
{
    if (s_clip_mtx == NULL) {
        s_clip_mtx = xSemaphoreCreateMutex();
    }
    if (s_rec_done == NULL) {
        s_rec_done = xSemaphoreCreateBinary();
    }
}

/* Takes ownership of wav (PSRAM). Returns the new id, 0 on failure. */
static uint32_t clip_store(voice_clip_kind_t kind, uint8_t *wav, size_t len,
                           uint16_t ms, voice_verdict_t verdict)
{
    if (s_clip_mtx == NULL || wav == NULL) {
        heap_caps_free(wav);
        return 0;
    }
    xSemaphoreTake(s_clip_mtx, portMAX_DELAY);
    int slot = 0;
    uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < VOICE_CLIPS_MAX; i++) {
        if (s_clips[i].id == 0) {
            slot = i;
            break;
        }
        if (s_clips[i].id < oldest) {
            oldest = s_clips[i].id;
            slot = i;
        }
    }
    voice_clip_t *c = &s_clips[slot];
    heap_caps_free(c->wav);
    memset(c, 0, sizeof(*c));
    c->id = s_clip_next_id++;
    c->kind = kind;
    c->verdict = verdict;
    c->ms = ms;
    c->wav = wav;
    c->len = len;
    time_t now = time(NULL);
    c->epoch = now > 1000000000 ? now : 0;
    c->uptime_s = (uint32_t)(esp_timer_get_time() / 1000000);
    uint32_t id = c->id;
    xSemaphoreGive(s_clip_mtx);
    return id;
}

static void clip_set_verdict(uint32_t id, voice_verdict_t v, const char *text)
{
    if (s_clip_mtx == NULL || id == 0) {
        return;
    }
    xSemaphoreTake(s_clip_mtx, portMAX_DELAY);
    for (int i = 0; i < VOICE_CLIPS_MAX; i++) {
        if (s_clips[i].id == id) {
            s_clips[i].verdict = v;
            if (text != NULL) {
                strlcpy(s_clips[i].text, text, sizeof(s_clips[i].text));
            }
            break;
        }
    }
    xSemaphoreGive(s_clip_mtx);
}

int voice_clips_list(voice_clip_info_t *out, int cap)
{
    if (s_clip_mtx == NULL || out == NULL || cap <= 0) {
        return 0;
    }
    int n = 0;
    xSemaphoreTake(s_clip_mtx, portMAX_DELAY);
    for (int i = 0; i < VOICE_CLIPS_MAX && n < cap; i++) {
        const voice_clip_t *c = &s_clips[i];
        if (c->id == 0) {
            continue;
        }
        out[n].id = c->id;
        out[n].kind = c->kind;
        out[n].verdict = c->verdict;
        out[n].epoch = c->epoch;
        out[n].uptime_s = c->uptime_s;
        out[n].ms = c->ms;
        out[n].len = c->len;
        strlcpy(out[n].text, c->text, sizeof(out[n].text));
        n++;
    }
    xSemaphoreGive(s_clip_mtx);
    /* ascending id = oldest first (insertion sort, n <= 16) */
    for (int i = 1; i < n; i++) {
        voice_clip_info_t t = out[i];
        int j = i - 1;
        while (j >= 0 && out[j].id > t.id) {
            out[j + 1] = out[j];
            j--;
        }
        out[j + 1] = t;
    }
    return n;
}

size_t voice_clip_copy(uint32_t id, uint8_t *dst, size_t cap)
{
    if (s_clip_mtx == NULL || id == 0) {
        return 0;
    }
    size_t got = 0;
    xSemaphoreTake(s_clip_mtx, portMAX_DELAY);
    for (int i = 0; i < VOICE_CLIPS_MAX; i++) {
        if (s_clips[i].id == id && s_clips[i].wav != NULL) {
            if (dst == NULL) {
                got = s_clips[i].len;
            } else if (cap >= s_clips[i].len) {
                memcpy(dst, s_clips[i].wav, s_clips[i].len);
                got = s_clips[i].len;
            }
            break;
        }
    }
    xSemaphoreGive(s_clip_mtx);
    return got;
}

void voice_clips_clear(void)
{
    if (s_clip_mtx == NULL) {
        return;
    }
    xSemaphoreTake(s_clip_mtx, portMAX_DELAY);
    for (int i = 0; i < VOICE_CLIPS_MAX; i++) {
        heap_caps_free(s_clips[i].wav);
        memset(&s_clips[i], 0, sizeof(s_clips[i]));
    }
    xSemaphoreGive(s_clip_mtx);
}

/* ---------- pre-roll ring (voice_detect task only) ---------- */

static void preroll_push(const int16_t *pcm, size_t n)
{
    if (s_preroll == NULL) {
        return;
    }
    while (n > 0) {
        size_t k = WAKE_PREROLL_SAMPLES - s_preroll_pos;
        if (k > n) {
            k = n;
        }
        memcpy(s_preroll + s_preroll_pos, pcm, k * sizeof(int16_t));
        s_preroll_pos = (s_preroll_pos + k) % WAKE_PREROLL_SAMPLES;
        pcm += k;
        n -= k;
        s_preroll_fill += k;
        if (s_preroll_fill > WAKE_PREROLL_SAMPLES) {
            s_preroll_fill = WAKE_PREROLL_SAMPLES;
        }
    }
}

/* The last `ms` of the ring as a complete WAV in PSRAM (caller frees). */
static uint8_t *preroll_snapshot(int ms, size_t *out_len)
{
    if (s_preroll == NULL) {
        return NULL;
    }
    size_t want = (size_t)ms * AUDIO_SAMPLE_RATE / 1000;
    if (want > s_preroll_fill) {
        want = s_preroll_fill;
    }
    if (want == 0) {
        return NULL;
    }
    uint8_t *wav = heap_caps_malloc(44 + want * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (wav == NULL) {
        return NULL;
    }
    int16_t *pcm = (int16_t *)(wav + 44);
    size_t start = (s_preroll_pos + WAKE_PREROLL_SAMPLES - want) % WAKE_PREROLL_SAMPLES;
    size_t first = WAKE_PREROLL_SAMPLES - start;
    if (first > want) {
        first = want;
    }
    memcpy(pcm, s_preroll + start, first * sizeof(int16_t));
    memcpy(pcm + first, s_preroll, (want - first) * sizeof(int16_t));
    wav_header(wav, (uint32_t)(want * sizeof(int16_t)));
    *out_len = 44 + want * sizeof(int16_t);
    return wav;
}

/* ---------- verifier ----------
 * Whisper renders "Hi ESP" as "Hi ESP", "Hi, E.S.P.", "High ESP", "Hi yes P",
 * "I ESP"... and "Hey Mr Clock" as "Hey Mr. Clock", "Hey mister clock",
 * "Hey Mr Clark"... Compare on letters only (lowercase, everything else
 * dropped) and accept any of these substrings. Deliberately lenient: the
 * verifier's job is to throw out TV, music and unrelated speech, not to
 * grade diction. Covers both wake phrases so either model can be embedded. */
static const char *const k_wake_accept[] = {
    "esp", "hisp", "highsp", "heysp", "yesp", "hiasp", "hiisp", "iesp",
    "mrclock", "misterclock", "mrclark", "misterclark", "mrcloak", "mistaclock",
    "mrclok", "misterclok", "mistercloak", "mrklock",
};

static bool wake_text_matches(const char *text)
{
    char compact[64];
    size_t n = 0;
    for (const char *p = text; *p != '\0' && n < sizeof(compact) - 1; p++) {
        if (isalpha((unsigned char)*p)) {
            compact[n++] = (char)tolower((unsigned char)*p);
        }
    }
    compact[n] = '\0';
    for (size_t i = 0; i < sizeof(k_wake_accept) / sizeof(k_wake_accept[0]); i++) {
        if (strstr(compact, k_wake_accept[i]) != NULL) {
            return true;
        }
    }
    return false;
}

/* STT then hand the text to the music engine. Runs on its own task (below):
 * the whisper round-trip blocks for up to 45 s, which used to park the
 * shared worker - and with it every settings save, WiFi reconnect and API
 * reconcile - behind one spoken request. */
static void voice_query_job(void *arg)
{
    voice_query_t *q = (voice_query_t *)arg;
    char text[96] = "";
    esp_err_t err = ai_transcribe_wav(q->wav, q->len, text, sizeof(text));
    heap_caps_free(q->wav);
    if (q->kind == VQ_VERIFY) {
        voice_verdict_t v;
        if (err != ESP_OK) {
            /* fail-open: a dead STT must not mute the clock */
            v = VOICE_VERDICT_OK;
            strlcpy(text, "(stt failed)", sizeof(text));
        } else {
            v = wake_text_matches(text) ? VOICE_VERDICT_OK : VOICE_VERDICT_REJECT;
        }
        clip_set_verdict(q->clip_id, v, text);
        if (q->seq == s_verify_seq) {
            s_verify_state = v;
        }
        ui_log("Voice: verifier heard \"%s\" -> %s", text,
               v == VOICE_VERDICT_OK ? "accept" : "REJECT");
        heap_caps_free(q);
        return;
    }
    if (err == ESP_OK) {
        ui_log("Voice: heard \"%s\"", text);
        badge_show(text, 0x4FC3F7, 2500);
        if (q->kind == VQ_PLAYLIST) {
            music_play_playlist_query(text);
        } else {
            music_play_song_query(text);
        }
    } else {
        ui_log("Voice: transcription failed");
        badge_show("Didn't catch that", 0xFFB74D, 2000);
    }
    heap_caps_free(q);
}

/* Dedicated STT task, created on first use. PSRAM stack is fine: this path
 * does HTTP, the LVGL lock and music commands, never a flash write (see the
 * stack rule in app_common.h / docs). Requests are processed one at a time;
 * the depth-1 queue lets one more wait while the previous one transcribes. */
static QueueHandle_t s_stt_q;

static void stt_task(void *arg)
{
    voice_query_t *q;
    for (;;) {
        if (xQueueReceive(s_stt_q, &q, portMAX_DELAY) == pdTRUE && q != NULL) {
            voice_query_job(q);
        }
    }
}

static bool stt_submit(voice_query_t *q)
{
    if (s_stt_q == NULL) {
        s_stt_q = xQueueCreate(1, sizeof(voice_query_t *));
        if (s_stt_q == NULL) {
            return false;
        }
        /* prio 3: below the LVGL task (4); the whisper upload is bulk TLS */
        if (xTaskCreateWithCaps(stt_task, "voice_stt", 16384, NULL, 3, NULL,
                                MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
            vQueueDelete(s_stt_q);
            s_stt_q = NULL;
            return false;
        }
    }
    return xQueueSend(s_stt_q, &q, 0) == pdTRUE;
}

/* runs on the voice_detect task, border still up, wakenet still disabled */
static void capture_query(bool playlist)
{
    if (!ai_is_configured()) {
        badge_show("AI server not configured", 0xFFB74D, 2500);
        return;
    }
    if (app_display_ready() && bsp_display_lock(1000)) {
        ui_play_open();
        bsp_display_unlock();
    }
    badge_show(playlist ? LV_SYMBOL_AUDIO "  Say the playlist name..."
                        : LV_SYMBOL_AUDIO "  Say the song name...",
               0x4FC3F7, 0);

    const size_t max_pcm = (CAPTURE_MAX_MS / 1000) * AUDIO_SAMPLE_RATE * 2;
    uint8_t *buf = heap_caps_malloc(44 + max_pcm, MALLOC_CAP_SPIRAM);
    if (buf == NULL) {
        badge_show("Out of memory", 0xFFB74D, 2000);
        return;
    }

    size_t len = 0;
    bool speech_seen = false;
    int silence_run = 0;
    int speech_ms = 0;                 /* cumulative frames VAD called speech */
    int waited_ms = 0, captured_ms = 0;

    while (captured_ms < CAPTURE_MAX_MS) {
        afe_fetch_result_t *res = s_afe->fetch(s_afe_data);
        if (res == NULL || res->ret_value == ESP_FAIL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (!speech_seen) {
            if (res->vad_state == VAD_SPEECH) {
                speech_seen = true;
                /* prepend the VAD cache so the first syllable survives */
                if (res->vad_cache_size > 0 &&
                    (size_t)res->vad_cache_size <= max_pcm) {
                    memcpy(buf + 44, res->vad_cache, res->vad_cache_size);
                    len = res->vad_cache_size;
                }
            } else {
                waited_ms += CAPTURE_FETCH_MS;
                if (waited_ms > CAPTURE_WAIT_MS) {
                    break;
                }
                continue;
            }
        }
        if (len + res->data_size > max_pcm) {
            break;
        }
        memcpy(buf + 44 + len, res->data, res->data_size);
        len += res->data_size;
        captured_ms += CAPTURE_FETCH_MS;
        if (res->vad_state == VAD_SPEECH) {
            speech_ms += CAPTURE_FETCH_MS;
            silence_run = 0;
        } else if (++silence_run >= CAPTURE_SILENCE_MS / CAPTURE_FETCH_MS) {
            /* only endpoint once we have heard a real word, not a noise
             * blip - otherwise keep waiting for the name */
            if (speech_ms >= 400) {
                break;
            }
            speech_seen = false;
            silence_run = 0;
            len = 0;
        }
    }

    if (!speech_seen || len < AUDIO_SAMPLE_RATE / 4) { /* < 0.25 s of audio */
        heap_caps_free(buf);
        ui_log("Voice: no name heard");
        badge_show("?", 0x9AA2AC, 1200);
        return;
    }
    wav_header(buf, (uint32_t)len);

    voice_query_t *q = heap_caps_malloc(sizeof(*q), MALLOC_CAP_SPIRAM);
    if (q == NULL) {
        heap_caps_free(buf);
        return;
    }
    q->wav = buf;
    q->len = 44 + len;
    q->kind = playlist ? VQ_PLAYLIST : VQ_SONG;
    if (!stt_submit(q)) {
        /* STT task busy/unavailable: without this the ~256 KB WAV would leak */
        heap_caps_free(buf);
        heap_caps_free(q);
        badge_show("Busy - try again", 0xFFB74D, 1500);
        return;
    }
    ESP_LOGI(TAG, "captured %u ms of speech for %s query",
             (unsigned)(len / 32), playlist ? "playlist" : "song");
    badge_show(playlist ? "Finding playlist..." : "Finding song...",
               0x4FC3F7, 3000);
}

/* WakeNet just fired: snapshot the pre-roll into the clip store and, when
 * configured and reachable, hand a copy to whisper. Sets s_verify_state.
 * voice_detect task only. */
static void wake_clip_begin(void)
{
    bedside_settings_t *cfg = settings_get();
    bool collect = cfg->voice_wake_collect != 0;
    bool verify = cfg->voice_wake_verify != 0 && ai_is_configured() &&
                  wifi_mgr_is_connected();
    s_verify_state = VOICE_VERDICT_NONE;
    if (!collect && !verify) {
        return;
    }
    size_t len = 0;
    uint8_t *wav = preroll_snapshot(WAKE_CLIP_MS, &len);
    if (wav == NULL) {
        return;
    }
    uint32_t id = 0;
    uint8_t *job_wav = wav;
    if (collect) {
        /* the store owns wav; the verifier works on its own copy so an
         * eviction can never pull the buffer out from under whisper */
        job_wav = NULL;
        if (verify) {
            job_wav = heap_caps_malloc(len, MALLOC_CAP_SPIRAM);
            if (job_wav != NULL) {
                memcpy(job_wav, wav, len);
            }
        }
        id = clip_store(VOICE_CLIP_WAKE, wav, len, (uint16_t)((len - 44) / 32),
                        verify ? VOICE_VERDICT_PENDING : VOICE_VERDICT_NONE);
    }
    if (!verify) {
        return;
    }
    voice_query_t *q = job_wav ? heap_caps_malloc(sizeof(*q), MALLOC_CAP_SPIRAM) : NULL;
    if (q == NULL) {
        heap_caps_free(job_wav);
        clip_set_verdict(id, VOICE_VERDICT_NONE, "(no memory)");
        return;
    }
    q->wav = job_wav;
    q->len = len;
    q->kind = VQ_VERIFY;
    q->clip_id = id;
    q->seq = ++s_verify_seq;
    if (stt_submit(q)) {
        s_verify_state = VOICE_VERDICT_PENDING;
    } else {
        heap_caps_free(job_wav);
        heap_caps_free(q);
        clip_set_verdict(id, VOICE_VERDICT_NONE, "(verifier busy)");
        ui_log("Voice: verifier busy - accepting wake unverified");
    }
}

/* A command was recognised while the verifier may still be working: keep
 * the AFE draining (its ring is small) until the verdict lands or
 * VERIFY_WAIT_MS passes. False = REJECT, drop the command. */
static bool verify_wait_ok(void)
{
    TickType_t t0 = xTaskGetTickCount();
    while (s_verify_state == VOICE_VERDICT_PENDING &&
           xTaskGetTickCount() - t0 < pdMS_TO_TICKS(VERIFY_WAIT_MS)) {
        afe_fetch_result_t *res = s_afe->fetch(s_afe_data);
        if (res == NULL || res->ret_value == ESP_FAIL) {
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (s_verify_state == VOICE_VERDICT_PENDING) {
        ui_log("Voice: verifier slow - accepting command unverified");
        return true;
    }
    return s_verify_state != VOICE_VERDICT_REJECT;
}

/* Serve a pending POST /api/v1/voice/record: copy AFE output for the
 * requested duration into a new clip and wake the waiting API task.
 * `first` is the chunk already fetched by the caller. */
static void record_request_serve(afe_fetch_result_t *first)
{
    int ms = s_rec_want_ms;
    if (ms > VOICE_REC_MAX_MS) {
        ms = VOICE_REC_MAX_MS;
    }
    size_t want = (size_t)ms * AUDIO_SAMPLE_RATE / 1000;
    uint8_t *wav = heap_caps_malloc(44 + want * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    size_t got = 0;
    if (wav != NULL) {
        int16_t *pcm = (int16_t *)(wav + 44);
        afe_fetch_result_t *res = first;
        int misses = 0;
        while (got < want && misses < 100) {
            if (res == NULL || res->ret_value == ESP_FAIL) {
                misses++;
                vTaskDelay(pdMS_TO_TICKS(20));
            } else {
                size_t n = (size_t)res->data_size / sizeof(int16_t);
                if (n > want - got) {
                    n = want - got;
                }
                memcpy(pcm + got, res->data, n * sizeof(int16_t));
                got += n;
                preroll_push((const int16_t *)res->data,
                             (size_t)res->data_size / sizeof(int16_t));
                if (got >= want) {
                    break;
                }
            }
            res = s_afe->fetch(s_afe_data);
        }
        wav_header(wav, (uint32_t)(got * sizeof(int16_t)));
    }
    s_rec_clip_id = (wav != NULL && got > 0)
                    ? clip_store(VOICE_CLIP_REC, wav, 44 + got * sizeof(int16_t),
                                 (uint16_t)(got / (AUDIO_SAMPLE_RATE / 1000)),
                                 VOICE_VERDICT_NONE)
                    : 0;
    if (s_rec_clip_id == 0) {
        heap_caps_free(wav);
    }
    s_rec_want_ms = 0;
    xSemaphoreGive(s_rec_done);
}

static void voice_detect_task(void *arg)
{
    bool listening = false;
    TickType_t listen_start = 0;
    uint32_t fetches = 0;

    while (s_running) {
        afe_fetch_result_t *res = s_afe->fetch(s_afe_data);
        if (res == NULL || res->ret_value == ESP_FAIL) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }

        /* pipeline heartbeat every ~8 s so a starved AFE is visible */
        if (++fetches % 256 == 0) {
            if (s_use_mww) {
                ESP_LOGI(TAG, "afe alive: fetches=%u vad=%d listening=%d mww peak=%.2f",
                         (unsigned)fetches, (int)res->vad_state, (int)listening,
                         wake_mww_peak() / 255.0f);
            } else {
                ESP_LOGI(TAG, "afe alive: fetches=%u vad=%d listening=%d",
                         (unsigned)fetches, (int)res->vad_state, (int)listening);
            }
        }

        if (!listening) {
            if (s_rec_want_ms > 0) {
                /* a recording session is in progress: the phrase being
                 * recorded IS the wake word, so it must not open a window */
                record_request_serve(res);
                if (s_use_mww) {
                    wake_mww_reset();
                }
                continue;
            }
            preroll_push((const int16_t *)res->data,
                         (size_t)res->data_size / sizeof(int16_t));
            bool woke;
            if (s_use_mww) {
                /* the streaming model runs on the same AFE output the
                 * verifier and MultiNet see (NS/AGC applied) */
                woke = wake_mww_feed((const int16_t *)res->data,
                                     (size_t)res->data_size / sizeof(int16_t));
            } else {
                woke = res->wakeup_state == WAKENET_DETECTED;
            }
            if (woke && s_enabled) {
                ui_log("Voice: wake word heard");
                if (!s_use_mww) {
                    s_afe->disable_wakenet(s_afe_data);
                }
                listen_frame_show(true);
                wake_blip();
                listening = true;
                listen_start = xTaskGetTickCount();
                wake_clip_begin();
            }
            continue;
        }

        if (s_verify_state == VOICE_VERDICT_REJECT) {
            /* the verifier says that was not the wake phrase: close the
             * window before a command gets through */
            ui_log("Voice: wake rejected by verifier");
            badge_show("Not for me", 0x9AA2AC, 1200);
        } else {
            esp_mn_state_t st = s_mn->detect(s_mn_data, res->data);
            if (st == ESP_MN_STATE_DETECTING) {
                /* belt-and-braces: never stay in the window past 10 s, even if
                 * MultiNet's own timeout fails to fire (e.g. silence-fed spans
                 * while the player owned the codec) */
                if (xTaskGetTickCount() - listen_start <= pdMS_TO_TICKS(10000)) {
                    continue;
                }
                ui_log("Voice: listening watchdog fired");
                st = ESP_MN_STATE_TIMEOUT;
            }
            if (st == ESP_MN_STATE_DETECTED) {
                esp_mn_results_t *r = s_mn->get_results(s_mn_data);
                if (r != NULL && r->num > 0) {
                    va_cmd_t cmd = (va_cmd_t)r->command_id[0];
                    if (!verify_wait_ok()) {
                        ui_log("Voice: \"%s\" dropped - wake rejected by verifier",
                               r->string);
                        badge_show("Not for me", 0x9AA2AC, 1200);
                    } else if (cmd == VA_PLAY_SONG || cmd == VA_PLAY_PLAYLIST) {
                        ui_log("Voice: %s -> capture window",
                               cmd == VA_PLAY_SONG ? "play song" : "playlist");
                        capture_query(cmd == VA_PLAY_PLAYLIST);
                    } else {
                        char fb[64];
                        bool ok = voice_actions_dispatch(r->command_id[0], fb,
                                                         sizeof(fb));
                        ui_log("Voice: \"%s\" (%.2f) -> %s%s", r->string,
                               r->prob[0], fb, ok ? "" : " [rejected]");
                        badge_show(fb, ok ? 0x7FD08A : 0xFFB74D, 1500);
                    }
                }
            } else { /* ESP_MN_STATE_TIMEOUT */
                ui_log("Voice: no command heard (timeout)");
                badge_show("?", 0x9AA2AC, 1200);
            }
        }
        listen_frame_show(false);
        s_mn->clean(s_mn_data);
        if (s_use_mww) {
            wake_mww_reset(); /* it was not fed during the window */
        } else {
            s_afe->enable_wakenet(s_afe_data);
        }
        listening = false;
        s_verify_state = VOICE_VERDICT_NONE;
    }
    vTaskDelete(NULL);
}

/* settings voice_wake_thr -> the active wake engine. s_afe_data must exist.
 * WakeNet: its detection threshold (0.40-0.99). microWakeWord: the cutoff on
 * the sliding-window mean probability (the stock models expect ~0.97, so
 * the same percent means "more sensitive" on this engine). */
static void apply_wake_threshold(void)
{
    int pct = settings_get()->voice_wake_thr;
    if (pct < VOICE_WAKE_THR_MIN || pct > VOICE_WAKE_THR_MAX) {
        pct = VOICE_WAKE_THR_DEFAULT;
    }
    if (s_use_mww) {
        /* microWakeWord models are meant to be decided at ~0.9-0.99 on the
         * sliding-window mean, so the 40..99 slider spans 0.88..0.99 here
         * (still: lower = more sensitive). 50 -> 0.90, 70 -> 0.94, 90 -> 0.97. */
        int cutoff = 88 + (pct - VOICE_WAKE_THR_MIN) * 11 /
                          (VOICE_WAKE_THR_MAX - VOICE_WAKE_THR_MIN);
        wake_mww_set_cutoff((uint8_t)cutoff);
        ESP_LOGI(TAG, "wake cutoff %d%% (slider %d%%, microWakeWord \"%s\")",
                 cutoff, pct, wake_mww_name());
        return;
    }
    float thr = pct / 100.0f;
    int ok = s_afe->set_wakenet_threshold(s_afe_data, 1, thr);
    ESP_LOGI(TAG, "wake threshold %.2f (%s)", thr,
             ok == 1 ? "applied" : "REJECTED");
}

void voice_apply_wake_threshold(void)
{
    if (s_running && s_afe != NULL && s_afe_data != NULL) {
        apply_wake_threshold();
    }
}

void voice_apply_wake_word(void)
{
    if (!s_running || !s_use_mww) {
        return; /* WakeNet has one fixed phrase; voice_start reads the id */
    }
    int i = wake_mww_model_index(settings_get()->voice_wake_word);
    if (i < 0) {
        i = 0;
    }
    wake_mww_select(i); /* swapped by the detect task between fetches */
    ESP_LOGI(TAG, "wake word -> \"%s\"", wake_mww_model_name(i));
}

uint32_t voice_record_clip(int ms)
{
    if (!s_running || s_capture_paused || s_rec_done == NULL || ms <= 0) {
        return 0;
    }
    if (ms > VOICE_REC_MAX_MS) {
        ms = VOICE_REC_MAX_MS;
    }
    if (s_rec_want_ms > 0) {
        return 0; /* one at a time */
    }
    xSemaphoreTake(s_rec_done, 0); /* drop a stale completion */
    s_rec_clip_id = 0;
    s_rec_want_ms = ms;
    /* the detect task starts once no command window is open (<= 10 s) */
    if (xSemaphoreTake(s_rec_done, pdMS_TO_TICKS(ms + 12000)) != pdTRUE) {
        s_rec_want_ms = 0;
        return 0;
    }
    return s_rec_clip_id;
}

/* Undo a partial voice_start(). Without this every failed bring-up leaks
 * the srmodel images, AFE and MultiNet instances (megabytes of PSRAM) -
 * and voice_set_enabled(true) retries the whole thing each time. */
static void voice_pipeline_teardown(void)
{
    if (s_mn_data != NULL) {
        s_mn->destroy(s_mn_data);
        s_mn_data = NULL;
    }
    if (s_afe_data != NULL) {
        s_afe->destroy(s_afe_data);
        s_afe_data = NULL;
    }
    if (s_feed_buf != NULL) {
        heap_caps_free(s_feed_buf);
        s_feed_buf = NULL;
    }
    if (s_preroll != NULL) {
        heap_caps_free(s_preroll);
        s_preroll = NULL;
        s_preroll_pos = 0;
        s_preroll_fill = 0;
    }
    if (s_models != NULL) {
        esp_srmodel_deinit(s_models);
        s_models = NULL;
    }
}

/* Full pipeline bring-up; runs on the worker task.  Idempotent. */
static void voice_start(void)
{
    if (s_running) {
        return;
    }
    /* Set here and cleared only on the success path at the bottom, so every
     * one of the failure returns below is recorded without touching each of
     * them. voice_service_state() needs this to tell "tried and failed" (red)
     * from "not brought up yet" (amber). */
    s_start_failed = true;
    size_t heap_before = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    if (!app_audio_ensure_init() || !voice_codec_reclaim()) {
        ui_log("voice: audio init failed");
        return;
    }

    voice_selftest();

    s_models = esp_srmodel_init("model");
    if (s_models == NULL || s_models->num <= 0) {
        ui_log("voice: no models in 'model' partition (full idf.py flash needed?)");
        voice_pipeline_teardown();
        return;
    }

    afe_config_t *cfg = afe_config_init("M", s_models, AFE_TYPE_SR,
                                        AFE_MODE_LOW_COST);
    if (cfg == NULL) {
        ui_log("voice: afe_config_init failed");
        voice_pipeline_teardown();
        return;
    }
    cfg->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    /* CONFIG_BEDSIDE_WAKE_MWW: the embedded microWakeWord model chosen in
     * settings replaces WakeNet (falls back to WakeNet if it fails to load) */
    s_use_mww = wake_mww_init(settings_get()->voice_wake_word);
    if (s_use_mww) {
        ui_log("voice: wake word engine microWakeWord \"%s\" (arena %u B)",
               wake_mww_name(), (unsigned)wake_mww_arena_bytes());
    }
#if CONFIG_BEDSIDE_WAKE_MWW
    else {
        ui_log("voice: microWakeWord model failed to load - using WakeNet");
    }
#endif
    cfg->wakenet_init = !s_use_mww;
    cfg->wakenet_mode = DET_MODE_95; /* aggressive: quiet analog mic needs it */
    cfg->aec_init = false;      /* single mic, no reference channel yet */
    cfg->vad_init = true;       /* endpointing seam for the LocalAI backend */
    cfg->agc_init = true;

    s_afe = esp_afe_handle_from_config(cfg);
    if (s_afe == NULL) {
        afe_config_free(cfg);
        ui_log("voice: no AFE for this config - model partition stale? "
               "(needs a full idf.py flash)");
        voice_pipeline_teardown();
        return;
    }
    s_afe_data = s_afe->create_from_config(cfg);
    afe_config_free(cfg);
    if (s_afe_data == NULL) {
        ui_log("voice: AFE create failed");
        voice_pipeline_teardown();
        return;
    }
    apply_wake_threshold();

    /* MultiNet7-EN + our command grammar */
    char *mn_name = esp_srmodel_filter(s_models, ESP_MN_PREFIX, ESP_MN_ENGLISH);
    if (mn_name == NULL) {
        ui_log("voice: no English MultiNet model found");
        voice_pipeline_teardown();
        return;
    }
    s_mn = esp_mn_handle_from_name(mn_name);
    if (s_mn == NULL) {
        ui_log("voice: no MultiNet handle for \"%s\" - model partition "
               "stale? (needs a full idf.py flash)", mn_name);
        voice_pipeline_teardown();
        return;
    }
    s_mn_data = s_mn->create(mn_name, VOICE_MN_TIMEOUT_MS);
    if (s_mn_data == NULL) {
        ui_log("voice: MultiNet create failed");
        voice_pipeline_teardown();
        return;
    }
    int mn_chunk = s_mn->get_samp_chunksize(s_mn_data);
    int fetch_chunk = s_afe->get_fetch_chunksize(s_afe_data);
    if (mn_chunk != fetch_chunk) {
        ESP_LOGW(TAG, "chunk mismatch: mn=%d afe=%d", mn_chunk, fetch_chunk);
    }

    esp_mn_commands_alloc((esp_mn_iface_t *)s_mn, s_mn_data);
    esp_mn_commands_clear();
    int n_phrases = 0;
    int bad_adds = 0;
    const va_phrase_t *ph = voice_actions_phrases(&n_phrases);
    for (int i = 0; i < n_phrases; i++) {
        if (esp_mn_commands_add((int)ph[i].cmd, ph[i].phrase) != ESP_OK) {
            bad_adds++;
        }
    }
    esp_mn_error_t *mn_err = esp_mn_commands_update();
    int bad_g2p = (mn_err != NULL) ? mn_err->num : 0;
    if (bad_g2p > 0) {
        ESP_LOGW(TAG, "%d phrases failed g2p", bad_g2p);
        for (int i = 0; i < bad_g2p; i++) {
            ESP_LOGW(TAG, "  bad phrase: %s", mn_err->phrases[i]->string);
        }
    }
    if (bad_adds > 0) {
        ESP_LOGW(TAG, "%d phrases rejected by esp_mn_commands_add", bad_adds);
    }
    if (n_phrases == 0 || bad_adds + bad_g2p >= n_phrases) {
        /* wake word would work but nothing could ever dispatch */
        ui_log("voice: no usable command phrases (%d rejected, %d failed g2p)",
               bad_adds, bad_g2p);
        esp_mn_commands_free();
        voice_pipeline_teardown();
        return;
    }

    s_feed_samples = s_afe->get_feed_chunksize(s_afe_data) *
                     s_afe->get_feed_channel_num(s_afe_data);
    s_feed_buf = heap_caps_malloc((size_t)s_feed_samples * sizeof(int16_t),
                                  MALLOC_CAP_INTERNAL);
    if (s_feed_buf == NULL) {
        ui_log("voice: no internal RAM for feed buffer");
        esp_mn_commands_free(); /* alloc'd above; mn destroy doesn't free it */
        voice_pipeline_teardown();
        return;
    }
    /* wake-clip pre-roll (PSRAM, 80 KB); optional - without it wakes are
     * simply not snapshotted or verified */
    clips_init();
    s_preroll_pos = 0;
    s_preroll_fill = 0;
    s_preroll = heap_caps_calloc(WAKE_PREROLL_SAMPLES, sizeof(int16_t),
                                 MALLOC_CAP_SPIRAM);
    if (s_preroll == NULL) {
        ESP_LOGW(TAG, "no PSRAM for the wake pre-roll: clips/verifier off");
    }

    s_capture_paused = false; /* fresh pipeline: not paused until the feed task says so */
    s_running = true;
    s_enabled = true; /* before the tasks look at it; toggle-off comes later */
    BaseType_t feed_ok = xTaskCreatePinnedToCore(voice_feed_task, "voice_feed",
                                                 VOICE_FEED_STACK, NULL, 6, NULL,
                                                 VOICE_CORE);
    BaseType_t detect_ok = pdFAIL;
    if (feed_ok == pdPASS) {
        detect_ok = xTaskCreatePinnedToCoreWithCaps(voice_detect_task,
                                                    "voice_detect",
                                                    VOICE_DETECT_STACK, NULL, 5,
                                                    NULL, VOICE_CORE,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (feed_ok != pdPASS || detect_ok != pdPASS) {
        /* Before: s_running stayed true with no tasks, so voice was dead for
         * the rest of the uptime and every later voice_start() was a no-op.
         * Undo everything; voice_set_enabled(true) can retry the bring-up. */
        ui_log("voice: %s task create failed (%s) - voice disabled",
               feed_ok != pdPASS ? "feed" : "detect",
               feed_ok != pdPASS ? "internal RAM?" : "PSRAM?");
        s_running = false;
        s_enabled = false;
        if (feed_ok == pdPASS) {
            /* let voice_feed see s_running==false and leave its 1 s read
             * before the AFE it feeds is destroyed */
            vTaskDelay(pdMS_TO_TICKS(1500));
        }
        esp_mn_commands_free();
        voice_pipeline_teardown();
        return;
    }

    s_start_failed = false;
    size_t heap_after = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    ESP_LOGI(TAG, "voice up: %d phrases, feed=%d, internal heap %u -> %u (delta %d)",
             n_phrases, s_feed_samples, (unsigned)heap_before,
             (unsigned)heap_after, (int)(heap_before - heap_after));
    ui_log("Voice: say \"%s\", then a command", s_use_mww ? wake_mww_name() : "Hi ESP");
}

svc_state_t voice_service_state(void)
{
    if (!settings_get()->voice_enabled) {
        return SVC_OFF;
    }
    if (s_running && s_enabled) {
        /* the feed task stops reading the mic for the whole music session
         * (player owns the codec at 44.1 kHz), so "up" would lie: grey it */
        return s_capture_paused ? SVC_PAUSED : SVC_UP;
    }
    if (s_start_failed) {
        return SVC_FAIL;
    }
    /* enabled in settings but not up yet: boot_seq has not reached voice_init
     * (or safe mode skipped it), or the user suspended listening */
    return SVC_WAIT;
}

void voice_init(void)
{
    if (!settings_get()->voice_enabled) {
        ESP_LOGI(TAG, "voice disabled in settings");
        return;
    }
    voice_start();
    s_enabled = s_running;
}

static void voice_toggle_job(void *arg)
{
    bool en = (bool)(uintptr_t)arg;
    if (en) {
        voice_start();       /* no-op if already up */
        s_enabled = s_running;
    } else {
        s_enabled = false;
        listen_frame_show(false);
    }
}

void voice_set_enabled(bool en)
{
    /* callable from LVGL callbacks: heavy work goes to the worker */
    if (!app_run_async(voice_toggle_job, (void *)(uintptr_t)en)) {
        ui_log("Voice: worker busy - toggle not applied, try again");
    }
}

void voice_apply_mic_gain(void)
{
    if (s_running) {
        bsp_extra_codec_in_gain_set(settings_get()->voice_mic_gain);
    }
}

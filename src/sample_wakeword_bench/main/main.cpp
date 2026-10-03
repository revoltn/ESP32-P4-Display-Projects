/*
 * sample_wakeword_bench - step 0 de-risk for running a microWakeWord streaming model on
 * the ESP32-P4 rev v1.3 (JC4880P443C) with esp-tflite-micro + ESP-NN.
 *
 * Feeds embedded 16 kHz mono WAV clips (Windows TTS "Okay Nabu" and a
 * negative sentence) through the tflite-micro microfrontend (40 log-mel
 * features / 10 ms) and the stock okay_nabu.tflite streaming model, exactly
 * the way ESPHome's micro_wake_word component does, and reports:
 *   - model load / arena size actually needed
 *   - per-invoke time (min/avg/max) and real-time factor
 *   - detections (sliding-window mean > cutoff) per clip
 *   - heap before/after, task stack high-water mark
 * Runs the whole thing twice: tensor arena in internal RAM, then in PSRAM.
 * Also soaks 60 s of synthetic noise to catch PIE-kernel faults.
 *
 * No display, no codec, no network: app-flash only.
 */
#include <cstdint>
#include <cstring>
#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/idf_additions.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#include "esp_chip_info.h"
#include "esp_system.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
/* esphome/esp-micro-speech-features: TF microfrontend fork (Apache-2.0) */
#include "frontend.h"
#include "frontend_util.h"

static const char *TAG = "wakeword_bench";

/* ---- embedded assets ---------------------------------------------------- */
extern const uint8_t okay_nabu_tflite_start[] asm("_binary_okay_nabu_tflite_start");
extern const uint8_t okay_nabu_tflite_end[]   asm("_binary_okay_nabu_tflite_end");
extern const uint8_t pos1_wav_start[] asm("_binary_okay_nabu_tts_wav_start");
extern const uint8_t pos1_wav_end[]   asm("_binary_okay_nabu_tts_wav_end");
extern const uint8_t pos2_wav_start[] asm("_binary_okay_nabu_tts2_wav_start");
extern const uint8_t pos2_wav_end[]   asm("_binary_okay_nabu_tts2_wav_end");
extern const uint8_t neg_wav_start[]  asm("_binary_negative_tts_wav_start");
extern const uint8_t neg_wav_end[]    asm("_binary_negative_tts_wav_end");

/* ---- microWakeWord preprocessor settings (must match training) --------- */
static constexpr int      SAMPLE_RATE            = 16000;
static constexpr uint8_t  FEATURE_SIZE           = 40;
static constexpr uint8_t  FEATURE_DURATION_MS    = 30;
static constexpr uint8_t  FEATURE_STEP_MS        = 10;   /* okay_nabu.json feature_step_size */
static constexpr float    FB_LOWER_HZ            = 125.0f;
static constexpr float    FB_UPPER_HZ            = 7500.0f;
static constexpr uint8_t  NR_SMOOTHING_BITS      = 10;
static constexpr float    NR_EVEN_SMOOTHING      = 0.025f;
static constexpr float    NR_ODD_SMOOTHING       = 0.06f;
static constexpr float    NR_MIN_SIGNAL_REMAINING= 0.05f;
static constexpr bool     PCAN_ENABLE            = true;
static constexpr float    PCAN_STRENGTH          = 0.95f;
static constexpr float    PCAN_OFFSET            = 80.0f;
static constexpr uint8_t  PCAN_GAIN_BITS         = 21;
static constexpr bool     LOG_ENABLE             = true;
static constexpr uint8_t  LOG_SCALE_SHIFT        = 6;

/* okay_nabu.json "micro" block */
static constexpr float    PROBABILITY_CUTOFF     = 0.97f;
static constexpr size_t   SLIDING_WINDOW         = 5;
static constexpr size_t   MANIFEST_ARENA         = 26080;
static constexpr int16_t  MIN_SLICES_BEFORE_DET  = 100;  /* ESPHome cool-off */
static constexpr size_t   VAR_ARENA_SIZE         = 1024;

/* The bedside_clock AFE hands out 512 samples (32 ms) per fetch */
static constexpr size_t   CHUNK_SAMPLES          = 512;

/* ---- WAV chunk walker --------------------------------------------------- */
static bool wav_pcm(const uint8_t *start, const uint8_t *end, const int16_t **pcm, size_t *samples)
{
    size_t len = end - start;
    if (len < 12 || memcmp(start, "RIFF", 4) || memcmp(start + 8, "WAVE", 4)) return false;
    size_t off = 12;
    int channels = 0, rate = 0, bits = 0;
    while (off + 8 <= len) {
        uint32_t csz;
        memcpy(&csz, start + off + 4, 4);
        if (!memcmp(start + off, "fmt ", 4)) {
            uint16_t ch, bps; uint32_t sr;
            memcpy(&ch, start + off + 10, 2);
            memcpy(&sr, start + off + 12, 4);
            memcpy(&bps, start + off + 22, 2);
            channels = ch; rate = sr; bits = bps;
        } else if (!memcmp(start + off, "data", 4)) {
            if (channels != 1 || rate != SAMPLE_RATE || bits != 16) {
                ESP_LOGE(TAG, "WAV not 16k/mono/16-bit (ch=%d sr=%d bits=%d)", channels, rate, bits);
                return false;
            }
            size_t avail = len - (off + 8);
            *pcm = reinterpret_cast<const int16_t *>(start + off + 8);
            *samples = std::min<size_t>(csz, avail) / 2;
            return true;
        }
        off += 8 + csz + (csz & 1);
    }
    return false;
}

/* ---- streaming model wrapper (mirrors ESPHome StreamingModel) ----------- */
struct Mww {
    tflite::MicroMutableOpResolver<20> resolver;
    tflite::MicroInterpreter *interp = nullptr;
    tflite::MicroAllocator *ma = nullptr;
    tflite::MicroResourceVariables *mrv = nullptr;
    uint8_t *arena = nullptr;
    size_t arena_size = 0;
    uint8_t *var_arena = nullptr;
    uint32_t caps = 0;

    uint8_t stride = 0;
    uint8_t stride_step = 0;
    uint8_t probs[SLIDING_WINDOW] = {0};
    size_t last_n = 0;
    int16_t ignore_windows = -MIN_SLICES_BEFORE_DET;
    uint8_t cutoff = (uint8_t)(PROBABILITY_CUTOFF * 255.0f);

    /* stats */
    int64_t inv_min = INT64_MAX, inv_max = 0, inv_sum = 0;
    uint32_t inv_count = 0;
    uint8_t max_prob = 0;
    uint32_t detections = 0;

    bool register_ops()
    {
        return resolver.AddCallOnce() == kTfLiteOk && resolver.AddVarHandle() == kTfLiteOk &&
               resolver.AddReshape() == kTfLiteOk && resolver.AddReadVariable() == kTfLiteOk &&
               resolver.AddStridedSlice() == kTfLiteOk && resolver.AddConcatenation() == kTfLiteOk &&
               resolver.AddAssignVariable() == kTfLiteOk && resolver.AddConv2D() == kTfLiteOk &&
               resolver.AddMul() == kTfLiteOk && resolver.AddAdd() == kTfLiteOk &&
               resolver.AddMean() == kTfLiteOk && resolver.AddFullyConnected() == kTfLiteOk &&
               resolver.AddLogistic() == kTfLiteOk && resolver.AddQuantize() == kTfLiteOk &&
               resolver.AddDepthwiseConv2D() == kTfLiteOk && resolver.AddAveragePool2D() == kTfLiteOk &&
               resolver.AddMaxPool2D() == kTfLiteOk && resolver.AddPad() == kTfLiteOk &&
               resolver.AddPack() == kTfLiteOk && resolver.AddSplitV() == kTfLiteOk;
    }

    void fresh_mrv()
    {
        ma = tflite::MicroAllocator::Create(var_arena, VAR_ARENA_SIZE);
        mrv = tflite::MicroResourceVariables::Create(ma, 20);
    }

    bool load(const uint8_t *model_data, uint32_t heap_caps)
    {
        caps = heap_caps;
        const tflite::Model *model = tflite::GetModel(model_data);
        if (model->version() != TFLITE_SCHEMA_VERSION) {
            ESP_LOGE(TAG, "schema %lu != %d", (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
            return false;
        }
        if (!register_ops()) { ESP_LOGE(TAG, "op registration failed"); return false; }

        var_arena = (uint8_t *)heap_caps_malloc(VAR_ARENA_SIZE, caps);
        if (!var_arena) return false;
        fresh_mrv();

        /* probe like ESPHome: manifest, 1.5x, 2x */
        size_t attempts[] = {(MANIFEST_ARENA + 15) & ~15u, (MANIFEST_ARENA * 3 / 2 + 15) & ~15u,
                             (MANIFEST_ARENA * 2 + 15) & ~15u};
        for (size_t sz : attempts) {
            uint8_t *a = (uint8_t *)heap_caps_aligned_alloc(16, sz, caps);
            if (!a) continue;
            auto *ip = new tflite::MicroInterpreter(model, resolver, a, sz, mrv);
            if (ip->AllocateTensors() == kTfLiteOk) {
                ESP_LOGI(TAG, "arena: manifest %u B, attempt %u B OK, used %u B", (unsigned)MANIFEST_ARENA,
                         (unsigned)sz, (unsigned)ip->arena_used_bytes());
                interp = ip; arena = a; arena_size = sz;
                break;
            }
            ESP_LOGW(TAG, "arena %u B too small", (unsigned)sz);
            delete ip;
            heap_caps_free(a);
            fresh_mrv();
        }
        if (!interp) { ESP_LOGE(TAG, "could not allocate tensors"); return false; }

        TfLiteTensor *in = interp->input(0);
        TfLiteTensor *out = interp->output(0);
        ESP_LOGI(TAG, "input dims=%d [%d,%d,%d] type=%d | output dims=%d [%d,%d] type=%d",
                 in->dims->size, in->dims->data[0], in->dims->data[1], in->dims->data[2], in->type,
                 out->dims->size, out->dims->data[0], out->dims->data[1], out->type);
        if (in->dims->size != 3 || in->dims->data[0] != 1 || in->dims->data[2] != FEATURE_SIZE ||
            in->type != kTfLiteInt8 || out->type != kTfLiteUInt8) {
            ESP_LOGE(TAG, "unexpected tensor layout");
            return false;
        }
        stride = in->dims->data[1];
        reset();
        return true;
    }

    void reset()
    {
        memset(probs, 0, sizeof(probs));
        ignore_windows = -MIN_SLICES_BEFORE_DET;
        stride_step = 0;
        last_n = 0;
    }

    void reset_stats()
    {
        inv_min = INT64_MAX; inv_max = 0; inv_sum = 0; inv_count = 0; max_prob = 0; detections = 0;
    }

    /* one 40-feature slice in; returns true if a detection fired */
    bool push(const int8_t *features, bool *invoked)
    {
        *invoked = false;
        TfLiteTensor *in = interp->input(0);
        stride_step %= stride;
        memcpy(tflite::GetTensorData<int8_t>(in) + FEATURE_SIZE * stride_step, features, FEATURE_SIZE);
        ++stride_step;
        bool new_prob = false;
        if (stride_step >= stride) {
            int64_t t0 = esp_timer_get_time();
            TfLiteStatus st = interp->Invoke();
            int64_t dt = esp_timer_get_time() - t0;
            if (st != kTfLiteOk) { ESP_LOGE(TAG, "Invoke failed"); return false; }
            *invoked = true;
            inv_min = std::min(inv_min, dt); inv_max = std::max(inv_max, dt); inv_sum += dt; ++inv_count;
            uint8_t p = interp->output(0)->data.uint8[0];
            max_prob = std::max(max_prob, p);
            ++last_n; if (last_n == SLIDING_WINDOW) last_n = 0;
            probs[last_n] = p;
            new_prob = true;
        }
        if (probs[last_n] < cutoff) ignore_windows = std::min<int16_t>(ignore_windows + 1, 0);

        if (!new_prob || ignore_windows < 0) return false;
        uint32_t sum = 0;
        for (uint8_t v : probs) sum += v;
        if (sum > (uint32_t)cutoff * SLIDING_WINDOW) {
            ++detections;
            ESP_LOGI(TAG, "  DETECTED  avg=%.2f max=%.2f", (sum / (float)SLIDING_WINDOW) / 255.0f, max_prob / 255.0f);
            reset();
            return true;
        }
        return false;
    }

    void unload()
    {
        delete interp; interp = nullptr;
        heap_caps_free(arena); arena = nullptr;
        heap_caps_free(var_arena); var_arena = nullptr;
    }
};

/* ---- frontend ----------------------------------------------------------- */
static FrontendConfig s_fcfg;
static FrontendState s_fstate;

static bool frontend_init()
{
    FrontendFillConfigWithDefaults(&s_fcfg);
    s_fcfg.window.size_ms = FEATURE_DURATION_MS;
    s_fcfg.window.step_size_ms = FEATURE_STEP_MS;
    s_fcfg.filterbank.num_channels = FEATURE_SIZE;
    s_fcfg.filterbank.lower_band_limit = FB_LOWER_HZ;
    s_fcfg.filterbank.upper_band_limit = FB_UPPER_HZ;
    s_fcfg.noise_reduction.smoothing_bits = NR_SMOOTHING_BITS;
    s_fcfg.noise_reduction.even_smoothing = NR_EVEN_SMOOTHING;
    s_fcfg.noise_reduction.odd_smoothing = NR_ODD_SMOOTHING;
    s_fcfg.noise_reduction.min_signal_remaining = NR_MIN_SIGNAL_REMAINING;
    s_fcfg.pcan_gain_control.enable_pcan = PCAN_ENABLE;
    s_fcfg.pcan_gain_control.strength = PCAN_STRENGTH;
    s_fcfg.pcan_gain_control.offset = PCAN_OFFSET;
    s_fcfg.pcan_gain_control.gain_bits = PCAN_GAIN_BITS;
    s_fcfg.log_scale.enable_log = LOG_ENABLE;
    s_fcfg.log_scale.scale_shift = LOG_SCALE_SHIFT;
    return FrontendPopulateState(&s_fcfg, &s_fstate, SAMPLE_RATE) != 0;
}

/* Feed PCM in AFE-sized chunks; every emitted slice goes to the model.
 * trace=true prints every invoke probability >= 0.10 as "t_ms:p" so the
 * sequence can be diffed against a PC run of the same model + frontend. */
static void run_clip(Mww &m, const char *name, const int16_t *pcm, size_t samples, bool expect, bool trace = false)
{
    FrontendReset(&s_fstate);
    m.reset();
    m.reset_stats();
    int64_t fe_us = 0, work_us = 0;
    size_t slices = 0, chunks = 0;
    size_t pos = 0;
    char line[200]; size_t ll = 0;
    if (trace) ESP_LOGI(TAG, "  trace (ms:prob) for %s:", name);
    while (pos < samples) {
        size_t n = std::min(CHUNK_SAMPLES, samples - pos);
        const int16_t *p = pcm + pos;
        size_t left = n;
        int64_t c0 = esp_timer_get_time();
        while (left > 0) {
            size_t consumed = 0;
            int64_t f0 = esp_timer_get_time();
            FrontendOutput fo = FrontendProcessSamples(&s_fstate, p, left, &consumed);
            fe_us += esp_timer_get_time() - f0;
            p += consumed; left -= consumed;
            if (fo.size == 0) continue;
            int8_t feat[FEATURE_SIZE];
            for (size_t i = 0; i < fo.size && i < FEATURE_SIZE; ++i) {
                int32_t v = ((int32_t)fo.values[i] * 256 + 333) / 666 + INT8_MIN;
                feat[i] = (int8_t)std::clamp<int32_t>(v, INT8_MIN, INT8_MAX);
            }
            ++slices;
            bool invoked;
            uint8_t before = m.probs[m.last_n];
            m.push(feat, &invoked);
            (void)before;
            if (trace && invoked) {
                uint8_t pr = m.interp->output(0)->data.uint8[0];
                if (pr >= 26) {
                    int w = snprintf(line + ll, sizeof(line) - ll, " %u:%.2f", (unsigned)(slices * FEATURE_STEP_MS), pr / 255.0f);
                    if (w > 0) ll += w;
                    if (ll > 150) { ESP_LOGI(TAG, "   %s", line); ll = 0; line[0] = 0; }
                }
            }
        }
        work_us += esp_timer_get_time() - c0;
        pos += n;
        /* yield now and then so the idle task feeds the task WDT (the real
         * pipeline is paced by the mic at 32 ms per chunk anyway) */
        if ((++chunks & 31) == 0) vTaskDelay(1);
    }
    if (trace && ll) ESP_LOGI(TAG, "   %s", line);
    int64_t total_us = work_us;
    float clip_ms = samples * 1000.0f / SAMPLE_RATE;
    ESP_LOGI(TAG, "[%s] %.0f ms audio, %u slices, %lu invokes | invoke min/avg/max = %lld/%lld/%lld us | "
                  "frontend %lld us total | processing %.1f ms = %.1f%% of real time | max prob %.2f | "
                  "detections %lu (%s)",
             name, clip_ms, (unsigned)slices, (unsigned long)m.inv_count,
             (long long)(m.inv_count ? m.inv_min : 0), (long long)(m.inv_count ? m.inv_sum / m.inv_count : 0),
             (long long)m.inv_max, (long long)fe_us, total_us / 1000.0f, 100.0f * total_us / 1000.0f / clip_ms,
             m.max_prob / 255.0f, (unsigned long)m.detections,
             expect ? (m.detections ? "PASS: expected wake" : "FAIL: expected wake")
                    : (m.detections ? "FAIL: false accept" : "PASS: no false accept"));
}

static void run_suite(const char *label, uint32_t caps)
{
    ESP_LOGI(TAG, "================ %s ================", label);
    size_t heap_int0 = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t heap_psr0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);

    Mww *m = new Mww();
    if (!m->load(okay_nabu_tflite_start, caps)) { ESP_LOGE(TAG, "load failed"); delete m; return; }
    ESP_LOGI(TAG, "model %u B in flash, arena %u B, stride %u slices/invoke, cutoff %u/255, window %u",
             (unsigned)(okay_nabu_tflite_end - okay_nabu_tflite_start), (unsigned)m->arena_size, m->stride,
             m->cutoff, (unsigned)SLIDING_WINDOW);
    ESP_LOGI(TAG, "heap after load: internal %u->%u (-%d), psram %u->%u (-%d)",
             (unsigned)heap_int0, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (int)(heap_int0 - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             (unsigned)heap_psr0, (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (int)(heap_psr0 - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));

    const int16_t *pcm; size_t n;
    /* ~1.2 s of silence first so the cool-off (100 slices) expires before speech */
    static int16_t *silence = nullptr;
    const size_t sil_n = SAMPLE_RATE * 6 / 5;
    if (!silence) { silence = (int16_t *)heap_caps_calloc(sil_n, 2, MALLOC_CAP_SPIRAM); }

    struct { const char *name; const uint8_t *s, *e; bool expect; } clips[] = {
        {"pos1 'Okay Nabu.'",  pos1_wav_start, pos1_wav_end, true},
        {"pos2 'Okay, Nabu!'", pos2_wav_start, pos2_wav_end, true},
        {"neg sentence",       neg_wav_start,  neg_wav_end,  false},
    };
    for (auto &c : clips) {
        if (!wav_pcm(c.s, c.e, &pcm, &n)) { ESP_LOGE(TAG, "bad wav %s", c.name); continue; }
        /* prepend silence: build a temp buffer in PSRAM */
        int16_t *buf = (int16_t *)heap_caps_malloc((sil_n + n) * 2, MALLOC_CAP_SPIRAM);
        memcpy(buf, silence, sil_n * 2);
        memcpy(buf + sil_n, pcm, n * 2);
        run_clip(*m, c.name, buf, sil_n + n, c.expect, /*trace=*/true);
        heap_caps_free(buf);
    }

    /* 60 s soak of low-level white noise: stability + sustained timing */
    {
        const size_t noise_n = SAMPLE_RATE * 60;
        int16_t *noise = (int16_t *)heap_caps_malloc(noise_n * 2, MALLOC_CAP_SPIRAM);
        for (size_t i = 0; i < noise_n; ++i) noise[i] = (int16_t)((int32_t)(esp_random() & 0x3FF) - 512);
        run_clip(*m, "60 s noise soak", noise, noise_n, false);
        heap_caps_free(noise);
    }

    ESP_LOGI(TAG, "task stack high-water mark: %u B free of allocation",
             (unsigned)uxTaskGetStackHighWaterMark(nullptr));
    m->unload();
    delete m;
    ESP_LOGI(TAG, "heap after unload: internal %u (leak %d), psram %u (leak %d)",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (int)(heap_int0 - heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             (int)(heap_psr0 - heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}

static void test_task(void *)
{
    /* give the serial capture on the PC time to open the port after reset */
    vTaskDelay(pdMS_TO_TICKS(4000));
    esp_chip_info_t ci; esp_chip_info(&ci);
    ESP_LOGI(TAG, "chip rev v%d.%d, %d cores, IDF %s", ci.revision / 100, ci.revision % 100, ci.cores, esp_get_idf_version());
#ifdef ESP_NN
    ESP_LOGI(TAG, "ESP_NN defined (optimized kernels compiled in)");
#else
    ESP_LOGI(TAG, "ESP_NN NOT defined (reference kernels)");
#endif
    if (!frontend_init()) { ESP_LOGE(TAG, "frontend init failed"); vTaskDelete(nullptr); }

    run_suite("arena in INTERNAL RAM", MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    run_suite("arena in PSRAM", MALLOC_CAP_SPIRAM);

    FrontendFreeStateContents(&s_fstate);
    ESP_LOGI(TAG, "======== sample_wakeword_bench DONE ========");
    vTaskDelete(nullptr);
}

extern "C" void app_main(void)
{
    /* Mirror bedside_clock's voice_detect task: core 1, 16 KB stack in PSRAM
     * (ESPHome needs >= 8 KB on the P4 for the PIE kernels). */
    TaskHandle_t h = nullptr;
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(test_task, "wakeword_bench", 16384, nullptr, 5, &h, 1,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS) ESP_LOGE(TAG, "task create failed");
}

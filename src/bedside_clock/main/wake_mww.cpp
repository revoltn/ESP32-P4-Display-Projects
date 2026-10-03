/* wake_mww.cpp - microWakeWord streaming engine on tflite-micro.
 *
 * Mirrors ESPHome's micro_wake_word component (which is GPL-3; this is an
 * independent re-implementation of the same public recipe, validated
 * against it clip-for-clip in src/sample_wakeword_bench):
 *   16 kHz PCM -> TF microfrontend (30 ms window, 10 ms step, 40 log-mel
 *   channels, PCAN + noise reduction, esphome/esp-micro-speech-features)
 *   -> int8 features ((v*256+333)/666 - 128) -> [1, stride, 40] streaming
 *   model with resource variables -> uint8 probability per invoke ->
 *   mean of the last `sliding_window_size` > cutoff => detection, then a
 *   100-slice cool-off before the next one.
 * Measured on the P4 rev v1.3: 0.55 ms per invoke (3 slices), arena 22 KB,
 * identical speed with the arena in PSRAM (sample_wakeword_bench/README.md).
 *
 * Models: every BEDSIDE_WAKE_MODELS entry in main/CMakeLists.txt is embedded
 * (models/<id>.tflite + models/<id>.json) and enumerated here through the
 * generated WAKE_MODEL_LIST X-macro. The active one is chosen at init from
 * the settings id and can be swapped live with wake_mww_select(): the swap
 * runs inside wake_mww_feed(), i.e. on the detect task between AFE fetches,
 * and the new model is fully built before the old one is freed.
 */
#include "sdkconfig.h"
#include "wake_mww.h"

#include <cstring>

#if CONFIG_BEDSIDE_WAKE_MWW

#include <algorithm>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "wake_models_gen.h"

#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/micro/micro_allocator.h"
#include "tensorflow/lite/micro/micro_resource_variable.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "frontend.h"
#include "frontend_util.h"

static const char *TAG = "wake_mww";

/* EMBED_FILES models/<id>.tflite -> _binary_<id>_tflite_start/_end etc. */
#define X(id) \
    extern const uint8_t id##_tflite_start[] asm("_binary_" #id "_tflite_start"); \
    extern const uint8_t id##_tflite_end[]   asm("_binary_" #id "_tflite_end");   \
    extern const uint8_t id##_json_start[]   asm("_binary_" #id "_json_start");   \
    extern const uint8_t id##_json_end[]     asm("_binary_" #id "_json_end");
WAKE_MODEL_LIST(X)
#undef X

/* preprocessor settings every microWakeWord model is trained with */
static constexpr int      SAMPLE_RATE   = 16000;
static constexpr uint8_t  FEATURE_SIZE  = 40;
static constexpr uint8_t  FEATURE_MS    = 30;
static constexpr int16_t  COOL_OFF_SLICES = 100;
static constexpr size_t   VAR_ARENA     = 1024;
static constexpr size_t   MAX_WINDOW    = 16;

namespace {

struct WakeModel {
    const char *id;                     /* CMake list entry = settings/API id */
    const uint8_t *tf, *tf_end;         /* .tflite */
    const uint8_t *js, *js_end;         /* .json manifest */
    char name[32];                      /* manifest "wake_word", filled at init */
};

#define X(id) { #id, id##_tflite_start, id##_tflite_end, id##_json_start, id##_json_end, "" },
WakeModel s_models[] = { WAKE_MODEL_LIST(X) };
#undef X
constexpr int MODEL_COUNT = (int)(sizeof(s_models) / sizeof(s_models[0]));
static_assert(MODEL_COUNT > 0, "BEDSIDE_WAKE_MODELS (main/CMakeLists.txt) is empty");

struct Manifest {
    char name[32] = "";
    int step_ms = 10;
    int window = 5;
    float cutoff = 0.97f;
    size_t arena = 26080;
};

/* Everything one loaded model owns. A switch builds a complete new Loaded
 * before the old one is released, so a model that fails to load leaves the
 * previous one running. */
struct Loaded {
    Manifest mf;
    tflite::MicroInterpreter *interp = nullptr;
    tflite::MicroResourceVariables *mrv = nullptr;
    uint8_t *arena = nullptr;
    size_t arena_size = 0;
    uint8_t *var_arena = nullptr;
    uint8_t stride = 3;
};

Loaded s_cur;
int s_current = -1;             /* index into s_models; -1 = nothing loaded */
volatile int s_pending = -1;    /* wake_mww_select() target, applied in feed */
bool s_ready = false;

tflite::MicroMutableOpResolver<20> *s_resolver;  /* shared by every model */

FrontendConfig s_fcfg;
FrontendState s_fstate;
bool s_frontend_up = false;

uint8_t s_stride_step;
uint8_t s_probs[MAX_WINDOW];
size_t  s_last_n;
int16_t s_ignore = -COOL_OFF_SLICES;
uint8_t s_cutoff = 247;
uint8_t s_peak;

const char *model_name(int i)
{
    if (i < 0 || i >= MODEL_COUNT) {
        return "";
    }
    return s_models[i].name[0] != '\0' ? s_models[i].name : s_models[i].id;
}

void parse_manifest(const WakeModel &m, Manifest &mf)
{
    mf = Manifest();
    strlcpy(mf.name, m.id, sizeof(mf.name));
    cJSON *root = cJSON_ParseWithLength(reinterpret_cast<const char *>(m.js),
                                        m.js_end - m.js);
    if (root == nullptr) {
        ESP_LOGW(TAG, "%s: manifest JSON unreadable, using defaults", m.id);
        return;
    }
    cJSON *ww = cJSON_GetObjectItemCaseSensitive(root, "wake_word");
    if (cJSON_IsString(ww) && ww->valuestring[0] != '\0') {
        strlcpy(mf.name, ww->valuestring, sizeof(mf.name));
    }
    cJSON *mi = cJSON_GetObjectItemCaseSensitive(root, "micro");
    if (cJSON_IsObject(mi)) {
        cJSON *v;
        if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(mi, "feature_step_size"))) mf.step_ms = (int)v->valuedouble;
        if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(mi, "sliding_window_size"))) mf.window = (int)v->valuedouble;
        if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(mi, "probability_cutoff"))) mf.cutoff = (float)v->valuedouble;
        if (cJSON_IsNumber(v = cJSON_GetObjectItemCaseSensitive(mi, "tensor_arena_size"))) mf.arena = (size_t)v->valuedouble;
    }
    cJSON_Delete(root);
    mf.window = std::clamp<int>(mf.window, 1, (int)MAX_WINDOW);
    mf.step_ms = std::clamp<int>(mf.step_ms, 5, 30);
}

bool register_ops(tflite::MicroMutableOpResolver<20> &r)
{
    return r.AddCallOnce() == kTfLiteOk && r.AddVarHandle() == kTfLiteOk &&
           r.AddReshape() == kTfLiteOk && r.AddReadVariable() == kTfLiteOk &&
           r.AddStridedSlice() == kTfLiteOk && r.AddConcatenation() == kTfLiteOk &&
           r.AddAssignVariable() == kTfLiteOk && r.AddConv2D() == kTfLiteOk &&
           r.AddMul() == kTfLiteOk && r.AddAdd() == kTfLiteOk &&
           r.AddMean() == kTfLiteOk && r.AddFullyConnected() == kTfLiteOk &&
           r.AddLogistic() == kTfLiteOk && r.AddQuantize() == kTfLiteOk &&
           r.AddDepthwiseConv2D() == kTfLiteOk && r.AddAveragePool2D() == kTfLiteOk &&
           r.AddMaxPool2D() == kTfLiteOk && r.AddPad() == kTfLiteOk &&
           r.AddPack() == kTfLiteOk && r.AddSplitV() == kTfLiteOk;
}

/* the resource-variable allocator lives inside l.var_arena */
bool fresh_mrv(Loaded &l)
{
    tflite::MicroAllocator *ma = tflite::MicroAllocator::Create(l.var_arena, VAR_ARENA);
    l.mrv = (ma != nullptr) ? tflite::MicroResourceVariables::Create(ma, 20) : nullptr;
    return l.mrv != nullptr;
}

bool frontend_init(int step_ms)
{
    if (s_frontend_up) {
        FrontendFreeStateContents(&s_fstate);
        s_frontend_up = false;
    }
    FrontendFillConfigWithDefaults(&s_fcfg);
    s_fcfg.window.size_ms = FEATURE_MS;
    s_fcfg.window.step_size_ms = step_ms;
    s_fcfg.filterbank.num_channels = FEATURE_SIZE;
    s_fcfg.filterbank.lower_band_limit = 125.0f;
    s_fcfg.filterbank.upper_band_limit = 7500.0f;
    s_fcfg.noise_reduction.smoothing_bits = 10;
    s_fcfg.noise_reduction.even_smoothing = 0.025f;
    s_fcfg.noise_reduction.odd_smoothing = 0.06f;
    s_fcfg.noise_reduction.min_signal_remaining = 0.05f;
    s_fcfg.pcan_gain_control.enable_pcan = 1;
    s_fcfg.pcan_gain_control.strength = 0.95f;
    s_fcfg.pcan_gain_control.offset = 80.0f;
    s_fcfg.pcan_gain_control.gain_bits = 21;
    s_fcfg.log_scale.enable_log = 1;
    s_fcfg.log_scale.scale_shift = 6;
    s_frontend_up = FrontendPopulateState(&s_fcfg, &s_fstate, SAMPLE_RATE) != 0;
    return s_frontend_up;
}

void unload(Loaded &l)
{
    delete l.interp;
    l.interp = nullptr;
    l.mrv = nullptr; /* lived in var_arena */
    if (l.arena != nullptr) {
        heap_caps_free(l.arena);
        l.arena = nullptr;
    }
    if (l.var_arena != nullptr) {
        heap_caps_free(l.var_arena);
        l.var_arena = nullptr;
    }
    l.arena_size = 0;
}

/* Build interpreter + arenas for one embedded model. On false the caller
 * unload()s the partial result. */
bool load_model(const WakeModel &m, Loaded &l)
{
    parse_manifest(m, l.mf);
    const tflite::Model *model = tflite::GetModel(m.tf);
    if (model->version() != TFLITE_SCHEMA_VERSION) {
        ESP_LOGE(TAG, "%s: model schema %lu != %d", m.id,
                 (unsigned long)model->version(), TFLITE_SCHEMA_VERSION);
        return false;
    }
    if (s_resolver == nullptr) {
        s_resolver = new tflite::MicroMutableOpResolver<20>();
        if (s_resolver == nullptr || !register_ops(*s_resolver)) {
            ESP_LOGE(TAG, "op registration failed");
            return false;
        }
    }
    l.var_arena = static_cast<uint8_t *>(heap_caps_malloc(VAR_ARENA, MALLOC_CAP_SPIRAM));
    if (l.var_arena == nullptr || !fresh_mrv(l)) {
        return false;
    }
    /* manifest size first, then 1.5x and 2x: esp-nn versions differ in scratch needs */
    const size_t attempts[] = {(l.mf.arena + 15) & ~(size_t)15,
                               (l.mf.arena * 3 / 2 + 15) & ~(size_t)15,
                               (l.mf.arena * 2 + 15) & ~(size_t)15};
    for (size_t sz : attempts) {
        uint8_t *a = static_cast<uint8_t *>(heap_caps_aligned_alloc(16, sz, MALLOC_CAP_SPIRAM));
        if (a == nullptr) {
            continue;
        }
        auto *ip = new tflite::MicroInterpreter(model, *s_resolver, a, sz, l.mrv);
        if (ip != nullptr && ip->AllocateTensors() == kTfLiteOk) {
            l.interp = ip;
            l.arena = a;
            l.arena_size = sz;
            break;
        }
        delete ip;
        heap_caps_free(a);
        if (!fresh_mrv(l)) {
            return false;
        }
    }
    if (l.interp == nullptr) {
        ESP_LOGE(TAG, "%s: tensor allocation failed (manifest arena %u B)", m.id,
                 (unsigned)l.mf.arena);
        return false;
    }
    TfLiteTensor *in = l.interp->input(0);
    TfLiteTensor *out = l.interp->output(0);
    if (in->dims->size != 3 || in->dims->data[0] != 1 || in->dims->data[2] != FEATURE_SIZE ||
        in->type != kTfLiteInt8 || out->type != kTfLiteUInt8) {
        ESP_LOGE(TAG, "%s: unexpected tensor layout (in dims=%d type=%d, out type=%d)",
                 m.id, in->dims->size, in->type, out->type);
        return false;
    }
    l.stride = static_cast<uint8_t>(in->dims->data[1]);
    return true;
}

void clear_state()
{
    std::memset(s_probs, 0, sizeof(s_probs));
    s_ignore = -COOL_OFF_SLICES;
    s_stride_step = 0;
    s_last_n = 0;
    if (s_frontend_up) {
        FrontendReset(&s_fstate);
    }
}

/* Make s_models[idx] the live model (detect task, or init before the task
 * exists). Keeps the current model on failure. The cutoff is deliberately
 * left alone: the settings slider is engine-relative, not model-relative. */
bool activate(int idx)
{
    if (idx < 0 || idx >= MODEL_COUNT) {
        return false;
    }
    Loaded nl;
    if (!load_model(s_models[idx], nl)) {
        unload(nl);
        ESP_LOGE(TAG, "\"%s\" (%s) failed to load%s", model_name(idx), s_models[idx].id,
                 s_current >= 0 ? " - keeping the current model" : "");
        return false;
    }
    bool step_changed = !s_frontend_up || nl.mf.step_ms != s_cur.mf.step_ms;
    unload(s_cur);
    s_cur = nl;
    s_current = idx;
    if (step_changed && !frontend_init(s_cur.mf.step_ms)) {
        ESP_LOGE(TAG, "frontend init failed");
        s_ready = false;
        return false;
    }
    clear_state();
    ESP_LOGI(TAG, "\"%s\" (%s): model %u B, arena %u B (used %u), stride %u, step %d ms, window %d",
             model_name(idx), s_models[idx].id,
             (unsigned)(s_models[idx].tf_end - s_models[idx].tf), (unsigned)s_cur.arena_size,
             (unsigned)s_cur.interp->arena_used_bytes(), s_cur.stride, s_cur.mf.step_ms,
             s_cur.mf.window);
    return true;
}

} // namespace

extern "C" {

bool wake_mww_init(const char *model_id)
{
    if (s_ready) {
        return true;
    }
    /* picker names: every manifest once (a few hundred bytes of JSON each) */
    for (int i = 0; i < MODEL_COUNT; i++) {
        Manifest mf;
        parse_manifest(s_models[i], mf);
        strlcpy(s_models[i].name, mf.name, sizeof(s_models[i].name));
    }
    int idx = wake_mww_model_index(model_id);
    if (idx < 0) {
        if (model_id != nullptr && model_id[0] != '\0') {
            ESP_LOGW(TAG, "unknown wake word id \"%s\" - using %s", model_id, s_models[0].id);
        }
        idx = 0;
    }
    if (!activate(idx)) {
        return false;
    }
    s_cutoff = static_cast<uint8_t>(std::clamp(s_cur.mf.cutoff * 255.0f, 1.0f, 255.0f));
    s_pending = idx;
    s_ready = true;
    ESP_LOGI(TAG, "%d wake word model(s) embedded, active \"%s\", cutoff %u/255",
             MODEL_COUNT, model_name(idx), s_cutoff);
    return true;
}

bool wake_mww_ready(void)
{
    return s_ready;
}

int wake_mww_model_count(void)
{
    return MODEL_COUNT;
}

const char *wake_mww_model_id(int i)
{
    return (i >= 0 && i < MODEL_COUNT) ? s_models[i].id : "";
}

const char *wake_mww_model_name(int i)
{
    return model_name(i);
}

int wake_mww_model_index(const char *id)
{
    if (id == nullptr || id[0] == '\0') {
        return -1;
    }
    for (int i = 0; i < MODEL_COUNT; i++) {
        if (strcmp(s_models[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

int wake_mww_current(void)
{
    return s_current;
}

void wake_mww_select(int i)
{
    if (i >= 0 && i < MODEL_COUNT) {
        s_pending = i;
    }
}

void wake_mww_set_cutoff(uint8_t pct)
{
    if (pct >= 1 && pct <= 99) {
        s_cutoff = static_cast<uint8_t>(pct * 255 / 100);
    }
}

void wake_mww_reset(void)
{
    clear_state();
}

bool wake_mww_feed(const int16_t *pcm, size_t samples)
{
    if (!s_ready) {
        return false;
    }
    int want = s_pending;
    if (want != s_current) {
        /* requested from settings/API; done here so the swap never races
         * an invoke (this task is the only consumer) */
        if (!activate(want)) {
            s_pending = s_current;
        }
        if (!s_ready) {
            return false;
        }
    }
    bool detected = false;
    while (samples > 0) {
        size_t consumed = 0;
        FrontendOutput fo = FrontendProcessSamples(&s_fstate, pcm, samples, &consumed);
        if (consumed == 0) {
            break; /* cannot happen with a sane frontend; avoid spinning */
        }
        pcm += consumed;
        samples -= consumed;
        if (fo.size == 0) {
            continue;
        }
        TfLiteTensor *in = s_cur.interp->input(0);
        int8_t *dst = tflite::GetTensorData<int8_t>(in) + FEATURE_SIZE * s_stride_step;
        for (size_t i = 0; i < FEATURE_SIZE; ++i) {
            int32_t v = (i < fo.size) ? ((int32_t)fo.values[i] * 256 + 333) / 666 + INT8_MIN : INT8_MIN;
            dst[i] = static_cast<int8_t>(std::clamp<int32_t>(v, INT8_MIN, INT8_MAX));
        }
        bool invoked = false;
        if (++s_stride_step >= s_cur.stride) {
            s_stride_step = 0;
            if (s_cur.interp->Invoke() != kTfLiteOk) {
                ESP_LOGW(TAG, "invoke failed");
                return false;
            }
            uint8_t p = s_cur.interp->output(0)->data.uint8[0];
            s_peak = std::max(s_peak, p);
            if (++s_last_n >= static_cast<size_t>(s_cur.mf.window)) {
                s_last_n = 0;
            }
            s_probs[s_last_n] = p;
            invoked = true;
        }
        /* cool-off counts SLICES (every 10 ms) whose latest probability is
         * below the cutoff: 100 slices = 1 s of quiet before the next wake */
        if (s_probs[s_last_n] < s_cutoff) {
            s_ignore = std::min<int16_t>(s_ignore + 1, 0);
        }
        if (!invoked || s_ignore < 0) {
            continue;
        }
        uint32_t sum = 0;
        for (int i = 0; i < s_cur.mf.window; ++i) {
            sum += s_probs[i];
        }
        if (sum > static_cast<uint32_t>(s_cutoff) * s_cur.mf.window) {
            detected = true;
            std::memset(s_probs, 0, sizeof(s_probs));
            s_ignore = -COOL_OFF_SLICES;
        }
    }
    return detected;
}

uint8_t wake_mww_peak(void)
{
    uint8_t p = s_peak;
    s_peak = 0;
    return p;
}

const char *wake_mww_name(void)
{
    return model_name(s_current >= 0 ? s_current : 0);
}

size_t wake_mww_arena_bytes(void)
{
    return s_cur.arena_size;
}

} // extern "C"

#else /* !CONFIG_BEDSIDE_WAKE_MWW: stubs. The registry reports the WakeNet
       * build's one fixed phrase so the settings picker/API stay valid. */

extern "C" {
bool wake_mww_init(const char *model_id) { (void)model_id; return false; }
bool wake_mww_ready(void) { return false; }
int wake_mww_model_count(void) { return 1; }
const char *wake_mww_model_id(int i) { return i == 0 ? "hi_esp" : ""; }
const char *wake_mww_model_name(int i) { return i == 0 ? "Hi ESP" : ""; }
int wake_mww_model_index(const char *id)
{
    return (id != NULL && strcmp(id, "hi_esp") == 0) ? 0 : -1;
}
int wake_mww_current(void) { return 0; }
void wake_mww_select(int i) { (void)i; }
void wake_mww_set_cutoff(uint8_t pct) { (void)pct; }
void wake_mww_reset(void) {}
bool wake_mww_feed(const int16_t *pcm, size_t samples) { (void)pcm; (void)samples; return false; }
uint8_t wake_mww_peak(void) { return 0; }
const char *wake_mww_name(void) { return "Hi ESP"; }
size_t wake_mww_arena_bytes(void) { return 0; }
}

#endif

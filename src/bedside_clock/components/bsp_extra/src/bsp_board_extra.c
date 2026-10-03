/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* MODIFIED (Apache-2.0 §4b notice): this file differs from upstream bsp_extra
 * 0.0.2 (Guition JC4880P443C vendor demo package,
 * 1-Demo/idf_examples/ESP-IDF_5.5.4/common_components/bsp_extra, an Espressif
 * BSP-extras derivative that is not on the component registry). Changes by
 * this repository's author (github.com/revoltn), 2026-09-02 to 2026-09-27:
 * stereo-to-mono downmix because the ES8311 DAC is mono; per-device recursive
 * locks around codec read/write/reconfigure; set_fs skips a redundant
 * close/reopen, re-applies the caller's mic gain and logs DMA-capable heap
 * headroom; a pluggable alternative output (bsp_extra_sink_t, used for USB
 * audio) is tried before the codec with an unconditional fallback to it. See
 * THIRD_PARTY_NOTICES.md. */

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_codec_dev_defaults.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

#include "bsp/esp-bsp.h"
#include "bsp_board_extra.h"

static const char *TAG = "bsp_extra_board";

static esp_codec_dev_handle_t play_dev_handle;
static esp_codec_dev_handle_t record_dev_handle;

static bool _is_audio_init = false;
static bool _is_player_init = false;
static int _vloume_intensity = CODEC_DEFAULT_VOLUME;
static int _in_gain_db = CODEC_DEFAULT_ADC_VOLUME;

/* Serializes codec reconfiguration against concurrent reads/writes: the
 * decoder task (44.1k stereo), the voice feed task (16k mono) and the alarm
 * chime all call set_fs/read/write on the same two codec devices. Without
 * this, a close/reopen can land under an in-flight esp_codec_dev_read.
 * Per-device locks so full-duplex (chime playing + mic capture) never
 * serializes the mic read behind a blocking speaker write. Reconfigure
 * paths take BOTH, always play first. Recursive: dev_resume() nests into
 * set_fs(). */
static SemaphoreHandle_t _play_mtx;
static SemaphoreHandle_t _rec_mtx;

/* Last applied format; lets set_fs skip a redundant close/reopen (which
 * cycles the PA and pops) when the format is unchanged. */
static bool _fs_valid = false;
static uint32_t _fs_rate;
static uint32_t _fs_bits;
static i2s_slot_mode_t _fs_ch;

static void dev_lock(SemaphoreHandle_t m)
{
    if (m != NULL) {
        xSemaphoreTakeRecursive(m, portMAX_DELAY);
    }
}

static void dev_unlock(SemaphoreHandle_t m)
{
    if (m != NULL) {
        xSemaphoreGiveRecursive(m);
    }
}

static void codec_lock(void)   /* both devices, play first */
{
    dev_lock(_play_mtx);
    dev_lock(_rec_mtx);
}

static void codec_unlock(void)
{
    dev_unlock(_rec_mtx);
    dev_unlock(_play_mtx);
}

static audio_player_cb_t audio_idle_callback = NULL;
static void *audio_idle_cb_user_data = NULL;
static char audio_file_path[128];

/**************************************************************************************************
 *
 * Extra Board Function
 *
 **************************************************************************************************/

static esp_err_t audio_mute_function(AUDIO_PLAYER_MUTE_SETTING setting)
{
    // Volume saved when muting and restored when unmuting. Restoring volume is necessary
    // as es8311_set_voice_mute(true) results in voice volume (REG32) being set to zero.

    bsp_extra_codec_mute_set(setting == AUDIO_PLAYER_MUTE ? true : false);

    // restore the voice volume upon unmuting
    if (setting == AUDIO_PLAYER_UNMUTE) {
        ESP_RETURN_ON_ERROR(esp_codec_dev_set_out_vol(play_dev_handle, _vloume_intensity), TAG, "Set Codec volume failed");
    }

    return ESP_OK;
}

static void audio_callback(audio_player_cb_ctx_t *ctx)
{
    if (audio_idle_callback) {
        ctx->user_ctx = audio_idle_cb_user_data;
        audio_idle_callback(ctx);
    }
}

esp_err_t bsp_extra_i2s_read(void *audio_buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms)
{
    dev_lock(_rec_mtx);
    esp_err_t ret = esp_codec_dev_read(record_dev_handle, audio_buffer, len);
    dev_unlock(_rec_mtx);
    if (ret == ESP_OK) {
        if (bytes_read) {
            *bytes_read = len;
        }
    } else {
        if (bytes_read) {
            *bytes_read = 0;
        }
    }
    return ret;
}

/* The ES8311 is a MONO codec: in stereo mode its DAC latches only one I2S
 * slot, silently dropping the other channel. Downmix stereo 16-bit audio to
 * the average of L+R written to BOTH slots, so the single speaker plays the
 * whole mix. Tracked from bsp_extra_codec_set_fs. */
static i2s_slot_mode_t _out_slot_mode = I2S_SLOT_MODE_MONO;
static uint32_t _out_bits = 16;

/* Alternative output (a USB speaker), preferred while it is active. Read once
 * per call into a local: the registering module may swap it from another task.
 * The BSP deliberately knows nothing about USB - see bsp_extra_sink_t. */
static const bsp_extra_sink_t *volatile _sink;

void bsp_extra_sink_register(const bsp_extra_sink_t *sink)
{
    _sink = sink;
}

/* Always the codec, whatever is registered. */
esp_err_t bsp_extra_i2s_write_codec(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    dev_lock(_play_mtx);
    if (_out_slot_mode == I2S_SLOT_MODE_STEREO && _out_bits == 16) {
        int16_t *s = (int16_t *)audio_buffer;
        size_t frames = len / 4;
        for (size_t i = 0; i < frames; i++) {
            int16_t m = (int16_t)(((int32_t)s[2 * i] + s[2 * i + 1]) / 2);
            s[2 * i] = m;
            s[2 * i + 1] = m;
        }
    }

    esp_err_t ret = esp_codec_dev_write(play_dev_handle, audio_buffer, len);
    dev_unlock(_play_mtx);
    if (ret == ESP_OK) {
        if (bytes_written) {
            *bytes_written = len;
        }
    } else {
        if (bytes_written) {
            *bytes_written = 0;
        }
    }
    return ret;
}

esp_err_t bsp_extra_i2s_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms)
{
    /* Try the alternative sink FIRST, and before the mono downmix below,
     * which rewrites the caller's buffer in place - a USB speaker must see
     * the untouched stereo. On any failure fall straight through to the
     * codec: the built-in speaker is the fallback that keeps the alarm
     * audible whatever USB does. */
    const bsp_extra_sink_t *sink = _sink;
    if (sink != NULL && sink->active != NULL && sink->active()) {
        if (sink->write(audio_buffer, len, timeout_ms) == ESP_OK) {
            if (bytes_written) {
                *bytes_written = len;
            }
            return ESP_OK;
        }
    }
    return bsp_extra_i2s_write_codec(audio_buffer, len, bytes_written, timeout_ms);
}

esp_err_t bsp_extra_codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;

    /* Tell the sink the new source format BEFORE taking the codec lock, so a
     * sink that needs its own lock can never invert lock order against it.
     * Told unconditionally, ahead of the unchanged-format early-out below,
     * because the sink tracks its own state and may have attached since. */
    const bsp_extra_sink_t *sink = _sink;
    if (sink != NULL && sink->set_fs != NULL) {
        sink->set_fs(rate, bits_cfg, ch);
    }

    codec_lock();
    if (_fs_valid && rate == _fs_rate && bits_cfg == _fs_bits && ch == _fs_ch) {
        /* unchanged format: skip the close/reopen (PA cycle + pop), which
         * the decoder otherwise triggers at the start of EVERY song */
        codec_unlock();
        return ESP_OK;
    }
    /* The I2S reconfig re-allocates its DMA descriptors/buffers from
     * DMA-capable INTERNAL RAM (6 x ~1 KB per direction). On this board that
     * pool is the scarce resource, and a failed allocation used to panic
     * (see the esp_codec_dev_open local patch). Log the headroom every time
     * so a failure can be correlated with what else was running. */
    ESP_LOGI(TAG, "rate = %" PRId32 " bits_cfg = %" PRId32 " ch = %d | internal free %u (largest %u), dma free %u (largest %u)",
             rate, bits_cfg, (int)ch,
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA),
             (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    _out_slot_mode = ch;
    _out_bits = bits_cfg;

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bits_cfg,
    };

    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_close(record_dev_handle);
        /* re-apply the caller-chosen mic gain, not the factory default -
         * the alarm chime used to silently reset the voice mic gain here */
        ret |= esp_codec_dev_set_in_gain(record_dev_handle, _in_gain_db);
    }

    if (play_dev_handle) {
        ret |= esp_codec_dev_open(play_dev_handle, &fs);
    }
    if (record_dev_handle) {
        ret |= esp_codec_dev_open(record_dev_handle, &fs);
    }

    _fs_valid = (ret == ESP_OK);
    _fs_rate = rate;
    _fs_bits = bits_cfg;
    _fs_ch = ch;
    codec_unlock();

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "set_fs %" PRId32 "/%" PRId32 "/%d FAILED ret=0x%x - internal heap state:",
                 rate, bits_cfg, (int)ch, ret);
        heap_caps_print_heap_info(MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA);
    } else {
        ESP_LOGI(TAG, "ret = 0x%x , %s", ret, strerror(ret));
    }
    return ret;
}

esp_err_t bsp_extra_codec_set_fs_play(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    esp_err_t ret = ESP_OK;
    ESP_LOGI(TAG,"rate = %" PRId32 "bits_cfg = %" PRId32, rate,bits_cfg);

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = CODEC_DEFAULT_CHANNEL,
        .bits_per_sample = bits_cfg,
    };

    dev_lock(_play_mtx);
    _fs_valid = false; /* play/record formats now diverge */
    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }

    if (play_dev_handle) {
        ret |= esp_codec_dev_open(play_dev_handle, &fs);
    }
    dev_unlock(_play_mtx);

    ESP_LOGI(TAG,"ret = 0x%x , %s",ret,strerror(ret));
    return ret;
}

bool bsp_extra_codec_get_fs(uint32_t *rate, uint32_t *bits_cfg, i2s_slot_mode_t *ch)
{
    if (!_fs_valid) {
        return false;
    }
    if (rate != NULL) {
        *rate = _fs_rate;
    }
    if (bits_cfg != NULL) {
        *bits_cfg = _fs_bits;
    }
    if (ch != NULL) {
        *ch = _fs_ch;
    }
    return true;
}

esp_err_t bsp_extra_codec_volume_set(int volume, int *volume_set)
{
    /* Outside the codec lock, for the same lock-order reason as set_fs. */
    const bsp_extra_sink_t *sink = _sink;
    if (sink != NULL && sink->volume != NULL) {
        sink->volume(volume);
    }
    if (play_dev_handle == NULL) {
        return ESP_ERR_INVALID_STATE; /* codec never came up: nothing to set */
    }
    /* Serialize against set_fs closing/reopening the same device from
     * another task (LVGL, httpd, music and alarm all set the volume). */
    dev_lock(_play_mtx);
    esp_err_t ret = esp_codec_dev_set_out_vol(play_dev_handle, volume);
    dev_unlock(_play_mtx);
    ESP_RETURN_ON_ERROR(ret, TAG, "Set Codec volume failed");
    _vloume_intensity = volume;

    ESP_LOGI(TAG, "Setting volume: %d", volume);

    return ESP_OK;
}

int bsp_extra_codec_volume_get(void)
{
    return _vloume_intensity;
}

esp_err_t bsp_extra_codec_mute_set(bool enable)
{
    esp_err_t ret = ESP_OK;
    ret = esp_codec_dev_set_out_mute(play_dev_handle, enable);
    return ret;
}

esp_err_t bsp_extra_codec_dev_stop(void)
{
    esp_err_t ret = ESP_OK;

    codec_lock();
    _fs_valid = false;
    if (play_dev_handle) {
        ret = esp_codec_dev_close(play_dev_handle);
    }

    if (record_dev_handle) {
        ret = esp_codec_dev_close(record_dev_handle);
    }
    codec_unlock();
    return ret;
}

esp_err_t bsp_extra_codec_dev_resume(void)
{
    return bsp_extra_codec_set_fs(CODEC_DEFAULT_SAMPLE_RATE, CODEC_DEFAULT_BIT_WIDTH, CODEC_DEFAULT_CHANNEL);
}

esp_err_t bsp_extra_codec_in_gain_set(int gain)
{
    if (!record_dev_handle) {
        return ESP_ERR_INVALID_STATE;
    }
    dev_lock(_rec_mtx);
    _in_gain_db = gain; /* survives future set_fs reconfigures */
    esp_err_t ret = esp_codec_dev_set_in_gain(record_dev_handle, gain);
    dev_unlock(_rec_mtx);
    return ret;
}

esp_err_t bsp_extra_codec_init()
{
    if (_is_audio_init) {
        return ESP_OK;
    }

    if (_play_mtx == NULL) {
        _play_mtx = xSemaphoreCreateRecursiveMutex();
    }
    if (_rec_mtx == NULL) {
        _rec_mtx = xSemaphoreCreateRecursiveMutex();
    }
    if (_play_mtx == NULL || _rec_mtx == NULL) {
        ESP_LOGE(TAG, "codec mutex alloc failed");
        return ESP_ERR_NO_MEM;
    }

    /* A codec that does not answer on I2C (or an I2S DMA allocation that
     * fails) is a runtime condition: report it and let the caller retry
     * later. This used to assert() - one flaky ES8311 probe at boot turned
     * the clock into a panic/reboot loop. Handles that did come up are kept
     * so a retry does not re-initialize them. */
    if (play_dev_handle == NULL) {
        play_dev_handle = bsp_audio_codec_speaker_init();
        if (play_dev_handle == NULL) {
            ESP_LOGE(TAG, "speaker codec init failed (ES8311 not responding?)");
            return ESP_FAIL;
        }
    }
    if (record_dev_handle == NULL) {
        record_dev_handle = bsp_audio_codec_microphone_init();
        if (record_dev_handle == NULL) {
            ESP_LOGE(TAG, "microphone codec init failed (ES8311 not responding?)");
            return ESP_FAIL;
        }
    }

    esp_err_t ret = bsp_extra_codec_set_fs(CODEC_DEFAULT_SAMPLE_RATE, CODEC_DEFAULT_BIT_WIDTH, CODEC_DEFAULT_CHANNEL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "initial codec format setup failed: 0x%x", ret);
        return ret; /* _is_audio_init stays false: next ensure_init retries */
    }

    _is_audio_init = true;

    return ESP_OK;
}

esp_err_t bsp_extra_player_init(void)
{
    if (_is_player_init) {
        return ESP_OK;
    }

    audio_player_config_t config = { .mute_fn = audio_mute_function,
                                     .write_fn = bsp_extra_i2s_write,
                                     .clk_set_fn = bsp_extra_codec_set_fs,
                                     .priority = 5
                                   };
    ESP_RETURN_ON_ERROR(audio_player_new(config), TAG, "audio_player_init failed");
    audio_player_callback_register(audio_callback, NULL);

    _is_player_init = true;

    return ESP_OK;
}

esp_err_t bsp_extra_player_del(void)
{
    _is_player_init = false;

    ESP_RETURN_ON_ERROR(audio_player_delete(), TAG, "audio_player_delete failed");

    return ESP_OK;
}

esp_err_t bsp_extra_file_instance_init(const char *path, file_iterator_instance_t **ret_instance)
{
    ESP_RETURN_ON_FALSE(path, ESP_FAIL, TAG, "path is NULL");
    ESP_RETURN_ON_FALSE(ret_instance, ESP_FAIL, TAG, "ret_instance is NULL");

    file_iterator_instance_t *file_iterator = file_iterator_new(path);
    ESP_RETURN_ON_FALSE(file_iterator, ESP_FAIL, TAG, "file_iterator_new failed, %s", path);

    *ret_instance = file_iterator;

    return ESP_OK;
}

esp_err_t bsp_extra_player_play_index(file_iterator_instance_t *instance, int index)
{
    ESP_RETURN_ON_FALSE(instance, ESP_FAIL, TAG, "instance is NULL");

    ESP_LOGI(TAG, "play_index(%d)", index);
    char filename[128];
    int retval = file_iterator_get_full_path_from_index(instance, index, filename, sizeof(filename));
    ESP_RETURN_ON_FALSE(retval != 0, ESP_FAIL, TAG, "file_iterator_get_full_path_from_index failed");

    ESP_LOGI(TAG, "opening file '%s'", filename);
    FILE *fp = fopen(filename, "rb");
    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, TAG, "unable to open file");

    ESP_LOGI(TAG, "Playing '%s'", filename);
    ESP_RETURN_ON_ERROR(audio_player_play(fp), TAG, "audio_player_play failed");

    memcpy(audio_file_path, filename, sizeof(audio_file_path));

    return ESP_OK;
}

esp_err_t bsp_extra_player_play_file(const char *file_path)
{
    ESP_LOGI(TAG, "opening file '%s'", file_path);
    FILE *fp = fopen(file_path, "rb");
    ESP_RETURN_ON_FALSE(fp, ESP_FAIL, TAG, "unable to open file");

    ESP_LOGI(TAG, "Playing '%s'", file_path);
    ESP_RETURN_ON_ERROR(audio_player_play(fp), TAG, "audio_player_play failed");

    memcpy(audio_file_path, file_path, sizeof(audio_file_path));

    return ESP_OK;
}

void bsp_extra_player_register_callback(audio_player_cb_t cb, void *user_data)
{
    audio_idle_callback = cb;
    audio_idle_cb_user_data = user_data;
}

bool bsp_extra_player_is_playing_by_path(const char *file_path)
{
    return (strcmp(audio_file_path, file_path) == 0);
}

bool bsp_extra_player_is_playing_by_index(file_iterator_instance_t *instance, int index)
{
    return (index == file_iterator_get_index(instance));
}
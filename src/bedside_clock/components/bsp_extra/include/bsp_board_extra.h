/*
 * SPDX-FileCopyrightText: 2015-2024 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* MODIFIED (Apache-2.0 §4b notice): this file differs from upstream bsp_extra
 * 0.0.2 (Guition JC4880P443C vendor demo package,
 * 1-Demo/idf_examples/ESP-IDF_5.5.4/common_components/bsp_extra, an Espressif
 * BSP-extras derivative that is not on the component registry). Changes by
 * this repository's author (github.com/revoltn), 2026-09-26: declares
 * bsp_extra_i2s_write_codec(), bsp_extra_codec_get_fs(), bsp_extra_sink_t and
 * bsp_extra_sink_register() for the alternative-output (USB speaker) path. See
 * THIRD_PARTY_NOTICES.md. */

#pragma once

#include <sys/cdefs.h>
#include <stdbool.h>
#include "esp_codec_dev.h"
#include "esp_err.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include "audio_player.h"
#include "file_iterator.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CODEC_DEFAULT_SAMPLE_RATE           (44100)
#define CODEC_DEFAULT_BIT_WIDTH             (16)
#define CODEC_DEFAULT_ADC_VOLUME            (24.0)
#define CODEC_DEFAULT_CHANNEL               (I2S_SLOT_MODE_STEREO)
#define CODEC_DEFAULT_VOLUME                (60)

#define BSP_LCD_BACKLIGHT_BRIGHTNESS_MAX    (95)
#define BSP_LCD_BACKLIGHT_BRIGHTNESS_MIN    (0)
#define LCD_LEDC_CH                         (CONFIG_BSP_DISPLAY_BRIGHTNESS_LEDC_CH)

/**************************************************************************************************
 * BSP Extra interface
 * Mainly provided some I2S Codec interfaces.
 **************************************************************************************************/
/**
 * @brief Player set mute.
 *
 * @param enable: true or false
 *
 * @return
 *    - ESP_OK: Success
 *    - Others: Fail
 */
esp_err_t bsp_extra_codec_mute_set(bool enable);

/**
 * @brief Player set volume.
 *
 * @param volume: volume set
 * @param volume_set: volume set response
 *
 * @return
 *    - ESP_OK: Success
 *    - Others: Fail
 */
esp_err_t bsp_extra_codec_volume_set(int volume, int *volume_set);

/** 
 * @brief Player get volume.
 * 
 * @return
 *   - volume: volume get
 */
int bsp_extra_codec_volume_get(void);

/**
 * @brief Stop I2S function.
 *
 * @return
 *    - ESP_OK: Success
 *    - Others: Fail
 */
esp_err_t bsp_extra_codec_dev_stop(void);

/**
 * @brief Resume I2S function.
 *
 * @return
 *    - ESP_OK: Success
 *    - Others: Fail
 */
esp_err_t bsp_extra_codec_dev_resume(void);

/**
 * @brief Set I2S format to codec.
 *
 * @param rate: Sample rate of sample
 * @param bits_cfg: Bit lengths of one channel data
 * @param ch: Channels of sample
 *
 * @return
 *    - ESP_OK: Success
 *    - Others: Fail
 */
esp_err_t bsp_extra_codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch);
esp_err_t bsp_extra_codec_set_fs_play(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch);

/**
 * @brief Set codec input (microphone) gain.
 *
 * @param gain: gain value to set (codec-specific)
 * @return
 *    - ESP_OK: Success
 *    - Others: Fail
 */
esp_err_t bsp_extra_codec_in_gain_set(int gain);

/**
 * @brief Read data from recoder.
 *
 * @param audio_buffer: The pointer of receiving data buffer
 * @param len: Max data buffer length
 * @param bytes_read: Byte number that actually be read, can be NULL if not needed
 * @param timeout_ms: Max block time
 *
 * @return
 *    - ESP_OK: Success
 *    - Others: Fail
 */
esp_err_t bsp_extra_i2s_read(void *audio_buffer, size_t len, size_t *bytes_read, uint32_t timeout_ms);

/**
 * @brief Write data to player.
 *
 * @param audio_buffer: The pointer of sent data buffer
 * @param len: Max data buffer length
 * @param bytes_written: Byte number that actually be sent, can be NULL if not needed
 * @param timeout_ms: Max block time
 *
 * @return
 *    - ESP_OK: Success
 *    - Others: Fail
 */
esp_err_t bsp_extra_i2s_write(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms);

/**
 * @brief Write PCM to the ES8311 codec, never to an alternative sink.
 *
 * Same as bsp_extra_i2s_write() but always goes to the on-board speaker even
 * when a USB sink is registered and active. Only for sound that is physically
 * tied to this board: the voice self-test tone, which is measured back through
 * the ES8311 microphone and is meaningless if it comes out of a USB speaker.
 */
esp_err_t bsp_extra_i2s_write_codec(void *audio_buffer, size_t len, size_t *bytes_written, uint32_t timeout_ms);

/**
 * @brief Report the audio format the codec is currently open at.
 *
 * Lets a sink that attaches mid-stream (a USB speaker plugged in during a
 * song) learn the source format without waiting for the next set_fs, which
 * the decoder only issues when the format actually changes.
 *
 * @return true when a format has been applied, false before the first set_fs.
 */
bool bsp_extra_codec_get_fs(uint32_t *rate, uint32_t *bits_cfg, i2s_slot_mode_t *ch);

/**
 * @brief An alternative audio output, tried ahead of the on-board codec.
 *
 * Registered by main/usb_audio.c so a plugged-in USB speaker takes over from
 * the built-in mono one. The BSP knows nothing about USB: it just prefers the
 * sink while active() says so, and silently falls back to the codec the moment
 * any call fails. That fallback is what keeps a USB fault from ever silencing
 * the alarm, so it must stay unconditional.
 *
 * All four callbacks must be non-blocking or bounded; they are invoked from
 * the decoder, alarm, voice and music tasks.
 */
typedef struct {
    bool      (*active)(void);      /*!< device present and stream running */
    esp_err_t (*write)(const void *buf, size_t len, uint32_t timeout_ms);
    esp_err_t (*set_fs)(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch);
    esp_err_t (*volume)(int volume); /*!< 0-100, same scale as the codec */
} bsp_extra_sink_t;

/**
 * @brief Register (or with NULL, remove) the alternative output.
 *
 * The pointer is stored, not copied: pass a pointer with static lifetime.
 */
void bsp_extra_sink_register(const bsp_extra_sink_t *sink);


/**
 * @brief Initialize codec play and record handle.
 *
 * @return
 *      - ESP_OK: Success
 *      - Others: Fail
 */
esp_err_t bsp_extra_codec_init();

/**
 * @brief Initialize audio player task.
 *
 * @param path file path
 *
 * @return
 *      - ESP_OK: Success
 *      - Others: Fail
 */
esp_err_t bsp_extra_player_init(void);

/**
 * @brief Delete audio player task.
 *
 * @return
 *      - ESP_OK: Success
 *      - Others: Fail
 */
esp_err_t bsp_extra_player_del(void);

/**
 * @brief Initialize a file iterator instance
 *
 * @param path The file path for the iterator.
 * @param ret_instance A pointer to the file iterator instance to be returned.
 * @return
 *     - ESP_OK: Successfully initialized the file iterator instance.
 *     - ESP_FAIL: Failed to initialize the file iterator instance due to invalid parameters or memory allocation failure.
 */
esp_err_t bsp_extra_file_instance_init(const char *path, file_iterator_instance_t **ret_instance);

/**
 * @brief Play the audio file at the specified index in the file iterator
 *
 * @param instance The file iterator instance.
 * @param index The index of the file to play within the iterator.
 * @return
 *     - ESP_OK: Successfully started playing the audio file.
 *     - ESP_FAIL: Failed to play the audio file due to invalid parameters or file access issues.
 */
esp_err_t bsp_extra_player_play_index(file_iterator_instance_t *instance, int index);

/**
 * @brief Play the audio file specified by the file path
 *
 * @param file_path The path to the audio file to be played.
 * @return
 *     - ESP_OK: Successfully started playing the audio file.
 *     - ESP_FAIL: Failed to play the audio file due to file access issues.
 */
esp_err_t bsp_extra_player_play_file(const char *file_path);

/**
 * @brief Register a callback function for the audio player
 *
 * @param cb The callback function to be registered.
 * @param user_data User data to be passed to the callback function.
 */
void bsp_extra_player_register_callback(audio_player_cb_t cb, void *user_data);

/**
 * @brief Check if the specified audio file is currently playing
 *
 * @param file_path The path to the audio file to check.
 * @return
 *     - true: The specified audio file is currently playing.
 *     - false: The specified audio file is not currently playing.
 */
bool bsp_extra_player_is_playing_by_path(const char *file_path);

/**
 * @brief Check if the audio file at the specified index is currently playing
 *
 * @param instance The file iterator instance.
 * @param index The index of the file to check.
 * @return
 *     - true: The audio file at the specified index is currently playing.
 *     - false: The audio file at the specified index is not currently playing.
 */
bool bsp_extra_player_is_playing_by_index(file_iterator_instance_t *instance, int index);

#ifdef __cplusplus
}
#endif

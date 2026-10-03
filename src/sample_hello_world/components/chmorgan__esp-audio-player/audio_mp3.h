/* MODIFIED (Apache-2.0 §4b notice): this file differs from upstream
 * esp-audio-player 1.0.7 (https://github.com/chmorgan/esp-audio-player).
 * Changes by this repository's author (github.com/revoltn), 2026-09-01: adds
 * consec_errors to the decoder instance (see audio_mp3.cpp). See
 * THIRD_PARTY_NOTICES.md. */

#pragma once

#include <stdio.h>
#include "audio_decode_types.h"
#include "mp3dec.h"

typedef struct {
    char header[3];     /*!< Always "TAG" */
    char title[30];     /*!< Audio title */
    char artist[30];    /*!< Audio artist */
    char album[30];     /*!< Album name */
    char year[4];       /*!< Char array of year */
    char comment[30];   /*!< Extra comment */
    char genre;         /*!< See "https://en.wikipedia.org/wiki/ID3" */
} __attribute__((packed)) mp3_id3_header_v1_t;

typedef struct {
    char header[3];     /*!< Always "ID3" */
    char ver;           /*!< Version, equals to3 if ID3V2.3 */
    char revision;      /*!< Revision, should be 0 */
    char flag;          /*!< Flag byte, use Bit[7..5] only */
    char size[4];       /*!< TAG size */
} __attribute__((packed)) mp3_id3_header_v2_t;

typedef struct {
    // Constants below
    uint8_t *data_buf;

    /** number of bytes in data_buf */
    size_t data_buf_size;

    // Values that change at runtime are below

    /**
     * Total bytes in data_buf,
     * not the number of bytes remaining after the read_ptr
     */
    size_t bytes_in_data_buf;

    /** Pointer to read location in data_buf */
    uint8_t *read_ptr;

    // set to true if the end of file has been reached
    bool eof_reached;

    /** consecutive MP3Decode failures without a good frame; used to give
     * up on undecodable streams instead of spinning forever */
    int consec_errors;
} mp3_instance;

bool is_mp3(FILE *fp);
DECODE_STATUS decode_mp3(HMP3Decoder mp3_decoder, FILE *fp, decode_data *pData, mp3_instance *pInstance);

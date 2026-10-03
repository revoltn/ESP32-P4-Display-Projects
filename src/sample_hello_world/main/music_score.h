/* Opening of J.S. Bach, Toccata and Fugue in D minor, BWV 565.
 * Up to 4 simultaneous frequencies per entry; f[0]==0 means rest. */
#pragma once

#include <stdint.h>

typedef struct {
    float f[4];     /* voice frequencies in Hz, 0 = unused */
    uint16_t ms;    /* duration */
} note_t;

static const note_t TOCCATA_SCORE[] = {
    /* Phrase 1: mordent + descending run (high octave) */
    {{880.00f},  180}, {{783.99f}, 180}, {{880.00f}, 700}, {{0}, 250},
    {{783.99f},  140}, {{698.46f}, 140}, {{659.26f}, 140}, {{587.33f}, 140},
    {{554.37f},  300}, {{587.33f}, 900}, {{0}, 400},
    /* Phrase 2: octave down */
    {{440.00f},  180}, {{392.00f}, 180}, {{440.00f}, 700}, {{0}, 250},
    {{329.63f},  240}, {{349.23f}, 240}, {{277.18f}, 300}, {{293.66f}, 900}, {{0}, 400},
    /* Phrase 3: another octave down */
    {{220.00f},  180}, {{196.00f}, 180}, {{220.00f}, 700}, {{0}, 250},
    {{164.81f},  240}, {{174.61f}, 240}, {{138.59f}, 300}, {{146.83f}, 900}, {{0}, 600},
    /* Finale: C#dim7 arpeggio up, held diminished chord, resolving D minor chord */
    {{138.59f},  200}, {{164.81f}, 200}, {{196.00f}, 200}, {{233.08f}, 200},
    {{277.18f, 329.63f, 392.00f, 233.08f}, 1800}, {{0}, 300},
    {{146.83f, 220.00f, 293.66f, 349.23f}, 2500},
};

#define TOCCATA_SCORE_LEN (sizeof(TOCCATA_SCORE) / sizeof(TOCCATA_SCORE[0]))

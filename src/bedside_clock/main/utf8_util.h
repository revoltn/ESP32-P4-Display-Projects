#pragma once

#include <string.h>

/* Repair the tail of a UTF-8 string that was cut at a byte boundary
 * (strlcpy / snprintf into a fixed buffer). A multibyte sequence sliced in
 * half leaves a dangling lead byte or stray continuation bytes; LVGL renders
 * that as a box and browsers reject it inside JSON. Removes the incomplete
 * sequence in place. Complete strings are left untouched. */
static inline void utf8_trim_tail(char *s)
{
    size_t n = strlen(s);
    if (n == 0) {
        return;
    }
    /* step back over trailing continuation bytes (10xxxxxx); a valid
     * sequence has at most three */
    size_t i = n;
    int cont = 0;
    while (i > 0 && cont < 4 && ((unsigned char)s[i - 1] & 0xC0) == 0x80) {
        i--;
        cont++;
    }
    if (i == 0) {
        s[0] = '\0'; /* nothing but continuation bytes */
        return;
    }
    unsigned char lead = (unsigned char)s[i - 1];
    int need;
    if (lead < 0x80) {
        need = 0;
    } else if ((lead & 0xE0) == 0xC0) {
        need = 1;
    } else if ((lead & 0xF0) == 0xE0) {
        need = 2;
    } else if ((lead & 0xF8) == 0xF0) {
        need = 3;
    } else {
        need = -1; /* not a valid lead byte */
    }
    if (need == cont) {
        return; /* last sequence is complete */
    }
    if (need == 0) {
        s[i] = '\0'; /* ASCII followed by stray continuation bytes */
    } else {
        s[i - 1] = '\0'; /* truncated or malformed sequence: drop it */
    }
}

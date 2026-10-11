/* Converting between UTF-8 and UTF-16, and building strings. */

#include "internal.h"

#include <stdlib.h>

#define REPLACEMENT 0xFFFD

void ghulrt_utf16_add(ghulrt_utf16 *text, uint16_t unit) {
    if (text->length == text->capacity) {
        int32_t capacity = text->capacity ? text->capacity * 2 : 64;
        uint16_t *chars = realloc(text->chars, (size_t)capacity * sizeof(uint16_t));

        if (!chars) {
            ghulrt_fail("Out of memory.\n");
        }

        text->chars = chars;
        text->capacity = capacity;
    }

    text->chars[text->length++] = unit;
}

void *ghulrt_utf16_make_string(ghulrt_utf16 *text) {
    static const uint16_t empty = 0;

    void *string = ghul_make_string(text->chars ? text->chars : &empty, text->length);

    free(text->chars);
    text->chars = NULL;
    text->length = 0;
    text->capacity = 0;

    return string;
}

static void add_code_point(ghulrt_utf16 *text, uint32_t code_point) {
    if (code_point >= 0x10000) {
        code_point -= 0x10000;
        ghulrt_utf16_add(text, (uint16_t)(0xD800 + (code_point >> 10)));
        ghulrt_utf16_add(text, (uint16_t)(0xDC00 + (code_point & 0x3FF)));
    } else {
        ghulrt_utf16_add(text, (uint16_t)code_point);
    }
}

void ghulrt_utf8_decode(const char *bytes, size_t length, ghulrt_utf16 *text) {
    const unsigned char *in = (const unsigned char *)bytes;
    size_t i = 0;

    while (i < length) {
        unsigned char lead = in[i];

        if (lead < 0x80) {
            ghulrt_utf16_add(text, lead);
            i++;
            continue;
        }

        size_t extra;
        uint32_t code_point;
        uint32_t minimum;

        if (lead >= 0xC2 && lead <= 0xDF) {
            extra = 1;
            code_point = lead & 0x1F;
            minimum = 0x80;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            extra = 2;
            code_point = lead & 0x0F;
            minimum = 0x800;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            extra = 3;
            code_point = lead & 0x07;
            minimum = 0x10000;
        } else {
            add_code_point(text, REPLACEMENT);
            i++;
            continue;
        }

        /*
         * A truncated or malformed sequence is replaced as far as its
         * valid prefix, and decoding resumes at the byte that broke it,
         * as .NET's decoder does.
         */
        size_t taken = 1;

        while (taken <= extra && i + taken < length && (in[i + taken] & 0xC0) == 0x80) {
            code_point = (code_point << 6) | (in[i + taken] & 0x3F);
            taken++;
        }

        if (taken <= extra || code_point < minimum || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            add_code_point(text, REPLACEMENT);
            i += taken;
            continue;
        }

        add_code_point(text, code_point);
        i += taken;
    }
}

static size_t put(char *out, uint32_t code_point) {
    if (code_point < 0x80) {
        out[0] = (char)code_point;
        return 1;
    }

    if (code_point < 0x800) {
        out[0] = (char)(0xC0 | (code_point >> 6));
        out[1] = (char)(0x80 | (code_point & 0x3F));
        return 2;
    }

    if (code_point < 0x10000) {
        out[0] = (char)(0xE0 | (code_point >> 12));
        out[1] = (char)(0x80 | ((code_point >> 6) & 0x3F));
        out[2] = (char)(0x80 | (code_point & 0x3F));
        return 3;
    }

    out[0] = (char)(0xF0 | (code_point >> 18));
    out[1] = (char)(0x80 | ((code_point >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((code_point >> 6) & 0x3F));
    out[3] = (char)(0x80 | (code_point & 0x3F));
    return 4;
}

size_t ghulrt_utf8_encode(const uint16_t *chars, int32_t length, char *out, uint16_t *pending) {
    size_t written = 0;

    for (int32_t i = 0; i < length; i++) {
        uint16_t unit = chars[i];

        if (*pending) {
            uint16_t high = *pending;
            *pending = 0;

            if (unit >= 0xDC00 && unit <= 0xDFFF) {
                uint32_t code_point = 0x10000 + (((uint32_t)high - 0xD800) << 10) + (unit - 0xDC00);
                written += put(out + written, code_point);
                continue;
            }

            written += put(out + written, REPLACEMENT);
        }

        if (unit >= 0xD800 && unit <= 0xDBFF) {
            *pending = unit;
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            written += put(out + written, REPLACEMENT);
        } else {
            written += put(out + written, unit);
        }
    }

    return written;
}

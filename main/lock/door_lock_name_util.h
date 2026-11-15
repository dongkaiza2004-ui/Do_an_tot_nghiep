#pragma once

#include <stddef.h>
#include <string.h>

// Cat chuoi UTF-8 xuong toi da max_bytes byte, KHONG cat giua mot ky tu nhieu byte
// (ten tieng Viet co dau chiem 2-3 byte/ky tu). Tra ve so byte duoc giu lai.
static inline size_t utf8_truncate_len(const char *s, size_t max_bytes)
{
    size_t n = strlen(s);
    if (n <= max_bytes) {
        return n;
    }
    size_t cut = max_bytes;
    // byte ngay sau vung giu lai la byte tiep noi (10xxxxxx) -> vung giu lai dang cat giua ky tu -> lui ve
    while (cut > 0 && (((unsigned char)s[cut]) & 0xC0) == 0x80) {
        cut--;
    }
    return cut;
}

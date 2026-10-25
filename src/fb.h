// Tiny software rasterizer over Screen buffers: tiles, sprites, rects, text.
#pragma once
#include <string.h>
#include "platform.h"
#include "../assets/tiles_data.h"
#include "../assets/sprites_data.h"
#include "../assets/font6x13.h"
#include "../assets/font7x14b.h"

namespace fb {

inline void clear(Screen &s, uint8_t r, uint8_t g, uint8_t b) {
    for (int i = 0; i < s.w * s.h; ++i) {
        s.rgb[i * 3] = r; s.rgb[i * 3 + 1] = g; s.rgb[i * 3 + 2] = b;
    }
}

inline void fillRect(Screen &s, int x0, int y0, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    if (x0 < 0) { w += x0; x0 = 0; }
    if (y0 < 0) { h += y0; y0 = 0; }
    if (x0 + w > s.w) w = s.w - x0;
    if (y0 + h > s.h) h = s.h - y0;
    for (int y = y0; y < y0 + h; ++y) {
        uint8_t *p = s.px(x0, y);
        for (int x = 0; x < w; ++x) { p[0] = r; p[1] = g; p[2] = b; p += 3; }
    }
}

inline void rectOutline(Screen &s, int x, int y, int w, int h, uint8_t r, uint8_t g, uint8_t b) {
    fillRect(s, x, y, w, 1, r, g, b);
    fillRect(s, x, y + h - 1, w, 1, r, g, b);
    fillRect(s, x, y, 1, h, r, g, b);
    fillRect(s, x + w - 1, y, 1, h, r, g, b);
}

// Blit a 16x16 palettized tile, clipped. `dim` darkens (for unpowered/overlay).
inline void tile(Screen &s, int sx, int sy, int t, bool dim = false) {
    if (t < 0 || t >= TILE_COUNT) t = 0;
    const unsigned char *src = mp_tiles + t * 256;
    for (int y = 0; y < 16; ++y) {
        const int yy = sy + y;
        if (yy < 0 || yy >= s.h) continue;
        for (int x = 0; x < 16; ++x) {
            const int xx = sx + x;
            if (xx < 0 || xx >= s.w) continue;
            const unsigned char *c = mp_palette[src[y * 16 + x]];
            uint8_t *p = s.px(xx, yy);
            if (dim) { p[0] = c[0] >> 1; p[1] = c[1] >> 1; p[2] = c[2] >> 1; }
            else { p[0] = c[0]; p[1] = c[1]; p[2] = c[2]; }
        }
    }
}

// Same tile scaled 2x (zoom mode).
inline void tile2x(Screen &s, int sx, int sy, int t) {
    if (t < 0 || t >= TILE_COUNT) t = 0;
    const unsigned char *src = mp_tiles + t * 256;
    for (int y = 0; y < 32; ++y) {
        const int yy = sy + y;
        if (yy < 0 || yy >= s.h) continue;
        const unsigned char *row = src + (y >> 1) * 16;
        for (int x = 0; x < 32; ++x) {
            const int xx = sx + x;
            if (xx < 0 || xx >= s.w) continue;
            const unsigned char *c = mp_palette[row[x >> 1]];
            uint8_t *p = s.px(xx, yy);
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
        }
    }
}

inline void indexedImage(Screen &s, int sx, int sy, const unsigned char *src, int w, int h, int scale = 1) {
    for (int y = 0; y < h * scale; ++y) {
        const int yy = sy + y;
        if (yy < 0 || yy >= s.h) continue;
        for (int x = 0; x < w * scale; ++x) {
            const int xx = sx + x;
            if (xx < 0 || xx >= s.w) continue;
            const unsigned char i = src[(y / scale) * w + (x / scale)];
            if (i == PAL_TRANSPARENT) continue;
            const unsigned char *c = mp_palette[i];
            uint8_t *p = s.px(xx, yy);
            p[0] = c[0]; p[1] = c[1]; p[2] = c[2];
        }
    }
}

inline void sprite(Screen &s, int sx, int sy, int type, int frame, int scale = 1) {
    if (type < 1 || type > 8 || frame < 1 || frame > mp_sprite_nframes[type]) return;
    const SpriteFrame &f = mp_sprite_frames[type][frame - 1];
    if (!f.px) return;
    indexedImage(s, sx, sy, f.px, f.w, f.h, scale);
}

// ---- text: 6x12 regular and 7x13 bold bitmap fonts (hinted, 1-bit) ----
struct Font { int w, h, n; const uint32_t *cp; const uint16_t *bits; };
static const Font FONT_REG  = { FONT_W,  FONT_H,  FONT_NGLYPHS,  font_cp,  &font_bits[0][0]  };
static const Font FONT_BOLD = { FONTB_W, FONTB_H, FONTB_NGLYPHS, fontb_cp, &fontb_bits[0][0] };

inline int glyphIndex(const Font &f, uint32_t c) {
    int lo = 0, hi = f.n - 1;
    while (lo <= hi) {
        const int mid = (lo + hi) / 2;
        if (f.cp[mid] == c) return mid;
        if (f.cp[mid] < c) lo = mid + 1; else hi = mid - 1;
    }
    return c == '?' ? 0 : glyphIndex(f, '?');
}

inline void glyph(Screen &s, int sx, int sy, uint32_t c, uint8_t r, uint8_t g, uint8_t b, int scale = 1,
                  const Font &f = FONT_REG) {
    const uint16_t *bits = f.bits + glyphIndex(f, c) * f.h;
    for (int y = 0; y < f.h; ++y)
        for (int x = 0; x < f.w; ++x)
            if (bits[y] & (1 << (f.w - 1 - x)))
                fillRect(s, sx + x * scale, sy + y * scale, scale, scale, r, g, b);
}

// Draws UTF-8 text (Latin-1 range + a few box/arrow glyphs). Returns the x after the last glyph.
inline int text(Screen &s, int x, int y, const char *str, uint8_t r, uint8_t g, uint8_t b, int scale = 1,
                const Font &f = FONT_REG) {
    const unsigned char *p = (const unsigned char *)str;
    while (*p) {
        uint32_t c = *p++;
        if (c >= 0xC0 && c < 0xE0 && *p) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        else if (c >= 0xE0 && c < 0xF0 && p[0] && p[1]) { c = ((c & 0x0F) << 12) | ((p[0] & 0x3F) << 6) | (p[1] & 0x3F); p += 2; }
        glyph(s, x, y, c, r, g, b, scale, f);
        x += f.w * scale;
    }
    return x;
}

inline int textB(Screen &s, int x, int y, const char *str, uint8_t r, uint8_t g, uint8_t b, int scale = 1) {
    return text(s, x, y, str, r, g, b, scale, FONT_BOLD);
}

inline int textWidth(const char *str, const Font &f = FONT_REG) {
    int n = 0;
    for (const unsigned char *p = (const unsigned char *)str; *p; ++p) if ((*p & 0xC0) != 0x80) ++n;
    return n * f.w;
}

// Word-wrapped text inside a width; returns the number of lines drawn (max maxLines).
inline int textWrap(Screen &s, int x, int y, int w, int lineH, int maxLines, const char *str,
                    uint8_t r, uint8_t g, uint8_t b, const Font &f = FONT_REG) {
    const int cols = w / f.w;
    int lines = 0;
    const char *p = str;
    while (*p && lines < maxLines) {
        while (*p == ' ') ++p;
        if (!*p) break;
        int len = (int)strlen(p);
        if (len > cols) {
            len = cols;
            while (len > 0 && p[len] != ' ' && p[len] != '\0') --len;
            if (len == 0) len = cols;
        }
        char buf[128];
        if (len > (int)sizeof(buf) - 1) len = sizeof(buf) - 1;
        memcpy(buf, p, len); buf[len] = 0;
        text(s, x, y + lines * lineH, buf, r, g, b, 1, f);
        p += len; ++lines;
    }
    return lines;
}

inline void textShadow(Screen &s, int x, int y, const char *str, uint8_t r, uint8_t g, uint8_t b, int scale = 1) {
    text(s, x + 1, y + 1, str, 0, 0, 0, scale);
    text(s, x, y, str, r, g, b, scale);
}

} // namespace fb

// Sprite-aware mixed ASCII + GBK glyph renderer for the CJK build variant.
//
// M5Display ships writeHzk()/writeHzkAsc()/writeHzkGbk(), but those only work
// when drawing to M5.Lcd — they touch protected TFT_eSPI state that the
// TFT_eSprite subclass can't share. We need to compose HUD text into the
// same sprite the GIF character renders into, so this module re-implements
// the glyph-blit path against the public sprite API (drawPixel / fillRect).
//
// Font sources:
//   - ASCII bytes (< 0x80)                   → ASC16 (8x16, bundled with M5StickCPlus)
//   - GBK pairs   (b1 in 0xA1-0xFE, b2 too)  → GB2312_L1.h (zones 16-55, 3760 glyphs)
//
// Only compiled when `CC_BUDDY_CJK_DISPLAY` is defined (fork-only build).

#pragma once

#ifdef CC_BUDDY_CJK_DISPLAY

#include <Arduino.h>

class TFT_eSprite;

// Draw a NUL-terminated byte string starting at (x, y) onto `spr`.
// Walks bytes: ASCII (1 byte) renders 8x16; GBK pair (2 bytes both >= 0xA1)
// renders 16x16. Pixels outside the sprite are silently clipped.
void cjkDrawMixed(TFT_eSprite* spr, const char* text, int x, int y,
                  uint16_t color, uint16_t bgcolor);

// Pixel width if `cjkDrawMixed` were called with this text. Useful for
// horizontal centering / overflow checks.
int  cjkMeasureMixed(const char* text);

// Maximum NUL-terminated bytes per wrapped output row. Tight upper bound:
// 16 ASCII chars × 1 byte = 16, plus null = 17; for GBK at 8 chars × 2
// bytes = 16; pad to 24 for slack against odd input.
#define CJK_ROW_CAP 24

// Pixel- and GBK-aware wrap. Splits `in` into rows of ≤ `maxPx` pixels each,
// never breaking a GBK 2-byte pair. Each row in `out` is NUL-terminated.
// Returns the number of rows actually written. ASCII chars count 8 px wide,
// GBK pairs 16 px wide. Hard-break: no word-aware logic — breaks at the
// exact glyph that wouldn't fit.
uint8_t cjkWrapInto(const char* in, char out[][CJK_ROW_CAP],
                    uint8_t maxRows, int maxPx);

#endif

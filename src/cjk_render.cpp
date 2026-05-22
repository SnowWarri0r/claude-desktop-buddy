#ifdef CC_BUDDY_CJK_DISPLAY

#include "cjk_render.h"
// TFT_eSprite is exposed via M5StickCPlus.h (which pulls in the bundled
// TFT_eSPI library); we don't have a direct lib_deps entry for TFT_eSPI.
#include <M5StickCPlus.h>
#include "Fonts/ASC12.h"        // 6x12 ASCII   (Fusion Pixel Font, OFL)
#include "Fonts/GB2312_L1.h"    // 12x12 GB2312 (Fusion Pixel Font, OFL)

// Width/height per glyph family. ASCII chars are half a CJK slot wide; both
// share the 12 px row height so the cursor math stays trivial and the HUD
// area shrinks back to ~52 px (vs 68 px for the previous 16 px variant).
static const int ASC_W = 6;
static const int GBK_W = 12;
static const int GLYPH_H = 12;

// True iff the two bytes starting at `p` form a valid GBK pair (both in
// 0xA1..0xFE). GB2312 Level 1 hanzi all live in that range; symbols and
// pinyin in zones 1-9 too, but we ship only zones 16-55 so anything else
// falls through to the '?' replacement below.
static inline bool isGbkLead(const uint8_t* p) {
  const uint8_t b1 = p[0], b2 = p[1];
  return b1 >= 0xA1 && b1 <= 0xFE && b2 >= 0xA1 && b2 <= 0xFE;
}

// Blit a bitmap glyph onto the sprite. `glyph` is row-major: rows of
// `bytes_per_row` bytes each, MSB-first within each byte. Bits outside the
// glyph's visible width are ignored. Pixels outside the sprite are clipped
// silently by TFT_eSprite::drawPixel.
static void blitGlyph(TFT_eSprite* spr, const uint8_t* glyph,
                      int x, int y,
                      int w_px, int h_px, int bytes_per_row,
                      uint16_t color, uint16_t bgcolor) {
  const bool fillBg = (color != bgcolor);
  for (int row = 0; row < h_px; row++) {
    for (int b = 0; b < bytes_per_row; b++) {
      const uint8_t bits = pgm_read_byte(glyph + row * bytes_per_row + b);
      uint8_t mask = 0x80;
      for (int bit = 0; bit < 8; bit++) {
        const int col = b * 8 + bit;
        if (col >= w_px) break;
        const bool on = (bits & mask) != 0;
        if (on) {
          spr->drawPixel(x + col, y + row, color);
        } else if (fillBg) {
          spr->drawPixel(x + col, y + row, bgcolor);
        }
        mask >>= 1;
      }
    }
  }
}

static void drawAsc12(TFT_eSprite* spr, uint8_t c, int x, int y,
                      uint16_t color, uint16_t bgcolor) {
  // ASC12 indexes 0..127 (slots 0..0x1F are zero-filled to keep
  // unprintable codepoints rendering blank). Each glyph: 12 rows × 1 byte.
  if (c >= ASC12_GLYPH_COUNT) return;
  const uint8_t* glyph = ASC12 + (uint32_t)c * ASC12_BYTES_PER_GLYPH;
  blitGlyph(spr, glyph, x, y, ASC_W, GLYPH_H,
            ASC12_BYTES_PER_ROW, color, bgcolor);
}

static void drawGb2312(TFT_eSprite* spr, uint8_t b1, uint8_t b2, int x, int y,
                       uint16_t color, uint16_t bgcolor) {
  const uint8_t* glyph = gb2312_l1_glyph(b1, b2);
  if (glyph == nullptr) {
    // Outside our zone range (Level 2 hanzi, UDA, or invalid pair) — fall
    // back to two '?' chars to flag the gap without leaving an empty slot.
    drawAsc12(spr, '?', x,         y, color, bgcolor);
    drawAsc12(spr, '?', x + ASC_W, y, color, bgcolor);
    return;
  }
  blitGlyph(spr, glyph, x, y, GBK_W, GLYPH_H,
            GB2312_L1_BYTES_PER_ROW, color, bgcolor);
}

void cjkDrawMixed(TFT_eSprite* spr, const char* text, int x, int y,
                  uint16_t color, uint16_t bgcolor) {
  if (spr == nullptr || text == nullptr) return;
  // Stop rendering once the next glyph wouldn't fit horizontally. Callers
  // that want multi-row layout should pre-wrap via cjkWrapInto.
  const int max_right = spr->width();
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
  int cx = x;
  while (*p) {
    int glyph_w;
    int advance;
    bool is_gbk = isGbkLead(p);
    if (is_gbk) {
      glyph_w = GBK_W;
      advance = 2;
    } else if (*p == '\n' || *p == '\r') {
      p++;
      continue;
    } else {
      glyph_w = ASC_W;
      advance = 1;
    }
    if (cx + glyph_w > max_right) break;
    if (is_gbk) drawGb2312(spr, p[0], p[1], cx, y, color, bgcolor);
    else        drawAsc12(spr, *p, cx, y, color, bgcolor);
    cx += glyph_w;
    p  += advance;
  }
}


uint8_t cjkWrapInto(const char* in, char out[][CJK_ROW_CAP],
                    uint8_t maxRows, int maxPx) {
  if (in == nullptr || maxRows == 0) return 0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(in);
  uint8_t row = 0;
  uint16_t col_bytes = 0;
  int cx = 0;
  while (*p && row < maxRows) {
    int glyph_w;
    int advance;
    if (isGbkLead(p)) {
      glyph_w = GBK_W;
      advance = 2;
    } else if (*p == '\n' || *p == '\r') {
      // Newline forces a wrap *now*, then skip the literal byte.
      out[row][col_bytes] = 0;
      row++;
      if (row >= maxRows) return row;
      col_bytes = 0;
      cx = 0;
      p++;
      continue;
    } else {
      glyph_w = ASC_W;
      advance = 1;
    }
    // Two break conditions: pixel overflow, or output-buffer overflow. Both
    // finalise the current row without consuming the current input glyph
    // — it goes onto the next row instead.
    if (cx + glyph_w > maxPx ||
        (uint16_t)(col_bytes + advance) >= (uint16_t)(CJK_ROW_CAP - 1)) {
      out[row][col_bytes] = 0;
      row++;
      if (row >= maxRows) return row;
      col_bytes = 0;
      cx = 0;
      // Edge case: single glyph wider than the entire usable width. Drop
      // it to avoid an infinite loop. Practically impossible (16 ≤ maxPx
      // for any realistic HUD area) but keep the guard.
      if (cx + glyph_w > maxPx) {
        p += advance;
        continue;
      }
    }
    for (int i = 0; i < advance; i++) {
      out[row][col_bytes++] = (char)p[i];
    }
    cx += glyph_w;
    p += advance;
  }
  if (col_bytes > 0 && row < maxRows) {
    out[row][col_bytes] = 0;
    row++;
  }
  return row;
}

int cjkMeasureMixed(const char* text) {
  if (text == nullptr) return 0;
  const uint8_t* p = reinterpret_cast<const uint8_t*>(text);
  int w = 0;
  while (*p) {
    if (isGbkLead(p)) { w += GBK_W; p += 2; }
    else if (*p == '\n' || *p == '\r') { p++; }
    else { w += ASC_W; p += 1; }
  }
  return w;
}

#endif // CC_BUDDY_CJK_DISPLAY

#pragma once

/**
 * Misaki Gothic 8x8 Japanese bitmap font.
 * Covers ASCII, hiragana, katakana, ~1000 kanji (1710 glyphs total).
 * Each glyph is 7 bytes (8th row is always blank).
 */

#include <stdint.h>

#define UI_FONT_JP_W  8
#define UI_FONT_JP_H  8    /* Native size; rendered at 2x (16x16) */
#define UI_FONT_JP_BYTES_PER_GLYPH  7

/**
 * Get 7-byte glyph data for a Unicode codepoint.
 * Returns NULL if character not found (after fallback to U+25A1).
 * Caller must treat as 7 bytes + implicit zero 8th row.
 */
const uint8_t *ui_font_jp_glyph(uint32_t codepoint);

/**
 * Get total number of glyphs in the font.
 */
int ui_font_jp_glyph_count(void);

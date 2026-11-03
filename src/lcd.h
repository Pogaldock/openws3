/* ST7735S 80x160 panel, used landscape 160x80, RGB565, bit-banged SPI.  No framebuffer (RAM is 8 KB). */
#ifndef LCD_H
#define LCD_H
#include "compat.h"

#define LCD_PX_W 160
#define LCD_PX_H 80

void lcd_init(void);
void lcd_wake(void);              /* sleep-out; the panel wants 5 ms before lcd_reconfig */
void lcd_reconfig(void);          /* re-send the panel registers (not the picture); see lcd.c */
void lcd_fill(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t color);
/* sx, sy: integer stretch of the 6x8 cell.  Characters outside the font draw as space. */
void lcd_text(uint8_t x, uint8_t y, const char *s, uint8_t sx, uint8_t sy, uint16_t fg, uint16_t bg);

/* ---- ROT theme drawing (reentrant: their temporaries go on the stack, the direct RAM is full) ---- */
/* Poked text: with the ROT theme a glyph stretched in both directions (2x2) is inked as a checkerboard of dots, like hand-poke
 * shading.  r = row inside the stretch (0..sy-1), j = column inside it (0..sx-1).  Text 1 pixel wide stays solid (it would break up). */
#define LCD_POKE(r, j) ((((r) + (j)) & 1) == 0)
/* Static: one step of a 16-bit Galois LFSR, and the grey of a noise pixel from its state (mostly black, some dark, a few bright) */
#define LCD_LFSR(s)   ((uint16_t)(((s) >> 1) ^ (((s) & 1) ? 0xB400u : 0u)))
#define LCD_STATIC(s) (((s) & 7) < 5 ? 0x0000 : ((s) & 7) == 5 ? 0x2945 : ((s) & 7) == 6 ? 0x6B4D : 0xD69A)
void lcd_noise(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t seed) REENT;
/* One full-width row of static at y.  If bits is not 0 it is one row of a 1-bit image, bw pixels wide, placed at bx: each of its lit
 * pixels shows in fg with a chance of keep/256 (0: never, 255: almost always), so the image surfaces through the static. */
void lcd_noise_row(uint8_t y, uint16_t seed, const CODE uint8_t *bits, uint8_t bx, uint8_t bw, uint8_t keep, uint16_t fg) REENT;
#define LCD_SURFACE(bits, i, s, keep) (((bits)[(i) >> 3] & (0x80 >> ((i) & 7))) && (uint8_t)((s) >> 8) < (keep))
/* 1 bit per pixel, rows top to bottom padded to whole bytes, MSB = leftmost (tools/gen_sprites.py, tools/gen_logo.py) */
void lcd_bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const CODE uint8_t *bits, uint16_t fg, uint16_t bg) REENT;
/* ROT: a sprite up to 7 pixels wide (one byte a row) drawn at twice its size, its top-left at (ox, oy) inside the w x h area at x, y
 * (ox, oy wrap: an area that starts below the sprite's top passes 0 - rows); the rest of the area in bg, or static when seed is not 0 */
void lcd_sprite2(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const CODE uint8_t *bits, uint8_t ox, uint8_t oy, uint16_t fg, uint16_t bg, uint16_t seed) REENT;

#endif

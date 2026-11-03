#include "theme.h"
#include "layout.h"

static const CODE uint16_t PAL[R_COUNT] = {
    /* ROT: SYMBIOZE.  White numbers, neon green eyes, red only for trouble, a full cell dark green.  The UI also draws this theme
     * differently (ui.c, lcd.c): a barbed-wire header with big icons, port numbers coloured by state (dark green empty, green with a
     * cell, red on a fault), big eyes, an empty port as static with a sleeping eye, small text, and a static splash. */
    0x0000, 0x10A2, 0xFFFF, 0xFFFF, 0xFFFF, 0x5AEB, 0x0000, 0xC618, 0xFFFF, 0xF800, 0xA514, 0x09C1, 0xFFFF, 0xFFFF,
    0x3FE2, 0x3FE2, 0x3FE2, 0x3FE2, 0x3FE2, 0x3FE2,                    /* port badges (ui.c colours them by state) */
    0x3FE2, 0x3FE2, 0x3FE2, 0x3FE2, 0x3FE2, 0x9CD3, 0xF800             /* the eyes, by PS_* */
};

uint16_t th(uint8_t role) { return PAL[role]; }

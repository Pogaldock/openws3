/*
 * Colour themes.  A theme only changes colours, never behaviour: every colour the UI uses is a role, and a theme is one table of
 * those roles.  Pure data, no hardware.  The role colours that carry meaning (run, good, bad, warn, info) keep their meaning in
 * every theme; the others (text, values, badges) are decoration.
 */
#ifndef THEME_H
#define THEME_H
#include "compat.h"

/* WS3 Unlocked has one look, ROT: OpenWS3's CLASSIC, CALM, AMBER and HIGH CONTRAST were removed to make room in flash.  th_get() is a
 * constant, so the code that drew the other looks is left out by the compiler. */
enum { TH_ROT = 1 };

/* roles: the first 14 are the base palette, then the six port badges and the eight per-state dots (index = PS_*) */
enum { R_BG = 0, R_BOX, R_TEXT, R_VIN, R_VOLT, R_DIM, R_RUN, R_WARN, R_GOOD, R_BAD, R_INFO, R_FULL, R_FULLTX, R_WAIT, R_BADGE0, R_DOT0 = R_BADGE0 + 6, R_COUNT = R_DOT0 + 7 };

#define  th_get() TH_ROT
uint16_t th(uint8_t role);               /* RGB565 of a role */

#endif

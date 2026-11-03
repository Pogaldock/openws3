/*
 * Buzzer on P4.6: a passive piezo, driven at the frequency of each note by Timer 3 (hal_tone_note), in melodies.
 * The melody engine is pure; buzz_tick() runs from the 0.5 ms interrupt and tells the interrupt when the note changes.
 * WS3 Unlocked: OpenWS3 drove the pin at 1 kHz only (its 0.5 ms tick toggling it); notes need a timer of their own.
 */
#ifndef BUZZ_H
#define BUZZ_H
#include "compat.h"

enum { PAT_NONE = 0, PAT_BOOT, PAT_KEY, PAT_DONE, PAT_ERROR, PAT_RAIL_ALARM, PAT_FINALE, PAT_STORE, PAT_HEART, PAT_COUNT };

#define TONE_KEEP 0                         /* buzz_tick: nothing changes */
#define TONE_OFF  1                         /* silence; 2 + n = note n (buzz_note_count) */

void    buzz_reset(void);
void    buzz_enable(uint8_t on);            /* user setting: when off, notes are silent (the melody still runs) */
void    buzz_bank(uint8_t rot);             /* 1: the ROT sound pack, 0: the plain beeps */
void    buzz_play(uint8_t pattern);         /* PAT_NONE stops */
uint8_t buzz_busy(void);
uint8_t buzz_tick(void);                    /* 0.5 ms tick: TONE_KEEP, TONE_OFF or 2 + a note index */
uint16_t buzz_note_count(uint8_t note);     /* Timer 3 counts for half a period of a note (12T clock at 40 MHz), for the hal */

#endif

#include "buzz.h"

/* Notes C5 .. C8: Timer 3 counts for half a period, 12T clock at 40 MHz (3.333 MHz): n = 1666667 / f.  A piezo is loudest around
   2 to 4 kHz, so the melodies sit mostly in octaves 6 and 7. */
#define N_C5 1   /* 523 Hz */
#define N_CS5 2   /* 554 Hz */
#define N_D5 3   /* 587 Hz */
#define N_DS5 4   /* 622 Hz */
#define N_E5 5   /* 659 Hz */
#define N_F5 6   /* 698 Hz */
#define N_FS5 7   /* 740 Hz */
#define N_G5 8   /* 784 Hz */
#define N_GS5 9   /* 831 Hz */
#define N_A5 10   /* 880 Hz */
#define N_AS5 11   /* 932 Hz */
#define N_B5 12   /* 988 Hz */
#define N_C6 13   /* 1047 Hz */
#define N_CS6 14   /* 1109 Hz */
#define N_D6 15   /* 1175 Hz */
#define N_DS6 16   /* 1245 Hz */
#define N_E6 17   /* 1319 Hz */
#define N_F6 18   /* 1397 Hz */
#define N_FS6 19   /* 1480 Hz */
#define N_G6 20   /* 1568 Hz */
#define N_GS6 21   /* 1661 Hz */
#define N_A6 22   /* 1760 Hz */
#define N_AS6 23   /* 1865 Hz */
#define N_B6 24   /* 1976 Hz */
#define N_C7 25   /* 2093 Hz */
#define N_CS7 26   /* 2217 Hz */
#define N_D7 27   /* 2349 Hz */
#define N_DS7 28   /* 2489 Hz */
#define N_E7 29   /* 2637 Hz */
#define N_F7 30   /* 2794 Hz */
#define N_FS7 31   /* 2960 Hz */
#define N_G7 32   /* 3136 Hz */
#define N_GS7 33   /* 3322 Hz */
#define N_A7 34   /* 3520 Hz */
#define N_AS7 35   /* 3729 Hz */
#define N_B7 36   /* 3951 Hz */
#define N_C8 37   /* 4186 Hz */
static const CODE uint16_t NOTE[] = { 3185, 3006, 2838, 2678, 2528, 2386, 2252, 2126, 2007, 1894, 1788, 1687, 1593, 1503, 1419, 1339, 1264, 1193, 1126, 1063, 1003, 947, 894, 844, 796, 752, 709, 670, 632, 597, 563, 531, 502, 473, 447, 422, 398 };

/* A melody: pairs (note, duration in 10 ms), note 0 = a rest; END stops it, LOOP starts it again (the rail alarm). */
#define END  0xFF
#define LOOP 0xFE
#define CRK  0xFD          /* crackle for the duration: each 10 ms, now and then a random high note (soft static, like an old record) */
#define R    0

/* ROT: one E minor phrase, plucked (a short note and a gap), with soft static around it.  Every sound is a piece of the phrase. */
static const CODE uint8_t r_boot[]   = { CRK, 30, N_E6, 12, R, 5, N_B6, 12, R, 5, N_A6, 12, R, 5, N_G6, 12, R, 5, N_FS6, 8, R, 4, N_G6, 8, R, 4, N_E6, 40, CRK, 25, END };
static const CODE uint8_t r_key[]    = { N_B6, 2, END };
static const CODE uint8_t r_done[]   = { N_G6, 9, R, 4, N_B6, 9, R, 4, N_E7, 24, CRK, 12, END };
static const CODE uint8_t r_error[]  = { CRK, 15, N_G6, 12, R, 6, N_DS6, 34, END };
static const CODE uint8_t r_rail[]   = { N_A7, 6, R, 6, N_A7, 6, R, 6, N_A7, 6, R, 18, LOOP };
static const CODE uint8_t r_finale[] = { CRK, 30, N_E6, 12, R, 5, N_B6, 12, R, 5, N_A6, 12, R, 5, N_G6, 12, R, 5, N_FS6, 12, R, 5, N_G6, 12, R, 5,
                                         N_E6, 20, R, 8, N_E6, 12, R, 5, N_B6, 12, R, 5, N_A6, 12, R, 5, N_B6, 12, R, 5, N_D7, 12, R, 5,
                                         N_E7, 60, CRK, 40, END };
static const CODE uint8_t r_store[]  = { N_B6, 12, R, 7, N_A6, 12, R, 7, N_G6, 12, R, 7, N_E6, 36, CRK, 30, END };
static const CODE uint8_t r_heart[]  = { N_E5, 5, R, 12, N_E5, 6, END };

static const CODE uint8_t *const CODE BANK[PAT_COUNT] = { 0, r_boot, r_key, r_done, r_error, r_rail, r_finale, r_store, r_heart };

static const CODE uint8_t *pos, *start;
/* active: a melody runs; written last by buzz_play (one byte, atomic), so the 0.5 ms interrupt never reads a half-set melody.  SDCC 4.6
   folds a test of a __code pointer for null to true, which is why the state is not "pos != 0" (found by the 8051 tests). */
static uint8_t  active, sounding, enabled, crk, rnd;
static uint16_t left;

void buzz_reset(void) { active = 0; pos = start = 0; left = 0; sounding = 0; enabled = 1; crk = 0; }
void buzz_enable(uint8_t on) { enabled = on ? 1 : 0; }
uint8_t buzz_busy(void) { return active; }
uint16_t buzz_note_count(uint8_t note) { return NOTE[note - 1]; }

void buzz_play(uint8_t id)
{
    active = 0;
    if (id == PAT_NONE || id >= PAT_COUNT) return;
    start = pos = BANK[id];
    left = 0; crk = 0;
    active = 1;
}

uint8_t buzz_tick(void)
{
    uint8_t n;
    if (!active) { if (sounding) { sounding = 0; return TONE_OFF; } return TONE_KEEP; }
    if (left) { left--; return TONE_KEEP; }
    if (crk) {                                                /* crackle: one 10 ms step, a random high note about one step in five */
        crk--; left = 19;
        rnd = (uint8_t)(rnd * 109u + 89u);
        if (rnd < 51 && enabled) { sounding = 1; return (uint8_t)(23 + (rnd & 15)); }   /* notes 22..37 (A6..C8), returned as note + 1 */
        sounding = 0; return TONE_OFF;
    }
    n = pos[0];
    if (n == LOOP) { pos = start; n = pos[0]; }
    if (n == END) { active = 0; sounding = 0; return TONE_OFF; }
    if (n == CRK) { crk = pos[1]; pos += 2; sounding = 0; return TONE_OFF; }   /* the crackle starts on the next tick (0.5 ms) */
    left = (uint16_t)pos[1] * 20u - 1u;                       /* 10 ms = 20 ticks, this one included */
    pos += 2;
    if (n == R || !enabled) { sounding = 0; return TONE_OFF; }
    sounding = 1;
    return (uint8_t)(n + 1);
}

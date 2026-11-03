/* Randomised, seeded fuzzer for the whole-firmware simulator (test/sim).  The real firmware (app.c and everything it calls) runs
 * against the board model in sim_hal.c, exactly as in sim_app.c, but driven by a random event list instead of a scenario: buttons
 * (taps, holds, chords, menu and port-settings sequences), USB host commands (valid and malformed), cells put in, pulled and swapped
 * (voltage, capacity, resistance, leakage), supply changes (USB-C level and PD steps, XT60 packs, VCC steps), rail faults, port
 * shorts, the thermistor, slow display passes and power cycles, over simulated hours.  After every control pass a set of
 * invariants taken from SAFETY.md / README.md / GUIDE.md is checked (see INVARIANTS below).
 *
 *   build/sim_fuzz [-n N] [-s SEED] [-j JOBS] [-H HOURS] [-q] [--no-shrink]   run seeds SEED .. SEED+N-1 (each in its own process)
 *   build/sim_fuzz -1 SEED [-v]                                                 one seed, in this process, verbose log
 *   build/sim_fuzz -r FILE [-v]                                                 replay an event list (the format printed on failure)
 *   build/sim_fuzz -m SEED                                                      shrink a failing seed to a short event list
 *
 * A run is reproducible from its seed alone (the generator and the model's ADC noise are both seeded).  On a violation the seed, the
 * invariant, the state and a replayable event list are printed, the list is shrunk (delta debugging, each trial in a fresh process),
 * and the exit status is nonzero.  Exit status of a single run: 0 ok, 64 + invariant number on a violation. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include "sim.h"
#include "adc.h"
#include "app.h"
#include "cfg.h"
#include "usb.h"
#include "chg.h"
#include "sup.h"
#include "cal.h"
#include "stats.h"
#include "tele.h"

#define SEC(s) ((uint64_t)((s) * 2000))
#define TOS(t) ((double)(t) / 2000.0)

/* ---------------------------------------------------------------------------------------------------------------------- */
/* random numbers: xorshift64*, seeded; the model's ADC noise uses rand(), seeded from the same seed */
static uint64_t rs;
static uint64_t rnd(void) { rs ^= rs >> 12; rs ^= rs << 25; rs ^= rs >> 27; return rs * 2685821657736338717ULL; }
static double U(double a, double b) { return a + (b - a) * ((rnd() >> 11) * (1.0 / 9007199254740992.0)); }
static int RI(int a, int b) { return a + (int)(rnd() % (uint64_t)(b - a + 1)); }
static int P(double p) { return U(0, 1) < p; }

/* ---------------------------------------------------------------------------------------------------------------------- */
/* events */
enum { E_WAIT, E_BTN, E_USB, E_ATTACH, E_DETACH, E_VIN, E_XT60, E_VCC, E_RAIL, E_SHORT, E_THERM, E_LCD, E_NOISE, E_POWER, E_CFG, NE };
static const char *const EN[NE] = { "WAIT", "BTN", "USB", "ATTACH", "DETACH", "VIN", "XT60", "VCC", "RAIL", "SHORT", "THERM", "LCD", "NOISE", "POWER", "CFG" };
typedef struct { uint8_t t, a, n; double x[4]; uint8_t b[24]; } ev_t;

#define MAXEV 20000
static ev_t EVS[MAXEV];
static int nev;

static void ev_print(FILE *f, const ev_t *e)
{
    int k;
    fprintf(f, "%s", EN[e->t]);
    switch (e->t) {
    case E_WAIT: case E_VIN: case E_XT60: case E_VCC: case E_RAIL: case E_THERM: case E_LCD: case E_NOISE: case E_POWER:
        fprintf(f, " %.4f", e->x[0]); break;
    case E_BTN: fprintf(f, " %u %.3f", e->a, e->x[0]); break;
    case E_USB: case E_CFG: fprintf(f, " %u", e->n); for (k = 0; k < e->n; k++) fprintf(f, " %u", e->b[k]); break;
    case E_ATTACH: fprintf(f, " %u %.4f %.1f %.4f %.4f", e->a, e->x[0], e->x[1], e->x[2], e->x[3]); break;
    case E_DETACH: fprintf(f, " %u", e->a); break;
    case E_SHORT: fprintf(f, " %u %.3f", e->a, e->x[0]); break;
    }
    fprintf(f, "\n");
}

static int ev_parse(const char *line, ev_t *e)
{
    char name[16];
    int k, n, off, v;
    const char *p;
    memset(e, 0, sizeof *e);
    if (sscanf(line, "%15s%n", name, &off) != 1 || name[0] == '#') return 0;
    for (k = 0; k < NE; k++) if (!strcmp(name, EN[k])) break;
    if (k == NE) return 0;
    e->t = (uint8_t)k; p = line + off;
    switch (k) {
    case E_BTN: { unsigned a; sscanf(p, "%u %lf", &a, &e->x[0]); e->a = (uint8_t)a; break; }
    case E_USB: case E_CFG:
        if (sscanf(p, "%d%n", &n, &off) != 1) return 0;
        p += off; e->n = (uint8_t)(n > 24 ? 24 : n);
        for (k = 0; k < e->n; k++) { if (sscanf(p, "%d%n", &v, &off) != 1) return 0; p += off; e->b[k] = (uint8_t)v; }
        break;
    case E_ATTACH: { unsigned a; sscanf(p, "%u %lf %lf %lf %lf", &a, &e->x[0], &e->x[1], &e->x[2], &e->x[3]); e->a = (uint8_t)a; break; }
    case E_DETACH: { unsigned a; sscanf(p, "%u", &a); e->a = (uint8_t)a; break; }
    case E_SHORT: { unsigned a; sscanf(p, "%u %lf", &a, &e->x[0]); e->a = (uint8_t)a; break; }
    default: sscanf(p, "%lf", &e->x[0]); break;
    }
    return 1;
}

/* ---------------------------------------------------------------------------------------------------------------------- */
/* the generator */
static ev_t *add(int t) { ev_t *e; if (nev >= MAXEV) return &EVS[MAXEV - 1]; e = &EVS[nev++]; memset(e, 0, sizeof *e); e->t = (uint8_t)t; return e; }
static void g_wait(double s) { add(E_WAIT)->x[0] = s; }
static void g_btn(int m, double d) { ev_t *e = add(E_BTN); e->a = (uint8_t)m; e->x[0] = d; }
#define OK_()   g_btn(3, 0.12)
#define BACK_() g_btn(3, 1.0)
#define B1_()   g_btn(1, 0.12)
#define B2_()   g_btn(2, 0.12)
static void g_usb(int n, const uint8_t *b) { ev_t *e = add(E_USB); e->n = (uint8_t)n; memcpy(e->b, b, (size_t)n); }

static const uint8_t CURS[] = { 21, 28, 35, 42, 49, 56, 63, 70, 77, 84, 91 };

static void g_usb_valid(void)
{
    uint8_t b[4] = { 0, 0, (uint8_t)RI(0, 255), 0 };
    switch (RI(0, 9)) {
    case 0: case 1: b[0] = 1; break;                                         /* stop */
    case 2: case 3: b[0] = 2; break;                                         /* start */
    case 4: b[0] = 3; b[1] = CURS[RI(0, 10)]; break;                         /* current */
    case 5: case 6: b[0] = 4; b[1] = (uint8_t)RI(0, 3); break;               /* target voltage */
    case 7: b[0] = 5; break;                                                 /* ping */
    case 8: b[0] = 10; b[1] = (uint8_t)RI(0, 5); b[3] = (uint8_t)(P(0.3) ? RI(0, 1) : CURS[RI(0, 10)]); break;   /* port value */
    default: b[0] = 6; break;                                                /* reboot into the loader */
    }
    g_usb(4, b);
}

static void g_usb_bad(void)
{
    uint8_t b[24];
    int k, n = RI(1, 24);
    for (k = 0; k < n; k++) b[k] = (uint8_t)rnd();
    switch (RI(0, 4)) {
    case 0: b[0] = (uint8_t)RI(7, 9); break;                                 /* the removed developer mode */
    case 1: b[0] = 4; b[1] = (uint8_t)RI(4, 255); break;                     /* a target out of range */
    case 2: b[0] = 3; b[1] = (uint8_t)rnd(); break;                          /* any current byte */
    case 3: b[0] = 10; b[1] = (uint8_t)rnd(); b[3] = (uint8_t)rnd(); break;  /* any port / value */
    default: break;                                                          /* anything */
    }
    g_usb(n, b);
}

/* a menu visit: open, a category, an item, maybe an edit (confirmed or undone), close; sometimes a host command in the middle */
static void g_menu(void)
{
    static const int NIT[3] = { 3, 4, 2 };
    int c = RI(0, 3), k, n;
    OK_();
    for (k = 0; k < c; k++) B2_();
    if (P(0.1)) B1_();
    OK_();
    if (c == 3) {                                                            /* diagnostics: page through, back to the menu */
        n = RI(0, 8); for (k = 0; k < n; k++) g_btn(P(0.5) ? 1 : 2, 0.12);
        if (P(0.3)) g_wait(U(1, 30));
        BACK_(); BACK_(); return;
    }
    if (c == 3 || P(0.1)) { BACK_(); BACK_(); return; }
    n = RI(0, NIT[c] - 1);
    for (k = 0; k < n; k++) B2_();
    OK_();                                                                   /* edit */
    n = RI(0, 6);
    for (k = 0; k < n; k++) {
        g_btn(P(0.5) ? 1 : 2, P(0.15) ? U(0.5, 2.5) : 0.12);                 /* sometimes held: repeats */
        if (P(0.1)) g_usb_valid();                                           /* a host acting during the edit */
        if (P(0.05)) g_btn(1, 0.6);
    }
    if (P(0.1)) g_wait(U(0.5, 15));
    if (P(0.75)) OK_(); else BACK_();
    if (P(0.1)) g_usb_valid();
    BACK_();
    if (P(0.85)) BACK_();
}

/* the port settings mode: hold B2, select, edit, confirm or cancel, leave or let it time out */
static void g_ports(void)
{
    int k, n;
    g_btn(2, U(0.55, 1.2));
    n = RI(0, 6); for (k = 0; k < n; k++) g_btn(P(0.5) ? 1 : 2, 0.12);
    if (P(0.8)) {
        OK_();
        n = RI(0, 8); for (k = 0; k < n; k++) { g_btn(P(0.5) ? 1 : 2, P(0.15) ? U(0.5, 2) : 0.12); if (P(0.08)) g_usb_valid(); }
        if (P(0.7)) OK_(); else if (P(0.5)) BACK_(); else g_wait(U(10.5, 13));
    }
    if (P(0.15)) g_usb_valid();
    if (P(0.7)) BACK_(); else g_wait(U(10.5, 13));
}

static void g_attach(int p)
{
    ev_t *e = add(E_ATTACH);
    double u = U(0, 1);
    e->a = (uint8_t)p;
    e->x[0] = u < 0.62 ? U(3.3, 4.2) : u < 0.74 ? U(2.6, 3.3) : u < 0.80 ? U(2.0, 2.6) : u < 0.95 ? U(4.15, 4.38) : U(4.38, 4.50);
    u = U(0, 1);
    e->x[1] = u < 0.6 ? U(80, 450) : u < 0.85 ? U(450, 1500) : u < 0.95 ? U(1500, 3000) : 1e6;    /* 1e6: never fills */
    e->x[2] = U(0.03, 0.35);
    u = U(0, 1);
    e->x[3] = u < 0.85 ? 0 : u < 0.95 ? U(0.001, 0.01) : U(0.02, 0.15);
}

static double g_waitlen(void)
{
    double u = U(0, 1);
    return u < 0.6 ? U(0.2, 5) : u < 0.85 ? U(5, 120) : u < 0.97 ? U(120, 1200) : U(1200, 3600);
}

static const double USBC[] = { 5.0, 5.0, 5.15, 5.15, 9.0, 15.0, 19.5, 20.0 };
static const double PACKS[] = { 7.4, 8.4, 11.1, 12.6, 14.8, 16.8, 18.5, 21.0, 22.2, 25.2 };
static double cur_vin, cur_xt;

static void g_supply(void)
{
    double u = U(0, 1);
    if (u < 0.45) {                                                          /* USB-C: a level, a PD step, a sag, or unplugged */
        double v = P(0.7) ? USBC[RI(0, 7)] : U(3.8, 4.95);
        if (P(0.1) && cur_xt > 0) v = 0;
        add(E_VIN)->x[0] = cur_vin = v;
    } else {                                                                 /* XT60: a pack, a sagging pack, or unplugged */
        double v = P(0.4) ? 0 : P(0.85) ? PACKS[RI(0, 9)] : U(5.5, 25.2);
        if (v == 0 && cur_vin <= 0) v = PACKS[RI(0, 9)];
        add(E_XT60)->x[0] = cur_xt = v;
    }
}

static void generate(uint64_t seed, double hours_max)
{
    double t = 0, T, u;
    int k, present[NPORTS] = { 0 };
    rs = seed * 0x9E3779B97F4A7C15ULL + 0x1234567ULL; if (!rs) rs = 1;
    for (k = 0; k < 4; k++) rnd();
    nev = 0;
    u = U(0, 1);
    T = 3600 * (u < 0.5 ? U(0.3, 1.5) : u < 0.85 ? U(1.5, 3.5) : U(3.5, 6.0));
    if (T > hours_max * 3600) T = hours_max * 3600;

    if (P(0.7)) {                                                            /* the settings page as a user left it */
        ev_t *e = add(E_CFG);
        e->n = 16;
        e->b[0] = CURS[RI(0, 10)]; e->b[1] = (uint8_t)RI(0, 3); e->b[2] = (uint8_t)(P(0.7) ? 2 : RI(0, 1)); e->b[3] = (uint8_t)P(0.75);
        e->b[4] = (uint8_t)(P(0.7) ? 0 : RI(5, 40)); e->b[5] = (uint8_t)(P(0.3) ? 0 : P(0.6) ? 98 : RI(96, 230)); e->b[6] = (uint8_t)P(0.3);
        e->b[7] = (uint8_t)RI(0, 2);
        for (k = 0; k < NPORTS; k++) e->b[8 + k] = (uint8_t)(P(0.8) ? 0 : CURS[RI(0, 10)]);
        e->b[14] = (uint8_t)(P(0.8) ? 0 : RI(0, 63)); e->b[15] = (uint8_t)P(0.1);
    }
    if (P(0.5)) add(E_NOISE)->x[0] = U(0, 4);
    add(E_VCC)->x[0] = P(0.85) ? U(4.45, 4.95) : U(4.40, 4.45);
    cur_vin = P(0.8) ? USBC[RI(0, 7)] : 0;
    cur_xt = cur_vin > 0 && P(0.8) ? 0 : PACKS[RI(0, 9)];
    add(E_VIN)->x[0] = cur_vin; add(E_XT60)->x[0] = cur_xt;
    for (k = 0; k < NPORTS; k++) if (P(0.55)) { g_attach(k); present[k] = 1; }
    g_wait(U(0.5, 8));
    if (P(0.8)) { g_btn(1, 0.6); g_wait(U(1, 60)); }                         /* most runs start early */

    while (t < T && nev < MAXEV - 64) {
        double w = U(0, 100);
        int p = RI(0, NPORTS - 1);
        if      (w < 12) g_btn(1, U(0.4, 1.0));                              /* start / stop */
        else if (w < 20) g_menu();
        else if (w < 25) { int n = RI(1, 6); for (k = 0; k < n; k++) { static const double D[] = { 0.03, 0.12, 0.3, 0.45, 0.6, 0.9, 1.2, 2.5 }; g_btn(RI(1, 3), D[RI(0, 7)]); } }
        else if (w < 29) g_ports();
        else if (w < 33) g_btn(P(0.5) ? 2 : 1, 0.12);                        /* B2 target / B1 current on the main screen; views */
        else if (w < 39) g_usb_valid();
        else if (w < 42) g_usb_bad();
        else if (w < 52) {                                                   /* cells in, out, swapped */
            if (!present[p]) { g_attach(p); present[p] = 1; }
            else if (P(0.5)) { add(E_DETACH)->a = (uint8_t)p; present[p] = 0; }
            else { add(E_DETACH)->a = (uint8_t)p; g_wait(P(0.3) ? U(0.005, 0.05) : U(0.1, 3)); g_attach(p); }
        }
        else if (w < 58) g_supply();
        else if (w < 62) add(E_VCC)->x[0] = (u = U(0, 1)) < 0.8 ? U(4.45, 4.95) : u < 0.92 ? U(4.30, 4.45) : U(4.98, 5.2);
        else if (w < 65) {                                                   /* the board warms, overheats, a sensor breaks; then normal */
            static const double TH[] = { 2150, 1280, 1000, 900, 780, 600, 1100, 1200, 4095, 50 };
            add(E_THERM)->x[0] = TH[RI(0, 9)]; g_wait(U(1, 300)); add(E_THERM)->x[0] = 2150;
        }
        else if (w < 67) {                                                   /* the rail stops listening for a while */
            static const double RV[] = { 3.0, 4.0, 4.30, 4.35, 4.5, 4.7, 4.9 };
            add(E_RAIL)->x[0] = RV[RI(0, 6)]; g_wait(U(0.3, 20)); add(E_RAIL)->x[0] = 0;
        }
        else if (w < 69) { ev_t *e = add(E_SHORT); e->a = (uint8_t)p; e->x[0] = U(1, 4); g_wait(U(0.3, 4)); e = add(E_SHORT); e->a = (uint8_t)p; e->x[0] = 0; }
        else if (w < 71) add(E_LCD)->x[0] = P(0.5) ? 0 : P(0.8) ? U(0, 1.5) : U(1.5, 3);   /* a slower display bus: passes up to ~4x (a bigger factor is not a real panel) */
        else if (w < 72) { add(E_POWER)->x[0] = U(0.1, 30); }
        else if (w < 74) { g_btn(1, U(0.4, 1.0)); g_wait(U(60, 900)); }    /* stop/start then a stretch */
        /* else: just time */
        { double d = g_waitlen(); g_wait(d); t += d; }
    }
}

/* ---------------------------------------------------------------------------------------------------------------------- */
/* the harness: executes events against the simulated board, checks the invariants after every control pass */

/* INVARIANTS (number = exit status - 64) */
enum {
    I_NONE, I_WDT, I_OFF_STOPPED, I_OFF_HOLD, I_HV, I_SET_CHANGED, I_OVERCHARGE, I_PORT_OFF_DRIVEN, I_EMPTY_DRIVEN,
    I_REFUSED_FED, I_T_CHG, I_T_PRE, I_T_BLEED, I_FAULT_CLEARED, I_FATAL_CLEARED, I_START_ALONE, I_HOT, I_VCC_HIGH, I_VCC_LOW,
    I_VIN_LOW, I_RAIL, I_OVERCURRENT, I_BACKFEED, NINV
};
static const char *const IN_NAME[NINV] = {
    "none", "watchdog bit (main loop starved 2.5 s)", "output driven while stopped / fatal", "output driven during a rail pause",
    "run at 4.35 V with ALLOW 4.35V off", "target / current / port setting changed mid-run", "cell above its target + 30 mV",
    "a port set OFF driven during a run", "an empty port driven for more than 3 s", "over-discharged cell (< 2.5 V at rest) given current",
    "3 h charge limit exceeded", "30 min pre-charge limit exceeded", "5 h bleed limit exceeded", "port fault cleared mid-run without the cell removed",
    "fatal condition cleared without a power cycle", "run started without a button or host start", "driving while too hot / thermistor broken",
    "VCC over 5.0 V not fatal", "driving with VCC low", "driving with the input low", "driving with the rail stuck out of range",
    "over-current not cut", "USB-C fed backwards from the XT60 beyond one probe"
};

static const double TGT_V[4] = { 4.20, 4.35, 3.80, 3.85 };
static int verbose;
static uint64_t g_seed;
static int cur_ev = -1;
static int res_fd = -1;                    /* a child reports the index of the violating event here */

static int booted, fatal_seen, prev_run, prev_evs, sess, sess_volt, sess_hv, sess_cur, sess_choff, sess_chcur[NPORTS];
static uint64_t user_tick, last_change_tick;
static double allowed[NPORTS], rail_extra;
static int refuse[NPORTS], fault_on[NPORTS], removed[NPORTS], shorted[NPORTS];
static double t_chg[NPORTS], t_pre[NPORTS], t_bleed[NPORTS];
static uint64_t empty_since[NPORTS], oc_since[NPORTS], fed_since[NPORTS];
static uint64_t hot_since, vcch_since, vccl_since, vinl_since, rail_since;
static int hot_h;
static double backfeed, backfeed_budget;
static uint64_t last_pass;
static uint64_t max_pass;

static void violation(int inv, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

static int any_drive(void) { int k; if (S.global) return 1; for (k = 0; k < NPORTS; k++) if (S.pwm[k] || S.sw[k]) return 1; return 0; }

static void state_dump(void)
{
    int k;
    printf("  t=%.3f s (%.3f h)  run %d  phase 0x%02x  volt %u hv %u cur %u ch_off %02x  vin %.2f xt60 %.2f vcc %.3f rail %.3f (stuck %.2f) therm %u global %u profile %u\n",
           TOS(S.ticks), TOS(S.ticks) / 3600, usb_tx[8] & 1, usb_tx[25], settings.volt, settings.hv, settings.cur, settings.ch_off,
           S.vin, S.xt60_v, S.vcc, S.rail, S.rail_stuck, S.therm, S.global, S.profile);
    for (k = 0; k < NPORTS; k++)
        printf("  port %d: %s cell %.4f V (allowed %.4f) amps %+.3f pwm %3u sw %u state %u | t_chg %.0f s t_pre %.0f s t_bleed %.0f s\n",
               k + 1, S.cell[k].present ? "cell" : "----", S.cell[k].present ? sim_cell_v(k) : 0.0, allowed[k], S.amps[k], S.pwm[k], S.sw[k],
               usb_tx[18 + k], t_chg[k], t_pre[k], t_bleed[k]);
}

#include <stdarg.h>
static void violation(int inv, const char *fmt, ...)
{
    va_list ap;
    int k;
    printf("VIOLATION seed %llu event %d: [%d] %s\n  ", (unsigned long long)g_seed, cur_ev, inv, IN_NAME[inv]);
    va_start(ap, fmt); vprintf(fmt, ap); va_end(ap);
    printf("\n");
    state_dump();
    if (verbose >= 0) {
        printf("  events up to the violation (replay with -r):\n");
        for (k = 0; k <= cur_ev && k < nev; k++) { printf("    "); ev_print(stdout, &EVS[k]); }
    }
    fflush(stdout);
    if (res_fd >= 0) { double h = -1 - cur_ev; ssize_t w = write(res_fd, &h, sizeof h); (void)w; }
    exit(64 + inv);
}

static double vin_sensed(void) { double u = S.vin <= 0 ? 0 : S.profile ? S.vin : S.vin - 0.6; return S.xt60_v > u ? S.xt60_v : u; }
static double term_v(int p) { return sim_cell_v(p) + S.amps[p] * S.cell[p].r; }
static int port_iset(int p) { return settings.ch_cur[p] ? settings.ch_cur[p] : settings.cur; }

static void new_session(void)
{
    int k;
    double tg;
    sess++;
    if (S.ticks - user_tick > SEC(2.0))
        violation(I_START_ALONE, "a run started %.1f s after the last button / host start", TOS(S.ticks - user_tick));
    sess_volt = settings.volt; sess_hv = settings.hv; sess_cur = settings.cur; sess_choff = settings.ch_off;
    for (k = 0; k < NPORTS; k++) sess_chcur[k] = settings.ch_cur[k];
    tg = TGT_V[sess_volt & 3];
    rail_extra = 0;
    for (k = 0; k < NPORTS; k++) {
        double v = S.cell[k].present ? sim_cell_v(k) : 0;
        allowed[k] = fmax(v + 0.015, tg + 0.030);
        refuse[k] = S.cell[k].present && v < 2.47;
        fault_on[k] = 0; removed[k] = 0;
        t_chg[k] = t_pre[k] = t_bleed[k] = 0;
    }
    if (verbose > 0) printf("  [%.1f s] run %d starts: target %.2f V, current %u, ports off %02x\n", TOS(S.ticks), sess, tg, sess_cur, sess_choff);
}

static void check(void)
{
    int k, run, hold, ph;
    double dt = TOS(S.ticks - last_pass);
    if (S.ticks - last_pass > max_pass) max_pass = S.ticks - last_pass;
    last_pass = S.ticks;
    ph = usb_tx[25]; run = usb_tx[8] & 1; hold = (usb_tx[8] >> 5) & 1;

    if (S.wdt_bites) violation(I_WDT, "the watchdog would have reset the chip (%d)", S.wdt_bites);

    /* a fatal condition holds until the power is cycled */
    if (ph == PH_FATAL) fatal_seen = 1;
    else if (fatal_seen) violation(I_FATAL_CLEARED, "phase 0x%02x after a fatal condition", ph);

    /* runs: a new one starts on a 0 -> 1 edge, or on a stop event with the run still on (a storage restart) */
    if (run && (!prev_run || (usb_tx[13] != prev_evs && usb_tx[14] == EV_STOPPED))) new_session();
    if (!run && prev_run && verbose > 0) printf("  [%.1f s] run %d stopped (reason %u)\n", TOS(S.ticks), sess, usb_tx[11]);
    prev_run = run; prev_evs = usb_tx[13];

    /* nothing driven while stopped, in a fatal condition or during a rail pause */
    if ((!run || ph == PH_FATAL) && any_drive()) violation(I_OFF_STOPPED, "run %d phase 0x%02x but outputs driven", run, ph);
    if (run && hold && any_drive()) violation(I_OFF_HOLD, "rail pause but outputs driven");

    if (run) {
        if (!settings.hv && settings.volt == VOLT_4_35) violation(I_HV, "running with target 4.35 V and ALLOW 4.35V off");
        if (settings.volt != sess_volt || settings.hv != sess_hv || settings.cur != sess_cur || settings.ch_off != sess_choff)
            violation(I_SET_CHANGED, "volt %d->%u hv %d->%u cur %d->%u ch_off %02x->%02x", sess_volt, settings.volt, sess_hv, settings.hv,
                      sess_cur, settings.cur, sess_choff, settings.ch_off);
        for (k = 0; k < NPORTS; k++) if (settings.ch_cur[k] != sess_chcur[k]) violation(I_SET_CHANGED, "port %d current %d->%u", k + 1, sess_chcur[k], settings.ch_cur[k]);
    }

    for (k = 0; k < NPORTS; k++) {
        sim_cell_t *c = &S.cell[k];
        int st = usb_tx[18 + k];
        /* the cell's own voltage never above what it came with or its run's target + 30 mV (a port short is a hardware fault the firmware
           cannot stop: the cell's level after it is the new ceiling) */
        if (c->present && !shorted[k] && sim_cell_v(k) > allowed[k] + rail_extra)
            violation(I_OVERCHARGE, "port %d cell %.4f V > allowed %.4f V (+%.3f rail fault)", k + 1, sim_cell_v(k), allowed[k], rail_extra);
        if (run && (sess_choff >> k & 1) && (S.pwm[k] || S.sw[k])) violation(I_PORT_OFF_DRIVEN, "port %d is OFF but pwm %u sw %u", k + 1, S.pwm[k], S.sw[k]);
        if (!c->present && (S.pwm[k] || S.sw[k])) { if (!empty_since[k]) empty_since[k] = S.ticks; else if (S.ticks - empty_since[k] > SEC(3)) violation(I_EMPTY_DRIVEN, "port %d", k + 1); }
        else empty_since[k] = 0;
        if (c->present && refuse[k] && !shorted[k] && S.amps[k] > 0.02) {
            if (!fed_since[k]) fed_since[k] = S.ticks; else if (S.ticks - fed_since[k] > SEC(0.2)) violation(I_REFUSED_FED, "port %d %.3f A", k + 1, S.amps[k]);
        } else fed_since[k] = 0;
        /* time limits, counted from below (time the firmware certainly counts too) */
        if (run && c->present && !shorted[k]) {
            if (S.amps[k] > 0.045) { t_chg[k] += dt; if (term_v(k) < 2.99) t_pre[k] += dt; }
            if (S.sw[k]) t_bleed[k] += dt;
            if (t_chg[k] > T_CHG_MAX_S + 120) violation(I_T_CHG, "port %d charged %.0f s", k + 1, t_chg[k]);
            if (t_pre[k] > T_PRE_MAX_S + 60) violation(I_T_PRE, "port %d pre-charged %.0f s", k + 1, t_pre[k]);
            if (t_bleed[k] > T_BLEED_MAX_S + 120) violation(I_T_BLEED, "port %d bled %.0f s", k + 1, t_bleed[k]);
        }
        /* a latched port fault stays until the cell is pulled or the run stops (a cell below 1 V reads as no cell: allowed) */
        if (run && st == PS_FAULT) fault_on[k] = 1;
        else if (run && fault_on[k] && !removed[k] && !(c->present && sim_cell_v(k) < 1.05))
            violation(I_FAULT_CLEARED, "port %d state %d after a fault", k + 1, st);
        /* over-current: twice the setting (0.1 A in pre-charge) + 7 counts for 0.9 s faults the port; allow 2 s */
        {
            double lim = (2.0 * port_iset(k) + 7) / 68.25 + 0.05;
            if (S.amps[k] > lim && S.pwm[k]) { if (!oc_since[k]) oc_since[k] = S.ticks; else if (S.ticks - oc_since[k] > SEC(2.0)) violation(I_OVERCURRENT, "port %d %.3f A > %.3f A with pwm %u", k + 1, S.amps[k], lim, S.pwm[k]); }
            else oc_since[k] = 0;
        }
    }

    /* the board too hot or its sensor broken (sup.c: below 815 counts, above 4000; back below 1082 ... no: at or above 1082 it may start again) */
    {
        int h = S.therm && (S.therm < 815 || S.therm > 4000), cool = !S.therm || (S.therm >= 1082 && S.therm <= 4000);
        if (h && !hot_h) { hot_h = 1; hot_since = S.ticks; }
        else if (cool) hot_h = 0;
        if (hot_h && h && S.ticks - hot_since > SEC(2.5) && any_drive()) violation(I_HOT, "therm %u for %.1f s", S.therm, TOS(S.ticks - hot_since));
    }
    /* VCC over 5.0 V (with margin): fatal within 0.5 s; VCC under 4.4 V: a stop */
    if (S.vcc >= 5.03) { if (!vcch_since) vcch_since = S.ticks; else if (S.ticks - vcch_since > SEC(0.6) && ph != PH_FATAL) violation(I_VCC_HIGH, "vcc %.3f", S.vcc); }
    else vcch_since = 0;
    if (S.vcc <= 4.38) { if (!vccl_since) vccl_since = S.ticks; else if (S.ticks - vccl_since > SEC(1.5) && any_drive()) violation(I_VCC_LOW, "vcc %.3f", S.vcc); }
    else vccl_since = 0;
    /* the input below the gate (about 4.59 V; checked at 4.50): a stop within 1.5 s */
    if (vin_sensed() < 4.50) { if (!vinl_since) vinl_since = S.ticks; else if (S.ticks - vinl_since > SEC(1.5) && any_drive()) violation(I_VIN_LOW, "input %.2f V", vin_sensed()); }
    else vinl_since = 0;
    /* the rail stuck well out of its band: nothing driven after 2.5 s (a pause or a fatal stop) */
    {
        double sp = TGT_V[settings.volt & 3] + 0.015;
        if (S.rail_stuck > 0 && fabs(S.rail - sp) > 0.06) {             /* the rail as it is (a stuck buck sags under load, maybe into the band) */
            if (!rail_since) rail_since = S.ticks;
            else if (S.ticks - rail_since > SEC(2.5) && any_drive()) violation(I_RAIL, "rail stuck at %.2f V, at %.3f V for %.1f s", S.rail_stuck, S.rail, TOS(S.ticks - rail_since));
        } else rail_since = 0;
    }
    /* USB-C fed backwards: the bypass on while a pack on the XT60 is clearly higher (0.6 V; under 0.4 V is documented as not covered);
       one probe (about 20 ms, under 40 ms) per change of a supply is the design */
    if (S.profile && S.xt60_v > S.vin + 0.6) {
        if (verbose > 1) printf("  [%.3f s] fed backwards (USB-C %.2f V, XT60 %.2f V), %.3f s so far\n", TOS(S.ticks), S.vin, S.xt60_v, backfeed + dt);
        backfeed += dt;
        if (backfeed > backfeed_budget) violation(I_BACKFEED, "fed backwards %.3f s in all (allowed %.3f), USB-C %.2f V, XT60 %.2f V",
                                                  backfeed, backfeed_budget, S.vin, S.xt60_v);
    }
}

static void pass(void) { app_pass(); check(); }
static void run_for(double s) { uint64_t end = S.ticks + SEC(s); while (S.ticks < end) pass(); }

static void power_up(void)
{
    int k;
    app_boot(); booted = 1; S.booted = 1;
    prev_run = 0; prev_evs = usb_tx[13]; fatal_seen = 0;
    backfeed_budget += 0.06; last_pass = S.ticks;
    if (any_drive()) violation(I_OFF_STOPPED, "outputs driven after the boot");
    for (k = 0; k < NPORTS; k++) { fault_on[k] = 0; empty_since[k] = 0; oc_since[k] = 0; fed_since[k] = 0; }
    hot_h = 0; vcch_since = vccl_since = vinl_since = rail_since = 0;   /* a fresh boot: the waits start again */
}
static void ensure_boot(void) { if (!booted) power_up(); }

static void power_cycle(double off_s)
{
    S.power = 0; S.global = 0; memset(S.pwm, 0, sizeof S.pwm); memset(S.sw, 0, sizeof S.sw); S.booted = 0; S.wdt_on = 0;
    S.profile = 0;
    sim_advance((uint32_t)SEC(off_s));
    S.btn1 = S.btn2 = 0;
    power_up();
}

static void exec(const ev_t *e)
{
    int k;
    sim_cell_t *c;
    switch (e->t) {
    case E_WAIT: ensure_boot(); run_for(e->x[0]); break;
    case E_BTN:
        ensure_boot();
        S.btn1 = e->a & 1; S.btn2 = (e->a >> 1) & 1; user_tick = S.ticks;
        run_for(e->x[0]);
        S.btn1 = S.btn2 = 0; user_tick = S.ticks;
        run_for(0.2);
        break;
    case E_USB:
        ensure_boot();
        memset(S.usb_cmd, 0, sizeof S.usb_cmd); memcpy(S.usb_cmd, e->b, e->n < USB_CMD ? e->n : USB_CMD); S.usb_pending = 1;
        if (e->b[0] == 2) user_tick = S.ticks;
        pass(); pass();
        if (S.reboot_isp) { S.reboot_isp = 0; if (verbose > 0) printf("  [%.1f s] reboot into the loader: power cycle\n", TOS(S.ticks)); power_cycle(1.0); }
        break;
    case E_ATTACH:
        k = e->a % NPORTS; c = &S.cell[k];
        sim_attach(k, e->x[0], e->x[1]);
        c->r = e->x[2]; c->r2 = e->x[2] * 0.5; c->leak = e->x[3]; S.amps[k] = 0;
        removed[k] = 1; refuse[k] = e->x[0] < 2.47;
        allowed[k] = sim_cell_v(k) + 0.015;
        if (prev_run) allowed[k] = fmax(allowed[k], TGT_V[sess_volt & 3] + 0.030);
        t_chg[k] = t_pre[k] = t_bleed[k] = 0;
        break;
    case E_DETACH: k = e->a % NPORTS; S.cell[k].present = 0; S.amps[k] = 0; removed[k] = 1; break;
    case E_VIN: S.vin = e->x[0]; if (S.vin <= 0 && S.xt60_v <= 0) S.vin = 5.0; backfeed_budget += 0.06; break;
    case E_XT60: S.xt60_v = e->x[0]; if (S.vin <= 0 && S.xt60_v <= 0) S.xt60_v = 16.8; backfeed_budget += 0.06; break;
    case E_VCC: S.vcc = e->x[0]; break;
    case E_RAIL: S.rail_stuck = e->x[0]; if (e->x[0] > 0) rail_extra = 0.035; break;   /* a broken buck: the port check allows 50 mV, 0.45 s */
    case E_SHORT:
        k = e->a % NPORTS; S.short_amps[k] = e->x[0];
        if (e->x[0] > 0) shorted[k] = 1;
        else { shorted[k] = 0; if (S.cell[k].present) allowed[k] = fmax(allowed[k], sim_cell_v(k) + 0.010); refuse[k] = 0; }
        break;
    case E_THERM: S.therm = (uint16_t)e->x[0]; break;
    case E_LCD: S.lcd_slow = e->x[0]; break;
    case E_NOISE: S.adc_noise = e->x[0]; break;
    case E_POWER: if (booted) power_cycle(e->x[0]); break;
    case E_CFG:
        if (booted) break;                                   /* only as the page found at power-up */
        cfg_load();
        settings.cur = e->b[0]; settings.volt = e->b[1]; settings.sound = e->b[2]; settings.hv = e->b[3];
        settings.ilim = e->b[4]; settings.vmin = e->b[5]; settings.vcut = e->b[6]; settings.lmode = e->b[7];
        for (k = 0; k < NPORTS; k++) settings.ch_cur[k] = e->b[8 + k];
        settings.ch_off = e->b[14]; settings.usb_off = e->b[15];
        cfg_save();
        break;
    }
}

static int run_events(int n)
{
    int k;
    sim_reset(); srand((unsigned)(g_seed * 2654435761u + 7));
    S.usb_host = 1; S.therm = 2150;
    booted = 0; sess = 0; backfeed = 0; backfeed_budget = 0; user_tick = 0; last_change_tick = 0; max_pass = 0;
    memset(shorted, 0, sizeof shorted); rail_extra = 0; hot_h = 0;
    for (k = 0; k < NPORTS; k++) allowed[k] = 9;
    for (cur_ev = 0; cur_ev < n; cur_ev++) {
        if (verbose > 0 && EVS[cur_ev].t != E_WAIT) { printf("  [%.1f s] ", TOS(S.ticks)); ev_print(stdout, &EVS[cur_ev]); }
        exec(&EVS[cur_ev]);
    }
    cur_ev = n - 1;
    ensure_boot(); run_for(1.0);
    return 0;
}

/* ---------------------------------------------------------------------------------------------------------------------- */
/* driver: seeds in child processes, shrinking */

static int child(int n, int quiet, double *hours)   /* *hours: simulated hours, or -1 - (index of the violating event) */
{
    pid_t pid;
    int st, fd[2];
    if (pipe(fd)) return -1;
    fflush(stdout);
    pid = fork();
    if (pid == 0) {
        close(fd[0]); res_fd = fd[1];
        if (quiet) { FILE *f = freopen("/dev/null", "w", stdout); (void)f; }
        run_events(n);
        { double h = TOS(S.ticks) / 3600; ssize_t w = write(fd[1], &h, sizeof h); (void)w; }
        fflush(stdout); _exit(0);
    }
    close(fd[1]);
    { double h = 0; ssize_t r = read(fd[0], &h, sizeof h); if (r == (ssize_t)sizeof h && hours) *hours = h; }
    close(fd[0]);
    waitpid(pid, &st, 0);
    if (WIFEXITED(st)) return WEXITSTATUS(st);
    return 255;
}

/* delta debugging on the event list, each trial in a fresh process; keeps the same invariant */
static int shrink(int inv_code, int max_trials)
{
    static ev_t keep[MAXEV];
    int n = nev, gran = 2, trials = 0, k, i;
    double r = 0;
    for (k = 0; k < n; k++) keep[k] = EVS[k];
    if (child(n, 1, &r) == inv_code && r < 0) n = (int)(-1 - r) + 1;   /* nothing after the violating event matters */
    trials++;
    while (n >= 2 && trials < max_trials) {
        int chunk = (n + gran - 1) / gran, reduced = 0;
        for (i = 0; i < gran && trials < max_trials; i++) {
            int lo = i * chunk, hi = lo + chunk > n ? n : lo + chunk, m = 0;
            if (lo >= n) break;
            for (k = 0; k < n; k++) if (k < lo || k >= hi) EVS[m++] = keep[k];
            nev = m; trials++;
            if (child(m, 1, 0) == inv_code) {
                for (k = 0; k < m; k++) keep[k] = EVS[k];
                n = m; reduced = 1; gran = gran > 2 ? gran - 1 : 2;
                break;
            }
        }
        if (!reduced) { if (gran >= n) break; gran = gran * 2 > n ? n : gran * 2; }
    }
    /* shorten the waits */
    for (k = 0; k < n && trials < max_trials; k++) {
        double w;
        if (keep[k].t != E_WAIT || keep[k].x[0] < 0.5) continue;
        for (w = keep[k].x[0] / 4; w >= 0.2 && trials < max_trials; w /= 4) {
            for (i = 0; i < n; i++) EVS[i] = keep[i];
            EVS[k].x[0] = w; nev = n; trials++;
            if (child(n, 1, 0) == inv_code) keep[k].x[0] = w; else break;
        }
    }
    for (k = 0; k < n; k++) EVS[k] = keep[k];
    nev = n;
    return trials;
}

static void usage(void)
{
    printf("sim_fuzz [-n N] [-s SEED] [-j JOBS] [-H MAXHOURS] [-q] [--no-shrink] | -1 SEED [-v] | -r FILE [-v] | -m SEED\n");
}

int main(int argc, char **argv)
{
    uint64_t seed = 1;
    int n = 20, jobs = 1, k, mode = 0, do_shrink = 1, quiet = 0, fails = 0;
    double hmax = 6.0, hours = 0;
    const char *file = 0;
    for (k = 1; k < argc; k++) {
        if (!strcmp(argv[k], "-n") && k + 1 < argc) n = atoi(argv[++k]);
        else if (!strcmp(argv[k], "-s") && k + 1 < argc) seed = strtoull(argv[++k], 0, 0);
        else if (!strcmp(argv[k], "-j") && k + 1 < argc) jobs = atoi(argv[++k]);
        else if (!strcmp(argv[k], "-H") && k + 1 < argc) hmax = atof(argv[++k]);
        else if (!strcmp(argv[k], "-1") && k + 1 < argc) { mode = 1; seed = strtoull(argv[++k], 0, 0); }
        else if (!strcmp(argv[k], "-m") && k + 1 < argc) { mode = 3; seed = strtoull(argv[++k], 0, 0); }
        else if (!strcmp(argv[k], "-r") && k + 1 < argc) { mode = 2; file = argv[++k]; }
        else if (!strcmp(argv[k], "-v")) verbose = 1;
        else if (!strcmp(argv[k], "-vv")) verbose = 2;
        else if (!strcmp(argv[k], "-q")) quiet = 1;
        else if (!strcmp(argv[k], "--no-shrink")) do_shrink = 0;
        else { usage(); return 2; }
    }
    if (getenv("FUZZ_SEEDS")) n = atoi(getenv("FUZZ_SEEDS"));
    if (getenv("FUZZ_SEED")) seed = strtoull(getenv("FUZZ_SEED"), 0, 0);
    if (getenv("FUZZ_JOBS")) jobs = atoi(getenv("FUZZ_JOBS"));
    if (getenv("FUZZ_HOURS")) hmax = atof(getenv("FUZZ_HOURS"));

    if (mode == 1) { g_seed = seed; generate(seed, hmax); printf("seed %llu: %d events\n", (unsigned long long)seed, nev); run_events(nev);
                     printf("seed %llu: ok, %.2f h simulated, longest pass %.1f ms\n", (unsigned long long)seed, TOS(S.ticks) / 3600, max_pass / 2.0); return 0; }
    if (mode == 2) {
        FILE *f = fopen(file, "r");
        char line[512];
        if (!f) { perror(file); return 2; }
        nev = 0;
        while (fgets(line, sizeof line, f) && nev < MAXEV) if (ev_parse(line, &EVS[nev])) nev++;
        fclose(f);
        g_seed = 0;
        { const char *s = getenv("FUZZ_SEED"); if (s) g_seed = strtoull(s, 0, 0); }
        printf("replay %s: %d events\n", file, nev);
        run_events(nev);
        printf("replay: ok, %.2f h simulated\n", TOS(S.ticks) / 3600);
        return 0;
    }
    if (mode == 3) {
        int r, t;
        g_seed = seed; generate(seed, hmax);
        r = child(nev, 1, 0);
        if (r < 64) { printf("seed %llu does not fail (%d)\n", (unsigned long long)seed, r); return 0; }
        t = shrink(r, 400);
        printf("seed %llu: [%d] %s, shrunk to %d events in %d trials (replay: FUZZ_SEED=%llu sim_fuzz -r FILE):\n", (unsigned long long)seed, r - 64,
               IN_NAME[r - 64], nev, t, (unsigned long long)seed);
        for (k = 0; k < nev; k++) { printf("    "); ev_print(stdout, &EVS[k]); }
        verbose = 0; child(nev, 0, 0);
        return 1;
    }

    /* many seeds, `jobs` at a time */
    {
        uint64_t s;
        int running = 0, done = 0;
        double total_h = 0;
        struct { pid_t pid; uint64_t seed; int fd; } J[64];
        if (jobs < 1) jobs = 1;
        if (jobs > 64) jobs = 64;
        printf("sim_fuzz: seeds %llu..%llu, up to %.1f h each, %d at a time\n", (unsigned long long)seed, (unsigned long long)(seed + n - 1), hmax, jobs);
        fflush(stdout);
        for (s = seed; done < n;) {
            while (running < jobs && s < seed + (uint64_t)n) {
                int fd[2];
                pid_t pid;
                if (pipe(fd)) return 2;
                pid = fork();
                if (pid == 0) {
                    char path[64];
                    FILE *f;
                    close(fd[0]); res_fd = fd[1];
                    snprintf(path, sizeof path, "/tmp/sim_fuzz_%d.log", (int)getpid());
                    f = freopen(path, "w", stdout); (void)f;
                    g_seed = s; generate(s, hmax);
                    run_events(nev);
                    { double h = TOS(S.ticks) / 3600; ssize_t w = write(fd[1], &h, sizeof h); (void)w; }
                    fflush(stdout); _exit(0);
                }
                close(fd[1]);
                J[running].pid = pid; J[running].seed = s; J[running].fd = fd[0]; running++; s++;
            }
            {
                int st, i;
                pid_t pid = wait(&st);
                for (i = 0; i < running; i++) if (J[i].pid == pid) break;
                if (i == running) continue;
                {
                    double h = 0;
                    char path[64];
                    ssize_t r = read(J[i].fd, &h, sizeof h);
                    int code = WIFEXITED(st) ? WEXITSTATUS(st) : 255;
                    uint64_t sd = J[i].seed;
                    close(J[i].fd);
                    snprintf(path, sizeof path, "/tmp/sim_fuzz_%d.log", (int)pid);
                    J[i] = J[--running]; done++;
                    if (r == (ssize_t)sizeof h && h > 0) total_h += h;
                    if (code) {
                        FILE *f = fopen(path, "r");
                        char line[1024];
                        int lines = 0;
                        fails++;
                        printf("seed %llu FAILED (%d):\n", (unsigned long long)sd, code >= 64 ? code - 64 : -code);
                        if (f) { while (fgets(line, sizeof line, f)) { if (strncmp(line, "    ", 4) || lines < 0) fputs(line, stdout); lines++; } fclose(f); }
                        if (do_shrink && code >= 64) {
                            int t;
                            g_seed = sd; generate(sd, hmax);
                            t = shrink(code, 250);
                            printf("  shrunk to %d events in %d trials; replay with FUZZ_SEED=%llu build/sim_fuzz -r FILE:\n", nev, t, (unsigned long long)sd);
                            for (k = 0; k < nev; k++) { printf("    "); ev_print(stdout, &EVS[k]); }
                        } else if (code >= 64) printf("  reproduce: build/sim_fuzz -1 %llu (full event list printed there)\n", (unsigned long long)sd);
                    } else if (!quiet) {
                        printf("seed %llu ok (%.2f h)\n", (unsigned long long)sd, h);
                    }
                    unlink(path);
                    fflush(stdout);
                }
            }
        }
        printf("sim_fuzz: %d seeds, %.1f simulated hours, %d failed\n", n, total_h, fails);
    }
    (void)hours;
    return fails ? 1 : 0;
}

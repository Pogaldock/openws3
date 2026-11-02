/* End-to-end safety scenarios: the real firmware (app.c and every module it calls, as built for the charger) runs against the
 * simulated board in sim_hal.c, driven by its buttons like a person would.  Each scenario runs in its own process (fresh statics, like a
 * power-up) and checks what the board did: currents, PWMs, the global enable, the cells' voltages.  Build and run: make sim */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/wait.h>
#include "sim.h"
#include "app.h"
#include "cfg.h"
#include "../check.h"

#define SEC(s) ((uint64_t)((s) * 2000))

static uint64_t max_gap;        /* the longest control pass while charging and nobody touches anything, ticks */
static int measure;             /* set by a scenario around its steady charging stretch */

static void pass(void)
{
    uint64_t t0 = S.ticks;
    app_pass();
    if (measure && S.ticks - t0 > max_gap) max_gap = S.ticks - t0;
    if (measure && getenv("SIM_DEBUG") && S.ticks - t0 > 2 * 18) {
        int k;
        printf("    long pass %.1f ms at %.1f s:", (S.ticks - t0) / 2.0, S.ticks / 2000.0);
        for (k = 0; k < NPORTS; k++) printf(" p%d pwm %u", k + 1, S.pwm[k]);
        printf("\n");
    }
}

static void run_for(double s) { uint64_t end = S.ticks + SEC(s); while (S.ticks < end) pass(); }
static void boot(void) { app_boot(); S.booted = 1; run_for(2.0); }   /* the rail self-test runs in the first passes */
static void tap(int b)  { if (b == 1) S.btn1 = 1; else S.btn2 = 1; run_for(0.12); S.btn1 = S.btn2 = 0; run_for(0.2); }
static void hold(int b) { if (b == 1) S.btn1 = 1; else S.btn2 = 1; run_for(0.6); S.btn1 = S.btn2 = 0; run_for(0.2); }
static void start(void) { hold(1); }
static int  all_off(void) { int k; for (k = 0; k < NPORTS; k++) if (S.pwm[k] || S.sw[k]) return 0; return !S.global; }
static int  any_pwm(void) { int k; for (k = 0; k < NPORTS; k++) if (S.pwm[k]) return 1; return 0; }
static void set_current(int tenths) { int n; for (n = 3; n < tenths; n++) tap(1); }     /* from the default 0.3 A, +0.1 A per tap */
static double rested(int p) { return sim_cell_v(p); }

/* the time (s) for cond to become true, at most limit s; -1 if it never did */
static double time_until(int (*cond)(void), double limit)
{
    uint64_t t0 = S.ticks, end = S.ticks + SEC(limit);
    while (S.ticks < end) { if (cond()) return (double)(S.ticks - t0) / 2000; pass(); }
    return -1;
}

/* ---- the scenarios ---- */

static void full_charge(const char *name, double vcc, double bg_err, double noise, double lo, double hi)
{
    static const double v0[5] = { 3.60, 3.75, 3.90, 4.05, 4.18 };
    int p;
    uint16_t pwm6 = 0;
    S.vcc = vcc; S.bg_err = bg_err; S.adc_noise = noise;
    for (p = 0; p < 5; p++) sim_attach(p, v0[p], 120);
    boot(); set_current(6); start();
    measure = 1;
    { uint64_t end = S.ticks + SEC(45 * 60); while (S.ticks < end) { pass(); if (S.pwm[5] > pwm6) pwm6 = S.pwm[5]; } }
    measure = 0;
    hold(1); run_for(60);                                          /* stop, let the cells rest a minute, then "measure with a meter" */
    printf("  %-26s finished at", name);
    for (p = 0; p < 5; p++) printf(" %.3f", rested(p));
    printf(" V   peak");
    for (p = 0; p < 5; p++) printf(" %.3f", S.max_cell_v[p]);
    printf(" V   rail max while charging %.3f V   longest pass %.1f ms\n", S.max_rail_driven, max_gap / 2.0);
    for (p = 0; p < 5; p++) { CHECK(rested(p) >= lo && rested(p) <= hi); CHECK(S.max_cell_v[p] <= hi + 0.01); }
    CHECK(pwm6 == 0);                                             /* the empty port was never driven */
    CHECK(S.max_rail_driven < 4.30);                              /* with cells connected the rail stayed near its 4.215 V setpoint */
    CHECK(max_gap <= 2 * 50);                                     /* no control pass over 50 ms while charging (stock: 28.5 ms when a cell finishes) */
}

static void s_nominal(void)   { full_charge("nominal (VCC 4.55 V)", 4.551, 0, 0, 4.17, 4.215); }
static void s_vcc_low(void)   { full_charge("VCC 4.45 V", 4.45, 0, 0, 4.17, 4.215); }
static void s_vcc_mid(void)   { full_charge("VCC 4.75 V", 4.75, 0, 0, 4.17, 4.215); }
static void s_vcc_high(void)  { full_charge("VCC 4.95 V", 4.95, 0, 0, 4.17, 4.215); }
static void s_bg_plus(void)   { full_charge("bandgap +1 % (chip spread)", 4.551, 0.01, 0, 4.15, 4.25); }
static void s_bg_minus(void)  { full_charge("bandgap -1 % (chip spread)", 4.551, -0.01, 0, 4.15, 4.25); }
static void s_noise(void)     { full_charge("ADC noise +-4 counts", 4.551, 0, 4, 4.16, 4.22); }
static void s_noise_vcc(void) { full_charge("noise + VCC 4.9 V + bg -1 %", 4.9, -0.01, 4, 4.14, 4.26); }

static void s_user_stop(void)
{
    double t;
    sim_attach(0, 3.7, 120); sim_attach(1, 3.8, 120);
    boot(); start(); run_for(60);
    CHECK(any_pwm() && S.global);
    S.btn1 = 1; t = time_until(all_off, 2.0); S.btn1 = 0;         /* hold the left button */
    printf("  user stop: all outputs off %.2f s after the button went down\n", t);
    CHECK(t >= 0 && t < 0.8);
    run_for(60);
    CHECK(all_off());                                             /* and it stays stopped */
}

static int low_input_off(void) { return all_off(); }
static void s_input_loss(void)
{
    double t;
    sim_attach(0, 3.7, 120); sim_attach(1, 3.8, 120);
    boot(); start(); run_for(30);
    S.vin = 3.0;                                                  /* the supply sags away */
    t = time_until(low_input_off, 5.0);
    printf("  input lost: outputs off after %.2f s\n", t);
    CHECK(t >= 0 && t < 1.5);
    run_for(5); S.vin = 5.15; run_for(60);                        /* it comes back */
    CHECK(all_off());                                             /* no restart on its own */
}

static int p1_off(void) { return S.pwm[1] == 0; }
static void s_removal(void)
{
    double t;
    sim_attach(0, 3.7, 120); sim_attach(1, 3.7, 120);
    boot(); start(); run_for(30);
    CHECK(S.pwm[1] > 0);
    S.cell[1].present = 0; S.amps[1] = 0;
    t = time_until(p1_off, 5.0);
    printf("  cell pulled mid-charge: its port off after %.2f s, the other still charging: %s\n", t, S.pwm[0] ? "yes" : "no");
    CHECK(t >= 0 && t < 3.0);
    CHECK(S.pwm[0] > 0);
}

static void s_dead_cell(void)
{
    uint64_t end;
    uint16_t mx = 0;
    sim_attach(0, 2.3, 120);
    boot(); start();
    end = S.ticks + SEC(120);
    while (S.ticks < end) { pass(); if (S.pwm[0] > mx) mx = S.pwm[0]; }
    printf("  2.30 V cell: highest PWM given %u, cell now %.3f V\n", mx, rested(0));
    CHECK(mx == 0);
    CHECK(rested(0) < 2.31);
}

static void s_gentle(void)
{
    uint64_t end;
    double max_low = 0, max_high = 0;
    sim_attach(0, 2.7, 120);
    boot(); set_current(10); start();
    end = S.ticks + SEC(20 * 60);
    while (S.ticks < end) {
        pass();
        if (rested(0) < 2.98 && S.amps[0] > max_low) max_low = S.amps[0];
        if (rested(0) > 3.1 && S.amps[0] > max_high) max_high = S.amps[0];
    }
    printf("  2.70 V cell at a 1.0 A setting: %.3f A below 3.0 V, %.3f A above\n", max_low, max_high);
    CHECK(max_low > 0.05 && max_low < 0.13);
    CHECK(max_high > 0.9);
}

static void s_broken_rail(void)
{
    double t, before;
    sim_attach(0, 4.10, 120); sim_attach(1, 3.9, 120);
    boot(); start(); run_for(20);
    before = S.max_cell_v[0];
    S.rail_stuck = 4.70;                                          /* the buck no longer listens */
    t = time_until(all_off, 10.0);
    run_for(60);
    printf("  rail stuck at 4.70 V: outputs off after %.2f s, cell peak %.3f V (was %.3f), now %.3f V\n", t, S.max_cell_v[0], before, rested(0));
    CHECK(t >= 0 && t < 2.0);
    CHECK(all_off());
    CHECK(S.max_cell_v[0] < 4.25);
}

static int p0_off(void) { return S.pwm[0] == 0; }
static void s_short(void)
{
    double t;
    sim_attach(0, 3.7, 120); sim_attach(1, 3.7, 120);
    boot(); set_current(6); start(); run_for(20);
    S.short_amps[0] = 3.0;                                        /* the port suddenly draws 3 A */
    t = time_until(p0_off, 5.0);
    printf("  3 A short on port 1: its PWM to 0 after %.2f s, port 2 still charging: %s\n", t, S.pwm[1] ? "yes" : "no");
    CHECK(t >= 0 && t < 1.5);
    run_for(5);
    CHECK(S.pwm[0] == 0 && S.pwm[1] > 0);
    S.short_amps[0] = 0; run_for(30);                             /* the short clears: a latched fault keeps the port off */
    printf("  ... short cleared, 30 s later port 1 PWM %u (latched off: %s)\n", S.pwm[0], S.pwm[0] ? "NO" : "yes");
    CHECK(S.pwm[0] == 0);
}

static void s_storage(void)
{
    int p;
    sim_attach(0, 4.15, 120); sim_attach(1, 4.05, 120); sim_attach(2, 3.60, 120);
    boot(); tap(2); tap(2);                                       /* 4.20 -> 4.35 -> 3.80 V storage */
    start(); run_for(30 * 60);
    hold(1); run_for(60);
    printf("  storage 3.80 V: cells at");
    for (p = 0; p < 3; p++) printf(" %.3f", rested(p));
    printf(" V\n");
    for (p = 0; p < 3; p++) CHECK(rested(p) > 3.76 && rested(p) < 3.84);
}

static void s_power_cycle(void)
{
    uint64_t end;
    int driven = 0;
    sim_attach(0, 3.7, 120);
    boot(); start(); run_for(30);
    CHECK(any_pwm());
    S.power = 0; S.global = 0; memset(S.pwm, 0, sizeof S.pwm); S.booted = 0;    /* power cut, then back: a fresh boot */
    boot();
    end = S.ticks + SEC(60);
    while (S.ticks < end) { pass(); if (any_pwm() || S.global) driven = 1; }
    printf("  power cycle mid-charge: outputs driven before anyone pressed start: %s\n", driven ? "YES" : "no");
    CHECK(!driven);
}

static void s_never_full(void)
{
    sim_attach(0, 4.00, 1000000);                                 /* a cell that never fills (a damaged one keeps drawing) */
    boot(); start();
    run_for(2.9 * 3600);
    CHECK(S.pwm[0] > 0);
    run_for(0.2 * 3600);
    printf("  cell that never tapers: still charging at 2.9 h, PWM after 3.1 h: %u\n", S.pwm[0]);
    CHECK(S.pwm[0] == 0);
}

/* VCC jumps mid-charge (another supply plugged in, a sagging input): the readings must not let the cells see a rail spike */
static void s_vcc_step(void)
{
    int p;
    double rail_before;
    for (p = 0; p < 4; p++) sim_attach(p, 4.05 + 0.04 * p, 120);
    boot(); set_current(6); start(); run_for(120);
    rail_before = S.max_rail_driven;
    S.vcc = 4.95; run_for(60);
    S.vcc = 4.45; run_for(60);
    S.vcc = 4.95; run_for(60);
    printf("  VCC steps 4.55 -> 4.95 -> 4.45 -> 4.95 V mid-charge: rail peak %.3f V (before the steps %.3f), cell peaks", S.max_rail_driven, rail_before);
    for (p = 0; p < 4; p++) printf(" %.3f", S.max_cell_v[p]);
    printf(" V\n");
    CHECK(S.max_rail_driven < 4.26);
    for (p = 0; p < 4; p++) CHECK(S.max_cell_v[p] < 4.215);
}

/* a damaged cell leaking 50 mA inside: it tapers (below the tail current), goes full, sags, gets topped up, sags... for ever unless the
   time spent charging adds up across the top-ups */
static void s_leaky(void)
{
    int faulted = 0;
    uint64_t end;
    sim_attach(0, 4.0, 120); S.cell[0].leak = 0.05;
    boot(); start();
    end = S.ticks + SEC(12 * 3600);
    while (S.ticks < end) {
        run_for(60);
        if (!S.pwm[0] && !S.sw[0] && sim_cell_v(0) < 3.9) { faulted = 1; break; }    /* left alone: it was given up on */
    }
    printf("  cell leaking 50 mA: %s after %.1f h\n", faulted ? "given up on (port off, cell draining)" : "STILL being topped up", S.ticks / 2000.0 / 3600);
    CHECK(faulted);
}

static void both(double s) { S.btn1 = S.btn2 = 1; run_for(s); S.btn1 = S.btn2 = 0; run_for(0.25); }
static void usb(const uint8_t *c, int n) { memset(S.usb_cmd, 0, sizeof S.usb_cmd); memcpy(S.usb_cmd, c, (size_t)n); S.usb_pending = 1; pass(); pass(); }

/* the menu while charging: the target voltage and the current must not change, whatever is pressed */
static void s_menu_lock(void)
{
    int k;
    sim_attach(0, 3.9, 120);
    boot(); start(); run_for(10);
    both(0.12);                                                   /* open the menu */
    both(0.12);                                                   /* open CHARGING */
    for (k = 0; k < 2; k++) { both(0.12); tap(2); tap(2); both(0.12); tap(2); }    /* try to edit VOLTAGE and CURRENT */
    both(1.0);                                                    /* close and save */
    run_for(5);
    printf("  menu while charging: target index %u (0 = 4.20 V), current %u (21 = 0.3 A), still charging: %s\n", settings.volt, settings.cur, S.pwm[0] ? "yes" : "no");
    CHECK(settings.volt == 0 && settings.cur == 21);
    CHECK(S.pwm[0] > 0 && S.global);
}

/* the rail goes out of range (4.35 V, still measurable) for 5 s: the run pauses with nothing driven, then resumes by itself */
static void s_rail_pause(void)
{
    double t;
    sim_attach(0, 3.9, 120); sim_attach(1, 4.15, 120);
    boot(); start(); run_for(20);
    S.rail_stuck = 4.35;
    t = time_until(all_off, 5.0);
    run_for(5); S.rail_stuck = 0; run_for(10);
    printf("  rail stuck at 4.35 V for a while: outputs off after %.2f s, cell peaks %.3f %.3f V, charging again afterwards: %s\n", t, S.max_cell_v[0], S.max_cell_v[1], S.pwm[0] ? "yes" : "no");
    CHECK(t >= 0 && t < 2.0);
    CHECK(S.max_cell_v[1] < 4.215);
    CHECK(S.pwm[0] > 0);
}

/* VCC over 5.0 V: everything off and locked until the power is cycled */
static void s_vcc_fatal(void)
{
    double t;
    sim_attach(0, 3.9, 120);
    boot(); start(); run_for(10);
    S.vcc = 5.15;
    t = time_until(all_off, 2.0);
    S.vcc = 4.55; run_for(30); start(); run_for(10);
    printf("  VCC 5.15 V: outputs off after %.2f s; back to normal and start pressed: still locked off: %s\n", t, all_off() ? "yes" : "NO");
    CHECK(t >= 0 && t < 0.5);
    CHECK(all_off());
}

/* the rail cannot be regulated from the start (it sits at 4.20 V): the self-test fails, a run can never start */
static void s_selftest_fail(void)
{
    uint64_t end;
    int driven = 0;
    sim_attach(0, 3.9, 120);
    S.rail_stuck = 4.20;
    app_boot(); S.booted = 1;
    end = S.ticks + SEC(20);
    while (S.ticks < end) { pass(); if (S.global || any_pwm()) driven = 1; }
    start(); run_for(10); if (S.global || any_pwm()) driven = 1;
    printf("  rail that does not respond at boot: outputs ever driven: %s\n", driven ? "YES" : "no");
    CHECK(!driven);
}

/* developer mode from a PC: drive port 1 flat out with the rail raw.  At the rail's top the ADC saturates and the unit locks out; just
   under that (about 4.45 V) the per-port guard must cut the port.  And the outputs must go off by themselves when the PC goes quiet */
static void dev_drive(int rail_duty, double secs)
{
    static const uint8_t enter[] = { 7, 9, 1 };
    uint8_t set[19] = { 8, 0x11, 2, 0, 0, 0, 0x01, 0x90 };          /* P5.2 on + rail raw, port 1 duty 400 */
    int k;
    set[4] = (uint8_t)(rail_duty >> 8); set[5] = (uint8_t)rail_duty; set[18] = 9;
    usb(enter, 3);
    for (k = 0; k < secs * 10; k++) { usb(set, 19); run_for(0.1); }
}

static void s_dev_top(void)
{
    sim_attach(0, 4.10, 120);
    boot(); dev_drive(0, 20);
    printf("  developer mode, rail raw at its top: locked out: %s, cell peak %.3f V\n", all_off() ? "yes" : "NO", S.max_cell_v[0]);
    CHECK(all_off());
}

static void s_dev_mode(void)
{
    uint8_t set[19] = { 8, 0x11, 2, 0, 0, 137, 0, 0, 0x01, 0x90 };  /* port 2 duty 400, rail raw 137 */
    double t;
    sim_attach(0, 4.10, 120); sim_attach(1, 4.10, 120);
    boot();
    dev_drive(137, 600);                                            /* 10 min of a host holding port 1 on, rail about 4.45 V */
    printf("  developer mode, port driven flat out, rail ~%.2f V: cell held at %.3f V, PWM %u (developer mode only cuts at 4.40 V)\n", S.rail, S.max_cell_v[0], S.pwm[0]);
    CHECK(S.max_cell_v[0] < 4.41);                                  /* all developer mode promises: the 4.40 V port guard */
    set[18] = 9;
    usb(set, 19); run_for(0.2);
    CHECK(S.pwm[1] > 0);
    t = time_until(all_off, 3.0);                                   /* the PC goes quiet */
    printf("  developer mode, PC stops sending: outputs off after %.2f s\n", t);
    CHECK(t >= 0 && t < 0.8);
}

typedef struct { const char *name; void (*fn)(void); } scen_t;
static const scen_t SC[] = {
    { "nominal", s_nominal }, { "vcc_low", s_vcc_low }, { "vcc_mid", s_vcc_mid }, { "vcc_high", s_vcc_high },
    { "bg_plus", s_bg_plus }, { "bg_minus", s_bg_minus }, { "noise", s_noise }, { "noise_vcc", s_noise_vcc },
    { "user_stop", s_user_stop }, { "input_loss", s_input_loss }, { "removal", s_removal }, { "dead_cell", s_dead_cell },
    { "gentle", s_gentle }, { "broken_rail", s_broken_rail }, { "short", s_short }, { "storage", s_storage },
    { "power_cycle", s_power_cycle }, { "never_full", s_never_full }, { "vcc_step", s_vcc_step }, { "leaky", s_leaky },
    { "menu_lock", s_menu_lock }, { "rail_pause", s_rail_pause }, { "vcc_fatal", s_vcc_fatal }, { "selftest_fail", s_selftest_fail },
    { "dev_top", s_dev_top }, { "dev_mode", s_dev_mode },
};

int main(int argc, char **argv)
{
    unsigned k, failed = 0;
    for (k = 0; k < sizeof SC / sizeof SC[0]; k++) {
        pid_t pid;
        int st;
        if (argc > 1 && strcmp(argv[1], SC[k].name)) continue;
        fflush(stdout);
        pid = fork();
        if (pid == 0) { srand(1234 + k); sim_reset(); SC[k].fn(); fflush(stdout); exit(fails ? (check_report(SC[k].name), 1) : 0); }
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st)) { failed++; printf("  ** %s FAILED\n", SC[k].name); }
    }
    printf("sim: %u scenarios failed\n", failed);
    return failed ? 1 : 0;
}

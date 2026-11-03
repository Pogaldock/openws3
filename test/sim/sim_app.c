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
#include "adc.h"
#include "app.h"
#include "cfg.h"
#include "usb.h"
#include "stats.h"
#include "app.h"
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
    double max_low = 0, max_high = 0, first = 0;
    uint64_t t_up = 0;
    sim_attach(0, 2.7, 120);
    boot(); set_current(10); start();
    end = S.ticks + SEC(20 * 60);
    while (S.ticks < end) {
        pass();
        if (rested(0) < 2.98 && S.amps[0] > max_low) max_low = S.amps[0];
        if (rested(0) > 3.1 && S.amps[0] > max_high) max_high = S.amps[0];
        if (!t_up && S.amps[0] > 0.13) t_up = S.ticks;                            /* the current leaves the pre-charge level */
        if (t_up && S.ticks - t_up < SEC(0.05) && S.amps[0] > first) first = S.amps[0];
    }
    printf("  2.70 V cell at a 1.0 A setting: %.3f A below 3.0 V, %.3f A above, at most %.3f A in the first 50 ms above (soft ramp)\n", max_low, max_high, first);
    CHECK(max_low > 0.05 && max_low < 0.13);
    CHECK(max_high > 0.9);
    CHECK(first < 0.6);                                                          /* up through the soft ramp, not in one jump */
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
    S.usb_host = 1;
    boot(); start(); run_for(10);
    S.vcc = 5.15;
    t = time_until(all_off, 2.0);
    S.vcc = 4.55; run_for(30); start(); run_for(10);
    printf("  VCC 5.15 V: outputs off after %.2f s; back to normal and start pressed: still locked off: %s; rail now %.3f V\n", t, all_off() ? "yes" : "NO", S.rail);
    CHECK(t >= 0 && t < 0.5);
    CHECK(all_off());
    CHECK(S.rail < 4.0);                                            /* a fatal stop leaves the rail at its lowest, not its highest */
    CHECK(usb_tx2[54] == SF_VCC_HIGH);                              /* and the host is told why */
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




static int prof_off(void) { return !S.profile; }
static int prof_on(void)  { return S.profile; }

/* a pack on the XT60 alone: strap high, the bypass never comes on */
static void s_xt60_only(void)
{
    S.vin = 0; S.xt60_v = 19.0;
    sim_attach(0, 3.70, 300);
    boot(); start(); run_for(30);
    printf("  XT60 only at 19 V: bypass ever on: %s, charging: %s\n", S.profile_ever ? "YES" : "no", any_pwm() ? "yes" : "NO");
    CHECK(!S.profile_ever && any_pwm());
}

/* started on USB-C, then a pack is plugged into the XT60 and later unplugged */
static void plug_pack(const char *src, double usb, double pack, int keeps_running)
{
    double t;
    S.vin = usb;
    sim_attach(0, 3.70, 300);
    boot(); start(); run_for(10);
    CHECK(S.profile);                                               /* on USB-C the bypass is on, as on the stock firmware */
    S.xt60_v = pack;
    t = time_until(prof_off, 1.0);
    run_for(10);
    printf("  %s, %.1f V pack plugged in: bypass off after %.4f s (fed backwards %.4f s), charging: %s\n", src, pack, t, S.backfeed_s, any_pwm() ? "yes" : "NO");
    CHECK(t >= 0 && S.backfeed_s < 0.04);                          /* the detection, then one probe */
    CHECK(any_pwm());
    S.xt60_v = 0;                                                   /* unplugged, USB-C still in */
    t = time_until(prof_on, 2.0);
    run_for(5);
    printf("  ... pack unplugged: bypass back on after %.3f s, charging: %s\n", t, any_pwm() ? "yes" : "no (input low, start again)");
    CHECK(t >= 0 && t < 1.0);
    if (keeps_running) CHECK(any_pwm());
}
static void s_pack_on_laptop(void) { plug_pack("laptop USB-C 5.0 V", 5.0, 16.8, 0); }
static void s_pack_on_pd(void)     { plug_pack("PD charger 19.5 V", 19.5, 25.2, 1); }

/* a PD charger that starts at 5 V and moves to 20 V, then drops to 5 V and back (a PD reset) mid-charge: no false cut */
static void s_pd_steps(void)
{
    S.vin = 5.0;
    sim_attach(0, 3.70, 300);
    app_boot(); S.booted = 1; run_for(0.6); S.vin = 19.5; run_for(2.0);
    start(); run_for(10);
    S.vin = 5.0; run_for(0.3); S.vin = 19.5; run_for(10);
    printf("  PD 5 V -> 20 V at start, a PD reset mid-charge: bypass on: %s\n", S.profile ? "yes" : "NO");
    CHECK(S.profile);
    S.vin = 5.0; run_for(5);                                        /* the source falls back to 5 V and stays there ... */
    S.xt60_v = 16.8; run_for(5);                                    /* ... then a 4S pack is plugged in */
    printf("  ... PD source back at 5 V for good, then a 16.8 V pack: bypass now %s, fed backwards %.3f s\n", S.profile ? "ON" : "off", S.backfeed_s);
    CHECK(!S.profile && S.backfeed_s < 0.05);
}

/* USB-C and a pack both in at power-up (or a reset with both in): the bypass must stay off whenever the pack is the higher supply */
static void both_at_boot(double usb, double pack, double plug_at)
{
    double b0 = S.backfeed_s;
    S.vin = usb; S.xt60_v = plug_at < 0 ? pack : 0;
    sim_attach(0, 3.70, 300);
    app_boot(); S.booted = 1;
    if (plug_at >= 0) { run_for(plug_at); S.xt60_v = pack; }
    run_for(8); start(); run_for(20);
    printf("  USB-C %.1f V and a %.1f V pack %s: bypass now %s, fed backwards %.3f s, charging: %s\n", usb, pack,
           plug_at < 0 ? "in at power-up" : "plugged during the learning", S.profile ? "on" : "off", S.backfeed_s - b0, any_pwm() ? "yes" : "NO");
    CHECK(S.backfeed_s - b0 < 0.05);                                     /* at most the one probe (about 35 ms) */
    CHECK(any_pwm());
}
static void s_both_at_boot(void)
{
    both_at_boot(5.0, 8.0, -1); both_at_boot(5.0, 15.6, -1); both_at_boot(5.0, 22.8, -1);
    both_at_boot(19.5, 19.8, -1); both_at_boot(19.5, 15.6, -1); both_at_boot(5.0, 16.8, 1.0);
}

/* MAX IN AMPS 0.5 A, 1.0 A setting, three cells that never fill: the limits hold every port all the time; the 3 h limit must still fire */
static void s_held_never_full(void)
{
    int k;
    cfg_load(); settings.ilim = 5; settings.cur = 70; cfg_save();
    for (k = 0; k < 3; k++) sim_attach(k, 4.00, 1000000);
    boot(); start();
    run_for(1.0 * 3600);
    CHECK(any_pwm());
    run_for(8.6 * 3600);                                             /* each port gets 3 h with current; they take turns in the budget */
    printf("  MAX IN AMPS 0.5 A, three cells that never fill: PWM after 9.6 h: %u %u %u (each stopped after 3 h with current)\n", S.pwm[0], S.pwm[1], S.pwm[2]);
    CHECK(!S.pwm[0] && !S.pwm[1] && !S.pwm[2]);
}

/* a 2.7 V cell with an internal short (0.11 A leak): it never comes up past 3.0 V; the 30 min pre-charge limit gives up on it */
static void s_pre_short(void)
{
    sim_attach(0, 2.70, 300); S.cell[0].leak = 0.11;
    boot(); start();
    run_for(25 * 60);
    CHECK(S.pwm[0] > 0);
    run_for(10 * 60);
    printf("  2.7 V cell shorted inside (0.11 A leak): still fed at 25 min, PWM at 35 min: %u, cell %.3f V\n", S.pwm[0], sim_cell_v(0));
    CHECK(S.pwm[0] == 0);
}

/* a damaged settings page: one stored field changed behind the check byte's back loads the defaults, not the damaged value */
static void s_cfg_damage(void)
{
    cfg_load(); settings.volt = 0; settings.cur = 21; settings.usb_off = 1; cfg_save();
    sim_ee_flip(CFG_ADDR + 2, 0x01);                                /* volt 0 -> 1 (4.35 V) */
    cfg_load();
    printf("  settings page with one bit flipped: target index %u, USB power off %u (defaults: 0, 0)\n", settings.volt, settings.usb_off);
    CHECK(settings.volt == 0 && settings.usb_off == 0);
}

/* the board heats up past 60 C, then cools; then the thermistor is unplugged (reads full scale) */
static void s_hot(void)
{
    double t;
    sim_attach(0, 3.80, 300);
    boot(); start(); run_for(10);
    S.therm = 1280; run_for(10);                                    /* 45 C: the fan, charging goes on */
    CHECK(any_pwm() && S.fan);
    S.therm = 780;                                                  /* about 61 C */
    t = time_until(all_off, 3.0);
    S.therm = 1000; run_for(5); start(); run_for(2);                /* 53 C: not cool enough to start again */
    CHECK(!any_pwm());
    S.therm = 1200; run_for(5); start(); run_for(5);                /* 47 C */
    printf("  board at 61 C: outputs off after %.2f s; start at 53 C refused: yes; at 47 C charging again: %s\n", t, any_pwm() ? "yes" : "NO");
    CHECK(t >= 0 && t < 2.0 && any_pwm());
    S.therm = 4095; t = time_until(all_off, 3.0);                   /* the sensor unplugged */
    printf("  thermistor open: outputs off after %.2f s\n", t);
    CHECK(t >= 0 && t < 2.0);
}

/* the rail's own sense reads 10 % low: the rail regulates 10 % high; the port readings (another sensor) must catch the cell going over */
static void s_rail_sense_low(void)
{
    sim_attach(0, 4.00, 300);
    S.adc_scale[ADC_RAIL] = 0.90;
    boot(); start(); run_for(1800);
    printf("  rail sense 10 %% low (rail really about %.2f V): cell peak %.3f V, port PWM now %u (faulted by the port reading: %s)\n", S.rail, S.max_cell_v[0], S.pwm[0], S.pwm[0] ? "NO" : "yes");
    CHECK(S.max_cell_v[0] < 4.27 && S.pwm[0] == 0);
}

/* a 4.45 V cell (overcharged elsewhere) with the storage target: it goes to BLEED, and above 4.40 V it must fault instead (no current, no bleed) */
static void s_bleed_over(void)
{
    sim_attach(0, 4.45, 300);
    S.cell[0].r = 0.02;
    cfg_load(); settings.volt = 2; cfg_save();                       /* 3.80 V storage */
    boot(); start(); run_for(60);
    printf("  4.45 V cell, storage target: PWM %u, bleed switch %u (faulted above 4.40 V: %s)\n", S.pwm[0], S.sw[0], !S.pwm[0] && !S.sw[0] ? "yes" : "NO");
    CHECK(!S.pwm[0] && !S.sw[0]);
}




/* OpenWS3's developer mode is removed: a host asking for it is refused and nothing it sends drives an output */
static void s_no_dev(void)
{
    static const uint8_t enter[] = { 7, 9, 1 };
    uint8_t set[19] = { 8, 0x15, 2, 0x3F, 0, 0, 0x01, 0x90 };
    int k;
    set[18] = 9;
    sim_attach(0, 3.80, 300);
    S.usb_host = 1;
    boot(); usb(enter, 3);
    for (k = 0; k < 20; k++) { usb(set, 19); run_for(0.1); }
    printf("  developer mode requested by a host: refused (result %u), outputs ever driven: %s\n", usb_tx[17], all_off() ? "no" : "YES");
    CHECK(usb_tx[17] != 0 && all_off());
}

/* the measurements against the model: charge put in, resistance, time to full; then the end of the run (finale, summary) */
static void s_stats(void)
{
    double q0, t0, eta_mid = 0, t_mid = 0, t_full;
    sim_attach(0, 3.70, 450);
    S.usb_host = 1;
    boot(); set_current(6); start();
    q0 = S.cell[0].q; t0 = S.ticks / 2000.0;
    while (S.cell[0].q < q0 + 0.30 * S.cell[0].cap_c) run_for(5);          /* a third of the way: the estimate then */
    eta_mid = stats.eta_s[0]; t_mid = S.ticks / 2000.0;
    while (S.pwm[0] && S.ticks / 2000.0 < t0 + 3 * 3600) run_for(1);
    t_full = S.ticks / 2000.0;
    run_for(10);
    printf("  450 mAh cell at 0.6 A: put in %.0f mAh (model %.0f), resistance %u mOhm (model %.0f + polarisation), time to full at a third: %.0f s (took %.0f s), screen %u\n",
           (double)stats.mah[0], (S.cell[0].q - q0) / 3.6, stats.r_mohm[0], S.cell[0].r * 1000, eta_mid, t_full - t_mid, usb_tx[12] >> 4);
    CHECK(fabs(stats.mah[0] - (S.cell[0].q - q0) / 3.6) < 0.08 * (S.cell[0].q - q0) / 3.6);   /* charge counted within 8 % */
    CHECK(stats.r_mohm[0] >= 60 && stats.r_mohm[0] <= 160);
    CHECK(eta_mid > 0.6 * (t_full - t_mid) && eta_mid < 1.6 * (t_full - t_mid));              /* the estimate within -40 % / +60 % */
    CHECK((usb_tx[12] >> 4) == SCR_SUMMARY && stats.packs == 1);                              /* every cell full: the summary */
}

/* full for two hours at 4.35 V: the offer to store them; B2 takes them to 3.85 V */
static void s_store(void)
{
    sim_attach(0, 4.30, 300);
    S.usb_host = 1;
    cfg_load(); settings.hv = 1; settings.volt = 1; cfg_save();   /* 4.35 V, allowed in the menu */
    boot(); start(); run_for(600);
    tap(1);                                                                    /* the summary: any key back */
    run_for(2 * 3600);
    printf("  full for 2 h at 4.35 V: storage offered (screen %u)", usb_tx[12] >> 4);
    CHECK((usb_tx[12] >> 4) == SCR_SUMMARY);
    tap(2); run_for(3600);
    printf(", B2: target index %u, cell now %.3f V\n", settings.volt, sim_cell_v(0));
    CHECK(settings.volt == 3 && sim_cell_v(0) < 3.95);
}

/* a storage run gives the user's target back when it stops, and never saves the storage target */
static void s_store_back(void)
{
    sim_attach(0, 4.30, 300);
    S.usb_host = 1;
    cfg_load(); settings.hv = 1; settings.volt = 1; cfg_save();
    boot(); start(); run_for(600); tap(1);
    run_for(2 * 3600); tap(2); run_for(600);                       /* storage accepted, running at 3.85 V */
    printf("  storage run: target index %u", settings.volt);
    CHECK(settings.volt == 3);
    hold(1); run_for(5);                                           /* stopped */
    printf(", after stop %u", settings.volt);
    CHECK(settings.volt == 1);
    cfg_load();
    printf(", saved %u (1 = 4.35 V)\n", settings.volt);
    CHECK(settings.volt == 1);
}

/* one full cell and one faulted one: no finale or summary over the red cell, no storage offer (whose restart would clear the fault) */
static void s_fault_full(void)
{
    int k, seen = 0;
    sim_attach(0, 4.10, 300); sim_attach(1, 2.30, 300);
    S.usb_host = 1;
    boot(); start();
    for (k = 0; k < 2 * 3600 + 1200; k += 5) { run_for(5); if ((usb_tx[12] >> 4) == SCR_SUMMARY) seen = 1; }
    printf("  a full cell and a faulted one, 2.3 h: ports %u %u (4 full, 6 fault), summary or storage offer ever shown: %s\n",
           stats_in.state[0], stats_in.state[1], seen ? "YES" : "no");
    CHECK(stats_in.state[1] == 6 && !seen);
}

/* a cell that sags a little after full and is topped up: the finale once, then the storage offer after 2 h (not a finale per top-up) */
static void s_sag_finale(void)
{
    int k, shown = 0, was = 0;
    sim_attach(0, 4.10, 300);
    S.cell[0].leak = 0.004;                                        /* 4 mA inside: a top-up now and then */
    S.usb_host = 1;
    boot(); start();
    for (k = 0; k < 3 * 3600; k += 5) {
        int now;
        run_for(5);
        now = (usb_tx[12] >> 4) == SCR_SUMMARY;
        if (now && !was) { shown++; tap(1); }                      /* each time it opens: count it, close it (B1: keep them full) */
        was = (usb_tx[12] >> 4) == SCR_SUMMARY;
    }
    printf("  a cell that sags after full (4 mA leak), 3 h: the summary screen opened %d times (the finale, then the storage offer: 2)\n", shown);
    CHECK(shown == 2);
}

/* a cell over 4.4 V at a low chip supply: the reading is at the top of the ADC's range; it must count as over-voltage */
static void s_saturated(void)
{
    sim_attach(0, 4.46, 300);
    S.vcc = 4.42;                                                  /* the port's full scale about 4.37 V: the cell reads at the top */
    S.usb_host = 1;
    boot(); start(); run_for(20);
    printf("  4.46 V cell, VCC 4.42 V (port full scale about 4.37 V): port state %u (6 = fault), PWM %u, bleed %u\n",
           stats_in.state[0], S.pwm[0], S.sw[0]);
    CHECK(stats_in.state[0] == 6 && !S.pwm[0] && !S.sw[0]);
}

/* the voltage being edited in the menu when a host starts the run: the start is refused, the target cannot change mid-run */
static void s_menu_edit_start(void)
{
    static const uint8_t go[] = { 2, 0, 5 };
    sim_attach(0, 3.9, 120);
    S.usb_host = 1;
    boot();
    both(0.12); both(0.12);                                        /* the menu, CHARGING */
    both(0.12); tap(2);                                            /* editing VOLTAGE: 3.80 V (4.35 V is skipped) */
    usb(go, 3); run_for(1);
    printf("  host start while VOLTAGE is being edited: result %u (1 = refused), charging: %s", usb_tx[17], S.pwm[0] ? "YES" : "no");
    CHECK(usb_tx[17] == 1 && !S.pwm[0]);
    both(0.12); both(1.0); both(1.0); run_for(1);                  /* OK (the edit confirmed), back, back (menu closed, saved) */
    usb(go, 3); run_for(5);
    printf("; after closing the menu: result %u, port active %s at target index %u\n", usb_tx[17], S.pwm[0] || S.sw[0] ? "yes" : "NO", settings.volt);
    CHECK(usb_tx[17] == 0 && S.global && (S.pwm[0] || S.sw[0]) && settings.volt == 2);   /* running at 3.80 V: the 3.9 V cell bleeds */
}

typedef struct { const char *name; void (*fn)(void); } scen_t;
static const scen_t SC[] = {
    { "nominal", s_nominal }, { "vcc_low", s_vcc_low }, { "vcc_mid", s_vcc_mid }, { "vcc_high", s_vcc_high },
    { "bg_plus", s_bg_plus }, { "bg_minus", s_bg_minus }, { "noise", s_noise }, { "noise_vcc", s_noise_vcc },
    { "user_stop", s_user_stop }, { "input_loss", s_input_loss }, { "removal", s_removal }, { "dead_cell", s_dead_cell },
    { "gentle", s_gentle }, { "broken_rail", s_broken_rail }, { "short", s_short }, { "storage", s_storage },
    { "power_cycle", s_power_cycle }, { "never_full", s_never_full }, { "vcc_step", s_vcc_step }, { "leaky", s_leaky },
    { "menu_lock", s_menu_lock }, { "rail_pause", s_rail_pause }, { "vcc_fatal", s_vcc_fatal }, { "selftest_fail", s_selftest_fail },
    { "xt60_only", s_xt60_only }, { "pack_on_laptop", s_pack_on_laptop }, { "pack_on_pd", s_pack_on_pd },
    { "pd_steps", s_pd_steps }, { "both_at_boot", s_both_at_boot }, { "no_dev", s_no_dev },
    { "stats", s_stats }, { "store", s_store }, { "store_back", s_store_back }, { "fault_full", s_fault_full },
    { "sag_finale", s_sag_finale }, { "saturated", s_saturated }, { "menu_edit_start", s_menu_edit_start },
    { "held_never_full", s_held_never_full }, { "pre_short", s_pre_short },
    { "hot", s_hot }, { "cfg_damage", s_cfg_damage }, { "rail_sense_low", s_rail_sense_low }, { "bleed_over", s_bleed_over },
   
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
        if (pid == 0) {
            srand(1234 + k); sim_reset(); SC[k].fn();
            if (S.wdt_bites) printf("  ** the watchdog would have reset the chip %d times\n", S.wdt_bites);
            CHECK(S.wdt_bites == 0);                                /* normal operation, faults included, always feeds it in time */
            fflush(stdout); exit(fails ? (check_report(SC[k].name), 1) : 0);
        }
        waitpid(pid, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st)) { failed++; printf("  ** %s FAILED\n", SC[k].name); }
    }
    printf("sim: %u scenarios failed\n", failed);
    return failed ? 1 : 0;
}

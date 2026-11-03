/* Host unit tests for the pure modules (cal, ctl).  make test */
#include <stdio.h>
#include <stdlib.h>
#include "cal.h"
#include "ctl.h"
#include "fmt.h"
#include "cfg.h"
#include "theme.h"
#include "input.h"
#include "dbgscr.h"
#include "buzz.h"
#include "usbq.h"
#include "stats.h"
#include "usbq.h"
#include "limits.h"
#include <string.h>

#include "check.h"
#define NEAR(a, b, tol) CHECK(abs((int)(a) - (int)(b)) <= (tol))

static void t_cal(void)
{
    NEAR(cal_port_mv(3822), 4199, 2);        /* the 4.20 V target 3822 is 4.20 V */
    NEAR(cal_port_mv(3958), 4349, 2);        /* 4.35 V */
    NEAR(cal_port_mv(3458), 3799, 2);        /* 3.80 V */
    NEAR(cal_port_mv(3504), 3850, 2);        /* 3.85 V */
    NEAR(cal_port_mv(3532), 3880, 2);        /* raw 3532 -> 3.88 V */
    CHECK(cal_port_mv(0) == 0);
    NEAR(cal_port_mv(4095), 4499, 2);
    NEAR(cal_ma_to_port(cal_port_ma(70)), 70, 1);
    CHECK(cal_ma_to_port(1000) == 68);       /* 1 A = 68.25 counts, rounded down */
    NEAR(cal_port_ma(21), 308, 1);           /* iset 21 counts = 0.308 A */
    NEAR(cal_port_ma(91), 1333, 2);
    CHECK(cal_port_ma(0) == 0);
    NEAR(cal_vin_mv(2999), 19700, 40);       /* 19.7 V at a filtered input reading of 2999 */
    CHECK(cal_set_to_ma(21) == 300);
    CHECK(cal_set_to_ma(91) == 1300);
    CHECK(cal_ma_step_up(21) == 28);
    CHECK(cal_ma_step_up(84) == 91);
    CHECK(cal_ma_step_up(91) == CUR_MIN);    /* wraps */
    CHECK(cal_ma_step_up(250) == CUR_MIN);   /* never overflows the 8-bit domain */
}

static void t_clamp(void)
{
    CHECK(ctl_clamp_duty(-5) == 0);
    CHECK(ctl_clamp_duty(0) == 0);
    CHECK(ctl_clamp_duty(PWM_PERIOD) == PWM_PERIOD);
    CHECK(ctl_clamp_duty(PWM_PERIOD + 1) == PWM_PERIOD);
    CHECK(ctl_clamp_duty(1000000L) == PWM_PERIOD);
    CHECK(ctl_clamp_duty(-1000000L) == 0);
}

static void t_port(void)
{
    CHECK(ctl_port_step(200, 21, 25) == 198);       /* too much current -> back off by err/2 */
    CHECK(ctl_port_step(200, 21, 17) == 202);
    CHECK(ctl_port_step(200, 21, 22) == 199);       /* err -1: 200 - 0.5 = 199.5 truncates to 199 (floor) */
    CHECK(ctl_port_step(200, 21, 23) == 199);       /* err -2 */
    CHECK(ctl_port_step(200, 21, 24) == 198);       /* err -3: 198.5 -> 198 */
    CHECK(ctl_port_step(200, 22, 21) == 200);       /* err +1: 200.5 -> 200 */
    CHECK(ctl_port_step(399, 91, 0) == PWM_PERIOD); /* clamps high */
    CHECK(ctl_port_step(3, 0, 100) == 0);           /* clamps low */
    CHECK(ctl_port_step(0, 21, 21) == 0);
}

/* A crude rail plant: ADC8 falls with duty (the slope measured on the bench is shallower; the boot
 * self-test implies >= 2.5 counts/duty with P4.7 on).  The loop must settle without windup. */
static int16_t plant(int16_t duty) { int32_t v = 4333 - 5 * (int32_t)duty / 2; return v > 4095 ? 4095 : (int16_t)v; }

static void t_rail(void)
{
    int16_t duty = 100;
    int i;
    ctl_rail_reset(100);
    for (i = 0; i < 4000; i++) duty = ctl_rail_step(plant(duty), 3958);
    NEAR(plant(duty), 3958, 3);
    CHECK(duty > 0 && duty < PWM_PERIOD);

    /* unreachable setpoint: duty pins at the limit, integral stays bounded, and recovery is quick */
    ctl_rail_reset(100);
    for (i = 0; i < 20000; i++) duty = ctl_rail_step(plant(duty), 3000);
    CHECK(duty == PWM_PERIOD);
    CHECK(labs(ctl_rail_integral()) <= 100000L);
    for (i = 0; i < 4000; i++) duty = ctl_rail_step(plant(duty), 3958);
    NEAR(plant(duty), 3958, 3);

    /* the other end: a rail that can never fall to the setpoint pins duty at 0 */
    ctl_rail_reset(100);
    for (i = 0; i < 20000; i++) duty = ctl_rail_step(4095, 4300);
    CHECK(duty == 0);

    /* at setpoint with zero integral nothing moves */
    ctl_rail_reset(150);
    CHECK(ctl_rail_step(3958, 3958) == 150);

    /* step response: a +30 count disturbance is rejected, with bounded overshoot */
    ctl_rail_reset(150);
    {
        int16_t worst = 0, m;
        for (i = 0; i < 3000; i++) {
            m = plant(duty = ctl_rail_step(plant(duty) + (i > 500 ? 30 : 0), 3958));
            if (i > 600 && abs(m - 3958) > worst) worst = (int16_t)abs(m - 3958);
        }
        NEAR(plant(duty) + 30, 3958, 4);
        CHECK(worst < 40);
    }
}

static void t_fmt(void)
{
    char b[8];
    CHECK(fmt_v2(b, 4197) == 5 && !strcmp(b, "4.20V"));
    CHECK(!strcmp((fmt_v2(b, 0), b), "0.00V"));
    CHECK(!strcmp((fmt_v2(b, 4204), b), "4.20V"));
    CHECK(!strcmp((fmt_v2(b, 4205), b), "4.21V"));   /* round half up */
    CHECK(!strcmp((fmt_v2(b, 65535), b), "9.99V"));   /* clamps, never overruns */
    CHECK(!strcmp((fmt_vin(b, 5000), b), "5.00V") && !strcmp((fmt_vin(b, 19680), b), "19.68V") && !strcmp((fmt_vin(b, 9996), b), "10.00V"));
    CHECK(fmt_vin(b, 65535) == 6 && !strcmp(b, "65.54V"));
    CHECK(!strcmp((fmt_v1(b, 19680), b), "19.7V"));
    CHECK(!strcmp((fmt_v1(b, 5000), b), "5.0V"));
    CHECK(!strcmp((fmt_v1(b, 65535), b), "65.5V"));   /* near uint16 max: must not wrap (int is 16-bit on target) */
    CHECK(!strcmp((fmt_v2(b, 65530), b), "9.99V"));
    CHECK(!strcmp((fmt_a1(b, 300), b), "0.3A"));
    CHECK(!strcmp((fmt_a1(b, 1300), b), "1.3A"));
    CHECK(!strcmp((fmt_a1(b, 10000), b), "10.0A"));
    CHECK(fmt_a1(b, 65535) == 5 && !strcmp(b, "65.5A"));
    CHECK(!strcmp((fmt_a2(b, 312), b), "0.31A") && !strcmp((fmt_a2(b, 0), b), "0.00A") && !strcmp((fmt_a2(b, 65535), b), "9.99A"));
    CHECK(!strcmp((fmt_u(b, 0), b), "0") && !strcmp((fmt_u(b, 65535), b), "65535") && !strcmp((fmt_u(b, 3958), b), "3958"));
}

static void press(uint8_t btn, int ms)
{
    int k;
    for (k = 0; k < ms; k++) input_tick(btn == 1, btn == 2);
    input_tick(0, 0);
}

static void hold(uint8_t b1, uint8_t b2, int ticks)
{
    int k;
    for (k = 0; k < ticks; k++) input_tick(b1, b2);
}

static void t_input(void)
{
    int k, n, t[80], ok;
    input_reset();
    press(1, 9);   CHECK(input_get() == EV_NONE);              /* bounce (< 10 ticks) */
    press(1, 100); CHECK(input_get() == EV_B1); CHECK(input_get() == EV_NONE);
    press(2, 100); CHECK(input_get() == EV_B2);
    press(1, REP_DELAY - 1); CHECK(input_get() == EV_B1);      /* just under the repeat delay: still a tap */
    /* two buttons one after the other are independent */
    press(1, 50); press(2, 50);
    CHECK(input_get() == EV_B1); CHECK(input_get() == EV_B2);
    /* the queue drops events instead of overflowing */
    for (k = 0; k < 10; k++) press(1, 50);
    k = 0; while (input_get() != EV_NONE) k++;
    CHECK(k == 3);

    /* holding: the first repeat after REP_DELAY (no tap on release), then every REP_SLOW x4, REP_MID x8, REP_FAST */
    input_reset();
    n = 0;
    for (k = 1; k <= 5000; k++) { input_tick(0, 1); if (input_get() == EV_B2_REP && n < 80) t[n++] = k; }
    input_tick(0, 0);
    CHECK(input_get() == EV_NONE);
    ok = n > 20 && t[0] == REP_DELAY;
    for (k = 1; ok && k < n; k++) ok = t[k] - t[k - 1] == (k < 5 ? REP_SLOW : k < 13 ? REP_MID : REP_FAST);
    CHECK(ok);
    press(1, REP_DELAY); CHECK(input_get() == EV_B1_REP); CHECK(input_get() == EV_NONE);
    press(1, 100); CHECK(input_get() == EV_B1);                      /* repeat state does not leak into the next press */

    /* chord: B1 first, B2 50 ms later, both released together: one EV_OK, no taps */
    input_reset();
    hold(1, 0, 100); hold(1, 1, 100); input_tick(0, 0);
    CHECK(input_get() == EV_OK); CHECK(input_get() == EV_NONE);
    press(1, 100); CHECK(input_get() == EV_B1);                      /* normal operation resumes */
    /* B2 first, B1 150 ms later; B1 let go first, OK when the last one is up */
    hold(0, 1, 300); hold(1, 1, 100); hold(0, 1, 100); input_tick(0, 0);
    CHECK(input_get() == EV_OK); CHECK(input_get() == EV_NONE);
    /* both held 0.8 s: one EV_BACK, at the threshold, and nothing more on release or while holding on */
    hold(1, 0, 100); hold(1, 1, CHORD_LONG - 1);
    CHECK(input_get() == EV_NONE);
    hold(1, 1, 1);
    CHECK(input_get() == EV_BACK);
    hold(1, 1, 5000); CHECK(input_get() == EV_NONE);
    input_tick(1, 0); hold(1, 0, 2000); input_tick(0, 0);
    CHECK(input_get() == EV_NONE);
    /* a quick press of both is nothing */
    hold(1, 1, 5); input_tick(0, 0);
    CHECK(input_get() == EV_NONE);
    /* the second button too late (> CHORD_WIN) is no chord: two taps */
    hold(1, 0, CHORD_WIN + 100); hold(1, 1, 100); input_tick(0, 0);
    CHECK(input_get() == EV_B1); CHECK(input_get() == EV_B2); CHECK(input_get() == EV_NONE);
    /* B1 already repeating when B2 arrives: not a chord, B2 acts alone */
    input_reset();
    hold(1, 0, REP_DELAY + 20);
    CHECK(input_get() == EV_B1_REP);
    hold(1, 1, 100); input_tick(1, 0); input_tick(0, 0);
    CHECK(input_get() == EV_B2); CHECK(input_get() == EV_NONE);

    /* main screen style: tap, or one long event (B1 350 ms, B2 500 ms) and no tap on release, no repeat */
    input_reset(); input_style(IS_MAIN);
    press(1, 100); CHECK(input_get() == EV_B1); CHECK(input_get() == EV_NONE);
    press(1, B1_LONG - 1); CHECK(input_get() == EV_B1);
    press(1, B1_LONG); CHECK(input_get() == EV_B1_LONG); CHECK(input_get() == EV_NONE);
    press(1, 5000); CHECK(input_get() == EV_B1_LONG); CHECK(input_get() == EV_NONE);
    press(2, B2_LONG - 1); CHECK(input_get() == EV_B2);
    press(2, B2_LONG); CHECK(input_get() == EV_B2_LONG); CHECK(input_get() == EV_NONE);
    hold(1, 0, 100); hold(1, 1, 100); input_tick(0, 0);               /* the chord is the same in both styles */
    CHECK(input_get() == EV_OK); CHECK(input_get() == EV_NONE);
    hold(1, 1, CHORD_LONG + 10); input_tick(0, 0);
    CHECK(input_get() == EV_BACK); CHECK(input_get() == EV_NONE);     /* and no long events from a chord */
    input_reset();
    press(2, REP_DELAY + 10); CHECK(input_get() == EV_B2_REP);        /* input_reset goes back to the menu style */
}

static void t_cfg(void)
{
    CHECK(cfg_valid_current(0xFF) == 21);          /* erased flash */
    CHECK(cfg_valid_ilim(0xFF) == 0 && cfg_valid_ilim(4) == 0 && cfg_valid_ilim(5) == 5 && cfg_valid_ilim(100) == 100 && cfg_valid_ilim(101) == 0);
    CHECK(cfg_valid_vmin(0xFF) == 98 && cfg_valid_vmin(0) == 0 && cfg_valid_vmin(95) == 98 && cfg_valid_vmin(96) == 96 && cfg_valid_vmin(230) == 230 && cfg_valid_vmin(231) == 98);
    CHECK(cfg_valid_lmode(0xFF) == LM_BY_CH && cfg_valid_lmode(0) == 0 && cfg_valid_lmode(2) == 2 && cfg_valid_lmode(3) == LM_BY_CH);   /* unknown -> port 1 first */
    CHECK(cfg_valid_ch_cur(0xFF) == 0 && cfg_valid_ch_cur(0) == 0 && cfg_valid_ch_cur(20) == 0 && cfg_valid_ch_cur(21) == 21 && cfg_valid_ch_cur(91) == 91 && cfg_valid_ch_cur(92) == 0);
    CHECK(cfg_valid_ch_off(0xFF) == 0 && cfg_valid_ch_off(0x40) == 0 && cfg_valid_ch_off(0) == 0 && cfg_valid_ch_off(5) == 5 && cfg_valid_ch_off(0x3F) == 0x3F);   /* erased flash: no port sits out */
    CHECK(cfg_valid_current(20) == 21);
    CHECK(cfg_valid_current(21) == 21);
    CHECK(cfg_valid_current(91) == 91);
    CHECK(cfg_valid_current(92) == 21);
    CHECK(cfg_valid_current(22) == 21 && cfg_valid_current(28) == 28 && cfg_valid_ch_cur(22) == 0);       /* only the steps */
    CHECK(cfg_valid_volt(0xFF) == 0); CHECK(cfg_valid_volt(3) == 3); CHECK(cfg_valid_volt(4) == 0);
    CHECK(cfg_valid_buzzer(0xFF) == 1);            /* erased = buzzer on */
    CHECK(cfg_valid_buzzer(0) == 0); CHECK(cfg_valid_buzzer(1) == 1);
    CHECK(cfg_valid_usb_off(1) == 1 && cfg_valid_usb_off(0) == 0 && cfg_valid_usb_off(0xFF) == 0);
    memset(&settings, 0xFF, sizeof settings); cfg_validate();                     /* erased page: the defaults */
    CHECK(settings.cur == 21 && settings.volt == 0 && settings.sound == 1 && settings.hv == 1);   /* 4.35 V allowed by default */
    CHECK(settings.ilim == 0 && settings.vmin == 98 && settings.vcut == 0 && settings.lmode == LM_BY_CH);
    CHECK(settings.ch_cur[0] == 0 && settings.ch_cur[NPORTS - 1] == 0 && settings.ch_off == 0 && settings.usb_off == 0);
    /* ALLOW 4.35V: on unless switched off (0); a stored 4.35 V target with it off loads as 4.20 V */
    CHECK(cfg_valid_hv(1) == 1 && cfg_valid_hv(0) == 0 && cfg_valid_hv(0xFF) == 1);
    settings.volt = VOLT_4_35; settings.hv = 0; cfg_validate(); CHECK(settings.volt == VOLT_4_20);
    settings.volt = VOLT_4_35; settings.hv = 1; cfg_validate(); CHECK(settings.volt == VOLT_4_35);
    settings.volt = VOLT_3_80; settings.hv = 0; cfg_validate(); CHECK(settings.volt == VOLT_3_80);
}

/* run the melody engine for `ms` ticks (0.5 ms): ticks with a note sounding, note changes, ticks busy */
static void buzz_run(int ms, int *high_ms, int *edges, int *on_ms)
{
    int k, sounding = 0;
    *high_ms = *edges = *on_ms = 0;
    for (k = 0; k < ms; k++) {
        uint8_t c = buzz_tick();
        if (c != TONE_KEEP) { (*edges)++; sounding = c != TONE_OFF; }
        if (sounding) (*high_ms)++;
        if (buzz_busy()) (*on_ms)++;
    }
}

static void t_buzz(void)
{
    int hi, ed, on, k;
    buzz_reset();
    buzz_play(PAT_DONE);                       /* G6 90 ms, gap 40, B6 90, gap 40, E7 240, then 120 ms of crackle: 0.5 ms ticks */
    buzz_run(2000, &hi, &ed, &on);
    NEAR(on, 1241, 3);
    CHECK(hi >= 840 && hi <= 840 + 240);      /* the three notes, and a few crackle notes */
    CHECK(!buzz_busy());

    buzz_reset(); buzz_enable(0);              /* muted by the user: the melody runs, nothing sounds */
    buzz_play(PAT_DONE);
    buzz_run(2000, &hi, &ed, &on);
    CHECK(hi == 0 && on > 0);

    buzz_reset();
    buzz_play(PAT_RAIL_ALARM);                 /* repeats until stopped */
    buzz_run(10000, &hi, &ed, &on);
    CHECK(buzz_busy() && hi > 2000);
    buzz_play(PAT_NONE);
    { uint8_t c1 = buzz_tick(), c2 = buzz_tick();                       /* silenced at once (if a note was on), then nothing */
      CHECK(!buzz_busy() && (c1 == TONE_OFF || c1 == TONE_KEEP) && c2 == TONE_KEEP); }

    buzz_reset();
    buzz_play(PAT_DONE);
    buzz_play(PAT_KEY);                        /* a new melody replaces the running one */
    buzz_run(200, &hi, &ed, &on);
    NEAR(on, 41, 2);

    buzz_reset();                              /* the ROT pack: every melody ends (but the alarm), every note is in the table */
    for (k = PAT_BOOT; k < PAT_COUNT; k++) {
        int t, c;
        if (k == PAT_RAIL_ALARM) continue;
        buzz_play((uint8_t)k);
        for (t = 0; t < 40000 && buzz_busy(); t++) { c = buzz_tick(); CHECK(c == TONE_KEEP || c == TONE_OFF || (c >= 2 && c <= 38)); }
        CHECK(!buzz_busy());
    }
    {                                          /* the crackle at the start of the ROT boot: 0.3 s, now and then a high note, mostly quiet */
        int t, c, notes = 0, offs = 0;
        buzz_play(PAT_BOOT);
        for (t = 0; t < 600; t++) { c = buzz_tick(); if (c >= 23) notes++; else if (c == TONE_OFF) offs++; }
        CHECK(notes >= 2 && notes <= 15 && offs >= 2);
        buzz_enable(0); buzz_play(PAT_BOOT);   /* muted: the crackle is silent too */
        for (t = 0; t < 600; t++) CHECK(buzz_tick() < 2);
        buzz_enable(1);
    }
    CHECK(buzz_note_count(1) == 3185 && buzz_note_count(37) == 398);   /* C5 523 Hz, C8 4186 Hz: 1666667 / f */
}

/* the measurements (stats.h): state of charge, charge counted, resistance, time to full */
static uint16_t vu_of(uint16_t mv) { return (uint16_t)((uint32_t)mv * 4096u / 4500u); }
static uint8_t st_dt = 5;                     /* ms per pass; longer steps keep the 8051 run short */
static void st_feed(uint8_t st, uint16_t mv, uint8_t i, long passes)
{
    long k;                                   /* long: int is 16 bits on the 8051, and a run is far more passes than that */
    for (k = 0; k < passes; k++) {
        stats_in.state[0] = st; stats_in.v[0] = vu_of(mv); stats_in.i[0] = i; stats_in.want[0] = 41; stats_in.held[0] = 0;
        stats_pass(vu_of(4200), 0, st_dt);
    }
}
static void t_stats(void)
{
    int k;
    CHECK(stats_soc(3270, 0) == 0 && stats_soc(4200, 0) == 1000 && stats_soc(3800, 0) == 400 && stats_soc(4350, 1) == 1000);
    CHECK(stats_soc(3820, 0) == 450);                                   /* halfway between 3.80 V (40 %) and 3.84 V (50 %) */
    CHECK(stats_grade(0) == RG_NONE && stats_grade(50) == RG_GOOD && stats_grade(100) == RG_TIRED && stats_grade(200) == RG_RETIRE);
    stats_start();
    for (k = 1; k < NPORTS; k++) stats_in.state[k] = PS_IDLE;
    st_feed(PS_IDLE, 0, 0, 2);
    st_feed(PS_PROBE, 3700, 0, 4);                                      /* resting at 3.70 V */
    st_feed(PS_CHARGE, 3760, 41, 200);                                 /* 0.60 A (41 iu = 600 mA), 60 mV above rest: 100 mOhm */
    CHECK(stats.r_mohm[0] == 0);                                        /* not yet: one second at the working current first */
    st_feed(PS_CHARGE, 3760, 41, 50);
    NEAR(stats.r_mohm[0], 100, 3);
    st_dt = 250; st_feed(PS_CHARGE, 3760, 41, 14400 - 5); st_dt = 5;   /* 3600 s at 0.6 A in all (in 0.25 s steps): 600 mAh */
    NEAR(stats.mah[0], 600, 3);
    CHECK(stats.eta_s[0] == ETA_UNKNOWN);                               /* the voltage never rose: no estimate */
    st_feed(PS_CHARGE, 4000, 41, 2000);                                 /* now it rose to about 69 %: an estimate exists */
    CHECK(stats.eta_s[0] != ETA_UNKNOWN && stats.eta_s[0] > 60);
    st_feed(PS_FULL, 4200, 0, 10);
    CHECK(stats.eta_s[0] == 0 && stats.packs == 1 && stats.weak == 0 && stats.total_mah == stats.mah[0]);
    st_feed(PS_IDLE, 0, 0, 2); st_feed(PS_PROBE, 3700, 0, 2);           /* a new cell: its own numbers */
    CHECK(stats.mah[0] == 0 && stats.r_mohm[0] == 0);
    /* a port held below its working current (a limited supply): no resistance reading, but after 60 s it gives up waiting for one
       and the time to full comes from the default resistance instead of staying blank all charge */
    st_feed(PS_CHARGE, 3740, 30, 13000);                                /* 0.44 A of 0.6 A: never 90 % of it */
    CHECK(stats.r_mohm[0] == 0);
    st_feed(PS_CHARGE, 3740, 30, 1000);                                 /* past 60 s */
    st_feed(PS_CHARGE, 4000, 30, 4000);
    CHECK(stats.r_mohm[0] == 0 && stats.eta_s[0] != ETA_UNKNOWN);
    /* the voltage under load not above the resting one: no reading (not "1 mOhm, GOOD") */
    st_feed(PS_IDLE, 0, 0, 2); st_feed(PS_PROBE, 3800, 0, 4);
    st_feed(PS_CHARGE, 3790, 41, 300);
    CHECK(stats.r_mohm[0] == 0 && stats_grade(stats.r_mohm[0]) == RG_NONE);
}

static void t_dbg(void)
{
    uint8_t k;
    memset(&dbg_view, 0, sizeof dbg_view);
    dbg_view.bandgap = 1073; dbg_view.vin = 768; dbg_view.rail = 3971; dbg_view.rail_duty = 187; dbg_view.adc[0] = 2345;
    dbg_view.adc[ADC_VIN] = 766; dbg_view.adc[ADC_RAIL] = 3975; dbg_view.adc[4] = 3970; dbg_view.adc[9] = 22;     /* raw counts: P4 is ADC4 / ADC9 */
    dbg_view.v[3] = 3966; dbg_view.i[3] = 21; dbg_view.duty[3] = 345; dbg_view.state[3] = 2;
    settings.volt = 1; settings.cur = 21;
    dbg_view.ms = 123456; dbg_view.fan = 1;
    CHECK(dbg_vcc_mv(1073) == 4543); CHECK(dbg_vcc_mv(976) == 4994); CHECK(dbg_vcc_mv(1219) == 3999); CHECK(dbg_vcc_mv(0) == 0);
    CHECK(!strncmp(dbg_text(1, 1), "BANDGAP 1073 counts", 19));
    CHECK(!strncmp(dbg_text(1, 2), "VCC 4.543V", 10));
    CHECK(!strncmp(dbg_text(1, 3), "VIN  766  5.042V", 16));                            /* raw count, corrected volts */
    CHECK(!strncmp(dbg_text(1, 4), "RAIL 3975  4.362V", 17));
    CHECK(!strncmp(dbg_text(1, 5), "RAIL PWM 187/400", 16));
    CHECK(!strncmp(dbg_text(1, 6), "THERM 2345  FAN ON", 18));
    dbg_view.chip_id = 0;                                                               /* version A: no CHIPID block */
    CHECK(!strncmp(dbg_text(1, 7), "CHIP A  NO STORED REF", 21));
    dbg_view.strap = 0; dbg_view.stack = 45;
    dbg_view.byp = 1; CHECK(!strncmp(dbg_text(1, 8), "STRAP 0  BYP ON  STACK 45", 25));
    dbg_view.byp = 2; CHECK(!strncmp(dbg_text(1, 8), "STRAP 0  BYP CUT  STACK 45", 26));       /* 26 characters: the full row */
    dbg_view.byp = 0;
    CHECK(!strncmp(dbg_text(1, 9), "UP 123s ", 8));
    dbg_view.chip_id = 1; dbg_view.chip_bgv = 1187;                                     /* version B: its measured reference */
    dbg_view.chip_date[0] = 0x23; dbg_view.chip_date[1] = 0x05; dbg_view.chip_date[2] = 0x14;
    CHECK(!strncmp(dbg_text(1, 7), "CHIP B  REF 1187mV", 18));
    CHECK(!strncmp(dbg_text(1, 9), "UP 123s  TEST 23-05-14", 22));
    CHECK(!strncmp(dbg_text(2, 4), "P4 3970 4.357", 13));
    CHECK(!strncmp(dbg_text(3, 4), "P4  22  308 D345 CHARGE", 23));
    CHECK(!strncmp(dbg_text(4, 1), "RUN 0  VOLT 1 4.35V", 19));
    CHECK(!strncmp(dbg_text(4, 2), "SET 21  300mA", 13));
    CHECK(!strncmp(dbg_text(4, 5), "USB no host", 11));
    dbg_view.adc[10] = 1234; dbg_view.adc[15] = 1073;
    CHECK(!strncmp(dbg_text(5, 4), "A9:  22 A10:1234 A11:   0", 25));
    CHECK(!strncmp(dbg_text(5, 6), "A15:1073", 8));
    /* every row of every page: exactly DBG_COLS printable characters, NUL terminated */
    for (k = 0; k < DBG_PAGES * DBG_ROWS; k++) {
        const char *t = dbg_text((uint8_t)(1 + k / DBG_ROWS), (uint8_t)(k % DBG_ROWS));
        int n = 0, bad = 0;
        while (t[n]) { if (t[n] < 32 || t[n] > 126) bad = 1; n++; }
        CHECK(n == DBG_COLS && !bad);
    }
    CHECK(!strncmp(dbg_text(1, 0), "1/6 ", 4) && !strncmp(dbg_text(6, 0), "6/6 ", 4));
}

/* the one look (ROT): text must be readable on every background it is drawn on, the meaning colours must stay distinct */
static void t_theme(void)
{
    uint8_t k;
    CHECK(th(R_TEXT) != th(R_BG) && th(R_TEXT) != th(R_BOX) && th(R_VOLT) != th(R_BOX) && th(R_DIM) != th(R_BOX));
    CHECK(th(R_BOX) != th(R_BG));
    CHECK(th(R_GOOD) != th(R_BAD) && th(R_WARN) != th(R_BAD) && th(R_GOOD) != th(R_WARN) && th(R_INFO) != th(R_BAD));
    for (k = 0; k < 6; k++) CHECK(th(R_BADGE0 + k) != th(R_BG));
    CHECK(th(R_FULLTX) != th(R_FULL) && th(R_FULL) != th(R_BOX) && th(R_FULL) != th(R_BG));     /* a full cell stands out, its text reads */
    CHECK(th(R_DOT0) != th(R_BOX) && th(R_DOT0 + PS_FAULT) == th(R_BAD));                    /* a shut eye shows; a fault is red */
    CHECK(th_get() == TH_ROT);
}

/* VCC correction: a 4.20 V cell must read 4.20 V whatever VCC is, within the counts' rounding */
/* the USB slot (usbq.h, REQUIREMENTS.md SR-C4): one waiting, the first one wins, a short report 0-filled, taken once */
static void t_usbq(void)
{
    uint8_t c[USB_CMD], k, *q;
    usbq_reset();
    CHECK(!usbq_take(c));
    q = usbq_open(); CHECK(q != 0); for (k = 0; k < USB_CMD; k++) q[k] = (uint8_t)(100 + k); usbq_close();
    CHECK(!usbq_open());                                                           /* a second while one waits: dropped */
    CHECK(usbq_take(c) && c[0] == 100 && c[USB_CMD - 1] == 100 + USB_CMD - 1);
    CHECK(!usbq_take(c));                                                          /* taken once */
    q = usbq_open(); CHECK(q != 0); q[0] = 1; q[1] = 0; q[2] = 9; usbq_close();   /* a stop, 3 bytes */
    CHECK(!usbq_open());                                                           /* a ping behind it: dropped, the stop stays */
    CHECK(usbq_take(c) && c[0] == 1 && c[2] == 9 && c[3] == 0 && c[USB_CMD - 1] == 0);    /* the old bytes read 0 */
}

static void t_vcc(void)
{
    static const double ratio[] = { 4.40 / 4.55, 1.0, 4.65 / 4.55, 4.80 / 4.55, 4.99 / 4.55 };   /* VCC over the VCC of the scales */
    unsigned k;
    CHECK(cal_vcc_gain(BG_NOM * 8) == 4096);                          /* at the calibration point nothing changes */
    CHECK(cal_vcc_fix(3822, 4096) == 3822);
    for (k = 0; k < sizeof ratio / sizeof ratio[0]; k++) {
        uint16_t bg8 = (uint16_t)(BG_NOM * 8 / ratio[k] + 0.5);     /* a higher VCC reads the fixed reference lower */
        uint16_t raw = (uint16_t)(3822 / ratio[k] + 0.5);           /* ... and the cell lower */
        int fixed = cal_vcc_fix(raw, cal_vcc_gain(bg8));
        CHECK(fixed >= 3822 - 3 && fixed <= 3822 + 3);                /* 3 vu = 3.3 mV */
    }
    /* the uncorrected reading at VCC 4.99 V would be 9 % low: the charger would push the cell to about 4.6 V */
    CHECK((uint16_t)(3822 / (4.99 / 4.55) + 0.5) < 3822 - 300);
    /* absurd bandgap readings are clamped, and nothing overflows */
    CHECK(cal_vcc_gain(0) == cal_vcc_gain(BG8_MIN) && cal_vcc_gain(0xFFFF) == cal_vcc_gain(BG8_MAX));
    CHECK(cal_vcc_fix(4095, cal_vcc_gain(0)) > 4095 && cal_vcc_fix(4095, cal_vcc_gain(0)) < 6000);
}

int main(void)
{
    t_vcc();
    t_usbq();
    t_cal(); t_clamp(); t_port(); t_rail(); t_fmt(); t_input(); t_cfg(); t_buzz(); t_stats(); t_dbg(); t_theme();
    return check_report("pure");
}

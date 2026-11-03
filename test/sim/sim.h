/* A simulated WhoopStor V3 board for running the real firmware (app.c and everything it calls) on a PC.
 * Only the hardware layer is replaced: hal.h, lcd.h and usb.h are implemented here against a model of the board.  Time is virtual,
 * in 0.5 ms ticks; it advances when the firmware waits (hal_ticks, hal_delay_ms) and while it draws (the panel's bus rate). */
#ifndef SIM_H
#define SIM_H
#include <stdint.h>
#include "board.h"

typedef struct {
    double q, cap_c;            /* charge (coulombs) and capacity */
    double r, r2, tau, vp;      /* series ohms; polarisation: ohms, time constant (s), its voltage now */
    double leak;                /* A drained inside the cell all the time (a damaged cell self-discharging) */
    int present;
} sim_cell_t;

typedef struct {
    /* ---- the world, set by a scenario ---- */
    double vin;                 /* input voltage at the connector */
    double vcc;                 /* the chip's VCC (the ADC reference) */
    double bg_err;              /* bandgap off its nominal, as a fraction (chip to chip spread) */
    double adc_scale[16];       /* per-channel gain error (1.0 = exact) */
    double adc_noise;           /* counts, uniform +- */
    double src_r;               /* the input source's resistance, ohms (0 = stiff): a weak supply sags under load */
    double rail_stuck;          /* > 0: the rail ignores its PWM and sits at this voltage (a broken buck) */
    double short_amps[NPORTS];  /* > 0: the port draws this much whatever the duty (a shorted cell or output) */
    int btn1, btn2;             /* buttons held */
    sim_cell_t cell[NPORTS];

    /* ---- the board's outputs, as the firmware set them ---- */
    uint16_t pwm[NPORTS], rail_duty;
    uint8_t sw[NPORTS], global, power, profile, profile_ever, fan;
    double  xt60_v;                     /* a supply on the XT60 / DC jack, 0 = none; S.vin is USB-C, 0 = none */
    uint8_t tone;                       /* the note the buzzer plays (buzz.h: 2 + n), 0 = silent */
    uint8_t usb_host;                   /* 1: a host is connected (the telemetry is filled) */
    uint16_t therm;                     /* thermistor counts (0 = room temperature) */
    double  backfeed_s;                 /* time with the bypass on while the XT60 has a supply: fed into the USB-C connector */

    /* ---- the plant's state ---- */
    double rail;                /* true rail voltage */
    double amps[NPORTS];        /* true current into each cell (charge positive, bleed negative) */
    double iin;                 /* true input current, A (the charge power at 90 % efficiency) */
    double vin_node;            /* the voltage at the input node, after the source's sag */
    uint64_t ticks;             /* 0.5 ms */
    double lcd_us;              /* display time not yet turned into ticks */
    int ee_erases;              /* settings page erases (flash wear) */
    double lcd_slow;            /* extra display time, a multiple of the bus rate (0: none): long passes */

    /* ---- what the scenario checks ---- */
    double max_cell_v[NPORTS];  /* the highest cell voltage (open circuit + polarisation, what a meter reads after a short rest) */
    double max_rail_after_boot;
    double max_rail_driven;     /* the highest rail while a port is driven (P5.2 on and a PWM above 0) */
    int booted;
    int reboot_isp;
    int wdt_on, wdt_bites;      /* the watchdog was started; times it would have reset the chip */
    uint64_t wdt_last;
} sim_t;

extern sim_t S;

void   sim_reset(void);
void   sim_advance(uint32_t ticks);
int    sim_usb_send(const uint8_t *c, int n);   /* 1: taken; 0: dropped (one was already waiting) */
void sim_ee_flip(uint16_t a, uint8_t mask);  /* damage one stored bit pattern (settings page) */
double sim_cell_v(int p);
double term_v_of(int p);                       /* the voltage at the port's pins (what the charger reads) */                     /* the cell's own voltage (open circuit + polarisation) */
void   sim_attach(int p, double v0, double cap_mah);
double sim_ocv_of(double frac);
#endif

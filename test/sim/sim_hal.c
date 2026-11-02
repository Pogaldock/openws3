/* The simulated board: hal.h, lcd.h and usb.h for the firmware, and the plant behind them.  See sim.h. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "sim.h"
#include "hal.h"
#include "lcd.h"
#include "usb.h"
#include "input.h"
#include "buzz.h"
#include "ports.h"

sim_t S;

#define DT        0.0005                   /* s per tick */
#define VPC       (4.5 / 4096)            /* volts per port-voltage count at the VCC of the scales */
#define VCC_CAL   (1.19 * 4096 / 1071)    /* that VCC: 4.551 V */
#define K_AMP     0.005                   /* A per PWM count, before the rail limits it */
#define R_PASS    0.15                    /* ohms in each port's path */
#define R_BLEED   18.0
#define R_SUPPLY  0.05                    /* rail sag per amp drawn */
#define RAIL_TOP  5.0                     /* rail at PWM 0 (the rail PWM is inverted: more duty = lower rail) ... */
#define RAIL_PER  0.004                   /* ... volts per count */

static const double OCV_S[] = { 0, .02, .08, .30, .60, .85, .97, 1.0 }, OCV_V[] = { 2.5, 3.2, 3.55, 3.70, 3.82, 4.00, 4.12, 4.20 };

double sim_ocv_of(double x)
{
    int k;
    if (x <= 0) return 2.5 + x * 20;                    /* below the curve: falls fast (an over-discharged cell) */
    if (x >= 1.0) return 4.2 + (x - 1.0) * 3.0;         /* overcharge: rises fast */
    for (k = 0; k < 7; k++) if (x <= OCV_S[k + 1]) return OCV_V[k] + (OCV_V[k + 1] - OCV_V[k]) * (x - OCV_S[k]) / (OCV_S[k + 1] - OCV_S[k]);
    return 4.2;
}

double sim_cell_v(int p) { sim_cell_t *c = &S.cell[p]; return sim_ocv_of(c->q / c->cap_c) + c->vp; }

void sim_attach(int p, double v0, double cap_mah)
{
    sim_cell_t *c = &S.cell[p];
    double lo = -0.2, hi = 1.3;
    int n;
    c->cap_c = cap_mah * 3.6; c->r = 0.08; c->r2 = 0.04; c->tau = 20; c->vp = 0; c->present = 1;
    for (n = 0; n < 60; n++) { double m = (lo + hi) / 2; if (sim_ocv_of(m) < v0) lo = m; else hi = m; }
    c->q = (lo + hi) / 2 * c->cap_c;
}

void sim_reset(void)
{
    int k;
    memset(&S, 0, sizeof S);
    S.vin = 5.15; S.vcc = VCC_CAL;
    for (k = 0; k < 16; k++) S.adc_scale[k] = 1.0;
}

static double term_v(int p)                               /* the voltage at the port's pins */
{
    sim_cell_t *c = &S.cell[p];
    if (!c->present) return S.global ? 0.3 : 0.0;         /* an empty port floats up a little with P5.2 on (HARDWARE.md) */
    return sim_cell_v(p) + S.amps[p] * c->r;
}

static void plant_tick(void)
{
    int p;
    double total = 0, top, target;
    for (p = 0; p < NPORTS; p++) {
        sim_cell_t *c = &S.cell[p];
        double a = 0;
        if (!c->present) { S.amps[p] = 0; continue; }
        if (S.short_amps[p] > 0) a = S.short_amps[p];
        else if (S.global && S.power && S.pwm[p]) {
            double want = K_AMP * S.pwm[p];
            double room = (S.rail - sim_cell_v(p)) / (c->r + R_PASS);
            a = want < room ? want : (room > 0 ? room : 0);
        }
        if (S.sw[p]) a -= term_v(p) / R_BLEED;
        S.amps[p] += (a - S.amps[p]) * 0.5;                 /* ~1 ms lag */
        c->q += (S.amps[p] - c->leak) * DT;
        c->vp += (S.amps[p] * c->r2 - c->vp) * DT / c->tau;
        if (S.amps[p] > 0) total += S.amps[p];
        if (sim_cell_v(p) > S.max_cell_v[p]) S.max_cell_v[p] = sim_cell_v(p);
    }
    top = S.vin - 0.3 < RAIL_TOP ? S.vin - 0.3 : RAIL_TOP;
    target = !S.power ? 0 : S.rail_stuck > 0 ? S.rail_stuck : top - RAIL_PER * S.rail_duty;
    if (target < 0) target = 0;
    S.rail += (target - S.rail) * DT / 0.002;
    S.rail -= total * R_SUPPLY * DT / 0.002;               /* sag under load, recovered by the regulator */
    if (S.rail < 0) S.rail = 0;
    if (S.booted && S.rail > S.max_rail_after_boot) S.max_rail_after_boot = S.rail;
    if (S.global) { for (p = 0; p < NPORTS; p++) if (S.pwm[p]) { if (S.rail > S.max_rail_driven) S.max_rail_driven = S.rail; break; } }
}

void sim_advance(uint32_t n)
{
    while (n--) {
        S.ticks++;
        plant_tick();
        input_tick((uint8_t)S.btn1, (uint8_t)S.btn2);      /* what the 0.5 ms interrupt does */
        (void)buzz_tick();
    }
}

/* ---- hal.h ---- */
void hal_init(void) {}
void hal_irq_on(void) {}
uint8_t hal_stack_free(void) { return 99; }
void hal_pwm(uint8_t port, uint16_t duty) { S.pwm[port] = duty; }
void hal_rail(uint16_t duty) { S.rail_duty = duty; }
void hal_sw(uint8_t port, uint8_t on) { S.sw[port] = on; }
void hal_global(uint8_t on) { S.global = on; }
void hal_power(uint8_t on) { S.power = on; }
void hal_profile(uint8_t on) { S.profile = on; }
void hal_profile_allow(void) {}
void hal_reboot_isp(void) { S.reboot_isp = 1; }
void hal_all_off(void) { int k; for (k = 0; k < NPORTS; k++) { S.pwm[k] = 0; S.sw[k] = 0; } S.global = 0; S.rail_duty = 0; }

static double noise(void) { return S.adc_noise ? ((double)rand() / RAND_MAX * 2 - 1) * S.adc_noise : 0; }
static uint16_t counts(double c, int ch)
{
    c = c * VCC_CAL / S.vcc * S.adc_scale[ch] + noise();
    if (c < 0) c = 0;
    if (c > 4095) c = 4095;
    return (uint16_t)(c + 0.5);
}

uint16_t hal_adc(uint8_t ch)
{
    int p;
    if (ch == ADC_BANDGAP) return counts(1071.0 * (1 + S.bg_err), ch);
    if (ch == ADC_IIN) return 2000;                          /* thermistor: cool */
    if (ch == ADC_VIN) return counts(S.vin * 152.5 - 1, ch);
    if (ch == ADC_RAIL) return counts(S.rail / VPC, ch);
    for (p = 0; p < NPORTS; p++) {
        if (ch == port_v_ch(p)) return counts(term_v(p) / VPC, ch);
        if (ch == port_i_ch(p)) return counts((S.amps[p] > 0 ? S.amps[p] : 0) * 68.25, ch);   /* net current, clamped at 0 (HARDWARE.md) */
    }
    return 0;
}

uint8_t hal_chip_byte(uint8_t n)
{
    uint16_t bgv = (uint16_t)(1190.0 * (1 + S.bg_err) + 0.5);
    switch (n & 31) {
    case 7: return (uint8_t)(bgv >> 8);
    case 8: return (uint8_t)bgv;
    case 25: return 0x00;
    case 27: return 0x23; case 28: return 0x05; case 29: return 0x14;
    case 31: return 0x5A;
    default: return 0x11;
    }
}

static uint8_t ee[1024];
uint8_t hal_ee_read(uint16_t a) { return ee[a & 1023]; }
void hal_ee_erase(uint16_t a) { memset(ee + (a & 1023 & ~511), 0xFF, 512); }
void hal_ee_write(uint16_t a, uint8_t v) { ee[a & 1023] &= v; }
uint32_t hal_ms(void) { return (uint32_t)(S.ticks / 2); }
uint32_t hal_ticks(void) { sim_advance(1); return (uint32_t)S.ticks; }     /* a busy wait on the tick moves time on */
void hal_delay_ms(uint16_t ms) { sim_advance((uint32_t)ms * 2); }
void hal_tick(void) {}
void hal_tone(uint8_t level) { (void)level; }
void hal_fan(uint8_t on) { S.fan = on; }
uint8_t hal_btn(uint8_t n) { return n == 1 ? (uint8_t)S.btn1 : (uint8_t)S.btn2; }
uint8_t hal_strap(void) { return 0; }

/* ---- lcd.h: nothing is drawn, but the time the bus takes is: 210 clocks per byte at 40 MHz, 2 bytes per pixel, ~11 bytes per window ---- */
static void lcd_cost(uint32_t pixels, uint32_t windows)
{
    S.lcd_us += pixels * 10.5 + windows * 58.0;
    if (S.lcd_us >= 500) { uint32_t t = (uint32_t)(S.lcd_us / 500); S.lcd_us -= t * 500.0; sim_advance(t); }
}
static uint32_t slen(const char *s) { uint32_t n = 0; while (s[n]) n++; return n; }
void lcd_init(void) { hal_delay_ms(260); lcd_cost(160 * 80, 1); }
void lcd_wake(void) {}
void lcd_reconfig(void) { lcd_cost(0, 12); }
void lcd_fill(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t c) { (void)x; (void)y; (void)c; lcd_cost((uint32_t)w * h, 1); }
void lcd_text(uint8_t x, uint8_t y, const char *s, uint8_t sx, uint8_t sy, uint16_t fg, uint16_t bg)
{ (void)x; (void)y; (void)fg; (void)bg; lcd_cost(slen(s) * 6u * sx * 8u * sy, slen(s)); }
void lcd_noise(uint8_t x, uint8_t y, uint8_t w, uint8_t h, uint16_t seed) { (void)x; (void)y; (void)seed; lcd_cost((uint32_t)w * h, 1); }
void lcd_noise_row(uint8_t y, uint16_t seed, const uint8_t *bits, uint8_t bx, uint8_t bw, uint8_t keep, uint16_t fg)
{ (void)y; (void)seed; (void)bits; (void)bx; (void)bw; (void)keep; (void)fg; lcd_cost(LCD_PX_W, 1); }
void lcd_bitmap(uint8_t x, uint8_t y, uint8_t w, uint8_t h, const uint8_t *bits, uint16_t fg, uint16_t bg)
{ (void)x; (void)y; (void)bits; (void)fg; (void)bg; lcd_cost((uint32_t)w * h, 1); }

/* ---- usb.h: no host ---- */
uint8_t usb_tx[USB_REPORT], usb_tx2[USB_REPORT];
void usb_init(void) {}
void usb_isr(void) {}
uint8_t usb_configured(void) { return 0; }
uint8_t usb_cmd_get(uint8_t *cmd) { int i; if (!S.usb_pending) return 0; for (i = 0; i < USB_CMD; i++) cmd[i] = S.usb_cmd[i]; S.usb_pending = 0; return 1; }

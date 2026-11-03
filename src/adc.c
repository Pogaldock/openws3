#include "adc.h"
#include "hal.h"
#include "ports.h"
#include "cal.h"

#define SLOW_EVERY 8
#define FULL       4092       /* a raw mean this close to 4095 is at full scale: the input is beyond what the ADC measures */

static uint16_t XD raw[NADC], filt[NADC];       /* raw: counts as converted; filt: the filtered counts (adc.h) */
static uint16_t XD gain;                        /* cal_vcc_gain: 4096 * BG_NOM / bandgap (no initialiser: README build rule 1) */
static uint16_t XD bg8;                         /* the bandgap count x 8 (cal_vcc_gain's unit), from the latest reading */

static uint8_t  XD slow_div;
static uint32_t XD vg_t0;                        /* input guard: when the learning started (hal_ms) */
static uint16_t XD vg_ref, vg_low;              /* the USB-C level learnt, passes back below it */
static uint8_t  XD vg_learn;                     /* 1 while the level is being learnt */
static uint8_t  XD vg_hi, vg_state;             /* passes above it; VG_* (adc.h) */

/* Input bypass guard.  The board starts the bypass when USB-C powers it (strap low).  The bypass conducts both ways, so a supply put
   on the XT60 / DC jack afterwards would be fed backwards into the USB-C source (a laptop port, a phone charger).  The USB-C level is
   learnt in the first VG_SETTLE_MS (the PD contract included: the highest reading); after that a USB-C source does not rise
   by itself, so the input VG_RISE_MV above it for VG_HI_PASSES passes means a second supply: the bypass goes off.  It comes back when
   the input is VG_BACK_MV below the learnt level for VG_LOW_PASSES passes: then only USB-C through the diode is left.  A level learnt
   at boot that is no USB PD voltage (5, 9, 15, 20 V), or any reading above the highest PD level, means a pack was already on the
   XT60: off until the next power-up.  Off is the
   safe side: the diode still carries USB-C power (0.6 V lower).  A failed conversion (4095) reads as 27 V: off. */
#define VG_SETTLE_MS   3000
#define VG_PD_MAX_MV   21600            /* 20 V + 5 % (PD tolerance; Apple's 20.3 V) + 3 % (the VIN divider, unverified) */
#define VG_RISE_MV     1000
#define VG_HI_PASSES   3
#define VG_BACK_MV     300
#define VG_LOW_PASSES  10

static uint8_t pd_level(uint16_t mv)
{
    static const CODE uint8_t L[4] = { 5, 9, 15, 20 };
    uint8_t k;
    uint16_t v;
    for (k = 0; k < 4; k++) { v = (uint16_t)L[k] * 1000u; if ((uint32_t)mv * 100u >= (uint32_t)v * 85u && (uint32_t)mv * 100u <= (uint32_t)v * 108u) return 1; }   /* -15 % (cable, diode) .. +8 % */
    return 0;
}

static void vin_guard(void)
{
    uint16_t mv = cal_vin_mv(cal_vcc_fix(hal_adc(ADC_VIN), gain));
    if (vg_learn) {
        if (mv > vg_ref) vg_ref = mv;
        if (mv > VG_PD_MAX_MV) vg_state = VG_LATCHED;                 /* no USB-C source gives this */
        if (hal_ms() - vg_t0 >= VG_SETTLE_MS) { vg_learn = 0; if (!pd_level(vg_ref)) vg_state = VG_LATCHED; }
    } else if (vg_state == VG_OK) {
        if (mv > vg_ref + VG_RISE_MV) { if (++vg_hi >= VG_HI_PASSES) { vg_state = VG_CUT; vg_low = 0; } } else vg_hi = 0;
    } else if (vg_state == VG_CUT) {
        if (mv + VG_BACK_MV < vg_ref) { if (++vg_low >= VG_LOW_PASSES) { vg_state = VG_OK; vg_hi = 0; } } else vg_low = 0;
    }
    hal_bypass_ok(vg_state == VG_OK);
}

uint8_t adc_bypass_state(void) { return vg_state; }

uint16_t adc_now(uint8_t ch)
{
    ch &= 0x0F;
    if (ch == ADC_BANDGAP || ch == ADC_IIN) return raw[ch];   /* the reference itself, and the thermistor (ratiometric to VCC) */
    return cal_vcc_fix(raw[ch], gain);                        /* everything else is corrected for VCC (cal.h) */
}

/* the mean of 2^sh samples (12-bit values: the sums fit 16 bits), and a first-order filter on the reading made from it */
static void conv(uint8_t ch, uint8_t sh)
{
    uint16_t s = 0;
    uint8_t k;
    for (k = 0; k < (uint8_t)(1u << sh); k++) s += hal_adc(ch);
    raw[ch] = s >> sh;
    if (ch == ADC_BANDGAP && raw[ch]) {
        bg8 = raw[ch] << 3;                                   /* no smoothing: a lagging gain lets the rail regulator chase a VCC step */
        gain = cal_vcc_gain(bg8);
    }
    filt[ch] = (filt[ch] + adc_now(ch) + 1) >> 1;
}

void adc_scan_slow_now(void)
{
    conv(ADC_VIN, 2); conv(ADC_IIN, 2);
}

void adc_scan(void)
{
    uint8_t k;
    conv(ADC_BANDGAP, 1);                            /* every pass and first: the VCC correction of the readings below follows VCC at once */
    vin_guard();
    for (k = 0; k < NPORTS; k++) { conv(port_v_ch(k), 1); conv(port_i_ch(k), 1); }
    conv(ADC_RAIL, 1);
    if (++slow_div >= SLOW_EVERY) { slow_div = 0; adc_scan_slow_now(); }
}

void adc_prime(void)
{
    uint8_t ch;
    gain = 4096; bg8 = 0; vg_t0 = hal_ms(); vg_learn = 1; vg_ref = 0; vg_low = 0; vg_hi = 0; vg_state = VG_OK;
    adc_scan(); adc_scan_slow_now();
    for (ch = 0; ch < NADC; ch++) filt[ch] = adc_now(ch);
    adc_scan(); adc_scan_slow_now();
}

uint16_t adc_raw(uint8_t ch) { return raw[ch & 0x0F]; }                     /* plain counts, uncorrected */
uint16_t adc_get(uint8_t ch) { return filt[ch & 0x0F]; }
uint8_t  adc_saturated(uint8_t ch) { return raw[ch & 0x0F] >= FULL; }

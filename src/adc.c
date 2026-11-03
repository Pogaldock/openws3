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
static uint32_t XD vg_t0, vg_tj;                /* input guard: start of the learning, last time the input jumped (hal_ms) */
static uint16_t XD vg_ref, vg_off, vg_mv;       /* level with the bypass on (the USB-C level), level with it off, this reading */
static uint8_t  XD vg_n, vg_hi, vg_lo, vg_state;   /* probe passes, passes above / far below the level, VG_* (adc.h) */

/* Input bypass guard.  The bypass (P5.3) is requested at start when USB-C powers the board (strap low).  It conducts both ways, so a
   supply on the XT60 / DC jack that is higher than the USB-C source would be fed backwards into it (a laptop port, a phone charger).
   With the bypass off the input reads the higher of the XT60 supply and USB-C less the diode (about 600 mV, HARDWARE.md).
   LEARN  bypass off; wait until the input has been steady (within VG_JUMP_MV) for VG_STEADY_MS, and at start for VG_START_MS at
          least (a PD contract settles), then PROBE.  A PD source that moves up later is learnt again the same way.
   PROBE  bypass on for VG_PROBE passes (about 18 ms).  If USB-C is the highest source the input rises by the diode's drop: OK, its
          level is noted.  If not, a higher supply is on the XT60: HOLD.  A probe is the only time such a supply reaches the USB-C
          side (under 20 ms), and it happens once per change of the input.
   OK     bypass on.  A USB-C source does not rise by itself (the level is the unloaded one; load lowers it), so the input VG_RISE_MV
          above the level for VG_HI_PASSES passes means a supply arrived on the XT60, or a PD source moved up: bypass off, LEARN.
   HOLD   bypass off; the input falling (the supply unplugged, or a pack sagging under load) goes back to LEARN.
   Off is the safe side everywhere: the diode still carries USB-C power, 0.6 V lower.  A failed conversion (4095) reads as 27 V. */
#define VG_START_MS      1500
#define VG_STEADY_MS     150
#define VG_JUMP_MV       200
#define VG_PROBE         4                      /* passes (about 18 ms): 2 to settle, 2 measured */
#define VG_DIODE_MV      200                    /* the rise that shows USB-C is the source (the diode drops about 600 mV) */
#define VG_RISE_MV       400
#define VG_HI_PASSES     3
#define VG_DROP_MV       2000
#define VG_LO_PASSES     3                      /* passes: a fall of 2 V is a source change, never load or noise */
#define VG_FALL_PASSES   22                     /* about 0.1 s */

static void vg_learn(void) { vg_state = VG_LEARN; vg_tj = hal_ms(); vg_off = vg_mv; }

void adc_guard_start(void)                      /* once per power-up, before the first scan (app.c) */
{
    vg_t0 = vg_tj = hal_ms(); vg_ref = vg_off = vg_mv = 0; vg_n = vg_hi = vg_lo = 0; vg_state = VG_LEARN;
    hal_bypass_ok(0);
}

static void vin_guard(void) REENT                 /* reentrant: its 32-bit time temporaries go on the stack, not the full internal RAM */
{
    vg_mv = cal_vin_mv(cal_vcc_fix(hal_adc(ADC_VIN), gain));
    switch (vg_state) {
    case VG_LEARN:
        if (vg_mv > vg_off + VG_JUMP_MV || vg_mv + VG_JUMP_MV < vg_off) { vg_tj = hal_ms(); vg_off = vg_mv; }
        if (hal_ms() - vg_t0 >= VG_START_MS && hal_ms() - vg_tj >= VG_STEADY_MS) { vg_off = vg_mv; vg_ref = 0; vg_n = 0; vg_state = VG_PROBE; }
        break;
    case VG_PROBE:
        if (++vg_n > VG_PROBE / 2 && vg_mv > vg_ref) vg_ref = vg_mv;
        if (vg_n >= VG_PROBE) { if (vg_ref >= vg_off + VG_DIODE_MV) { vg_state = VG_OK; vg_hi = 0; } else { vg_state = VG_HOLD; vg_lo = 0; } }
        break;
    case VG_OK:
        if (vg_mv > vg_ref + VG_RISE_MV) { if (++vg_hi >= VG_HI_PASSES) vg_learn(); } else vg_hi = 0;
        /* far below the level: not load (that sags by tenths of a volt) but a new USB-C level (a PD source reset to 5 V).  Bypass off at
           once and learnt again: with a pack on the XT60 between the old and the new USB-C level, the pack would otherwise feed the USB-C
           source through the bypass (found by the fuzzer: 20 V PD, a 16.8 V pack, the PD source back to 5 V, fed for a second) */
        if (vg_mv + VG_DROP_MV < vg_ref) { if (++vg_lo >= VG_LO_PASSES) { vg_lo = 0; vg_learn(); } } else vg_lo = 0;
        break;
    default:                                    /* VG_HOLD: a higher reading is still the XT60 supply; a fall may mean it has gone */
        if (vg_mv > vg_off) vg_off = vg_mv;
        /* a fall that lasts: one noisy low sample under a peak that crept up with the noise would probe again and again */
        if (vg_mv + VG_JUMP_MV < vg_off) { if (++vg_lo >= VG_FALL_PASSES) { vg_lo = 0; vg_learn(); } } else vg_lo = 0;
        break;
    }
    hal_bypass_ok(vg_state == VG_OK || vg_state == VG_PROBE);
}

uint8_t adc_bypass_state(void) { return vg_state; }
uint16_t adc_gain(void) { return gain; }

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
    gain = 4096; bg8 = 0;
    adc_scan(); adc_scan_slow_now();
    for (ch = 0; ch < NADC; ch++) filt[ch] = adc_now(ch);
    adc_scan(); adc_scan_slow_now();
}

uint16_t adc_raw(uint8_t ch) { return raw[ch & 0x0F]; }                     /* plain counts, uncorrected */
uint16_t adc_get(uint8_t ch) { return filt[ch & 0x0F]; }
uint8_t  adc_saturated(uint8_t ch) { return raw[ch & 0x0F] >= FULL; }

/*
 * Calibration: ADC readings (adc.h) -> engineering units.  Pure functions, no hardware.
 *
 * Status of each constant is marked.  Anything not [VERIFIED] should be checked on the hardware
 * before the number is trusted for a safety decision.
 */
#ifndef CAL_H
#define CAL_H
#include "compat.h"

/* Port voltage sense (ADC1..6) in mV: 1 vu = 4.5 V / 4096.  [VERIFIED: the 4.20 / 3.80 / 3.85 V targets sit at
 * 3822 / 3458 / 3504 on this scale.]  The hardware divider is still to be confirmed with a DMM. */
uint16_t cal_port_mv(uint16_t raw);

/* Port current sense (ADC 12,14,13,9,10,11) in mA: 1 iu = 1/68.25 A = 14.652 mA.  [VERIFIED: (raw+1)*4/273 A.  The regulator works on
 * this value, so the current setting is in iu too.] */
uint16_t cal_port_ma(uint16_t raw);
uint16_t cal_ma_to_port(uint16_t ma);              /* inverse, rounded down */

/* Input voltage in mV.  [UNVERIFIED] 19.7 V at raw 3000 fits
 * (raw+1)*4/61 in 0.1 V; i.e. ~26.9 V full scale.  Needs a bench sweep. */
uint16_t cal_vin_mv(uint16_t raw);

/* VCC correction.  Every ADC count is relative to VCC, which follows the supply (about 4.55 V on USB-C, unknown on XT60 / DC).  The
 * internal 1.19 V bandgap read on the same ADC gives the ratio: gain = 4096 * BG_NOM / bandgap, and corrected = raw * gain / 4096.
 * At the VCC the scales were made at (bandgap BG_NOM) nothing changes.  The bandgap itself differs by about 1 % from chip to chip,
 * which is the error left: far smaller than the up to 10 % a VCC between 4.55 V and the 5.0 V fatal limit would give uncorrected
 * (a cell charged to 4.6 V instead of 4.2 V).  bg8 is the bandgap count x 8 (smoothed, adc.c); it is clamped to the range the supply
 * supervision allows plus a margin, so a bad reading cannot make an absurd gain. */
#define BG_NOM      1071
#define BG8_MIN     (900 * 8)
#define BG8_MAX     (1250 * 8)
uint16_t cal_vcc_gain(uint16_t bg8);               /* 4096 = no correction */
uint16_t cal_vcc_fix(uint16_t raw, uint16_t gain); /* raw * gain / 4096, rounded */

/* Current setting in iu: nominally 0.1 A = 7 iu (really 7/68.25 = 0.1026 A), range 21..91. */
#define CUR_UNITS_PER_100MA  7
#define CUR_MIN              21
#define CUR_MAX              91
uint16_t cal_set_to_ma(uint8_t units);          /* 21 -> 300 mA */
uint8_t  cal_cur_valid(uint8_t units);          /* a setting the steps can reach: 21, 28 ... 91 */
uint8_t  cal_ma_step_up(uint8_t units);         /* +0.1 A, wraps to CUR_MIN after CUR_MAX */

#endif

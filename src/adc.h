/*
 * ADC scanner.  Conversion time on the hardware is not measured yet, so a pass converts only what the control needs:
 *   fast (every pass):  the bandgap (VCC, first), 6 port voltages, 6 port currents, the rail - 2 samples each
 *   slow (every 8th):   VIN, ADC0 - 4 samples each
 *
 * Values are the ADC counts as converted (the mean of the samples), 0..4095 relative to VCC.  A count is in a fixed unit of the measured
 * quantity:
 *               vu  4.5 V / 4096 = 1.0986 mV   port voltage and rail channels
 *               iu  1/68.25 A    = 14.65 mA    port current channels
 *             and cal.h for VIN.  The control works on these.
 * Every channel except the bandgap and ADC0 is corrected for VCC (cal.h, cal_vcc_gain): a count is relative to VCC, which follows the
 * supply, and an uncorrected port reading would make the charge voltage follow it too (up to 10 % high below the 5.0 V VCC limit).
 * The correction leaves the bandgap's chip-to-chip spread, about 1 %.  The thermistor (ADC0) divider sits on VCC, so it stays a plain
 * count.  adc_raw() and adc_saturated() are always the plain counts.
 *   adc_now() is the latest value, adc_get() a light filter on it: (old + new) / 2 per scan of that channel.
 */
#ifndef ADC_H
#define ADC_H
#include "compat.h"
#include "board.h"

void     adc_scan(void);              /* one pass worth of conversions (fast, plus slow when due) */
void     adc_scan_slow_now(void);
uint16_t adc_raw(uint8_t ch);         /* the latest counts (same as adc_now) */
uint16_t adc_now(uint8_t ch);         /* the latest value */
uint16_t adc_get(uint8_t ch);         /* the filtered value */
uint16_t adc_gain(void);              /* the VCC correction (cal_vcc_gain): also the highest corrected reading, about */
uint8_t  adc_saturated(uint8_t ch);   /* the latest counts are at full scale: the input is beyond what the ADC measures */
void     adc_prime(void);             /* scan everything twice so filtered values start at the real readings */

enum { VG_OK = 0, VG_HOLD, VG_LEARN, VG_PROBE };   /* input guard (adc.c): bypass allowed / off, a higher supply is on the XT60 /
                                                    off, learning the input level / on for a moment, testing which source is highest */
void     adc_guard_start(void);
uint8_t  adc_bypass_state(void);

#endif

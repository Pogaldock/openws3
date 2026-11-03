/* The real hal.c, built by the firmware's compiler, on the simulated 8051 (s51): what hal_all_off writes into the chip's registers.
 * The whole-firmware simulation replaces hal.c with a model, so this is where its register values are checked (REQUIREMENTS.md
 * SR-F5).  s51 does not know the STC8H's PWM, but it keeps whatever is written to xdata and to the SFRs, so the values can be read
 * back: a fatal stop must leave every port's PWM at 0, every bleed switch and the global enable off, and the rail actuator at
 * full duty (inverted: full duty is the LOWEST rail; 0 was the highest). */
#include <stdio.h>
#include <stdint.h>
#include "board.h"
#include "hal.h"
#include "cfg.h"

cfg_t XD settings;                               /* hal.c reads one setting (DISABLE USB POWER); cfg.c itself needs the flash */

#define XB(a)  (*(volatile uint8_t __xdata *)(a))
__sbit __at (0xCA) G_EN;                         /* P5.2, the global enable (hal.c GLOBAL_EN) */
__sfr  __at (0xBA) P_SW2_T;                      /* the extended registers (xdata 0xFExx) are reached with P_SW2.7 set */

__sfr __at (0xA0) P2_T;  __sfr __at (0xB0) P3_T;  __sfr __at (0xC0) P4_T;  __sfr __at (0xC8) P5_T;
__sfr __at (0x96) P2M0_T; __sfr __at (0x95) P2M1_T; __sfr __at (0xB2) P3M0_T; __sfr __at (0xB1) P3M1_T;
__sfr __at (0xB4) P4M0_T; __sfr __at (0xB3) P4M1_T; __sfr __at (0xCA) P5M0_T; __sfr __at (0xC9) P5M1_T;
__sfr __at (0xC1) WDT_T;

void hal_test_set_ticks(uint32_t t);             /* hal.c, built with HAL_TEST */

static const uint16_t ccr[NPORTS] = PORT_CCR_INIT;
static uint8_t p2, p3, p4, p5, m2, m3, m4, m5;   /* the pins as the start-up code left them, before the test touches anything */
static int fails;

static void check(int ok, const char *what) { if (!ok) { printf("FAIL %s\n", what); fails++; } }
static uint16_t rd16(uint16_t a) { return (uint16_t)((uint16_t)XB(a) << 8 | XB(a + 1)); }

/* a pin "driven low": its latch 0 and its mode push-pull (M0 = 1, M1 = 0), so it pulls low rather than floating */
static int driven_low(uint8_t latch, uint8_t m0, uint8_t m1, uint8_t bit) { return !(latch & bit) && (m0 & bit) && !(m1 & bit); }

static void t_startup(void)
{
#if (__SDCC_VERSION_MAJOR * 100 + __SDCC_VERSION_MINOR) < 403
    printf("start-up: skipped, SDCC before 4.3 calls _sdcc_external_startup, not hal.c's __sdcc_external_startup (build with 4.6)\n");
#else
    /* what __sdcc_external_startup (the first code to run after a reset) left: every pin that can switch power driven low */
    printf("start-up: P2 %02X P3 %02X P4 %02X P5 %02X, modes P2 %02X/%02X P3 %02X/%02X P4 %02X/%02X P5 %02X/%02X\n",
           p2, p3, p4, p5, m2, P2M1_T, m3, P3M1_T, m4, P4M1_T, m5, P5M1_T);
    check(driven_low(p2, m2, P2M1_T, 0xFF), "P2 (port PWMs, enables of ports 2 and 3) driven low at start-up");
    check(driven_low(p3, m3, P3M1_T, 0x08), "P3.3 (rail actuator) driven low at start-up");
    check(driven_low(p4, m4, P4M1_T, 0x20 | 0x04 | 0x08 | 0x10 | 0x80), "P4 port enables and POWER_EN driven low at start-up");
    check(driven_low(p5, m5, P5M1_T, 0x04 | 0x08), "P5.2 (GLOBAL_EN) and P5.3 (USB-C bypass) driven low at start-up");
#endif
}

static void t_wdt(void)
{
    hal_wdt_feed();
    printf("watchdog: WDT_CONTR %02X\n", WDT_T);
    check((WDT_T & 0x20) && (WDT_T & 0x07) == 0x07, "the watchdog on, at its longest prescale (2.5 s at 40 MHz)");
}

static void t_adc_timeout(void)
{
    /* s51 has no ADC: the conversion never finishes, as on a chip whose ADC has failed.  The read must give up with full scale,
       which the supervision reads as an over-voltage (safe), not hang the firmware */
    uint16_t v = hal_adc(8);
    printf("ADC that never finishes: read %u\n", v);
    check(v == 4095, "an ADC read that never finishes returns full scale");
}

static void t_clock_wrap(void)
{
    uint32_t a, b;
    hal_test_set_ticks(0xFFFFFF00UL); a = hal_ms();            /* just before the 2^32-tick wrap (24.9 days) */
    hal_test_set_ticks(0x00000100UL); b = hal_ms();            /* 512 ticks (256 ms) later */
    printf("clock across the 24.9-day wrap: %lu ms then %lu ms (difference %ld)\n", a, b, (int32_t)(b - a));
    check((int32_t)(b - a) == 256, "the ms clock keeps counting across the tick wrap");
    hal_test_set_ticks(0x7FFFFF00UL); a = hal_ms(); hal_test_set_ticks(0x80000100UL); b = hal_ms();
    check((int32_t)(b - a) == 256, "and across the tick counter's half-way point");
}

int test_main(void)
{
    uint8_t k;
    p2 = P2_T; p3 = P3_T; p4 = P4_T; p5 = P5_T; m2 = P2M0_T; m3 = P3M0_T; m4 = P4M0_T; m5 = P5M0_T;   /* before anything else */
    t_startup(); t_wdt(); t_adc_timeout(); t_clock_wrap();
    P_SW2_T |= 0x80;
    hal_rail(0); hal_global(1);                  /* the worst state to stop from: rail at its highest, every port driven */
    for (k = 0; k < NPORTS; k++) { hal_pwm(k, 300); hal_sw(k, 1); }
    check(rd16(RAIL_CCR) == 0, "the test set the rail duty to 0 first");
    check(rd16(ccr[0]) == 300, "the test set port 1's duty first");
    hal_all_off();
    printf("after hal_all_off: rail duty %u (PWM_PERIOD %u = lowest rail), global %u, port duties", rd16(RAIL_CCR), PWM_PERIOD, G_EN);
    for (k = 0; k < NPORTS; k++) printf(" %u", rd16(ccr[k]));
    printf("\n");
    check(rd16(RAIL_CCR) == PWM_PERIOD, "the rail at full duty (lowest) after a fatal stop");
    check(G_EN == 0, "the global enable off");
    for (k = 0; k < NPORTS; k++) check(rd16(ccr[k]) == 0, "every port's duty 0");
    hal_rail(PWM_PERIOD + 100);
    check(rd16(RAIL_CCR) == PWM_PERIOD, "a rail duty over the period is clamped to it");
    printf("hal: %d failures\n", fails);
    return fails;
}

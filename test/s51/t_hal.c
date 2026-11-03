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

static const uint16_t ccr[NPORTS] = PORT_CCR_INIT;
static int fails;

static void check(int ok, const char *what) { if (!ok) { printf("FAIL %s\n", what); fails++; } }
static uint16_t rd16(uint16_t a) { return (uint16_t)((uint16_t)XB(a) << 8 | XB(a + 1)); }

int test_main(void)
{
    uint8_t k;
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

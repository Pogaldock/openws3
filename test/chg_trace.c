/* Differential test of the charge logic: one integer-only scenario, compiled for the PC and for the 8051 (run in the s51 simulator,
 * 16-bit int), printing the same trace.  Any line that differs between the two is arithmetic the chip does differently.
 *   PC:    make trace          8051: see test/run_8051.sh */
#include <stdio.h>
#include <stdint.h>
#include "chg.h"
#include "sup.h"

static chg_in_t XD in;
static chg_out_t XD out;
static uint32_t XD q[NPORTS];          /* each cell's charge, arbitrary units: its voltage is 3000 + q / 64 vu, capped at 4100 */
static uint8_t  XD present[NPORTS], shorted[NPORTS];
static uint16_t XD forced_v[NPORTS];   /* nonzero: the port reads this voltage whatever the cell does */

static uint16_t cell_v(uint8_t p)
{
    uint32_t v;
    if (forced_v[p]) return forced_v[p];
    v = 2000u + q[p] / 64u;
    return v > 4100u ? 4100u : (uint16_t)v;
}

static void step(void)
{
    uint8_t p;
    for (p = 0; p < NPORTS; p++) {
        uint16_t v = cell_v(p), room, a = 0;
        if (!present[p]) { in.v_now[p] = 0; in.i_now[p] = 0; continue; }
        if (shorted[p]) a = 200;                                       /* 2.9 A whatever the duty */
        else if (!out.sw[p] && out.duty[p]) {
            room = in.rail > v ? (uint16_t)((in.rail - v) / 2u) : 0u; /* current the rail allows (iu) */
            a = out.duty[p] / 4u;
            if (a > room) a = room;
        }
        if (out.sw[p] && q[p] > 4u) q[p] -= 4u;                         /* bleed (slow, so the trace sees it) */
        q[p] += a;
        in.v_now[p] = (uint16_t)(cell_v(p) + a / 8u);                   /* the reading sees a little series drop */
        in.i_now[p] = a;
    }
    chg_pass(&in, &out);
}

int main(void)
{
    uint32_t n;
    uint8_t p, k;
    in.tgt = sup_target(VOLT_4_20); in.setpoint = sup_setpoint(VOLT_4_20); in.rail = in.setpoint; in.run = 1; in.cut = NPORTS;
    for (p = 0; p < NPORTS; p++) { in.iset[p] = 42; in.en[p] = 1; present[p] = 1; }
    q[0] = 64UL * 1500UL;        /* 3.5 V-ish: a normal charge */
    q[1] = 64UL * 1870UL;        /* near full */
    q[2] = 64UL * 1880UL;        /* 4.27 V: above the target, under the 4.40 V fault: bleeds down */
    q[3] = 64UL * 600UL;         /* below 3.0 V: the gentle start */
    forced_v[4] = 2094;        /* 2.3 V: refused */
    q[5] = 64UL * 1500UL;        /* shorted later */

    chg_init();
    for (n = 0; n < 40000u; n++) {
        if (n == 9000u) shorted[5] = 1;
        if (n == 9600u) shorted[5] = 0;
        if (n == 20000u) { present[0] = 0; }                           /* pulled */
        step();
        if (n % 250u == 0u) {
            printf("%u", (unsigned)(n / 250u));
            for (k = 0; k < NPORTS; k++) printf(" %u/%u/%u/%u", out.state[k], out.duty[k], out.v[k], out.i[k]);
            printf("\n");
        }
    }
    printf("END\n");

    return 0;
}

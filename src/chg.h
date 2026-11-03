/*
 * Per-port charge controller.  Pure integer logic: no hardware, time = number of control passes (4.5 ms).
 *
 * Thresholds are in the units of the ADC readings (adc.h): port voltage in 4.5 V/4096 (1.0986 mV), current in 1/68.25 A, and
 * are named after what they mean.
 *
 * Per port the state is:
 *   IDLE    no cell (V < 1.00 V) or not running                         duty 0, switch low
 *   PROBE   output off, settle, classify the cell                        duty 0, switch low
 *   CHARGE  constant current with soft ramp; ends in the rest test: after the tail (the rail sits just above the target, so it holds the
 *           cell there and the current tapers), or with no current at all for 2 s (a pulled cell)
 *   REST    output off for ~1 s, then the rest voltage decides like a probe (an empty port reads below 1 V and is released)
 *   FULL    cell inside the target window; watched: below it for 2 s = test again, above it = bleed
 *   BLEED   cell above the target: switch HIGH (discharge path) down to the target, then the rest test
 *   FAULT   over-voltage / over-current latched; cleared only by removing the cell or stopping
 */
#ifndef CHG_H
#define CHG_H
#include "compat.h"
#include "board.h"
#include "sup.h"

enum { PS_IDLE = 0, PS_PROBE, PS_CHARGE, PS_REST, PS_FULL, PS_BLEED, PS_FAULT };

/* events (one-shot, per port) for the UI / buzzer */
enum { CE_NONE = 0, CE_FULL = 1, CE_FAULT = 2 };

typedef struct {
    uint16_t v_now[NPORTS];      /* latest port voltage reading (adc.h: 4.5 V/4096 units) */
    uint16_t i_now[NPORTS];      /* latest port current reading (1/68.25 A units) */
    uint16_t rail;               /* filtered rail reading */
    uint16_t tgt;                /* target voltage T (vu) */
    uint16_t setpoint;           /* rail setpoint S of that voltage */
    uint8_t  iset[NPORTS];       /* per-port current setting (iu) */
    uint8_t  run;                /* charging requested */
    uint8_t  en[NPORTS];         /* the port takes part in the run (0: it stays idle, its output off) */
    /* the user limits' gate (limits.h) */
    uint8_t  lim;                /* a limit is in force: a duty rises at most one count per pass, and only on the ports in rise */
    uint8_t  rise;               /* bit k: port k may raise its duty this pass */
    uint8_t  cut;                /* the port to bring down this pass (NPORTS = none) ... */
    uint8_t  excess;             /* ... by about this much current (iu) */
    uint8_t  late;               /* control passes lost since the last one (a long pass): the time limits count them too, so they
                                    run on real time, not on passes (0 normally; the app caps it) */
} chg_in_t;

typedef struct {
    uint16_t duty[NPORTS];       /* PWM compare, 0..PWM_PERIOD */
    uint8_t  sw[NPORTS];         /* switch pin: 0 = charge path, 1 = bleed path */
    uint8_t  state[NPORTS];
    uint8_t  event[NPORTS];      /* CE_* raised on the pass it happens */
    uint16_t v[NPORTS];          /* filtered voltage (vu) */
    uint16_t i[NPORTS];          /* filtered current (iu) */
    uint8_t  any_active;         /* some port is charging or bleeding (global enable P5.2 must be on) */
    uint8_t  want[NPORTS];       /* CHARGE: the working current (iu), else 0 */
    uint8_t  up[NPORTS];         /* CHARGE, below the working current and the duty can still rise: about what one more count adds (iu,
                                    at least 1); else 0 */
    uint8_t  held[NPORTS];       /* CHARGE: the limits hold the port below its working current (now or within the last second):
                                    1, or 2 = and it draws nothing (waiting); else 0 */
} chg_out_t;

void chg_init(void);
void chg_pass(const chg_in_t XD *in, chg_out_t XD *out);
void chg_abort(void);            /* drop everything to the idle state (stop) */
void chg_cut(void);              /* rail trip: duties to zero and the soft ramp restarts, port states and timers are kept */

/* thresholds, in vu / iu, exposed for tests and documentation */
#define V_DEAD            910    /* 1.00 V: below this there is no cell */
#define V_REFUSE          2276   /* 2.50 V: a cell resting below this is over-discharged: not charged, the port faults */
#define V_PRE             2731   /* 3.00 V: below this a cell charges at I_PRE only (a gentle start for a deeply discharged cell) */
#define I_PRE             7      /* 0.1 A */
#define V_PRE_EXIT        2777   /* 3.05 V: pre-charge ends here, not at V_PRE: the reading is taken under current, and a damaged cell's
                                    resistance lifts it (0.1 A x 0.5 ohm = 50 mV); a healthy cell's barely */
#define T_CHG_MAX_S       10800  /* 3 h of charging one cell, top-ups included (not counting time held by the limits): the port faults */
#define T_PRE_MAX_S       1800   /* 30 min below V_PRE (at I_PRE) in one fill: a cell that does not come up is damaged: the port faults */
#define T_BLEED_MAX_S     18000  /* 5 h of bleeding one cell: a discharge path that does not bring it down: the port faults (a storage
                                    run takes 60 % off a full cell at about 0.2 A: 5 h covers cells up to about 1600 mAh) */
#define I_ZERO_MAX        4      /* the current offset measured with the output off is used up to this (58 mA); more is not an offset */
#define V_SATURATED       4600   /* a port reading at the ADC's full scale is passed on as this (5.05 V): over every fault threshold */
#define PASSES_PER_S      222
#define V_FAULT           4004   /* 4.40 V: over-voltage latch while bleeding */
#define I_TAIL            5      /* tail-current threshold (73 mA) */
#define PASS_TICKS        9      /* control pass: 9 x 0.5 ms = 4.5 ms */

#endif

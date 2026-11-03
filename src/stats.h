/*
 * Per-port measurements for the display (WS3 Unlocked): charge put in (mAh), the cell's resistance, an estimate of the time to
 * full, and a summary of the run.  Pure: fed with the charge controller's outputs every pass.  It only reads: nothing here
 * changes how a port charges.
 */
#ifndef STATS_H
#define STATS_H
#include "compat.h"
#include "board.h"
#include "chg.h"

#define ETA_UNKNOWN 0xFFFF            /* not enough charge in yet to estimate */

/* resistance grades, mOhm.  Measured at the port pins one second into the charge, so it includes the pack's lead and plug and a
 * little polarisation: compare packs with each other on this charger, not with a lab meter. */
#define R_TIRED   90
#define R_RETIRE  150
enum { RG_NONE = 0, RG_GOOD, RG_TIRED, RG_RETIRE };

typedef struct {
    uint16_t mah[NPORTS];             /* charge put in this fill (top-ups of the same cell included) */
    uint16_t r_mohm[NPORTS];          /* resistance measured at the start of the fill, 0 = not measured */
    uint16_t eta_s[NPORTS];           /* estimated seconds to the target, ETA_UNKNOWN; 0 when the port is full */
    uint16_t total_mah;               /* the run: all ports */
    uint8_t  packs;                   /* ports that reached full in this run */
    uint8_t  weak;                    /* the port with the highest resistance, NPORTS = none measured */
} stats_t;

extern stats_t XD stats;

void    stats_start(void);                                            /* a run starts: everything from zero */
extern chg_out_t XD stats_in;                                         /* the charge controller's outputs, copied in by the app */
void    stats_pass(uint16_t tgt, uint8_t volt, uint16_t dt_ms);      /* every control pass, after stats_in is set */
uint8_t stats_grade(uint16_t r_mohm);                                 /* RG_* */
uint16_t stats_soc(uint16_t mv, uint8_t volt);                        /* resting voltage -> state of charge, per mille (exposed for tests) */

#endif

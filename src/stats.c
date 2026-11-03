#include <string.h>
#include "stats.h"
#include "cal.h"

stats_t XD stats;
chg_out_t XD stats_in;

#define ACC_PER_MAH    61425u        /* 1 mAh = 3.6e6 mA.ms = 245700 iu.ms (1 iu = 1/68.25 A); counted in units of 4 iu.ms */
#define R_MIN_I        14            /* iu (0.2 A): below this the resistance step is too small to read */
#define R_STABLE       222           /* passes (1 s) at the working current before the reading is taken */
#define R_GIVE_UP      13333         /* passes (60 s) without a stable current: no measurement for this fill */
#define R_DEFAULT      60            /* mOhm used for the estimate until the cell's own is known */
#define R_COMP_MAX     240           /* mOhm: the estimate's correction is capped here (16-bit arithmetic) */
#define ETA_MIN_SOC    50            /* per mille of charge put in before the time to full is estimated */
#define ETA_TAIL_S     60            /* the end of a charge: the taper's last minutes and the rest test */

/* resting voltage at 0, 10 .. 100 % for a LiPo (4.20 V full) and a LiHV (4.35 V full) cell, mV.  Typical curves: the estimate is only
 * as good as how close the cell is to them. */
static const CODE uint16_t OCV[2][11] = {
    { 3270, 3690, 3730, 3770, 3800, 3840, 3870, 3950, 4020, 4110, 4200 },
    { 3300, 3720, 3770, 3810, 3850, 3900, 3950, 4040, 4130, 4230, 4350 },
};

typedef struct {
    uint16_t acc;                     /* charge not yet counted as a whole mAh, 4 iu.ms */
    uint16_t v0, soc0;                /* resting voltage before the fill (mV) and its state of charge (per mille, 0 = not started) */
    uint16_t rt;                      /* resistance: passes in the current phase */
    uint16_t sec_ms, secs;            /* ms into the current second; seconds since the fill started */
    uint8_t  prev, ron, full;
} pst_t;

static pst_t XD P[NPORTS];
/* the port being worked on, copied out of P and back in one block each: fixed addresses make small 8051 code, and nothing here is a
 * parameter or a temporary in the (full) direct RAM */
static pst_t XD w;
static uint16_t XD mv, ma, soc, tsoc, r, eta_v, v, i, u;
static uint32_t XD t32;
static uint8_t  XD k, st, lihv, want, held, s_dt, curve;
static uint16_t XD s_tgt;

uint16_t stats_soc(uint16_t m, uint8_t volt)
{
    static const CODE uint16_t *XD c;
    static uint8_t XD j;
    static uint16_t XD lo, hi;
    curve = volt == 1 || volt == 3;
    c = OCV[curve];
    if (m <= c[0]) return 0;
    if (m >= c[10]) return 1000;
    for (j = 1; j < 10; j++) { hi = c[j]; if (m < hi) break; }
    lo = c[j - 1]; hi = c[j];
    hi -= lo; m -= lo; m *= 100u; m /= hi;                             /* (m - lo) <= 420 mV: x 100 fits 16 bits */
    return (uint16_t)(j - 1) * 100u + m;
}

uint8_t stats_grade(uint16_t q)
{
    if (!q) return RG_NONE;
    return q < R_TIRED ? RG_GOOD : q < R_RETIRE ? RG_TIRED : RG_RETIRE;
}

void stats_start(void)
{
    memset(P, 0, sizeof P);
    for (k = 0; k < NPORTS; k++) { stats.mah[k] = 0; stats.r_mohm[k] = 0; stats.eta_s[k] = ETA_UNKNOWN; }
    stats.total_mah = 0; stats.packs = 0; stats.weak = NPORTS;
}

/* once a second while charging: the state of charge from the voltage less the drop across the cell's resistance, and the time to
 * the target at the rate it has risen since the fill started (x 1.25: the taper at the end is slower) */
static void estimate(uint16_t tgt)
{
    ma = cal_port_ma(i);
    u = r ? r : R_DEFAULT; if (u > R_COMP_MAX) u = R_COMP_MAX;
    u /= 4; u *= ma / 10u; u /= 25u;                                    /* mA x mOhm / 1000 = mV, in 16 bits */
    mv = cal_port_mv(v);
    mv = mv > u ? mv - u : 0;
    soc = stats_soc(mv, lihv);
    tsoc = stats_soc(cal_port_mv(tgt), lihv);
    if (w.ron || ma < 20 || soc < w.soc0 + ETA_MIN_SOC) return;         /* the resistance first; too little current, or too early */
    t32 = ETA_TAIL_S;
    if (soc < tsoc) { t32 = (uint32_t)(tsoc - soc) * w.secs; t32 /= (uint16_t)(soc - w.soc0); t32 += t32 / 4u + ETA_TAIL_S; }
    u = t32 > 60000u ? 60000u : (uint16_t)t32;
    if (eta_v == ETA_UNKNOWN) eta_v = u;
    else { eta_v -= eta_v / 4u; eta_v += u / 4u; }                      /* smoothed */
}

void stats_pass(uint16_t tgt, uint8_t volt, uint16_t dt_ms)
{
    lihv = volt; s_tgt = tgt; s_dt = dt_ms > 255 ? 255 : (uint8_t)dt_ms;   /* external RAM: kept through the loop without direct-RAM copies */
    stats.total_mah = 0; stats.weak = NPORTS;
    for (k = 0; k < NPORTS; k++) {
        memcpy(&w, &P[k], sizeof w);
        st = stats_in.state[k]; v = stats_in.v[k]; i = stats_in.i[k]; want = stats_in.want[k]; held = stats_in.held[k];
        r = stats.r_mohm[k]; eta_v = stats.eta_s[k];
        if (w.prev == PS_IDLE && st == PS_PROBE) {                          /* a new cell: its own numbers */
            memset(&w, 0, sizeof w); stats.mah[k] = 0; r = 0; eta_v = ETA_UNKNOWN;
        }
        if (st == PS_PROBE) w.v0 = cal_port_mv(v);                          /* the output is off: the cell's resting voltage */
        if (w.prev == PS_PROBE && st == PS_CHARGE && !w.soc0) {             /* the first charge of the fill, from rest */
            w.soc0 = stats_soc(w.v0, lihv); if (!w.soc0) w.soc0 = 1;
            w.ron = !r; w.rt = 0; w.secs = 0;
        }
        if (st == PS_CHARGE) {
            u = i > 255 ? 255 : i; u *= s_dt; w.acc += (u + 2u) / 4u;      /* charge put in, rounded to the nearest unit */
            if (w.acc >= ACC_PER_MAH) { w.acc -= ACC_PER_MAH; stats.mah[k]++; }
            if (w.ron) {                                                    /* the resistance, one second into the working current */
                mv = i * 10u; ma = want * 9u;
                if (i >= R_MIN_I && mv >= ma && !held) {
                    if (++w.rt >= R_STABLE) {
                        mv = cal_port_mv(v); ma = cal_port_ma(i) / 10u;
                        r = 1;
                        if (mv > w.v0) { mv -= w.v0; if (mv > 655) mv = 655; r = mv * 100u / ma; }    /* mV x 1000 / mA */
                        if (r > 999) r = 999;
                        w.ron = 0;
                    }
                } else if (++w.rt >= R_GIVE_UP) w.ron = 0;
                else if (w.rt < R_STABLE) w.rt = 0;
            }
            w.sec_ms += s_dt;
            if (w.sec_ms >= 1000) { w.sec_ms -= 1000; if (w.secs < 0xFFFF) w.secs++; estimate(s_tgt); }
        } else if (st == PS_FULL) {
            eta_v = 0;
            if (!w.full) { w.full = 1; stats.packs++; }
        } else if (st == PS_IDLE) w.soc0 = 0;
        w.prev = st;
        memcpy(&P[k], &w, sizeof w);
        stats.r_mohm[k] = r; stats.eta_s[k] = eta_v;
        stats.total_mah += stats.mah[k];
        if (r) { if (stats.weak == NPORTS) stats.weak = k; else if (r > stats.r_mohm[stats.weak]) stats.weak = k; }
    }
}

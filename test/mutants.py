#!/usr/bin/env python3
"""
Mutation testing for WS3 Unlocked: are the safety checks really tested?

Each entry breaks one safety check on purpose (a threshold made ten times wider, a fault removed, a guard bypassed) in a scratch copy
of the repository, then runs the simulator scenario (test/sim) or the unit tests that should catch it.  "killed" = the tests failed
on the broken firmware, as they must; "SURVIVED" = a bug that the tests would not notice.

  python3 test/mutants.py            (from the repository root; needs gcc and make; about 30 minutes)
"""
import os, shutil, subprocess, sys, tempfile

# file, the line as it is, the broken line, the scenario that must catch it (TEST = make test)
M = [
 ("src/chg.c", "#define V_OVER          45 ", "#define V_OVER          450 ", "rail_sense_low"),
 ("src/chg.c", "#define RAIL_MARGIN     30 ", "#define RAIL_MARGIN     300 ", "rail_sense_low"),
 ("src/chg.c", "    if (W.v >= V_FAULT) return go_fault();", "    ;", "bleed_over"),
 ("src/chg.c", "    else if (W.pre) { W.pre = 0; W.ramp = 1; W.ramp_div = 0; }", "    else if (W.pre) { W.pre = 0; }", "gentle"),
 ("src/chg.c", "    if (!W.t_held || IC > I_OPEN) {", "    if (!W.t_held) {", "held_never_full"),
 ("src/chg.c", "        if (W.pre && ++W.t_pre_s >= T_PRE_MAX_S) return go_fault();", "        ;", "pre_short"),
 ("src/hal.c", "    hal_rail(PWM_PERIOD);", "    hal_rail(0);", "vcc_fatal"),
 ("test/sim/sim_hal.c", "S.global = 0; S.rail_duty = PWM_PERIOD; }", "S.global = 0; S.rail_duty = 0; }", "vcc_fatal"),
 ("src/sup.c", "    if (hot) o->stop = STOP_TEMP;", "    ;", "hot"),
 ("src/cfg.c", "    if (ok && hal_ee_read(SUM_ADDR) != cfg_sum) for (k = 0; k < sizeof settings; k++) BYTES[k] = 0xFF;   /* a damaged page: the defaults */", "    ;", "cfg_damage"),
 ("src/adc.c", "        if (vg_n >= VG_PROBE) { if (vg_ref >= vg_off + VG_DIODE_MV) { vg_state = VG_OK; vg_hi = 0; } else vg_state = VG_HOLD; }", "        if (vg_n >= VG_PROBE) { vg_state = VG_OK; vg_hi = 0; }", "both_at_boot"),
 ("src/adc.c", "        if (vg_mv > vg_ref + VG_RISE_MV) { if (++vg_hi >= VG_HI_PASSES) vg_learn(); } else vg_hi = 0;", "        ;", "pack_on_laptop"),
 ("src/adc.c", "        if (vg_mv + VG_DROP_MV < vg_ref) { if (++vg_lo >= VG_LO_PASSES) { vg_lo = 0; vg_learn(); } } else vg_lo = 0;", "        ;", "pd_steps"),
 ("src/adc.c", "    return cal_vcc_fix(raw[ch], gain);", "    return raw[ch];", "vcc_high"),
 ("src/buzz_pure.c", "uint8_t buzz_busy(void) { return active; }", "uint8_t buzz_busy(void) { return 1; }", "TEST"),
 ("src/tele.c", "    if (ph == PH_FATAL) usb_tx2[54] = *ctx.fatal;", "    ;", "vcc_fatal"),
 ("src/chg.c", "    if (W.v < V_REFUSE) return go_fault();", "    ;", "dead_cell"),
 # developer mode is gone: a host asking for it (command 7) must get "unknown command", and nothing may drive an output
 ("src/app.c", "    default: res = 2; break;", "    default: res = 0; break;", "no_dev"),
 # the storage offer must really lower the target (B2 on the offer), not just say so
 ("src/app.c", "        settings.volt = settings.volt == VOLT_4_35 ? VOLT_3_85 : VOLT_3_80;", "        ;", "store"),
 # the cell-resistance reading (an estimate shown to the user) must stay checked against the model
 ("src/stats.c", "                        if (mv > w.v0) { mv -= w.v0; if (mv > 655) mv = 655; r = mv * 100u / ma; }    /* mV x 1000 / mA */", "                        if (mv > w.v0) { mv -= w.v0; if (mv > 655) mv = 655; r = mv * 10u / ma; }", "TEST"),
 # from the independent audit (SAFETY.md "Audit"): each fix must be caught if it is undone
 ("src/app.c", "    if (fault) { if (screen == SCR_SUMMARY) summary_close(); return; }", "    ;", "fault_full"),
 ("src/app.c", "    if (done && stats.packs != fin_packs) done = 0;  /* a new cell went in: when all are full again, a finale of their own */", "    ;", "TEST"),
 ("src/app.c", "    if (!full || (busy_ports & 0x7F)) return;        /* a top-up of a cell that sagged keeps `done`: no replay, the 2 h go on */", "    if (!full || (busy_ports & 0x7F)) { done = 0; return; }", "sag_finale"),
 ("src/app.c", "    if (store_on) { settings.volt = store_prev; store_on = 0; }   /* the storage run is over: the user's own target again */", "    ;", "store_back"),
 ("src/app.c", "        if (adc_saturated(port_v_ch(k))) cin.v_now[k] = V_SATURATED;", "        ;", "saturated"),
 ("src/app.c", "    if (screen == SCR_MENU && menu_editing()) return 0;   /* an edit open: finish it first (the voltage must not change mid-run) */", "    ;", "menu_edit_start"),
 ("src/menu.c", "    if (it == IT_VOLT && *v == VOLT_4_35 && !settings.hv) { if (up) (*v)++; else (*v)--; }   /* 4.35 V is skipped until it is allowed */", "    ;", "TEST"),
 ("src/menu.c", "    if (editing && level && is_locked(item())) { *SLOT[item()] = backup; editing = 0; }", "    ;", "TEST"),
 ("src/cfg_pure.c", "    if (!settings.hv && settings.volt == VOLT_4_35) settings.volt = VOLT_4_20;   /* 4.35 V only once it is allowed */", "    ;", "TEST"),
 ("src/stats.c", "                if (w.ron && ++w.tt >= R_GIVE_UP) w.ron = 0;    /* no stable second in 60 s (a limited supply): no reading this fill */", "                ;", "TEST"),
 ("src/chg.c", "        W.t_chg_div += LT;               /* passes, and those a long pass lost: real time */", "        W.t_chg_div += 1;", "TEST"),
 ("src/chg.c", "    W.i0 = W.i > I_ZERO_MAX ? I_ZERO_MAX : (uint8_t)W.i;   /* the output has been off: what the current reads now is its offset */", "    W.i0 = 0;", "TEST"),
 ("src/chg.c", "    if (W.t_chg_div >= PASSES_PER_S) { W.t_chg_div -= PASSES_PER_S; if (++W.t_bleed_s >= T_BLEED_MAX_S) return go_fault(); }", "    ;", "TEST"),
 ("src/sup.c", "        if (++ok_cnt >= RAIL_OK_PASSES) { rail_alarm = 0; div_cnt = 0; ok_cnt = 0; }   /* in band for a second, not one lucky pass */", "        { rail_alarm = 0; div_cnt = 0; ok_cnt = 0; }", "TEST"),
]

def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    subprocess.run(['make', '-s', 'build/version_build.h'], cwd=root, capture_output=True)
    survived = 0
    for i, (f, a, b, scen) in enumerate(M):
        d = tempfile.mkdtemp(prefix='mut%d_' % i)
        shutil.copytree(root, d, dirs_exist_ok=True, ignore=shutil.ignore_patterns('.git', 'build'))
        os.makedirs(d + '/build', exist_ok=True)
        shutil.copy(root + '/build/version_build.h', d + '/build/')
        p = os.path.join(d, f)
        s = open(p).read()
        if s.count(a) != 1:
            print('M%d %s: the line is not in the code any more, update the list' % (i, f)); survived += 1; continue
        open(p, 'w').write(s.replace(a, b))
        cmd = 'make -s test' if scen == 'TEST' else 'make -s build/sim_app >/dev/null 2>&1 && timeout 1200 ./build/sim_app ' + scen
        killed = subprocess.run(cmd, shell=True, cwd=d, capture_output=True).returncode != 0
        survived += not killed
        print('M%d %s [%s]: %s' % (i, f, scen, 'killed' if killed else 'SURVIVED'), flush=True)
        shutil.rmtree(d, ignore_errors=True)
    print('%d of %d planted bugs caught' % (len(M) - survived, len(M)))

if __name__ == '__main__':
    main()

# Result on 2026-11-03 (developer mode removed; storage and statistics added), and the same on 0.2.0-beta.1 (one look, new screen),
# run on GitHub (Actions, "Mutation tests", run 41652240488):
#   M0 src/chg.c [rail_sense_low]: killed
#   M1 src/chg.c [rail_sense_low]: killed
#   M2 src/chg.c [bleed_over]: killed
#   M3 src/chg.c [gentle]: killed
#   M4 src/chg.c [held_never_full]: killed
#   M5 src/chg.c [pre_short]: killed
#   M6 src/hal.c [vcc_fatal]: SURVIVED
#   M7 test/sim/sim_hal.c [vcc_fatal]: killed
#   M8 src/sup.c [hot]: killed
#   M9 src/cfg.c [cfg_damage]: killed
#   M10 src/adc.c [both_at_boot]: killed
#   M11 src/adc.c [pack_on_laptop]: killed
#   M12 src/adc.c [pd_steps]: killed
#   M13 src/adc.c [vcc_high]: killed
#   M14 src/buzz_pure.c [TEST]: killed
#   M15 src/tele.c [vcc_fatal]: killed
#   M16 src/chg.c [dead_cell]: killed
#   M17 src/app.c [no_dev]: killed
#   M18 src/app.c [store]: killed
#   M19 src/stats.c [TEST]: killed
#   19 of 20 caught.  M6 survives by construction: the simulator replaces src/hal.c with its model (test/sim/sim_hal.c), so the
#   fatal-stop rail value in hal.c is checked by review and on the charger only; M7 is the same change made in the model, and is caught.

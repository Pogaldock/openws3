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
 ("src/app.c", "    case 2: res = screen != SCR_DEV && start_run() ? 0 : 1; break;", "    case 2: res = start_run() ? 0 : 1; break;", "dev_rules"),
 ("src/app.c", "        rail_duty = dev_pass(sout.stop == STOP_VIN || sout.stop == STOP_VCC, sout.fan);", "        rail_duty = dev_pass(0, sout.fan);", "dev_rules"),
 ("src/chg.c", "    if ((!W.t_held || W.i > I_OPEN) && ++W.t_chg_div >= PASSES_PER_S) {", "    if (!W.t_held && ++W.t_chg_div >= PASSES_PER_S) {", "held_never_full"),
 ("src/chg.c", "        if (W.pre && ++W.t_pre_s >= T_PRE_MAX_S) return go_fault();", "        ;", "pre_short"),
 ("src/dev.c", "        if (dv >= vcut || di >= I_MAX) trip |= (uint8_t)(1u << k);", "        if ((out_duty[k] || (was_sw & (1u << k))) && (dv >= vcut || di >= I_MAX)) trip |= (uint8_t)(1u << k);", "dev_pulse"),
 ("src/dev.c", "        if ((was_sw & (1u << k)) && dv < V_PRE) trip |= (uint8_t)(1u << k);", "        ;", "dev_bleed"),
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

# Result on 2026-11-03 (commit after 78a79d3):
#   M0 src/chg.c [rail_sense_low]: killed
#   M1 src/chg.c [rail_sense_low]: killed
#   M2 src/chg.c [bleed_over]: killed
#   M3 src/chg.c [gentle]: killed
#   M4 src/app.c [dev_rules]: killed
#   M5 src/app.c [dev_rules]: killed
#   M6 src/chg.c [held_never_full]: killed
#   M7 src/chg.c [pre_short]: killed
#   M8 src/dev.c [dev_pulse]: killed
#   M9 src/dev.c [dev_bleed]: killed
#   M10 src/hal.c [vcc_fatal]: SURVIVED
#   M11 test/sim/sim_hal.c [vcc_fatal]: killed
#   M12 src/sup.c [hot]: killed
#   M13 src/cfg.c [cfg_damage]: killed
#   M14 src/adc.c [both_at_boot]: killed
#   M15 src/adc.c [pack_on_laptop]: killed
#   M16 src/adc.c [pd_steps]: killed
#   M17 src/adc.c [vcc_high]: killed
#   M18 src/buzz_pure.c [TEST]: killed
#   M19 src/tele.c [vcc_fatal]: killed
#   M20 src/chg.c [dead_cell]: killed
#   20 of 21 caught.  M10 survives by construction: the simulator replaces src/hal.c with its model (test/sim/sim_hal.c), so the
#   fatal-stop rail value in hal.c is checked by review and on the charger only; M11 is the same change made in the model, and is caught.

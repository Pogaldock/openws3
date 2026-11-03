#!/usr/bin/env python3
"""Puts the simulator beside a real charge, so the model the tests rely on can be checked against the charger itself.

  1. On the charger: one cell on one port, start a charge, and log it until it is full:
         python3 tools/fwmon.py log charge.jsonl
  2. Here:
         python3 tools/simcompare.py charge.jsonl --port 1 [--cap MAH]

What it does: reads the logged charge (the charger's own readings of that port, once a second or so), works out what it can about
the cell (its resting voltage at the start, its resistance from the step when the current came on, the charge put in), runs the
same charge in the simulator (test/sim, scenario "replay"), and prints both side by side with the differences:
  - time to full, charge put in, final voltage
  - the voltage and current curves: largest and typical difference
Differences of a few percent are the model being a model.  A different shape (the real charge ending much sooner or later, a real
voltage well above the simulated one at the same current) means the simulator is wrong about this charger or this cell, and the
tests built on it deserve less trust: report it with the log.

The capacity is estimated from the charge put in and the starting voltage (an open-circuit curve of a typical LiPo); give --cap
when you know it.  Without a charger, --self-test runs the simulator against itself (it must agree with itself exactly).
"""
import argparse, json, os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
V_PER_COUNT = 4.5 / 4096
A_PER_COUNT = 1 / 68.25
PS_CHARGE, PS_FULL = 2, 4
OCV = [(0.0, 3.30), (0.05, 3.55), (0.15, 3.65), (0.30, 3.72), (0.50, 3.80), (0.70, 3.92), (0.85, 4.03), (1.0, 4.20)]   # typical LiPo


def soc_of(v):
    if v <= OCV[0][1]: return 0.0
    for (s0, v0), (s1, v1) in zip(OCV, OCV[1:]):
        if v <= v1: return s0 + (s1 - s0) * (v - v0) / (v1 - v0)
    return 1.0


def load_real(path, port):
    rows = []
    for line in open(path):
        try: t = json.loads(line)
        except ValueError: continue
        if 'v' not in t: continue
        rows.append((t['ms'] / 1000.0, t['v'][port] * V_PER_COUNT, t['i'][port] * A_PER_COUNT, t['state'][port], t['iset'], t['volt'], t['run']))
    if not rows: sys.exit('no telemetry in %s' % path)
    run = [r for r in rows if r[6]]
    if not run: sys.exit('the log has no charge in it (the run never started)')
    t0 = run[0][0]
    return [(r[0] - t0, r[1], r[2], r[3]) for r in run], run[0][4], run[0][5], rows[0][1]


def run_sim(v0, cap, r, cur, volt, hours):
    env = dict(os.environ, REPLAY_V0='%.4f' % v0, REPLAY_CAP='%.1f' % cap, REPLAY_R='%.4f' % r, REPLAY_CUR=str(cur),
               REPLAY_VOLT=str(volt), REPLAY_HOURS='%.2f' % hours)
    subprocess.run(['make', '-s', 'build/sim_app'], cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    out = subprocess.run(['./build/sim_app', 'replay'], cwd=ROOT, env=env, capture_output=True, text=True).stdout
    sim = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 5 and p[0] == 'R': sim.append((float(p[1]), float(p[2]), float(p[3]), int(p[4])))
    return sim


def summary(rows):
    mah = sum(max(0.0, b[2]) * (b[0] - a[0]) for a, b in zip(rows, rows[1:])) / 3.6
    full = next((t for t, v, i, st in rows if st == PS_FULL), None)
    return mah, full, rows[-1][1]


def at(rows, t):
    for a, b in zip(rows, rows[1:]):
        if a[0] <= t <= b[0]: return a if t - a[0] < b[0] - t else b
    return rows[-1]


def compare(real, sim, cur, volt):
    rm, rf, rv = summary(real); sm, sf, sv = summary(sim)
    print('                    real charger   simulator   difference')
    print('charge put in       %8.0f mAh  %6.0f mAh  %+5.1f %%' % (rm, sm, 100 * (sm - rm) / rm if rm else 0))
    if rf and sf: print('time to full        %8.2f h    %6.2f h    %+5.1f %%' % (rf / 3600, sf / 3600, 100 * (sf - rf) / rf))
    else: print('time to full        %s' % ('the real charge never reached FULL in the log' if not rf else 'the simulated charge never reached FULL'))
    print('last voltage        %8.3f V    %6.3f V    %+6.3f V' % (rv, sv, sv - rv))
    end = min(real[-1][0], sim[-1][0])
    dv = []; di = []
    for t, v, i, st in real:
        if t > end: break
        s = at(sim, t); dv.append(abs(s[1] - v)); di.append(abs(s[2] - i))
    dv.sort(); di.sort()
    if dv:
        print('voltage curve       typical %.3f V apart, at most %.3f V' % (dv[len(dv) // 2], dv[-1]))
        print('current curve       typical %.3f A apart, at most %.3f A' % (di[len(di) // 2], di[-1]))
    if abs(sv - rv) > 0.025:
        print('note: at the end the charger reads %.0f mV %s than the simulator, which charges to the target exactly.  The charger\'s\n'
              '      reading is %s off, or its target is: measure the rested cell with a multimeter (SAFETY.md, "Check your own charger")'
              % (abs(sv - rv) * 1000, 'higher' if rv > sv else 'lower', 'high' if rv > sv else 'low'))
    worry = (rf and sf and abs(sf - rf) / rf > 0.25) or (dv and dv[len(dv) // 2] > 0.05)
    print('verdict: %s' % ('the simulator is NOT a fair model of this charge: worth reporting, with the log' if worry else
                           'the simulator follows this charge closely enough for its tests to mean something'))
    return 0 if not worry else 2


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('log', nargs='?'); ap.add_argument('--port', type=int, default=1)
    ap.add_argument('--cap', type=float, help='the cell\'s capacity in mAh, if known')
    ap.add_argument('--self-test', action='store_true', help='compare the simulator with itself (no charger needed)')
    a = ap.parse_args()
    if a.self_test:
        sim = run_sim(3.70, 300, 0.12, 21, 0, 2.0)
        sys.exit(compare(sim, run_sim(3.70, 300, 0.12, 21, 0, 2.0), 21, 0))
    if not a.log: ap.error('a log file from fwmon.py log, or --self-test')
    real, cur, volt, v_rest = load_real(a.log, a.port - 1)
    # resistance: the step from the resting voltage to the voltage at the full current, in the first seconds at that current (later the
    # cell's own voltage has risen); the median of those samples, so one reading taken mid-ramp does not decide it
    i_set = 0.1 * (cur - 21) / 7 + 0.3
    early = [r for r in real if r[2] > 0.8 * i_set][:8]
    steps = sorted((v - v_rest) / i for t, v, i, st in early) if early else []
    r = steps[len(steps) // 2] if steps else 0.1
    mah = summary(real)[0]
    cap = a.cap or max(50.0, mah / max(0.05, 1.0 - soc_of(v_rest)))
    print('the cell, as far as the log shows: resting %.3f V at the start, about %.2f ohm with its lead, about %.0f mAh%s' %
          (v_rest, r, cap, '' if a.cap else ' (estimated; give --cap if you know it)'))
    sim = run_sim(v_rest, cap, max(0.01, r), cur, volt, max(0.5, real[-1][0] / 3600 + 0.5))
    sys.exit(compare(real, sim, cur, volt))


if __name__ == '__main__':
    main()

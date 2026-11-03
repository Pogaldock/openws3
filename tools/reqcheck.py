#!/usr/bin/env python3
"""Checks REQUIREMENTS.md against the tests (run by `make test` and on every build).  Exits 1 with a message if:

- a requirement (a "### SR-..." section) has no Proof line, or a Proof with nothing on it;
- a proof names something that doesn't exist: `sim:NAME` not in the scenario table of test/sim/sim_app.c, `unit:FILE.FUNCTION` not
  defined in test/FILE.c, or `build:X` other than imgcheck;
- a `review` proof has no "Why review" line saying why no test can reach it;
- a simulation scenario belongs to no requirement and is not listed under "Other checks": tested without a reason.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main():
    req = open(os.path.join(ROOT, 'REQUIREMENTS.md')).read()
    sim = open(os.path.join(ROOT, 'test/sim/sim_app.c')).read()
    scenarios = set(re.findall(r'\{ "(\w+)", s_\w+ \}', sim))
    errors = []
    sections = re.split(r'^### ', req, flags=re.M)[1:]
    used = set()
    n = 0
    for sec in sections:
        title = sec.splitlines()[0]
        m = re.match(r'(SR-[A-Z]\d+)', title)
        if not m:
            continue
        sr, n = m.group(1), n + 1
        proof = re.search(r'\*\*Proof:\*\*(.*?)(?=\n- \*\*|\Z)', sec, re.S)
        if not proof:
            errors.append('%s: no Proof line' % sr); continue
        items = re.findall(r'`([^`]+)`', proof.group(1))
        if not items:
            errors.append('%s: Proof names nothing' % sr)
        for it in items:
            if it.startswith('sim:'):
                name = it[4:]
                if name not in scenarios: errors.append('%s: %s is not a scenario in test/sim/sim_app.c' % (sr, it))
                used.add(name)
            elif it.startswith('unit:'):
                f, _, fn = it[5:].partition('.')
                path = os.path.join(ROOT, 'test', f + '.c')
                if not os.path.exists(path) or not re.search(r'\bstatic void %s\(void\)' % re.escape(fn), open(path).read()):
                    errors.append('%s: %s: no function %s in test/%s.c' % (sr, it, fn, f))
            elif it == 'fuzz':
                if not os.path.exists(os.path.join(ROOT, 'test/sim/fuzz.c')): errors.append('%s: fuzz named, test/sim/fuzz.c missing' % sr)
            elif it == 'build:imgcheck':
                pass
            elif it == 's51:hal':
                if not os.path.exists(os.path.join(ROOT, 'test/s51/t_hal.c')): errors.append('%s: s51:hal named, test/s51/t_hal.c missing' % sr)
            elif it == 'review':
                if 'Why review' not in sec: errors.append('%s: review named without a "Why review" line' % sr)
            else:
                errors.append('%s: %s: not a known kind of proof' % (sr, it))
    other = req.split('## Other checks', 1)
    if len(other) == 2:
        used |= set(re.findall(r'`sim:(\w+)`', other[1]))
    for s in sorted(scenarios - used):
        errors.append('scenario %s belongs to no requirement (add it to one, or to "Other checks")' % s)
    for e in errors:
        print('error: REQUIREMENTS.md: ' + e)
    print('requirements: %d, every one with a proof that exists; scenarios: %d, every one tied to a requirement' % (n, len(scenarios))
          if not errors else 'requirements check: %d problems' % len(errors))
    sys.exit(1 if errors else 0)


if __name__ == '__main__':
    main()

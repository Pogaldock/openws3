#!/usr/bin/env python3
"""Worst-case stack use, worked out from the compiler's assembly (build/*.asm), not measured.

Why: the chip's stack is what is left of 256 bytes of fast RAM (132 bytes here). STACK on diagnostics page 1 shows only the deepest
path that has actually run; a path that runs rarely (an error screen, a USB command during a menu edit) could go deeper. A static
analysis follows every call the code can make. It is standard practice for small embedded systems (ISO 26262 and IEC 61508 list
"static analysis of stack usage" among the methods).

How: for each function, the bytes it pushes itself (push/pop, and the stack frame of a reentrant function); for each call it makes,
2 bytes of return address plus the callee's own worst case; the main loop's worst chain plus the deepest interrupt (all interrupts
here share one priority, so they never nest: one at a time on top of the main loop).  Calls through a pointer go to any function whose
address is taken; library routines (the compiler's helpers) count a fixed LIB_BYTES.

Usage: tools/stackcheck.py BUILD_DIR [MIN_SPARE]   prints the worst chain; exits 1 if the spare is under MIN_SPARE.
"""
import os, re, sys

LIB_BYTES = 6            # a compiler helper (multiply, divide, pointer access): a few pushes at most
ISR_NAMES = ('timer0_isr', 'timer3_isr', 'usb_vector')


def parse(build):
    funcs, addr_taken = {}, set()
    for fn in sorted(os.listdir(build)):
        if not fn.endswith('.asm'): continue
        cur = None
        lines = open(os.path.join(build, fn)).read().splitlines()
        for k, line in enumerate(lines):
            m = re.match(r';\s+function (\w+)\s*$', line)
            if m:
                cur = {'own': 0, 'calls': [], 'depth': 0, 'file': fn}
                funcs[m.group(1)] = cur
                continue
            for t in re.findall(r'#_(\w+)\b', line):
                addr_taken.add(t)
            if cur is None: continue
            s = line.split(';')[0].strip()
            if not s: continue
            op = s.split()[0]
            if op == 'push': cur['depth'] += 1
            elif op == 'pop': cur['depth'] -= 1
            elif op == 'mov' and re.match(r'mov\s+sp\s*,\s*a', s):         # sp = a: a reentrant function's frame (a = sp + n before)
                prev = [l.split(';')[0].strip() for l in lines[max(0, k - 3):k]]
                for p in prev:
                    mm = re.match(r'add\s+a\s*,\s*#(0x[0-9a-fA-F]+|\d+)', p)
                    if mm:
                        n = int(mm.group(1), 0); n = n - 256 if n > 127 else n
                        cur['depth'] += n
            elif op in ('lcall', 'acall', 'ljmp') and op != 'ljmp':
                tgt = s.split()[1]
                cur['calls'].append((tgt[1:] if tgt.startswith('_') else tgt, cur['depth']))
            cur['own'] = max(cur['own'], cur['depth'])
    return funcs, addr_taken


def main():
    build = sys.argv[1] if len(sys.argv) > 1 else 'build'
    min_spare = int(sys.argv[2]) if len(sys.argv) > 2 else 0
    funcs, taken = parse(build)
    if not funcs: sys.exit('stackcheck: no assembly in %s (build first)' % build)
    pointer_targets = [f for f in funcs if f in taken]
    memo, unknown, recursive = {}, set(), set()

    def worst(f, stack=()):
        if f in memo: return memo[f]
        if f in stack: recursive.add(f); return 0, [f + ' (recursion)']
        if f not in funcs:
            if f.startswith('_sdcc_call_dptr'):
                best = max(((worst(t, stack + (f,))[0], t) for t in pointer_targets), default=(0, None))
                return 2 + best[0], ['(through a pointer) ' + str(best[1])] + (worst(best[1], stack + (f,))[1] if best[1] else [])
            unknown.add(f); return LIB_BYTES, ['%s (library, %d assumed)' % (f, LIB_BYTES)]
        d = funcs[f]
        best, chain = d['own'], []
        for tgt, at in d['calls']:
            w, c = worst(tgt, stack + (f,))
            if at + 2 + w > best: best, chain = at + 2 + w, c
        memo[f] = (best, [f] + chain)
        return memo[f]

    main_w, main_c = worst('main')
    isr = [(worst(n)[0] + 2, n, worst(n)[1]) for n in ISR_NAMES if n in funcs]
    isr_w, isr_n, isr_c = max(isr) if isr else (0, None, [])
    mem = open(os.path.join(build, 'fw.mem')).read() if os.path.exists(os.path.join(build, 'fw.mem')) else ''
    room = re.search(r'with (\d+) bytes available', mem)
    room = int(room.group(1)) if room else 132
    total = main_w + isr_w
    print('stack, worst case worked out from the code: main loop %d bytes (%s)' % (main_w, ' > '.join(main_c[:12])))
    print('  plus the deepest interrupt %d bytes (%s), one at a time (one priority)' % (isr_w, ' > '.join(isr_c[:6])))
    print('  = %d of %d bytes: %d spare%s' % (total, room, room - total, '' if not recursive else '; recursion: %s' % ', '.join(sorted(recursive))))
    if unknown: print('  library routines counted as %d bytes each: %s' % (LIB_BYTES, ', '.join(sorted(unknown))))
    if room - total < min_spare:
        print('error: under %d bytes of stack to spare in the worst case' % min_spare); sys.exit(1)


if __name__ == '__main__':
    main()

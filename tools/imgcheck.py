#!/usr/bin/env python3
"""Checks run on every firmware build (Makefile).  Exits 1 with a message if any fails.

  python3 tools/imgcheck.py BUILD_DIR CODE_LIMIT

- The image ends below the chip's settings area: the highest address the linker wrote (from fw.ihx, so trailing 0xFF bytes that are
  real data count too), compared with CODE_LIMIT.
- The interrupt code calls none of the compiler's helper routines (__mulint, __divuint, ...).  In the large memory model they keep an
  operand at one fixed address shared by every caller; an interrupt that calls one while the main loop is in the middle of the same
  routine corrupts the main loop's result.  The pointer helpers (__gptrget, __gptrput) use registers only and are allowed.
  Modules checked: those whose functions run in an interrupt (main.c: the handlers; hal.c, input_pure.c, buzz_pure.c, usb.c).
- The stack keeps its room.  The chip's 256 bytes of fast (internal) RAM hold the registers, a few variables, the compiler's shared
  scratch area and, in what is left, the stack; on the charger 24 bytes of stack were never used while charging.  Any growth of the
  rest takes room from the stack, so the stack the linker leaves (fw.mem) must stay at STACK_MIN or more.  Typical cause: a function
  that called nothing starts calling a compiler helper (a 32-bit multiply), and its scratch bytes can no longer be shared.
"""
import os, re, sys

ISR_MODULES = ['main', 'hal', 'input_pure', 'buzz_pure', 'usb', 'usbq_pure']
ALLOWED = {'__gptrget', '__gptrput'}
STACK_MIN = 132                 # bytes: what 0.2.0-beta.2 left (the stack measured on the charger with it: 24 never used)


def ihx_end(path):
    end = 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(':'):
            continue
        n, addr, typ = int(line[1:3], 16), int(line[3:7], 16), int(line[7:9], 16)
        if typ == 0 and n:
            end = max(end, addr + n)
    return end


def main():
    build, limit = sys.argv[1], int(sys.argv[2], 0)
    ok = True
    end = ihx_end(os.path.join(build, 'fw.ihx'))
    print('code ends at 0x%04X: %d bytes free below the settings area at 0x%04X' % (end, limit - end, limit))
    if end > limit:
        print('error: the image reaches the settings area')
        ok = False
    mem = os.path.join(build, 'fw.mem')
    st = re.search(r'with (\d+) bytes available', open(mem).read()) if os.path.exists(mem) else None
    if not st:
        print('error: no stack figure in %s' % mem); ok = False
    else:
        print('stack room %s bytes (at least %d)' % (st.group(1), STACK_MIN))
        if int(st.group(1)) < STACK_MIN:
            print('error: the fast RAM grew and took room from the stack (see the notes at the top of tools/imgcheck.py)')
            ok = False
    # the pin set-up at reset (hal.c __sdcc_external_startup) must be the one linked: SDCC 4.3 renamed the hook, and with an older
    # compiler the library's empty one runs instead and the power-path pins float until hal_init
    mp = os.path.join(build, 'fw.map')
    hooks = re.findall(r'^\s*C:\s+[0-9A-F]+\s+(_+sdcc_external_startup)\s+(\S+)', open(mp).read(), re.M) if os.path.exists(mp) else []
    if not hooks or any(mod != 'hal' for _, mod in hooks):
        print('error: the reset hook linked is %s, not hal.c\'s (build with SDCC 4.6, README)' % (hooks or 'none')); ok = False
    for m in ISR_MODULES:
        asm = os.path.join(build, m + '.asm')
        if not os.path.exists(asm):
            print('error: %s missing (the interrupt check needs it)' % asm); ok = False; continue
        for k, line in enumerate(open(asm), 1):
            h = re.search(r'\blcall\s+(__\w+)', line)
            if h and h.group(1) not in ALLOWED:
                print('error: %s:%d calls %s: compiler helper in a module that runs in an interrupt' % (asm, k, h.group(1)))
                ok = False
    sys.exit(0 if ok else 1)


if __name__ == '__main__':
    main()

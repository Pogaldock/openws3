#!/usr/bin/env python3
"""Checks run on every firmware build (Makefile).  Exits 1 with a message if any fails.

  python3 tools/imgcheck.py BUILD_DIR CODE_LIMIT

- The image ends below the chip's settings area: the highest address the linker wrote (from fw.ihx, so trailing 0xFF bytes that are
  real data count too), compared with CODE_LIMIT.
- The interrupt code calls none of the compiler's helper routines (__mulint, __divuint, ...).  In the large memory model they keep an
  operand at one fixed address shared by every caller; an interrupt that calls one while the main loop is in the middle of the same
  routine corrupts the main loop's result.  The pointer helpers (__gptrget, __gptrput) use registers only and are allowed.
  Modules checked: those whose functions run in an interrupt (main.c: the handlers; hal.c, input_pure.c, buzz_pure.c, usb.c).
"""
import os, re, sys

ISR_MODULES = ['main', 'hal', 'input_pure', 'buzz_pure', 'usb']
ALLOWED = {'__gptrget', '__gptrput'}


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

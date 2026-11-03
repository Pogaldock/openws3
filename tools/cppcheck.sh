#!/bin/sh
# Static analysis: cppcheck reads the firmware source without running it, looking for known bug patterns (out-of-range indexes,
# uninitialised values, overflows, null pointers, undefined behaviour).  Warnings, portability and performance findings fail the
# build; the ones checked by hand and kept are in tools/cppcheck.supp, each with its reason.  Style remarks are listed, not failed.
set -e
cd "$(dirname "$0")/.."
SDCC_WORDS="-D__xdata= -D__code= -D__data= -D__idata= -D__pdata= -D__bit=char -D__sbit=char -D__sfr=char -D__at(x)= -D__interrupt(x)= -D__using(x)= -D__naked= -D__reentrant= -DSDCC_CPPCHECK"
cppcheck --std=c99 --quiet --enable=warning,portability,performance --error-exitcode=1 --inline-suppr \
    --suppressions-list=tools/cppcheck.supp $SDCC_WORDS -Isrc src/*.c
echo "cppcheck: no warnings (style remarks below, for information)"
cppcheck --std=c99 --quiet --enable=style $SDCC_WORDS -Isrc src/*.c 2>&1 | grep -E '^src/.*: style:' || true

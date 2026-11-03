#!/bin/sh
# Runs tests compiled for the 8051 in the s51 simulator (ucsim, shipped with SDCC), where int is 16 bits as on the charger:
#  - the pure, menu and port-settings unit tests (must print 0 failures)
#  - the charge-logic trace (test/chg_trace.c), which must match the same scenario built for the PC line for line
# The limits tests are left out: 20 000 checks take over half an hour in the simulator.
# Usage: test/run_8051.sh   (SDCC and s51 on the PATH; build/version_build.h made by make)
set -e
cd "$(dirname "$0")/.."
if ! command -v s51 >/dev/null; then
    echo "run_8051: s51 (ucsim) not found, nothing run"
    [ -n "$CI" ] && exit 1                  # in CI a missing simulator is a failure, not a pass
    exit 0
fi
W=build/s51; mkdir -p $W
CF="-mmcs51 --model-large -Isrc -Ibuild -Itest"
LINK="--xram-size 65536 --code-size 65536"
sdcc $CF -c test/s51/runner.c -o $W/runner.rel
sdcc $CF -c test/s51/io.c -o $W/io.rel
for m in ports menu portset theme rowbuf limits dbgscr cal ctl fmt cfg_pure input_pure buzz_pure chg sup stats usbq_pure; do sdcc $CF -c src/$m.c -o $W/$m.rel; done
MODS=$(ls $W/*.rel | grep -v -e runner -e /io.rel -e /t_)

run() {   # $1 = image, $2 = output file, $3 = seconds; stops at the program's "EXIT" line
    rm -f "$2"
    (sleep "$3" | timeout "$3" s51 -t 8052 -X 40M -s "$2" -G "$1" >/dev/null 2>&1 &)
    i=0; while [ $i -lt "$3" ]; do [ -f "$2" ] && tr -d '\r' < "$2" | grep -q '^EXIT' && break; sleep 2; i=$((i + 2)); done
    pkill -f "s51 -t 8052 -X 40M -s $2" 2>/dev/null || true
    tr -d '\r' < "$2" > "$2.txt"
}

fail=0
for t in pure menu portset; do
    sdcc $CF -Dmain=test_main -c test/test_$t.c -o $W/t_$t.rel
    sdcc $CF $LINK $W/runner.rel $W/t_$t.rel $W/io.rel $MODS -o $W/t_$t.ihx >/dev/null
done
for t in pure menu portset; do run $W/t_$t.ihx $W/o_$t 1500 & done   # the three simulators side by side: as long as the slowest
wait
for t in pure menu portset; do
    grep -E 'FAIL|checks' $W/o_$t.txt || true
    grep -q '^EXIT 0' $W/o_$t.txt || { echo "8051 $t: did not finish with 0 failures"; fail=1; }
done

test/hal_8051.sh || fail=1                # the real hal.c: what a fatal stop writes into the registers

cc -std=c99 -Wall -Wextra -Isrc -o $W/trace_pc test/chg_trace.c src/chg.c src/sup.c src/ctl.c src/cal.c
$W/trace_pc > $W/trace_pc.txt
sdcc $CF -Dmain=test_main -c test/chg_trace.c -o $W/t_trace.rel
sdcc $CF $LINK $W/runner.rel $W/t_trace.rel $W/io.rel $W/chg.rel $W/sup.rel $W/ctl.rel $W/cal.rel -o $W/t_trace.ihx >/dev/null
run $W/t_trace.ihx $W/o_trace 900
grep -v '^EXIT' $W/o_trace.txt > $W/trace_8051.txt
if cmp -s $W/trace_8051.txt $W/trace_pc.txt; then echo "8051 charge trace: identical to the PC ($(wc -l < $W/trace_pc.txt) lines)"
else echo "8051 charge trace DIFFERS from the PC:"; diff $W/trace_8051.txt $W/trace_pc.txt | head -20; fail=1; fi
exit $fail

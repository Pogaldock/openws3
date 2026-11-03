#!/bin/sh
# The real src/hal.c on the simulated 8051 (s51): the register values a fatal stop leaves (test/s51/t_hal.c, REQUIREMENTS.md SR-F5).
# A few seconds.  Also run by test/run_8051.sh (make check-8051) and by the planted-bug runner (test/mutants.py, kind HAL).
set -e
cd "$(dirname "$0")/.."
if ! command -v s51 >/dev/null; then echo "hal_8051: s51 (ucsim) not found"; exit 1; fi
W=build/s51h; mkdir -p $W
CF="-mmcs51 --model-large -Isrc -Ibuild -Itest"
sdcc $CF -c test/s51/runner.c -o $W/runner.rel
sdcc $CF -c test/s51/io.c -o $W/io.rel
sdcc $CF -DHAL_TEST -c src/hal.c -o $W/hal.rel
sdcc $CF -c src/buzz_pure.c -o $W/buzz_pure.rel
sdcc $CF -Dmain=test_main -c test/s51/t_hal.c -o $W/t_hal.rel
sdcc $CF --xram-size 65536 --code-size 65536 $W/runner.rel $W/t_hal.rel $W/io.rel $W/hal.rel $W/buzz_pure.rel -o $W/t_hal.ihx >/dev/null
rm -f $W/o_hal
(sleep 60 | timeout 60 s51 -t 8052 -X 40M -s $W/o_hal -G $W/t_hal.ihx >/dev/null 2>&1 &)
i=0; while [ $i -lt 60 ]; do [ -f $W/o_hal ] && grep -q '^EXIT' $W/o_hal && break; sleep 1; i=$((i + 1)); done
pkill -f "s51 -t 8052 -X 40M -s $W/o_hal" 2>/dev/null || true
tr -d '\r' < $W/o_hal | grep -v '^EXIT' || true
tr -d '\r' < $W/o_hal | grep -q '^EXIT 0' || { echo "8051 hal: did not finish with 0 failures"; exit 1; }

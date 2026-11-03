#!/bin/sh
# One unit-test suite (test/test_NAME.c) built by SDCC and run on the simulated 8051 (s51), where int is 16 bits as on the charger.
# For the suites too slow for every build (chg, limits: tens of minutes to hours); the Deep tests workflow runs them.
# Usage: test/s51_suite.sh NAME [SECONDS]   (SDCC 4.6 and s51 on the PATH)
set -e
cd "$(dirname "$0")/.."
N=$1; T=${2:-14400}
command -v s51 >/dev/null || { echo "s51 (ucsim) not found"; exit 1; }
make -s build/version_build.h
W=build/s51_$N; mkdir -p $W
CF="-mmcs51 --model-large -Isrc -Ibuild -Itest"
sdcc $CF -c test/s51/runner.c -o $W/runner.rel
sdcc $CF -c test/s51/io.c -o $W/io.rel
for m in ports menu portset theme rowbuf limits dbgscr cal ctl fmt cfg_pure input_pure buzz_pure chg sup stats usbq_pure; do sdcc $CF -c src/$m.c -o $W/$m.rel; done
sdcc $CF -Dmain=test_main -c test/test_$N.c -o $W/t.rel
sdcc $CF --xram-size 65536 --code-size 65536 $W/runner.rel $W/t.rel $W/io.rel $(ls $W/*.rel | grep -v -e runner -e /io.rel -e /t.rel) -o $W/t.ihx >/dev/null
rm -f $W/out
(sleep "$T" | timeout "$T" s51 -t 8052 -X 40M -s $W/out -G $W/t.ihx >/dev/null 2>&1 &)
i=0; while [ $i -lt "$T" ]; do [ -f $W/out ] && tr -d '\r' < $W/out | grep -q '^EXIT' && break; sleep 10; i=$((i + 10)); done
pkill -f "s51 -t 8052 -X 40M -s $W/out" 2>/dev/null || true
tr -d '\r' < $W/out | grep -E 'FAIL|checks|^EXIT' || true
tr -d '\r' < $W/out | grep -q '^EXIT 0' || { echo "8051 $N: did not finish with 0 failures within $T s"; exit 1; }
echo "8051 $N: 0 failures"

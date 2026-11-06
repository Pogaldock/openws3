#!/bin/sh
# Coverage: which lines, and which branches (each way an if can go), of the firmware the unit tests and the whole-firmware
# simulation never run (gcc --coverage, gcovr).  IEC 61508-3 (functional safety software) highly recommends statement and branch
# coverage at SIL 2; we hold the safety files to FLOOR % of lines and BFLOOR % of branches, so a new path nothing tests fails here
# before it ships.  The untaken branches left are mostly defensive (a counter's cap, an out-of-range index): build/cov/branches.txt
# lists them.  Usage: tools/coverage.sh   (make coverage)
set -e
cd "$(dirname "$0")/.."
FLOOR=98
BFLOOR=85
SAFETY='src/(chg|sup|limits|adc|app|cal|cfg|cfg_pure|usbq_pure|input_pure|ctl)\.c'
B=build/cov
rm -rf $B; mkdir -p $B
make -s BUILD=$B HOSTCC="gcc --coverage -O0" test $B/sim_app >/dev/null
./$B/sim_app >/dev/null
command -v gcovr >/dev/null || pip install --break-system-packages -q gcovr
# Hot simulator loops run some lines billions of times; gcovr flags such counts as "suspicious" (possible counter wrap) and stops.
# They are real counts here, so it only warns.
gcovr --gcov-ignore-parse-errors=suspicious_hits.warn -r . --object-directory $B --filter 'src/' --exclude 'src/(font|font_rot|sprites|logo)\.c' > $B/coverage.txt
cat $B/coverage.txt
gcovr --gcov-ignore-parse-errors=suspicious_hits.warn -r . --object-directory $B --filter "$SAFETY" --fail-under-line $FLOOR > $B/coverage_safety.txt || {
    cat $B/coverage_safety.txt; echo "coverage: a safety file is under $FLOOR % of its lines run by the tests"; exit 1; }
gcovr --gcov-ignore-parse-errors=suspicious_hits.warn -r . --object-directory $B --filter "$SAFETY" --txt-metric branch --fail-under-branch $BFLOOR > $B/branches.txt || {
    cat $B/branches.txt; echo "coverage: the safety files' branches under $BFLOOR %"; exit 1; }
cat $B/branches.txt
echo "coverage: every safety file at $FLOOR % of its lines or more, and $BFLOOR % of the safety files' branches or more"

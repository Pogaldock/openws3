**What:** built with SDCC 4.6, the rail-fault alarm never sounds. `buzz_busy()` returns `pat != 0`, and SDCC 4.6 assumes a pointer into program memory (`__code`) is never null, so the test compiles to "always busy". The rail alarm starts only when the buzzer is idle, so it never starts. The fault itself still cuts the power.

**How it shows:** only on the chip (or the s51 simulator); host tests built with gcc cannot see it.

**A fix:** an explicit "playing" flag instead of the pointer test ([WS3 Unlocked `src/buzz_pure.c`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/src/buzz_pure.c)).

**Test:** the unit tests run on s51 with the firmware's compiler ([`test/run_8051.sh`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/test/run_8051.sh)).

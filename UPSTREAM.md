# Fixes for OpenWS3

WS3 Unlocked is built on [OpenWS3](https://github.com/openws3/openws3). Some of the defects found and fixed here are also in OpenWS3
itself (as of commit `b20df96`, the one this fork started from). This file is ready to be offered back: each item says what is wrong,
how it shows, where the fix is, and the test that proves it, so the OpenWS3 authors can take what they want, in their own way.

Nothing here has been sent yet. Offering it is a post on someone else's project, so it waits for the maintainer of WS3 Unlocked
(SYMBIOZE) to decide; the plan is one issue per item, each linking to the fix and its test.

## Safety

### Port readings are not corrected for the chip's supply
- **What:** the ADC measures against the chip's own supply (VCC). OpenWS3 corrects only the input voltage, so every port voltage
  scales with VCC, which differs from unit to unit and supply to supply.
- **How it shows:** in simulation, a cell charged to **4.57 V** on the 4.20 V setting with VCC at 4.95 V.
- **Fix:** read the internal reference each pass and correct every port, current and rail reading with it: `src/adc.c`, `src/cal.c`.
- **Test:** simulation scenarios `vcc_low`, `vcc_mid`, `vcc_high`, `vcc_step` (4.200 to 4.203 V from VCC 4.45 to 4.95 V).

### A fatal stop drives the rail to its highest
- **What:** the rail actuator is inverted (higher duty, lower rail). OpenWS3's fatal stop sets duty 0, the highest rail.
- **How it shows:** simulated 4.85 V on the rail after a fatal stop, instead of 3.25 V.
- **Fix:** full duty in `hal_all_off` (`src/hal.c`).
- **Test:** `test/s51/t_hal.c` runs the real `hal.c` on the simulated 8051 and reads back what it wrote.

### The rail-fault alarm never sounds (SDCC 4.6)
- **What:** SDCC 4.6 assumes a pointer into program memory is never null, so the buzzer's "busy" test compiles to "always busy",
  and the rail alarm, which starts only when the buzzer is idle, never starts. The fault still cuts the power.
- **Fix:** an explicit "playing" flag instead of the pointer test (`src/buzz_pure.c`).
- **Test:** the unit tests run on the simulated 8051 (`make check-8051`); PC tests cannot see it.

### Developer mode can push a cell past the cut, and drain one
- **What:** the 50 mV cut was checked only on ports that had been on, and switching a port off released it, so a host pulsing a port
  on and off pushed a cell to **4.45 V**; a held bleed switch drained a cell to 0 V in an hour.
- **Fix in WS3 Unlocked:** first both holes closed, then the mode removed (no user needs it). For OpenWS3, closing the holes alone is
  the smaller change: the cut checked on every port, whatever its history; a time limit on the bleed switch.

### An ADC conversion that never finishes hangs the firmware
- **What:** `while (!(ADC_CONTR & 0x20));` waits for ever on a failed converter, and the watchdog then resets the chip again and again.
- **Fix:** a bounded wait that returns full scale, which the supervision treats as an over-voltage (`hal_adc`, `src/hal.c`).
- **Test:** `test/s51/t_hal.c` (s51 has no ADC, so the conversion never finishes there).

### A USB command can replace one not yet run
- **What:** a second report arriving before the main loop takes the first overwrites it, so a stop can be lost.
- **Fix:** one waiting command, the first one wins; the host resends what is not acknowledged, and a resend with the same number is
  run once (`src/usbq_pure.c`, `src/usb.c`, `src/app.c`, USB.md).
- **Test:** unit test `t_usbq`, simulation scenario `usb_stop_burst`.

## Robustness

### The millisecond clock jumps after 24.9 days of continuous power
- **What:** `hal_ms()` is ticks / 2, so it has 31 bits and jumps back by 2^31 ms when the tick counter wraps; every "is it time yet"
  comparison misfires once.
- **Fix:** carry the top bit across the wrap (`hal_ms`, `src/hal.c`). **Test:** `test/s51/t_hal.c`.

### The reset hook is not called by SDCC before 4.3
- **What:** SDCC 4.3 renamed `_sdcc_external_startup` to `__sdcc_external_startup`. OpenWS3 uses the new name, so built with an older
  SDCC its early pin set-up silently never runs. Not a defect with SDCC 4.6, but worth a build check.
- **Check:** `tools/imgcheck.py` fails the build if the hook linked is not `hal.c`'s.

## Tools that could help OpenWS3 too
- The whole-firmware simulator (`test/sim`), the randomised fuzzer (`test/sim/fuzz.c`), the planted-bug runner (`test/mutants.py`),
  the requirements check (`REQUIREMENTS.md`, `tools/reqcheck.py`), coverage and cppcheck in CI.

# WS3 Unlocked: safety case

This file says what WS3 Unlocked does to keep a lithium cell, the charger and whatever is plugged into it safe, how each part was
checked, what it takes for granted, and what nobody has checked yet. It is written so that you can judge for yourself, and so that
anyone can repeat or extend the checks.

WS3 Unlocked is a fork of [OpenWS3](https://github.com/openws3/openws3) (Paweł Sadowski and the OpenWS3 contributors). The charge
control, supervision and most of the protections below the line "what OpenWS3 already does" are theirs; this file credits them as it
goes. Items marked *(upstream too)* are defects that are also in OpenWS3 as of commit `b20df96`.

## Read this first

- **Unofficial firmware for a lithium battery charger.** Not made, tested or supported by VIFLY or by the OpenWS3 authors.
- **Tested on one charger.** Everything else is simulation, tests and review. Hardware differs from unit to unit.
- **Supervise every charge.** Charge on a non-flammable surface, never unattended, never overnight, and only cells in good condition.
- **The firmware cannot fix hardware.** A failed transistor that stays on, a cracked board or a damaged cell are beyond what it can stop.
- **Check your unit once** with a multimeter (below) before you rely on it.

## The hazards, and what guards each one

### A cell charged above its voltage
The main risk of any LiPo charger: over 4.2 V (4.35 V for LiHV) a cell degrades, swells and can catch fire.
- **CV regulation and over-voltage stop (OpenWS3):** the rail is held 15 mV above the target; a port whose voltage goes above the rail
  and the target is faulted; 4.40 V is an absolute fault.
- **Readings corrected for the chip's supply (WS3 Unlocked, upstream too):** the ADC measures against the chip's own supply (VCC),
  which differs from unit to unit and supply to supply. OpenWS3 corrected only the input voltage, so every port reading scaled with
  VCC. Simulated: OpenWS3 charges to **4.57 V** on a 4.20 V setting at VCC 4.95 V; WS3 Unlocked finishes at 4.200 to 4.203 V from
  VCC 4.45 to 4.95 V, and at 4.217 V after VCC steps mid-charge. Each pass now reads the internal reference first and corrects every
  port, current and rail reading with it (`src/adc.c`, `src/cal.c`).
- **Developer mode removed (WS3 Unlocked):** OpenWS3 lets a host program drive the outputs directly. Its guards cut a port 50 mV
  above the selected target, but the cut was checked only on ports that had been on, and switching a port off released it, so a
  host pulsing a port on and off pushed a cell to **4.45 V** (found by review, reproduced in simulation; a held bleed switch also
  drained a cell to 0 V in an hour). Both were fixed first *(upstream too)*; the mode was then removed altogether, to make room for
  the statistics and because no user needs it. Commands 7 to 9 now get "unknown command" and drive nothing (simulated: `no_dev`).

### Too much current, or current into a damaged cell
- **Per-port over-current fault and soft start (OpenWS3).** A 3 A short is cut in 10 ms and stays latched (simulated).
- **Over-discharged cells refused (WS3 Unlocked):** a cell resting below 2.5 V faults instead of charging; simulated, a 2.3 V cell
  never gets current (`src/chg.c`).
- **Pre-charge (WS3 Unlocked):** from 2.5 to 3.0 V a cell charges at 0.1 A, then ramps to its setting; simulated 0.110 A below and
  1.105 A above 3.0 V on a 1.0 A setting. The over-current fault uses the 0.1 A current there, not the full setting.
- **Pre-charge time limit (WS3 Unlocked):** a cell that does not reach 3.0 V in 30 minutes is shorted inside: the port faults.
  Simulated with a 0.11 A internal leak: fed at 25 minutes, off at 35.

### A cell that never finishes
- **Charge time limit (WS3 Unlocked):** 3 hours of charging per cell, top-ups of the same cell included. Time with the port held at
  zero by the input limits does not count; time held with current flowing does. The first version did not count any held time, so
  with MAX IN AMPS on, a bad cell charged for **10 hours** (found by review); now each port stops after its own 3 hours with current
  (simulated: three never-full cells sharing 0.5 A all stop by 9.6 h). A cell leaking 50 mA is given up after 6.6 h of top-ups.

### Heat
- **Fan (OpenWS3)** by the board thermistor.
- **Over-temperature stop (WS3 Unlocked):** charging stops above an **estimated** 60 C and may restart below 50 C; a thermistor
  reading near either end of its scale (unplugged or shorted) also stops it (`src/sup.c`). The thermistor's part is unknown: the
  thresholds come from a 10 k NTC (B 3950) model that matches the readings (about 2150 counts at 22 to 24 C on one unit, the fan's
  thresholds at 39 / 44 C). They are not calibrated.
- **Not covered:** the cells' own temperature. The board has no sensor on the cells.

### The firmware stops or misbehaves
- **Watchdog (WS3 Unlocked):** if the main loop stops for 2.5 s the chip resets and starts with everything off (`WDT_CONTR = 0x37`).
  It is fed only from the main loop, never from an interrupt. The simulation checks that no scenario, faults included, goes 2.5 s
  without feeding it.
- **Low-voltage reset at 3.0 V (WS3 Unlocked):** a collapsing supply resets the chip instead of letting it run erratically
  (`RSTCFG = 0x43`).
- **Pin modes at the first instruction (WS3 Unlocked):** the power-path pins are set before the C start-up runs, so they float for
  the shortest time the chip allows.
- **No endless ADC wait (WS3 Unlocked):** a converter that never finishes reads full scale after a bounded wait, which the supervision
  treats as a fault.
- **Fatal stop leaves the rail at its lowest (WS3 Unlocked, upstream too):** the rail actuator is inverted, so OpenWS3's "duty 0"
  in a fatal stop drove the rail to its highest (simulated 4.85 V); now full duty, the lowest (simulated 3.25 V).
- **Interrupts at one priority (WS3 Unlocked):** the timer and USB interrupts could nest, and together they need an estimated 45 bytes
  of stack on a chip that measured 24 spare while charging. They can no longer interrupt each other.
- **Smaller race fixes (WS3 Unlocked):** the buzzer pattern switch against the timer interrupt; a button read shared by the interrupt
  and the main loop; an input-current estimate that wrapped around above 10 A in total.

### Alarms that do not sound
- **Rail-fault alarm (WS3 Unlocked, upstream too):** SDCC 4.6 assumes a pointer into program memory is never null, so the buzzer's
  "busy" test became "always busy" and the rail alarm (started only when the buzzer is idle) never sounded. The fault itself still cut
  the power. Found by the tests on a simulated 8051 (PC tests cannot see it); fixed with an explicit flag (`src/buzz_pure.c`).

### The charger's supplies: USB-C fed backwards
The charger takes USB-C (PD, measured: STRAP 0 on a Samsung 45 W charger at 19.5 V, an Apple 61 W at 20.0 V and a laptop port at
5.0 V) and the XT60 / DC jack (STRAP 1 on a pack alone). On USB-C the firmware switches on a bypass (P5.3) around the input diode. That
bypass conducts both ways: a pack plugged into the XT60 afterwards would be fed backwards into the laptop or charger on the USB-C side.
- **Guard (WS3 Unlocked, `src/adc.c`):** at start the bypass stays off while the input settles; then it is switched on for about
  18 ms. If USB-C is the highest supply the input rises by the diode's drop (0.6 V) and the bypass stays on; if a pack is higher,
  nothing changes and it stays off. While on, the input rising 0.4 V above the USB-C level means a second supply arrived: off at once,
  then the same test again. A lasting fall of 2 V (a PD source back at 5 V) is learnt again.
- **Simulated:** a 16.8 V or 25.2 V pack plugged in while on a laptop or a PD charger reaches the USB-C side for 31 ms (the detection,
  then one test); a 2S, 4S or 6S pack already in at power-up for 4 ms; a pack lower than the USB-C supply never; no false cut on a PD
  start (5 V, then 20 V) or a PD reset.
- **The first version of this guard had a hole** (found by review): it learnt a 2S to 5S pack present at start-up as the USB level and
  back-fed it for the whole session. The redesign above is what replaced it.
- **Not covered:** a pack less than 0.4 V above the USB-C supply (little current can flow), and whatever the hardware does in the
  first milliseconds before the firmware runs. **Setting DISABLE USB POWER = YES** (menu) keeps the bypass off for good and removes
  the question when you power the charger from the XT60.

### Settings
- **Validation (OpenWS3):** every stored value goes through its own rule on load and save, so a damaged value cannot select an unsafe
  target.
- **Check byte (WS3 Unlocked):** the settings page also carries a check byte; a page with any changed bit loads the defaults
  (simulated: one flipped bit that would have selected 4.35 V loads 4.20 V).
- **Flash wear (WS3 Unlocked):** settings changed by a host are saved once it stops changing them, so a program cannot wear the flash out.

### The installer and the files you download
- **The page checks itself:** the firmware inside `openws3.html` comes with its SHA-256; the page computes it again and refuses to
  install if it differs (tested in Chromium: one changed character in the page is refused). The full SHA-256 is shown in the install
  confirmation, and an image that would reach the charger's settings area is refused.
- **The release:** built by GitHub Actions only after every test passed, in a job that has no write access; published by a separate job
  with `SHA256SUMS` and a signed build-provenance attestation. Actions are pinned to commit SHAs and the compiler download is checked
  by its SHA-256. Each version (a `v*` tag) is kept as its own release; `rot-latest` is the newest build of the branch.
- **Not covered:** the installer does not read the flash back after writing (the loader's read command is not used), and anyone with
  write access to the repository could publish a different build. The attestation and building it yourself are the checks for that.

## How it was checked

### Unit tests (OpenWS3's, extended) - `make test`
35,469 checks on the PC: the charge state machine (9,508), the input limits (20,852), the menu (321), port settings (4,490), and
settings, display text and helpers (298). WS3 Unlocked added the refusal, pre-charge and time-limit tests and the diagnostics text.

### Whole-firmware simulation (WS3 Unlocked) - `make sim`
The real firmware (`src/app.c` and every module it calls; only the register layer is replaced) runs on the PC against a model of the
board: the supply and its USB-C / XT60 paths, the inverted rail with sag, six ports, cells with an open-circuit curve, internal
resistance, polarisation and optional leakage, an ADC referenced to VCC with noise, gain errors and reference spread, the display's bus
time, the watchdog and the thermistor. It presses the buttons like a person and sends USB commands like a host, in 38 scenarios:
- full charges at VCC 4.45 to 4.95 V, reference spread plus or minus 1 %, ADC noise, combined worst case
- user stop, input lost, cell pulled mid-charge, short circuit and its latch, storage voltage, power cycle mid-charge
- dead cell, deeply discharged cell, internally shorted cell, cell that never fills, leaking cell, never-full cells under MAX IN AMPS
- stuck rail high and low, rail out of range for a while, the rail's own sensor reading 10 % low (the port readings must catch it),
  VCC over 5.0 V (and the host told why), rail self-test failure, an overcharged 4.45 V cell put in on the storage setting
- menu changes while charging, settings page damage
- board over 60 C, thermistor unplugged
- a host asking for the removed developer mode: refused, no output ever driven
- the statistics: mAh within 2 % of the model, resistance and time to full against the model's cell; the storage offer after 2 h
  full, and B2 on it taking the cells down to 3.85 V
- USB-C: XT60 alone, a pack plugged in on a laptop and on a PD charger, packs present at power-up, PD start and reset, PD back at 5 V
- in every scenario: the watchdog is never starved

The output of the last run is in each CI log. OpenWS3 as of `b20df96` fails five of the charging scenarios run against it, among them cells to 4.57 V at VCC
4.95 V, a 2.3 V cell charged, no gentle start and no time limit.

### Mutation testing: do the tests catch a real bug?
A test suite is only worth what it catches. Each safety check was broken on purpose, one at a time (a threshold made ten times wider,
a fault removed, a guard bypassed), and the suites run against the broken firmware. The independent review did this first on the
earlier code: 15 of 29 planted bugs survived at least one suite, among them a disabled charge over-voltage cut, a removed 4.40 V bleed
fault and developer mode ignoring a lost input. The scenarios added since (the rail sensor reading low, the overcharged cell, developer
mode rules, the soft ramp after pre-charge) were written to catch exactly those. The runner and its list of planted bugs are in
`test/mutants.py`, with the last result at its end. On the current code **19 of 20 are caught**. The one that survives is the fatal-stop rail value in `src/hal.c`:
the simulator replaces that file with its model, so that line is checked by review and on the charger only (the same change made in
the model is caught).

### Tests on a simulated 8051 (WS3 Unlocked) - `make check-8051`
`int` is 16 bits on the chip and 32 on a PC, so overflow bugs can pass on the PC. The unit tests run again in the s51 simulator, built
with the same compiler (SDCC 4.6.0) and the same memory model as the firmware, and the charge logic runs one scenario on both, whose
traces must match line for line (161 lines, every port state). The input-limits suite is left out: it takes over 30 minutes in the simulator.

### Independent review
Four separate AI review agents (not people) read the code, each with one area and without the author's conclusions: the charge logic, the hardware
layer, USB / installer / build, and the test evidence. Every defect they found was reproduced in the simulator first, then fixed with a
test where the simulator can show it (the race and interrupt fixes cannot be simulated: they rest on review). That review is where the developer-mode pulse, the time limit under MAX IN AMPS, the fatal-stop
rail, the start-up hole in the USB-C guard, the interrupt nesting and the settings check came from.

### On the charger (one unit, VIFLY WhoopStor V3, chip version A)
- VCC 4.484 V on every supply tried: two PD chargers, a laptop port, a 4S pack (it comes from its own regulator).
- Input reading within about 2 % of the supply on a 20 V and a 5 V source.
- Rail 4.36 V at the 4.35 V LiHV setting; a cell reading 4.338 V while charging at that setting.
- Stack free 24 bytes after charging one cell, 45 idle (the firmware measures the stack never used since boot). With the 0.2 screen
  (big eyes, melodies): 48, then 45, while charging one cell.
- **The 4.35 V setting, an open question.** A LiHV cell charged on 4.35 V and rested 5 minutes: the charger read 4.33 V; two drones'
  flight controllers read the same cell as 4.40 V and 4.25 V. The drones disagree with each other by 150 mV, so they cannot say
  whether the charger is accurate; a multimeter reading has not been taken yet. Until it is, use 4.20 V, where even the worst case
  of the uncertainty (about 3 %) stays inside what a LiHV cell is made for.
- Thermistor about 2150 counts at room temperature.

## What it takes for granted

- **The port voltage scale (4.5 V full scale):** the charger's own targets fall on it (OpenWS3, checked on hardware); the board's
  resistor dividers have a tolerance nobody has measured. Up to about 2 % is plausible.
- **The chip's internal reference (1.19 V nominal):** about 1 % from chip to chip. Simulated, plus or minus 1 % gives 4.157 to
  4.247 V on a 4.20 V setting. Version B chips store a factory measurement (shown on diagnostics page 1); version A, like the tested
  unit, does not, so it is not used yet.
- **Worst case without a multimeter check:** both together, about plus or minus 3 %: up to about 4.33 V on 4.20 V, 4.48 V on 4.35 V.
  That is why the multimeter check below matters.
- **The strap (P3.4) means "USB-C powers the board":** measured on one unit with three USB-C sources and a pack.
- **The input diode drops about 0.6 V:** OpenWS3, checked on hardware. The USB-C guard needs a rise of at least 0.2 V.
- **The thermistor model:** estimated (above).
- **The rail actuator is inverted** (more duty, lower rail): OpenWS3, checked on hardware.
- **Register values** (watchdog, low-voltage reset, pin modes, interrupt priority): checked line by line against the STC8H manual; only
  a charger can prove them (the first boot after installing).
- **The simulation's model:** realistic, not exact. It shows that the logic does the right thing under the conditions modelled, and
  nothing about what was left out. Known gaps:
  - The register layer (`src/hal.c`), the start-up code, `src/main.c` and the USB driver are replaced by the model, so the watchdog,
    low-voltage reset, pin modes and the ADC timeout are checked against the manual only. The bypass gating is re-implemented in the
    model (`test/sim/sim_hal.c`), not taken from `hal.c`.
  - The input voltage scale is modelled as the firmware's own conversion; on one unit it read within about 2 % of three supplies.
  - The strap is modelled as "USB-C present", as measured on one unit; how it is wired is not known.
  - VCC is a fixed value. HARDWARE.md notes it drops about 0.24 V when the bypass is off; on a 5 V USB-C supply that can show VCC LOW
    for the 1.5 s the guard keeps the bypass off at start. That is a wait, not a fault, and it is not modelled.
  - The port current reading changes about five times faster per duty step in the model than HARDWARE.md measured; ramp and
    no-current timings in the model are therefore faster than on the charger.
  - The simulation runs with 32-bit `int`. On the 8051 (16-bit) only the charge logic, the menu, port settings and the pure helpers are
    run; `app.c`, `adc.c`, `sup.c` and `limits.c` are not.

### The features added after the safety work (WS3 Unlocked)
- **They only display.** The statistics (`src/stats.c`), the views, the summary, the animations and the melodies read the same values
  the charge logic uses; nothing in the charge logic reads them. The one exception is the storage offer: B2 on it stops the run, sets
  the storage voltage and starts again, exactly like doing it in the menu (simulated: `store`).
- **Estimates are marked as such.** Time to full has a `~` and a 25 % margin; the resistance includes the leads; the grade thresholds
  (GOOD under 90 mOhm, TIRED to 150, RETIRE above) are rules of thumb for whoop-size 1S cells, not a measurement of capacity.
- **Timing.** The full-screen pictures (finale, summary) are drawn a strip at a time, so a control pass stays under 50 ms (simulated:
  longest 41.5 ms).
- **Drawing is part of the clock.** The charge time limits count control passes, so a screen that draws too much on average makes
  them run slow in real time. The first version of the empty-port static redrew it on every visit; the simulator caught it (the 3 h
  limit, the 30 min pre-charge limit and the fatal stop all came late). The static is now drawn 7 rows a visit and again only every few
  seconds, the big header icons and the eyes only when they change, and the same scenarios pass (the fatal stop at 0.46 s, 0.47 s
  before the new screen).
- **The tone timer is hardware only.** Melodies use Timer 3 and its interrupt, which the simulator does not have. A wrong register would
  most likely show as no sound, or as an interrupt storm that starves the watchdog: a reset loop at first boot (below).
- **Stack.** One byte more in the deepest screen-drawing frame; check STACK on page 1 while charging (below).

## Check your own charger

- **First boot:** the splash plays once with its jingle and the main screen comes up. A splash every few seconds means a reset loop
  (most likely the tone timer): reinstall the previous build (hold the left button while plugging in USB-C) and report it.
- **Multimeter (the important one):** charge a cell to full, let it rest 5 minutes, measure it at the cell. 4.17 to 4.23 V for 4.20 V,
  4.32 to 4.38 V for 4.35 V. Outside that, stop and report it with a photo of diagnostics page 1.
- **Diagnostics page 1** (menu, DIAGNOSTICS): VCC between 4.45 and 4.95 V (the range the correction was tested over); `STACK` after a
  long charge with the web page connected, 15 or more; `BYP ON` on USB-C alone, `BYP OFF` on the XT60 alone; `THERM` about 2000 to
  2300 in a room.
- **Outputs off:** with no cells in, after a charge, the ports read near 0 V on page 2.
- **Downloads:** `sha256sum -c SHA256SUMS`, and `gh attestation verify openws3.html -R Pogaldock/ws3-unlocked`.

## Still open (for anyone who wants to be sure)

- **Measure a rested cell with a multimeter after a 4.35 V charge** (above). Then measure the port voltage divider and the reference
  on several units, and add a per-unit calibration from a multimeter reading.
- Calibrate the thermistor against a thermometer, and decide the stop temperature from it.
- Watch the power-path pins and the rail with an oscilloscope from power-up to the first instruction, and after a watchdog reset.
- Measure what the XT60 path does when USB-C is higher (does it conduct backwards into a pack?).
- Measure the stack under every interrupt with USB telemetry running during a full six-port charge.
- Run long soak tests: six ports, every supply, days.
- Have a human who knows battery chargers review `src/chg.c`, `src/sup.c` and `src/adc.c` (all review so far was by AI agents).
- Read the flash back after installing, in the installer.
- Run `app.c`, `adc.c`, `sup.c` and `limits.c` on the simulated 8051 too, not only on the PC.
- Offer the *(upstream too)* fixes back to OpenWS3.

## Reporting a problem

[Open an issue](https://github.com/Pogaldock/ws3-unlocked/issues) with what the charger did, the supply and cells, the firmware build
(on the installer page), and a photo of diagnostics page 1. Not to OpenWS3 or VIFLY: they did not make these changes.

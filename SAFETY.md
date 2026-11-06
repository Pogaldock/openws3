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
- **Check your unit once** with a multimeter (below) before you rely on it, especially before trusting the 4.35 V (LiHV) setting.
- **An independent audit found real bugs, and they were fixed** (section "Independent audit" below). An audit lowers the risk; it does
  not prove the firmware correct. Expect more.

## The hazards, and what guards each one

### A cell charged above its voltage
The main risk of any LiPo charger: over 4.2 V (4.35 V for LiHV) a cell degrades, swells and can catch fire.
- **CV regulation and over-voltage stop (OpenWS3):** the rail is held 15 mV above the target; a port whose voltage goes above the rail
  and the target (by 50 mV) for 0.45 s is faulted while charging; a full cell that rises leaves FULL and is bled down; while
  bleeding, 4.40 V is a fault. On 4.35 V the top of the FULL window is 4.40 V itself, so a cell there faults rather than bleeds.
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
- **ALLOW 4.35V (WS3 Unlocked 0.2-beta.2):** a menu switch (CHARGING) for the LiHV target, **on by default**: this charger is for
  whoop cells, almost all LiHV, and the author chose that over a safer-for-LiPo default. Set to NO, 4.35 V is skipped by the menu and
  by B2 and refused over USB, and a stored 4.35 V target loads as 4.20 V: for anyone who charges only LiPo. With it on, as in OpenWS3,
  one B2 tap on the stopped main screen moves the target from 4.20 V to 4.35 V: **check the target shown in the header before you
  start.** A LiPo charged to 4.35 V is overcharged by 150 mV.
- **The target cannot change mid-run (0.2-beta.2):** a run started (by a USB host, say) while the voltage was being edited in the
  menu kept the edit going, and the target could be stepped mid-run, to 4.35 V on a LiPo run among others (found by the audit,
  reproduced in simulation). Now a run cannot start during such an edit, and a run that starts undoes it (simulated: `menu_edit_start`;
  unit test).
- **A reading at the top of the ADC's range counts as over-voltage (0.2-beta.2):** a port's full scale is about 0.99 x VCC: 4.43 V at
  the tested unit's 4.484 V, about 4.36 V at VCC 4.40 V. Between those, a cell above 4.40 V read at most the full scale, so the
  over-voltage and 4.40 V bleed faults could not fire (it was bled instead). Such a reading is now passed on as 5.05 V, over every
  threshold (simulated: `saturated`, a 4.46 V cell at VCC 4.42 V faults; without the change it bled). Side effect: at a VCC near
  4.40 V a LiHV cell reaching 4.35 V can fault at the end of its charge. That is the safe direction.

### Too much current, or current into a damaged cell
- **Per-port over-current fault and soft start (OpenWS3).** A 3 A short pulls the port below 1 V and its output is off within 10 ms
  (the cell-removed check, simulated); a current over twice the setting for 0.9 s faults the port and stays latched.
- **Over-discharged cells refused (WS3 Unlocked):** a cell resting below 2.5 V faults instead of charging; simulated, a 2.3 V cell
  never gets current (`src/chg.c`).
- **Pre-charge (WS3 Unlocked):** from 2.5 to 3.0 V a cell charges at 0.1 A, then ramps to its setting; simulated 0.110 A below and
  1.105 A above 3.0 V on a 1.0 A setting. The over-current fault uses the 0.1 A current there, not the full setting.
- **Pre-charge time limit (WS3 Unlocked):** a cell that does not reach 3.0 V in 30 minutes is shorted inside: the port faults.
  Simulated with a 0.11 A internal leak: fed at 25 minutes, off at 35.
- **Current sense zero (0.2-beta.2):** the end of charge is a current of 73 mA, "no current" (a pulled cell) 29 mA, read through a
  sense resistor with no amplifier: a few counts of offset on a unit would stop charges from ending (each cell then held at full
  voltage until the 3 h limit) or hide a pulled cell. The offset is now read with the output off, before each charge, and subtracted
  (up to 58 mA; more is not taken as an offset). Unit test: a 4-count offset, the charge still ends and a pulled cell is still found.

### A cell that never finishes
- **Charge time limit (WS3 Unlocked):** 3 hours of charging per cell, top-ups of the same cell included. Time with the port held at
  zero by the input limits does not count; time held with current flowing does. The first version did not count any held time, so
  with MAX IN AMPS on, a bad cell charged for **10 hours** (found by review); now each port stops after its own 3 hours with current
  (simulated: three never-full cells sharing 0.5 A all stop by 9.6 h). A cell leaking 50 mA is given up after 6.6 h of top-ups.
- **The time limits run on real time (0.2-beta.2):** they used to count control passes, assumed 4.5 ms each. A pass that took longer
  (a full repaint after a supply dip is about 140 ms) made every limit late: at 9 ms passes the 3 h limit became 6 h (found by the
  audit). The app now measures each pass and hands the passes it lost to the charge logic, which counts them (the 3 h, 30 min and bleed
  limits, and the over-current and over-voltage waits). Unit test: with every pass four passes long, the 3 h limit fires at 3 h.
- **Bleed time limit (0.2-beta.2):** 5 hours of bleeding one cell faults the port: a discharge path that does not bring the cell down
  is not left on for ever (about 1 W a port). 5 h, not 3: a storage run on a large full cell legitimately bleeds for hours.

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
- **No compiler helper routines in interrupts (0.2-beta.2):** the melody engine, which runs in the 0.5 ms timer interrupt, multiplied
  by 20. On this chip and compiler that is a call to a shared routine that keeps an operand at one fixed address, also used by the
  main loop's multiplies, among them the filter of every cell voltage. An interrupt at the wrong moment could corrupt one filtered
  reading: rare and silent, and no PC test or simulation can see it (found by the audit, confirmed in the compiled code). Now shifts;
  and every build fails if the code that runs in an interrupt calls any such routine (`tools/imgcheck.py`).
- **Buzzer pin and interrupt flag (0.2-beta.2):** a note's last half-period could leave the buzzer pin high after the note ended;
  reading the tick counter could switch interrupts on where they had been off. Both fixed (`src/hal.c`); neither was harmful today.

### Alarms that do not sound
- **Rail-fault alarm (WS3 Unlocked, upstream too):** SDCC 4.6 assumes a pointer into program memory is never null, so the buzzer's
  "busy" test became "always busy" and the rail alarm (started only when the buzzer is idle) never sounded. The fault itself still cut
  the power. Found by the tests on a simulated 8051 (PC tests cannot see it); fixed with an explicit flag (`src/buzz_pure.c`).
- **Rail alarm that clears too easily (0.2-beta.2):** one pass back in range cleared it, so a rail drifting out of regulation and
  back each second kept charging about 99 % of the time. Now it needs a second in range (unit test).

### The charger's supplies: USB-C fed backwards
The charger takes USB-C (PD, measured: STRAP 0 on a Samsung 45 W charger at 19.5 V, an Apple 61 W at 20.0 V and a laptop port at
5.0 V) and the XT60 / DC jack (STRAP 1 on a pack alone). On USB-C the firmware switches on a bypass (P5.3) around the input diode. That
bypass conducts both ways: a pack plugged into the XT60 afterwards would be fed backwards into the laptop or charger on the USB-C side.
- **Guard (WS3 Unlocked, `src/adc.c`):** at start the bypass stays off while the input settles; then it is switched on for about
  18 ms. If USB-C is the highest supply the input rises by the diode's drop (0.6 V) and the bypass stays on; if a pack is higher,
  nothing changes and it stays off. While on, the input rising 0.4 V above the USB-C level means a second supply arrived: off at once,
  then the same test again. A fall of 2 V (a PD source back at 5 V) turns it off at once (3 passes, about 14 ms) and is learnt again.
- **A PD source falling back under a pack (found by the fuzzer, fixed after 0.2-beta.4):** USB-C at 20 V, a 16.8 V pack on the XT60,
  then the PD source back at 5 V: the guard waited a second before believing the 3.2 V fall, and the pack fed the USB-C side for that
  second. Now 3 passes (a 2 V fall is never load or noise); simulated 0.03 s (scenario `pd_drop_pack`).
- **Simulated:** a 16.8 V or 25.2 V pack plugged in while on a laptop or a PD charger reaches the USB-C side for 31 ms (the detection,
  then one test); a 2S, 4S or 6S pack already in at power-up for 4 ms; a pack lower than the USB-C supply never; no false cut on a PD
  start (5 V, then 20 V) or a PD reset.
- **The first version of this guard had a hole** (found by review): it learnt a 2S to 5S pack present at start-up as the USB level and
  back-fed it for the whole session. The redesign above is what replaced it.
- **Not covered:** a PD source falling back to 5 V while a pack sits less than 2 V below its old level (the input reading alone
  cannot tell that from load sag); a pack less than 0.4 V above the USB-C supply (little current can flow), and whatever the hardware does in the
  first milliseconds before the firmware runs. **Setting DISABLE USB POWER = YES** (menu) keeps the bypass off for good and removes
  the question when you power the charger from the XT60.

### Settings
- **Validation (OpenWS3):** every stored value goes through its own rule on load and save, so a damaged value cannot select an unsafe
  target.
- **Check byte (WS3 Unlocked):** the settings page also carries a check byte; a page with any changed bit loads the defaults
  (simulated: one flipped bit that would have selected 4.35 V loads 4.20 V).
- **Flash wear (WS3 Unlocked):** settings changed by a host are saved once it stops changing them, so a program cannot wear the flash out.
- **New layout in 0.2-beta.2:** the byte that held the theme now holds ALLOW 4.35V. So that no old value can switch 4.35 V on, the
  layout number changed: **installing 0.2-beta.2 resets every setting to its default** (0.3 A, 4.20 V, 4.35 V allowed, sound on, MAX IN AMPS off, MIN IN VOLTS 4.90 V).

### The installer and the files you download
- **The page checks itself:** the firmware inside `openws3.html` comes with its SHA-256; the page computes it again and refuses to
  install if it differs (tested in Chromium: one changed character in the page is refused). The full SHA-256 is shown in the install
  confirmation. This catches a damaged download, not a deliberate change: whoever edits the page can edit the hash too. The
  attestation (below) is what shows where a file came from.
- **Before installing (0.2-beta.2):** five boxes must be ticked (a WhoopStor V3; supervision; the multimeter check; unofficial, AI-assisted,
  one unit, maybe no way back; at your own risk). An image past 0xF400 is refused whatever the chip reports, as is one reaching the
  settings area the chip reports; a unit whose settings area is not where the tested unit's is (another revision or board) is
  asked about first. The same size limit is in the build (`tools/imgcheck.py`, run by `make`, which also deletes a failed image so a
  second `make` cannot pick it up), in `tools/bundle.py`, `tools/isp.py` and `fwmon.py flash`.
- **The release:** built by GitHub Actions only after every test passed, in a job that has no write access; published by a separate job
  with `SHA256SUMS` and a signed build-provenance attestation. Actions are pinned to commit SHAs and the compiler download is checked
  by its SHA-256. Each version (a `v*` tag) is kept as its own release; `rot-latest` is the newest build of the branch.
- **Releases (0.2-beta.2):** a version is released only from a commit on the main branch, only if its name matches what the firmware reports
  (`src/version.h`), and never over an existing release; LICENSE is shipped with the files.
- **Not covered:** the installer does not read the flash back after writing (the loader's read command is not used); the loader's
  answers to its erase commands are not checked (what a good answer looks like is not documented; checking a guess could break every
  install); the installer cannot tell a WhoopStor V3 from another board with the same chip family beyond the settings-area check; and
  anyone with write access to the repository could publish a different build. The attestation and building it yourself are the checks
  for the last one.

## How it was checked

### Unit tests (OpenWS3's, extended) - `make test`
52,239 checks on the PC: the charge state machine (9,515), the input limits (20,852), the menu (324), port settings (4,490), and
settings, display text, the melody engine, the statistics and helpers (17,058). WS3 Unlocked added the refusal, pre-charge, time-limit,
real-time, current-offset, rail-alarm, 4.35 V switch, menu-lock and statistics tests and the diagnostics text.

### Whole-firmware simulation (WS3 Unlocked) - `make sim`
The real firmware (`src/app.c` and every module it calls; only the register layer is replaced) runs on the PC against a model of the
board: the supply and its USB-C / XT60 paths, the inverted rail with sag, six ports, cells with an open-circuit curve, internal
resistance, polarisation and optional leakage, an ADC referenced to VCC with noise, gain errors and reference spread, the display's bus
time, the watchdog and the thermistor. It presses the buttons like a person and sends USB commands like a host, in 65 scenarios:
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
- passes running long for the whole run (a never-full cell must still stop at 3 h), a 4.35 V edit raced by a USB host, a stop
  pressed while the rail is paused, 4.35 V left selected with ALLOW 4.35V off by a path nobody has found (the start must still run on 4.20 V)
- after the audit: a storage run giving the user's target back when stopped (and never saving the storage one); a full cell next to a
  faulted one (no finale, no storage offer, the fault stays); a cell that sags after full (one finale, then the offer, not one per
  top-up); a cell over 4.40 V at a low VCC (faults); a host start while the voltage is being edited (refused); B2 on the stopped
  main screen (skips 4.35 V when switched off, changes nothing while charging); a new cell after the finale (a finale of its own)
- after the red team: B1 held to stop on the menu, port settings and diagnostics screens; a damaged (high resistance) flat cell kept
  on pre-charge; VCC too low for the 4.35 V target (a waiting stop, not a rail runaway); VCC stepping every second (the 3 h limit on
  time); two settings bits flipped 8 bytes apart; a host wearing out the settings flash with refused starts; the storage target never
  saved from the offer; a 6S pack with ADC noise (the USB-C guard probes once)
- the input limits on the whole charger: MAX IN AMPS 1.0 A with four cells at 1.0 A (the real input current, not the firmware's
  estimate, stays at the limit); MIN IN VOLTS on a weak supply (0.4 ohm), LIMIT holding the input up and STOP RUN stopping; a USB stop
  among other commands, and behind one (dropped, resent by the host, carried out), and a command resent with the same number (run once)
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
`test/mutants.py`, with the last result at its end; it also runs on GitHub (Actions, "Mutation tests"). After the audit, the second review round, the red team and the work after 0.2-beta.4 it has 55 planted bugs, one for each fix. On
the current code **all 55 are caught** (GitHub Actions, "Mutation tests", run 41724679624), including the fatal-stop rail value in
`src/hal.c`, which used to survive by design: that file now runs on the simulated 8051 (`make check-hal`).

### Tests on a simulated 8051 (WS3 Unlocked) - `make check-8051`
`int` is 16 bits on the chip and 32 on a PC, so overflow bugs can pass on the PC. The settings, display, statistics, menu and port
settings unit tests run again in the s51 simulator, built with the same compiler (SDCC 4.6.0) and the same memory model as the firmware,
and the charge logic runs one scenario on both, whose traces must match line for line (161 lines, every port state). The input-limits
suite and the charge-logic unit tests take hours there: they run weekly (GitHub Actions, "Deep tests", `test/s51_suite.sh`).

### The hardware layer on the simulated 8051 (after 0.2-beta.4) - `make check-hal`
The whole-firmware simulation replaces `src/hal.c` (the code that writes the chip's registers) with a model, so that file was checked
by reading only. Now the real file, built by the firmware's compiler, runs on s51, and a test reads back what it wrote
(`test/s51/t_hal.c`): the power pins driven low by the reset hook, the watchdog on at 2.5 s, a conversion that never finishes read as
full scale, the clock counting on across its 24.9-day wrap (a bug, fixed: it jumped back), and a fatal stop leaving the rail at its
lowest. The planted bug that used to survive by design (the fatal-stop rail value) is now caught. Writing this test also showed that
SDCC 4.3 renamed the reset hook: with an older compiler the pin set-up silently never runs, so the build now checks which hook is linked.

### Coverage (after 0.2-beta.4) - `make coverage`
Which lines and which branches (each way an `if` can go) the unit tests and the simulation actually run. IEC 61508-3, the standard for
safety software, highly recommends statement and branch coverage at its middle integrity level; the safety files are held at 98 % of
their lines and 85 % of their branches on every build (at the time of writing: 99 % of lines, 88 % of branches; the branches left are
mostly defensive, a counter at its cap or an out-of-range index). The first run found three paths no test took (USB commands that
succeed, the rail never coming up, a full cell rising): each now has a scenario.

### Static analysis (after 0.2-beta.4) - `make cppcheck`
cppcheck reads the code for known bug patterns on every build; warnings fail the build. It found no defect; one finding is kept on
purpose (a signed shift in the rail loop, the same on SDCC and on the PC, shown by the 8051 trace), listed with its reason in
`tools/cppcheck.supp`. MISRA C checking is not included: its rule texts are not free.

### Worst-case stack, from the code (after 0.2-beta.4) - `tools/stackcheck.py`, every build
STACK on diagnostics page 1 shows only the deepest path that has run. The build now works out the worst case from the compiler's
assembly: every call the code can make, the bytes each function pushes, the deepest interrupt on top (one priority: never two at
once). With the 0.2-beta.5 code built by SDCC 4.6: 103 of 132 bytes, 29 spare (the measured 45 to 48 never hit that path); the build fails
under 16 spare. Library routines are counted as 6 bytes each, calls through a pointer as the deepest function whose address is taken.

### The simulator itself - `tools/simcompare.py`
Every test above rests on the simulator's model of the cells and the board. To check the model: log a charge on a real charger
(`fwmon.py log`), then `tools/simcompare.py` estimates the cell (starting voltage, resistance, capacity), runs the same charge in the
simulator and shows the differences in time to full, charge put in, final voltage and the curves. A final voltage that differs by more
than 25 mV points at the charger's calibration: measure that cell with a multimeter. Not run on a real log yet.

### Independent review
Four separate AI review agents (not people) read the code, each with one area and without the author's conclusions: the charge logic, the hardware
layer, USB / installer / build, and the test evidence. Every defect they found was reproduced in the simulator first, then fixed with a
test where the simulator can show it (the race and interrupt fixes cannot be simulated: they rest on review). That review is where the developer-mode pulse, the time limit under MAX IN AMPS, the fatal-stop
rail, the start-up hole in the USB-C guard, the interrupt nesting and the settings check came from.

### Independent audit (2026-11-03, before 0.2-beta.2)
A second round, on the whole project after the new screen and sounds: four AI reviewers (again not people), each given only the
repository and one area (the charge and safety logic; the hardware layer, interrupts and timing; the screens, features and tests;
build, release, installer and documentation), asked for defects and for the assumptions the design rests on, with what breaks if
one is wrong. Their claims were checked against the code before anything was changed; several they had reproduced in the simulator.
- **Fixed** (each with a test, and a planted bug in `test/mutants.py` that the tests must catch):
  - the interrupt calling a shared multiply routine (above); a build check now forbids it
  - "ALL FULL" with a faulted port, and the storage offer restarting the run and clearing the fault
  - a run starting mid-edit, so the voltage could change mid-run
  - 4.35 V one tap away: an ALLOW 4.35V switch, for LiPo-only users (on by default: the author's choice, above)
  - time limits counting passes instead of time
  - over-voltage blind at the top of the ADC's range
  - the current sense offset near the end-of-charge threshold
  - no bleed time limit; the rail alarm clearing on one good pass
  - the storage target left in place (and saved) after the storage run; the finale replayed at every top-up, which also kept the
    storage reminder from ever coming
  - the resistance reading never giving up on a limited supply (time to full blank for the whole charge); a resistance of "1 mOhm,
    GOOD" when the voltage did not rise
  - the view name flashing every 65 s; a summary text buffer one byte short above 10 Ah; the buzzer pin left high; interrupts switched
    on by a tick read; a null test the compiler can fold (screen saver)
  - the size check that a second `make` skipped; no size check in `bundle.py`, `isp.py`, `fwmon.py`; the installer applying no limit
    when the chip reports no settings area, and calling 512 bytes of settings area enough (the settings sit at 2 KB)
  - releases: a version name not checked against the firmware, an existing release overwritable, no LICENSE in the release
  - documentation: the claim that VIFLY's firmware can be put back (no tested way exists); checksums described as proof of origin
- **Not fixed, and why:**
  - the loader's erase answers: not checked (above)
  - the strap pin has no pull-up: on the tested unit it is driven (STRAP 0 and 1 read as expected on four supplies); a pull-up
    could change what it reads on a unit where it is weak. A unit where it floats would request the USB-C bypass at random, and the
    guard (above) then limits it
  - per-unit calibration: needs a multimeter reading to calibrate against (Still open)
  - human expert review: none yet
  - a stop clears a latched port fault: when the run stops (by you, or by the supply guard), every port's fault is cleared with it,
    so the red skull on that port goes away and the cell can be charged again at the next start. Found by the second review round
    (low). The port is off either way and nothing restarts by itself; but if a supply dip stops the run, the fault screen you would
    have seen is gone. Look at the port's state before starting again, and pull a cell that faulted once

### Fuzzer (after 0.2-beta.2) - `make fuzz`, and on GitHub: Actions, "Fuzz"
`test/sim/fuzz.c` runs the same simulated board with random events, each seed reproducible: buttons (menus, edits, port settings,
diagnostics), USB commands valid and malformed, cells put in, pulled and swapped (2.0 to 4.5 V, small to never-full, healthy to
damaged, leaking), USB-C levels and PD steps, XT60 packs, VCC 4.30 to 5.2 V, stuck rails, port shorts, thermistor faults, a slow
display, power cycles. After every control pass it checks the rules this document states: no cell over its target + 30 mV, nothing
driven while stopped, in a fatal stop or a rail pause, settings unchanged through a run, no 4.35 V with ALLOW 4.35V off, the time
limits, the supply and temperature stops, the over-current cut, no run that nobody started, the USB-C side fed backwards for no more
than one probe per supply change. A failing seed is printed with a short list of events that repeats it. Every build runs 24 seeds;
the first 70 seeds (157 simulated hours) found one bug (the USB-C guard probing again and again with a noisy reading, fixed).

### Red team (after 0.2-beta.2, 2026-11-03)
Five AI agents, each told to break one surface rather than review it, each finding to come with a reproduction: any way to overcharge
a cell or drive a port; a hostile USB host and the install tools; the chip's edges (interrupts, stack, arithmetic, flash, timers);
the build, release and documentation; and the fuzzer above. None found a way to take a cell over its target with the sensors in
calibration. Fixed in 0.2-beta.3, each with a test and a planted bug:
- **medium:** holding B1 did not stop a run on the menu, port settings or diagnostics screens (it only scrolled), and on the summary
  or storage offer it only closed the screen. It now stops on every screen after about a second
- **medium:** a damaged flat cell (high resistance) left pre-charge early: its reading under the 0.1 A lifted it past 3.0 V while it
  was really lower, and it got up to 0.68 A. Pre-charge now ends at 3.05 V and holds 0.1 A until then
- **medium:** with the 4.35 V target and the chip's supply at 4.40 to 4.42 V, the rail could not be read up to its setpoint: the
  regulator wound it up to 4.43 V and latched a false fatal. Each target now has its own minimum chip supply (a waiting `VCC LOW`)
- **medium:** a small supply step repainted the whole screen inside one control pass (0.3 s), and the time it lost past 135 ms was
  dropped: the 3 h limit came at 3.3 h. Lost time is now carried over to the next passes, never dropped
- **medium:** a host could wear out the settings flash (a refused start saved pending settings, 20 to 40 times a second); a second USB
  command before the first ran replaced it (a stop could be lost); the installer page and `fwmon.py` could take an old
  acknowledgement for a new command; `isp.py` flashed into the settings area of a chip that reports it lower than the tested one
- **low:** the settings check missed two bits flipped 8 bytes apart (now a CRC-8; settings saved by 0.2-beta.2 load as defaults once);
  the storage target could be saved from the offer screen; a short USB report reused old bytes; the USB-C guard probed again and
  again with a 6S pack and a noisy reading (found by the fuzzer)
- **build:** a push cancelled a release in progress; `rot-latest` was deleted before being recreated (a dead link if cancelled); the
  8051 simulator package was installed before the published files were built; changes were not tested before a merge
- **documentation:** an automatic stop also clears a port's fault and its time limits (now in the guide); the rail alarm pauses, not
  stops; on 4.35 V a cell at 4.40 V faults rather than bleeds; the 3 h limit does not count time held at zero current by the input
  limits; which tests run on the simulated 8051; older releases recommended for going back
- **not fixed, and why:**
  - **sensor tolerances that stack:** the rail sensor reading 0.8 % low together with the chip's reference reading 1 % high takes a
    4.35 V cell to 4.40 V at rest without a fault (4.25 V on the 4.20 V target). Every reading shifts together, so no cross-check in
    firmware can see it; it is the per-unit accuracy already listed below, and the multimeter check is how you find it. Version B
    chips store their own factory-measured reference, which could correct it; the tested unit is a version A with none stored
    (diagnostics page 1: "CHIP A  NO STORED REF"), so on it the multimeter is the only way
  - **the millisecond clock wraps after about 25 days of continuous power:** once, a pending settings save or the USB-C guard's
    steady wait may come early or late. The simulator replaces that code, so a change could not be tested; power it off now and then
  - the mAh total can lose about 1 mAh on a very long pass (display only)
  - **no self-checks of the chip itself** (a checksum of the program at start-up, a RAM test, a clock check: what IEC 60730 calls
    "Class B" software for appliance controls): decided against, they would cost 100 to 200 bytes of flash the chip does not have
    spare. A corrupted program or bad RAM is caught only as far as the watchdog and the range checks on every reading catch it

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
- **0.2-beta.5 candidate, six cells charging at 0.6 A on 4.35 V from a 20 V PD charger** (diagnostics, 89 s after power-up): VCC
  4.484 V, input 19.36 V, rail 4.364 V (setpoint 14 counts over the 4.35 V target, as designed), rail actuator 23/400, rail ADC 4031 of
  4095 (64 counts of headroom at that VCC: why the 4.35 V target needs the per-target VCC check), the six currents about 0.64 A
  each, the input estimate 905 mA (3.6 A of charge at 4.36 V and 90 % efficiency would draw 0.89 A from 19.36 V: consistent), no
  limit holding a port, stack 41 bytes never used (the worst case worked out from the code leaves 29), no stored reference.
  Port 3, nearly full, already in its constant-voltage phase (duty 400 of 400, 391 mA and falling) while the others held 0.6 A.
  Holding B1 on the menu screen stopped the run (REQUIREMENTS.md SR-C2, on the charger).
- **Why the chip reset is now recorded** (after 0.2-beta.5's first test on the charger): a restart in the middle of that session
  could not be told apart from someone unplugging it. Diagnostics page 4 now shows the reset cause from the chip's own flags (STC8
  manual: WDT_CONTR.7 set by a watchdog overflow, PCON.4 by power coming on, PCON.5 by a low-voltage event): `RESET 16` power on,
  `128` watchdog (the firmware hung: report it), `32` low voltage, `0` other (the loader, a software reset).

## What it takes for granted, and what breaks if it is wrong

Each line: what is assumed, how well it is supported, what happens if it is wrong, and how to check it. Most of these are why
"supervise every charge" and "check with a multimeter" are not optional.

- **The port and rail voltage scale (4.5 V full scale).** Support: the charger's own targets fall on it (OpenWS3, on hardware); the
  resistor dividers' tolerance has never been measured; up to about 2 % is plausible. If wrong: every cell ends that much off its
  target, up to about 4.29 V on 4.20 V. Check: a rested full cell with a multimeter.
- **The chip's internal reference (1.19 V nominal).** Support: datasheet, about 1 % from chip to chip. Simulated, plus or minus 1 %
  gives 4.157 to 4.247 V on a 4.20 V setting. Version B chips store a factory measurement (diagnostics page 1); version A, like the
  tested unit, does not, so it is not used yet. If wrong: as above, all channels together.
- **Worst case of the two without a multimeter check: about plus or minus 3 %**, up to about 4.33 V on 4.20 V and 4.48 V on 4.35 V.
  This is why the multimeter check matters, before 4.35 V above all.
- **The rail and the six ports read alike.** Support: none measured. The cell's final voltage is set by the rail sensor; "full", the
  over-voltage faults and the bleed are judged by the port sensors. If they differ by about 0.7 % or more: a cell never reads "full"
  and is held at full voltage until the 3 h fault, or false over-voltage faults. Check: compare the rail and each port with a
  multimeter (Still open: per-unit calibration).
- **VCC is above about 4.45 V.** Support: one unit, 4.484 V on every supply. If lower: the ports cannot read above about 4.40 V;
  since 0.2-beta.2 such a reading counts as over-voltage, so the result is a fault, not a missed one. Check: diagnostics page 1.
- **The current sense offset is under 58 mA.** Support: assumed about 29 mA upstream; since 0.2-beta.2 it is measured and subtracted.
  If larger: charges end late (at the 3 h limit) or a pulled cell is not noticed. Check: page 2 with no cells.
- **The control pass length.** Support: never measured on the charger; since 0.2-beta.2 the time limits no longer depend on it.
  Short waits (a fault must hold 0.45 to 0.9 s) still count whole passes and stretch with slow passes. Check: a pass-length readout
  (Still open).
- **The strap (P3.4) means "USB-C powers the board".** Support: one unit, three USB-C sources and a pack. If wrong or floating: the
  USB-C bypass is requested wrongly; the guard and the diode limit what happens.
- **The input diode drops about 0.6 V.** OpenWS3, on hardware. The USB-C guard needs a rise of at least 0.2 V.
- **The thermistor model (10 k, B 3950).** Support: fitted to two points. If wrong: the 60 C stop could really be at 70 to 80 C. The
  cells' temperature is not measured at all. Check: a thermometer at two temperatures.
- **The rail actuator is inverted** (more duty, lower rail): OpenWS3, on hardware. If wrong, a fatal stop would drive the rail to its
  highest; the over-voltage supervision is then the only stop.
- **Register values** (watchdog, low-voltage reset, pin modes, interrupt priority, Timer 3): checked line by line against the STC8H
  manual (twice, by two reviewers); only a charger can prove them (the first boot after installing).
- **Pins after a watchdog reset are as at power-up.** Support: the manual only. If not: a reset could briefly switch outputs on.
  Check: an oscilloscope across a forced reset (Still open).
- **The settings area starts at 0xF400 (3 KB) on every unit.** Support: one unit. The installer asks before writing to a unit where it
  does not.
- **The cell curves and health thresholds** (time to full, GOOD / TIRED / RETIRE at 90 and 150 mOhm): generic, not measured on whoop
  cells; the resistance includes the lead and connector. If wrong: the estimate is off by minutes, and good packs read "RETIRE".
  These only display; nothing charges by them.
- **Users read the warnings, own a multimeter and supervise.** Support: none; many will not. That is why the installer asks for five
  boxes to be ticked and the default target is 4.20 V. Supervision remains the only backup for what the
  firmware cannot see (a failing transistor, a cell heating up).
- **VIFLY's firmware can be put back.** Not supported: no restore package has been tested, and the loader cannot read the old firmware
  out first. Treat installing as possibly one-way.
- **Passing the simulator means the chip behaves the same.** Partly: the simulator has no interrupts and replaces the register layer,
  so interrupt-timing bugs (like the one the audit found) are invisible to it. The chip-level tests and the build checks cover some of
  that gap.
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
  the storage voltage and starts again, exactly like doing it in the menu (simulated: `store`). Since 0.2-beta.2 the storage target
  is not saved and the user's own target comes back when the run stops (`store_back`), and there is no offer, finale or summary
  while any port is in fault (`fault_full`).
- **Estimates are marked as such.** Time to full has a `~` and a 25 % margin; the resistance includes the leads; the grade thresholds
  (GOOD under 90 mOhm, TIRED to 150, RETIRE above) are rules of thumb for whoop-size 1S cells, not a measurement of capacity.
- **Timing.** The full-screen pictures (finale, summary) are drawn a strip at a time, so a control pass stays under 50 ms (simulated:
  longest 41.5 ms).
- **Drawing and the clock.** The charge time limits used to count control passes, so a screen that drew too much made them late. The
  first version of the empty-port static did; the simulator caught it, and the drawing was made cheap (the static 7 rows a visit and
  rarely, the big icons only when they change). Since 0.2-beta.2 the limits count real time, so this is no longer what keeps them
  right; short waits and the 0.5 s fatal stop still count passes.
- **The tone timer is hardware only.** Melodies use Timer 3 and its interrupt, which the simulator does not have. A wrong register would
  most likely show as no sound, or as an interrupt storm that starves the watchdog: a reset loop at first boot (below).
- **Stack.** The big sprites add a few bytes to the deepest screen-drawing chain; measured 48, then 45 bytes free while charging one
  cell. STACK on page 1 shows only the deepest point reached so far, not the worst case: check it after a full run (below).

## Check your own charger

- **Coming from 0.2-beta.2 or older, every setting is back to its default after installing** (0.3 A, 4.20 V, 4.35 V allowed): set
  yours again. From 0.2-beta.3 they are kept.
- **First boot:** the splash plays once with its jingle and the main screen comes up. A splash every few seconds means a reset loop
  (most likely the tone timer): install OpenWS3 from its own releases (hold the left button while plugging in USB-C) and report it.
  Not a WS3 Unlocked older than 0.2.0-beta.2: those predate the audit fixes.
- **Multimeter (the important one):** charge a cell to full, let it rest 5 minutes, measure it at the cell. 4.17 to 4.23 V for 4.20 V,
  4.32 to 4.38 V for 4.35 V. Outside that, stop and report it with a photo of diagnostics page 1.
- **Diagnostics page 1** (menu, DIAGNOSTICS): VCC between 4.45 and 4.95 V (the range the correction was tested over); `STACK` after a
  long charge with several cells and the finale and summary shown, with the web page connected, 15 or more; `BYP ON` on USB-C alone, `BYP OFF` on the XT60 alone; `THERM` about 2000 to
  2300 in a room.
- **Outputs off:** with no cells in, after a charge, the ports read near 0 V on page 2.
- **Downloads:** `sha256sum -c SHA256SUMS` (a damaged download), and the proof that GitHub built it from this repository's workflow:
  `gh attestation verify openws3.html -R Pogaldock/ws3-unlocked --signer-workflow Pogaldock/ws3-unlocked/.github/workflows/rot-build.yml`.

## Still open (for anyone who wants to be sure)

- **Measure a rested cell with a multimeter after a 4.35 V charge** (above). Then compare the rail and each port with a multimeter on
  several units, and add a per-unit calibration from those readings (the biggest open assumption above).
- Find out what the loader answers to its erase commands, then check those answers in the installer and `isp.py`.
- Show the average control-pass length on diagnostics page 1, and count the short fault waits on real time too.
- Restore a unit to VIFLY's firmware once, with VIFLY's own tool, and document how (or confirm it cannot be done).
- A capacity cap per fill, and a check that the voltage rises as charge goes in (a cell that takes charge without rising is damaged).
- Calibrate the thermistor against a thermometer, and decide the stop temperature from it.
- Watch the power-path pins and the rail with an oscilloscope from power-up to the first instruction, and after a watchdog reset.
- Measure what the XT60 path does when USB-C is higher (does it conduct backwards into a pack?).
- Measure the stack under every interrupt with USB telemetry running during a full six-port charge.
- Run long soak tests: six ports, every supply, days.
- Have a human who knows battery chargers review `src/chg.c`, `src/sup.c`, `src/adc.c` and `src/app.c` (all review so far, both rounds,
  was by AI agents).
- Read the flash back after installing, in the installer.
- Run `app.c` and `adc.c` on the simulated 8051 too, not only on the PC (`sup.c` and `limits.c` run there weekly, Deep tests).
- Offer the *(upstream too)* fixes back to OpenWS3: written up, ready, in UPSTREAM.md.
- Log a real charge (`fwmon.py log`) and compare it with the simulator (`tools/simcompare.py`): it checks the model the tests rest on.
- The USB-C guard's last blind spot: a PD source falling back to 5 V while a pack sits less than 2 V below its old level (re-testing
  the source now and then would close it; it needs checking on a charger first).

## Reporting a problem

[Open an issue](https://github.com/Pogaldock/ws3-unlocked/issues) with what the charger did, the supply and cells, the firmware build
(on the installer page), and a photo of diagnostics page 1. Not to OpenWS3 or VIFLY: they did not make these changes.

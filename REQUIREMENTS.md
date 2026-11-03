# Safety requirements

What WS3 Unlocked must always (or never) do, one requirement per section, each with the code that enforces it and the tests that
prove it. This is the backbone the rest hangs on: SAFETY.md explains the hazards and the reasoning, this file is the checklist.

How to read a section:
- **ID**: a short permanent name (SR-V1 ...). A test, a commit or a bug report can say "breaks SR-V1".
- **Requirement**: pass or fail, in plain words.
- **Enforced in**: where in the firmware it happens.
- **Proof**: what checks it, written so a script can find it:
  - `sim:NAME`: a scenario of the whole-firmware simulation (`test/sim/sim_app.c`, `make sim`)
  - `unit:FILE.FUNCTION`: a unit test (`test/FILE.c`, `make test`)
  - `fuzz`: the fuzzer's rules, checked after every control pass (`test/sim/fuzz.c`, `make fuzz`)
  - `build:imgcheck`: a check run on every firmware build (`tools/imgcheck.py`)
  - `s51:hal`: the real `src/hal.c`, built by the firmware's compiler, on the simulated 8051 (`test/hal_8051.sh`, `make check-hal`)
  - `review`: checked by reading the code only; the section then says why no test can reach it

`tools/reqcheck.py` runs on every build: every requirement must have a proof, every proof must exist, and every simulation scenario
must belong to a requirement (or to "Other checks" at the end), so nothing is tested without a reason or claimed without a test.
Planted bugs (`test/mutants.py`) then show that the tests named here really catch a broken check.

## Voltage

### SR-V1: no cell charged above its target
- **Requirement:** a cell is never charged more than 50 mV above its target, at any chip supply from 4.45 to 4.95 V, with sensor
  noise and the chip's reference spread (plus or minus 1 %).
- **Enforced in:** `src/chg.c` (constant voltage, over-voltage fault), `src/adc.c` and `src/cal.c` (every reading corrected for the
  chip's supply), `src/sup.c` (the rail band).
- **Proof:** `sim:nominal` `sim:vcc_low` `sim:vcc_mid` `sim:vcc_high` `sim:vcc_step` `sim:bg_plus` `sim:bg_minus` `sim:noise`
  `sim:noise_vcc` `sim:full_rises` `unit:test_chg.t_cut` `unit:test_pure.t_cal` `fuzz`
- **Limit:** sensor errors that shift every reading together (the reference 1 % high and the rail sensor 0.8 % low) are not visible
  to the firmware; the multimeter check finds them (SAFETY.md, "Red team").

### SR-V2: a wrong rail sensor is caught by the port readings
- **Requirement:** if the rail's own sensor reads low, so the rail runs high, the port readings fault a cell that goes over.
- **Enforced in:** `src/chg.c` (the port over-voltage check uses the port readings, not the rail's).
- **Proof:** `sim:rail_sense_low`

### SR-V3: a cell at 4.40 V or more is never charged or bled
- **Requirement:** a cell reading 4.40 V or more, or a reading at the top of the ADC's range, faults its port: no current, no bleed.
- **Enforced in:** `src/chg.c` (V_FAULT, V_SATURATED).
- **Proof:** `sim:bleed_over` `sim:saturated` `unit:test_chg.t_bleed` `unit:test_chg.t_faults`

### SR-V4: 4.35 V only when allowed
- **Requirement:** with ALLOW 4.35V set to NO, no run is ever at 4.35 V, whatever the path: buttons, menu, USB, a race between
  them, saved settings, or a path nobody has found.
- **Enforced in:** `src/menu.c`, `src/app.c` (B2, USB command 4, the check in `start_run`), `src/cfg_pure.c` (settings validation).
- **Proof:** `sim:b2_volt` `sim:hv_race` `sim:hv_backstop` `unit:test_menu.t_edit` `unit:test_pure.t_cfg` `fuzz`

### SR-V5: the target and current never change during a run
- **Requirement:** during a run the target voltage and the charge current stay as they were at the start.
- **Enforced in:** `src/app.c` (start refused while a menu edit is open; B1, B2 and USB commands 3 and 4 refused while running),
  `src/menu.c` (items locked while running).
- **Proof:** `sim:menu_lock` `sim:menu_edit_start` `sim:usb_commands` `unit:test_menu.t_locked` `fuzz`

### SR-V6: a storage run gives your target back
- **Requirement:** when a storage run ends, by any path, the target is your own again, and the storage target is never saved.
- **Enforced in:** `src/app.c` (`stop_run`, `go_fatal`, `save_cfg`, the storage offer).
- **Proof:** `sim:store_back` `sim:store_saved` `sim:store` `sim:storage`

## Current

### SR-I1: a short is cut
- **Requirement:** a port drawing more than twice its current (a short) is faulted within about a second.
- **Enforced in:** `src/chg.c` (current supervision).
- **Proof:** `sim:short` `unit:test_chg.t_faults` `fuzz`

### SR-I2: no current into an over-discharged cell
- **Requirement:** a cell below 2.5 V gets no current.
- **Enforced in:** `src/chg.c` (`classify`).
- **Proof:** `sim:dead_cell` `unit:test_chg.t_refuse` `fuzz`

### SR-I3: a gentle start for a deeply discharged cell
- **Requirement:** a cell below 3.0 V (at rest) gets at most 0.1 A, damaged cells included, then the current ramps up gently.
- **Enforced in:** `src/chg.c` (pre-charge to 3.05 V under current, the soft ramp).
- **Proof:** `sim:gentle` `sim:pre_ir` `unit:test_chg.t_precharge`

### SR-I4: the input limits hold
- **Requirement:** with MAX IN AMPS set, the input current stays at the limit; with MIN IN VOLTS set, LIMIT holds the input at the
  floor and STOP RUN stops the run.
- **Enforced in:** `src/limits.c`.
- **Proof:** `sim:input_limit` `sim:min_in_volts` `unit:test_limits.t_est` `unit:test_limits.t_vmin` `unit:test_limits.t_cut`

## Time

### SR-T1: 3 hours of charging per cell
- **Requirement:** a cell stops after 3 hours of charging with current, top-ups included, counted on real time even when the screen
  is slow or the supply wobbles.
- **Enforced in:** `src/chg.c` (T_CHG_MAX_S), `src/app.c` (lost passes handed over).
- **Proof:** `sim:never_full` `sim:never_full_slow` `sim:never_full_wobble` `sim:held_never_full` `sim:leaky`
  `unit:test_chg.t_late` `fuzz`

### SR-T2: 30 minutes of pre-charge at most
- **Requirement:** a cell that stays below 3.0 V for 30 minutes of pre-charge faults.
- **Enforced in:** `src/chg.c` (T_PRE_MAX_S).
- **Proof:** `sim:pre_short` `fuzz`

### SR-T3: 5 hours of bleeding at most
- **Requirement:** a cell bled for 5 hours without coming down faults.
- **Enforced in:** `src/chg.c` (T_BLEED_MAX_S).
- **Proof:** `unit:test_chg.t_bleed_limit` `fuzz`

## Heat

### SR-H1: too hot, or a broken sensor, stops charging
- **Requirement:** at about 60 C on the board, or with the temperature sensor unplugged or shorted, charging stops, and it can start
  again only below about 50 C.
- **Enforced in:** `src/sup.c`.
- **Proof:** `sim:hot` `unit:test_chg.t_sup` `fuzz`

## Supplies

### SR-S1: no charging on a low input
- **Requirement:** with the input below about 4.6 V nothing charges.
- **Enforced in:** `src/sup.c` (VIN_MIN).
- **Proof:** `sim:input_loss` `unit:test_chg.t_gate` `fuzz`

### SR-S2: no charging when the chip's supply is too low to measure the target
- **Requirement:** with the chip's supply at 4.4 V or less, or too low for the rail sensor to read the target's setpoint, nothing
  charges: a waiting stop, not a rail runaway.
- **Enforced in:** `src/sup.c` (VCC_STOP and the per-target check).
- **Proof:** `sim:hv_lowvcc` `unit:test_chg.t_sup` `fuzz`

### SR-S3: a chip supply over 5.0 V locks everything off
- **Requirement:** with the chip's supply over 5.0 V everything turns off and stays off until the power is cycled.
- **Enforced in:** `src/sup.c` (VCC_HIGH), `src/app.c` (`go_fatal`).
- **Proof:** `sim:vcc_fatal` `fuzz`

### SR-S4: a rail out of range pauses charging
- **Requirement:** with the rail out of range nothing charges; the run resumes only once the rail has been back in range for a
  second.
- **Enforced in:** `src/sup.c`.
- **Proof:** `sim:rail_pause` `sim:broken_rail` `fuzz`

### SR-S5: a rail that fails its self-test, or never comes up, never runs
- **Requirement:** if the rail can't be regulated at power-up, or never comes up although the input is fine, no run can start and
  nothing is driven.
- **Enforced in:** `src/app.c` (the rail self-test), `src/sup.c` (SF_NO_RAIL).
- **Proof:** `sim:selftest_fail` `sim:no_rail`

### SR-S6: a pack on the XT60 never feeds the USB-C side
- **Requirement:** a supply on the XT60 never reaches the USB-C connector, beyond one probe of under 20 ms per change of the input.
- **Enforced in:** `src/adc.c` (the USB-C guard), `src/hal.c`.
- **Proof:** `sim:xt60_only` `sim:pack_on_laptop` `sim:pack_on_pd` `sim:pd_steps` `sim:pd_drop_pack` `sim:both_at_boot` `sim:backfeed_noise`
  `fuzz`
- **Limit:** a PD source that falls back to 5 V while a pack sits less than 2 V below its old level (a 4.5S-like pack on 20 V) is not
  told apart from load sag by the input reading alone; SAFETY.md, USB-C.

## Control

### SR-C1: nothing starts by itself
- **Requirement:** nothing charges unless someone started it: not after a power cut, a reset, or any other event.
- **Enforced in:** `src/app.c` (a run only from B1 or USB command 2).
- **Proof:** `sim:power_cycle` `fuzz`

### SR-C2: holding B1 stops a run on every screen
- **Requirement:** holding B1 stops a run on the main screen, the menu, port settings, diagnostics, the summary, and while a
  condition is shown.
- **Enforced in:** `src/app.c` (`handle_buttons`, `summary_key`), `src/input_pure.c` (the long press at the 5th repeat).
- **Proof:** `sim:user_stop` `sim:stop_any_screen` `sim:rail_stop` `unit:test_pure.t_input`

### SR-C3: a USB host can't drive an output
- **Requirement:** nothing a USB host sends switches a port or the rail directly; developer mode is gone.
- **Enforced in:** `src/app.c` (commands 7 to 9 unknown).
- **Proof:** `sim:no_dev` `fuzz`

### SR-C4: a USB stop can't be lost
- **Requirement:** a stop from a USB host is always carried out: it is never replaced by a later command, and one dropped behind
  another is sent again by the host; a command sent again is never run twice.
- **Enforced in:** `src/usbq_pure.c` (one waiting command, the first one wins; `src/usb.c`'s interrupt and the simulator both use
  it), `src/app.c` (a number runs once), `tools/openws3.html` and `tools/fwmon.py` (resend until acknowledged).
- **Proof:** `sim:usb_stop_burst` `unit:test_pure.t_usbq` `review`
- **Why review too:** only the loop in `src/usb.c` that reads the bytes out of the USB hardware into the slot; the simulator
  replaces that file.

### SR-C5: a pulled cell's port turns off
- **Requirement:** a cell removed mid-charge leaves its port off.
- **Enforced in:** `src/chg.c`.
- **Proof:** `sim:removal` `unit:test_chg.t_pulled` `fuzz`

## Integrity

### SR-F1: damaged settings load as safe defaults
- **Requirement:** a damaged settings page (any one or two bits flipped) loads the defaults, not the damaged values.
- **Enforced in:** `src/cfg.c` (CRC-8), `src/cfg_pure.c` (every value checked).
- **Proof:** `sim:cfg_damage` `sim:cfg_damage2` `unit:test_pure.t_cfg`

### SR-F2: a frozen chip resets
- **Requirement:** if the firmware stops running, the watchdog resets the chip within about 2.5 s; nothing in normal running starves
  it.
- **Enforced in:** `src/hal.c`, `src/app.c` (fed once a pass).
- **Proof:** `sim:nominal` `fuzz` (every scenario and every fuzz seed fails if the watchdog would have reset the chip) `s51:hal`
  (the real hal.c switches the watchdog on at its 2.5 s setting)

### SR-F3: a USB host can't wear out the settings flash
- **Requirement:** no sequence of USB commands writes the settings flash more than once per change a person could make.
- **Enforced in:** `src/app.c` (saves only after 1.5 s of quiet, or when a run actually starts).
- **Proof:** `sim:usb_wear`

### SR-F4: the image, the interrupts and the stack stay within the chip
- **Requirement:** the firmware never reaches the settings area; interrupt code never calls a shared compiler helper; the stack
  keeps at least the room it had in 0.2.0-beta.2.
- **Enforced in:** `Makefile`, `tools/imgcheck.py`, `tools/bundle.py`, `tools/isp.py`, the installer page.
- **Proof:** `build:imgcheck`

### SR-F5: a fatal stop leaves the rail at its lowest
- **Requirement:** a fatal stop drives the rail to its lowest voltage, not its highest.
- **Enforced in:** `src/hal.c` (`hal_all_off`).
- **Proof:** `s51:hal`
- **How:** the whole-firmware simulation replaces `src/hal.c`, so the real file runs on the simulated 8051 instead, and the test
  reads back what the "all off" function wrote into the rail and port registers. Whether the pin then really drives the rail low is
  hardware: checked on the charger.

### SR-F6: at a reset the power pins are driven low first
- **Requirement:** the first code to run after a reset drives every pin that can switch power low: the port PWMs and enables, the
  rail actuator, the global enable, the USB-C bypass and the power switch.
- **Enforced in:** `src/hal.c` (`__sdcc_external_startup`, the reset hook; SDCC 4.3 and later call it).
- **Proof:** `s51:hal` (the pins as the hook left them) `build:imgcheck` (the hook linked is hal.c's, not the library's empty one)
- **Limit:** between power-on and that first instruction the pins float (the chip's own behaviour); what the board does then is
  hardware, unchecked (SAFETY.md, assumptions).

### SR-F7: a sensor read that fails is read as a fault
- **Requirement:** an ADC conversion that never finishes (a failed converter) returns full scale, which the supervision treats as an
  over-voltage, instead of hanging the firmware.
- **Enforced in:** `src/hal.c` (`hal_adc`).
- **Proof:** `s51:hal`

### SR-F8: the clock never jumps
- **Requirement:** the millisecond clock counts on across the tick counter's wrap (every 24.9 days of continuous power), so no
  time-based decision misfires.
- **Enforced in:** `src/hal.c` (`hal_ms`).
- **Proof:** `s51:hal`

## Other checks

Simulation scenarios that check something other than a safety requirement:
- `sim:stats`: the mAh, resistance and time-to-full readings against the model, and the end-of-run screens.
- `sim:fault_full`: no finale or storage offer over a faulted port (that offer's restart used to clear the fault; also SR-V6).
- `sim:sag_finale` `sim:new_cell_finale`: the finale plays once per cell, not at every top-up.
- `sim:replay`: not a check; a charge described by `tools/simcompare.py`, to compare the simulation with a charge logged on a real
  charger.
- `sim:rail_sense_low` is under SR-V2; `sim:held_never_full` under SR-T1.

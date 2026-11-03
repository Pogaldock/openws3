**Unofficial, experimental firmware for the VIFLY WhoopStor V3. Read the README's "Before you install" section, APPROACH.md and SAFETY.md first.**
It replaces VIFLY's firmware and decides the voltage and current that go into your cells. It's vibe coded (written largely with an AI coding
assistant, directed and checked by me) and has run on one charger. Going back to VIFLY's firmware may not be possible. Charge in a LiPo bag or on a non-flammable
surface, supervise every charge, never overnight. No warranty, no responsibility taken: you install it at your own risk.

### 0.2.0-beta.5: more of the charger tested, two bugs fixed
- **USB-C guard (found by the fuzzer):** with a pack on the XT60 and a USB-C PD charger at 20 V, a PD charger falling back to 5 V let
  the pack feed it for about a second. Now the bypass goes off within about 14 ms.
- **The clock no longer jumps after 24.9 days of continuous power** (a pending settings save or the USB-C guard's wait could misfire).
- **A short current spike no longer counts against a port forever:** unchanged behaviour, now tested at its boundary.
- **More is tested:** the real hardware layer (`hal.c`) runs on a simulated copy of the chip (the pins at a reset, the watchdog, a
  failed sensor read, the fatal stop); the USB command slot is shared code tested on its own; coverage and static analysis run on every
  build; the fuzzer injects sensor errors; the slow tests run weekly.
- **New documents:** GLOSSARY.md (every term in plain words), REQUIREMENTS.md (what the charger must always or never do, each with its
  tests), UPSTREAM.md (the fixes that apply to OpenWS3 too).
- **Tools:** `tools/simcompare.py` compares a charge logged on your charger with the simulator.
- Settings are kept from beta.3 and beta.4.

### 0.2.0-beta.4: two gaps closed (replaces beta.3)
- **A USB stop can't be lost.** The installer page and `fwmon.py` resend a command that isn't acknowledged; the unit runs each
  command number once, so a resend never does anything twice (USB.md has the rule for other programs).
- **The input limits are now checked on the whole simulated charger:** MAX IN AMPS against the real input current, MIN IN VOLTS on a
  weak supply in both modes.
- Settings are kept from beta.3. From beta.2 or older they load as defaults once: set yours again.
- Everything else as in beta.3, below.

### 0.2.0-beta.3: fixes from a red-team round
Five attackers were sent at 0.2.0-beta.2 to break it. None found a way to overcharge a cell with the sensors in calibration. Fixed:
- **Hold B1 for about a second to stop, on any screen.** In the menu, port settings and diagnostics it used to only scroll.
- A damaged flat cell now stays on the 0.1 A pre-charge until 3.05 V (its reading under current could end it early).
- 4.35 V with the chip's supply at 4.40 to 4.42 V: a waiting `VCC LOW`, not a rail runaway and a false fatal.
- The time limits no longer lose time when the screen repaints after a supply step.
- USB: a refused start no longer writes the settings flash; a second command can't replace a stop; the installer page and
  `fwmon.py` can't take an old acknowledgement for a new command. `isp.py` refuses a chip whose settings area starts lower.
- The USB-C guard probes once, not again and again, with a 6S pack and a noisy reading.
- **Settings load as defaults once** after installing this (a stronger settings check): set yours again.
- Diagnostics pages 2 to 4 are shorter (rows that repeated other pages removed, to make room for the fixes).
- Docs: an automatic stop (`INPUT LOW`, `VCC LOW`, `TOO HOT`) clears a port's fault too; treat a cell that faulted once as suspect.
- Don't install WS3 Unlocked older than 0.2.0-beta.2: those predate the audit.
- Details, and what was not fixed: SAFETY.md, "Red team".

### 0.2.0-beta.2: fixes from an independent audit
- **Settings are reset to their defaults when you install this** (0.3 A, 4.20 V): set yours again.
- **ALLOW 4.35V** (menu, CHARGING): on by default (whoop cells are mostly LiHV); set it to NO if you charge only LiPo, and 4.35 V can
  no longer be selected. Check the target in the header before every start, and check your unit once with a multimeter.
- The time limits (3 h charge, 30 min pre-charge, new 5 h bleed limit) now run on real time, not on how fast the screen draws.
- A port reading at the top of the measurable range now counts as over-voltage (it could not fire there at a low chip supply).
- The current sense offset is measured with the output off and subtracted, so charges end where they should.
- The sound code can no longer corrupt a cell-voltage reading from inside an interrupt; every build checks this.
- No finale, summary or storage offer while a port is in fault; the storage target is not kept after the storage run; the finale
  plays once, not at every top-up.
- The voltage can no longer be changed mid-run by a run starting during a menu edit, and a USB host can't slip 4.35 V in during an
  ALLOW 4.35V edit.
- A stop pressed while the rail is paused is no longer lost.
- B2 changes the target voltage when stopped (as in OpenWS3), and cycles the views while charging.
- Installer: five confirmations before installing, a fixed size limit, a warning on a unit that differs from the tested one.
- The full list, what was not fixed and why, and every assumption with what breaks if it is wrong: SAFETY.md.

### Still open
The 4.35 V accuracy on the tested unit is not confirmed by a multimeter yet. No human expert has reviewed the code. Stopping a run
clears a port's fault (SAFETY.md, Not fixed): pull a cell that faulted once.

### Checked
60 whole-firmware scenarios, a fuzzer (a short batch on every build), the unit tests on PC and on a simulated 8051, and 49 of 50
planted bugs caught (the one left can't be reached by the simulator by design). An audit, a review round on the changes, and a red-team round, all by AI agents.

### Install and check
Download `openws3.html`, open it in Chrome or Edge, press Connect, follow the steps. `sha256sum -c SHA256SUMS` checks the download
is not damaged; `gh attestation verify openws3.html -R Pogaldock/ws3-unlocked --signer-workflow Pogaldock/ws3-unlocked/.github/workflows/rot-build.yml`
checks that GitHub built it from this repository (where it came from, not that it is correct).

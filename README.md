# WS3 Unlocked

Custom firmware for the VIFLY WhoopStor V3, a six-port 1S LiPo / LiHV charger (STC8H8K64U, 8051 core). It replaces the firmware the
charger ships with: a new look (the ROT theme: neon green, skulls, eyes, static, thorns, an animated SYMBIOZE splash) and a set of
safety changes, each tested in a simulation of the whole firmware and on a simulated copy of the chip.

> **This is a jailbreak of a lithium battery charger.** It is unofficial: not made, tested or supported by VIFLY, and not by the
> OpenWS3 authors either. It has run on one charger. Supervise every charge, never leave it charging unattended, and read
> [SAFETY.md](SAFETY.md) before you install it: what was changed, how each change was checked, what it assumes, and what is still open.
> You use it at your own risk.

**Using the charger?** [GUIDE.md](GUIDE.md): the buttons, the screens, every setting. **Installing?** [Install](#install) below.

## Built on OpenWS3: full credit

WS3 Unlocked is a fork of **[OpenWS3](https://github.com/openws3/openws3)**, the open firmware for this charger written by
**Paweł Sadowski and the OpenWS3 contributors**. Almost everything that makes this firmware work is theirs: working out the board
(pins, ADC channels, the inverted rail, the ports, the USB loader), every driver, the charge state machine, the rail / input / VCC
supervision, the input limits, the menu and the port settings, the USB monitor and developer mode, the browser installer and the ISP
flashing, the unit tests, and the documentation (`GUIDE.md`, `HARDWARE.md`, `USB.md` are theirs, with the edits noted below). Without
that work this fork would not exist.

- This fork starts from OpenWS3 commit [`b20df96`](https://github.com/openws3/openws3/commit/b20df960e0f57bcb687b09705a246208d612fbed)
  (2026-10-30). `git diff b20df96..rot-theme` shows every line changed since, and the git history shows who wrote what.
- Licence: MIT, as OpenWS3. `LICENSE` keeps the OpenWS3 copyright notice and adds one for the changes listed below.
- Not affiliated with OpenWS3 or VIFLY. Problems with WS3 Unlocked go to [this repository's issues](https://github.com/Pogaldock/openws3/issues),
  not to them: they did not make these changes.
- The safety fixes below that apply to OpenWS3 itself are marked *(upstream too)*, with the details in SAFETY.md, so they can be taken back into OpenWS3.

## What WS3 Unlocked changed and added

Everything not listed here is OpenWS3's, unchanged.

### The look
- **ROT theme**, the default (`src/theme.c`, `src/ui.c`, `src/lcd.c`): neon green on black, skull and eye sprites that blink and look
  around while charging, static noise, thorn borders, a spiky digit font, a charge bar and status icons in the header, a screen saver.
  The other four themes are OpenWS3's and still in the menu.
- **Animated SYMBIOZE splash** (`src/logo.c`, made by `tools/gen_logo.py` from `tools/symbioze_logo.png`).
- **Sprites and font** as editable text files: `tools/sprites.txt`, `tools/font_rot.txt`, turned into C by `tools/gen_sprites.py` and
  `tools/gen_font.py` (`src/sprites.c`, `src/font_rot.c`).
- The theme only draws: it reads the same values as the other themes and changes no charging behaviour. Its code was written to fit
  the chip's full internal RAM (no new direct-RAM use) and its stack (measured on the charger: 24 to 45 bytes free).

### Safety (details, evidence and limits for each: [SAFETY.md](SAFETY.md))
- **Charge voltage that no longer depends on the supply** *(upstream too)*: every ADC reading is corrected with the chip's internal
  reference (`src/adc.c`, `src/cal.c`). In simulation OpenWS3 charged cells to 4.57 V on a 4.20 V setting with the chip's supply at
  4.95 V; WS3 Unlocked holds 4.20 V from 4.45 to 4.95 V.
- **Rail-fault alarm that sounds** *(upstream too)*: with SDCC 4.6 the buzzer's "busy" test compiled to "always busy", so the alarm
  never started (`src/buzz_pure.c`). Found by the tests on a simulated 8051.
- **Over-discharged cells** (`src/chg.c`): below 2.5 V a cell is refused; from 2.5 to 3.0 V it charges at 0.1 A, at most 30 minutes.
- **Charge time limit** (`src/chg.c`): 3 hours of charging per cell, top-ups included, also when the input limits share the current.
- **Board over-temperature stop** (`src/sup.c`): at an estimated 60 C, or a broken thermistor, charging stops.
- **Watchdog, low-voltage reset, pin modes at the first instruction, no endless ADC wait** (`src/hal.c`).
- **USB-C backfeed guard** (`src/adc.c`, `src/hal.c`): a pack plugged into the XT60 while USB-C powers the charger is no longer fed
  backwards into the laptop or charger on the USB-C side.
- **Developer mode guards** *(upstream too)* (`src/dev.c`): a host can no longer push a cell past the cut by switching a port on and
  off, nor drain a cell below 3.0 V.
- **Fatal stop leaves the rail at its lowest** *(upstream too)* (`src/hal.c`): it was driven to its highest.
- **Settings check byte and flash wear limit** (`src/cfg.c`, `src/app.c`); **one interrupt priority** so their stack frames never
  stack up (`src/usb.c`); smaller fixes listed in SAFETY.md.

### Verification
- **Whole-firmware simulation** (`test/sim`, `make sim`): the real firmware against a simulated board, cells and supplies, 40 scenarios.
- **Tests on a simulated 8051** (`test/run_8051.sh`, `test/s51`, `test/chg_trace.c`, `make check-8051`): the unit tests and a
  charge-logic trace on the chip's own integer sizes, with the compiler the firmware is built with.
- **New unit tests** in `test/test_pure.c` and `test/test_chg.c`.
- **Mutation testing** (`test/mutants.py`): each safety check broken on purpose, to show the tests above catch it.
- **An independent review** of the charge logic, the hardware layer, USB / installer / build and the test evidence; what it found and
  what was fixed is in SAFETY.md.
- **Diagnostics page 1** shows the chip's version and stored reference, the USB-C bypass state and the stack headroom (`src/dbgscr.c`).

### Installer, release and documentation
- **Installer page** (`tools/openws3.html`): named WS3 Unlocked, links to this repository; checks the SHA-256 of the firmware inside it,
  shows it in full, and refuses an image that would reach the settings area. USB name "WS3 Unlocked" (`src/usb.c`).
- **Build and release** (`.github/workflows/rot-build.yml`): every test must pass before anything is published; the published files
  come with SHA256SUMS and a signed build-provenance attestation. OpenWS3's GitHub Pages workflow was removed from this fork.
- **Documentation**: this README (rewritten top), [SAFETY.md](SAFETY.md) (new), and small edits to `GUIDE.md` (the ROT theme, the
  alarm note) and `USB.md` (stop reason 6, developer-mode leave command, product name).

## Install

- **The easy way:** download `openws3.html` from the [latest release](https://github.com/Pogaldock/openws3/releases/tag/rot-latest),
  open it in Chrome or Edge, press Connect and follow the steps. A charger that runs the VIFLY firmware is put into its loader by
  holding the left button while plugging in USB-C; one that runs OpenWS3 or WS3 Unlocked restarts into it by itself.
- **Going back:** the VIFLY firmware can be put back with VIFLY's own update package; OpenWS3 from its own releases.
- **First charges:** follow the checklist in SAFETY.md (a multimeter check of a full cell, diagnostics page 1).

### Check what you downloaded
- `sha256sum -c SHA256SUMS` next to the downloaded files: they are the files GitHub built.
- `gh attestation verify openws3.html -R Pogaldock/openws3`: GitHub signed that this file was built by this repository's workflow, from
  the commit named in the release.
- The page itself shows the SHA-256 of the firmware it holds and refuses to install it if it does not match.
- To be independent of GitHub's build: build it yourself (below) with SDCC 4.6.0 and compare `build/fw.bin`.

## Build and test

```bash
make                 # build/fw.bin and build/openws3.html (SDCC 4.6.0; the chip's internal RAM is full: older versions may not link)
make test            # unit tests: charge logic, limits, menu, settings (plain C on the PC)
make sim             # the whole firmware against a simulated board: 40 safety scenarios
make check-8051      # unit tests and a charge-logic trace on a simulated 8051 (needs s51 from sdcc-ucsim; about 16 minutes)
```

Flash from the command line (Linux, read/write access to `/dev/hidraw*`):

```bash
python3 tools/isp.py write build/fw.bin --erase      # the unit in ISP mode: hold the left button while plugging in USB-C
python3 tools/fwmon.py flash build/fw.bin            # a running unit, nothing charging: no button needed
```

## Monitoring over USB

The firmware is also a USB HID device (`34bf:1004`, product "WS3 Unlocked"): `python3 tools/fwmon.py watch` shows boot phases, port states,
voltages, currents, events and faults live; `start`/`stop`/`current`/`voltage` drive it. Details and report layout: `USB.md`.

`python3 tools/devtool.py` is a terminal UI for bench work: it switches every output by hand (port PWMs and bleed switches,
global enable, fan, input profile, the rail as a setpoint or a raw duty) and shows every input live (port and rail voltages,
port currents, input, VCC, thermistor, buttons, strap). The firmware keeps guards underneath (a 0.5 s link watchdog, a cut 50 mV above the selected target voltage or at 2.8 A per
port, checked whether the port is on or not, no bleeding below 3.0 V, at most 0.2 A into a cell below 3.0 V, the rail over-voltage stop); holding both buttons on the unit leaves the mode. Not while charging.
`tools/openws3.html` is the tool for users: open the file in Chrome or Edge (WebHID, no server, no driver) and press Connect. It opens
in a simple view: start / stop, charge current and target voltage, each port's state, voltage and current with a 10 s plot, and a
firmware installer for users (a guided "hold the left button while plugging in USB-C" flow for a charger that does not run this firmware yet; a running unit restarts into the loader by itself). Esc stops charging.
The Advanced view adds the raw inputs, the limiter, setpoints, an event log and developer mode (the manual controls above).
`make` also writes `build/openws3.html`, the same page with `build/fw.bin` inside: that is the one to hand out.

## Menu

Press **B1 and B2 together** (within about 0.2 s of each other) and release. Holding both for 0.8 s (a beep) closes the menu and saves it.

The menu has four categories; **OK** opens one and **BACK** returns to the list of categories (from there, **BACK** closes the menu and saves).

| Category | Item | Setting |
|---|---|---|
| CHARGING | VOLTAGE | 4.20 V / 4.35 V / 3.80 V storage / 3.85 V storage (not while charging) |
| | CURRENT | charge current, 0.3 to 1.3 A in 0.1 A steps (not while charging) |
| LIMITS | MAX IN AMPS | limit on the estimated input current: off, 0.5 to 3.0 A in 0.1 A steps, then 0.5 A steps up to 10 A. The ports are limited smoothly |
| | MIN IN VOLTS | minimum input voltage: off, 4.80 to 5.00 V in 0.05 V steps, to 6.0 V in 0.1 V steps, then 0.5 V steps up to 25 V |
| | BELOW MIN | what happens when the input is below that minimum: `LIMIT` reduces the charge current smoothly until the input recovers; `STOP RUN` stops charging after a second and shows `V LIMIT` |
| | REGULATION | how the limits share the budget over the ports: `EQUAL`, `CH1 FIRST`, `HIGH V FIRST` (below) |
| PREFERENCES | SOUND | on / off |
| | DISABLE USB POWER | `YES` keeps the USB-C input switched off for good (the XT60 powers the unit, USB is data only); `NO` (default) switches it on at start when the board straps it so |
| | THEME | the look only, no behaviour change: `ROT` (WS3 Unlocked's, the default), `CLASSIC` (the OpenWS3 look), `CALM` (colour only where it means something), `AMBER` (one hue on black, red for trouble), `HIGH CONTRAST`. Applies at once |
| DIAGNOSTICS | | the debug pages (below) |

Keys: the two buttons sit under the screen, **left = B1, right = B2**. A tap steps once; holding a button repeats, faster the longer it is held.
Both buttons together are **OK**, both held for 0.8 s are **BACK**. In a list **B2** moves down and **B1** up (it wraps around, the position is shown as n/m);
**OK** opens a category or edits the item and **BACK** goes one level up. While editing, **B2** raises the value and **B1** lowers it, **OK** confirms and **BACK** cancels (the old value comes back).
The bottom row of the screen shows these cues in its corners. Values apply at once and are saved when the menu closes; only changed settings are written.

**The limits.** Both limits work on one power budget, the most the charging ports may draw together (shown on diagnostics page 6). The input
current is estimated as rail voltage x the sum of the measured port currents / (90 % x input voltage).
The current limit sets the budget directly (limit x input voltage). With BELOW MIN on `LIMIT`, the voltage floor keeps a stored budget that shrinks
while the input is below the floor (a weak source recovers when it is loaded less) and grows slowly once it is back 0.1 V above it; the smaller of the
two budgets rules. With `STOP RUN` the floor does not regulate: charging stops when the input stays below it for a second.

The ports only grow into the budget, never past it. Every control pass the measured port currents are compared with it: a port may raise its
current a step only while there is room for that step, and the room goes to the ports in priority order. Above the budget (it shrinks when the
input sags), the lowest-priority port is brought down; a higher-priority port that wants more takes its room from the lowest-priority one the
same way. Room is handed out only once it is free as measured, so a newly connected cell cannot draw more before the others have given it up.
REGULATION is the priority order:

| REGULATION | Who gets the room first |
|---|---|
| EQUAL | the port with the lowest current: the currents even out (a port that wants less leaves the rest to the others) |
| CH1 FIRST | port 1, then port 2 ...; the highest port number is reduced first, down to switched off |
| HIGH V FIRST | the cell with the highest voltage |

A port in its taper draws less than it is set to, and the others get what it does not use. A port the limits hold with no current shows
`LIMIT` and starts as soon as there is room. The supervisor's input gate (about 4.6 V) still applies underneath.

**Running from a LiPo pack:** nothing detects the pack or its cell count. Set MIN IN VOLTS to the pack's cut-off (about 3.3 V per
cell: 6.6 V for 2S, 13.2 V for 4S, 19.8 V for 6S) with BELOW MIN on `STOP RUN`, so charging stops before the pack is
over-discharged. The unit itself stays on and draws a little; unplug it from the pack when done.

### Diagnostics pages

Opened from the menu. B2 is the next page, B1 the previous page, BACK (both held) goes back to the menu.

| Page | Shows |
|---|---|
| 1 power | bandgap count and the VCC it implies, VIN, rail and its PWM, thermistor count and fan, chip version and its stored reference, strap, USB-C bypass (`BYP ON / OFF / CUT`), stack bytes never used since boot, uptime |
| 2 port V | raw ADC count and voltage per port |
| 3 port I | raw ADC count, mA, PWM duty, state, bleed switch |
| 4 status | run, target voltage, current setting, targets, stop/alarm, strap, USB |
| 5 ADC raw | all 16 channels |
| 6 limits | estimated input current, the two limits and their modes, the budget (as power and as total port current), which ports the limits hold (H) or keep waiting (W) |

## Using it

| Input | Action |
|---|---|
| Button 1, tap | current setting +0.1 A (0.3 … 1.3 A, wraps); ignored while charging. Taps are saved 1.5 s after the last one, or when charging starts |
| Button 1, hold (0.35 s) | start / stop charging |
| Button 2, tap | target voltage: 4.20 V → 4.35 V → 3.80 V storage → 3.85 V storage; ignored while charging |
| Button 2, hold (0.5 s) | port settings (below) |
| Both buttons, tap | open the menu |

Header: set current (with a `*` when some port has its own current or is off), input voltage, target voltage (red while charging).
Cells: port voltage, and below it the measured current while charging, for one second in four the current the port charges at
(`>0.8A`), and `LIMIT` when a user limit keeps the port waiting with no current. An idle port shows the current it will charge at
(`>0.5A`), or `OFF` (dimmed) if it sits the run out; otherwise `TEST` / `REST` / `FULL` / `BLEED` / `ERR`. The dot shows the state.

**Port settings.** Each port can have its own current, or be left out of the run. Hold button 2 on the main screen: the cells
show each port's value (`AUTO` = the set current in the header, `SET` = its own, `OFF`) and port 1 is framed.

| Input | Selecting a port | Editing its value |
|---|---|---|
| Button 1 / 2, tap | previous / next port | value down / up: `OFF` < `AUTO` < 0.3 … 1.3 A (holding repeats) |
| Both buttons, tap | edit this port | confirm |
| Both buttons, hold | done (saves) | cancel (the old value comes back) |
| Nothing for 10 s | done (saves) | cancel, done |

While charging the values are shown but cannot be changed (`LOCKED`). Button 1 on the main screen still sets the current that the
`AUTO` ports use.

## Layout

```
src/board.h        pins, ADC channels, PWM — single source of truth
src/hal.c          the only code that touches registers          src/lcd.c   ST7735S driver (asm byte shifter)
src/chg.c          per-port charge state machine (pure)           src/sup.c   rail / input / VCC supervision (pure)
src/limits.c       input current / voltage limits (pure)          src/menu.c  settings menu (pure), rowbuf.c text rows
src/ctl.c          rail PI, cal.c units, fmt.c text, input_pure.c buttons, buzz_pure.c patterns, cfg*.c settings
src/usb.c          USB HID monitor (telemetry + commands)         src/ui.c    screens
src/dev.c          developer mode: outputs set by the host, guards  src/portset.c  port settings mode (pure)
src/app.c          boot sequence, 4.5 ms control pass             src/font.c  GENERATED from tools/font5x7.txt
test/              host tests (C)                                 tools/      fwmon.py (USB monitor / flasher), devtool.py, openws3.html (web tool), bundle.py, isp.py, render_screens.* (the pictures in GUIDE.md)

WS3 Unlocked additions:
src/adc.c          also: VCC correction of every reading, the USB-C bypass guard
src/theme.c, ui.c, lcd.c   the ROT theme             src/sprites.c, font_rot.c, logo.c   GENERATED (tools/gen_sprites.py, gen_font.py, gen_logo.py)
test/sim/          the whole firmware against a simulated board (make sim)
test/run_8051.sh, test/s51/, test/chg_trace.c   tests on a simulated 8051 (make check-8051)
```

"Pure" files have no hardware access and are what the host tests compile.

## Build rules

1. No initialised XRAM variables (SDCC's startup would drive `P2` high, which is a power-path port on this board).
2. Large memory model (not small: out of direct RAM; not medium: `MOVX @Ri` uses `P2`).
3. The timer ISR lives in `main.c` (SDCC only emits the vector there).
4. `int` is 16 bits here: any sum that can pass 65535 is done in `uint32_t`.

## License

MIT, see `LICENSE`.

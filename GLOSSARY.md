# Glossary

The words used in this project's documents, in plain language, grouped by topic. If a word you meet is missing, open an issue.

## Batteries and charging
- **Cell:** one lithium battery (1S). A whoop battery is one cell.
- **LiPo / LiHV:** two kinds of lithium cell. A LiPo is full at 4.20 V; a LiHV ("high voltage") at 4.35 V. Charging a LiPo to 4.35 V
  overcharges it.
- **Overcharge:** pushing a cell above its full voltage. It damages the cell and, far enough, makes it swell, vent or catch fire.
- **Over-discharged cell:** a cell drained below about 2.5 V. It may be damaged inside; charging it is risky.
- **Capacity (mAh):** how much charge a cell holds. A 300 mAh cell gives 300 mA for an hour.
- **C-rate:** current relative to capacity. 1C for a 300 mAh cell is 0.3 A; 2C is 0.6 A.
- **CC / CV (constant current, constant voltage):** how lithium cells are charged. First a fixed current until the cell reaches its
  target voltage, then the voltage held there while the current falls by itself.
- **Taper / termination current:** the falling current in the CV phase; the charge ends when it drops to a set value (73 mA here).
- **Pre-charge:** a gentle start (0.1 A) for a deeply discharged cell, until it is back above 3.0 V.
- **Storage voltage:** the level a lithium cell keeps best at when unused for weeks: 3.80 V (LiPo), 3.85 V (LiHV).
- **Bleed:** discharging a cell through a resistor to bring it down (to storage, or back under its target).
- **Internal resistance:** how much a cell's voltage rises above its resting value when current flows. It grows as a cell ages, which
  is why the charger can say GOOD / TIRED / RETIRE.
- **Resting voltage (open-circuit voltage):** a cell's voltage with no current, after a few minutes. The true measure of its charge.
- **Polarisation:** the part of a cell's voltage rise under current that fades slowly after the current stops.

## The charger's hardware
- **Port:** one of the six charging outputs.
- **Rail:** the shared supply all six ports charge from. The charger holds it just above the target, which is what stops a cell from
  going higher.
- **Buck converter:** the circuit that makes the rail from the input voltage.
- **PWM (pulse-width modulation), duty:** switching an output on and off very fast; the share of time it is on (the duty) sets how
  much gets through. Each port's current and the rail are set this way.
- **Input / VIN:** the power coming in, from USB-C or the XT60 socket.
- **USB-C PD (Power Delivery):** a USB-C charger that can supply more than 5 V (9, 15, 20 V) after agreeing it with the device.
- **XT60:** the large socket for a battery pack or bench supply.
- **Backfeed:** current flowing the wrong way, from a pack on the XT60 into the USB-C charger. The USB-C guard stops it.
- **Bypass:** the switch that connects USB-C to the input directly instead of through a diode (a one-way valve that costs 0.6 V).
- **Thermistor:** a temperature sensor. This charger has one on its board, none on the cells.
- **Fan:** cools the board; switched by the board temperature.

## The chip
- **Microcontroller / chip:** the small computer that runs the charger (an STC8H8K64U).
- **8051:** the family of processor inside it: old, simple, 8-bit. It adds two 8-bit numbers in one step and needs many for 32-bit
  ones, which is why the code avoids large arithmetic where speed or memory matters.
- **Firmware:** the program stored in the chip.
- **Flash:** the chip's memory for the program (64 KB here, with the last 3 KB kept for the settings).
- **RAM, internal RAM, XRAM:** working memory. Internal RAM is tiny (256 bytes) and fast, and holds the stack; XRAM is bigger (8 KB).
- **Stack:** the part of RAM that remembers which function called which. If it runs out, the program crashes.
- **Register:** a memory address that controls the hardware: writing a number there sets a PWM, switches a pin, starts a reading.
- **Pin:** one electrical connection of the chip. It can be an input, or an output driven high or low, or left floating.
- **ADC (analog-to-digital converter):** turns a voltage into a number the program can use. Every voltage and current the charger
  knows comes through it.
- **Bandgap / internal reference:** a voltage inside the chip, meant to be constant; in practice it varies between chips.
- **VCC:** the chip's own supply voltage, about 4.5 V here. The ADC measures relative to it.
- **Interrupt:** a piece of code the hardware runs in between the main program's steps (on a timer tick, a USB packet). Code shared
  by both has to be written so they cannot corrupt each other's data.
- **Watchdog:** a hardware timer that resets the chip if the program stops checking in, so a frozen program cannot leave a port on.
- **Reset hook:** the first code that runs after a reset, before anything else; here it drives the power pins low.
- **Control pass:** one round of the main loop (about every 4.5 ms): read every sensor, decide, set every output.
- **Compiler (SDCC):** the program that turns the C source into the chip's firmware.

## Testing and checking
- **Unit test:** a small program that checks one piece of the code on its own, on a PC.
- **Simulator (here):** a model of the whole charger on a PC (chip, cells, supplies, buttons, USB) with the real firmware running on
  it, so dangerous situations can be tried safely.
- **Scenario:** one scripted situation in the simulator with a pass or fail check at the end.
- **s51:** a simulator of the 8051 chip itself: the firmware's own compiled code runs there, with the chip's 16-bit arithmetic.
- **Fuzzer / fuzzing:** testing with random inputs: random buttons, commands, cells and faults, with the safety rules checked all the
  time. It finds what nobody thought to script. A **seed** is the number that makes one random run repeatable.
- **Mutation testing / planted bug:** breaking a safety check on purpose to see whether the tests notice. If they do not, the tests
  were not really checking it.
- **Coverage:** which lines (and which branches, each way an `if` can go) of the code the tests actually run.
- **Static analysis:** a tool (cppcheck here) reading the code without running it, looking for known bug patterns.
- **Requirement (SR-...):** something the charger must always or never do, written so it can be checked; REQUIREMENTS.md lists each
  one with the tests that prove it.
- **Traceability:** being able to follow each requirement to the code that enforces it and the test that proves it.
- **Red team:** people (here, AI agents) told to attack and break something rather than review it.
- **Regression:** a bug that comes back after being fixed. Each fix here gets a test so it cannot come back unnoticed.
- **Calibration:** correcting a sensor against a trusted measurement (a multimeter). This charger is not calibrated per unit yet.
- **Tolerance:** how far a part may be from its stated value (a resistor of 1 % tolerance, say).

## Code, GitHub and releases
- **Repository (repo):** the project's files and their whole history, on GitHub.
- **Commit:** a saved snapshot of the code with a note of what changed; it has an ID like `e3c0ccd`.
- **Branch:** a line of commits. `rot-theme` is this project's main one.
- **Push:** sending commits from a computer to GitHub.
- **Pull request (PR):** a proposed change shown as a difference, tested before anyone merges it.
- **Fork:** a copy of someone else's repository that you then change; WS3 Unlocked is a fork of OpenWS3.
- **Upstream:** the project a fork came from (OpenWS3).
- **CI (continuous integration):** GitHub running every check automatically on each push (GitHub Actions).
- **Workflow:** one set of those automatic steps (the build, the planted-bug run, the deep tests).
- **Build:** turning the code into the files that get installed (`fw.bin`, and `openws3.html` with the firmware inside).
- **Release:** a fixed, numbered version with its files (v0.2.0-beta.4). **Beta:** a version still being tested.
- **Tag:** a name pinned to one commit; a release hangs on a tag.
- **Checksum (SHA-256):** a fingerprint of a file; if one byte changes, the fingerprint changes. Proves a download is not damaged.
- **Attestation:** GitHub's signed record of which code a file was built from. Proves where a file came from (not that it is right).
- **Branch protection:** rules on a branch, such as "changes only through a tested pull request".

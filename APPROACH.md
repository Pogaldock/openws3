# How I think about this project

This is the "catch me up" document. SAFETY.md has every number, test and threshold; this file is the thinking behind them, in plain
words, for anyone who wants to use WS3 Unlocked, review it, or try to break it.

I'm SYMBIOZE (@fpv.symbioze). I fly whoops, I make FPV videos, and I wanted my WhoopStor V3 to look like the rest of my stuff:
skulls, static, neon green. That was the whole plan at the start.

## The moment it stopped being a skin

The screen and the charging live in the same firmware. The day I opened OpenWS3's code to change the colours, I was also holding the
code that decides how many volts go into a lithium cell. A wrong colour is ugly. A wrong voltage swells a pack or sets it on fire.

So most of the work on this project went into safety, not into the look. The look came almost for free; the checking did not.

## How it was built, honestly

It's vibe coded: most of the code was written by an AI coding assistant (Claude), directed by me. That doesn't mean it was thrown
together. I've spent a long time thinking about this and researching how to do it properly: how lithium cells should be charged
(constant current then constant voltage, a gentle pre-charge for deeply discharged cells, time and temperature cut-offs), how this
chip measures voltage and what that measurement depends on, and how people check code that can hurt someone (simulation, planted-bug
testing, independent review, measuring the real hardware).

I know some code, so I've reviewed parts of it myself. Some of it is beyond me. In those cases I made sure I could still check the
output: does the cell stop where it should, does the fault fire when it should, do the numbers make physical sense. I don't need to
follow every line of a function to see whether it gives sane results.

That way of working isn't new to me. I'm a cognitive science student, and that's most of what the field is: you can't open up a brain
and read the code, so you build a model of the process, predict what it should do, and test whether its outputs hold up. A charger
with firmware I only partly read is a much easier black box than a person. The simulator in this project is exactly that kind of
model, and the planted-bug tests are how I check that the model would notice when something is wrong.

If that's a problem for you, don't install it. Simple as that.

## Where this sits

When you work with emerging technology, cutting-edge tools, new ways of building things, you've gone over the guardrails. The
barriers that are normally there to protect you (a manufacturer's testing, certification, years of other people using the same
thing) aren't there yet. You're in the jungle, exploring territory nobody has mapped.

That's what this is. An AI-built firmware for a lithium charger, checked with methods I put together myself, on one unit: it's the
Wild West. I've done everything I can to make it as safe as I can, and the rest of this document is how. But it is not the safe,
paved road, and nothing I write can make it one.

If you don't feel comfortable being in an unsafe, exploratory environment like that, you probably shouldn't fuck around with this.

I still don't trust it by default, and neither should you. An AI writes code that looks confident whether it's right or
not. So the rule for this project has been: **assume it's wrong until something independent shows it isn't.** Everything below is
about how "something independent" was done.

## The framework: layers that catch different things

No single check catches everything, so there are several, each looking from a different angle:

- **A simulator of the whole charger.** The real firmware runs on a PC against a model of the board, the supplies and the cells.
  It presses the buttons like a person and sends USB commands like a program. Each scenario is a way things go wrong in real life: a
  short, a dead cell, a cell that never fills, a supply that sags, a rail that sticks, a pack plugged in backwards through USB-C.
- **Tests on a simulated copy of the chip.** The 8051 does arithmetic with 16-bit numbers where a PC uses 32. A bug can pass every PC
  test and still be wrong on the charger. This layer already caught one (an alarm that never sounded, also in OpenWS3).
- **Planted bugs.** I break each safety check on purpose, one at a time, and the tests must notice. If a broken check passes, the test
  was decoration. When this caught gaps in my own tests, I added the missing tests before releasing.
- **A random tester (a fuzzer).** Instead of scripted situations, thousands of random ones: buttons, USB commands, cells in and out,
  supply drops, sensor errors, with every safety rule checked after every step. It finds the situations nobody thought to script; it
  found a real USB-C bug the scripted tests had missed.
- **Attackers, not just reviewers.** A red-team round where AI agents were told to break it, any way they could, not to review it.
- **Independent reviews.** Several rounds of AI reviewers, each given only the code and one area, never my conclusions, so they
  aren't just agreeing with me. I check every finding against the code before acting on it, because reviewers are wrong sometimes too.
- **Measurements on my charger.** Supply voltage, the input reading, the rail, the stack headroom, the temperature sensor, the
  USB-C guard on a laptop. Simulation says the logic is right; only the hardware says the model is.
- **The build itself.** GitHub builds the firmware only after every test passes, refuses an image too big for the chip, refuses
  one known kind of code that is unsafe inside an interrupt (the kind the last audit found), and signs a record of what it built.
- **A written list of promises.** REQUIREMENTS.md says what the charger must always or never do, and names the tests that prove
  each one. The build fails if a promise has no test, or a test has no promise. It's how the safety-critical world does it (they call
  it traceability), and it caught a gap the first day: "holding B1 stops the charge" was true on one screen out of five.
- **Measuring the tests themselves.** Coverage shows which lines and branches no test runs; a static checker reads the code for known
  bug patterns. Both run on every build, with limits taken from what the functional-safety standards recommend.

Every bug that got found went the same way: reproduce it, fix it, write a test that would have caught it, plant it again as a bug the
tests must catch. That loop is the most important thing about this project. Not that it's bug-free, but that each bug leaves a test
behind.

## What we actually know (measured, on one unit)

- The chip's own supply sits at about 4.48 V on every supply I tried (two PD chargers, a laptop, a 4S pack).
- The input voltage reading is within about 2 % of the real supply.
- The rail sits at 4.36 V on the 4.35 V setting, and a charging cell read 4.338 V.
- The firmware runs with stack to spare (41 to 48 bytes never used while charging, depending on the version; the worst case worked
  out from the code leaves 29).
- The USB-C guard keeps a pack on the XT60 from feeding backwards into a laptop.
- The new firmware boots, plays its sounds and draws its screen without resetting.

That's one charger. Treat it as one data point, not a guarantee for yours.

## What's assumed (and why that matters)

These are the things the whole safety case leans on that nobody has measured yet. SAFETY.md lists each one with what breaks if it's
wrong; here's the short version:

- **The voltage scale is right.** The resistors that scale each port's voltage have a tolerance, and the chip's internal reference
  varies by about 1 %. Together that could put a cell up to about 3 % off target on an unchecked unit. This is the big one.
- **The rail sensor and the six port sensors agree with each other.** If they don't, a charge can end late or fault for no reason.
- **The temperature sensor model is right.** It's fitted to two readings. The 60 C stop could really be 70 to 80 C.
- **Pins behave after a crash-reset the way the manual says.** Never checked with a scope.
- **Other units are like mine.** Different batches can use different parts.

And one honest open question: on my own charger, a LiHV cell charged on 4.35 V and rested read 4.33 V on the charger, and 4.40 V and
4.25 V on two different drones. The drones disagree with each other, so they can't settle it. A multimeter will.

## What would help us know more

If you want to help, these are worth more than another code review:

- **A multimeter reading** on your unit: a rested full cell, and what the charger showed for it. Even a handful of these from
  different units would turn the biggest assumption into data.
- **A logged charge** (`tools/fwmon.py log`) run through `tools/simcompare.py`: it shows how closely the simulator, which every test
  rests on, matches a real charger.
- **An oscilloscope** on the outputs across a reset, and on the rail at power-up.
- **A thermometer** against the temperature reading at two temperatures.
- **"Check the unit" output** from the installer page, so we know which hardware is out there.
- **Someone who knows chargers** reading `src/chg.c` and `src/sup.c`. All review so far has been by AI.
- **A tested way back to VIFLY's firmware.** I don't have one. Until someone does, installing is one-way.

## Choices I made on purpose

Some decisions trade a little safety for usability, and I'd rather you know which:

- **4.35 V is available by default.** Whoop cells are almost all LiHV, so hiding it would just annoy everyone. If you only charge LiPo,
  switch it off in the menu (ALLOW 4.35V = NO) and it can't be selected at all.
- **B2 changes the voltage when stopped**, like OpenWS3. Fast, but one tap too many puts a LiPo on 4.35 V. Look at the target in the
  top-right box before you start.
- **I removed OpenWS3's developer mode**, which let a USB program drive the outputs by hand. Its guards had holes, and nobody needs it to
  charge batteries.
- **I removed the other colour themes** to make room for the safety work and the new features in the chip's small flash.
- **Defaults are the boring, gentle ones:** 0.3 A and 4.20 V.

## Security and the files you download

The firmware is built by GitHub from this repository, only after every test passes, by a job that can't publish anything. A separate
job publishes it with checksums and a signed record (an attestation) of exactly which code it came from. The installer page checks the
firmware inside it before writing, refuses anything too big for the chip, and asks you to confirm the important points first.

What that does and doesn't mean: the checksum catches a damaged download; the attestation proves where a file came from. Neither
proves the firmware is correct. That's what all the testing is for, and even that is a reduction of risk, not a proof.

## If you want to attack it

Good, please do. Some places to start:

- **Try to overcharge a cell in the simulator.** `test/sim/sim_app.c` has the scenarios and helpers. Any sequence of buttons, USB
  commands, supply dips or cell swaps that ends above target is the most valuable bug you can find.
- **Break a check and see if the tests notice.** `test/mutants.py` is the list; add your own.
- **Read the assumptions in SAFETY.md and measure one.**
- **Compare the docs to the code.** If the guide says one thing and the firmware does another, that's a safety bug too.

Open an issue with what you found, and how to reproduce it if you can. I'd rather hear it from you than from a swollen pack.

## What I can and can't promise

I can promise I've done this carefully and that everything I know is written down, including the parts that don't flatter it. I can't
promise it's correct. It's tested on one charger, reviewed by AI, and written by someone who learnt a lot of this along the way.

So use it like you'd use any charger you don't fully trust: supervise it, charge in a LiPo bag or on something that can't burn, never
overnight, and check it once with a multimeter. If something looks wrong, stop and tell me.

And if you know more than me about any of this, I really want to hear from you.

SYMBIOZE

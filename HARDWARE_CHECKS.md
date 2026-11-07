# Hardware checks

What the tests cannot prove: they run against a model of the charger, and a model only knows what it was told. These checks compare
the charger with something outside it. Each says what it proves, what it needs, how to do it, and what counts as a pass. Record the
result, the firmware version and the date under the check. Diagnostics: menu, DIAGNOSTICS, B2 for the next page.

## Already confirmed (one unit, USB-C)
- A full charge of every port to ALL FULL (8 packs, 59 min), and a B1 hold stopping a charge from the menu.
- Clock: 39.97 MHz (the loader's handshake).
- Chip A, no stored reference. Internal reference 1087 counts (1071 assumed until 0.2-beta.5).

## No multimeter needed
- [ ] **All ports read alike.** Proves: each port's resistor divider agrees with the others.
  - Needs: one cell, resting (not charged or flown for 30 min).
  - How: put it on port 1, read its volts on page 2; repeat on ports 2 to 6, one at a time, other ports empty.
  - Pass: all six within 15 mV of each other. A port further out is the one to measure first once there is a meter.
- [ ] **The chip's supply is the same on XT60 as on USB-C.** Proves: readings don't shift with the input (on this chip a supply up to
  1.5 % higher would not be corrected, and would charge cells that much higher).
  - Needs: a pack or bench supply on the XT60.
  - How: page 1, `BANDGAP` on USB-C alone; then on the XT60 alone. Note both.
  - Pass: within 3 counts. A lower count on XT60 means a higher supply there: report it before charging from XT60.
- [ ] **The temperature sensor is what the code guesses.** Proves: the over-temperature stop (60 C, guessed) is near 60 C.
  - Needs: a fridge, a freezer bag, a thermometer if you have one.
  - How: page 1, `THERM` at room temperature, note the room temperature. Then the charger in a sealed bag, no cells, 1 hour in the
    fridge; power it in the bag (cable through the opening, so no condensation forms on the board) and read `THERM` within a minute.
  - Pass: about 2140 at 23 C and about 2950 at 5 C (the guess). More than about 100 counts off either: the guess is wrong.
- [ ] **The input voltage reading.** Proves: the XT60 low-voltage cut, which protects a pack powering the charger, cuts where it says.
  - Needs: a USB-C PD charger that gives 20 V (laptop chargers do).
  - How: page 1, `VIN` while a charge runs at 0.5 A or more (the charger has negotiated by then).
  - Pass: 19.0 to 21.0 V. PD itself allows 5 %, so this catches gross errors only; a meter on the XT60 settles it.
- [ ] **The fan comes on.** How: a long 6-port charge at a high current, watching page 1.
  Pass: `FAN ON` once `THERM` is under 1300, off again above 1451.

## With a multimeter
- [ ] **A charged cell is at its setting (the important one).** Proves: the voltage the charger stops at is the real one.
  - How: charge a cell on 4.35 V, let it rest 5 minutes, measure at the cell's connector. Same on 4.20 V.
  - Pass: 4.32 to 4.36 V, and 4.17 to 4.21 V. Above that: stop using that setting and report it.
- [ ] **Each port reads right.** How: one resting cell on each port in turn; the meter on the cell and page 2's volts side by side.
  Pass: within 10 mV on every port.
- [ ] **The input voltage scale.** How: meter on the XT60 supply against `VIN` on page 1, at two voltages (say 8 V and 16 V).
  Pass: within 2 %.
- [ ] **Current.** How: meter in series with one cell (10 A range, an adapter lead), at 0.5 A and 0.9 A, against page 3's mA.
  Pass: within 5 %. A USB-C power meter on the input gives a rough check without a lead.

## Needs a second unit, or someone else's
- [ ] **Same board.** Proves: the pin map and the inverted rail (more duty, lower rail; the emergency stop relies on it) are not
  specific to one unit. How: another owner runs a charge and sends photos of pages 1 to 4; ask OpenWS3's author which revisions he has seen.
- [ ] **The internal reference on other chips.** How: page 1 `BANDGAP` and the CHIP line from other units, to see the real spread.

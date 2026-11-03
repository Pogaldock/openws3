**Unofficial, experimental firmware for the VIFLY WhoopStor V3. Read the README's "Read this before you install" and SAFETY.md first.**
It replaces VIFLY's firmware and decides the voltage and current that go into your cells. It was written largely with an AI coding
assistant and has run on one charger. Going back to VIFLY's firmware may not be possible. Charge in a LiPo bag or on a non-flammable
surface, supervise every charge, never overnight. No warranty, no responsibility taken: you install it at your own risk.

### 0.2.0-beta.2: fixes from an independent audit
- **Settings are reset to their defaults when you install this** (0.3 A, 4.20 V): set yours again.
- **4.35 V is off until you allow it** (menu, CHARGING, ALLOW 4.35V). Allow it only for LiHV cells, after checking a rested full cell
  with a multimeter.
- **B2 no longer changes the voltage**: it changes what the cells show. The voltage is set in the menu only.
- The time limits (3 h charge, 30 min pre-charge, new 3 h bleed limit) now run on real time, not on how fast the screen draws.
- A port reading at the top of the measurable range now counts as over-voltage (it could not fire there at a low chip supply).
- The current sense offset is measured with the output off and subtracted, so charges end where they should.
- The sound code can no longer corrupt a cell-voltage reading from inside an interrupt; every build checks this.
- No finale, summary or storage offer while a port is in fault; the storage target is not kept after the storage run; the finale
  plays once, not at every top-up.
- The voltage can no longer be changed mid-run by a run starting during a menu edit.
- Installer: five confirmations before installing, a fixed size limit, a warning on a unit that differs from the tested one.
- The full list, what was not fixed and why, and every assumption with what breaks if it is wrong: SAFETY.md.

### Still open
The 4.35 V accuracy on the tested unit is not confirmed by a multimeter yet. No human expert has reviewed the code.

### Install and check
Download `openws3.html`, open it in Chrome or Edge, press Connect, follow the steps. `sha256sum -c SHA256SUMS` checks the download
is not damaged; `gh attestation verify openws3.html -R Pogaldock/ws3-unlocked --signer-workflow Pogaldock/ws3-unlocked/.github/workflows/rot-build.yml`
checks that GitHub built it from this repository (where it came from, not that it is correct).

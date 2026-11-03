**Unofficial firmware for the VIFLY WhoopStor V3. Read the README's "Read this before you install" and SAFETY.md first.**
It replaces VIFLY's firmware and decides the voltage and current that go into your cells. Supervise every charge, never leave it
unattended. Much of it was written with an AI coding assistant; everything that was done to check it is in SAFETY.md.

### New in 0.2
- One look, ROT: barbed-wire header, port numbers coloured by state (dark green empty, green with a cell, red on a fault), big eyes,
  an empty port as static with a sleeping eye, every eye looking at a port that gets a cell. OpenWS3's other themes were removed.
- Sounds: one E minor phrase with soft crackle; SOUND `HEARTBEAT` adds a soft beat while charging.
- B2 while charging: AMPS, MAH IN, ETA (an estimate), HEALTH (GOOD / TIRED / RETIRE from the measured resistance).
- A finale and a run summary when every cell is full; a storage reminder after 2 hours full.
- OpenWS3's developer mode is removed: a USB host can no longer drive an output.

### Safety changes over OpenWS3
Supply-independent voltage readings, refusal of over-discharged cells, a gentle pre-charge with its own time limit, a 3 hour charge
limit, a board over-temperature stop, a watchdog, a USB-C backfeed guard, and more: README "What WS3 Unlocked changed and added".

### Known open question
On the 4.35 V (LiHV) setting the accuracy on your unit is not confirmed: check a rested full cell with a multimeter (SAFETY.md).
Until you have, use 4.20 V.

### Install and check
Download `openws3.html`, open it in Chrome or Edge, press Connect, follow the steps. `sha256sum -c SHA256SUMS` checks the files;
`gh attestation verify openws3.html -R Pogaldock/ws3-unlocked` checks that GitHub built them from this repository.

**What:** the ADC measures against the chip's own supply (VCC). OpenWS3 corrects only the input voltage reading for it, so every port voltage, current and the rail reading scale with VCC, which differs from unit to unit and from supply to supply.

**How it shows:** in a whole-firmware simulation of the board (the real OpenWS3 firmware against a model of the board, cells and supplies), a cell is charged to **4.57 V on the 4.20 V setting** with VCC at 4.95 V.

**A fix:** read the internal reference (bandgap, ADC channel 15) every pass and correct every port, current and rail reading with it. In WS3 Unlocked, a fork of OpenWS3: [`src/adc.c`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/src/adc.c), [`src/cal.c`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/src/cal.c) (`cal_vcc_gain`, `cal_vcc_fix`). With it, cells finish at 4.200 to 4.203 V from VCC 4.45 to 4.95 V.

**Test:** simulation scenarios `vcc_low`, `vcc_mid`, `vcc_high`, `vcc_step` in [`test/sim/sim_app.c`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/test/sim/sim_app.c).

Found while building WS3 Unlocked on top of OpenWS3 (thank you for it). Offered for you to take in whatever way suits the project.

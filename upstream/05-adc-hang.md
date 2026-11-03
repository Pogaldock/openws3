**What:** `hal_adc()` waits for the conversion with `while (!(ADC_CONTR & 0x20));`. A converter that never finishes (a hardware fault) hangs the firmware, and the watchdog then resets the chip again and again.

**A fix:** a bounded wait that returns full scale, which the supervision already treats as an over-voltage, so the outputs go off ([WS3 Unlocked `src/hal.c`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/src/hal.c), `hal_adc`).

**Test:** [`test/s51/t_hal.c`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/test/s51/t_hal.c): s51 has no ADC, so the conversion never finishes there, as on a failed converter.

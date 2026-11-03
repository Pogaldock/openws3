**What:** the rail actuator is inverted (higher duty means a lower rail), but `hal_all_off()` ends with `hal_rail(0)`, which drives the rail to its **highest**. So a fatal stop leaves the rail high.

**How it shows:** simulated, the rail sits at about 4.85 V after a fatal stop instead of about 3.25 V. The ports are off at the same time, so it is a second line of defence that points the wrong way rather than an immediate overcharge.

**A fix:** `hal_rail(PWM_PERIOD)` in `hal_all_off()` ([WS3 Unlocked `src/hal.c`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/src/hal.c)).

**Test:** [`test/s51/t_hal.c`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/test/s51/t_hal.c) runs the real `hal.c`, built by SDCC, on the s51 simulator and reads back what `hal_all_off()` wrote into the rail and port registers.

**What:** `hal_ms()` returns `hal_ticks() >> 1`, so it has 31 bits. When the 32-bit tick counter wraps (after 24.9 days of continuous power) the millisecond clock jumps back by 2^31 ms, and every "is it time yet" comparison misfires once (a pending settings save, the timed waits).

**A fix:** carry the top bit across the wrap ([WS3 Unlocked `src/hal.c`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/src/hal.c), `hal_ms`).

**Test:** [`test/s51/t_hal.c`](https://github.com/Pogaldock/ws3-unlocked/blob/a447816/test/s51/t_hal.c) sets the tick counter just before the wrap and checks the clock counts on.

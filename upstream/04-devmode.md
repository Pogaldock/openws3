**What:** two holes in developer mode's guards, both reproduced in simulation:
- the 50 mV over-voltage cut is checked only on ports that have been switched on, and switching a port off releases it, so a host that pulses a port on and off can push a cell to **4.45 V**;
- the bleed switch has no time limit: a host holding it on drained a cell to 0 V in an hour.

**A fix:** check the cut on every port whatever its history, and give the bleed switch a time limit. (WS3 Unlocked fixed both, then removed developer mode altogether; for OpenWS3 the two fixes alone are the smaller change.)

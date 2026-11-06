#!/bin/sh
# HOLD: talk to OpenWS3's author first (Discord) before posting any of these.
# Opens the issues in this folder on OpenWS3 (github.com/openws3/openws3), one per fix, from your GitHub account.
# Needs the GitHub CLI, logged in (gh auth login).  Run from the repository root:  sh upstream/post.sh
set -e
R=openws3/openws3
cd "$(dirname "$0")"
post() { gh issue create -R "$R" --title "$1" --body-file "$2"; }
post "Fatal stop drives the rail to its highest (hal_all_off sets duty 0 on an inverted actuator)" 02-fatal-rail.md
post "Rail-fault alarm never sounds when built with SDCC 4.6 (buzz_busy pointer test)" 03-alarm.md
post "Developer mode: pulsing a port defeats the 50 mV cut (4.45 V simulated); bleed switch has no time limit" 04-devmode.md
post "hal_adc waits forever on a conversion that never finishes" 05-adc-hang.md
post "A USB command can overwrite one not yet run, so a stop can be lost" 06-usb-slot.md
post "hal_ms jumps back by 2^31 ms after 24.9 days of continuous power" 07-clock.md

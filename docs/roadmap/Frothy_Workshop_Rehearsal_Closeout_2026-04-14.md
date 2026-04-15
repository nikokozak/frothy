# Frothy Workshop Rehearsal Closeout

Status: branch-local status note
Date: 2026-04-14

This note is the workshop rehearsal closeout surface for the current branch.
It does not claim that a successful measured real-device rehearsal has already
been captured here.

## Required Proof Command

```sh
sh tools/frothy/proof_m10_smoke.sh \
  --assume-blink-confirmed \
  --transcript-out /tmp/frothy-m10.txt \
  <PORT>
```

## Current Branch Status

- The docs/front-door, clean-machine checklist, and room-side recovery card
  are checked in on this branch.
- The measured rehearsal closeout stays open until the maintained real-device
  path finishes cleanly and this file records the successful run.

## Measured Notes

- No successful measured real-device rehearsal is recorded in this file yet.
- A 2026-04-14 attempt reached the ESP32 proof path but did not finish cleanly
  after `dangerous.wipe`; the flash/monitor session lost the device on
  `/dev/cu.usbserial-0001`, so no timings are claimed here.

## Remaining Manual Gate

- rerun the real-device lesson path to completion on the maintained
  `esp32-devkit-v1`
- record the successful transcript and any measurement output from that run
- keep the remaining note limited to exact observed behavior

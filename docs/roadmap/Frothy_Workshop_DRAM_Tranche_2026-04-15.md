# Frothy Workshop DRAM Tranche 2026-04-15

Status: implemented first downsize tranche  
Date: 2026-04-15  
Scope: `esp32-devkit-v4-game-board` workshop image and adjacent runtime sizing work

## Authority And Scope

Authority order used for this tranche:

1. `docs/spec/Frothy_Language_Spec_v0_1.md`
2. Frothy ADRs, especially:
   - `docs/adr/104-cells-store-profile.md`
   - `docs/adr/106-snapshot-format-and-overlay-walk-rules.md`
   - `docs/adr/117-record-value-representation-and-persistence.md`
   - `docs/adr/118-explicit-evaluator-frame-stack-for-canonical-ir-execution.md`
   - `docs/adr/121-workshop-base-image-board-library-surface.md`
3. the current-state block in
   `docs/roadmap/Frothy_Development_Roadmap_v0_1.md`
4. `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`
5. current implementation

No accepted spec/ADR conflict blocked this work.

One user-supplied baseline assumption did not match the current tree:

- `frothy_board_base_lib` was already flash-mapped rodata, not DRAM.
  On the baseline ESP-IDF image it linked at `0x3f407fec`, so moving it out of
  DRAM was not a live win in this checkout.

## Baseline DRAM Budget

Maintained build path:

- `frothy build --target esp-idf --board esp32-devkit-v4-game-board`

Baseline image, before this tranche:

- `dram0_0_seg`: `180,736` bytes total
- `.dram0.data`: `104,664`
- `.dram0.bss`: `58,672`
- static DRAM used: `163,336`
- static DRAM slack: `17,400`

Largest verified baseline DRAM symbols:

- `froth_vm`: `96,088`
- `heap_memory`: `16,384`
- `frothy_snapshot_codec_workspace`: `10,448`
- `frothy_eval_frame_stack`: `10,240`
- `slot_table`: `4,096`
- `frothy_base_registry`: `1,024`

Baseline `frothy_runtime_t` shape facts:

- `payload_storage[65536]` reserved `65,536` bytes inline
- `object_storage[256]` reserved `22,528` bytes inline
- `free_span_storage[256]` reserved `2,048` bytes inline
- `payload_free_span_storage[256]` reserved `2,048` bytes inline
- `eval_value_storage[512]` reserved `2,048` bytes inline

## High-Water Measurements

Host workshop proof:

- `build-workshop-memory-post/frothy_workshop_memory_tests`
- the workshop-memory proof stays intentionally narrow: it measures the
  inspection/workshop behaviors through the underlying inspection surface rather
  than duplicating shell-command coverage in a second proof target

Follow-up workshop-board payload sweep on real hardware:

- the initial tranche shipped the v4 board at `65,536` payload bytes because
  that was the first measured downsize point.
- a same-day maintained device sweep then tested larger payload settings on
  `esp32-devkit-v4-game-board`.
- `81,920`, `90,112`, and `98,304` all passed the maintained `workshop-v4`
  proof on hardware.
- the earlier `81,920` prompt timeouts did not reproduce; they appear to have
  been transient post-flash prompt-acquisition noise rather than a
  payload-specific runtime failure.
- the raw boot banner alone is not diagnostic here: the v4 base image defines
  `boot` as `demo.pong.setup` plus `demo.pong.run`, so the shell is expected to
  sit inside the live Pong boot path until `frothy connect` interrupts it back
  to the prompt.
- the repo now sets the workshop board payload arena to `90,112`, not `65,536`,
  because it buys materially more attendee overlay headroom than `81,920` while
  still leaving a more defensible DRAM margin than `98,304`.

Board capacities used by the implemented tranche:

- heap: `8,192`
- data space: `256`
- slot table: `256`
- eval values: `512`
- eval frames: `64`
- objects: `256`
- payload arena during the measured host high-water sweep: `65,536`
- current checked-in workshop-board payload arena after the hardware follow-up:
  `90,112`

Observed scenario peaks:

- `base image install` is the cold installed baseline.
- Every later row starts from `frothy_snapshot_wipe()`, which erases snapshots
  and resets back to the base image, then resets the high-water counters to the
  current live usage.
- Read those later rows as incremental headroom above the installed base image,
  not as independent cold-start totals.

| Scenario | Heap high | Slot high | Object high | Payload high | Eval value high | Eval frame high |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| base image install | 2,209 | 165 | 114 | 60,184 | 0 | 2 |
| fresh boot | 2,209 | 165 | 114 | 60,184 | 9 | 15 |
| inspection/discovery | 2,209 | 165 | 114 | 60,184 | 0 | 0 |
| shipped Pong | 2,209 | 165 | 114 | 60,184 | 15 | 15 |
| attendee-modified Pong | 2,242 | 167 | 117 | 62,592 | 15 | 15 |
| save/restore peak | 2,242 | 167 | 117 | 62,592 | 0 | 3 |

Snapshot codec peak during save/restore:

- payload bytes: `947`
- symbols: `16`
- objects: `3`
- bindings: `7`

Important conclusion:

- the workshop board's payload arena is not fake reserve; the base image alone
  actively consumes about `60 KB` of the live payload arena.
- heap, eval-frame stack, and snapshot workspace duplication were the real
  first-tranche DRAM headroom wins.
- after the hardware follow-up, the shipped v4 board now reserves `90,112`
  payload bytes; the same measured modified-Pong peak (`62,592`) therefore
  leaves about `27,520` bytes of payload headroom on the workshop board.

## DRAM Classification

### Intentional Reserved Capacity That Directly Supports Workshop Scale

- `frothy_runtime.payload_storage`
  - reserved: `90,112` on the current workshop-board config
  - observed high-water: `62,592`
  - classification: intentional and actively used
  - reason: packed code/text payload for the base image and overlay lives here;
    cutting this aggressively would directly reduce live-image capacity.

- `slot_table`
  - reserved: `256` slots / `4,096` bytes
  - observed high-water: `167`
  - classification: controlled reserve
  - reason: top-level identity growth is part of the workshop model; this still
    has meaningful but not huge spare headroom.

- `frothy_runtime.object_storage` and paired free-span arrays
  - observed object high-water: `117 / 256`
  - classification: controlled reserve
  - reason: object ids are part of the live image; trimming this is possible,
    but it directly narrows overlay/persistence headroom.

### Acceptable Fixed Runtime Infrastructure

- `frothy_eval_frame_stack`
  - now `64` frames / `5,120` bytes
  - observed high-water: `15`
  - classification: acceptable fixed runtime infrastructure after tuning

- `heap_memory`
  - now `8,192`
  - observed high-water: `2,242`
  - classification: acceptable reserve after tuning

- `cellspace_memory` + `cellspace_base_seed_memory`
  - `2,048` total at the current v4 setting
  - observed high-water in workshop scenarios: `0`
  - classification: currently acceptable but still a future tuning candidate
  - note: v4 remained at `256` cells because the generic object/cell churn host
    proof still assumes the existing cell/object profile.

### Internal Tax / Duplication / Wrong Lifetime

- baseline snapshot codec workspace duplicated separate encode and decode arrays
  permanently in BSS even though save and restore are disjoint phases
  - fixed in this tranche

- baseline base-image registry carried a permanent `const char *` array sized to
  the full slot-table capacity
  - fixed in this tranche

- baseline eval-frame stack and heap were sized well above observed workshop use
  on the v4 board
  - fixed in this tranche through board-tuned capacities

## Opportunity Ranking

| Candidate | Est. bytes | Impl risk | Behavior risk | UX preservation | Future tuning effect |
| --- | ---: | --- | --- | --- | --- |
| Shrink v4 heap to `8192` | `8,192` | low | low | high | high |
| Shrink v4 eval-frame stack to `64` | `5,120` | low | low | high | high |
| Overlay snapshot encode/decode workspaces | `4,080` | low-medium | low | very high | medium |
| Remove fixed base-name registry array | `1,024` | low | low | very high | low |
| Tune v4 data space below `256` | `1,024` max | low | medium | medium | medium |
| Lower slot capacity below `256` | `2,304` max | low | medium | medium | medium |
| Lower object capacity below `256` | `~8,000` max | medium | medium | medium | medium |
| Change base-image code representation in payload arena | `10 KB+` possible | high | medium-high | unknown | very high |

Why the base-image payload representation remains the large future item:

- `frothy_board_base_lib` source bytes are already in flash.
- the real DRAM cost is the runtime's packed code representation of that base
  library inside `payload_storage`, not the seed source bytes.
- changing that representation would be a broader runtime/persistence tranche,
  not a safe first downsize pass.

## Implemented Tranche

Implemented fixes:

1. `heap_size` on `esp32-devkit-v4-game-board` reduced from `16384` to `8192`
2. `frothy_eval_frame_capacity` exposed as a board knob and set to `64` on v4
3. `FROTH_DATA_SPACE_SIZE` exposed as a board knob for future board tuning
4. snapshot codec workspace restructured so encode-only and decode-only arrays
   share the same BSS lifetime instead of being duplicated permanently
5. base-image registry changed from a full pointer array to a captured base-slot
   count
6. instrumentation added for:
   - heap high-water
   - cellspace high-water
   - slot-table high-water
   - eval-frame high-water
   - snapshot codec usage peaks
7. dedicated workshop-memory proof added:
   - `tests/frothy_workshop_memory_test.c`

## Post-Change DRAM Budget

Maintained ESP-IDF build after the implemented tranche:

- `dram0_0_seg`: `180,736` bytes total
- `.dram0.data`: `104,680`
- `.dram0.bss`: `40,264`
- static DRAM used: `144,944`
- static DRAM slack: `35,792`

Net improvement versus baseline:

- static DRAM recovered: `18,392` bytes
- static DRAM slack improvement: from `17,400` to `35,792`

Largest post-change DRAM symbols:

- `froth_vm`: `96,096`
- `heap_memory`: `8,192`
- `frothy_snapshot_codec_workspace`: `6,368`
- `frothy_eval_frame_stack`: `5,120`
- `slot_table`: `4,096`
- `cellspace_memory`: `1,024`
- `cellspace_base_seed_memory`: `1,024`

Recovered bytes by implemented change:

- v4 heap downsize: `8,192`
- v4 eval-frame downsize: `5,120`
- snapshot workspace lifetime overlap: `4,080`
- base registry removal: `1,024`
- small metadata growth elsewhere: `-24`

## What Remains Intentionally Reserved

The remaining large static DRAM consumers are mostly defended reserve, not
careless tax:

- payload arena:
  the base image itself uses about `60 KB`, so this is now active workshop
  state, not speculative slack.

- slot table:
  `167 / 256` peak under the modified workshop flow leaves enough headroom for
  attendees to create new top-level names without making the board feel brittle.

- object capacity:
  `117 / 256` peak leaves live-image and restore headroom; trimming it further
  is possible, but it is a user-facing scale decision rather than a pure
  placement win.

- cellspace:
  exposed for board tuning, but left at the existing v4 value until a follow-on
  tranche proves the narrower cell/object coupling honestly.

## Validation

Focused host validation run:

- `build-workshop-memory-post/frothy_eval_tests`
- `build-workshop-memory-post/frothy_snapshot_tests`
- `build-workshop-memory-post/frothy_tm1629_board_tests`
- `build-workshop-memory-post/frothy_workshop_memory_tests`
- `sh tests/esp_idf_board_config_smoke.sh "$(command -v cmake)" /Users/niko/Developer/Frothy esp32-devkit-v4-game-board`
- `sh tests/esp_idf_board_config_smoke.sh "$(command -v cmake)" /Users/niko/Developer/Frothy esp32-devkit-v1`
- `frothy build --target esp-idf --board esp32-devkit-v4-game-board`
- `frothy build --target esp-idf --board esp32-devkit-v1`

Real-device proof status:

- not completed in this tranche run because `/dev/cu.usbserial-0001` was busy
  with another active Frothy/minicom session when sign-off validation was due.
- required follow-up once the line is free:
  - `sh tools/frothy/proof.sh workshop-v4 <PORT>`
  - if hardware/runtime changes beyond the workshop board are being signed off,
    also run a minimal `esp32-devkit-v1` real-device sanity proof per repo
    policy.

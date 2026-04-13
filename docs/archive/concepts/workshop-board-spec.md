# Froth Workshop Board Specification

**Date:** 2026-03-23
**Status:** Proposed concept for the April 17 workshop at NYC Resistor
**Audience:** Froth maintainers, board designer, procurement reviewer, workshop planner
**Related:** `PROGRESS.md`, `TIMELINE.md`, `docs/spec/Froth_Interactive_Development_v0_5.md`, `docs/archive/concepts/vscode-extension-design.md`, `docs/archive/adr/028-board-platform-architecture.md`, `docs/archive/adr/036-protocol-sideband-probes.md`

## Purpose

This document describes the board Froth should use for the April 17 workshop.

The board should not be a generic ESP32 breakout or a vague "creative coding shield." It should be a **small live visual computer**: something participants can teach, mutate, and save until it feels like their own machine.

The workshop should still end with ownership. But the emotional center shifts from "living artifact" toward:

**a programmable grid that visibly thinks in real time**

That is a strong Froth fit. Tiny word changes should produce obvious system-wide visual changes.

## Executive Summary

The recommended workshop board is:

- a **carrier board** around the currently supported ESP32 dev board
- centered on a **driver-backed 96-LED monochrome matrix**, ideally **12x8**
- bordered by **4 buttons** and **2 knobs**
- equipped with **1 ambient light sensor**
- optionally equipped with **1 accelerometer** if it can be added without schedule risk
- intentionally **simpler than the earlier RGB artifact-board concept**

The LEDs should be factory pick-and-place, not hand-assembled.

The board should be optimized for:

- visual immediacy
- legible computation
- games, automata, and sequencers
- live probes and signal visualization
- workshop durability
- driver-backed reliability rather than clever scanning tricks

The board should **not** try to be:

- an RGB sculpture board
- an audio instrument first
- a breadboard teaching platform
- a display-plus-menu gadget
- a risky custom ESP32 motherboard

## Core Recommendation

### Main shape

Build a **grid-computer carrier board** for the existing ESP32 development board already proven in the repo.

That is the correct call because it:

- reuses the known-good flashing, serial, daemon, and live workflow
- keeps the core compute story boring and reliable
- puts the design effort where the workshop value is: visible interaction
- avoids wasting April on USB, power-path, boot, and RF mistakes

### Main visual surface

The front face should be dominated by a **96-LED matrix**.

The recommended geometry is:

- **12 columns by 8 rows**

That format is good for:

- glyphs
- scrolling
- tiny games
- signal bars
- cellular automata
- particle systems
- strip-chart visualizers
- step sequencers

### Driver-backed, not clever

The matrix should be **driver-backed**.

Preferred approach:

- one matrix-driver IC that can comfortably cover a 12x8 matrix

Fallback approach:

- two driver-backed submatrices or a slightly smaller grid

Last-resort fallback:

- a raw muxed design only if it is already proven and known stable

Do not turn the workshop board into a refresh-rate, ghosting, and brightness-debugging project.

## Why This Fits Froth

Per the interactive spec, the device must remain the computer. Per the extension design, workshop usage defaults to live mode. Per ADR-028, the board layer should expose a small named vocabulary. Per ADR-036, live probes should make running behavior legible.

The grid board fits that well because Froth excels at:

- small compositional rules
- live redefinition
- quotations as behaviors
- persistence of a taught system
- visible experimentation without a recompile cycle

A grid turns these strengths into something immediate:

- redefine a rule, the whole field changes
- swap a mode quotation, the whole board behaves differently
- save it, reboot, and the board comes back as the participant's machine

## Design Goals

### Emotional goals

The board should feel:

- immediate
- strange in a good way
- computational
- personal
- safe to experiment with

### Practical workshop goals

The board must support:

- a first five-minute wow demo
- a one-hour guided tutorial
- multiple project styles without hardware changes
- room-scale repeatability
- fast board-level testing and replacement

### Project goals

A participant should be able to use the same board to build:

- a tiny game
- a grid-based art system
- a sequencer or rhythm machine
- a visual probe display
- a rules-based simulation
- a small puzzle or oracle

## Hardware Architecture

### Overall structure

Recommended stack:

1. Known-good ESP32 dev board as compute core
2. Custom workshop carrier board as the visible interaction surface
3. Optional front overlay or diffuser only if it does not threaten schedule

### Front-face layout

Recommended front-face features:

- large central **12x8 LED matrix**
- **2 buttons** on the left side of the matrix
- **2 buttons** on the right side of the matrix
- **2 knobs** at the lower corners
- **1 ambient light sensor** at the upper edge

Optional:

- one small accelerometer placed anywhere convenient on the PCB

### Mechanical spacing guidance

The board should be laid out for hurried workshop hands.

Practical guidance:

- buttons must be easy to hit without grazing the matrix
- knobs must be far enough apart to grab simultaneously
- USB cable access must not block controls
- sensor placement must not feel like a hidden afterthought
- the grid must remain visually dominant

### Back-face layout

Recommended back-face features:

- ESP32 dev board mounting area
- accessible USB connector
- clear access to reset and boot if needed
- minimal exposed wiring or add-on cabling

### Board size and orientation

The board should read as a **small landscape terminal**, not a decorative radial object.

Practical target:

- large enough that the grid is meaningful from a short distance
- small enough to remain a single hand-held or deskable object

## Core Components

### Component set

| Component | Count | Why it exists | Workshop value |
|-----------|-------|---------------|----------------|
| Monochrome LEDs | 96 | Main visual surface | Games, art, probes, sequencing, simulation |
| Matrix driver IC | 1 | Reliable refresh and brightness control | Keeps the board from becoming a mux-debug project |
| Buttons | 4 | Discrete input | Game controls, mode switching, stepping, menu-less interaction |
| Potentiometers | 2 | Continuous input | Tempo, density, threshold, speed, scale |
| Ambient light sensor | 1 | Environmental input | Reactivity, calibration, puzzle logic |
| Accelerometer | 0 or 1 | Optional motion input | Tilt games, particles, gravity, physical interaction |

### Why this exact mix

This set is strong because it supports:

- a rich visual output surface
- enough discrete controls for actual games
- enough analog control to make the board feel shapeable
- one cheap environmental input
- one optional high-wow sensor

It is deliberately more focused than the earlier artifact-board concept.

## Matrix-First Design

### Monochrome is the correct call

For April, the matrix should be **monochrome**, not RGB.

That is the right trade because it improves:

- power behavior
- software simplicity
- readability of patterns
- current budgeting
- sourcing flexibility
- classroom friendliness

It also keeps the board concept honest. The grid itself is the instrument. It does not need RGB to be compelling.

### Recommended LED appearance

Strong options:

- amber
- warm white
- green

Avoid defaulting to cold white unless there is a strong reason. The board should have a visual identity.

### Recommended geometry

Preferred:

- **12x8**

Acceptable fallback:

- **8x8** if the driver situation or layout risk makes 96 LEDs unrealistic for the workshop schedule

I would not spend April trying to force a more ambitious geometry than 12x8.

### Factory assembly

The LED matrix should be factory pick-and-place.

Do not make the LED field a hand-soldered workshop-prep burden. It is too central to the board identity to tolerate inconsistent assembly quality.

## Driver Strategy

### Preferred architecture

Use a dedicated LED matrix driver on I2C.

The current leading shape is an **IS31FL3737-class device** or equivalent:

- enough outputs for a 12x8 monochrome grid
- per-LED brightness control
- I2C control
- no need to bit-bang or hand-scan the matrix from Froth-visible code

### What the driver buys

The driver should own:

- multiplex timing
- LED current control
- per-pixel intensity storage
- ghosting and refresh discipline

That leaves the board API free to talk in terms of a grid rather than rows, columns, and scan phases.

### What not to do

Do not choose a driver path that:

- is only available from a single fragile source if avoidable
- forces an awkward matrix geometry
- introduces a lot of fragile bring-up work

Do not choose a raw mux scheme unless you already know it works on this exact board class.

## Controls and Sensors

### Buttons

Use **4 large buttons**.

Four is important. Two is too sparse for a board that wants to host:

- games
- mode switches
- stepping
- confirm/cancel style interactions

Guidance:

- same part family for all four
- comfortable finger feel
- direct silkscreen labels like `A`, `B`, `C`, `D`
- simple electrical semantics

### Knobs

Use **2 linear potentiometers**.

They are useful for:

- tempo
- simulation speed
- density
- threshold
- cursor movement rate
- step probability

Guidance:

- real finger controls, not trimmers
- mechanically anchored
- comfortable to grab

### Ambient light sensor

Include **1 simple analog ambient light sensor**.

It gives you:

- room-reactive art
- hidden-state puzzles
- auto-dimming if you want it
- one more probe-friendly live value

### Accelerometer

The accelerometer is optional, but it is the most compelling optional addition.

If it can be added without risk, it unlocks:

- tilt-controlled particles
- gravity games
- gesture-driven modes
- physical sequencers

If it adds schedule risk, cut it. The board is still strong without it.

## Mechanical and Industrial Direction

### Overall character

This board should look like:

- a tiny computer
- a small terminal
- a pocket simulation machine

It should not look like:

- a flower
- a decorative talisman
- a random sensor shield

### Control framing

The grid should visually dominate the board.

The buttons and knobs should feel like the border controls of a small game machine or lab instrument.

### Optional overlay

If time allows, a simple overlay is worth it.

Good overlay properties:

- reduces LED glare
- improves the visual coherence of the grid
- does not interfere with the buttons or knobs

Do not let enclosure work become the schedule sink.

## Electrical Guidance

### Power model

Use the ESP32 dev board's known-good power path.

The workshop carrier should not reinvent:

- USB power
- charging
- battery management
- boot circuitry

### LED current discipline

The software should never default to needlessly aggressive brightness.

This board will likely appear in quantity in one room. Good defaults matter.

### ADC guidance

Use ADC1 pins for:

- knob left
- knob right
- ambient light

Do not build the workshop board around ADC2 assumptions if Wi-Fi remains on the medium-priority roadmap.

### Recommended omissions

The base board should deliberately omit:

- audio as a required feature
- large displays
- motors
- battery systems
- any feature that makes the board meaningfully more fragile

This is a visual computing board first.

## Sourcing-Oriented Component Matrix

This section exists specifically to help procurement review.

| Function | Preferred implementation | Acceptable fallback | Notes |
|----------|--------------------------|---------------------|-------|
| Main display | 96 discrete monochrome LEDs in 12x8 matrix | 64 LEDs in 8x8 matrix | Keep the grid concept even if geometry shrinks |
| Matrix drive | single dedicated matrix driver | two dedicated drivers or smaller grid | Avoid raw mux if possible |
| Discrete input | 4 identical tactile buttons | none | Four should be treated as fixed |
| Continuous input | 2 identical linear pots | one pot only if forced | Two is materially better |
| Environment input | analog light sensor | none | Cheap and useful |
| Motion input | small I2C accelerometer | omit if risky | Optional, not sacred |

Recommended board priorities, in order:

1. Keep the grid.
2. Keep a dedicated driver.
3. Keep 4 buttons.
4. Keep 2 knobs.
5. Keep the light sensor.
6. Add the accelerometer only if it is low-risk.

## Concrete Candidate Pin Map

This is a practical first-pass mapping for an ESP32 DevKit V1-based carrier.

| Board function | Candidate GPIO | Reasoning |
|----------------|----------------|-----------|
| Matrix SDA | GPIO21 | standard I2C choice |
| Matrix SCL | GPIO22 | standard I2C choice |
| Matrix shutdown or enable | GPIO18 | optional control pin |
| Matrix interrupt | GPIO19 | optional status pin |
| Knob left | GPIO32 | ADC1-capable |
| Knob right | GPIO33 | ADC1-capable |
| Light sensor | GPIO34 | ADC1 input-only is fine |
| Button A | GPIO13 | ordinary input candidate |
| Button B | GPIO14 | ordinary input candidate |
| Button C | GPIO16 | ordinary input candidate |
| Button D | GPIO17 | ordinary input candidate |
| Optional accel interrupt | GPIO25 | spare digital candidate |
| Spare | GPIO26 | future use |
| Spare | GPIO27 | future use |

This is not sacred. It is simply a reasonable starting point that:

- leaves the serial path alone
- keeps analog on ADC1
- uses conventional I2C pins
- preserves a little headroom

## Software Surface the Board Needs

The workshop should never force participants to think about matrix scanning or driver registers.

The board should ship with a small, named vocabulary that matches the physical surface.

### Baseline naming policy

Good example names:

- `grid.clear`
- `grid.fill`
- `grid.set`
- `grid.toggle`
- `grid.show`
- `grid.width`
- `grid.height`
- `button.a?`
- `button.b?`
- `button.c?`
- `button.d?`
- `knob.left.raw`
- `knob.right.raw`
- `knob.left`
- `knob.right`
- `light.raw`
- `light`
- `tilt.x`
- `tilt.y`

The exact names can change, but the principle should not.

### Reference workshop API sketch

| Word | Stack effect | Purpose |
|------|--------------|---------|
| `grid.clear` | `( -- )` | clear framebuffer |
| `grid.fill` | `( v -- )` | fill framebuffer with one value |
| `grid.set` | `( x y v -- )` | set one cell |
| `grid.toggle` | `( x y -- )` | flip one cell |
| `grid.get` | `( x y -- v )` | read one cell |
| `grid.show` | `( -- )` | flush framebuffer to hardware |
| `grid.width` | `( -- n )` | width constant |
| `grid.height` | `( -- n )` | height constant |
| `button.a?` | `( -- flag )` | button state |
| `button.b?` | `( -- flag )` | button state |
| `button.c?` | `( -- flag )` | button state |
| `button.d?` | `( -- flag )` | button state |
| `knob.left.raw` | `( -- n )` | raw ADC |
| `knob.right.raw` | `( -- n )` | raw ADC |
| `knob.left` | `( -- n )` | normalized value |
| `knob.right` | `( -- n )` | normalized value |
| `light.raw` | `( -- n )` | raw ambient light |
| `light` | `( -- n )` | normalized ambient light |
| `tilt.x` | `( -- n )` | optional accelerometer axis |
| `tilt.y` | `( -- n )` | optional accelerometer axis |

This is a workshop API, not a final language-level commitment.

### Reference board metadata sketch

Per ADR-028, a plausible `board.json` shape would look like:

```json
{
  "name": "Froth Grid",
  "platform": "esp-idf",
  "chip": "esp32",
  "description": "Workshop carrier board built around a 12x8 LED matrix",
  "peripherals": ["gpio", "adc", "i2c"],
  "pins": {
    "MATRIX_SDA": 21,
    "MATRIX_SCL": 22,
    "MATRIX_SDB": 18,
    "MATRIX_INT": 19,
    "KNOB_LEFT": 32,
    "KNOB_RIGHT": 33,
    "LIGHT_SENSOR": 34,
    "BUTTON_A": 13,
    "BUTTON_B": 14,
    "BUTTON_C": 16,
    "BUTTON_D": 17
  }
}
```

If the accelerometer lands, it should usually share the I2C bus rather than forcing a second bus story.

## Why This Board Will Demo Well

This board is excellent for:

- Conway's Game of Life
- particle systems
- falling sand or gravity toy
- tiny maze or reflex games
- cellular automata with live rule changes
- step sequencers
- animated glyphs
- live probe graphs

The killer demo is not "lots of LEDs."

It is:

**change one word and the whole world on the grid changes**

That is very Froth.

## Theory and Tutorial Alignment

### Stack tutorial

Map stack values to:

- coordinates
- cell values
- speeds
- thresholds

That makes the stack feel concrete fast.

### Definition tutorial

Teach small words like:

- `seed`
- `step`
- `draw`
- `wrap`

Then redefine one live.

### Quotation tutorial

Quotations become:

- update rules
- game modes
- drawing modes
- particle behaviors

### `while` tutorial

A running simulation is the cleanest `while` demo you could ask for.

### `catch` tutorial

Break the rule function, recover immediately, keep the board alive.

### Probe tutorial

This board can become its own probe display.

A watched value can be shown as:

- a bar graph
- a strip chart
- a histogram

That aligns cleanly with ADR-036's probe direction.

## Workshop Project Tracks

### 1. Tiny worlds

Examples:

- Life-like automata
- diffusion
- growth systems
- walkers

### 2. Games and puzzles

Examples:

- reaction game
- memory game
- maze
- dodge game
- lock puzzle

### 3. Sequencers and visual instruments

Examples:

- step sequencer
- Euclidean-ish rhythm visualizer
- tempo and density sketchpad

### 4. Probe and signal tools

Examples:

- analog meter
- multi-column strip chart
- threshold finder
- sensor visualizer

## Workshop Software Requirements

### Must-have

- named board words
- clear live send workflow
- save and restore path that feels safe
- one or two polished matrix demos
- one polished game or sequencer demo
- one board self-test routine

### Strongly recommended

- lightweight help surface for board components
- probe presets for knobs and light sensor
- one-click save action in tooling
- a small starter library

### Nice-to-have

- accelerometer support
- on-device strip-chart helpers
- `components` or `help board` style discovery

### Not required for April

- a full oscilloscope
- RGB support
- audio-first features
- a general-purpose debug UI

## Procurement Guidance

### Procurement strategy

Optimize for:

- common monochrome LEDs with comfortable assembly characteristics
- a driver IC that is actually buyable now, not merely theoretically correct
- mechanically robust buttons and pots
- footprint choices with substitutes

### Parts-review priorities

The stock reviewer should focus first on:

1. matrix driver availability
2. LED package and sourcing flexibility
3. button and pot mechanical sanity
4. accelerometer only after the base board is secure

### Good substitution policy

For each component class, accept families rather than one precious exact SKU where possible.

Especially:

- matrix LEDs
- buttons
- pots
- ambient light sensor

### Avoid

- fancy RGB paths that change the whole electrical budget
- fragile add-on displays
- exotic sensors that complicate the workshop story
- any board feature that depends on heroic bring-up

## Manufacturing and Bring-Up

### Board bring-up checklist

Every assembled board should pass:

1. power-on with ESP32 attached
2. serial connection and Froth `info`
3. matrix self-test
4. button state readback
5. knob sweep on both channels
6. light sensor response
7. accelerometer readout if fitted

### Suggested board self-test words

Useful words:

- `board.test.matrix`
- `board.test.buttons`
- `board.test.knobs`
- `board.test.light`
- `board.test.accel`
- `board.test.all`

These are useful for manufacturing and for pre-workshop triage.

### Workshop fleet checks

Before the event:

- flash all boards with the same known-good image
- run the same smoke script across all units
- verify save/restore on a sample set
- verify the opening demo works identically everywhere

## Open Decisions

### 1. Exact matrix driver

Choose:

- preferred driver IC
- acceptable backup driver
- geometry fallback if the best driver path goes sour

### 2. LED color

Choose:

- amber
- warm white
- green

This matters more than it sounds. It defines the board's visual identity.

### 3. Accelerometer or not

Decide whether the accelerometer is in-scope for April or explicitly deferred.

### 4. Exact board API names

Freeze:

- grid words
- button words
- normalized analog words
- optional tilt words

If this API hardens beyond workshop scaffolding, it likely deserves an ADR.

## Recommended Next Steps

1. Commit to the **grid-computer** direction.
2. Freeze the **base component set**: 12x8 monochrome matrix, dedicated driver, 4 buttons, 2 knobs, light sensor.
3. Treat the accelerometer as optional until the base board is safe.
4. Do a parts review centered on driver availability first.
5. Draft the board library and demos around `grid.*` words immediately.
6. Build the first workshop demos around simulations, games, and probe displays rather than RGB animations.

## Bottom Line

The right workshop board is not the fanciest board.

It is the board that makes Froth feel like a live programmable world.

For April, that points to:

- a driver-backed monochrome grid
- enough controls to make it inhabitable
- a small named API
- a board simple enough to trust

The compute core should stay boring.

The grid should be unforgettable.

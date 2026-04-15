# Frothy Workshop Run Spec 2026-04-17

Status: draft workshop-content and rehearsal spec
Date: 2026-04-14
Authority: `docs/spec/Frothy_Language_Spec_v0_1.md`, `docs/adr/107-interactive-profile-boot-and-interrupt.md`, `docs/adr/108-frothy-ffi-boundary.md`, `docs/adr/121-workshop-base-image-board-library-surface.md`, `docs/roadmap/Frothy_Post_v0_1_Priorities_And_Workshop_Prep.md`, `docs/guide/Frothy_Workshop_Install_Quickstart.md`

## Purpose

This note freezes a viable two-hour workshop run for Friday 2026-04-17.

It is a maintainer-facing content spec, not a language-semantics document.
It defines:

- the teaching arc
- the inspection puzzle
- the primary shared workshop game
- the minimum workshop helper surface
- the rehearsal and freeze criteria

It does not widen the accepted Frothy `v0.1` language contract.
Where the workshop uses helper words beyond the accepted `v0.1` core, those
helpers must remain small, explicit, and truthful to the maintained Frothy
surface.

## Why This Shape

The workshop must use Frothy's strongest current properties:

- live top-level redefinition
- inspection through `words`, `show @name`, `core @name`, and `info @name`
- safe interruption and recovery
- explicit `save`, `restore`, and `dangerous.wipe`

The workshop should not depend on attendees first inventing architecture from a
blank editor.

The content shape is therefore:

1. code along
2. inspect and repair
3. modify a shared exported game file
4. extend it into a personal variant

This keeps the first hour structured and the second hour open enough to feel
like authorship rather than a lab exercise.

## Run Constraints

The workshop run must assume:

- attendees arrive on the maintained install path from
  `docs/guide/Frothy_Workshop_Install_Quickstart.md`
- boards are preflashed
- the first supported board remains the current workshop ESP32 path
- the maintained workshop path is editor-first, but the REPL must remain usable
- the workshop can succeed even if draft language work such as records,
  `cond`, `case`, `in prefix`, and ordinary-code `@name` is not yet part of
  the stable teaching surface

The workshop run must not depend on:

- blank-sheet project invention in the first hour
- undocumented pin maps
- inherited Froth library carry-over
- students understanding records, modules, or draft recovery forms
- any feature that still requires explanation as a future-language idea

## Teaching Goals

By the end of the session, an attendee should be able to:

- connect to a board and send Frothy code
- interrupt a running loop and recover to a prompt
- inspect a named slot with `show @name`, `core @name`, and `info @name`
- redefine a top-level word to change live behavior
- use a tiny board helper layer for display and input
- save a working state and understand what `restore` and `dangerous.wipe` do
- modify a working game rather than only replay a scripted demo

## Student Model

The workshop is designed for pairs.

Recommended pair roles:

- driver: types and runs code
- navigator: reads the task card, inspects names, and watches behavior

Switch roles at least:

- once after the initial code-along
- once after the inspection puzzle

Pairs are the default support unit.
Solo work is allowed, but the facilitator should teach to pairs.

## Two-Hour Agenda

### 0. Room setup and first success: 0-10 min

Goals:

- confirm editor/board connection
- establish interrupt and recovery confidence
- show one immediate visible win

Flow:

1. connect board
2. send one tiny expression
3. run `led.blink: 2, 120` or the matrix hello check if the matrix helper path
   is already frozen
4. demonstrate `Ctrl-C`
5. explain that `dangerous.wipe` is the factory reset for the overlay image

Success rule:

- every attendee sees one visible board response before syntax teaching starts

### 1. Tiny language and image code-along: 10-28 min

Teach only the minimum needed for the workshop:

- top-level definitions
- calling functions
- redefining functions
- inspecting names
- saving and restoring

Required demonstration sequence:

1. define a tiny visible behavior
2. redefine it live
3. inspect it with `show @name`
4. mention `core @name` and `info @name`
5. `save`
6. modify it again
7. `restore`

Important truth to preserve:

- use the real runtime spelling `dangerous.wipe`
- do not teach `wipe()` as if it already exists unless an alias is added

### 2. Inspection puzzle: `Get Home`: 28-48 min

This is the mandatory bridge from syntax to authorship.

The puzzle must require:

- inspection
- code reading
- one live redefinition

The puzzle must not require:

- writing a renderer from scratch
- understanding the transport layer
- understanding records or draft language features

### 3. Puzzle debrief and input discovery: 48-58 min

Debrief:

- what names were important
- what changed after redefinition
- how `save`, `restore`, and `dangerous.wipe` interact

Then show the board input surface:

- button or joystick helper words
- knob or potentiometer helper words
- the discovery helper path for unknown pins or ranges

### 4. Shared workshop game: `Pong`: 58-100 min

All pairs start from one common exported game file.

The file should already:

- draw the paddles and ball
- read the knob control surface
- handle reset and replay
- expose a small visible parameter surface

Attendees spend this phase modifying and extending the same `pong.frothy`.

### 5. Personal extension sprint: 100-115 min

Each pair chooses one extension from the ladder or invents one of similar size.

They should leave the workshop with a board behavior that is recognizably
their own.

### 6. Share-out and close: 115-120 min

Each pair shows:

- one thing they changed
- one thing they learned about inspection or recovery

Close by reminding them to `save` if they want the current overlay retained.

## Inspection Puzzle Spec: `Get Home`

### Story

A small lit pixel is trying to get inside a house on the LED grid.
The house is already drawn.
The character keeps moving, but it never reaches the interior.

The attendees must inspect the moving logic, redefine it, and make the
character get home.

### Time Budget

- target: 12 minutes
- acceptable spread: 10-18 minutes

If most pairs are still blocked at 10 minutes, the facilitator should give the
next hint.

### Required Student Actions

Each pair must do all of the following:

1. interrupt the running loop if needed
2. run `words`
3. inspect at least one named word with `show @name`
4. inspect at least one named word with `info @name`
5. redefine one word
6. run the scene again and observe the changed behavior

Inspection alone is not a valid completion.
The puzzle only counts as solved once a pair has changed code.

### Preloaded Names

The preloaded puzzle image should expose only a small number of names.
Recommended names:

- `house.draw`
- `house.left`
- `house.top`
- `house.right`
- `house.bottom`
- `door.x`
- `door.y`
- `hero.x`
- `hero.y`
- `hero.draw`
- `hero.step`
- `goal.reached?`
- `scene.reset`
- `scene.tick`
- `scene.run`
- `scene.win`

The important inspection target is `hero.step`.

### Behavior Contract

Before the fix:

- `scene.run` repeatedly calls `scene.tick`
- `scene.tick` draws the scene, advances the hero, and checks the goal
- the house is visible at all times
- the hero visibly moves
- the current `hero.step` is wrong but legible

Recommended broken behavior:

- the hero moves right until aligned with the door column
- then it moves up instead of into the doorway
- it can never satisfy `goal.reached?`

Recommended fix shape:

- move horizontally until aligned with the door
- then move vertically down or up into the house interior

This gives the student a simple, local, believable change.

### Implementation Guidance

The puzzle state should use plain top-level values, not records.

Recommended state shape:

- `hero.x`
- `hero.y`
- `door.x`
- `door.y`

Keep the logic obvious.
Do not hide the bug behind math noise or rendering complexity.

The draw path should be intentionally boring.
The puzzle is about inspecting and changing behavior, not reverse-engineering
graphics.

### Success Signal

When the hero reaches the interior:

- the house flashes three times, or
- the matrix fills briefly, or
- a single key glyph appears

One short text message at the prompt is acceptable, but visible board feedback
must be the primary success cue.

### Hint Ladder

Hint 1:

- "Find the word that sounds like movement, not drawing."

Hint 2:

- "Use `show @hero.step`."

Hint 3:

- "You do not need to change the house or the renderer. Only the move logic."

### Recovery Rule

If a pair breaks the puzzle:

- `restore` should bring back their last saved state if they saved it
- `dangerous.wipe` should return the session to base-only state
- the facilitator must also be able to resend the puzzle file quickly

## Shared Workshop Game Spec: `Pong`

### Why This Is The Primary Shared Game

`Pong` should be the main workshop game because it is already running on the
shipped preflashed board and can be exported directly from the same canonical
base-image source.

That continuity matters:

- the first board state is already visible on plug-in
- the editable workshop file matches the shipped demo board
- the key names already make sense
- the second half of the workshop feels like extension, not a second setup path

### Core Loop

The shared workshop game is a tiny Pong loop:

- each player controls one paddle
- the ball bounces across the grid
- a miss resets the rally
- joystick click exits the loop cleanly

Required core behaviors:

- paddle read/update
- ball update
- screen redraw
- collision/reset
- replay

Optional base behaviors:

- faster/slower frame timing
- different paddle sizes
- different reset behavior

### Required Shared Names

Recommended names:

- `demo.pong.setup`
- `demo.pong.readPaddles`
- `demo.pong.advanceBall`
- `demo.pong.tickBall`
- `demo.pong.draw`
- `demo.pong.frame`
- `demo.pong.run`
- `boot`

The shared file should preserve the simple naming style used in the puzzle.

### Required Student Modification Surface

Every pair must be able to complete the workshop by changing only a few small
top-level words.

The intended modification surface is:

- one speed or timing slot
- one draw word
- one reset or bounce behavior
- one control mapping or paddle-size slot

### Extension Ladder

Offer the ladder as a menu, not a command.

Level 1: safe extensions

- change ball speed
- change paddle size
- invert the controls
- add a trail
- change the reset flash

Level 2: game-feel extensions

- use the joystick for one paddle
- add a second ball
- add a countdown
- make the ball accelerate
- add a restart gesture

Level 3: authorship extensions

- add score or rally count
- add AI on one paddle
- add ball spin or angle changes
- add attract mode or demo variations
- save a preferred difficulty or behavior in the overlay

No pair should be forced past Level 1 to feel successful.

## Minimum Workshop Helper Surface

The workshop content needs a small named board-facing helper layer.
This section is a workshop requirement, not an accepted new language contract.

The exact vehicle is:

- preflashed base-image helper code for the board surface and shipped Pong demo
- one tiny workshop repo containing `README.md` and exported `pong.frothy`

The attendee-facing names should still be frozen before rehearsal.

### Display Helpers

Required:

- `grid.width`
- `grid.height`
- `grid.clear`
- `grid.set: x, y, v`
- `grid.toggle: x, y`
- `grid.show`

Strongly preferred:

- `grid.rect: x, y, w, h, v`
- `grid.fill: v`

The workshop should never force attendees to think about matrix driver
registers, raw I2C traffic, or scanning details.

### Input Helpers

Required if the board uses a joystick:

- `joy.up?`
- `joy.down?`
- `joy.left?`
- `joy.right?`
- `joy.click?`

Required if the board uses discrete buttons instead:

- stable named helpers such as `button.a?()` through `button.d?()`

Required if the board exposes analog controls:

- `knob.left`
- `knob.right`

or equivalent named helpers built on `adc.percent`.

### Mapping Helpers

Students must be able to remap controls without editing C.

Recommended approach:

- store the pin numbers in top-level slots
- define the readable helpers in Frothy

Example shape:

- `joy.up.pin`
- `to joy.up? [ (gpio.read: joy.up.pin) == 0 ]`

This keeps remapping on the maintained Frothy surface and uses stable
top-level rebinding directly.

### Discovery Helpers

This tranche does not add new `learn.*` helpers.

The workshop relies on:

- the documented board map
- the frozen semantic `joy.*` and `knob.*` helpers
- one copy/paste prompt-check path from the quick reference

## Persistence Story To Teach

The workshop must teach the real persistence story, not a softer paraphrase.

Required truths:

- `save` snapshots the current overlay image
- `restore` replaces the current live overlay with the saved overlay
- `dangerous.wipe` clears both the live overlay and the saved snapshot
- startup restores a snapshot if one exists
- after restore, startup runs `boot` only if `boot` is currently bound to
  `Code`

Do not teach:

- `dangerous.wipe` as if it only clears RAM
- `restore` as if it works after `dangerous.wipe`
- `boot` as if restore only happens when `boot` is defined

## Facilitator Script Notes

### Language Introduction

Keep the first syntax pass short.

Teach:

- top-level definitions
- calling named words
- live redefinition
- inspection commands
- `save`, `restore`, `dangerous.wipe`

Defer:

- draft language features
- formal parser detail
- internal IR discussion beyond one quick `core @name` glimpse

### Puzzle Framing

Use playful framing, but keep the task concrete:

- "This character cannot get into the house."
- "Your job is to find the movement word and fix it."

Avoid framing the puzzle as pure scavenger hunt.
The important action is changing code.

### Support Rules

When helping a stuck pair:

1. ask what names they have inspected
2. point them at one smaller word
3. only then suggest a concrete code change

The facilitator should not type the answer for them unless the room is behind
schedule.

## Artifacts To Prepare

Required artifacts:

- attendee install note
- one one-page cheat sheet
- one puzzle file: `Get Home`
- one workshop repo containing `README.md` and `pong.frothy`
- one facilitator note with hint ladder and pacing cues
- one board smoke check routine

Recommended cheat sheet content:

- send and interrupt
- `words`
- `show @name`
- `core @name`
- `info @name`
- `save`
- `restore`
- `dangerous.wipe:`
- the frozen board helper names

## Freeze Criteria Before Rehearsal

The workshop run is not frozen until all of the following are true:

- the install path is truthful and repeatable
- the board connect/send/interrupt/reconnect path is boring
- the inspection puzzle can be solved in under 20 minutes by a fresh pair
- the starter game can be modified without touching C or hidden board details
- the board helper names are frozen for the workshop
- the persistence explanation uses the real `save` / `restore` /
  `dangerous.wipe` semantics
- the facilitator can recover any broken board to a known state quickly

## Rehearsal Checklist

Run at least one full rehearsal with another person playing the student role.

Check:

- can a fresh attendee complete first connect?
- do they understand that `dangerous.wipe` also erases the saved snapshot?
- do they find `hero.step` without being told immediately?
- do they actually redefine code during the puzzle?
- do they make at least one meaningful modification to `pong.frothy`?
- is the second half still energetic, or does it collapse into debugging?

If the rehearsal exposes friction, cut breadth before adding features.

Cut in this order:

1. extra Pong features beyond speed/draw/reset edits
2. scoring or AI features
3. analog-control extensions beyond the shipped knobs
4. richer discovery helpers beyond the minimum path

Do not cut:

- inspection
- live redefinition
- recovery teaching
- one visible personal extension

## Closed Decisions

- the workshop board helper names are the frozen `grid.*`, `joy.*`, and
  `knob.*` surface on `esp32-devkit-v4-game-board`
- this tranche does not add `learn.digital` or `learn.analog`
- the matrix path is the maintained workshop route
- the shipped board boots the Pong demo from the base image, and the workshop
  source is a tiny repo containing exported `pong.frothy`
- the workshop gate date should match the actual Friday 2026-04-17 run

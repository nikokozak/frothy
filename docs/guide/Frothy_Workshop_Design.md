# Frothy Workshop Design: "The Broken Beacon"

**Date**: 2026-04-16
**Workshop date**: 2026-04-17 (Friday)
**Venue**: NYU ITP / NYC Resistor (mixed audience)
**Participants**: ~10 (individual, not paired)
**Duration**: 2 hours
**Hardware**: Preflashed `esp32-devkit-v4-game-board` proto boards (12x8 LED matrix, joystick, two knobs)

---

This is a facilitation design note.
It does not override Frothy ADR-122's public release surface: the maintained
public workshop repo is the tiny exported repo with `README.md` and
`starter.frothy`.

## Workshop Thesis

The workshop teaches Frothy's live image model by making participants *need* it. The first half is a structured discovery puzzle where introspection and live redefinition are the solving tools, not taught-then-practiced skills. The second half is a bounded creative sprint where participants fill in a game loop scaffold, choose from three mission tiers, and show their work at the end. The board should feel like a living thing you talk to, not a chip you program.

---

## Table of Contents

1. [Research Summary](#1-research-summary)
2. [Workshop Arcs Considered](#2-workshop-arcs-considered)
3. [Recommendation](#3-recommendation)
4. [Minute-by-Minute Agenda](#4-minute-by-minute-agenda-2-hours)
5. [Shortened Agenda](#5-shortened-agenda)
6. [Pre-Workshop Install Email](#6-pre-workshop-install-email)
7. [Facilitator Script — First 10 Minutes](#7-facilitator-script--first-10-minutes)
8. [Frothy Teaching Progression](#8-frothy-teaching-progression)
9. [Puzzle Design: "The Broken Beacon"](#9-puzzle-design-the-broken-beacon)
10. [Creative Activity: "Your Board, Your Rules"](#10-creative-activity-your-board-your-rules)
11. [Solo Checkpoints](#11-solo-checkpoints)
12. [Fallback Plans](#12-fallback-plans)
13. [Attendee Cheat Sheet](#13-attendee-cheat-sheet)
14. [Facilitator Recovery Card](#14-facilitator-recovery-card)
15. [Workshop Artifacts Checklist](#15-workshop-artifacts-checklist)
16. [Rehearsal Checklist](#16-rehearsal-checklist)

---

## 1. Research Summary

Eleven design patterns emerged from researching live coding (Sonic Pi, TidalCycles, Strudel, Hydra), creative coding (Processing, p5.js), embedded education (micro:bit, CircuitPython, Arduino), escape room pedagogy, game jam formats, maker/hardware workshops, and REPL-driven development (Smalltalk, Lisp, Clojure).

The six most load-bearing patterns for this workshop:

| Pattern | Source tradition | How it applies |
|---|---|---|
| **Time to first output < 60 seconds** | Sonic Pi, p5.js, micro:bit | Board is already running Pong. First live change (brightness) happens before any explanation. |
| **Use-Modify-Create scaffolding** | PRIMM, UMC research | Puzzle is Use then Modify. Creative segment is Create with scaffold. |
| **Introspection as pedagogy** | Smalltalk, REPL-driven development | `words`/`show`/`info` are puzzle mechanics, not sidebar demos. |
| **Error normalization** | Sonic Pi ("no mistakes, only discoveries"), live coding literature | Stage 8 intentionally breaks the board; `restore` is the recovery tool. |
| **20-minute activity blocks** | Workshop design literature | No segment exceeds 20 minutes without a mode change. |
| **Public deadline for creative work** | Game jams, maker spaces | Show-and-tell at a fixed time gives shape to the creative sprint. |

Additional patterns informing specific decisions:

- **Tiered hints** (escape room research): nudge, then stronger hint, then answer. Self-service via the docs site.
- **PRIMM's Predict step**: "What do you think this does?" before running it builds the notional machine.
- **Game jam scope control**: "Do 1 thing very well." Each mission card has a bounded core and extension tiers.
- **Solo pacing** (game jam, escape room): clear success criteria so participants know they're done without asking.
- **Helper ratio**: 1 facilitator for 10 people is tight but workable because the puzzle has self-service hints and the board is recoverable.

---

## 2. Workshop Arcs Considered

### Arc A: "Archaeologist" — Puzzle-first, then creative capstone

Connect, first live change, learn inspection via discovery puzzle (introspection + live redefinition), then creative build from a game loop scaffold.

- Solo cognitive load: Low (puzzle provides clear goals, no blank page)
- Teaches live image model: Excellent (puzzle requires understanding overlay, redefinition, persistence)
- Uses introspection: Excellent (introspection IS the puzzle mechanic)
- Recoverability: High (`restore`/`dangerous.wipe` taught as puzzle tools)
- Fits 2 hours: Yes

### Arc B: "Pong Surgeon" — Dissect Pong, then transform it

Play Pong, inspect internals, guided mods, transform into a different game.

- Solo cognitive load: Medium-high (Pong has ~15 interacting functions; transformation is ambitious)
- Teaches live image model: Good but not as deep
- Uses introspection: Good but instrumental rather than central
- Recoverability: High
- Fits 2 hours: Tight (full game transformation is ambitious for mixed levels)

### Arc C: "Toy Shop" — Mini-challenges, then build a toy

Five bite-sized challenges (draw a shape, read a knob, animate a pixel), then creative build.

- Solo cognitive load: Low
- Teaches live image model: Weak (teaches API, not the image/overlay/persistence model)
- Uses introspection: Weak (no natural reason to inspect)
- Fits 2 hours: Yes, very flexible

### Arc D: "Escape the Loop" — All-puzzle, creative extensions optional

Multi-stage puzzle chain as the entire workshop. Optional creative extensions for fast finishers.

- Teaches live image model: Excellent
- Uses introspection: Excellent
- Fits 2 hours: Risky (pacing for 10 people at mixed speeds is hard)

---

## 3. Recommendation

**Arc A: "Archaeologist."**

1. The puzzle teaches introspection as a *tool*, not a lecture. ITP people learn by doing. NYC Resistor people enjoy detective work.
2. It naturally teaches the live image model. The puzzle requires understanding that the device has words you didn't write, that you can inspect them, that you can redefine them live, and that `save`/`restore`/`wipe` control persistence.
3. The creative capstone gives ITP people personal expression and NYC Resistor people systems depth.
4. Mixed speeds are handled by tiered hints (puzzle) and tiered missions (creative). Fast movers extend; slow movers get answers and move on.
5. Recovery is a feature, not a failure. `restore` and `dangerous.wipe` are taught as puzzle mechanics before they're needed as safety nets.

---

## 4. Minute-by-Minute Agenda (2 hours)

### Act I: First Contact (10 min)

| Time | Activity | Mode |
|---|---|---|
| 0:00-0:03 | Boards are running Pong. Plug in USB. Open VS Code. Connect. | Solo, facilitator narrates |
| 0:03-0:05 | **Stop Pong** (joystick click). Type `matrix.brightness!: 4`. Type `demo.pong.run:`. Pong restarts brighter. "You just changed a running program." | Solo — first live change |
| 0:05-0:08 | Stop Pong again. Type `grid.clear:`, `grid.set: 5, 3, true`, `grid.show:`. "You replaced Pong with a single pixel." | Solo — first pixel |
| 0:08-0:10 | Facilitator framing: "This board has a live image — named things it knows how to do. You can inspect any name, redefine any function, and you cannot permanently break it." | Facilitator talk, 2 min max |

### Act II: The Puzzle — "The Broken Beacon" (35 min)

| Time | Stage | What happens | Checkpoint |
|---|---|---|---|
| 0:10-0:14 | 1-2: Discovery | `words` — find `puzzle.*`. `show @puzzle.dot` — predict, then call `puzzle.dot:`. | "You see a pixel at (5,3)" |
| 0:14-0:18 | 3: Change a value | `set puzzle.x to 10`, call `puzzle.dot:` again. | "Your dot moved" |
| 0:18-0:25 | 4: Fix a function | Inspect `puzzle.frame` (one pixel instead of border). Redefine it. | "You see a border rectangle" |
| 0:25-0:38 | 5: Fix a chain | Inspect `puzzle.scene` — calls `puzzle.top` + `puzzle.bottom`, both broken. Fix all three. | "Two-part pattern appears" |
| 0:38-0:45 | 6: Wire to input | Redefine `puzzle.reveal` to use `knob.left:`. Call, twist, call again. | "Twisting the knob changes what you see" |

**Room-wide checkpoint at 0:38**: "If `puzzle.scene:` shows a two-part pattern, move to stage 6. If not, raise your hand."

Fast movers who finish stage 5 early get bonus prompts:
- "Make `puzzle.reveal` use *both* knobs"
- "Define a new `puzzle.sparkle` that sets random pixels"
- "Check frothy.frothlang.org — find a function you haven't used"

### Act III: Persistence & Recovery (10 min)

| Time | Stage | What happens |
|---|---|---|
| 0:45-0:48 | 7: Save | `save` — "Your fixes are now on the board's storage." Then `restore` — "Everything still works." |
| 0:48-0:52 | 8: Break and recover | Redefine with `to puzzle.scene [ 42 ]`. Call it — broken. `restore` — fixed. |
| 0:52-0:55 | Wipe demo | Facilitator demonstrates `dangerous.wipe` on their own board (projected). "This is factory reset. All your fixes are gone. But the base words are still here." |

### Break (10 min)

| Time | Activity |
|---|---|
| 0:55-1:05 | Break. Facilitator helps anyone who fell behind. |

### Act IV: Mission Briefing (5 min)

| Time | Activity |
|---|---|
| 1:05-1:10 | Present three mission cards (projected or on frothy.frothlang.org). "Open `starter.frothy` from the workshop repo. Send it to your board. Type `my.run:`. You should see a single dot. Click the joystick to stop. Pick a mission and make it yours. In 30 minutes, everyone shows their board." |

### Act V: Creative Sprint (30 min)

| Time | Activity | Mode |
|---|---|---|
| 1:10-1:25 | Build phase 1 | Solo. Facilitator circulates. |
| 1:25 | **Mid-sprint checkpoint**: "15 minutes left. If you're stuck, simplify or ask for help." | Facilitator announcement |
| 1:25-1:40 | Build phase 2 | Solo. Facilitator circulates. |

### Act VI: Show-and-Tell + Closure (20 min)

| Time | Activity |
|---|---|
| 1:40-1:55 | Show-and-tell. Each person: 60-90 seconds. "Show your board, tell us what it does, tell us what surprised you." |
| 1:55-2:00 | Closure. "You just did live embedded authorship. The docs are at frothy.frothlang.org. The language isn't public yet — you're the first outside users." |

---

## 5. Shortened Agenda

If setup eats 15+ minutes (install issues, cable problems, port confusion):

| Original | Shortened | What's cut |
|---|---|---|
| Puzzle: 35 min (stages 1-8) | Puzzle: 15 min (stages 1-4 + facilitator demos save/restore) | Multi-function fix (stage 5), input wiring (stage 6), hands-on break/recover |
| Creative: 30 min | Creative: 20 min | 10 minutes of build time |
| Show-and-tell: 15 min | Show-and-tell: 10 min (30 seconds each) | Depth per person |
| Total non-setup: 110 min | Total non-setup: 65 min | |

**What to preserve at all costs:**
- First live change (brightness) in under 5 minutes of being connected
- At least one redefinition (stage 4)
- At least one `save`/`restore` (even if facilitator-led)
- Show-and-tell at the end

**How to decide:** If by 0:20 fewer than half the room has connected, switch to the shortened agenda. Announce: "We're going to adjust the plan to give everyone more build time."

---

## 6. Pre-Workshop Install Email

```
Subject: Frothy workshop prep — please install before Friday

Hi! Looking forward to the workshop on Friday.

Please install two things before you arrive:

1. THE FROTHY CLI

   macOS:
     brew tap nikokozak/frothy
     brew install frothy

   Linux x86_64:
     Download the release tarball, extract, and put `frothy` on your PATH.

   Verify it works:
     frothy doctor

2. THE VS CODE EXTENSION

   In VS Code:
     Install "Frothy" by Nikolai Kozak from the Marketplace
     (Extension ID: NikolaiKozak.frothy)

   Or from the command line:
     code --install-extension NikolaiKozak.frothy

3. BRING A USB DATA CABLE

   A USB cable that can carry data (not charge-only).
   If you're not sure, bring two — we'll figure it out.

4. CLONE THE WORKSHOP REPO

   git clone https://github.com/nikokozak/frothy-workshop.git

YOU DO NOT NEED:
- ESP-IDF or any embedded toolchain
- Any C compiler or build tools
- Arduino IDE or PlatformIO

If `frothy doctor` fails or the extension won't install, don't
troubleshoot further — just bring that exact error to the workshop
and we'll sort it out.

Reference site: https://frothy.frothlang.org

See you Friday!
```

---

## 7. Facilitator Script — First 10 Minutes

```
[0:00 — boards are at each seat, powered on, running Pong]

"Welcome. You each have a board in front of you. It's running a
small Pong game. The paddles are controlled by the two knobs on
either side. Try twisting them.

[PAUSE — let people play with Pong for 20 seconds]

Now let's talk to the board. Plug in the USB cable if you haven't.
Open VS Code with the workshop repo.

Open the command palette — Cmd-Shift-P — and type 'Frothy: Connect Device'.
Select your serial port. If you see more than one, try the one
that looks like /dev/tty.usbserial or /dev/ttyUSB0.

[PAUSE — let people connect. Walk the room. Help with ports.]

[IF SOMEONE CAN'T CONNECT VIA VS CODE]:
  "That's fine — open a terminal and type:
   frothy connect
   We'll use the command line instead."

Good. Now let's do something to the running game.

First, stop Pong. Click the joystick button — press it straight
down. You should get a prompt.

Now type exactly this:

  matrix.brightness!: 4

Hit enter. Now restart Pong:

  demo.pong.run:

[PAUSE — Pong restarts, noticeably brighter]

You just changed a running program. You didn't recompile anything.
You didn't reflash anything. You talked to the board and it
listened.

[0:05]

Stop Pong again — click the joystick. Now type:

  grid.clear:

Then:

  grid.set: 5, 3, true

Then:

  grid.show:

[PAUSE — single pixel appears on each board]

You just replaced Pong with a single pixel. And you can bring
Pong back any time — just type demo.pong.run: again.

[0:08]

Here's what's going on. This board has a live image — a set of
named things it knows. 'grid.set' is one. 'demo.pong.run' is
another. You can ask the board what it knows. You can look at
how anything is defined. You can change any definition while
it's running.

And you cannot permanently break this board. I'll prove that
to you in about 30 minutes.

There are some mystery programs already on this board that you
haven't seen yet. Let's find them.

[Direct them to the puzzle — on frothy.frothlang.org]

Start with stage 1. Type 'words' and hit enter. Look for names
that start with 'puzzle.' — those are yours to investigate."
```

---

## 8. Frothy Teaching Progression

Concepts are introduced in this exact order, each through a concrete action — never through lecture:

| Order | Concept | Introduced via | When |
|---|---|---|---|
| 1 | Calling a word with an argument | `matrix.brightness!: 4` | Act I |
| 2 | Calling a zero-arg word | `grid.clear:`, `grid.show:` | Act I |
| 3 | Calling with multiple arguments | `grid.set: 5, 3, true` | Act I |
| 4 | Calling a named program | `demo.pong.run:` | Act I |
| 5 | Listing all names | `words` | Puzzle stage 1 |
| 6 | Inspecting a definition | `show @puzzle.dot` | Puzzle stage 2 |
| 7 | Inspecting metadata | `info @puzzle.dot` | Puzzle stage 2 |
| 8 | Reading a top-level value | `puzzle.x` (type name, see value) | Puzzle stage 3 |
| 9 | Setting a value | `set puzzle.x to 10` | Puzzle stage 3 |
| 10 | Redefining a function | `to puzzle.frame [ ... ]` | Puzzle stage 4 |
| 11 | Function composition | `puzzle.scene` calls `puzzle.top` and `puzzle.bottom` | Puzzle stage 5 |
| 12 | Reading physical input | `knob.left:` returns 0-100 | Puzzle stage 6 |
| 13 | Expressions and arithmetic | `knob.left: * grid.width / 100` | Puzzle stage 6 |
| 14 | Save / restore / wipe | `save`, `restore`, `dangerous.wipe` | Act III |
| 15 | Conditionals | `when joy.up?: [ ... ]` | Creative segment |
| 16 | Local variables | `here x is 5` | Creative segment |
| 17 | While loops | `while (not joy.click?:) [ ... ]` | Creative scaffold |

Items 1-14 are taught during the puzzle. Items 15-17 are encountered in the creative scaffold — participants read them in context, not from instruction.

---

## 9. Puzzle Design: "The Broken Beacon"

### Narrative frame

"This board was set up by someone before you. It has a set of tools called `puzzle.*` that are supposed to display a signal pattern on the LED matrix. But the tools are broken in different ways. Your job: figure out what each piece is supposed to do, fix it, and get the full signal working."

### Required `puzzle.*` words for base image

Add the following to `boards/esp32-devkit-v4-game-board/lib/base.frothy`, after the `demo.pong.*` block and before the `boot` definition:

```frothy
\ -- Puzzle namespace (workshop discovery activity) --

puzzle.x is 5
puzzle.y is 3

to puzzle.dot [
  grid.clear:;
  grid.set: puzzle.x, puzzle.y, true;
  grid.show:
]

to puzzle.frame [
  grid.set: 0, 0, true;
  grid.show:
]

to puzzle.top [
  grid.fill: true
]

to puzzle.bottom [
  nil
]

to puzzle.scene [
  puzzle.top:;
  puzzle.bottom:
]

to puzzle.reveal [
  grid.clear:;
  grid.rect: 0, 0, 3, grid.height, true;
  grid.show:
]
```

That is 8 bindings (2 values, 6 functions) totaling ~25 lines.

The `boot` definition remains unchanged — it still runs Pong:

```frothy
to boot [
  demo.pong.setup:;
  demo.pong.run:
]
```

### Stage-by-stage puzzle guide

#### Stage 1 — Discovery: "What's here?"

- **New concept**: `words`
- **Action**: Type `words`. Scan the output.
- **Discovery**: Among familiar names (`grid.*`, `matrix.*`, `demo.pong.*`), there are names starting with `puzzle.*`.
- **Success signal**: Terminal lists `puzzle.x`, `puzzle.y`, `puzzle.dot`, `puzzle.frame`, `puzzle.top`, `puzzle.bottom`, `puzzle.scene`, `puzzle.reveal`.
- **Hint (on frothy.frothlang.org)**: "Type `words` and hit enter. Look for the names you haven't seen before."

#### Stage 2 — Inspect and predict: "What does it do?"

- **New concept**: `show @name`, `info @name`
- **Action**: `show @puzzle.dot`. Read the definition. Predict what it will do. Then call `puzzle.dot:`.
- **Discovery**: The definition shows `grid.clear:; grid.set: puzzle.x, puzzle.y, true; grid.show:`. It clears the grid, sets a pixel, shows it.
- **Success signal**: Pixel appears at (5, 3).
- **Also try**: `info @puzzle.dot` — see that it's Code, zero-arity, base origin.
- **Hint 1**: "Try `show @puzzle.dot` — it shows you the definition."
- **Hint 2**: "Now call it: `puzzle.dot:`"

#### Stage 3 — Change a value: "Can I move it?"

- **New concept**: `set name to value`
- **Action**: `show @puzzle.dot` reveals it uses `puzzle.x` and `puzzle.y`. Type `puzzle.x` to see its current value (5). Type `set puzzle.x to 10`. Call `puzzle.dot:` again.
- **Discovery**: The dot moved. Values are live — changing them changes behavior.
- **Success signal**: Dot at (10, 3) or wherever they moved it.
- **Extension**: "Put the dot in the corner. What coordinates is the corner?"
- **Hint 1**: "What values does `puzzle.dot` use? Check with `show @puzzle.dot`."
- **Hint 2**: "`set puzzle.x to 0` changes the x position."

#### Stage 4 — Fix a single function: "It's broken"

- **New concept**: `to name [ ... ]` (live redefinition)
- **Action**: Call `puzzle.frame:`. See a single pixel at (0,0) — not a border.
- **Inspection**: `show @puzzle.frame` reveals: `grid.set: 0, 0, true; grid.show:`. It only draws one pixel and doesn't clear first.
- **Challenge**: "This is supposed to draw a border rectangle around the whole screen. Fix it."
- **Fix**: `to puzzle.frame [ grid.clear:; grid.rect: 0, 0, grid.width, grid.height, true; grid.show: ]`
- **Success signal**: A 12x8 border rectangle.
- **Hint 1**: "What grid function draws a rectangle?"
- **Hint 2**: "`grid.rect:` takes x, y, width, height, flag. The full screen is `grid.width` wide and `grid.height` tall."
- **Hint 3** (answer): "Type: `to puzzle.frame [ grid.clear:; grid.rect: 0, 0, grid.width, grid.height, true; grid.show: ]`"

#### Stage 5 — Fix a function chain: "Three bugs, one scene" *(core puzzle)*

- **New concept**: Functions call other functions. A fix requires understanding the whole chain.
- **Action**: Call `puzzle.scene:`. Nothing visible happens.
- **Inspection path**:
  1. `show @puzzle.scene` reveals: `puzzle.top:; puzzle.bottom:` — it calls two functions but doesn't clear before or show after.
  2. `show @puzzle.top` reveals: `grid.fill: true` — fills the entire screen (overshoots, should only draw the top half).
  3. `show @puzzle.bottom` reveals: `nil` — does nothing (stub).
- **Three bugs, three fixes**:
  1. `puzzle.top` draws everything instead of just the top 4 rows.
  2. `puzzle.bottom` is a stub that does nothing.
  3. `puzzle.scene` doesn't clear before drawing or show after drawing.
- **Iterative discovery path** (intended participant experience):
  1. Fix `puzzle.scene` first — add `grid.clear:` at start and `grid.show:` at end.
  2. Call `puzzle.scene:` — see full-fill from broken `puzzle.top`. Progress! Something is visible.
  3. Fix `puzzle.top` to only draw the top 4 rows — `to puzzle.top [ grid.rect: 0, 0, grid.width, 4, true ]`.
  4. Call `puzzle.scene:` — see a top-half outline. More progress!
  5. Fix `puzzle.bottom` to draw the bottom 4 rows — `to puzzle.bottom [ grid.rect: 0, 4, grid.width, 4, true ]`.
  6. Call `puzzle.scene:` — see complete two-part pattern. Done!
- **Success signal**: Two distinct outlined regions visible.
- **Hint 1**: "Type `show @puzzle.scene`. What does it call?"
- **Hint 2**: "Now inspect those two functions: `show @puzzle.top` and `show @puzzle.bottom`. What does each one actually do?"
- **Hint 3**: "Start by fixing `puzzle.scene` — it needs to clear before and show after. Then fix the other two."
- **Hint 4** (answer): "Fix all three: `to puzzle.scene [ grid.clear:; puzzle.top:; puzzle.bottom:; grid.show: ]` then `to puzzle.top [ grid.rect: 0, 0, grid.width, 4, true ]` then `to puzzle.bottom [ grid.rect: 0, 4, grid.width, 4, true ]`"
- **Bonus for fast movers**: "Make `puzzle.top` and `puzzle.bottom` draw different patterns — a diagonal line? A border? A checkerboard?"

#### Stage 6 — Wire to input: "Make it respond"

- **New concept**: Physical input feeds into visual output.
- **Action**: Call `puzzle.reveal:`. See a 3-wide outlined bar on the left. Twist the left knob. Call again. Same bar — it ignores the knob.
- **Inspection**: `show @puzzle.reveal` reveals: `grid.clear:; grid.rect: 0, 0, 3, grid.height, true; grid.show:` — the width is hardcoded to 3.
- **Challenge**: "Make the bar width respond to the left knob."
- **Discovery step**: Type `knob.left:` — see a number 0-100 depending on knob position.
- **Fix**: `to puzzle.reveal [ grid.clear:; grid.rect: 0, 0, (knob.left: * grid.width / 100), grid.height, true; grid.show: ]`
- **Success signal**: Call `puzzle.reveal:`, twist knob, call again — different bar width.
- **Hint 1**: "Type `knob.left:` and hit enter. What do you get? Twist the knob and try again."
- **Hint 2**: "The knob returns 0-100. The grid is 12 wide. You need to scale: `knob.left: * grid.width / 100`."
- **Bonus**: "Make it use both knobs — left controls width, right controls height."

#### Stage 7 — Save your work

- **Room-wide, facilitator-led.**
- **Action**: "Everyone type `save`. Your fixes are now on the board's storage. Type `restore` — everything still works."
- **Discovery**: Persistence. The board remembers what you did.

#### Stage 8 — Break and recover

- **Room-wide, facilitator-led.**
- **Action**: "Now intentionally break something. Type `to puzzle.scene [ 42 ]`. Call `puzzle.scene:`. It's broken."
- **Recovery**: "Type `restore`. Call `puzzle.scene:`. It's back."
- **Wipe demo** (facilitator only, projected): `dangerous.wipe` — "This is factory reset. Everything is gone. But `grid.set` and `matrix.init` are still here — the base words survive."
- **Discovery**: You can always get back. The system is recoverable. Breaking things is safe.

### Puzzle recovery paths

| Situation | Recovery |
|---|---|
| Participant redefines a puzzle word incorrectly | Try again — just redefine it again |
| Participant can't remember what the original broken version was | Doesn't matter — they're fixing it, not restoring the broken version |
| Participant breaks everything | `restore` (if they saved) |
| Participant never saved and everything is broken | `dangerous.wipe` — puzzle words reset to their broken defaults. Restart from stage 1 (much faster the second time). |
| Board is unresponsive | Ctrl-C interrupt. If still stuck: safe boot (Ctrl-C during boot), then `dangerous.wipe`. |

---

## 10. Creative Activity: "Your Board, Your Rules"

### Starter scaffold: `starter.frothy` (in workshop repo)

```frothy
\ -- Your Frothy project -----------------------------------------
\ Send this file to your board, then type: my.run:
\ Click the joystick to stop. Edit, re-send, run again.

my.x is 5
my.y is 3

to my.setup [
  matrix.init:;
  matrix.brightness!: 1
]

to my.update [
  \ Read inputs and change state here.
  nil
]

to my.draw [
  grid.clear:;
  \ Draw your scene here.
  grid.set: my.x, my.y, true;
  grid.show:
]

to my.frame [
  my.update:;
  my.draw:;
  ms: 42
]

to my.run [
  my.setup:;
  while (not joy.click?:) [
    my.frame:
  ]
]
```

### Three mission cards

#### Mission 1: Etch-a-Sketch (Easy)

> Make the joystick move a cursor that draws. Every pixel it visits stays lit. A knob does something extra — your choice.

**First safe change** (replace `my.update` and `my.draw`):
```frothy
to my.update [
  when joy.up?:    [ set my.y to my.y - 1 ];
  when joy.down?:  [ set my.y to my.y + 1 ];
  when joy.left?:  [ set my.x to my.x - 1 ];
  when joy.right?: [ set my.x to my.x + 1 ]
]

to my.draw [
  grid.set: my.x, my.y, true;
  grid.show:
]
```

Note: removing `grid.clear:` from `my.draw` is what makes pixels persist.

**Success state**: Joystick moves cursor, pixels stay lit where it's been.

**Extension (easy)**: Wrap coordinates so the cursor doesn't go off-screen. Use `math.wrap:` or `math.clamp:`.

**Extension (medium)**: Left knob controls drawing speed (change the `ms:` value in `my.frame`).

**Extension (ambitious)**: Joystick click clears the screen. Add `when joy.click?: [ grid.clear: ]` to `my.update` and change `my.run`'s exit condition.

#### Mission 2: Light Instrument (Medium)

> Both knobs control a visual pattern in real time. The joystick switches modes.

**First safe change** (replace `my.draw`):
```frothy
to my.draw [
  grid.clear:;
  grid.rect: 0, 0, (knob.left: * grid.width / 100), (knob.right: * grid.height / 100), true;
  grid.show:
]
```

**Success state**: Twisting knobs changes what's on the screen every frame.

**Extension (easy)**: Add a second pattern — `matrix.fillRect:` instead of `grid.rect:`. Switch between them with a mode variable.

**Extension (medium)**: Use a counter variable for animation. Add `my.tick is 0` at top level, increment it in `my.update`, use `math.wrap: my.tick, grid.width` to create scrolling or cycling effects.

**Extension (ambitious)**: Random sparkle — `random.below: grid.width`, `random.below: grid.height` to place random pixels each frame.

#### Mission 3: Micro-Game (Ambitious)

> Build a playable thing with something that moves on its own and something the player controls.

**First safe change** (replace `my.update`):
```frothy
to my.update [
  when joy.left?:  [ set my.x to my.x - 1 ];
  when joy.right?: [ set my.x to my.x + 1 ];
  set my.y to my.y + 1;
  when my.y >= grid.height [ set my.y to 0 ]
]
```

**Success state**: Something falls and the player can move horizontally.

**Extension (easy)**: Add a target pixel at a random position. Reset when the player reaches it.

**Extension (medium)**: Add an obstacle column that scrolls. Detect collision.

**Extension (ambitious)**: Score counter, speed increase over time, multiple obstacles.

### What participants inspect during creative work

The introspection skills from the puzzle carry over naturally:
- `show @my.update` — "What did I define this as? I forgot."
- `my.x` — Check current state without stopping the loop.
- `info @knob.left` — "What does this return again?"
- `show @math.clamp` — "How does clamp work?"

### What participants safely change first

Every mission card includes a "first safe change" — a concrete block of code to add or replace. This ensures nobody stares at the scaffold for 5 minutes. The first change always produces a visible difference.

### Recovery path

- Click joystick to stop the loop (`my.run` exits on `joy.click?:`)
- Edit `starter.frothy` in VS Code, re-send, type `my.run:` again
- If state is bad: `dangerous.wipe`, re-send file, `my.run:`
- If the board is stuck in a tight loop that doesn't check `joy.click?:`: Ctrl-C interrupt from VS Code or CLI

### The documentation site as overflow

For fast movers who finish their mission early, frothy.frothlang.org provides:
- Function reference for words they haven't used
- Stretch challenges beyond the mission card
- The `random.*` and `math.*` families for more complex behaviors

---

## 11. Solo Checkpoints

Every segment has a visible, self-verifiable checkpoint. The participant knows they're done without asking.

| Time | Checkpoint | Self-verification |
|---|---|---|
| 0:03 | "Pong is brighter" | Pong visibly brighter after `matrix.brightness!: 4` |
| 0:05 | "I see a pixel" | Single pixel on matrix after clearing Pong |
| 0:14 | "I found puzzle.* words" | Terminal shows puzzle names from `words` |
| 0:18 | "I moved the dot" | Dot at a different position after `set puzzle.x` |
| 0:25 | "My border works" | Full rectangle border from fixed `puzzle.frame` |
| 0:38 | "My scene works" | Two-part pattern from fixed `puzzle.scene` |
| 0:45 | "Knob changes the bar" | `puzzle.reveal:` shows different width on successive calls |
| 0:48 | "I saved and restored" | State survives `save` then `restore` |
| 0:52 | "I broke and recovered" | State returns after intentional break then `restore` |
| 1:12 | "Scaffold is running" | `my.run:` shows a dot on the board |
| 1:20 | "Something responds to input" | Board behavior is interactive |
| 1:38 | "I have something to show" | Board does something personal |

### Room-wide checkpoints (facilitator-initiated)

| Time | Facilitator says |
|---|---|
| 0:25 | "If you see a border rectangle, move to stage 5. If not, raise your hand." |
| 0:38 | "If `puzzle.scene:` works, move to stage 6. If not, I'm coming around." |
| 0:45 | "Everyone — type `save` now." |
| 1:10 | "Everyone — send `starter.frothy` and type `my.run:`. You should see a dot." |
| 1:25 | "15 minutes left. If you're stuck, simplify or ask for help." |
| 1:38 | "2 minutes — whatever you have right now is what you're showing." |

---

## 12. Fallback Plans

| Failure | Detection | Fix | Time budget |
|---|---|---|---|
| **VS Code can't find `frothy`** | Extension shows "CLI not found" | Set `frothy.cliPath` in VS Code settings to absolute path (`which frothy` to find it). | 1 min |
| **Multiple serial ports** | Connect dialog shows 2+ ports | `frothy --port /dev/tty.usbserial-XXXX doctor` to identify the right one. Try each. | 2 min |
| **Board boots into bad saved state** | Board is unresponsive or shows garbage on boot | Power-cycle. Press Ctrl-C during "CTRL-C for safe boot" prompt. Then `dangerous.wipe`. | 1 min |
| **Matrix stays dark** | No LED response to grid commands | 1. `matrix.init:` 2. `matrix.brightness!: 4` 3. `grid.fill: true; grid.show:` 4. If still dark: reseat USB. 5. If still dark: swap board. | 2 min |
| **Extension fails but CLI works** | Extension errors on connect/send | Fall back to `frothy --port <path> connect`. Paste code at REPL. File send via `frothy send starter.frothy`. Workshop continues in CLI mode. | 1 min |
| **Participant breaks the puzzle** | Puzzle functions produce wrong results | `restore` if they saved. `dangerous.wipe` if they didn't (resets to broken defaults). | 30 sec |
| **Participant breaks the creative** | `my.run:` errors or tight loop | Ctrl-C interrupt. Edit file. Re-send. If state poisoned: `dangerous.wipe`, re-send. | 1 min |
| **Participant falls behind puzzle** | Still on stage 3 when room is on stage 5 | Give direct answers for stages 3-4. Participant joins stage 5 with the group. | 2 min |
| **Participant falls behind creative** | No interactive behavior 10 min in | Point to Mission 1 (Etch-a-Sketch). Give the "first safe change" code verbatim. | 1 min |
| **Participant finishes everything early** | Done with mission, time remaining | Point to frothy.frothlang.org. "Find a function you haven't used." Or: "Redefine `boot` so your project runs on power-on." Or: "Try a second mission card." | 0 min |
| **USB cable is charge-only** | `frothy doctor` sees no serial port | Swap cable from spare kit. | 30 sec |
| **Board is dead** | No power LED, no serial | Swap board from spare kit. Label dead board for post-workshop triage. | 30 sec |
| **Whole-room install crisis** | 4+ people can't connect after 10 min | Switch to shortened agenda. Get each person working one-on-one. | Ongoing |

---

## 13. Attendee Cheat Sheet

Print this on one page, both sides. Hand out at the start or at the mission briefing.

```
FROTHY WORKSHOP CHEAT SHEET
------------------------------------------------------------

TALK TO THE BOARD
  matrix.init:                    Initialize the display
  grid.clear:                     Clear all pixels
  grid.set: x, y, true           Light a pixel (0,0 = top-left)
  grid.rect: x, y, w, h, true    Draw a rectangle outline
  matrix.fillRect: x,y,w,h,true  Draw a filled rectangle
  grid.fill: true                 Fill all pixels
  grid.fill: false                Clear all pixels
  grid.show:                      Push to display
  matrix.brightness!: level       Brightness (0-7)

  grid.width  -> 12              grid.height -> 8

READ INPUTS
  joy.up?:     joy.down?:         Joystick -> true/false
  joy.left?:   joy.right?:
  joy.click?:                     Joystick button
  knob.left:                      Left knob -> 0-100
  knob.right:                     Right knob -> 0-100

INSPECT
  words                           List all names
  show @name                      See a definition
  info @name                      See metadata

DEFINE AND CHANGE
  name is value                   Create a named value
  set name to value               Change a value
  to name [ ... ]                 Define/redefine a function
  to name with a, b [ ... ]       Function with parameters
  here x is 5                     Local variable

CONTROL FLOW
  if expr [ ... ] else [ ... ]    Conditional
  when expr [ ... ]               One-branch conditional
  while expr [ ... ]              Loop
  repeat n [ ... ]                Fixed-count loop
  repeat n as i [ ... ]           With counter

SAVE AND RECOVER
  save                            Snapshot to storage
  restore                         Reload from snapshot
  dangerous.wipe                  Factory reset
  Ctrl-C during boot              Safe boot

USEFUL EXTRAS
  ms: 100                         Wait 100 milliseconds
  millis:                         Current uptime in ms
  random.below: n                 Random 0 to n-1
  math.clamp: val, lo, hi        Clamp to range
  math.wrap: val, size            Wrap around (modulo)

CALL SYNTAX
  word:                           No arguments
  word: a, b                      With arguments
  (word: a, b)                    Nested (wrap in parens)

DOCS: frothy.frothlang.org
```

---

## 14. Facilitator Recovery Card

Print this on one card. Keep it at the facilitator station.

```
FACILITATOR RECOVERY CARD
------------------------------------------------------------

BOARD WON'T CONNECT
  1. Check cable (swap if in doubt)
  2. frothy --port /dev/tty.usbserial-XXXX doctor
  3. Multiple ports? Try each one
  4. Nothing? Swap board from spares

MATRIX IS DARK
  1. matrix.init:
  2. grid.fill: true; grid.show:
  3. matrix.brightness!: 4
  4. Still dark? Swap board

BOARD STUCK / UNRESPONSIVE
  1. Ctrl-C
  2. No response? Unplug/replug USB
  3. Ctrl-C during safe boot prompt
  4. dangerous.wipe
  5. Verify: 1 + 1 -> 2, info @matrix.init

PUZZLE STATE BROKEN
  1. restore (if they saved)
  2. dangerous.wipe (resets puzzle to broken defaults)
  3. Restart from stage 1 (fast second time)

CREATIVE STATE BROKEN
  1. Ctrl-C to stop running loop
  2. Re-send starter.frothy
  3. If poisoned: dangerous.wipe, re-send

VS CODE BLOCKED
  Don't debug the extension during the session.
  Fall back to: frothy --port <path> connect
  File send: frothy send starter.frothy

PARTICIPANT BEHIND
  Puzzle 3-4: give them the answer, move to stage 5
  Puzzle 5: walk through it, 2 min max
  Creative: suggest Mission 1, give first safe change

VERIFY BOARD HEALTHY
  1 + 1                -> 2
  info @matrix.init    -> Code, base origin
  info @puzzle.dot     -> Code, base origin
  grid.clear:; grid.set: 1,1,true; grid.show:  -> pixel
```

---

## 15. Workshop Artifacts Checklist

Everything that must exist before the session:

| # | Artifact | Location | Notes |
|---|---|---|---|
| 1 | `puzzle.*` words added to base.frothy | `boards/esp32-devkit-v4-game-board/lib/base.frothy` | ~25 lines, see Section 9 for exact code |
| 2 | All boards reflashed with updated base image | Physical boards | Must show `puzzle.*` names on `words` |
| 3 | `starter.frothy` scaffold file | `workshop/starter.frothy` in workshop repo | See Section 10 for exact code |
| 4 | Workshop repo exported and pushed | github.com/nikokozak/frothy-workshop | Must contain `README.md` and `starter.frothy` per ADR-122 |
| 5 | Puzzle guide on docs site | frothy.frothlang.org (Machine section) | Stages 1-8 with hints |
| 6 | Mission cards on docs site | frothy.frothlang.org (Machine section) | Three missions with first safe changes and extensions |
| 7 | Attendee cheat sheet printed | Physical handout | One per participant, see Section 13 |
| 8 | Facilitator recovery card printed | Physical card | One copy, see Section 14 |
| 9 | Pre-workshop email sent | Email to participants | See Section 6 |
| 10 | Spare boards packed | Physical spares in room kit | At least 1, ideally 2 |
| 11 | Spare USB data cables packed | Physical spares in room kit | At least 2 |
| 12 | Facilitator laptop verified | Facilitator machine | CLI, VSIX, esptool, can reflash |
| 13 | Docs site live and accessible | frothy.frothlang.org | Verify from a non-author browser |

---

## 16. Rehearsal Checklist

### What to rehearse

| Item | How to verify |
|---|---|
| Flash a board with updated base image | `frothy flash` succeeds; board boots into Pong; `words` shows `puzzle.*` names |
| Run through puzzle stages 1-8 on a real board | Each stage produces expected board signal; total time ~25-30 min solo |
| Verify puzzle broken states are correct | `puzzle.frame:` shows single pixel at (0,0). `puzzle.scene:` shows nothing visible. `puzzle.reveal:` shows fixed 3-wide bar. |
| Send `starter.frothy` and run `my.run:` | Dot appears at (5,3); joystick click stops the loop |
| Implement Mission 1 first safe change | Joystick moves cursor, pixels persist |
| Implement Mission 2 first safe change | Knobs control rectangle dimensions in real time |
| Implement Mission 3 first safe change | Pixel falls, joystick moves horizontally |
| Test save/restore/wipe cycle | State persists across save/restore; wipe returns to base image with broken puzzle defaults |
| Test safe boot | Ctrl-C during boot prompt skips restore and boot, enters REPL |
| Test CLI fallback | `frothy connect` works; pasting code works; `frothy send starter.frothy` works |
| Test with a charge-only cable | Confirm it fails cleanly and is distinguishable from a dead board |
| Verify docs site | frothy.frothlang.org loads; puzzle guide and mission cards are accessible |
| Print handouts | Cheat sheet readable at arm's length; recovery card has all commands |
| Time full workshop solo | Fits in 2 hours with margin for helping participants |

### The workshop is ready when

- Every board shows `puzzle.*` names on `words`
- Every board boots into Pong on clean power-cycle
- `puzzle.scene:` on a fresh board produces no visible output (correct broken state)
- `starter.frothy` sends cleanly and `my.run:` works
- The facilitator can recover a broken board in under 60 seconds
- Puzzle guide and mission cards are on frothy.frothlang.org
- Printed handouts are in the room kit
- The pre-workshop email has been sent
- The facilitator has run through the full workshop solo at least once on a real board

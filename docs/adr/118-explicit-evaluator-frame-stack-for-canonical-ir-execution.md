# Frothy ADR-118: Explicit Evaluator Frame Stack For Canonical IR Execution

**Date**: 2026-04-14
**Status**: Accepted
**Spec sections**: `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 6.3, 6.4, 6.5
**Roadmap milestone(s)**: M5, M8, M10, post-v0.1 evaluator hardening
**Inherited Froth references**: `docs/archive/adr/040-cs-trampoline-executor.md`

## Context

Frothy `v0.1` persists canonical tree-shaped IR as the source of truth for
`Code` values. Frothy ADR-105 made that choice explicitly so persistence and
inspection stay semantic, stable, and independent of an early bytecode
decision.

The current evaluator executes that IR with recursive C calls. `CALL`,
`IF`, `SEQ`, local writes, index operations, and other compound node kinds
re-enter `frothy_eval_node()` recursively. `while` itself is implemented as an
iterative C loop, so one long-running loop does not currently add one C frame
per iteration, but nested call depth and nested expression depth still consume
host C stack.

That is not an acceptable long-term execution architecture for embedded Frothy.
An ESP32 `boot` loop panic exposed the issue sharply. The immediate regression
was an oversized evaluator frame caused by a large local array, but that bug
only surfaced because the evaluator still relies on hidden C stack depth in the
first place. Shrinking frames and adding stack-budget proofs helps catch such
regressions, but it does not answer the architectural requirement.

The requirement is stricter:

- ordinary embedded loops must be boringly safe
- nested game logic must not depend on compiler-specific C frame sizes
- execution depth limits must be explicit, bounded, and testable
- overflow must fail as a normal Frothy runtime error rather than as a target
  crash
- hot execution paths must not hide heap allocation

Frothy also has simplifying constraints:

- `Code` is non-capturing in `v0.1` per Frothy ADR-103, so evaluator frames do
  not need closure environments
- canonical IR remains the persisted truth per Frothy ADR-105
- the runtime already uses fixed-capacity storage for other hot-path resources

The inherited Froth substrate already reached the same conclusion in archived
ADR-040: a guard on recursive execution is not enough. The durable answer is an
explicit continuation stack managed by the VM/runtime rather than by the host C
stack.

## Options Considered

### Option A: Keep the recursive evaluator and harden around it

Continue with recursive `frothy_eval_node()` execution, shrink oversized local
frames, and rely on stack-budget proofs plus optional depth guards.

Trade-offs:

- Pro: smallest code change.
- Pro: the recent ESP32 regression is addressed immediately.
- Pro: compile-time stack-usage proofs stay useful.
- Con: nested Frothy execution depth still depends on target stack size,
  compiler output, and unrelated native frame usage.
- Con: the architecture remains vulnerable to new large locals or deeper helper
  layering.
- Con: ordinary looping and nested user code still compete with hidden host
  stack consumption.

### Option B: Introduce a bytecode VM as the new required execution form

Compile canonical IR to bytecode and make the bytecode interpreter the main
execution engine immediately.

Trade-offs:

- Pro: explicit instruction pointer and explicit execution frames come naturally.
- Pro: may create a path to later performance wins.
- Con: larger rewrite than the immediate problem requires.
- Con: risks turning bytecode into the practical semantic truth too early,
  which would blur Frothy ADR-105's canonical-IR contract.
- Con: increases implementation surface while the immediate requirement is
  execution-depth safety, not a new persisted form.

### Option C: Execute canonical IR through an explicit evaluator frame stack

Keep canonical IR as the authoritative persisted form, but replace recursive
IR-to-IR execution with a trampoline loop over explicit evaluator frames.

Trade-offs:

- Pro: preserves Frothy ADR-105 unchanged. Canonical IR remains the persisted
  truth and inspection source.
- Pro: makes Frothy execution depth bounded by an explicit frame capacity rather
  than hidden C stack depth.
- Pro: gives embedded targets a clean runtime overflow mode instead of a panic
  or segfault.
- Pro: keeps the fix focused on the actual architecture problem.
- Con: the evaluator becomes a small state machine instead of a direct
  recursive tree walk.
- Con: several node kinds need explicit phase/state handling for intermediate
  results.

### Option D: Special-case loops or tail calls only

Reduce recursive pressure by optimizing `while`-like paths or tail positions
while keeping the general recursive evaluator shape.

Trade-offs:

- Pro: smaller rewrite than a full trampoline.
- Con: does not solve nested non-tail calls or deep expression trees.
- Con: leaves the hidden C stack as the real depth bound.
- Con: complicates the evaluator without fixing the core architecture error.

## Decision

**Option C.**

Frothy will execute canonical IR through an explicit evaluator frame stack.
Recursive C re-entry from one IR node into another is no longer an acceptable
steady-state architecture for Frothy on embedded targets.

Canonical IR remains the persisted and inspectable truth. This ADR does not
promote bytecode into a required semantic layer. If a lower execution form ever
arrives later, it must remain an additive cache or implementation detail rather
than the normative persisted contract.

### Execution model

Evaluation will run in a single dispatcher loop. That loop advances explicit
frames until the active evaluator frame stack is empty or an error, interrupt,
or reset unwinds execution.

An evaluator frame must carry enough state to resume one node without relying
on the host C stack. The exact struct may change during implementation, but the
frame model must support at least:

- program identity
- current node id
- current phase/state within that node
- local-frame ownership or a stable reference to the active locals
- child index or resume position where needed
- temporary owned values needed across child evaluation steps
- destination for the produced result

The implementation may either adapt the dormant VM control-stack substrate or
add a Frothy-specific evaluator-frame stack inside `frothy_runtime_t`. The
accepted requirement is architectural, not cosmetic: Frothy execution depth
must be bounded by an explicit runtime-managed frame store, not by the host C
stack.

### Required invariants

The explicit evaluator-frame design must satisfy all of these:

1. No recursive evaluator calls for IR-to-IR execution.
2. No hot-path heap allocation in the dispatcher loop.
3. Left-to-right evaluation order remains unchanged from the current canonical
   IR contract.
4. `while` continues to poll interrupts at safe points and must not grow
   execution depth per iteration.
5. Reset, interrupt, and ordinary error exits unwind evaluator state
   deterministically.
6. Frame-stack overflow reports a normal Frothy error. Use
   `FROTH_ERROR_CALL_DEPTH` unless a later accepted ADR deliberately widens the
   error surface.
7. Inspection, persistence, and restore semantics remain driven by canonical IR
   rather than by frame layout.

### Node obligations

At minimum, the first explicit-stack tranche must cover the nodes that create
recursive evaluator pressure in normal user code:

- `CALL`: evaluate callee and arguments left to right, then dispatch builtin,
  native, record-definition, or `Code` body without recursive evaluator entry
- `IF`: evaluate the condition, choose one branch, and resume without C
  recursion
- `WHILE`: cycle through condition and body phases in one resumable frame
- `SEQ`: advance over items with explicit resume state
- compound expression nodes such as local writes, slot writes, index reads,
  index writes, and record construction: store intermediate state in frames
  rather than on the C stack

Leaf nodes such as literals and direct local reads may still complete in one
dispatcher step because they do not recurse.

### Native and FFI boundary

This ADR removes hidden C-stack growth from Frothy-to-Frothy execution inside
the evaluator. It does not claim that arbitrary native code consumes no C
stack.

If a future native or FFI entry point needs to re-enter Frothy evaluation from
C, that re-entry must go through an explicitly bounded top-level evaluator
entry. Future features must not restore unbounded recursive evaluation by
another path.

### Migration rules

- Keep the compile-time stack-budget proof during the migration. It remains a
  useful tripwire for oversized helper frames and native call paths.
- Land the evaluator-frame stack before expanding more game-facing or
  loop-heavy language/library surface.
- Remove or quarantine the recursive evaluator path once the explicit-stack
  path passes the focused proof ladder.

## Consequences

- Ordinary embedded looping is no longer hostage to hidden evaluator C-stack
  depth once the implementation lands.
- The relevant depth budget becomes explicit, target-visible, and testable.
- Frothy ADR-105 stays intact: persistence and inspection still speak canonical
  IR.
- The evaluator implementation becomes more state-machine-like and therefore
  needs strong focused tests around control flow, ownership, and unwind paths.
- The existing stack-budget proof remains useful, but only as a secondary
  guardrail rather than the main architectural defense.
- Later bytecode work, if any, is constrained to additive execution caching
  rather than a silent change to the persisted source of truth.

## References

- `docs/spec/Frothy_Language_Spec_v0_1.md`, sections 6.3, 6.4, 6.5
- `docs/adr/103-non-capturing-code-value-model.md`
- `docs/adr/105-canonical-ir-as-persisted-code-form.md`
- `docs/archive/adr/040-cs-trampoline-executor.md`
- `src/frothy_eval.c`
- `src/froth_vm.h`

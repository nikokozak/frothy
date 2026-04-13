# ADR-045: catch Returns Froth-Truthy Flag

**Date**: 2026-03-21
**Status**: Accepted
**Spec sections**: Section 6.1 (catch/throw), Section 7.2 (truth convention)
**Supersedes**: Spec Section 6.1 stack effect `( q -- ... 0 | e )`

## Context

`catch` currently returns `( q -- ... 0 | e )` — 0 on success, the error code on failure. This follows the C/POSIX convention where 0 means success.

Froth's truth convention is: 0 = false, -1 (or any nonzero) = true. All boolean-returning words follow this: `key?`, `<`, `>`, `=`, `s.=` all return -1 for true and 0 for false.

`catch` is the only word in the language where 0 means "the good thing happened." This creates an impedance mismatch with `if`, `choose`, and every conditional pattern:

```froth
\ Broken — success takes the else branch
[ something ] catch if "this runs on FAILURE" else "this runs on SUCCESS"
```

Every `catch` usage requires `0 =` to invert the result before branching. This is a tax on every error-handling pattern and a trap for newcomers.

## Decision

Change `catch` to return a predictable two-value result: `( q -- ... ecode flag )`.

- **flag** is -1 (Froth true) on success, 0 (Froth false) on failure. Consistent with every other boolean in the language.
- **ecode** is 0 on success, the error code on failure. Always present, so stack depth is the same in both paths.

Add `try` as a stdlib convenience: `( q -- ... flag )`. Drops the error code, returns only the flag.

### Stack effects

**`catch`:** `( q -- ... ecode flag )`

On success:
- DS is restored to pre-catch state (with whatever `q` left on it)
- 0 is pushed (error code = no error)
- -1 is pushed (flag = success = Froth true)

On failure:
- DS is restored to pre-catch snapshot
- The error code (positive integer) is pushed
- 0 is pushed (flag = failure = Froth false)

**`try`:** `( q -- ... flag )` — defined as `catch swap drop` in stdlib.

### Usage patterns

```froth
\ Simple: did it work?
[ something ] try if "ok" s.emit cr

\ Detailed: branch on success/failure, use error code on failure
[ something ] catch if
  drop "ok" s.emit cr
else
  dup 4 = if "undefined word" s.emit cr else . cr
```

### What changes

**Kernel (`froth_primitives.c`):** The `catch` implementation currently pushes one value (0 or the error code). Change to push two values: error code then flag.

**Boot (`froth_boot.c`):** The autorun pattern `[ 'autorun call ] catch drop` becomes `[ 'autorun call ] catch drop drop` (two values to discard). Or use `try drop`.

**REPL (`froth_repl.c`):** The REPL's error handler checks the catch result. Needs to pop flag then ecode instead of just ecode.

**Stdlib (`core.froth`):** Add `: try catch swap drop ;`

**Spec:** Update Section 6.1 stack effect from `( q -- ... 0 | e )` to `( q -- ... ecode flag )`.

**Snapshot prims:** `save`, `restore`, `wipe` are called from Froth via catch patterns. Check all catch usage sites.

**Tests:** Kernel test `test_error_handling.sh` checks catch behavior. Must be updated.

## Consequences

- `catch` is consistent with Froth's truth convention. `if`, `choose`, and conditional patterns work naturally.
- Stack depth after `catch` is always +2 (ecode + flag), regardless of success or failure. Predictable for stack-effect analysis.
- The `try` convenience covers the common "I just need to know if it worked" case.
- Existing code that uses `catch` needs updating (one extra `drop` for the flag, or switch to `try`).
- The `autorun` boot pattern and REPL error handler are internal and updated as part of this change.

## References

- Spec Section 6.1: catch/throw
- Spec Section 7.2: truth convention (0 = false, -1 = true)
- ADR-015: catch/throw via C-return propagation (implementation mechanism, unchanged)
- ADR-037: autorun boot pattern (uses catch)

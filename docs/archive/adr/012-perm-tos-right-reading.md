# ADR-012: Perm Pattern TOS-Right Reading Direction

**Date**: 2026-03-02
**Status**: Accepted
**Spec sections**: 3.4 (Pattern literal), 5.10 (pat), 5.11 (perm), 8 (FROTH-Named), 11 (FROTH-Stdlib), 13 (FROTH-Perf)

## Context

The original `perm` spec used a TOS-left convention: the leftmost element of a pattern is the output TOS, and index 0 = TOS. This means `p[1 0]` is swap, `p[0 1]` is identity, and `p[2 0 1]` is rot.

Froth displays stacks left-to-right with TOS on the right: `[10 20 30]` means 30 is TOS. Stack comments follow the same convention: `( a b c -- b c a )`.

The TOS-left pattern reads in the opposite direction from every other stack representation in the language. The user must mentally reverse the pattern to visualize the result, which undermines `perm`'s goal of being a readable, canonical stack operation.

Additionally, we want a letter-based surface syntax (`p[a b c]`) where letters name input positions. The letter convention must be consistent with the numeric convention used by `pat`.

## Options Considered

### Option A: TOS-left (original spec)

Pattern reads TOS-first. Index 0 = TOS. `p[0 1]` is identity, `p[1 0]` is swap.

Trade-offs:
- Pro: identity is ascending order `[0 1 2]`
- Con: pattern reads opposite to stack diagrams, stack comments, and REPL output
- Con: `rot` is `p[2 0 1]` — no visual correspondence to `( a b c -- b c a )`

### Option B: TOS-right with 0=deepest

Pattern reads bottom-to-top (matching stack diagrams). Index 0 = deepest in window. `p[0 1]` is identity, `p[1 0]` is swap (same numeric patterns as Option A, but reading direction flipped).

Trade-offs:
- Pro: pattern is a picture of the resulting stack
- Pro: identity is ascending order
- Con: 0=deepest contradicts how programmers typically think about stack indexing (0=TOS is the norm in Forth `pick`, etc.)
- Con: if `pat` also uses 0=deepest, experienced programmers will make off-by-one-direction errors

### Option C: TOS-right with 0=TOS

Pattern reads bottom-to-top (matching stack diagrams). Index 0 = TOS (same as the old spec). Letters: `a`=TOS, ascending letters move deeper. Both `p[...]` and `pat` use the same convention.

Trade-offs:
- Pro: pattern is a picture of the resulting stack
- Pro: 0=TOS matches programmer expectations and existing stack indexing
- Pro: `pat` and `p[...]` use a single consistent convention — the only difference is reading direction, not index meaning
- Pro: letter-based `p[a b]` for swap reads as "put TOS on the left (bottom), put next-below on the right (top)" — matches the visual result
- Con: identity is descending order: `p[b a]` for n=2, `p[c b a]` for n=3, `[1 0]`/`[2 1 0]` for `pat`
- Con: descending identity is unfamiliar at first

## Decision

**Option C: TOS-right with 0=TOS.**

The deciding factors:

1. **The pattern is a picture.** `p[a b]` for swap on `[25 33]` gives `[33 25]` — you can read the pattern as the output stack layout. This is the single most important usability property.

2. **Consistent index convention.** 0=TOS everywhere — in `perm`, `pat`, `pick` (future), FROTH-Named depth tracking. No convention splits between surface syntax and programmatic constructors.

3. **Descending identity is cheap.** Nobody writes identity patterns in practice. The descending order for n=3..5 is immediately recognizable (`c b a`, `d c b a`). The common patterns (swap, rot, dup, over) are what matter, and they all read naturally.

4. **Stack comments map directly.** Under this convention with letters, the standard stack comment notation can be read alongside patterns:
   - `rot ( a b c -- b c a )` where a=TOS: the `--` right side `b c a` matches `p[b c a]` reading left-to-right as `[bottom ... TOS]`, giving `[b c a]` = b at bottom, c in middle, a at top — wait, that's not rot.

   Correction: with a=TOS in Froth perm convention (not the Forth stack comment convention), the labels differ from traditional stack comments. The pattern is still a visual picture of the output stack, which is the primary benefit.

## Consequences

### What becomes easier

- Reading `perm` patterns: the pattern visually matches the resulting stack layout.
- Writing `perm` patterns: "draw" the output stack you want using the input labels.
- Teaching `perm`: "the pattern is a picture of your output stack, left=bottom, right=top."

### What becomes harder

- Identity patterns are descending, which looks unfamiliar initially.
- The spec's FROTH-Named compilation algorithm (Section 8) must emit patterns in reversed order compared to the old spec.

### Spec sections requiring updates

- **Section 3.4** (`p[...]`): add letter-based syntax, state TOS-right reading direction.
- **Section 5.10** (`pat`): state that the quotation reads bottom-to-top (leftmost = deepest output, rightmost = TOS output), 0=TOS.
- **Section 5.11** (`perm`): reverse the output mapping. Pattern rightmost element → new TOS.
- **Section 8** (FROTH-Named): update compilation algorithm for name references and cleanup perm.
- **Section 11** (FROTH-Stdlib): update all `perm` definitions.
- **Section 13** (FROTH-Perf): no semantic changes, but examples should use new convention.

### Letter syntax summary

| Letter | Meaning |
|--------|---------|
| `a` | TOS (index 0) |
| `b` | next below TOS (index 1) |
| `c` | next (index 2) |
| ... | ... |

Pattern reads left-to-right = bottom-to-top of output stack.

### Updated standard library

```
dup:   1 p[a a]   perm    or  1 [0 0] pat perm
swap:  2 p[a b]   perm    or  2 [0 1] pat perm
drop:  1 p[]      perm    or  1 [] pat perm
over:  2 p[b a b] perm    or  2 [1 0 1] pat perm
rot:   3 p[b a c] perm    or  3 [1 0 2] pat perm
-rot:  3 p[a c b] perm    or  3 [0 2 1] pat perm
nip:   2 p[a]     perm    or  2 [0] pat perm
tuck:  2 p[a b a] perm    or  2 [0 1 0] pat perm
```

## References

- Froth Language Spec v1.0, Sections 3.4, 5.10, 5.11, 8, 11, 13
- ADR discussion session 2026-03-02

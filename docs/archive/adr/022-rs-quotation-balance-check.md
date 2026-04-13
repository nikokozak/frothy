# ADR-022: Return Stack Quotation Balance Check

**Date**: 2026-03-07
**Status**: Accepted
**Spec sections**: 5.8 (Return stack transfer)

## Context

The spec requires `>r`, `r>`, `r@` primitives and mandates that RS under/overflow signals `ERR.RSTACK`. It does not require RS depth balance across quotation boundaries.

Without a balance check, a quotation that pushes to RS without a matching pop silently leaks values onto the RS. On an embedded target, this corruption is hard to diagnose — the RS is shared across all quotation calls.

## Options Considered

### Option A: No balance check (spec-minimal)

Trust the programmer. RS leaks are their problem.

- Pro: zero overhead, spec-compliant.
- Con: silent corruption on embedded targets.

### Option B: Static check at quotation build time

Count `>r` and `r>` in the body at compile time.

- Pro: catches simple cases early.
- Con: breaks down with conditional paths (`choose` branches). False positives.

### Option C: Runtime check at quotation exit

Snapshot RS depth on `froth_execute_quote` entry; compare on exit. One local variable, one comparison.

- Pro: catches all violations, near-zero cost (stack-local variable).
- Con: slightly beyond spec.

## Decision

Option C. The cost is a single `uint8_t` local and one comparison per quotation call — effectively free. The benefit is catching silent RS corruption that would otherwise be extremely hard to debug on embedded hardware.

On violation, clear `last_error_slot` (the error is structural, not attributable to a single word) and return `FROTH_ERROR_UNBALANCED_RETURN_STACK_CALLS` (code 15). The REPL displays `error(15): unbalanced return stack`.

If a `throw` fires mid-quotation, `catch` handles RS cleanup and the balance check never runs.

## Consequences

- Quotations must leave RS depth unchanged on normal exit.
- Combinators like `dip` (`swap >r call r>`) naturally satisfy this — the `>r`/`r>` pair is balanced within the quotation body.
- Error code 15 added to the stable error enum.

## References

- Spec v1.1 Section 5.8: Return stack transfer
- ADR-016: Stable explicit error codes

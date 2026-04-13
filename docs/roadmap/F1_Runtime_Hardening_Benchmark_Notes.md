# F1 Runtime Hardening Benchmark Notes

Date: 2026-04-12

Host profile:

- repo-local host benchmark run from `./build/frothy_runtime_bench`
- default checked-in capacities:
  `FROTHY_EVAL_VALUE_CAPACITY=256`,
  `FROTHY_OBJECT_CAPACITY=128`
- single-run host timings recorded as local comparison notes, not release
  gates

Proof command:

- `make build`
- `ctest --test-dir build --output-on-failure -R '^frothy_(parser|eval|snapshot|ffi)$'`
- `./build/frothy_runtime_bench`

Scope:

- this slice fixes evaluator scratch and runtime object/free-span metadata to
  explicit capacities and removes per-call heap churn from
  `src/frothy_eval.c`
- text bytes and cloned IR program bodies still allocate dynamically
- parser growth, snapshot codec payload buffers, and shell source accumulation
  remain separate bounded-memory follow-on work

## Before Fixed-Capacity Hardening

| case | iterations | total_ms | ns_per_iter | peak_eval_values | peak_objects |
|---|---:|---:|---:|---:|---:|
| arithmetic | 12696 | 260.460 | 20515.1 | 5 | 0 |
| call_dispatch | 1388562 | 272.844 | 196.5 | 2 | 0 |
| slot_access | 1040438 | 275.421 | 264.7 | 2 | 0 |
| snapshot_save | 3150 | 272.104 | 86382.2 | 0 | 0 |
| snapshot_restore | 5806 | 281.877 | 48549.3 | 0 | 19 |
| ffi_loop | 1652487 | 274.700 | 166.2 | 1 | 0 |

## After Fixed-Capacity Hardening

| case | iterations | total_ms | ns_per_iter | peak_eval_values | peak_objects |
|---|---:|---:|---:|---:|---:|
| arithmetic | 13751 | 267.895 | 19481.9 | 5 | 0 |
| call_dispatch | 1750758 | 270.549 | 154.5 | 1 | 0 |
| slot_access | 916667 | 260.086 | 283.7 | 2 | 0 |
| snapshot_save | 2796 | 264.873 | 94732.8 | 0 | 0 |
| snapshot_restore | 5572 | 280.145 | 50277.3 | 0 | 19 |
| ffi_loop | 2750001 | 500.945 | 182.2 | 1 | 0 |

## Notes

- `call_dispatch` peak evaluator scratch dropped from `2` values to `1`
  because user `Code` calls now evaluate arguments directly into callee
  locals.
- This pass is primarily about explicit memory budgets and removing hot-path
  heap churn, not a full speed-only optimization sweep. `slot_access` and the
  snapshot cases remain candidates for later targeted tuning.
- The benchmark kernels now match the checked-in tranche scope directly:
  `slot_access` times a top-level slot read/write kernel without an extra user
  call frame, and the snapshot cases seed one text slot, one code slot, and
  one cells slot before save/restore timing.
- The saved `before` table was captured before this late benchmark-kernel
  tightening. Arithmetic, call-dispatch, and FFI rows remain directly
  comparable; the `slot_access` and snapshot rows should be read as historical
  baseline versus tightened final kernel rather than exact like-for-like
  samples.
- The benchmark target now re-runs the measured pass until each case clears
  the `250 ms` floor so the checked-in table reflects the intended timing
  window.

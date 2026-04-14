# F1 Runtime Hardening Benchmark Notes

Date: 2026-04-13

Host profile:

- repo-local host benchmark run from `./build/frothy_runtime_bench`
- default checked-in capacities:
  `FROTHY_EVAL_VALUE_CAPACITY=256`,
  `FROTHY_OBJECT_CAPACITY=128`,
  `FROTHY_PAYLOAD_CAPACITY=16384`
- single-run host timings recorded as local comparison notes, not release
  gates

Proof command:

- `make build-kernel`
- `ctest --test-dir build --output-on-failure -R '^frothy_(parser|eval|snapshot|ffi)$'`
- `./build/frothy_runtime_bench`

Scope:

- this follow-on slice moves live `Text` bytes and runtime `Code` program
  bodies into one explicit runtime payload arena, with packed one-block code
  clones and direct snapshot restore materialization into runtime-owned
  storage
- evaluator scratch, runtime object/free-span metadata, parser growth,
  snapshot codec work buffers, and shell source accumulation remain on the
  previously landed bounded-memory paths
- snapshot wire format, parser capacities, shell behavior, and inspect/control
  surfaces remain unchanged in this pass

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

## After Persistent-Payload Hardening

| case | iterations | total_ms | ns_per_iter | peak_eval_values | peak_objects | peak_payload_bytes |
|---|---:|---:|---:|---:|---:|---:|
| arithmetic | 14473 | 274.974 | 18999.1 | 5 | 0 | 0 |
| call_dispatch | 1672662 | 277.485 | 165.9 | 1 | 0 | 0 |
| code_literal | 1375001 | 301.951 | 219.6 | 0 | 0 | 144 |
| slot_access | 1553271 | 274.214 | 176.5 | 2 | 0 | 0 |
| parse_named_fn | 48542 | 273.957 | 5643.7 | 0 | 0 | 0 |
| snapshot_save | 3013 | 300.227 | 99643.9 | 0 | 0 | 0 |
| snapshot_restore | 4969 | 266.569 | 53646.4 | 0 | 19 | 1488 |
| ffi_loop | 1559389 | 274.504 | 176.0 | 1 | 0 | 0 |

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
- `peak_payload_bytes` is now part of the checked-in benchmark surface. The
  new `code_literal` row isolates direct runtime `Code` literal creation
  churn without a setup-time factory binding, and `snapshot_restore` now
  reports the live payload high-water for restoring the maintained
  text-plus-code snapshot kernel.
- Relative to the previous fixed-capacity run, `snapshot_restore` moved from
  `50277.3 ns/iter` to `53646.4 ns/iter` while now reporting
  `1488` payload bytes at peak. That modest host-only regression buys one
  explicit owner for runtime text/code payload and removes the restore-time
  heap decode plus second code clone path.
- The saved `before` table was captured before this late benchmark-kernel
  tightening. Arithmetic, call-dispatch, and FFI rows remain directly
  comparable; the `slot_access` and snapshot rows should be read as historical
  baseline versus tightened final kernel rather than exact like-for-like
  samples.
- The benchmark target now re-runs the measured pass until each case clears
  the `250 ms` floor so the checked-in table reflects the intended timing
  window.

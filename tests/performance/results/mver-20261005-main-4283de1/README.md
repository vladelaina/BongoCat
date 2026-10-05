# Mver benchmark evidence against current upstream, 2026-10-05

Upstream base: `4283de1599c7da914138f82a99405d67c2c861ec`.
These are newly collected samples for this base. No measurements from the
previous, reverted upstream history are included.

## Setup

- Linux x86-64, Intel Xeon Platinum 8573C, GCC 14.2.0
- Release `-Os`, IPO enabled for both renderer variants and core; opaque sinks
  compiled without IPO; strict `-Wall -Wextra -Werror` benchmark build
- Benchmark executable SHA-256:
  `37215a0e4b674b8c5a4ab5d1f8618a68892b4031bdf4b1ef59a0df1386197826`
- Pinned to allowed CPU 0; other project builds/tests paused during timing
- Three runs, each with 3 warm-up and 15 measured rounds; 2,000 operations per
  variant interleaved in 100-operation AB/BA blocks
- Collected 2026-10-05 14:48:14–14:48:57 UTC on a noisy shared cloud host

Both paths use the same current overlay layout, isolating computation cost.
The baseline renderer is frozen from the base commit above; only its test
symbols are remapped. The original layout is separately retained for size
measurement: 360 bytes before, 600 bytes after, +240 bytes per overlay and no
additional heap allocations.

## Results and limits

`summary.csv` aggregates all 45 paired trials. Paired CPU median changes:

- Stationary tablet: **−85.149%**, −37.379512 microseconds per frame
- Moving tablet: **−43.181%**, −19.277795 microseconds per frame
- Tablet changed between phases, forcing two misses: **+0.138%**
- Geometry-only cache miss: **+2.269%**
- Uncached mouse controls: stationary −1.093%, moving +1.296%

The control variation and miss-path results should remain visible alongside
successful reuse. These measurements support a component CPU improvement when
the cache can reuse geometry, not a guarantee of faster execution for every
input. Values are batch averages; p95 is the percentile of batch averages,
not single-event tail latency. GL callbacks consume vertex data but perform no
GPU work. Native windows, capture/input dispatch, GPU rendering, composition,
and displayed end-to-end latency are not measured.

## Reproduce

Build the optional benchmark as described in [MVER.md](../../MVER.md), then run:

```sh
for run in 1 2 3; do
  taskset -c 0 build/bongo_cat_mver_bench 2000 15 100 > run-$run.csv 2> run-$run.meta
done
python3 tests/performance/analyze_mver.py run-1.csv run-2.csv run-3.csv
```

The changed runtime sources and focused test/benchmark targets compiled with
strict warnings-as-errors. All six non-runtime CTests passed in that build.
The full diagnostic build and all seven CTests also passed after explicitly
allowing two existing upstream warning categories with
`-Wno-error=misleading-indentation -Wno-error=unused-function`; the affected
unrelated source files remain unchanged. See MVER.md for exact locations.

Current-source ASan/UBSan runs also passed both focused tests: 41,066 byte-exact
geometry comparisons and 8,308 ordered GL phases (44,765,568 exact bytes).
Leak detection was disabled because LeakSanitizer is unsupported under this
execution environment's tracing; no leak-check result is claimed.

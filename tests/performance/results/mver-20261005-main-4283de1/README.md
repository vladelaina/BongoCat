# Tablet cache specification-driven evidence, 2026-10-05

Upstream base: `4283de1599c7da914138f82a99405d67c2c861ec`.
These fresh eleven-scenario samples supersede the earlier seven-scenario run.
The acceptance contract was committed before the gap-closing tests; production
logic did not change during this reconciliation. See
[the specification](../../../../docs/specs/tablet-geometry-cache.md).

## Setup

- Linux x86-64, Intel Xeon Platinum 8573C, GCC 14.2.0
- Release `-Os`, IPO for both renderer variants and core; opaque sinks without
  IPO; strict `-Wall -Wextra -Werror`; no counting in timed renderer subjects
- Executable SHA-256:
  `29f864e5046e49289928bfafa05e03cf2e2eb03c8b55bbba075a8d9496f4d58b`
- Pinned to allowed CPU 0; other project builds/tests paused during timing
- Three runs, each with 3 warm-up and 15 measured rounds; 2,000 operations per
  variant interleaved in 100-operation AB/BA blocks
- Collected 2026-10-05 15:41:11–15:42:39 UTC on a noisy shared cloud host
- 45 measured pairs per scenario, 495 pairs total, 990 timing rows

## CPU results and variation

`summary.csv` reports CPU/wall medians and p95 batch averages. The changes below
are medians of within-trial paired percentages, not ratios of separate medians.
The range shows the three individual runs' paired CPU medians.

| Scenario | Paired CPU change | Three-run range |
|---|---:|---:|
| geometry-stationary | -99.423% | -99.575% to -99.404% |
| geometry-changing | -0.527% | -2.983% to +4.366% |
| mouse-stationary | -1.189% | -3.306% to -0.551% |
| mouse-changing | +1.809% | -2.356% to +5.587% |
| tablet-stationary | -84.961% | -85.387% to -84.120% |
| tablet-changing | -42.823% | -43.990% to -42.576% |
| tablet-midframe-changing | -1.625% | -5.539% to -0.358% |
| tablet-left-handed | -85.433% | -85.453% to -85.126% |
| tablet-handedness-midframe | -41.320% | -43.104% to -40.822% |
| tablet-after-only-changing | -1.594% | -2.569% to -1.091% |
| tablet-invalid-recovery | -33.490% | -34.911% to -32.174% |

Stationary tablet reuse saved a paired median 41.138536 microseconds per frame;
moving tablet reuse saved 21.080893 microseconds. The no-reuse and mouse controls
remain visible: small changes there do not establish a portable speedup.
Geometry-changing and moving-mouse controls changed sign across runs. These
results support component CPU gains when geometry can be reused, not faster
execution for every input or a universal latency guarantee.

Values are batch averages. p95 is a percentile of batch averages, not individual
input tail latency. Mock GL calls consume data without GPU work. Native-window
rendering, OS input/capture dispatch, compositing, displayed FPS, application
CPU utilization and end-to-end latency are not measured.

## Untimed correctness preflight

Every run first replays all requested fixture/block sequences through the timed
baseline and candidate functions plus separate counted copies. Three exact
comparisons per phase/call check geometry or ordered GL arguments/vertex bytes.
An independent fixture-state oracle checks geometry calls without reading cache
fields. Fixture warming is outside the reported workload counts and timing.

| Scenario | Baseline geometry calls | Candidate geometry calls |
|---|---:|---:|
| geometry-stationary | 2000 | 0 |
| geometry-changing | 2000 | 2000 |
| mouse-stationary | 2000 | 2000 |
| mouse-changing | 2000 | 2000 |
| tablet-stationary | 4000 | 0 |
| tablet-changing | 4000 | 2000 |
| tablet-midframe-changing | 4000 | 4000 |
| tablet-left-handed | 4000 | 0 |
| tablet-handedness-midframe | 4000 | 2000 |
| tablet-after-only-changing | 2000 | 2000 |
| tablet-invalid-recovery | 4000 | 2500 |

Per run: 38,000 phase/call cases, 114,000 exact comparisons and 516,768,000 checked
bytes. Invalid/recovery repeats invalid, invalid, recovery, reuse frames; failed
phases must draw nothing and must recompute. Both counters are reset before
clocks start and verified unchanged afterward. An additional 13-operation,
3-operation-block preflight passed, including a partial final block and cache
resets during the invalid/recovery cycle.

## Bounded memory and validation

Both renderers use the same current overlay layout for timing. The frozen
baseline layout reports 360 bytes versus 600 bytes for the candidate:
**+240 bytes per live overlay** on this ABI. State is one 216-byte geometry,
six float inputs and two flags, with ABI padding accounting for the total.
No allocation is added; the existing overlay allocation grows. Stack usage
changes and process RSS were not measured; no memory reduction is claimed.

Full strict Release build, source line policy and all seven CTests passed.
Current-source ASan/UBSan passed cache, draw and both benchmark preflights:
41,066 geometry comparisons and 8,378 draw-phase comparisons / 44,997,376 bytes.
Targeted phase/dimension/interleaved-lifecycle cases used 8 geometry evaluations
versus the baseline's 33; uninstrumented production output was also checked.
Leak checking is unavailable under this environment's tracing and was disabled.

Metadata and regular-file validation are real. Images and GL calls are stubbed;
creation/destruction ownership is source-inspection evidence, not a live-GL
resource-lifecycle test. The Cubism-defined syntax check is not a full Cubism
SDK link/runtime build. Windows/macOS validation remains untested. The two
previous baseline warning cleanups remain separate from this optimization.

## Reproduce

Build the optional benchmark as described in [MVER.md](../../MVER.md), then:

```sh
build/bongo_cat_mver_bench --preflight-only 13 1 3
for run in 1 2 3; do
  taskset -c 0 build/bongo_cat_mver_bench 2000 15 100 > run-$run.csv 2> run-$run.meta
done
python3 tests/performance/analyze_mver.py run-1.csv run-2.csv run-3.csv
```

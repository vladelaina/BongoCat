# Shortcut dispatch prefilter

For catalogs with at least 32 behaviors, dispatch checks the existing pure
press/release matchers once per binding before traversing behavior IDs. Unrelated
events skip that traversal. Smaller catalogs keep ID-first matching because
parsing stale bindings can cost more than a short ID scan. Empty catalogs return
immediately. This adds no cache, allocation, invalidation rule, or input parsing.
The 32-behavior threshold is empirical, not a guarantee for every string mix or
CPU; small cases can have measurable overhead or noise.

The binding-major/catalog-minor order, duplicate IDs, multiple actions, global
shortcut priority, repeated downs, momentary releases and Alt+1…0 fallback are
unchanged. The baseline is current upstream
`4283de1599c7da914138f82a99405d67c2c861ec`; this patch does not restore functionality
removed by upstream. In particular, expressions still select the requested index,
and matching motions still run in binding order.

## Reproduction

Use the normal dependencies documented in the root README. Cubism is optional;
these tests need no licensed SDK, display, audio device, or real input device.

```sh
cmake -S . -B build-perf -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBONGO_CAT_WARNINGS_AS_ERRORS=ON \
  -DBONGO_CAT_BUILD_SHORTCUT_BENCHMARK=ON
cmake --build build-perf --target bongo_cat_shortcut_dispatch_tests \
  bongo_cat_shortcut_benchmark --parallel 2
ctest --test-dir build-perf -R '^shortcut-dispatch$' --output-on-failure
for run in 1 2 3; do
  ./build-perf/bongo_cat_shortcut_benchmark "shortcut-samples-$run.csv" \
    > "shortcut-summary-$run.csv"
done
```

Both implementations are compiled and linked into the same executable with the
same flags. The reference is the unmodified full dispatch source from the stated
baseline, with only exported-symbol renames. Verify its provenance:

```sh
git show 4283de1599c7da914138f82a99405d67c2c861ec:src/runtime/shortcuts.c \
  > /tmp/shortcuts-upstream.c
tail -n +7 tests/runtime/shortcuts_reference.c > /tmp/shortcuts-oracle.c
cmp /tmp/shortcuts-upstream.c /tmp/shortcuts-oracle.c
```

## Method and scope

Each scenario calibrates a batch toward 8 ms of baseline wall time, bounded to
128–32768 events and rounded to an eight-event cycle. It performs three warmup
pairs and 31 measured pairs, alternating which implementation runs first.
Medians and nearest-rank p95s describe distributions of batch-average ns/event,
not individual input latency. Raw paired samples are written to the optional
output path. Wall time uses SDL's monotonic performance counter; process CPU time
uses `clock()` on POSIX and `GetProcessTimes` on Windows.

Keyboard cases alternate down/up; mixed cases have half their event pairs hit a
configured shortcut. Gamepad cases alternate press/release. The 20 scenarios
cover empty/tiny catalogs, 8/4 and 32/16 catalog/binding sizes, 128/8 and the current
128/128 capacity, label-only overrides, stale bindings, gamepad and non-key
inputs. Every fixture passes the real current configuration validator without
changing binding IDs, shortcuts, labels or count. Stale-binding cases use IDs
absent from the active catalog and long modifier sequences. Synthetic `F1`…`F128`
tokens are parser stress accepted by configuration validation, not a claim about
ordinary physical keyboard usage.

Audio, rendering and window operations are deterministic doubles. This dispatch
microbenchmark does not measure real capture, actual model/audio work,
input-to-frame latency, whole-app CPU, FPS or power. Linux cloud results are not
evidence of macOS or Windows timing. Bounded calibration can leave tiny batches
shorter than 8 ms; optimized batches may be much shorter than baseline batches.
Shared-host scheduling and frequency changes can distort small differences and
p95s even when other project work is paused.

## Correctness coverage

Differential tests compare the complete ordered external-action trace and all
app bytes after every event against the frozen upstream oracle. Explicit
expected traces additionally cover global precedence, repeated downs, ordered
multi-actions, duplicate/stale/empty IDs, empty sounds, failures, repeated
expression selection, momentary key/modifier releases, every Alt digit, gamepad
thresholds including NaN/infinity, and unsupported events. Explicit cases run
at tiny sizes and 31/32/33 behaviors. Deterministic random differential traces
include malformed shortcuts, both strategies and the current
128-behavior/128-binding limit.

## Separate baseline build cleanups

Two minimal cleanups let the full diagnostic build pass strict GCC 14 warnings
without changing runtime behavior. `src/runtime/live2d_audit.c` places the two
independent mouse-screen and mouse-hand-screen conditions on separate lines.
`src/runtime/model_import.c` compiles the `remove_receipt` definition only when
`BONGO_CAT_HAS_CUBISM` is defined, matching its existing sole caller's condition.
The function body and all call paths are unchanged. These cleanups are separate
from the shortcut optimization, and neither file is linked into the benchmark.

Real Windows/macOS input, native GUI smoke, actual audio/rendering and the
licensed Cubism backend are outside this microbenchmark's coverage.

## Linux cloud measurements (2026-10-05)

These fresh measurements compare this patch only with
`4283de1599c7da914138f82a99405d67c2c861ec`. Intel Xeon Platinum 8573C, Linux x86-64,
GCC 14.2.0, CMake 3.31.10 and Ninja 1.11.1.4 were used with pinned open-source
dependencies and the diagnostic backend. Both benchmark implementations use
Release `-O3 -DNDEBUG -std=c11`, followed by the project's effective `-Os`,
`-ffunction-sections -fdata-sections`, `-flto=auto -fno-fat-lto-objects`, and
`-Wall -Wextra -Werror`. The final strict benchmark binary is byte-identical to
the binary used for timing. Other project builds/tests/benchmarks were paused
for all three runs (14:46:12–14:46:44 UTC), but this is a shared host without
exclusive CPU access or affinity isolation.

The table shows medians of three run-level medians and p95s; speedup ranges retain
all three run-level median ratios. All 20 scenarios × 3 summaries and CPU timing
are in [shortcut-results-20261005.csv](shortcut-results-20261005.csv), with all
1,860 paired samples in
[shortcut-measurements-20261005.csv](shortcut-measurements-20261005.csv).

| Scenario | Baseline median µs/event | Candidate median µs/event | Baseline p95 µs/event | Candidate p95 µs/event | Median speedup range |
|---|---:|---:|---:|---:|---:|
| no-bindings | 0.660 | 0.648 | 0.790 | 0.988 | 0.95–1.03× |
| single-valid-miss | 0.220 | 0.220 | 0.327 | 0.298 | 0.94–1.02× |
| single-stale-binding | 0.159 | 0.153 | 0.227 | 0.228 | 1.01–1.08× |
| small-key-miss | 1.039 | 1.032 | 1.899 | 1.517 | 0.79–1.03× |
| small-mixed-keys | 0.844 | 0.858 | 1.325 | 1.166 | 0.94–0.98× |
| medium-key-miss | 3.908 | 1.795 | 5.534 | 2.609 | 2.07–2.24× |
| stale-long-32 | 9.842 | 8.506 | 14.379 | 11.761 | 0.97–1.20× |
| stale-long-64 | 18.385 | 8.237 | 24.512 | 12.826 | 2.10–2.23× |
| large-key-miss | 86.811 | 9.500 | 123.254 | 18.433 | 9.14–9.77× |
| large-mixed-keys | 87.920 | 10.766 | 128.614 | 15.309 | 7.97–8.17× |
| large-few-bindings | 5.570 | 1.375 | 7.436 | 1.814 | 4.05–4.15× |
| large-label-overrides | 75.382 | 1.877 | 108.564 | 4.612 | 37.91–42.90× |
| capacity-stale-long | 74.272 | 16.176 | 113.802 | 35.677 | 4.53–4.73× |

The 128-behavior/eight-binding mixed fixture reduced isolated dispatch time by
75.3–75.9% across repeats. This is not a whole-app or input-to-frame improvement.
The stale-long-32 boundary fixture ranged from 0.975× to 1.197×: two runs improved,
while the third was 2.6% slower. This reinforces that 32 is a conservative empirical
strategy choice rather than a universal crossover. Tiny cases also show slower
runs; neither tiny-case gains nor stable per-event tail latency are established.
No prior-base measurements are used in these results.

Verification: strict warnings-as-errors focused targets and 16,557 differential
ordered-action/app-state comparisons passed. The complete diagnostic build and
all 6 CTests passed with warnings-as-errors and no warning exemptions. Focused AddressSanitizer + UndefinedBehaviorSanitizer passed;
LeakSanitizer was disabled because this execution environment cannot run it under
ptrace. The existing line-policy check passed.

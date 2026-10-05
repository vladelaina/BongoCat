# Shortcut dispatch prefilter

For catalogs with at least 32 behaviors, dispatch uses the existing pure
press/release matchers as an outer guard before traversing behavior IDs. Unrelated
events skip that traversal. The original ID-first matching/action body is kept
verbatim; actual large-catalog hits intentionally recheck the pure matchers. Smaller catalogs keep ID-first matching because
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
uses `clock()` on POSIX and `GetProcessTimes` on Windows. A failed CPU timer
read exits with an explicit error instead of recording invalid statistics;
these checks are outside the timed dispatch loop.

Keyboard cases alternate down/up; mixed cases have half their event pairs hit a
configured shortcut. Gamepad cases alternate press/release. The 26 scenarios
cover empty/tiny catalogs, 8/4 and adjacent 31/16, 32/16, 33/16 catalog/binding
sizes, 128/8 and the current
128/128 capacity, label-only overrides, long-stale bindings at 31/32/33 behaviors,
Alt fallback at 31/32/33 behaviors, gamepad and non-key inputs. Alt fixtures hold
Alt and alternate Num1/Num0 press-release pairs; their Control-prefixed bindings
miss, so the real fallback dispatches motion and expression actions. Every
fixture passes the real current configuration validator without
changing binding IDs, shortcuts, labels or count. Before calibration and timing,
`benchmark_outputs_match` dispatches two complete eight-event cycles through the
frozen baseline and candidate with separate traces, comparing every app byte and
the ordered action trace after each event. Recording is then disabled and timing
fixtures are reset. Thus each run checks 416 untimed event pairs across all 26
fixtures, in addition to configuration validation. Stale-binding cases use IDs
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

`shortcut_live_edit_cases` adds 33 differential events in one uninterrupted
stream: 31→32→33→32→31 growth/shrink with a held modifier and primary key,
repeated downs, in-place binding/ID/action edits, left/right modifier changes,
release after modifier release, shrink-to-zero/regrowth and Alt fallback. It
never resets shortcut state at the strategy transitions. Explicit expected
traces and held-state assertions supplement the oracle comparisons.

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

## Why the action path stays unchanged

The earlier cached-result version at `bf4ecdc` kept press/release booleans live
across ID traversal and added ternaries to the action body. In the exact GCC/LTO
benchmark binary, the catalog index was loaded from and incremented in a stack
slot on every ID comparison. The guard-only version restores a register-held
catalog index and binding index. Its local stack reservation drops from 88 to
72 bytes (baseline: 56), and this dispatch routine shrinks from 588 to 499 bytes
(baseline: 423). These are compiler-specific component observations, not
application memory or cross-platform performance claims.

Unstripped diagnostic copies were checked to have identical `.text` to their
corresponding timed binaries. The baseline dispatch routine's machine code was
also byte-identical between both executables. Three contemporaneous cached/guard
run pairs alternated outer order while preserving inner baseline/candidate
pairing. The cached stale-31 speedup ratios were 0.943/0.904/0.884×, versus
1.094/1.687/1.100× for the guard. The 31-entry key-miss and fallback controls
also stopped showing a consistent regression. Together, code generation and
repeat controls support removing the avoidable
index spill; noisy timings alone do not prove the size of its causal effect.
No threshold or fixture was tuned. Diagnostic cached-version measurements remain
separate from the final guard-only dataset below.

## Linux cloud measurements (2026-10-05)

These measurements compare the final guard-only patch with
`4283de1599c7da914138f82a99405d67c2c861ec`. Intel Xeon Platinum 8573C, Linux x86-64,
GCC 14.2.0, CMake 3.31.10 and Ninja 1.11.1.4 were used with pinned open-source
dependencies and the diagnostic backend. Both implementations use Release
`-O3 -DNDEBUG -std=c11`, followed by the project's effective `-Os`,
`-ffunction-sections -fdata-sections`, `-flto=auto -fno-fat-lto-objects`, and
`-Wall -Wextra -Werror`. The final strict benchmark binary is byte-identical to
the guard binary used for timing. Other project builds/tests/benchmarks were
paused for all six diagnostic/final runs (16:01:32–16:02:59 UTC). This is still a
shared host without exclusive CPU access or affinity isolation. Each guard run
passed all 26 fixture parity checks (416 event pairs) outside timing.

The table shows medians of three guard run-level medians and p95s; speedup ranges
retain all three run-level median ratios. All 26 scenarios × 3 guard summaries
and CPU timing are in [shortcut-results-20261005.csv](shortcut-results-20261005.csv),
with all 2,418 final paired samples in
[shortcut-measurements-20261005.csv](shortcut-measurements-20261005.csv).

| Scenario | Baseline median µs/event | Candidate median µs/event | Baseline p95 µs/event | Candidate p95 µs/event | Median speedup range |
|---|---:|---:|---:|---:|---:|
| empty-catalog | 0.124 | 0.069 | 0.155 | 0.103 | 1.72–1.90× |
| no-bindings | 0.685 | 0.678 | 0.987 | 0.872 | 0.92–1.02× |
| single-valid-miss | 0.222 | 0.224 | 0.301 | 0.279 | 0.97–1.04× |
| single-stale-binding | 0.158 | 0.161 | 0.262 | 0.282 | 0.96–1.01× |
| small-key-miss | 1.097 | 1.110 | 1.856 | 1.457 | 0.95–1.10× |
| small-mixed-keys | 0.887 | 0.883 | 1.403 | 1.364 | 1.00–1.08× |
| lower-boundary-key-miss | 4.040 | 3.816 | 7.538 | 5.514 | 1.02–1.06× |
| medium-key-miss | 4.078 | 1.745 | 5.602 | 3.595 | 2.29–2.40× |
| upper-boundary-key-miss | 4.261 | 1.807 | 7.374 | 3.671 | 2.34–2.46× |
| stale-long-16 | 6.138 | 5.740 | 10.577 | 8.755 | 1.07–1.10× |
| stale-long-31 | 10.038 | 9.128 | 15.950 | 14.599 | 1.09–1.69× |
| stale-long-32 | 9.971 | 8.314 | 14.339 | 18.001 | 1.18–1.27× |
| stale-long-33 | 11.030 | 8.932 | 24.541 | 17.814 | 1.24–1.31× |
| stale-long-64 | 18.825 | 9.069 | 31.167 | 13.417 | 2.08–2.31× |
| large-key-miss | 87.684 | 9.364 | 149.054 | 15.247 | 9.30–10.52× |
| large-mixed-keys | 118.996 | 15.240 | 171.398 | 28.079 | 7.81–12.06× |
| large-few-bindings | 5.820 | 1.476 | 9.798 | 3.862 | 3.87–7.04× |
| large-label-overrides | 89.555 | 1.793 | 131.787 | 10.220 | 41.32–85.86× |
| capacity-stale-long | 70.105 | 15.153 | 100.331 | 31.511 | 4.63–5.81× |
| small-stale-long | 0.518 | 0.555 | 0.801 | 0.731 | 0.93–0.99× |
| gamepad-miss | 76.182 | 1.377 | 95.016 | 2.607 | 49.89–154.87× |
| gamepad-mixed | 79.569 | 1.703 | 110.334 | 3.475 | 43.53–116.27× |
| non-key-event | 77.267 | 0.890 | 122.582 | 1.950 | 81.42–229.33× |
| alt-fallback-31 | 3.894 | 3.636 | 8.324 | 9.882 | 1.01–1.19× |
| alt-fallback-32 | 3.986 | 1.456 | 6.803 | 2.851 | 2.48–7.27× |
| alt-fallback-33 | 3.760 | 1.477 | 6.067 | 2.169 | 2.49–2.61× |

The 31-entry long-stale, key-miss and fallback controls no longer show the
consistent regression of the cached-result version in these repeats. Large
benefits remain: the 128/8 mixed fixture improves 3.872–7.042× and 128/128 key
misses improve 9.298–10.519×. Run 2 has substantial shared-host variation, so the
high end of these ranges should not be treated as stable device performance.

There is still no universal improvement: the 1-behavior/64-stale-binding stress
case is 0.933–0.991×, about 0.9–7.2% slower, and other tiny/no-binding controls
include slower runs. The unchanged 32-entry threshold is an empirical tradeoff.
These are component results, not whole-app or individual-event latency gains.
No cached-version samples are mixed into the final data files.

Verification: strict Release full build and all 6 CTests passed, including
16,590 ordered-action/app-state comparisons. Current-source AddressSanitizer +
UndefinedBehaviorSanitizer passed with zero failures; LeakSanitizer was disabled
because this execution environment cannot run it under ptrace. The source line
policy passed. The separate build cleanups were checked with and without
`BONGO_CAT_HAS_CUBISM` as described above. A POSIX `clock()` failure-injection
check exited with code 2 and an explicit error, emitting no measurement rows.

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

## Linux cloud measurements (2026-10-05)

These fresh measurements compare this patch only with
`4283de1599c7da914138f82a99405d67c2c861ec`. Intel Xeon Platinum 8573C, Linux x86-64,
GCC 14.2.0, CMake 3.31.10 and Ninja 1.11.1.4 were used with pinned open-source
dependencies and the diagnostic backend. Both benchmark implementations use
Release `-O3 -DNDEBUG -std=c11`, followed by the project's effective `-Os`,
`-ffunction-sections -fdata-sections`, `-flto=auto -fno-fat-lto-objects`, and
`-Wall -Wextra -Werror`. The final strict benchmark binary is byte-identical to
the binary used for timing. Other project builds/tests/benchmarks were paused
for all three runs (15:44:15–15:44:52 UTC), but this is a shared host without
exclusive CPU access or affinity isolation. Each run first passed all 26
fixture parity checks (416 event pairs); these checks are outside timing.

The table shows medians of three run-level medians and p95s; speedup ranges retain
all three run-level median ratios. All 26 scenarios × 3 summaries and CPU timing
are in [shortcut-results-20261005.csv](shortcut-results-20261005.csv), with all
2,418 paired samples in
[shortcut-measurements-20261005.csv](shortcut-measurements-20261005.csv).

| Scenario | Baseline median µs/event | Candidate median µs/event | Baseline p95 µs/event | Candidate p95 µs/event | Median speedup range |
|---|---:|---:|---:|---:|---:|
| empty-catalog | 0.128 | 0.072 | 0.198 | 0.122 | 1.71–1.97× |
| no-bindings | 0.697 | 0.717 | 1.004 | 0.972 | 0.97–1.04× |
| single-valid-miss | 0.227 | 0.225 | 0.325 | 0.285 | 0.99–1.02× |
| single-stale-binding | 0.164 | 0.164 | 0.231 | 0.233 | 0.98–1.01× |
| small-key-miss | 1.042 | 1.052 | 1.377 | 1.446 | 0.91–1.01× |
| small-mixed-keys | 0.933 | 0.852 | 1.138 | 1.176 | 0.95–2.11× |
| lower-boundary-key-miss | 3.813 | 3.973 | 5.798 | 8.946 | 0.94–0.96× |
| medium-key-miss | 4.031 | 1.710 | 5.835 | 2.947 | 2.26–2.38× |
| upper-boundary-key-miss | 4.126 | 1.670 | 5.330 | 2.412 | 2.40–2.47× |
| stale-long-16 | 5.179 | 5.696 | 6.364 | 7.421 | 0.84–0.91× |
| stale-long-31 | 9.069 | 10.093 | 12.779 | 12.267 | 0.87–0.91× |
| stale-long-32 | 9.566 | 7.982 | 14.910 | 11.978 | 1.19–1.23× |
| stale-long-33 | 9.757 | 7.931 | 12.645 | 10.442 | 1.14–1.26× |
| stale-long-64 | 18.452 | 8.182 | 31.096 | 13.463 | 2.25–2.41× |
| large-key-miss | 82.794 | 8.560 | 96.237 | 11.329 | 9.36–9.67× |
| large-mixed-keys | 87.171 | 10.696 | 114.093 | 14.122 | 7.86–8.50× |
| large-few-bindings | 5.584 | 1.457 | 6.946 | 1.862 | 3.77–3.97× |
| large-label-overrides | 72.285 | 1.823 | 91.262 | 3.252 | 33.90–39.65× |
| capacity-stale-long | 67.840 | 15.496 | 92.857 | 31.167 | 4.38–4.64× |
| small-stale-long | 0.509 | 0.681 | 0.614 | 0.961 | 0.75–0.77× |
| gamepad-miss | 75.206 | 1.407 | 112.919 | 3.005 | 52.41–56.73× |
| gamepad-mixed | 78.622 | 1.908 | 117.572 | 4.548 | 36.10–43.95× |
| non-key-event | 72.968 | 0.923 | 127.128 | 1.471 | 77.41–79.32× |
| alt-fallback-31 | 3.571 | 3.901 | 4.899 | 6.240 | 0.89–1.01× |
| alt-fallback-32 | 3.742 | 1.524 | 5.108 | 2.372 | 2.46–2.61× |
| alt-fallback-33 | 4.039 | 1.598 | 5.994 | 2.631 | 2.50–2.84× |

The 128-behavior/eight-binding mixed fixture improved 3.770–3.971× across repeats.
At 32/33 behaviors, the adjacent key-miss, long-stale and fallback controls
improved in all three runs. This does not make the optimization free: the
31-entry long-stale control was consistently 10.0–15.5% slower, and the 31-entry
key-miss/fallback controls also include slower runs. Tiny workloads retain
regressions/noise. The 32-entry threshold remains an empirical strategy choice,
not a universal crossover or a device-level speed guarantee. These component
results establish neither whole-app improvements nor stable per-event tail
latency. No earlier-harness or prior-base samples are used here.

Verification: the final strict Release build and all 6 CTests passed, including
16,590 ordered-action/app-state comparisons. Focused AddressSanitizer +
UndefinedBehaviorSanitizer passed with zero failures. LeakSanitizer was disabled
because this execution environment cannot run it under ptrace. The existing
line-policy check passed, and `model_import.c` passed a strict syntax check with
`BONGO_CAT_HAS_CUBISM` defined as well as the diagnostic build without it.
A POSIX failure-injection check made `clock()` return `(clock_t)-1`; the final
benchmark exited with code 2 and an explicit error, emitting no measurement rows.

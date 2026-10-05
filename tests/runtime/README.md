# Configuration-store eligibility regression and microbenchmark

The production change moves each update-time hash into its existing eligibility
branch. No hash fields, debounce/retry deadlines, validation, serialization,
load behavior, or flush behavior change. In particular, flush still updates
both observed hashes even when the corresponding store cannot be saved.

`config_store_reference.c` is a frozen copy of the production implementation at
`666999650f8afb405fa34dbf1ee0b98cd7145117`, with only its public symbols renamed
by the test target. Keep this reference independent from later optimizations.
The old and current implementations are compiled separately, using the same
compiler, Release optimization, and native IPO setting.

## Run

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBONGO_CAT_WARNINGS_AS_ERRORS=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
cmake --build build --target bongo_cat_config_store_bench --parallel 2
./build/bongo_cat_config_store_bench samples.csv > summary.csv
```

The benchmark is explicitly built, not part of the normal test run. No SDL
window or display is needed for either executable. Run from a disposable working
directory without `config-store-test-settings.json` or
`config-store-test-session.json`; the correctness test writes and removes these
files through the real atomic JSON serializers. CTest uses its own dedicated
working directory.

## Correctness coverage

The deterministic differential test compares settings/session bytes, observed
and saved hashes, deadlines, validity/blocking flags, and each ordered save
attempt's timestamp, outcome, and exact emitted JSON bytes. The clock is an
explicit argument; there are no sleeps or timing-tolerance assertions.

- Default and capacity-filled configuration, all eight combinations of secondary
  pet / settings blocked / session blocked
- First-store persistence, unchanged polling, changes, and reverts before save
- Exactly 300 ms debounce and 1 s retries, including one nanosecond before each
- Independent save failures, mutation by validation on failure, pending retries
  across blocking, and restored eligibility
- Flush success/failure, its observed-hash behavior for ineligible stores, and
  requeueing after failed flush
- Null app, smoke mode, either missing path, and unsigned clock wrap
- Two seeded 512-step mixed sequences, including capacity-overflow counts,
  binding/session edits, eligibility transitions, and flush/failure combinations

Save interception injects deterministic errors and records attempts. Successful
saves use the real core validation, JSON serializer, and file writer. System
language detection alone is stubbed to keep first-run state deterministic.

## Measurement method and scope

The benchmark measures steady-state `bongo_cat_config_store_update` wall elapsed
time (SDL monotonic timer) and process CPU time (`clock()` on POSIX, `GetProcessTimes` on Windows) separately. It starts with clean saved state and aborts on any unexpected save. Both
implementations receive the same fixture, eligibility flags, and clock value.
The saturated fixture contains 256 behavior bindings, 128 model labels,
128 removed models, 7 additional models, and 256 active behaviors.

Each variant has untimed calibration/warmup, followed by 31 samples, alternating
baseline/optimized execution order. Each sample is a batch calibrated to at
least 30 ms; fast and slow variants use independent iteration counts so the
no-hash path remains measurable. Reported median and nearest-rank p95 describe
batch-average nanoseconds per call, not individual-call tail latency. An optional
second argument overrides the calibration target in milliseconds. Functions
remain reachable through function pointers and resulting state is consumed;
no synthetic replacement hash or volatile full-structure scan is timed.

This does not measure input-to-render latency, FPS, disk-save latency, CPU usage
of a complete app, or Live2D rendering. The production change introduces no
allocation, cache, new state, or change to structure sizes. Do not convert these numbers into those
claims. Compiler optimization can already eliminate some unused baseline
hashing, so secondary-pet improvements must be demonstrated rather than assumed.

## Linux cloud measurements, 2026-10-05

Environment: Intel Xeon Platinum 8573C, GCC 14.2.0, CMake 3.31.10,
Release `-O3 -DNDEBUG -Os` (last optimization flag wins), native IPO/LTO enabled.
Both versions use exactly the same flags and linked libraries. `taskset -c 0`
succeeded; this is CPU affinity on a shared cloud host, not dedicated hardware.

Three fresh-process runs each used 31 alternating paired samples per scenario
and a 30 ms calibration target. The tables pool 93 batch averages per variant;
all 1,860 raw samples are in `config_store_measurements.csv`. They measure wall
elapsed time and Linux process CPU time separately. p95 is the nearest-rank
p95 of batch averages, never individual input, frame, or save latency.

Only session-blocked savings are supported consistently by these measurements.
The compiler already moves baseline settings hashing behind its eligibility
checks. The primary and secondary controls have no established improvement;
several controls are slower in individual runs. Broad tails and round-to-round
variation prevent a precise small-regression or small-gain claim. The primary
default exploratory 10 ms run showed an 8.10% slowdown; the three longer repeats
were +2.64%, -0.67%, and +2.76% reductions, so that slowdown did not reproduce.

In the saturated session-blocked case the pooled wall median falls from
616.932 to 390.059 microseconds per update (36.77%), with 33.57–38.81% reductions
across the three runs. When both stores are blocked, the saturated median falls
from 233.395 microseconds to 4.478 nanoseconds, leaving only guard/call overhead.
These are unusual blocked-store paths, not evidence of faster normal rendering.

All entries below are nanoseconds per update, baseline → optimized.

| Fixture / mode | Wall median | Wall p95 | Process CPU median | Process CPU p95 |
| --- | ---: | ---: | ---: | ---: |
| defaults / primary | 8913.912 → 8853.679 | 13300.299 → 13947.401 | 8914.551 → 8851.318 | 13297.363 → 13948.242 |
| defaults / secondary | 327.120 → 338.289 | 437.215 → 444.418 | 327.164 → 338.303 | 437.256 → 444.443 |
| defaults / settings-blocked | 329.431 → 336.846 | 447.670 → 441.014 | 329.330 → 336.884 | 447.601 → 441.048 |
| defaults / session-blocked | 8978.440 → 8552.805 | 12423.929 → 13086.853 | 8975.830 → 8553.467 | 12424.561 → 13087.646 |
| defaults / both-blocked | 344.644 → 4.460 | 555.538 → 7.278 | 344.666 → 4.460 | 555.565 → 7.279 |
| saturated / primary | 638310.438 → 635797.297 | 1071957.656 → 1069403.984 | 638359.375 → 635828.125 | 1072031.250 → 1075796.875 |
| saturated / secondary | 235052.844 → 239811.559 | 423165.395 → 377093.062 | 235078.125 → 239820.312 | 423187.500 → 377117.188 |
| saturated / settings-blocked | 238874.562 → 234269.000 | 280633.023 → 315140.438 | 238898.438 → 234273.438 | 280656.250 → 315078.125 |
| saturated / session-blocked | 616932.000 → 390059.133 | 1046382.672 → 783879.922 | 616968.750 → 390085.938 | 1046421.875 → 783914.062 |
| saturated / both-blocked | 233395.492 → 4.478 | 359344.312 → 5.676 | 233406.250 → 4.478 | 359390.625 → 5.656 |

Per-run wall-median reduction percentages (negative means slower):

| Fixture / mode | Run 1 | Run 2 | Run 3 |
| --- | ---: | ---: | ---: |
| defaults / primary | +2.64% | -0.67% | +2.76% |
| defaults / secondary | -1.79% | +2.55% | -8.93% |
| defaults / settings-blocked | -0.97% | -5.56% | -3.17% |
| defaults / session-blocked | +2.66% | +6.65% | +1.48% |
| defaults / both-blocked | +98.72% | +98.73% | +98.58% |
| saturated / primary | +2.58% | +2.72% | -8.35% |
| saturated / secondary | -2.25% | -1.58% | -8.94% |
| saturated / settings-blocked | +2.69% | +6.43% | -3.76% |
| saturated / session-blocked | +34.53% | +38.81% | +33.57% |
| saturated / both-blocked | +100.00% | +100.00% | +100.00% |

Validation on the same cloud toolchain:

- Full diagnostic-backend Release build with warnings as errors passed
- All six CTests passed; config-store exercised 1,425 differential steps,
  260 save attempts, and 3,196,535 exact JSON bytes with zero differences
- Line, localization, platform-runtime, and Cubism safety policies passed
- Focused AddressSanitizer/UndefinedBehaviorSanitizer run passed for the store,
  frozen reference, test, and support code; linked core/SDL archives were not
  instrumented. LeakSanitizer is unavailable under executor ptrace, so leak
  detection was disabled explicitly
- Frozen source verified byte-for-byte against the baseline; production flush
  verified byte-for-byte unchanged
- IPO disassembly confirmed an indirect update call on every timed iteration,
  and baseline/current hash helpers share the same function addresses
- A source variant moving only the session hash produced identical optimized
  update assembly to moving both declarations; the two-move version is retained

Cubism SDK rendering and Windows/macOS execution were not tested.

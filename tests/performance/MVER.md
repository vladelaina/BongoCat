# Exact-key Mver geometry cache

The optimization memoizes the last successful geometry for tablet-mode pointer
overlays. Mouse-mode drawing retains the original computation path; there is
no second mouse draw phase to reuse. The tablet cache key contains the bit
representations of both pointer ratios and all four geometry offsets, plus
handedness. Each tablet draw phase checks the key independently. A failed
computation is never cached; loading metadata or releasing textures discards
the cache. The original geometry calculation and all renderer arithmetic are
unchanged. Buttons, device selection, scale, reference size, color, GL state,
vertex generation and draw order continue to be evaluated on every draw.

## Correctness

With the project's pinned dependencies available:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DBONGO_CAT_WARNINGS_AS_ERRORS=ON -DBONGO_CAT_BUILD_MVER_BENCHMARK=ON
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

Two small baseline warning cleanups are separate from the geometry optimization:
`src/runtime/live2d_audit.c` puts two independent pointer-audit conditions on
separate lines, and `src/runtime/model_import.c` compiles the unchanged
`remove_receipt` helper only with Cubism, matching its existing sole caller.
Neither changes a call path or behavior. The full Release build uses strict
warnings-as-errors without warning downgrades. Neither file is linked into the
component benchmark.

`mver-geometry-cache` compares complete geometry object bytes with the original
function. It covers a dense grid, out-of-range inputs, both hands, all key
fields, signed zeros, subnormals, maximum finite values, infinities, multiple
NaN payloads, deterministic arbitrary float bit patterns, and repeated failed
computations. The test counts underlying computations to verify hits/misses.

`mver-draw-trace` compares every GL call argument in order and every uploaded
vertex byte against the renderer frozen from upstream commit
`4283de1599c7da914138f82a99405d67c2c861ec`. Its body is unchanged; the preamble
only remaps test symbols. Cases include both hands, mouse/tablet modes, every
button combination, absent textures, color/scale/reference changes, disabled
rendering, load/reload/disabled/failure, and anchor/offset changes between phases.
Targeted phase tests also cover cold/repeated after-only and before-only calls,
handedness changes between phases, and selected texture width/height changes
on cache hits. Separate correctness-only copies count geometry evaluations;
these copies and the ordinary production entry points both match the frozen
renderer. Interleaved lifecycle tests keep a second distinct overlay live while
the first loads, reloads, disables and fails loading, checking the untouched
instance's cache key, geometry bytes and continued cache hits.
Metadata parsing and regular-file validation are real; image decoding and GL
resource operations use deterministic stubs. Successful GL-backed creation and
destruction ownership have source-inspection evidence only.

These are strict CPU-output equivalence checks. Identical GL command streams
and payloads support unchanged rendering under identical external GPU state,
but are not a native-window screenshot test. Cubism, OS capture/input dispatch,
GPU work, compositing and displayed end-to-end latency are not measured here.

## Reproducible component benchmark (UNIX only)

```sh
cmake --build build --parallel 2 --target bongo_cat_mver_bench
# On Linux, choose a CPU allowed by the current affinity mask.
taskset -c 0 build/bongo_cat_mver_bench 2000 21 > mver.csv
python3 tests/performance/analyze_mver.py mver.csv
```

The optional benchmark target is available on UNIX platforms only. The
correctness tests are not restricted to UNIX.

The benchmark links the frozen original renderer and cache-enabled renderer in
one executable with the same compiler and Release optimization/IPO settings.
Both use the current private overlay layout to isolate computation cost; the
original layout is retained separately to report exact per-instance growth.
The GL sink is intentionally compiled without IPO, so the renderers cannot
remove opaque callback calls or vertex generation. In timing mode it avoids
copying full traces but consumes arguments and vertex data. Geometry-only runs
also pass the complete output to an opaque non-IPO sink to prevent IPO from
removing unused arm-coordinate computation or cache-copy work.

Before starting any clocks, all eleven workloads are replayed at the requested
operation count and block boundaries. The original timed baseline/candidate
paths and separate counted copies must have exactly matching geometry or
ordered GL traces. An independent fixture-state oracle checks expected geometry
calls phase by phase; per-scenario counts and checked byte totals are printed
to stderr. The preflight does not read production cache state. Counters are
reset before timing and must remain zero afterward; the timed renderer subjects
contain no counting instrumentation. To run only these checks:

```sh
build/bongo_cat_mver_bench --preflight-only 2000 15 100
```

There are three warm-up rounds and 21 measured A/B pairs by default. Each trial
interleaves 100-operation blocks in alternating AB/BA order to
reduce drift on shared hosts. The first variant also alternates between rounds.
Each row sums 2,000 geometry calls or complete two-phase CPU draw preparations
using wall and process CPU clocks. The optional third argument changes the
block size (use the full count to reproduce the exploratory long batches).
Statistics report microseconds per operation averaged over each batch;
p95 is the 95th percentile of batch averages, **not** single-input tail latency.

Scenarios separate stationary/repeated inputs from every-frame changes. A
moving tablet can reuse geometry between its two phases; the mid-frame-change
scenario changes the anchor again and must miss in both phases. Additional
controls cover left-handed stationary reuse, handedness toggles between phases,
after-only changing inputs, and a four-frame invalid/invalid/recovery/reuse cycle.
The invalid phases must draw nothing and recompute on every failed attempt.
Both mouse
drawing paths remain uncached controls, and the mouse second phase
remains the original no-op. No additional heap allocation is
introduced; retained memory is one geometry plus its key and validity flags in
the existing overlay allocation. Exact structure sizes are printed to stderr.
Stack usage changes have not been measured. Process RSS has not been measured,
and the inline cache growth is not a memory-reduction claim.

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
cmake --build build --parallel 2 --target \
  bongo_cat_mver_cache_tests bongo_cat_mver_draw_tests
ctest --test-dir build --output-on-failure -R '^mver-'
```

At this upstream base, the full GCC 14 `-Werror` build also encounters unrelated
existing warnings in `src/runtime/live2d_audit.c:121` (misleading indentation)
and `src/runtime/model_import.c:207` (unused function). These files are unchanged.
For a diagnostic full build, retain all other warnings as errors and allow those
two categories explicitly:

```sh
cmake -S . -B build \
  -DCMAKE_C_FLAGS="-Wno-error=misleading-indentation -Wno-error=unused-function"
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

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
Metadata parsing and regular-file validation are real; image decoding and GL
resource operations use deterministic stubs.

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
scenario changes the anchor again and must miss in both phases. Both mouse
drawing paths remain uncached controls, and the mouse second phase
remains the original no-op. No additional heap allocation is
introduced; retained memory is one geometry plus its key and validity flags in
the existing overlay allocation. Exact structure sizes are printed to stderr.

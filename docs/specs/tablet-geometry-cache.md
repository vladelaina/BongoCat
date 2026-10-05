# Tablet geometry cache specification

## Status and scope

Baseline: upstream `4283de1599c7da914138f82a99405d67c2c861ec`.
Initial requirements frozen on 2026-10-05 before the spec reviewer inspected
this candidate implementation or its added tests. Implementation existed before
the user's new spec-driven requirement; this is an explicit reconciliation,
not a claim that the earlier implementation was written from this document.
The requirements below derive from baseline `src/core/mver_pointer.c`,
`src/render/mver_pointer_overlay{,_draw,_internal}.c/.h`, and the user's
preservation/maintainability goals. No repository-specific SDD framework,
AGENTS.md, or relevant local skill was found; use plain Markdown.

Goal: reuse an exact successful tablet geometry result on the owning overlay
instance when all geometry inputs are unchanged. Preserve application behavior,
geometry mathematics, drawing, model lifecycle, and the existing mouse path.
No approximate geometry, shared global cache, new rendering policy, batching,
or replacement optimization is in scope.

## Requirements and acceptance

- **TG-01: Exact geometry and key.** Keep the geometry generator unchanged.
  Reuse only a successful result whose effective horizontal ratio
  (including handedness), vertical ratio and four geometry offsets match
  current inputs. Never use approximate float comparison. Any input that
  could change generated geometry must force recomputation; edge ratios,
  clamping, signed zero, infinities and NaNs must retain baseline observable
  success/failure and drawing semantics. Key comparison may conservatively
  miss when equivalence has not been proved.
- **TG-02: Phase revalidation.** Each before-keys and after-keys call checks
  current inputs independently. Support after-only, before-only, repeated
  calls and input/config/handedness changes between phases. Do not assume
  the two calls are paired or that inputs are constant during a frame.
- **TG-03: Failure behavior.** Failed geometry generation draws nothing for
  that phase. A prior successful result must not be drawn for failing
  inputs. Do not cache a failure as reusable success. Repeated invalid
  inputs and recovery back to valid inputs preserve the baseline outcomes.
- **TG-04: Draw parity and non-geometric state.** Preserve primitive order,
  texture choice/binding, vertex payload, colors, texture dimensions,
  scale/reference dimensions and button state. Changes to style, textures,
  buttons and projection must affect drawing immediately even when geometry
  is reused. Disabled/null calls remain no-ops. Mouse geometry and drawing
  retain their existing before-phase path and after-phase no-op.
- **TG-05: Per-instance lifecycle.** Cache ownership is per overlay instance,
  with bounded inline state and no added heap allocation. Creation, texture
  clearing, successful reload, disabled metadata, failed reload and
  destruction must not retain usable stale results. Interleave two distinct
  overlay instances with different inputs; prove they do not contaminate
  each other, and reloading/clearing one neither invalidates nor alters the
  other's valid cache or draw results.
- **TG-06: Differential evidence.** Drive actual production draw functions
  and the frozen baseline with deterministic stubs. Compare ordered draw
  calls and vertex payloads exactly, not screenshots by visual judgment or
  approximate coordinates. Count geometry evaluations to show both reuse
  and expected misses. Cover all requirements above with targeted and
  varied cases. Separately test real lifecycle entry points; direct struct
  zeroing does not establish real reload invalidation.
- **TG-07: Honest component measurement.** Benchmark baseline/candidate
  production geometry/draw preparation using identical fixtures and
  compiler/flags, repeated runs and reported variation. Include stationary
  tablet reuse, moving tablet before/after pairs, handedness, after-only or
  one-phase misses, invalid/recovery inputs, and the unchanged mouse control.
  Report geometry calls, outputs checked outside timing, workload and
  environment. Miss/no-benefit/negative controls must be visible. Mock GL
  CPU timing is not GPU latency, end-to-end input latency, rendered FPS or
  application CPU utilization. Do not impose an invented universal speedup.
- **TG-08: Bounded memory cost.** Report exact overlay object size before and
  after, target/compiler ABI, the number/shape of cached results, allocation
  count changes, and whether measured stack usage changes. Added memory
  must be bounded per live instance and have no input-dependent growth.
  Do not label inline cache growth as a memory reduction or make an RSS
  claim without a controlled process measurement.
- **TG-09: Build hygiene.** A strict native build with
  `BONGO_CAT_WARNINGS_AS_ERRORS=ON`, applicable tests, and repository source
  line policy must pass. The only unrelated baseline warning repairs
  permitted here are separating the two audit-scenario `if` statements
  without changing order/returns, and defining `remove_receipt` only under
  the same `BONGO_CAT_HAS_CUBISM` condition as its existing call. Validate
  touched conditional source with the macro absent and defined. A strict
  diagnostic-backend build and Cubism-defined syntax check must not be
  described as a full Cubism SDK/link/runtime or multi-platform validation.

## Planned verification before implementation reconciliation

1. Inspect the candidate after freezing the requirements; record a readable
   requirement-to-code/test/evidence mapping and concrete missing evidence.
2. Close justified gaps with minimal changes; do not change the original
   application behavior to make a test pass.
3. Rerun strict builds, parity/lifecycle tests and controlled timing/memory
   measurements on the final code, then independently review the diff.
4. Publish only after functional gaps are resolved and remaining environment
   or measurement limits are stated explicitly.

## Reconciliation and evidence

### Initial review, before gap-closing changes

The implementation adds a small dedicated geometry helper, inline state,
cache invalidation in existing texture cleanup, and tablet-only call sites.
No geometry formula changes or production defect were found in the initial
review. The raw-ratio-plus-handedness key is conservative: it can miss for
inputs whose effective horizontal ratio is equivalent, which TG-01 permits.

| Requirement | Code and existing evidence | Initial assessment / gap |
|---|---|---|
| TG-01 | `mver_pointer_overlay_geometry.c`; generator unchanged; `dense_grid`, `boundaries`, `key_and_phase_changes`, `unusual_bit_patterns` | Full six-float object-representation key and handedness; initial 41,066 byte-exact comparisons reported, including signed zero/NaN payloads and deterministic varied bits |
| TG-02 | Both production draw phases call helper; `changed_state` alters anchor and all offsets between phases | **Open G-TG1:** explicitly test cold after-only/repeated-after calls and handedness mutation between phases |
| TG-03 | Clear valid flag before recomputation and set only on success; special values and repeated failures compare generator outputs/draw traces | Existing failure/recovery coverage; extend benchmark negative controls in G-TG4 |
| TG-04 | Draw arithmetic unchanged; ordered GL probe records arguments and vertices; grid covers hands/modes/buttons and `changed_state` covers colors, scale, reference dimensions, missing texture IDs | **Open G-TG2:** mutate texture width/height while geometry remains reusable; exact mouse/disabled/null draw traces already covered |
| TG-05 | Cache fields belong to overlay; create uses zeroing allocation; real `clear_textures` invalidates before load/error/disabled handling | **Open G-TG3:** interleave distinct instances and reload one while checking the other's cache/draws stay intact; successful creation/destruction GL-resource paths have source review only, no live-GL test |
| TG-06 | Geometry test counts computations; production draw/lifecycle sources are compiled through symbol-remapping wrappers; metadata/files are real and image/GL services are stubs | Baseline renderer body verified byte-for-byte after ten-line remap preamble; initial 8,308 phase/44,765,568 byte comparisons reported; extend per G-TG1..3 and assert draw-level hits/misses |
| TG-07 | `bench_mver.c`, same-binary baseline/candidate, opaque non-IPO payload sink, AB/BA blocks, CPU/wall clocks; stationary/moving/midframe tablet plus mouse controls | **Open G-TG4:** add handedness, after-only miss, invalid/recovery controls, per-fixture untimed parity and geometry-call checks |
| TG-08 | One 216-byte geometry, six 4-byte floats and two flags within existing overlay allocation; reference layout separately retained | Measured Linux x86-64 GCC size 360→600 bytes (**+240 bytes/instance**), no added heap allocations; **G-TG5:** explicitly mark stack usage unmeasured unless measured |
| TG-09 | Separate exact baseline warning cleanups; prior strict full build and 7/7 CTests passed; shared warning source matches branch #101 exactly | Rerun final-source tests; macro-defined source syntax check is not a full licensed Cubism link/runtime build; native GPU/window and Windows/macOS validation remain untested |

Initial evidence and reproduction are in
[`MVER.md`](../../tests/performance/MVER.md) and
[the current-base measurement directory](../../tests/performance/results/mver-20261005-main-4283de1/README.md).
These are earlier candidate results, not pending gap-closing measurements.
The recorded +2.269% geometry-only miss cost, +0.138% midframe tablet cost,
and changing mouse +1.296% control remain visible alongside reuse gains.
ASan/UBSan had passed focused tests; LeakSanitizer was disabled due to the
execution environment and no leak-check result is claimed.

### Final verification

Pending gap-closing tests, final strict checks and updated measurements.
The initial requirements above remain the acceptance basis.

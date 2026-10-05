# Shortcut prefilter specification

## Status and scope

Baseline: upstream `4283de1599c7da914138f82a99405d67c2c861ec`.
The candidate implementation already existed when this contract was written.
The requirements were derived from baseline `src/runtime/shortcuts.c` and
`src/core/shortcut.c`, together with the behavior-preservation and
maintainability goals, and frozen on 2026-10-05 before the reconciliation
changes. Candidate code and evidence were then checked against that contract.
This does not imply the earlier implementation was written from this document.
Plain Markdown follows the project's existing documentation style.

Goal: avoid behavior-ID scans for inputs that cannot match a configured
shortcut, while retaining the baseline's exact observable behavior. Do not
change the shortcut grammar, feature policy, or input-state model. PR #103's
configuration-store work is separate and is not part of this optimization.

## Requirements and acceptance

- **SC-01: Pure, local optimization.** Reorder only side-effect-free matching
  relative to behavior-ID lookup. Do not add persistent application state,
  heap allocations, lookup indexes, parsing caches, or new user settings.
  Keep the production change small and readable, with a short explanation
  of any strategy threshold. Existing undefined/invalid API uses need not
  become new supported behavior.
- **SC-02: Ordered dispatch.** Iterate configured bindings and matching
  catalog entries in the same order. Preserve duplicate IDs/bindings,
  stale/absent IDs, empty bindings, every behavior kind, and success/failure
  outcomes. Compare ordered side-effect traces and observable state with a
  frozen baseline, including failures followed by successful matches.
- **SC-03: Input semantics.** Preserve primary-key tracking, left/right
  modifiers, repeated key-down dispatch, releases that match the primary
  key after modifiers are released, keyboard aliases, gamepad threshold and
  case behavior, and irrelevant input kinds. Do not add modifier conditions
  to release matching or suppress repeated behavior activation.
- **SC-04: Priority and fallbacks.** Global shortcut precedence and its
  first-primary-event gate remain unchanged. Preserve explicit-binding
  handled aggregation and Alt+1 through Alt+0 fallback, including failed
  explicit actions, stale IDs, and fewer than ten behaviors. Do not short
  circuit a later side effect because an earlier binding was handled.
- **SC-05: Live data and strategy transitions.** Binding/catalog edits take
  effect on the next event, including in-place edits with unchanged counts.
  If a count-based strategy is used, exercise count changes on both sides
  and at the strategy boundary while modifiers and a primary key remain
  held. Growth, shrinkage, repeated-down, release, and fallback must agree
  with baseline throughout a continuous event stream without reinitializing
  shortcut state at the boundary.
- **SC-06: Differential evidence.** Tests call the actual production
  dispatcher and a baseline reference under identical stubs and compare
  side-effect order plus resulting application/shortcut state. Include
  targeted edge cases and deterministic varied inputs at zero, small,
  boundary-adjacent, and large catalog sizes. A self-test of the pure
  matcher alone does not establish dispatcher parity. Keep the reference
  recognizable and identify its baseline revision.
- **SC-07: Honest component measurement.** Measure the dispatcher on the
  baseline and candidate with identical fixtures, compiler/flags and
  repeatable runs. Report timing units, distribution/variation, environment,
  input mix and catalog/binding counts. Include irrelevant input and actual
  matches, key-up, stale IDs and fallback controls; if a threshold exists,
  include its adjacent sizes, not only distant small/large endpoints.
  Exercise live production dispatch, not an invented stand-in. Confirm
  outputs outside timing, and prevent dead-code elimination. Report any
  regression/noise rather than hiding it or promising an arbitrary speedup.
  These are component CPU measurements, not end-to-end latency, FPS, CPU
  utilization, or user-visible speed guarantees.
- **SC-08: Memory accounting.** Report added persistent state, added dynamic
  allocations, and relevant object-size changes separately from process
  memory. Source inspection can establish no new allocation/state; it
  cannot establish a process RSS reduction. Do not claim one without a
  controlled process-level measurement.
- **SC-09: Build hygiene.** A strict native build with
  `BONGO_CAT_WARNINGS_AS_ERRORS=ON`, applicable tests, and repository source
  line policy must pass. The only unrelated baseline warning repairs
  permitted here are separating the two audit-scenario `if` statements
  without changing their order/returns, and defining `remove_receipt` only
  under the same `BONGO_CAT_HAS_CUBISM` condition as its existing call.
  Validate the touched conditional source with the macro absent and defined;
  a strict diagnostic-backend build plus a strict Cubism-defined syntax
  check is useful evidence but is not a full Cubism SDK/link/runtime build.
  Identify any unavailable platform/SDK validation as untested, not passed.

## Planned verification before implementation reconciliation

1. Read the candidate only after these requirements are frozen; map its
   production edits and tests to these IDs and record missing evidence.
2. Add or correct only justified implementation/test gaps, then rerun strict
   builds and targeted/full applicable tests on the final source.
3. Re-run controlled component measurements and explain their limits.
4. Review the final diff for readable scope, record actual evidence below,
   and do not publish while an unresolved functional requirement remains.

## Reconciliation and evidence

### Initial review, before gap-closing changes

The implementation is a 17-line production diff in `src/runtime/shortcuts.c`:
empty catalogs return, catalogs smaller than 32 retain ID-first lookup, and
larger catalogs calculate pure press/release matches once per binding. No
production defect was found in this initial review. Missing evidence below is
not a claim of a discovered behavioral regression.

| Requirement | Code and existing evidence | Initial assessment / gap |
|---|---|---|
| SC-01 | `behavior_shortcut` retains the nested order and uses three local booleans; no new fields or allocator calls | Satisfied by source inspection; threshold is explained as empirical |
| SC-02 | `ordered_actions`, `multiple_motions`, `empty_sound_and_failed_expression`, random cases compare ordered actions and complete application bytes | Existing coverage includes duplicate/stale/empty IDs, all behavior kinds, repeated actions and failure outcomes |
| SC-03 | `releases_and_gamepad`, globals/repeat tests, deterministic random events and unchanged `src/core/shortcut.c` | Existing cases cover release without modifiers, repeated downs, gamepad 0.5 boundary/NaN/infinities and aliases; left/right modifiers occur in varied cases |
| SC-04 | `globals_and_fallback` plus every Alt digit in `shortcut_random_cases`; global chain and fallback body unchanged | Existing failed explicit effect followed by fallback verifies handled aggregation |
| SC-05 | Bindings, IDs and binding counts mutate in place; separate fixtures run at 31/32/33 | **Open G-SC1:** no continuous held-input stream changes catalog count across the strategy boundary |
| SC-06 | `shortcut_check` invokes real dispatch plus `shortcuts_reference.c`, comparing traces/app bytes after each event | Reference body verified byte-for-byte against baseline after its six-line symbol-remap preamble; initial 16,557 comparisons reported by strict test run; extend for G-SC1 |
| SC-07 | `benchmark_shortcut_dispatch.c`, README and paired raw CSVs; same binary/flags, alternation, warmups, medians/p95, stubs and platform limitations | **Open G-SC2:** add 33-size adjacent timing control and fallback fixture; **G-SC3:** compare outputs for every benchmark fixture outside timing, rather than validating config alone |
| SC-08 | No application-layout edits, no new allocation, fixed automatic locals | Added persistent state **0 bytes**, added dynamic allocations **0** by source inspection; no process-RSS improvement established |
| SC-09 | Exact warning cleanups are separate, conditional helper/caller agree; prior strict full build and 6/6 CTests passed; macro-defined syntax check passed | Rerun final-source tests after gap closure; licensed Cubism link/runtime, native windows, Windows/macOS behavior and real audio/input remain untested |

The table above records the initial review, before the gaps were closed.
Initial measurements are historical evidence retained in the earlier revision;
the current linked files below contain the final rerun and supersede them.

### Final verification

The SC requirements were not changed to fit the result. All three initial
evidence gaps are closed. A later performance review found avoidable register
pressure in the first implementation, so the production prefilter was simplified
before publication rather than accepting that counterexample.

- **G-SC1 / SC-05/06:** `shortcut_live_edit_cases` in
  [`test_shortcut_live_edits.c`](../../tests/runtime/test_shortcut_live_edits.c)
  preserves one continuous state stream through 31→32→33→32→31, zero/regrowth,
  repeated-down, same-count binding/catalog mutations, left/right modifier
  changes and releases. It then checks fallback across catalog changes,
  including fewer than ten entries. Both explicit action expectations and
  the baseline differential checker run on every event.
- **G-SC2 / SC-07:** the benchmark now contains 26 scenarios, including
  like-for-like 31/32/33 catalog controls for key misses, long stale bindings
  and Alt fallback. Existing gamepad, key-up, actual match, empty and large
  catalog workloads remain. POSIX/Windows CPU clock failures now fail clearly
  instead of yielding invalid measurements.
- **G-SC3 / SC-07:** `benchmark_outputs_match` checks all application bytes
  and ordered action traces over two complete event cycles (16 events) for
  every timed fixture before starting its clock. Timed tracing remains off.
- **SC-01 / performance correction:** the final production change adds only
  eight lines around the original lookup/action body. It uses an outer pure
  matcher guard and intentionally rechecks matches after an ID hit; the inner
  loop and following source are byte-identical to baseline. Removing cached
  match booleans returns both loop indices to registers in the tested GCC/LTO
  dispatcher. Its stack reservation is 72 bytes versus 88 in the rejected
  version and 56 in the baseline. These are benchmark-compiler observations,
  not application or cross-platform memory measurements. Contemporaneous
  cached/guard controls kept the threshold and all 26 fixtures fixed; the
  consistent stale-31 regression did not persist with the simpler guard.
- **SC-01..06/08/09:** final strict Release build and all **6/6 CTests**
  passed, with **16,590** ordered-action/state comparisons. Current-source
  ASan/UBSan passed the differential suite. The source line policy passed.
  There are still **0 added persistent bytes and 0 added heap allocations**
  by source inspection. The unchanged warning-cleanup source passed the
  diagnostic build and strict Cubism-defined syntax check.

Current reproduction, environment, caveats and final measurements are in
[`README-shortcuts.md`](../../tests/runtime/README-shortcuts.md),
[`shortcut-results-20261005.csv`](../../tests/runtime/shortcut-results-20261005.csv)
and [`shortcut-measurements-20261005.csv`](../../tests/runtime/shortcut-measurements-20261005.csv).
The final dataset is 26 scenarios × three runs × 31 paired samples.
Adjacent threshold and negative/no-benefit controls remain visible. The final
1-behavior/64-stale-binding control still costs 6.60–37.01 ns more per event
(0.9–7.2%) in these runs. The outer guard adds a decision per binding even when
ID-first scanning is retained, and the compiled frame is not identical to the
baseline; timings do not establish how much each effect contributes. The spec
does not promise zero overhead or an arbitrary universal speedup. Avoidable
index spills were removed without duplicating the original action logic, and
the remaining bounded component tradeoff is explicitly reported for review.
No universal crossover or end-to-end latency gain is claimed. Final samples,
all 78 summaries and binary identity were independently verified before
publication; cached-version controls are not mixed into these measurements.

Remaining validation limits are explicit: no licensed Cubism link/runtime,
Windows/macOS native execution, live GUI/audio/input-device test, process-RSS
measurement or individual-event tail-latency measurement. LeakSanitizer was
disabled due to the execution environment; it is not a passed leak check.
These limits do not erase the component-level parity evidence or imply a
platform-wide performance guarantee.

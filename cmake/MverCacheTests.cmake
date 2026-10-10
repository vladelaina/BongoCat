add_library(bongo_cat_mver_cache_test_subject OBJECT
  src/render/mver_pointer_overlay_geometry.c)
target_compile_definitions(bongo_cat_mver_cache_test_subject PRIVATE
  bongo_cat_mver_pointer_geometry=mver_cache_counted_geometry)
target_include_directories(bongo_cat_mver_cache_test_subject PRIVATE src/render)
target_link_libraries(bongo_cat_mver_cache_test_subject PRIVATE
  bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
add_executable(bongo_cat_mver_cache_tests tests/render/test_mver_cache.c
  $<TARGET_OBJECTS:bongo_cat_mver_cache_test_subject>)
target_include_directories(bongo_cat_mver_cache_tests PRIVATE src/render tests)
target_link_libraries(bongo_cat_mver_cache_tests PRIVATE
  bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
add_test(NAME mver-geometry-cache COMMAND bongo_cat_mver_cache_tests)

add_library(bongo_cat_mver_draw_subjects OBJECT
  tests/render/mver_draw_subject.c tests/render/mver_draw_reference.c
  tests/render/mver_lifecycle_subject.c
  src/render/mver_pointer_overlay_geometry.c)
target_include_directories(bongo_cat_mver_draw_subjects PRIVATE src/render)
target_link_libraries(bongo_cat_mver_draw_subjects PRIVATE
  bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
# Instrument only separate correctness/preflight copies, never timing subjects.
add_library(bongo_cat_mver_counted_subject OBJECT tests/render/mver_counted_subject.c)
add_library(bongo_cat_mver_counted_reference OBJECT tests/render/mver_draw_reference.c)
target_compile_definitions(bongo_cat_mver_counted_reference PRIVATE
  bongo_cat_mver_pointer_geometry=mver_counted_reference_geometry
  mver_reference_before=mver_counted_reference_before
  mver_reference_after=mver_counted_reference_after)
foreach(target bongo_cat_mver_counted_subject bongo_cat_mver_counted_reference)
  target_include_directories(${target} PRIVATE src/render)
  target_link_libraries(${target} PRIVATE bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
  bongo_cat_enable_release_ipo(${target})
endforeach()
# Keep GL callbacks opaque to the IPO-compiled renderers, also in benchmarks.
add_library(bongo_cat_mver_probe OBJECT tests/render/mver_probe.c)
target_include_directories(bongo_cat_mver_probe PRIVATE src/render)
target_link_libraries(bongo_cat_mver_probe PRIVATE
  bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
set_property(TARGET bongo_cat_mver_probe PROPERTY INTERPROCEDURAL_OPTIMIZATION FALSE)
add_executable(bongo_cat_mver_draw_tests tests/render/test_mver_draw_trace.c
  $<TARGET_OBJECTS:bongo_cat_mver_draw_subjects>
  $<TARGET_OBJECTS:bongo_cat_mver_probe>
  $<TARGET_OBJECTS:bongo_cat_mver_counted_subject>
  $<TARGET_OBJECTS:bongo_cat_mver_counted_reference>)
target_include_directories(bongo_cat_mver_draw_tests PRIVATE src/render tests)
target_compile_definitions(bongo_cat_mver_draw_tests PRIVATE
  BONGO_CAT_MVER_FIXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/render/fixtures/mver")
target_link_libraries(bongo_cat_mver_draw_tests PRIVATE
  bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
add_test(NAME mver-draw-trace COMMAND bongo_cat_mver_draw_tests)
bongo_cat_enable_release_ipo(bongo_cat_mver_cache_test_subject
  bongo_cat_mver_cache_tests bongo_cat_mver_draw_subjects bongo_cat_mver_draw_tests)

# Opt-in component benchmark; intentionally not a timing-sensitive CTest.
option(BONGO_CAT_BUILD_MVER_BENCHMARK "Build Mver geometry/draw CPU benchmark" OFF)
if(BONGO_CAT_BUILD_MVER_BENCHMARK AND UNIX)
  add_executable(bongo_cat_mver_bench tests/performance/bench_mver.c
    $<TARGET_OBJECTS:bongo_cat_mver_draw_subjects>
    $<TARGET_OBJECTS:bongo_cat_mver_probe>
    $<TARGET_OBJECTS:bongo_cat_mver_counted_subject>
    $<TARGET_OBJECTS:bongo_cat_mver_counted_reference>)
  target_include_directories(bongo_cat_mver_bench PRIVATE src/render tests/render)
  target_link_libraries(bongo_cat_mver_bench PRIVATE
    bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
  bongo_cat_enable_release_ipo(bongo_cat_mver_bench)
endif()

if(BUILD_TESTING)
  set(BONGO_CAT_SHORTCUT_TEST_SOURCES
    src/runtime/shortcuts.c
    src/core/shortcut.c
    tests/runtime/shortcuts_reference.c
    tests/runtime/shortcut_test_support.c)
  add_executable(bongo_cat_shortcut_dispatch_tests
    ${BONGO_CAT_SHORTCUT_TEST_SOURCES}
    tests/runtime/test_shortcut_dispatch.c
    tests/runtime/test_shortcut_random.c)
  target_include_directories(bongo_cat_shortcut_dispatch_tests PRIVATE
    include tests tests/runtime src/runtime)
  target_link_libraries(bongo_cat_shortcut_dispatch_tests PRIVATE
    bongo_cat_warnings SDL3::SDL3-static)
  target_compile_definitions(bongo_cat_shortcut_dispatch_tests PRIVATE
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
  add_test(NAME shortcut-dispatch COMMAND bongo_cat_shortcut_dispatch_tests)

  option(BONGO_CAT_BUILD_SHORTCUT_BENCHMARK
    "Build the dispatch microbenchmark (not an end-to-end latency test)" OFF)
  if(BONGO_CAT_BUILD_SHORTCUT_BENCHMARK)
    add_executable(bongo_cat_shortcut_benchmark
      ${BONGO_CAT_SHORTCUT_TEST_SOURCES}
      tests/runtime/benchmark_shortcut_dispatch.c)
    target_include_directories(bongo_cat_shortcut_benchmark PRIVATE
      include tests/runtime src/runtime)
    target_link_libraries(bongo_cat_shortcut_benchmark PRIVATE
      bongo_cat_core bongo_cat_warnings SDL3::SDL3-static)
    target_compile_definitions(bongo_cat_shortcut_benchmark PRIVATE
      $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
    bongo_cat_enable_release_ipo(bongo_cat_shortcut_benchmark)
  endif()
  if(MSVC)
    set_property(SOURCE ${BONGO_CAT_SHORTCUT_TEST_SOURCES}
      tests/runtime/test_shortcut_dispatch.c tests/runtime/test_shortcut_random.c
      tests/runtime/benchmark_shortcut_dispatch.c APPEND PROPERTY
      COMPILE_OPTIONS "/experimental:c11atomics")
  endif()
endif()

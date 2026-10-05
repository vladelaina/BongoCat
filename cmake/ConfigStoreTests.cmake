# Compile the exact old and new store implementations in separate objects.
# Only save calls are intercepted; validation and JSON serialization are real.
add_library(bongo_cat_config_store_subject OBJECT
  src/runtime/lifecycle/config_store.c)
add_library(bongo_cat_config_store_reference OBJECT
  tests/runtime/config_store_reference.c)
foreach(target bongo_cat_config_store_subject bongo_cat_config_store_reference)
  target_include_directories(${target} PRIVATE
    ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
  target_link_libraries(${target} PRIVATE
    bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
  target_compile_definitions(${target} PRIVATE
    bongo_cat_settings_save=config_test_settings_save
    bongo_cat_session_save=config_test_session_save)
endforeach()
target_compile_definitions(bongo_cat_config_store_reference PRIVATE
  bongo_cat_config_store_load=config_reference_load
  bongo_cat_config_store_update=config_reference_update
  bongo_cat_config_store_flush=config_reference_flush)

function(bongo_cat_config_store_test_target target source)
  add_executable(${target} ${ARGN} ${source}
    tests/runtime/config_store_support.c
    $<TARGET_OBJECTS:bongo_cat_config_store_subject>
    $<TARGET_OBJECTS:bongo_cat_config_store_reference>)
  target_compile_definitions(${target} PRIVATE
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>)
  target_include_directories(${target} PRIVATE tests/runtime tests/support
    ${BONGO_CAT_RUNTIME_INTERNAL_INCLUDE_DIRS})
  target_link_libraries(${target} PRIVATE
    bongo_cat_core SDL3::SDL3-static bongo_cat_warnings)
  if(UNIX AND NOT APPLE)
    target_link_libraries(${target} PRIVATE m)
  endif()
endfunction()

bongo_cat_config_store_test_target(bongo_cat_config_store_tests
  tests/runtime/test_config_store.c)
add_test(NAME config-store COMMAND bongo_cat_config_store_tests)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/config-store-test")
set_tests_properties(config-store PROPERTIES
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/config-store-test")
bongo_cat_config_store_test_target(bongo_cat_config_store_bench
  tests/runtime/bench_config_store.c EXCLUDE_FROM_ALL)

if(MSVC)
  set_property(SOURCE tests/runtime/config_store_reference.c
    tests/runtime/config_store_support.c tests/runtime/test_config_store.c
    tests/runtime/bench_config_store.c APPEND PROPERTY
    COMPILE_OPTIONS "/experimental:c11atomics")
endif()

# Match the native Release targets' IPO setting for both implementations.
bongo_cat_enable_release_ipo(bongo_cat_config_store_subject
  bongo_cat_config_store_reference bongo_cat_config_store_tests
  bongo_cat_config_store_bench)

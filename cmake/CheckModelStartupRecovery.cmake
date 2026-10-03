if(NOT EXISTS "${EXECUTABLE}" OR NOT EXISTS "${ASSET_ROOT}/models" OR
    NOT TEST_ROOT)
  message(FATAL_ERROR "Startup recovery test requires executable, assets and test root")
endif()

string(RANDOM LENGTH 12 ALPHABET 0123456789abcdef run_id)
set(storage "${TEST_ROOT}/${run_id}")
file(MAKE_DIRECTORY "${storage}")
file(COPY "${ASSET_ROOT}/models" DESTINATION "${storage}")
foreach(model standard keyboard gamepad)
  file(WRITE "${storage}/models/${model}/cat.model3.json" "{}")
endforeach()

execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env BONGO_CAT_DISABLE_NEARBY_MODEL_SCAN=1
    "${EXECUTABLE}" --ci-smoke --ci-ignore-global-input "--storage-root=${storage}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 50)
if(NOT result STREQUAL "0" OR
    NOT "${output}${errors}" MATCHES "Recovered startup using bundled model")
  message(FATAL_ERROR
    "Startup recovery failed (${result}); data retained at ${storage}\n${output}\n${errors}")
endif()
message(STATUS "Startup recovered from damaged stored models; test data: ${storage}")

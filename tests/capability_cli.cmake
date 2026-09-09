if(NOT DEFINED EMOJINEER)
  message(FATAL_ERROR "EMOJINEER executable path is required")
endif()
if(NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "TEST_ROOT is required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(SOURCE "${TEST_ROOT}/native.emoji")
file(WRITE "${SOURCE}" "📝 🕰️ 🫴 🤲\n📝 🎲 🫴 1000 🤲\n")

function(run_expect_success label)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${label} failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
  endif()
  set(LAST_OUTPUT "${output}" PARENT_SCOPE)
  set(LAST_ERROR "${error}" PARENT_SCOPE)
endfunction()

function(run_expect_failure label needle)
  execute_process(
    COMMAND ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE error)
  if(result EQUAL 0)
    message(FATAL_ERROR "${label} unexpectedly succeeded\nstdout:\n${output}")
  endif()
  string(FIND "${error}" "${needle}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "${label} did not report '${needle}'\nstdout:\n${output}\nstderr:\n${error}")
  endif()
endfunction()

run_expect_success("capabilities source" "${EMOJINEER}" capabilities "${SOURCE}")
if(NOT LAST_OUTPUT STREQUAL "required capabilities: clock, random\n")
  message(FATAL_ERROR "unexpected capability report: ${LAST_OUTPUT}")
endif()

run_expect_failure("default denial" "missing capability grant(s): clock, random"
                   "${EMOJINEER}" run "${SOURCE}")

run_expect_success("deterministic run A"
                   "${EMOJINEER}" run "${SOURCE}"
                   --deterministic --grant clock --grant random --seed 7 --clock-ms 55)
set(OUTPUT_A "${LAST_OUTPUT}")
run_expect_success("deterministic run B"
                   "${EMOJINEER}" run "${SOURCE}"
                   --deterministic --grant clock --grant random --seed 7 --clock-ms 55)
if(NOT LAST_OUTPUT STREQUAL OUTPUT_A)
  message(FATAL_ERROR "deterministic executions differed\nA:\n${OUTPUT_A}\nB:\n${LAST_OUTPUT}")
endif()
string(FIND "${LAST_OUTPUT}" "55\n" starts_at)
if(NOT starts_at EQUAL 0)
  message(FATAL_ERROR "deterministic clock did not start at 55: ${LAST_OUTPUT}")
endif()

run_expect_failure("sandbox grant rejection" "sandbox mode cannot grant host capabilities"
                   "${EMOJINEER}" run "${SOURCE}" --sandbox --grant clock)
run_expect_failure("deterministic real-host rejection" "permits only clock and random"
                   "${EMOJINEER}" run "${SOURCE}" --deterministic --grant filesystem)
run_expect_failure("non-execution option rejection" "does not accept execution policy options"
                   "${EMOJINEER}" check "${SOURCE}" --grant clock)

set(BYTECODE "${TEST_ROOT}/native.emjbc")
run_expect_success("compile v8" "${EMOJINEER}" compile "${SOURCE}" -o "${BYTECODE}")
run_expect_success("capabilities bytecode" "${EMOJINEER}" capabilities "${BYTECODE}")
if(NOT LAST_OUTPUT STREQUAL "required capabilities: clock, random\n")
  message(FATAL_ERROR "unexpected bytecode capability report: ${LAST_OUTPUT}")
endif()

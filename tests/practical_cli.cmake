if(NOT DEFINED EMOJINEER)
  message(FATAL_ERROR "EMOJINEER executable path is required")
endif()
if(NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "TEST_ROOT is required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")
set(SOURCE "${TEST_ROOT}/arguments.emoji")
file(WRITE "${SOURCE}" "📝 🧳 🫴 🤲\n")

execute_process(
  COMMAND "${EMOJINEER}" run "${SOURCE}" -- alpha "two words"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "argv run failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
endif()
if(NOT output STREQUAL "[alpha, two words]\n")
  message(FATAL_ERROR "argv run output mismatch: ${output}")
endif()

set(BYTECODE "${TEST_ROOT}/arguments.emjbc")
execute_process(
  COMMAND "${EMOJINEER}" compile "${SOURCE}" -o "${BYTECODE}"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "argv compile failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
endif()
execute_process(
  COMMAND "${EMOJINEER}" exec "${BYTECODE}" -- alpha "two words"
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "argv exec failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
endif()
if(NOT output STREQUAL "[alpha, two words]\n")
  message(FATAL_ERROR "argv exec output mismatch: ${output}")
endif()

execute_process(
  COMMAND "${EMOJINEER}" check "${SOURCE}" -- nope
  RESULT_VARIABLE result
  OUTPUT_VARIABLE output
  ERROR_VARIABLE error)
if(result EQUAL 0)
  message(FATAL_ERROR "non-execution argv unexpectedly succeeded")
endif()
string(FIND "${error}" "accepted only by run or exec" found)
if(found EQUAL -1)
  message(FATAL_ERROR "non-run argv rejection was unclear: ${error}")
endif()

if(NOT DEFINED EMOJINEER OR NOT DEFINED EMJI OR NOT DEFINED SOURCE_ROOT OR NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "EMOJINEER, EMJI, SOURCE_ROOT, and TEST_ROOT are required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}")

function(run_ok label)
  execute_process(COMMAND ${ARGN}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${label} failed (${result})\nstdout:\n${output}\nstderr:\n${error}")
  endif()
  set(LAST_OUTPUT "${output}" PARENT_SCOPE)
  set(LAST_ERROR "${error}" PARENT_SCOPE)
endfunction()

function(run_fail label needle)
  execute_process(COMMAND ${ARGN}
    RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
  if(result EQUAL 0)
    message(FATAL_ERROR "${label} unexpectedly succeeded")
  endif()
  string(FIND "${error}" "${needle}" found)
  if(found EQUAL -1)
    message(FATAL_ERROR "${label} missing '${needle}'\nstderr:\n${error}")
  endif()
endfunction()

run_ok("hello example" "${EMOJINEER}" run "${SOURCE_ROOT}/examples/hello.emoji")
if(NOT LAST_OUTPUT STREQUAL "Hello from Emojineer 🌍\n")
  message(FATAL_ERROR "hello output changed: ${LAST_OUTPUT}")
endif()

run_ok("calculator example" "${EMOJINEER}" run "${SOURCE_ROOT}/examples/calculator.emoji")
if(NOT LAST_OUTPUT STREQUAL "42\n42\n7\n")
  message(FATAL_ERROR "calculator output changed: ${LAST_OUTPUT}")
endif()

run_ok("data example" "${EMOJINEER}" run "${SOURCE_ROOT}/examples/practical_data.emoji")
string(FIND "${LAST_OUTPUT}" "Person{age: 38, name: Ada}" data_found)
if(data_found EQUAL -1)
  message(FATAL_ERROR "practical data output missing updated record: ${LAST_OUTPUT}")
endif()

run_ok("encoding example" "${EMOJINEER}" run "${SOURCE_ROOT}/examples/encoding.emoji")
string(FIND "${LAST_OUTPUT}" "4869f09f9982" encoding_found)
if(encoding_found EQUAL -1)
  message(FATAL_ERROR "encoding example did not emit expected UTF-8 hex")
endif()

run_ok("argv example" "${EMOJINEER}" run "${SOURCE_ROOT}/examples/cli_args.emoji"
       -- alpha "two words")
if(NOT LAST_OUTPUT STREQUAL "2\n[alpha, two words]\n")
  message(FATAL_ERROR "argv example output changed: ${LAST_OUTPUT}")
endif()

run_ok("package example check" "${EMJI}" check "${SOURCE_ROOT}/examples/package_use/app")
run_ok("package example run" "${EMOJINEER}" run
       "${SOURCE_ROOT}/examples/package_use/app/src/main.emoji")
if(NOT LAST_OUTPUT STREQUAL "42\n")
  message(FATAL_ERROR "package example output changed: ${LAST_OUTPUT}")
endif()

run_ok("network capability inspection" "${EMOJINEER}" capabilities
       "${SOURCE_ROOT}/examples/network_request.emoji")
if(NOT LAST_OUTPUT STREQUAL "required capabilities: network\n")
  message(FATAL_ERROR "network example capability changed: ${LAST_OUTPUT}")
endif()
run_fail("network default denial" "missing capability grant(s): network"
         "${EMOJINEER}" run "${SOURCE_ROOT}/examples/network_request.emoji")

run_ok("CER example" "${EMOJINEER}" run "${SOURCE_ROOT}/examples/cer.emoji"
       --cer "${SOURCE_ROOT}/cer/example.json")
string(FIND "${LAST_OUTPUT}" "CER online" cer_found)
if(cer_found EQUAL -1)
  message(FATAL_ERROR "CER example output missing expected text")
endif()

run_ok("interop inspection" "${EMOJINEER}" interop "${SOURCE_ROOT}/examples/interop.emoji")
string(FIND "${LAST_OUTPUT}" "example.double" interop_found)
if(interop_found EQUAL -1)
  message(FATAL_ERROR "interop example missing adapter contract")
endif()

run_ok("EASM example" "${EMOJINEER}" easm-run "${SOURCE_ROOT}/examples/lowlevel.easm")
if(NOT LAST_OUTPUT STREQUAL "42\n")
  message(FATAL_ERROR "EASM example output changed: ${LAST_OUTPUT}")
endif()

foreach(template IN ITEMS hello cli data network)
  set(project "${TEST_ROOT}/${template}")
  run_ok("init ${template}" "${EMJI}" init "${project}" --name "${template}-demo"
         --template "${template}")
  run_ok("check ${template}" "${EMJI}" check "${project}")
endforeach()

run_ok("run hello template" "${EMOJINEER}" run "${TEST_ROOT}/hello/src/main.emoji")
run_ok("run cli template" "${EMOJINEER}" run "${TEST_ROOT}/cli/src/main.emoji" -- demo)
if(NOT LAST_OUTPUT STREQUAL "1\n[demo]\n")
  message(FATAL_ERROR "CLI template output changed: ${LAST_OUTPUT}")
endif()
run_ok("run data template" "${EMOJINEER}" run "${TEST_ROOT}/data/src/main.emoji")
run_ok("network template capabilities" "${EMOJINEER}" capabilities
       "${TEST_ROOT}/network/src/main.emoji")
if(NOT LAST_OUTPUT STREQUAL "required capabilities: network\n")
  message(FATAL_ERROR "network template capability changed")
endif()

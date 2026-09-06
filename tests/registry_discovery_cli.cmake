if(NOT DEFINED EMJI)
  message(FATAL_ERROR "EMJI executable path is required")
endif()
if(NOT DEFINED TEST_ROOT)
  message(FATAL_ERROR "TEST_ROOT is required")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")
file(MAKE_DIRECTORY "${TEST_ROOT}/registry")
file(MAKE_DIRECTORY "${TEST_ROOT}/core/src")
file(MAKE_DIRECTORY "${TEST_ROOT}/consumer/src")

file(WRITE "${TEST_ROOT}/core/emojineer.toml" "[package]\nname = \"core\"\nversion = \"1.0.0\"\nentry = \"src/main.emoji\"\n")
file(WRITE "${TEST_ROOT}/core/src/main.emoji" "📝 📜core📜\n")
file(WRITE "${TEST_ROOT}/consumer/emojineer.toml" "[package]\nname = \"consumer\"\nversion = \"1.0.0\"\nentry = \"src/main.emoji\"\n\n[dependencies]\ncore = \"../core\"\n")
file(WRITE "${TEST_ROOT}/consumer/src/main.emoji" "📝 📜consumer📜\n")

function(run_ok label)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT result EQUAL 0)
    message(FATAL_ERROR "${label} failed (${result})\nstdout:\n${out}\nstderr:\n${err}")
  endif()
  set(LAST_OUT "${out}" PARENT_SCOPE)
endfunction()

run_ok("registry init" "${EMJI}" registry-init "${TEST_ROOT}/registry" --id cli.discovery)
run_ok("publish core" "${EMJI}" publish "${TEST_ROOT}/core" --registry "${TEST_ROOT}/registry")
run_ok("publish consumer stable" "${EMJI}" publish "${TEST_ROOT}/consumer" --registry "${TEST_ROOT}/registry")

file(WRITE "${TEST_ROOT}/consumer/emojineer.toml" "[package]\nname = \"consumer\"\nversion = \"2.0.0-beta.1\"\nentry = \"src/main.emoji\"\n\n[dependencies]\ncore = \"../core\"\n")
run_ok("publish consumer prerelease" "${EMJI}" publish "${TEST_ROOT}/consumer" --registry "${TEST_ROOT}/registry")

run_ok("stable search" "${EMJI}" search consumer --registry "${TEST_ROOT}/registry")
if(NOT LAST_OUT MATCHES "consumer  1.0.0  stable")
  message(FATAL_ERROR "stable search did not select 1.0.0\n${LAST_OUT}")
endif()

run_ok("prerelease search" "${EMJI}" search consumer --registry "${TEST_ROOT}/registry" --include-prerelease)
if(NOT LAST_OUT MATCHES "consumer  2.0.0-beta.1  prerelease")
  message(FATAL_ERROR "prerelease search did not select 2.0.0-beta.1\n${LAST_OUT}")
endif()

run_ok("dependency keyword search" "${EMJI}" search core --registry "${TEST_ROOT}/registry")
if(NOT LAST_OUT MATCHES "consumer  1.0.0")
  message(FATAL_ERROR "dependency metadata was not searchable\n${LAST_OUT}")
endif()

run_ok("reverse dependencies" "${EMJI}" dependents core --registry "${TEST_ROOT}/registry")
if(NOT LAST_OUT MATCHES "consumer  1.0.0")
  message(FATAL_ERROR "reverse dependency query missed consumer\n${LAST_OUT}")
endif()

run_ok("package info JSON" "${EMJI}" package-info consumer --registry "${TEST_ROOT}/registry" --json)
if(NOT LAST_OUT MATCHES "emojineer.registry-package-info.v1")
  message(FATAL_ERROR "package-info JSON schema missing\n${LAST_OUT}")
endif()

run_ok("wire discovery index" "${EMJI}" discovery-index --registry "${TEST_ROOT}/registry")
if(NOT LAST_OUT MATCHES "^EMJREGDISC1")
  message(FATAL_ERROR "discovery-index did not emit canonical wire format\n${LAST_OUT}")
endif()

file(REMOVE_RECURSE "${TEST_ROOT}")

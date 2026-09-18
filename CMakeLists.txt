cmake_minimum_required(VERSION 3.20)
project(Emojineer VERSION 0.23.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT EMSCRIPTEN)
 find_package(ICU 70 REQUIRED COMPONENTS uc i18n)
 find_package(CURL QUIET)
endif()
add_library(emojineer_core
    src/unicode.cpp
    src/cer.cpp
    src/lexer.cpp
    src/parser.cpp
    src/module.cpp
    src/stdlib.cpp
    src/hash.cpp
    src/package.cpp
    src/package_report.cpp
    src/package_artifact.cpp
    src/registry.cpp
    src/registry_transport.cpp
    src/registry_discovery.cpp
    src/authenticated_publication.cpp
    src/browser_runtime.cpp
    src/web.cpp
    src/capability.cpp
    src/intrinsic.cpp
    src/interop.cpp
    src/easm.cpp
    src/bytecode.cpp
    src/compiler.cpp
    src/vm.cpp
    src/disassembler.cpp
    src/repl.cpp
    src/source_tools.cpp
    src/project.cpp
    src/lsp.cpp
    src/debugger.cpp
)
target_include_directories(emojineer_core PUBLIC include)
target_compile_definitions(emojineer_core PUBLIC EMOJINEER_VERSION="${PROJECT_VERSION}")
if(EMSCRIPTEN)
 set(EMOJINEER_ICU_ARCHIVE "${CMAKE_BINARY_DIR}/icu4c-68.2-data-bin-l.zip")
 set(EMOJINEER_ICU_EXTRACT_DIR "${CMAKE_BINARY_DIR}/icu68-data")
 set(EMOJINEER_ICU_DATA_FILE "${EMOJINEER_ICU_EXTRACT_DIR}/icudt68l.dat")
 if(NOT EXISTS "${EMOJINEER_ICU_DATA_FILE}")
  file(DOWNLOAD
       "https://github.com/unicode-org/icu/releases/download/release-68-2/icu4c-68.2-data-bin-l.zip"
       "${EMOJINEER_ICU_ARCHIVE}"
       EXPECTED_HASH "SHA256=89e4022c14cdaec92e7fc8f7214412dd02c8010ccba26fcee7c9ae30fdfadcbc"
       STATUS EMOJINEER_ICU_DOWNLOAD_STATUS)
  list(GET EMOJINEER_ICU_DOWNLOAD_STATUS 0 EMOJINEER_ICU_DOWNLOAD_CODE)
  if(NOT EMOJINEER_ICU_DOWNLOAD_CODE EQUAL 0)
   list(GET EMOJINEER_ICU_DOWNLOAD_STATUS 1 EMOJINEER_ICU_DOWNLOAD_MESSAGE)
   message(FATAL_ERROR "failed to download pinned ICU data: ${EMOJINEER_ICU_DOWNLOAD_MESSAGE}")
  endif()
  file(MAKE_DIRECTORY "${EMOJINEER_ICU_EXTRACT_DIR}")
  file(ARCHIVE_EXTRACT INPUT "${EMOJINEER_ICU_ARCHIVE}" DESTINATION "${EMOJINEER_ICU_EXTRACT_DIR}")
 endif()
 target_compile_options(emojineer_core PUBLIC "SHELL:-sUSE_ICU=1" -fexceptions)
 target_link_options(emojineer_core PUBLIC "SHELL:-sUSE_ICU=1" -fexceptions)
else()
 target_link_libraries(emojineer_core PUBLIC ICU::uc ICU::i18n)
endif()
if(CURL_FOUND)
 target_link_libraries(emojineer_core PRIVATE CURL::libcurl)
 target_compile_definitions(emojineer_core PRIVATE EMOJINEER_HAVE_CURL=1)
endif()
target_compile_options(emojineer_core PRIVATE -Wall -Wextra -Wpedantic)
add_executable(emojineer src/main.cpp)
target_link_libraries(emojineer PRIVATE emojineer_core)
add_executable(emji src/emji_main.cpp)
target_link_libraries(emji PRIVATE emojineer_core)
add_executable(emojineer-lsp src/lsp_main.cpp)
target_link_libraries(emojineer-lsp PRIVATE emojineer_core)
if(EMSCRIPTEN)
 add_executable(emojineer-playground browser/emojineer_browser.cpp)
 target_link_libraries(emojineer-playground PRIVATE emojineer_core)
 target_link_options(emojineer-playground PRIVATE
     "SHELL:-sUSE_ICU=1"
     "SHELL:--preload-file ${EMOJINEER_ICU_DATA_FILE}@/icu/icudt68l.dat"
     "SHELL:-sALLOW_MEMORY_GROWTH=1"
     "SHELL:-sMODULARIZE=1"
     "SHELL:-sEXPORT_NAME=EmojineerModule"
     "SHELL:-sEXPORTED_RUNTIME_METHODS=['ccall']")
 set_target_properties(emojineer-playground PROPERTIES SUFFIX ".js")
 add_custom_command(TARGET emojineer-playground POST_BUILD
     COMMAND ${CMAKE_COMMAND} -E copy_if_different
             ${CMAKE_CURRENT_SOURCE_DIR}/browser/index.html
             ${CMAKE_CURRENT_SOURCE_DIR}/browser/playground.js
             ${CMAKE_CURRENT_SOURCE_DIR}/browser/style.css
             $<TARGET_FILE_DIR:emojineer-playground>)
endif()
include(CTest)
if(BUILD_TESTING)
 add_executable(emojineer_tests tests/tests.cpp)
 target_link_libraries(emojineer_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_tests COMMAND emojineer_tests)
 add_executable(emojineer_source_tools_tests tests/source_tools_tests.cpp)
 target_link_libraries(emojineer_source_tools_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_source_tools_tests COMMAND emojineer_source_tools_tests)
 add_executable(emojineer_project_tests tests/project_tests.cpp)
 target_link_libraries(emojineer_project_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_project_tests COMMAND emojineer_project_tests)
 add_executable(emojineer_package_tests tests/package_tests.cpp)
 target_link_libraries(emojineer_package_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_package_tests COMMAND emojineer_package_tests)
 add_executable(emojineer_package_report_tests tests/package_report_tests.cpp)
 target_link_libraries(emojineer_package_report_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_package_report_tests COMMAND emojineer_package_report_tests)
 add_executable(emojineer_package_artifact_tests tests/package_artifact_tests.cpp)
 target_link_libraries(emojineer_package_artifact_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_package_artifact_tests COMMAND emojineer_package_artifact_tests)
 add_executable(emojineer_registry_transport_tests tests/registry_transport_tests.cpp)
 target_link_libraries(emojineer_registry_transport_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_registry_transport_tests COMMAND emojineer_registry_transport_tests)
 add_executable(emojineer_repl_stream_tests tests/repl_stream_tests.cpp)
 target_link_libraries(emojineer_repl_stream_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_repl_stream_tests COMMAND emojineer_repl_stream_tests)
 add_executable(emojineer_module_tests tests/module_tests.cpp)
 target_link_libraries(emojineer_module_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_module_tests COMMAND emojineer_module_tests)
 add_executable(emojineer_package_import_tests tests/package_import_tests.cpp)
 target_link_libraries(emojineer_package_import_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_package_import_tests COMMAND emojineer_package_import_tests)
 add_executable(emojineer_stdlib_tests tests/stdlib_tests.cpp)
 target_link_libraries(emojineer_stdlib_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_stdlib_tests COMMAND emojineer_stdlib_tests)
 add_executable(emojineer_acceptance_journey_tests tests/acceptance_journey_tests.cpp)
 target_link_libraries(emojineer_acceptance_journey_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_acceptance_journey_tests COMMAND emojineer_acceptance_journey_tests)
 add_executable(emojineer_authenticated_publication_tests tests/authenticated_publication_tests.cpp)
 target_link_libraries(emojineer_authenticated_publication_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_authenticated_publication_tests COMMAND emojineer_authenticated_publication_tests)
 add_executable(emojineer_lsp_tests tests/lsp_tests.cpp)
 target_link_libraries(emojineer_lsp_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_lsp_tests COMMAND emojineer_lsp_tests)
 add_executable(emojineer_source_diagnostic_tests tests/source_diagnostic_tests.cpp)
 target_link_libraries(emojineer_source_diagnostic_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_source_diagnostic_tests COMMAND emojineer_source_diagnostic_tests)
 add_executable(emojineer_debugger_tests tests/debugger_tests.cpp)
 target_link_libraries(emojineer_debugger_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_debugger_tests COMMAND emojineer_debugger_tests)
 add_executable(emojineer_registry_discovery_tests tests/registry_discovery_tests.cpp)
 target_link_libraries(emojineer_registry_discovery_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_registry_discovery_tests COMMAND emojineer_registry_discovery_tests)
 add_test(NAME emojineer_registry_discovery_cli
          COMMAND ${CMAKE_COMMAND}
                  -DEMJI=$<TARGET_FILE:emji>
                  -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/registry-discovery-cli
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/registry_discovery_cli.cmake)
 add_executable(emojineer_capability_tests tests/capability_tests.cpp)
 target_link_libraries(emojineer_capability_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_capability_tests COMMAND emojineer_capability_tests)
 add_executable(emojineer_interop_tests tests/interop_tests.cpp)
 target_link_libraries(emojineer_interop_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_interop_tests COMMAND emojineer_interop_tests)
 add_executable(emojineer_easm_tests tests/easm_tests.cpp)
 target_link_libraries(emojineer_easm_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_easm_tests COMMAND emojineer_easm_tests)
 add_executable(emojineer_practical_runtime_tests tests/practical_runtime_tests.cpp)
 target_link_libraries(emojineer_practical_runtime_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_practical_runtime_tests COMMAND emojineer_practical_runtime_tests)
 add_executable(emojineer_browser_runtime_tests tests/browser_runtime_tests.cpp)
 target_link_libraries(emojineer_browser_runtime_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_browser_runtime_tests COMMAND emojineer_browser_runtime_tests)
 add_executable(emojineer_web_tests tests/web_tests.cpp)
 target_link_libraries(emojineer_web_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_web_tests COMMAND emojineer_web_tests)
 add_executable(emojineer_release_hardening_tests tests/release_hardening_tests.cpp)
 target_link_libraries(emojineer_release_hardening_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_release_hardening_tests COMMAND emojineer_release_hardening_tests)
 add_test(NAME emojineer_capability_cli
          COMMAND ${CMAKE_COMMAND}
                  -DEMOJINEER=$<TARGET_FILE:emojineer>
                  -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/capability-cli
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/capability_cli.cmake)
 add_test(NAME emojineer_interop_cli
          COMMAND ${CMAKE_COMMAND}
                  -DEMOJINEER=$<TARGET_FILE:emojineer>
                  -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/interop-cli
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/interop_cli.cmake)
 add_test(NAME emojineer_easm_cli
          COMMAND ${CMAKE_COMMAND}
                  -DEMOJINEER=$<TARGET_FILE:emojineer>
                  -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/easm-cli
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/easm_cli.cmake)
 add_test(NAME emojineer_version_cli
          COMMAND ${CMAKE_COMMAND}
                  -DEMOJINEER=$<TARGET_FILE:emojineer>
                  -DEMJI=$<TARGET_FILE:emji>
                  -DLSP=$<TARGET_FILE:emojineer-lsp>
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/version_cli.cmake)
 add_test(NAME emojineer_practical_cli
          COMMAND ${CMAKE_COMMAND}
                  -DEMOJINEER=$<TARGET_FILE:emojineer>
                  -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/practical-cli
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/practical_cli.cmake)
 add_test(NAME emojineer_examples_cli
          COMMAND ${CMAKE_COMMAND}
                  -DEMOJINEER=$<TARGET_FILE:emojineer>
                  -DEMJI=$<TARGET_FILE:emji>
                  -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
                  -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/examples-cli
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/examples_cli.cmake)
 add_test(NAME emojineer_web_cli
          COMMAND ${CMAKE_COMMAND}
                  -DEMOJINEER=$<TARGET_FILE:emojineer>
                  -DSOURCE_ROOT=${CMAKE_CURRENT_SOURCE_DIR}
                  -DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/web-cli
                  -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/web_cli.cmake)
endif()

include(cmake/Packaging.cmake)

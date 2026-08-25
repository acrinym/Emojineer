cmake_minimum_required(VERSION 3.20)
project(Emojineer VERSION 0.18.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
find_package(ICU 70 REQUIRED COMPONENTS uc i18n)
find_package(CURL QUIET)
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
    src/bytecode.cpp
    src/compiler.cpp
    src/vm.cpp
    src/disassembler.cpp
    src/repl.cpp
    src/source_tools.cpp
    src/project.cpp
    src/debugger.cpp
)
target_include_directories(emojineer_core PUBLIC include)
target_link_libraries(emojineer_core PUBLIC ICU::uc ICU::i18n)
if(CURL_FOUND)
 target_link_libraries(emojineer_core PRIVATE CURL::libcurl)
 target_compile_definitions(emojineer_core PRIVATE EMOJINEER_HAVE_CURL=1)
endif()
target_compile_options(emojineer_core PRIVATE -Wall -Wextra -Wpedantic)
add_executable(emojineer src/main.cpp)
target_link_libraries(emojineer PRIVATE emojineer_core)
add_executable(emji src/emji_main.cpp)
target_link_libraries(emji PRIVATE emojineer_core)
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
 add_executable(emojineer_debugger_tests tests/debugger_tests.cpp)
 target_link_libraries(emojineer_debugger_tests PRIVATE emojineer_core)
 add_test(NAME emojineer_debugger_tests COMMAND emojineer_debugger_tests)
endif()
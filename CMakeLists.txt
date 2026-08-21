cmake_minimum_required(VERSION 3.20)
project(Emojineer VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(ICU 70 REQUIRED COMPONENTS uc i18n)

add_library(emojineer_core
    src/unicode.cpp
    src/lexer.cpp
    src/parser.cpp
    src/bytecode.cpp
    src/compiler.cpp
    src/vm.cpp
)
target_include_directories(emojineer_core PUBLIC include)
target_link_libraries(emojineer_core PUBLIC ICU::uc ICU::i18n)
target_compile_options(emojineer_core PRIVATE -Wall -Wextra -Wpedantic)

add_executable(emojineer src/main.cpp)
target_link_libraries(emojineer PRIVATE emojineer_core)

include(CTest)
if(BUILD_TESTING)
    add_executable(emojineer_tests tests/tests.cpp)
    target_link_libraries(emojineer_tests PRIVATE emojineer_core)
    add_test(NAME emojineer_tests COMMAND emojineer_tests)
endif()

cmake_minimum_required(VERSION 3.16)
project(myshell CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clangd/IDE tooling

add_executable(myshell
    src/main.cpp
    src/exec_builtin.cpp
    src/executor.cpp
    src/functions.cpp
    src/history.cpp
    src/reader.cpp
    src/shell_ui.cpp
    src/tokenizer.cpp
    src/prompt.cpp
    src/redirection.cpp
    src/builtins.cpp
    src/path_search.cpp
)

target_include_directories(myshell PRIVATE include)
target_compile_options(myshell PRIVATE -Wall -Wextra)

if(BUILD_TESTING)
    add_executable(tokenizer_tests
        tests/tokenizer_tests.cpp
        src/tokenizer.cpp
    )
    target_include_directories(tokenizer_tests PRIVATE include)
    target_compile_options(tokenizer_tests PRIVATE -Wall -Wextra)
    add_test(NAME tokenizer_parser COMMAND tokenizer_tests)
endif()

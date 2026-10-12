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
    src/explain.cpp
    src/explain_ai.cpp
    src/functions.cpp
    src/history.cpp
    src/job_control.cpp
    src/reader.cpp
    src/shell_ui.cpp
    src/shell_state.cpp
    src/tokenizer.cpp
    src/prompt.cpp
    src/redirection.cpp
    src/builtins.cpp
    src/path_search.cpp
)

find_program(MYSHELL_CURL_EXECUTABLE curl)
if(NOT MYSHELL_CURL_EXECUTABLE)
    set(MYSHELL_CURL_EXECUTABLE "/usr/bin/curl")
    message(STATUS "curl was not found; local explain modes work, AI mode needs curl at runtime")
endif()
target_compile_definitions(myshell PRIVATE MYSHELL_CURL_EXECUTABLE="${MYSHELL_CURL_EXECUTABLE}")
target_include_directories(myshell PRIVATE include)
target_compile_options(myshell PRIVATE -Wall -Wextra)

if(BUILD_TESTING)
    add_executable(tokenizer_tests
        tests/tokenizer_tests.cpp
        src/shell_state.cpp
        src/tokenizer.cpp
    )
    target_include_directories(tokenizer_tests PRIVATE include)
    target_compile_options(tokenizer_tests PRIVATE -Wall -Wextra)
    add_test(NAME tokenizer_parser COMMAND tokenizer_tests)

    add_executable(shell_status_tests tests/shell_status_tests.cpp)
    target_compile_options(shell_status_tests PRIVATE -Wall -Wextra)
    add_test(
        NAME shell_exit_status
        COMMAND shell_status_tests $<TARGET_FILE:myshell>
    )

    add_executable(explain_tests tests/explain_tests.cpp)
    target_compile_options(explain_tests PRIVATE -Wall -Wextra)
    add_test(NAME shell_explain COMMAND explain_tests $<TARGET_FILE:myshell>)
    set_tests_properties(shell_explain PROPERTIES TIMEOUT 30)

    add_executable(fake_openai_client tests/fake_openai_client.cpp)
    target_include_directories(fake_openai_client PRIVATE include)
    target_compile_options(fake_openai_client PRIVATE -Wall -Wextra)
    add_executable(explain_ai_tests tests/explain_ai_tests.cpp src/explain_ai.cpp)
    target_include_directories(explain_ai_tests PRIVATE include)
    target_compile_options(explain_ai_tests PRIVATE -Wall -Wextra)
    target_compile_definitions(explain_ai_tests PRIVATE MYSHELL_CURL_EXECUTABLE="$<TARGET_FILE:fake_openai_client>")
    add_dependencies(explain_ai_tests fake_openai_client)
    add_test(NAME shell_explain_ai COMMAND explain_ai_tests)
    set_tests_properties(shell_explain_ai PROPERTIES TIMEOUT 30)

    add_executable(interactive_signal_tests tests/interactive_signal_tests.cpp)
    target_compile_options(interactive_signal_tests PRIVATE -Wall -Wextra)
    find_library(UTIL_LIBRARY NAMES util)
    if(UTIL_LIBRARY)
        target_link_libraries(interactive_signal_tests PRIVATE ${UTIL_LIBRARY})
    endif()
    add_test(
        NAME interactive_signals
        COMMAND interactive_signal_tests $<TARGET_FILE:myshell>
    )
endif()

cmake_minimum_required(VERSION 3.16)
project(myshell CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)  # for clangd/IDE tooling

add_executable(myshell
    src/main.cpp
    src/reader.cpp
    src/tokenizer.cpp
    src/shell.cpp
)

target_include_directories(myshell PRIVATE include)
target_compile_options(myshell PRIVATE -Wall -Wextra)
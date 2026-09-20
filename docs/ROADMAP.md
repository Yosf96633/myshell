# Project roadmap

This roadmap describes the intended direction of `myShell`. It is a guide, not
a fixed release schedule.

## Foundation

- [x] Create a C++20 project structure.
- [x] Split a simple command line on whitespace.
- [x] Display an interactive prompt and read commands until end-of-file.
- [x] Show the username, hostname, and working directory in a colored prompt.
- [x] Ignore empty input and display a fresh prompt.
- [x] Complete the reader component referenced by CMake.
- [x] Include every implemented component in the CMake target.
- [x] Add a registry for dispatching builtin commands.
- [x] Exit when the user enters the `exit` builtin.
- [ ] Add automated tokenizer and parser tests with CTest.

## Command parsing

- [ ] Preserve quoted strings as single arguments.
- [ ] Support escaped characters.
- [ ] Parse environment-variable expansion.
- [ ] Represent commands with a dedicated parsed-command type.
- [ ] Report incomplete quotes and other syntax errors clearly.

## Execution

- [x] Launch external programs with arguments.
- [ ] Return useful exit statuses.
- [x] Implement the `cd` builtin.
- [x] Implement the `pwd` builtin.
- [x] Implement the basic `echo` builtin.
- [x] Implement the `exit` builtin.
- [x] Search for executables using `PATH`.
- [x] Cache executable paths and expose them through a `hash` builtin.
- [ ] Handle common process and system-call failures.

## Shell features

- [ ] Add input and output redirection.
- [ ] Connect commands with pipelines.
- [ ] Support environment assignment and expansion.
- [ ] Handle signals correctly in interactive mode.
- [ ] Add foreground and background job control.
- [ ] Add command history and line editing.

## Documentation and quality

- [ ] Add architecture documentation as the parser and executor stabilize.
- [ ] Run formatting and static analysis in continuous integration.
- [ ] Document supported platforms and known limitations.
- [ ] Select and add an open-source license if desired.

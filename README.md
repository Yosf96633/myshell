# myShell

`myShell` is a Unix-like command shell written in C++20. It combines a
quote-aware parser with external command execution, pipelines, redirections,
environment expansion, aliases, shell functions, command history, signal
handling, and foreground/background job control.

The project is designed as a practical systems-programming implementation with
clear component boundaries for parsing, execution, file descriptors, process
groups, and terminal ownership.

## Features

- Interactive colored prompt showing the user, host, and working directory
- Single and double quotes, backslash escaping, and clear syntax errors
- Parameter expansion for `$NAME`, `${NAME}`, `$$`, and `$?`
- Persistent and command-scoped environment assignments
- Input, output, append, descriptor-duplication, and descriptor-close
  redirections
- Concurrent pipelines with final-stage exit status reporting
- External command lookup through `PATH` with a reusable command cache
- Command aliases and simple single-line shell functions
- Session command history with basic arrow-key line editing
- Interactive `Ctrl+C`, `Ctrl+\`, and `Ctrl+Z` handling
- Foreground and background jobs with `&`, `jobs`, `fg`, and `bg`
- Automated parser, status, signal, and pseudo-terminal integration tests

## Example session

```text
$ ./build/myshell
yosf@computer:/home/yosf/Desktop/my-shell$ pwd
/home/yosf/Desktop/my-shell
yosf@computer:/home/yosf/Desktop/my-shell$ NAME=world
yosf@computer:/home/yosf/Desktop/my-shell$ echo "hello $NAME" | tr a-z A-Z
HELLO WORLD
yosf@computer:/home/yosf/Desktop/my-shell$ sleep 30 &
[1] 12345
yosf@computer:/home/yosf/Desktop/my-shell$ jobs
[1] Running    sleep 30
yosf@computer:/home/yosf/Desktop/my-shell$ fg %1
sleep 30
^C
```

The exact prompt values and process ID depend on the environment.

## Built-in commands

| Command | Behavior |
| --- | --- |
| `alias [-p] [name[=value] ...]` | Defines or displays command aliases. |
| `bg [job]` | Continues a stopped job in the background. |
| `cd <directory>` | Changes the shell's working directory. |
| `declare -f\|-F [name ...]` | Displays shell-function definitions or names. |
| `echo [arguments...]` | Prints its arguments separated by spaces. |
| `exec [command [argument ...]]` | Replaces the shell or applies persistent redirections. |
| `exit [status]` | Exits with an explicit or previous command status. |
| `fg [job]` | Continues a job in the foreground. |
| `hash [options] [name ...]` | Displays or modifies the external-command path cache. |
| `help [-dms] [pattern ...]` | Displays help for supported builtins. |
| `history` | Displays commands entered during the current session. |
| `jobs` | Lists running and stopped jobs. |
| `pwd` | Prints the current working directory. |
| `type [-afptP] name ...` | Reports how command names would be resolved. |
| `unset -f [name ...]` | Removes shell-function definitions. |

Run `help` to list built-ins or `help <name>` for command-specific details.
Alias values may contain multiple words when quoted, for example
`alias ll='ls -l'`.

See the [roadmap](docs/ROADMAP.md) for planned project work.

## Requirements

- A C++20-compatible compiler (GCC 10+, Clang 10+, or equivalent)
- CMake 3.16 or newer
- A Unix-like environment with POSIX process, signal, file-descriptor, and
  terminal APIs

## Build and run

Configure and build the project out of source with CMake:

```bash
cmake -S . -B build
cmake --build build
./build/myshell
```

Alternatively, compile all sources directly:

```bash
g++ -std=c++20 -Wall -Wextra -Iinclude \
    src/*.cpp \
    -o myshell
./myshell
```

## Testing

Configure the project with testing enabled, build it, and run the complete test
suite:

```bash
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

The suite covers:

- Tokenization, quoting, expansion, assignments, redirections, and pipelines
- Exit statuses, external execution, environment behavior, and functions
- Interactive signals, process groups, terminal handoff, and job control

The interactive tests use pseudo-terminals so they exercise the same terminal
semantics as a real shell session.

## Project layout

For the component boundaries and execution flow, see the
[architecture documentation](docs/ARCHITECTURE.md).

```text
.
├── CMakeLists.txt
├── include/myshell/
│   ├── builtins.hpp
│   ├── executor.hpp
│   ├── job_control.hpp
│   ├── parsed_command.hpp
│   ├── path_search.hpp
│   ├── redirection.hpp
│   └── ...
├── src/
│   ├── builtins.cpp
│   ├── executor.cpp
│   ├── job_control.cpp
│   ├── main.cpp
│   ├── path_search.cpp
│   ├── reader.cpp
│   ├── redirection.cpp
│   ├── tokenizer.cpp
│   └── ...
└── docs/
    ├── ARCHITECTURE.md
    └── ROADMAP.md
```

- `src/main.cpp` runs the interactive loop and dispatches commands.
- `src/tokenizer.cpp` parses commands, pipelines, expansions, assignments, and
  redirections into the dedicated parsed-command model.
- `src/executor.cpp` dispatches built-ins and functions or launches child
  processes and pipelines.
- `src/builtins.cpp` contains the built-in registry, implementations, help
  topics, and alias expansion.
- `src/job_control.cpp` tracks jobs and manages process groups and terminal
  ownership.
- `src/redirection.cpp` applies child redirections and transactional parent
  redirections.
- `src/path_search.cpp` resolves external commands and maintains their cached
  paths and hit counts.
- `src/reader.cpp` displays a supplied prompt and reads a line, using an empty
  `std::optional` to signal end-of-file.
- `include/myshell/` contains public declarations for shell components.

## Contributing

Contributions are welcome while the project takes shape. Read
[CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow and coding
expectations.

## License

No license has been selected yet. Until one is added, all rights are reserved
by the project owner.

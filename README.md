# myShell

`myShell` is an early-stage Unix-like shell written in C++20. It currently
provides a colored, context-aware prompt, reads commands one line at a time,
splits each non-empty line into whitespace-separated tokens, and supports a
small builtin-command system.

## Current status

The executable repeatedly displays a prompt containing the username, hostname,
and current working directory. The `user@host` portion is green and the path is
blue in a compatible terminal. Each entered line is tokenized and dispatched
through the builtin-command registry.

```text
$ ./build/myshell
yosf@computer:/home/yosf/Desktop/my-shell$ pwd
/home/yosf/Desktop/my-shell
yosf@computer:/home/yosf/Desktop/my-shell$ cd /tmp
yosf@computer:/tmp$ pwd
/tmp
yosf@computer:/tmp$ exit
```

The exact username, hostname, and path depend on the current environment.

### Builtin commands

| Command | Behavior |
| --- | --- |
| `cd <directory>` | Changes the current working directory. |
| `pwd` | Prints the current working directory. |
| `echo [arguments...]` | Prints its arguments separated by spaces. |
| `exit` | Exits the shell. |
| `jobs` | Lists running and stopped jobs. |
| `fg [job]` | Continues a job in the foreground. |
| `bg [job]` | Continues a stopped job in the background. |
| `hash [options] [name ...]` | Displays or modifies the external-command path cache. |
| `type [options] name...` | Reports whether names are aliases, builtins, or executable files. |
| `alias [-p] [name[=value] ...]` | Defines or displays command aliases. |
| `help [-dms] [pattern ...]` | Displays help for supported builtins. |

The `hash` builtin supports Bash-style `-d`, `-l`, `-p pathname`, `-r`, and
`-t` options. Its default display includes both the cached path and its hit
count.

`type` supports `-a`, `-f`, `-p`, `-P`, and `-t`; `alias` supports `-p`; and
`help` supports `-d`, `-m`, and `-s`. Alias values may contain multiple words
when quoted, for example `alias ll='ls -l'`.

Pressing Enter on an empty line displays another prompt. Pressing `Ctrl+D`
sends end-of-file and also exits the program cleanly.

External commands are resolved using `PATH`, cached, and executed in a child
process. Basic single quotes, double quotes, backslash escaping, pipelines,
redirections, environment assignments, and variable expansion are supported.
In interactive mode, `Ctrl+C` and `Ctrl+\` affect the foreground command
without terminating the shell. A trailing `&` starts a background job;
`Ctrl+Z` stops the foreground job; and `jobs`, `fg`, and `bg` inspect or resume
jobs. See the [roadmap](docs/ROADMAP.md) for the planned direction.

## Requirements

- A C++20-compatible compiler (GCC 10+, Clang 10+, or equivalent)
- CMake 3.16 or newer
- A Unix-like environment providing `gethostname()`, `getcwd()`, and `chdir()`

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

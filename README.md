# myShell

`myShell` is an early-stage Unix-like shell written in C++20. It currently
provides a colored, context-aware prompt, reads commands one line at a time,
splits each non-empty line into whitespace-separated tokens, and supports a
small builtin-command system.

## Current status

The executable repeatedly displays a prompt containing the username, hostname,
and current working directory. The `user@host` portion is green and the path is
blue in a compatible terminal. Each entered line is tokenized and printed.

The `cd` builtin changes the shell's working directory, which is reflected in
the next prompt:

```text
$ ./myshell
yosf@computer:/home/yosf/Desktop/my-shell$ cd /tmp
parsed 2 token(s): [cd] [/tmp]
yosf@computer:/tmp$
```

The exact username, hostname, and path depend on the current environment.

Pressing Enter on an empty line displays another prompt. Pressing `Ctrl+D`
sends end-of-file and exits the program cleanly.

External commands are recognized as non-builtins but are not executed yet. For
example, entering `ls` prints `not a builtin (yet): ls`. Quoting, escaping,
additional builtins, pipes, redirection, and job control are also not
implemented. See the [roadmap](docs/ROADMAP.md) for the planned direction.

## Requirements

- A C++20-compatible compiler (GCC 10+, Clang 10+, or equivalent)
- CMake 3.16 or newer for the planned CMake workflow
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
    src/main.cpp src/builtins.cpp src/prompt.cpp \
    src/reader.cpp src/tokenizer.cpp \
    -o myshell
./myshell
```

## Project layout

```text
.
├── CMakeLists.txt
├── include/myshell/
│   ├── builtins.hpp
│   ├── prompt.hpp
│   ├── reader.hpp
│   └── tokenizer.hpp
├── src/
│   ├── builtins.cpp
│   ├── main.cpp
│   ├── prompt.cpp
│   ├── reader.cpp
│   └── tokenizer.cpp
└── docs/
    └── ROADMAP.md
```

- `src/main.cpp` runs the interactive loop and dispatches builtin commands.
- `src/builtins.cpp` registers builtins and implements `cd`.
- `src/prompt.cpp` builds the colored prompt from the user, host, and current
  directory.
- `src/reader.cpp` displays a supplied prompt and reads a line, using an empty
  `std::optional` to signal end-of-file.
- `src/tokenizer.cpp` contains the initial whitespace-based tokenizer.
- `include/myshell/` contains public declarations for shell components.

## Contributing

Contributions are welcome while the project takes shape. Read
[CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow and coding
expectations.

## License

No license has been selected yet. Until one is added, all rights are reserved
by the project owner.

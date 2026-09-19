# myShell

`myShell` is an early-stage Unix-like shell written in C++20. The project is
currently focused on building the command-processing pipeline one piece at a
time, beginning with tokenizing a command line into arguments.

## Current status

The current executable tokenizes the hard-coded command `ls -la /tmp` and
prints the resulting tokens:

```text
parsed 3 token(s): [ls] [-la] [/tmp]
```

Interactive input, command execution, quoting, pipes, redirection, and job
control are not implemented yet. See the [roadmap](docs/ROADMAP.md) for the
planned direction.

## Requirements

- A C++20-compatible compiler (GCC 10+, Clang 10+, or equivalent)
- CMake 3.16 or newer for the planned CMake workflow
- A Unix-like environment for future process-management features

## Build and run

At this stage, build the implemented sources directly:

```bash
g++ -std=c++20 -Wall -Wextra -Iinclude \
    src/main.cpp src/tokenizer.cpp \
    -o myshell
./myshell
```

The `CMakeLists.txt` already describes the intended application structure, but
it also references `src/reader.cpp` and `src/shell.cpp`, which have not been
added yet. Once those components exist, the standard out-of-source workflow
will be:

```bash
cmake -S . -B build
cmake --build build
./build/myshell
```

## Project layout

```text
.
├── CMakeLists.txt
├── include/myshell/
│   ├── reader.hpp
│   └── tokenizer.hpp
├── src/
│   ├── main.cpp
│   └── tokenizer.cpp
└── docs/
    └── ROADMAP.md
```

- `src/main.cpp` is the current program entry point.
- `src/tokenizer.cpp` contains the initial whitespace-based tokenizer.
- `include/myshell/` contains public declarations for shell components.

## Contributing

Contributions are welcome while the project takes shape. Read
[CONTRIBUTING.md](CONTRIBUTING.md) for the development workflow and coding
expectations.

## License

No license has been selected yet. Until one is added, all rights are reserved
by the project owner.

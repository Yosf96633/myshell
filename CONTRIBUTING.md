# Contributing to myShell

Thank you for helping improve `myShell`. The codebase is still small, so focused
changes with a clear purpose are easiest to review.

## Development workflow

1. Create a branch for your change.
2. Keep the change focused on one feature or fix.
3. Build with warnings enabled.
4. Add or update tests when a test target becomes available.
5. Update the README or roadmap when behavior or project scope changes.

Until all sources listed in `CMakeLists.txt` are present, compile the implemented
prototype directly:

```bash
g++ -std=c++20 -Wall -Wextra -Wpedantic -Iinclude \
    src/main.cpp src/tokenizer.cpp \
    -o myshell
./myshell
```

After the reader and shell components are added, use the CMake build:

```bash
cmake -S . -B build
cmake --build build
```

## Code guidelines

- Target C++20 and keep compiler warnings enabled.
- Prefer the C++ standard library over platform-specific code where practical.
- Keep declarations in `include/myshell/` and implementations in `src/`.
- Avoid `using namespace` directives in header files.
- Document user-visible behavior and non-obvious design decisions.
- Keep commits small and use concise, descriptive commit messages.

## Testing changes

There is no automated test suite yet. For now, contributors should:

- compile with `-Wall -Wextra -Wpedantic`;
- run the executable and verify its output;
- manually check empty input, repeated whitespace, and multiple arguments when
  changing the tokenizer.

Adding a CTest-based test target is an especially useful contribution; it is
also tracked in the [roadmap](docs/ROADMAP.md).

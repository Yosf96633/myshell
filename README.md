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
- Deterministic `explain` breakdowns with saved execution results
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
| `explain [-d] [-v|-b] [--ai] [--] [command ...]` | Runs and explains a whole command/pipeline, or explains the last execution without rerunning it. |
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

## Explaining commands

Use the unquoted `explain` prefix to execute a command normally, then see a
plain-language breakdown and its recorded result:

```sh
explain cd /tmp
explain cat access.log | grep '404' > errors.txt 2>&1
explain -d NAME=world echo "hello $NAME"
explain -v echo hello | cat > /tmp/hello.txt
explain -dv ls -l /tmp
```

The prefix covers the entire pipeline. Command redirections apply to the
command, and the explanation appears after execution with the shell's output
restored. Redirections are described left to right: `>file 2>&1` sends output
and errors to the file; `2>&1 >file` keeps errors on the original output stream.
Descriptors 0, 1, and 2 mean standard input, output, and error; other descriptors
and descriptor closing are also supported.

Enter `explain` at the next prompt to inspect the last execution without running
it again. Repeated inspections keep that record and preserve `$?`.
`explain -d` adds execution location, expanded argument descriptions, final
file descriptor destinations, restoration behavior, and all saved function
steps. `--detail` and the original `--verbose` are aliases for `-d`.
`explain -v` draws an ASCII diagram; `-b` and `--visual` are aliases. Combine
flags as `-dv`, or use them with a target command. Without a target, these
flags change how the saved execution is displayed without rerunning it. Records
include the original source, arguments after expansion, aliases after
resolution, command type, foreground pipeline stage statuses, and working
directory changes. A syntax error is saved as a failed parse rather than
showing an older command. Definitions are saved as registration results.

Local explanations use parser rules and execution records, with no API calls. Familiar utility descriptions describe their usual purpose; arbitrary
executables get a structural explanation of their arguments, connections, and
status. Internal program actions and filesystem effects are not traced.
Functions executed in the parent shell include their actual command steps;
steps inside pipeline/background child processes are not transferred back.
Background commands report launch status, not completion. Successful `exec`
replaces the shell, so it cannot print an explanation afterward; persistent
`exec` redirections also affect subsequent explanations.

### Optional OpenAI explanations

C++ sends a request to the OpenAI Responses API through the `curl` HTTPS client.
AI mode is explicit: `--ai` (or `-a`) adds an explanation to the recorded local
facts. `-d` and diagrams work offline and do not require a key.

Set the key in your **Bash terminal before launching myShell**. A hidden prompt
keeps the actual key out of the command you type:

```bash
read -rs -p 'OpenAI API key: ' OPENAI_API_KEY
export OPENAI_API_KEY
./build/myshell
```

Then, inside myShell:

```sh
explain --ai -d ls -l /tmp
explain --ai -dv echo hello | cat > /tmp/hello.txt 2>&1
explain --ai -d
```

The default model is `gpt-4.1-mini`; set `MYSHELL_EXPLAIN_MODEL` before launch
(or with a persistent shell assignment) to use another Responses model that
supports strict structured outputs. API access and billing are separate from
ChatGPT subscriptions.

Each request contains at most 8,000 bytes of compact command/trace context,
including the saved command, a bounded set of arguments and ordered
redirections, statuses, working directory changes, a few function steps, and
up to three recent commands saved at execution time. Inputs may be clipped.
It excludes file contents, program output, and a full environment dump. The
configured API key and common secret assignments/options are redacted;
command text may still contain other private information, so use `--ai` only
for context you intend to send to OpenAI.

Requests use `store: false` and strict JSON-schema output, with an output limit
of 900 tokens (1,800 with `-d`). The client validates the returned fields and
renders the summary, steps, expected behavior, and caveats in the terminal.
Diagrams remain derived from the recorded descriptor setup. Identical requests
are cached in memory (up to 16 entries); new requests consume API tokens.

Missing credentials, HTTP errors, timeouts, refusals, or malformed responses
retain the local explanation and the executed command's exit status. The
request has a 5-second connection timeout and a 30-second total timeout;
Ctrl+C cancels an interactive request. Credentials travel over a private pipe,
not shell commands, process arguments, or temporary files.

`curl` must be available at runtime for AI mode. JSON parsing uses the vendored
nlohmann/json 3.11.3 header; its MIT license is included under
`include/third_party/nlohmann/`. No package installation or network download is
needed to build.

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
- Explain execution, detail/ASCII modes, alias/function resolution, pipes, and descriptor order
- OpenAI request/schema fixtures, context limits, secret redaction, caching, and failure fallback
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

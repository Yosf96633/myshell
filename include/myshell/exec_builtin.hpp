#pragma once

#include "myshell/parsed_command.hpp"

// Apply exec redirections and optionally replace the shell process.
int run_exec_builtin(const ParsedCommand& command);

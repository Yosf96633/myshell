#pragma once

#include <string>
#include <vector>

// Execute a parsed simple command and return its shell-style exit status.
int execute_command(std::vector<std::string> tokens, int function_depth = 0);

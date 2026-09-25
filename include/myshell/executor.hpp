#pragma once

#include "myshell/parsed_command.hpp"

#include <string>

// Execute a parsed simple command and return its shell-style exit status.
int execute_command(
    ParsedCommand command,
    int function_depth = 0,
    const std::string& source = {});

// Execute a pipeline and return the exit status of its final command.
int execute_pipeline(ParsedPipeline pipeline, int function_depth = 0);

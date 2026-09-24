#pragma once

#include "myshell/parsed_command.hpp"

#include <optional>
#include <string>

struct ParseResult {
    std::optional<ParsedCommand> command;
    std::string error;

    bool has_error() const {
        return !error.empty();
    }
};

struct PipelineParseResult {
    std::optional<ParsedPipeline> pipeline;
    std::string error;

    bool has_error() const {
        return !error.empty();
    }
};

// Parse one simple command. An empty input has no command and no error.
ParseResult parse_command(const std::string& line);

// Parse one or more commands connected by unquoted `|' operators.
PipelineParseResult parse_pipeline(const std::string& line);

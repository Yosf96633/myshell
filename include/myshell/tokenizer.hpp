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

// Parse one simple command. An empty input has no command and no error.
ParseResult parse_command(const std::string& line);

#pragma once

#include <string>
#include <vector>

// The parser's representation of one executable command.
struct ParsedCommand {
    std::string name;
    std::vector<std::string> arguments;
    bool present = false;

    bool empty() const {
        return !present;
    }
};

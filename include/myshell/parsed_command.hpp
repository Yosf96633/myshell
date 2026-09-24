#pragma once

#include <string>
#include <vector>

enum class RedirectionType {
    input,
    output,
    append,
    duplicate,
    close,
};

struct Redirection {
    int target_fd = -1;
    RedirectionType type = RedirectionType::output;
    std::string path;
    int source_fd = -1;
};

// The parser's representation of one executable command.
struct ParsedCommand {
    std::string name;
    std::vector<std::string> arguments;
    std::vector<Redirection> redirections;
    bool present = false;

    bool empty() const {
        return !present;
    }
};

struct ParsedPipeline {
    std::vector<ParsedCommand> commands;

    bool empty() const {
        return commands.empty();
    }
};

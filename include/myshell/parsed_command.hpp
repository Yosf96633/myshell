#pragma once

#include <string>
#include <utility>
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

using EnvironmentAssignment = std::pair<std::string, std::string>;

// The parser's representation of one executable command.
struct ParsedCommand {
    std::string name;
    std::vector<std::string> arguments;
    std::vector<EnvironmentAssignment> environment_assignments;
    std::vector<Redirection> redirections;
    bool present = false;

    bool empty() const {
        return !present;
    }
};

struct ParsedPipeline {
    std::vector<ParsedCommand> commands;
    std::string source;
    bool background = false;

    bool empty() const {
        return commands.empty();
    }
};

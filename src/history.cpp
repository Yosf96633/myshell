#include "myshell/history.hpp"

#include <cctype>
#include <algorithm>

std::vector<std::string> command_history;

void add_history_entry(const std::string& line) {
    const bool has_non_whitespace = std::any_of(
        line.begin(), line.end(),
        [](unsigned char character) { return !std::isspace(character); });

    if (has_non_whitespace) {
        command_history.push_back(line);
    }
}

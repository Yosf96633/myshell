#pragma once

#include <string>
#include <vector>

// The commands entered during this shell session, in execution order.
extern std::vector<std::string> command_history;

// Store a command unless it is empty or contains only whitespace.
void add_history_entry(const std::string& line);

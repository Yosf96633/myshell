#pragma once

#include <optional>
#include <string>

using namespace std;

// Prints `prompt`, then waits for the user to type a line and press Enter.
// Returns the line if there was input.
// Returns "nothing" (std::nullopt) if the user pressed Ctrl+D (no more input).
optional<string> read_line(const string& prompt);
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct SimpleCommand {
    std::string source;
    std::vector<std::string> words;
};

struct ShellFunction {
    std::string name;
    std::vector<SimpleCommand> commands;
};

enum class FunctionDefinitionResult {
    not_definition,
    registered,
    syntax_error,
};

extern std::unordered_map<std::string, ShellFunction> shell_functions;

// Parse and register a single-line definition such as:
//     greet() { echo hello; pwd; }
// If the line resembles a definition but is invalid, `error` describes why.
FunctionDefinitionResult register_function_definition(
    const std::string& line,
    std::string& error);

bool is_shell_function(const std::string& name);
bool remove_shell_function(const std::string& name);
std::vector<std::string> sorted_function_names();
void print_function_definition(const ShellFunction& function);

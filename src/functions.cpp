#include "myshell/functions.hpp"

#include "myshell/tokenizer.hpp"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <utility>

using namespace std;

unordered_map<string, ShellFunction> shell_functions;

namespace {

string trim(const string& value) {
    const size_t first = value.find_first_not_of(" \t\r\n");
    if (first == string::npos) {
        return {};
    }

    const size_t last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

void skip_whitespace(const string& line, size_t& position) {
    while (position < line.size()
           && isspace(static_cast<unsigned char>(line[position]))) {
        ++position;
    }
}

bool valid_function_name(const string& name) {
    if (name.empty()
        || !(isalpha(static_cast<unsigned char>(name[0])) || name[0] == '_')) {
        return false;
    }

    return all_of(name.begin() + 1, name.end(), [](unsigned char character) {
        return isalnum(character) || character == '_';
    });
}

bool split_function_body(
    const string& body,
    vector<SimpleCommand>& commands,
    string& error) {
    enum class QuoteMode { none, single, double_quote };

    QuoteMode quote = QuoteMode::none;
    bool escaped = false;
    size_t command_start = 0;

    for (size_t i = 0; i < body.size(); ++i) {
        const char character = body[i];

        if (escaped) {
            escaped = false;
            continue;
        }

        if (quote != QuoteMode::single && character == '\\') {
            escaped = true;
            continue;
        }

        if (quote == QuoteMode::single) {
            if (character == '\'') {
                quote = QuoteMode::none;
            }
            continue;
        }

        if (quote == QuoteMode::double_quote) {
            if (character == '"') {
                quote = QuoteMode::none;
            }
            continue;
        }

        if (character == '\'') {
            quote = QuoteMode::single;
        } else if (character == '"') {
            quote = QuoteMode::double_quote;
        } else if (character == ';') {
            const string source = trim(body.substr(command_start, i - command_start));
            if (!source.empty()) {
                commands.push_back({source, tokenize(source)});
            }
            command_start = i + 1;
        }
    }

    if (escaped) {
        error = "function definition ends with an escape character";
        return false;
    }
    if (quote != QuoteMode::none) {
        error = "unclosed quote in function body";
        return false;
    }

    if (!trim(body.substr(command_start)).empty()) {
        error = "expected `;' before `}'";
        return false;
    }
    if (commands.empty()) {
        error = "function body must contain a command";
        return false;
    }

    return true;
}

} // namespace

FunctionDefinitionResult register_function_definition(
    const string& line,
    string& error) {
    size_t position = 0;
    skip_whitespace(line, position);

    const size_t name_start = position;
    while (position < line.size()
           && (isalnum(static_cast<unsigned char>(line[position]))
               || line[position] == '_')) {
        ++position;
    }

    const string name = line.substr(name_start, position - name_start);
    size_t after_name = position;
    skip_whitespace(line, after_name);
    if (after_name >= line.size() || line[after_name] != '(') {
        return FunctionDefinitionResult::not_definition;
    }

    position = after_name + 1;
    skip_whitespace(line, position);
    if (position >= line.size() || line[position] != ')') {
        error = "expected `)' after function name";
        return FunctionDefinitionResult::syntax_error;
    }

    if (!valid_function_name(name)) {
        error = "`" + name + "' is not a valid function name";
        return FunctionDefinitionResult::syntax_error;
    }

    ++position;
    skip_whitespace(line, position);
    if (position >= line.size() || line[position] != '{') {
        error = "expected `{' before function body";
        return FunctionDefinitionResult::syntax_error;
    }

    const size_t body_start = ++position;
    enum class QuoteMode { none, single, double_quote };
    QuoteMode quote = QuoteMode::none;
    bool escaped = false;
    size_t closing_brace = string::npos;

    for (; position < line.size(); ++position) {
        const char character = line[position];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (quote != QuoteMode::single && character == '\\') {
            escaped = true;
            continue;
        }
        if (quote == QuoteMode::single) {
            if (character == '\'') quote = QuoteMode::none;
            continue;
        }
        if (quote == QuoteMode::double_quote) {
            if (character == '"') quote = QuoteMode::none;
            continue;
        }
        if (character == '\'') {
            quote = QuoteMode::single;
        } else if (character == '"') {
            quote = QuoteMode::double_quote;
        } else if (character == '}') {
            closing_brace = position;
            break;
        }
    }

    if (closing_brace == string::npos) {
        error = "expected `}' after function body";
        return FunctionDefinitionResult::syntax_error;
    }

    position = closing_brace + 1;
    skip_whitespace(line, position);
    if (position != line.size()) {
        error = "unexpected text after function definition";
        return FunctionDefinitionResult::syntax_error;
    }

    ShellFunction function;
    function.name = name;
    const string body = line.substr(body_start, closing_brace - body_start);
    if (!split_function_body(body, function.commands, error)) {
        return FunctionDefinitionResult::syntax_error;
    }

    shell_functions[name] = move(function);
    return FunctionDefinitionResult::registered;
}

bool is_shell_function(const string& name) {
    return shell_functions.count(name) > 0;
}

bool remove_shell_function(const string& name) {
    return shell_functions.erase(name) > 0;
}

vector<string> sorted_function_names() {
    vector<string> names;
    names.reserve(shell_functions.size());
    for (const auto& [name, function] : shell_functions) {
        (void)function;
        names.push_back(name);
    }
    sort(names.begin(), names.end());
    return names;
}

void print_function_definition(const ShellFunction& function) {
    cout << function.name << " ()\n{\n";
    for (size_t i = 0; i < function.commands.size(); ++i) {
        cout << "    " << function.commands[i].source;
        if (i + 1 < function.commands.size()) {
            cout << ';';
        }
        cout << '\n';
    }
    cout << "}\n";
}

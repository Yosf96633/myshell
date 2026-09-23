#include "myshell/tokenizer.hpp"

#include <unistd.h>

#include <cctype>
#include <cstdlib>
#include <iterator>
#include <utility>

using namespace std;

namespace {

bool is_variable_start(char character) {
    const unsigned char byte = static_cast<unsigned char>(character);
    return isalpha(byte) || character == '_';
}

bool is_variable_character(char character) {
    const unsigned char byte = static_cast<unsigned char>(character);
    return isalnum(byte) || character == '_';
}

enum class ExpansionResult { not_parameter, expanded, error };

// Expand a parameter beginning at line[position] and advance position past it.
ExpansionResult expand_parameter(
    const string& line,
    size_t& position,
    string& word,
    string& error) {
    if (position + 1 >= line.size()) {
        return ExpansionResult::not_parameter;
    }

    const char next = line[position + 1];
    if (next == '$') {
        word += to_string(getpid());
        ++position;
        return ExpansionResult::expanded;
    }

    string name;
    if (next == '{') {
        const size_t closing_brace = line.find('}', position + 2);
        if (closing_brace == string::npos) {
            error = "missing `}' in parameter expansion";
            return ExpansionResult::error;
        }

        name = line.substr(position + 2, closing_brace - position - 2);
        if (name.empty()) {
            error = "empty variable name in parameter expansion";
            return ExpansionResult::error;
        }
        if (!is_variable_start(name.front())) {
            error = "invalid variable name in parameter expansion: " + name;
            return ExpansionResult::error;
        }
        for (char character : name) {
            if (!is_variable_character(character)) {
                error = "invalid variable name in parameter expansion: " + name;
                return ExpansionResult::error;
            }
        }
        position = closing_brace;
    } else {
        if (!is_variable_start(next)) {
            return ExpansionResult::not_parameter;
        }

        size_t end = position + 2;
        while (end < line.size() && is_variable_character(line[end])) {
            ++end;
        }
        name = line.substr(position + 1, end - position - 1);
        position = end - 1;
    }

    if (const char* value = getenv(name.c_str())) {
        word += value;
    }
    return ExpansionResult::expanded;
}

} // namespace

ParseResult parse_command(const string& line) {
    vector<string> tokens;
    string word;
    bool token_started = false;

    enum class QuoteMode { none, single, double_quote };
    QuoteMode quote = QuoteMode::none;

    for (size_t i = 0; i < line.size(); ++i) {
        const char character = line[i];

        if (quote == QuoteMode::single) {
            if (character == '\'') {
                quote = QuoteMode::none;
            } else {
                word += character;
            }
            continue;
        }

        if (quote == QuoteMode::double_quote) {
            if (character == '"') {
                quote = QuoteMode::none;
            } else if (character == '\\') {
                if (i + 1 >= line.size()) {
                    return {nullopt, "trailing escape character"};
                }
                word += line[++i];
            } else if (character == '$') {
                string error;
                const auto expansion = expand_parameter(line, i, word, error);
                if (expansion == ExpansionResult::error) {
                    return {nullopt, move(error)};
                }
                if (expansion == ExpansionResult::not_parameter) {
                    word += character;
                }
            } else {
                word += character;
            }
            continue;
        }

        if (isspace(static_cast<unsigned char>(character))) {
            if (token_started) {
                tokens.push_back(word);
                word.clear();
                token_started = false;
            }
        } else if (character == '\'') {
            quote = QuoteMode::single;
            token_started = true;
        } else if (character == '"') {
            quote = QuoteMode::double_quote;
            token_started = true;
        } else if (character == '\\') {
            if (i + 1 >= line.size()) {
                return {nullopt, "trailing escape character"};
            }
            word += line[++i];
            token_started = true;
        } else if (character == '$') {
            const size_t original_size = word.size();
            string error;
            const auto expansion = expand_parameter(line, i, word, error);
            if (expansion == ExpansionResult::error) {
                return {nullopt, move(error)};
            }
            if (expansion == ExpansionResult::expanded) {
                token_started = token_started || word.size() > original_size;
            } else {
                word += character;
                token_started = true;
            }
        } else {
            word += character;
            token_started = true;
        }
    }

    if (quote == QuoteMode::single) {
        return {nullopt, "unclosed single quote"};
    }
    if (quote == QuoteMode::double_quote) {
        return {nullopt, "unclosed double quote"};
    }

    if (token_started) {
        tokens.push_back(word);
    }

    if (tokens.empty()) {
        return {};
    }

    ParsedCommand command;
    command.name = move(tokens.front());
    command.present = true;
    command.arguments.assign(
        make_move_iterator(tokens.begin() + 1),
        make_move_iterator(tokens.end()));
    return {move(command), {}};
}

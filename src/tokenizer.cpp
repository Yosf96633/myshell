#include "myshell/tokenizer.hpp"
#include "myshell/shell_state.hpp"

#include <unistd.h>

#include <cctype>
#include <charconv>
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
enum class LexTokenType { word, redirection };
enum class LexRedirection {
    input,
    output,
    append,
    duplicate_input,
    duplicate_output,
};

struct LexToken {
    LexTokenType type = LexTokenType::word;
    string text;
    LexRedirection redirection = LexRedirection::output;
    string target_fd;
    bool assignment_word = false;
};

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
    if (next == '?') {
        word += to_string(shell_last_status());
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

bool parse_file_descriptor(const string& text, int fallback, int& result, string& error) {
    if (text.empty()) {
        if (fallback < 0) {
            error = "invalid empty file descriptor";
            return false;
        }
        result = fallback;
        return true;
    }

    const char* first = text.data();
    const char* last = first + text.size();
    const auto parsed = from_chars(first, last, result);
    if (parsed.ec != errc{} || parsed.ptr != last || result < 0) {
        error = "invalid file descriptor: " + text;
        return false;
    }
    return true;
}

} // namespace

ParseResult parse_command(const string& line) {
    vector<LexToken> tokens;
    string word;
    bool token_started = false;
    bool word_can_be_fd = true;
    bool assignment_candidate = true;
    bool assignment_equals_seen = false;

    enum class QuoteMode { none, single, double_quote };
    QuoteMode quote = QuoteMode::none;

    const auto flush_word = [&] {
        if (token_started) {
            tokens.push_back({
                LexTokenType::word,
                move(word),
                {},
                {},
                assignment_candidate && assignment_equals_seen,
            });
            word.clear();
            token_started = false;
            word_can_be_fd = true;
            assignment_candidate = true;
            assignment_equals_seen = false;
        }
    };

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
            flush_word();
        } else if (character == '\'') {
            quote = QuoteMode::single;
            token_started = true;
            word_can_be_fd = false;
            if (!assignment_equals_seen) {
                assignment_candidate = false;
            }
        } else if (character == '"') {
            quote = QuoteMode::double_quote;
            token_started = true;
            word_can_be_fd = false;
            if (!assignment_equals_seen) {
                assignment_candidate = false;
            }
        } else if (character == '\\') {
            if (i + 1 >= line.size()) {
                return {nullopt, "trailing escape character"};
            }
            word += line[++i];
            token_started = true;
            word_can_be_fd = false;
            if (!assignment_equals_seen) {
                assignment_candidate = false;
            }
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
            word_can_be_fd = false;
            if (!assignment_equals_seen) {
                assignment_candidate = false;
            }
        } else if (character == '<' || character == '>') {
            string target_fd;
            if (token_started && word_can_be_fd) {
                target_fd = move(word);
                word.clear();
                token_started = false;
                word_can_be_fd = true;
                assignment_candidate = true;
                assignment_equals_seen = false;
            } else {
                flush_word();
            }

            LexRedirection redirection = character == '<'
                ? LexRedirection::input
                : LexRedirection::output;
            if (i + 1 < line.size() && line[i + 1] == '&') {
                redirection = character == '<'
                    ? LexRedirection::duplicate_input
                    : LexRedirection::duplicate_output;
                ++i;
            } else if (character == '>' && i + 1 < line.size() && line[i + 1] == '>') {
                redirection = LexRedirection::append;
                ++i;
            }
            tokens.push_back({
                LexTokenType::redirection, {}, redirection, move(target_fd), false});
        } else {
            word += character;
            token_started = true;
            if (!assignment_equals_seen) {
                if (character == '=' && !word.empty() && word.size() > 1) {
                    assignment_equals_seen = assignment_candidate;
                } else {
                    const size_t name_position = word.size() - 1;
                    const bool valid = name_position == 0
                        ? is_variable_start(character)
                        : is_variable_character(character);
                    assignment_candidate = assignment_candidate && valid;
                }
            }
            if (!isdigit(static_cast<unsigned char>(character))) {
                word_can_be_fd = false;
            }
        }
    }

    if (quote == QuoteMode::single) {
        return {nullopt, "unclosed single quote"};
    }
    if (quote == QuoteMode::double_quote) {
        return {nullopt, "unclosed double quote"};
    }

    flush_word();

    if (tokens.empty()) {
        return {};
    }

    vector<pair<string, bool>> words;
    ParsedCommand command;
    for (size_t i = 0; i < tokens.size(); ++i) {
        LexToken& token = tokens[i];
        if (token.type == LexTokenType::word) {
            words.emplace_back(move(token.text), token.assignment_word);
            continue;
        }

        if (i + 1 >= tokens.size() || tokens[i + 1].type != LexTokenType::word) {
            return {nullopt, "redirection requires a file or descriptor"};
        }
        string operand = move(tokens[++i].text);

        Redirection redirection;
        const bool redirects_input = token.redirection == LexRedirection::input
            || token.redirection == LexRedirection::duplicate_input;
        const int fallback_fd = redirects_input ? 0 : 1;
        string error;
        if (!parse_file_descriptor(
                token.target_fd, fallback_fd, redirection.target_fd, error)) {
            return {nullopt, move(error)};
        }

        if (token.redirection == LexRedirection::input) {
            redirection.type = RedirectionType::input;
            redirection.path = move(operand);
        } else if (token.redirection == LexRedirection::output) {
            redirection.type = RedirectionType::output;
            redirection.path = move(operand);
        } else if (token.redirection == LexRedirection::append) {
            redirection.type = RedirectionType::append;
            redirection.path = move(operand);
        } else if (operand == "-") {
            redirection.type = RedirectionType::close;
        } else {
            redirection.type = RedirectionType::duplicate;
            if (!parse_file_descriptor(operand, -1, redirection.source_fd, error)) {
                return {nullopt, move(error)};
            }
        }
        command.redirections.push_back(move(redirection));
    }

    size_t word_index = 0;
    while (word_index < words.size() && words[word_index].second) {
        string assignment = move(words[word_index].first);
        const size_t equals = assignment.find('=');
        command.environment_assignments.emplace_back(
            assignment.substr(0, equals), assignment.substr(equals + 1));
        ++word_index;
    }
    if (word_index < words.size()) {
        command.name = move(words[word_index].first);
        ++word_index;
        for (; word_index < words.size(); ++word_index) {
            command.arguments.push_back(move(words[word_index].first));
        }
    }
    command.present = !words.empty() || !command.redirections.empty();
    return {move(command), {}};
}

PipelineParseResult parse_pipeline(const string& line) {
    enum class QuoteMode { none, single, double_quote };

    ParsedPipeline pipeline;
    QuoteMode quote = QuoteMode::none;
    bool escaped = false;
    size_t command_start = 0;

    const auto append_command = [&](size_t command_end, string& error) {
        ParseResult parsed = parse_command(
            line.substr(command_start, command_end - command_start));
        if (parsed.has_error()) {
            error = move(parsed.error);
            return false;
        }
        if (!parsed.command) {
            error = "expected a command near `|'";
            return false;
        }
        pipeline.commands.push_back(move(*parsed.command));
        return true;
    };

    for (size_t i = 0; i < line.size(); ++i) {
        const char character = line[i];
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
        } else if (character == '|') {
            string error;
            if (!append_command(i, error)) {
                return {nullopt, move(error)};
            }
            command_start = i + 1;
        }
    }

    // Let the simple-command parser produce its more specific quote/escape error.
    string error;
    if (!append_command(line.size(), error)) {
        if (pipeline.commands.empty() && line.find_first_not_of(" \t\r\n") == string::npos) {
            return {};
        }
        return {nullopt, move(error)};
    }
    return {move(pipeline), {}};
}

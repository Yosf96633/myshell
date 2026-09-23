#include "myshell/tokenizer.hpp"

#include <unistd.h>

#include <cctype>
#include <cstdlib>

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

// Expand a parameter beginning at line[position]. Returns true when the dollar
// sign introduced a supported parameter and advances position past its name.
bool expand_parameter(const string& line, size_t& position, string& word) {
    if (position + 1 >= line.size()) {
        return false;
    }

    const char next = line[position + 1];
    if (next == '$') {
        word += to_string(getpid());
        ++position;
        return true;
    }

    string name;
    if (next == '{') {
        const size_t closing_brace = line.find('}', position + 2);
        if (closing_brace == string::npos) {
            return false;
        }

        name = line.substr(position + 2, closing_brace - position - 2);
        if (name.empty() || !is_variable_start(name.front())) {
            return false;
        }
        for (char character : name) {
            if (!is_variable_character(character)) {
                return false;
            }
        }
        position = closing_brace;
    } else {
        if (!is_variable_start(next)) {
            return false;
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
    return true;
}

} // namespace

vector<string> tokenize(const string& line) {
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
            } else if (character == '\\' && i + 1 < line.size()) {
                word += line[++i];
            } else if (character == '$' && expand_parameter(line, i, word)) {
                // Opening the quote already marked this token as present, even
                // when the variable is unset or has an empty value.
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
        } else if (character == '\\' && i + 1 < line.size()) {
            word += line[++i];
            token_started = true;
        } else if (character == '$') {
            const size_t original_size = word.size();
            if (expand_parameter(line, i, word)) {
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

    if (token_started) {
        tokens.push_back(word);
    }

    return tokens;
}

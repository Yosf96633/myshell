#include "myshell/tokenizer.hpp"
#include <cctype>

using namespace std;

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

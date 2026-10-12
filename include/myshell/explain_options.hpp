#pragma once

#include <string>

struct ExplainOptions {
    bool detail = false;
    bool visual = false;
    bool ai = false;
};

enum class ExplainOptionResult { accepted, end, help, operand, invalid };

// Shared by the raw prefix parser and the saved-execution builtin.
inline ExplainOptionResult parse_explain_option(const std::string& word, ExplainOptions& options) {
    if (word == "--") return ExplainOptionResult::end;
    if (word == "--help" || word == "-h") return ExplainOptionResult::help;
    if (word == "--detail" || word == "--verbose") options.detail = true;
    else if (word == "--visual") options.visual = true;
    else if (word == "--ai") options.ai = true;
    else if (word.size() > 1 && word[0] == '-' && word[1] != '-') {
        for (std::size_t i = 1; i < word.size(); ++i) {
            if (word[i] == 'd') options.detail = true;
            else if (word[i] == 'v' || word[i] == 'b') options.visual = true;
            else if (word[i] == 'a') options.ai = true;
            else return ExplainOptionResult::invalid;
        }
    } else if (word.size() > 1 && word[0] == '-') return ExplainOptionResult::invalid;
    else return ExplainOptionResult::operand;
    return ExplainOptionResult::accepted;
}

inline constexpr const char* explain_usage =
    "explain [-d] [-v|-b] [--ai] [--] [command ...]";

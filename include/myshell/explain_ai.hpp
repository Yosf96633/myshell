#pragma once

#include "myshell/explain.hpp"

#include <string>
#include <vector>

struct AiCommandExplanation {
    std::string summary;
    std::vector<std::string> steps;
    std::string expected_behavior;
    std::vector<std::string> caveats;
};

// Pure request/response boundaries for fixture testing without paid API calls.
std::string build_explain_ai_request(
    const ExecutionExplanation& record, bool detail,
    const std::string& model, const std::string& api_key);
bool parse_explain_ai_response(
    const std::string& response, AiCommandExplanation& explanation, std::string& error);
void print_ai_explanation(const ExecutionExplanation& record, bool detail);

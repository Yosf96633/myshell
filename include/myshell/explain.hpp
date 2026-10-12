#pragma once

#include "myshell/parsed_command.hpp"

#include <optional>
#include <string>
#include <vector>

struct ExplainedStage {
    ParsedCommand command;
    std::string kind;
    bool observed = false;
    std::optional<int> status;
};

struct ExecutionExplanation {
    ParsedPipeline pipeline;
    std::vector<ExplainedStage> stages;
    std::vector<ExecutionExplanation> children;
    std::string directory_before;
    std::string directory_after;
    std::string note;
    std::vector<std::string> recent_history;
    int status = 0;
    bool job_completed = true;
};

// One frame per pipeline; nested function executions attach to their caller.
class ExplanationFrame {
public:
    explicit ExplanationFrame(const ParsedPipeline& pipeline);
    ~ExplanationFrame();
    ExplanationFrame(const ExplanationFrame&) = delete;
    ExplanationFrame& operator=(const ExplanationFrame&) = delete;
    void finish(int status);
private:
    ExecutionExplanation record_;
    ExecutionExplanation* parent_ = nullptr;
    bool inspection_ = false;
};

void observe_explained_command(const ParsedCommand& command, std::size_t stage);
void observe_explained_statuses(const std::vector<std::optional<int>>& statuses, bool completed);
void observe_explained_error(const std::string& error);
void remember_explanation_note(const std::string& source, const std::string& note, int status);
int builtin_explain(const std::vector<std::string>& arguments);

#include "myshell/explain_ai.hpp"
#include "third_party/nlohmann/json.hpp"

#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace std;
using Json = nlohmann::json;
namespace {
int failures = 0;
void expect(bool condition, const string& message) {
    if (!condition) { cerr << "FAIL: " << message << '\n'; ++failures; }
}
Json envelope(const string& text) {
    return {{"status", "completed"}, {"output", Json::array({
        {{"type", "reasoning"}},
        {{"type", "message"}, {"content", Json::array({{{"type", "output_text"}, {"text", text}}})}}
    })}};
}
string capture(const ExecutionExplanation& record, bool detail = false) {
    ostringstream output;
    auto* previous_out = cout.rdbuf(output.rdbuf());
    auto* previous_err = cerr.rdbuf(output.rdbuf());
    print_ai_explanation(record, detail);
    cout.rdbuf(previous_out); cerr.rdbuf(previous_err);
    return output.str();
}
}
int main() {
    ExecutionExplanation record;
    record.pipeline.source = "echo unit-test-key >file 2>&1";
    record.directory_before = record.directory_after = "/tmp";
    ParsedCommand command;
    command.name = "echo"; command.arguments = {"unit-test-key", "--password", "other-private-value"}; command.present = true;
    command.redirections = {{1, RedirectionType::output, "file", -1}, {2, RedirectionType::duplicate, "", 1}};
    command.environment_assignments = {{"OTHER_SECRET", "hidden"}, {"PASSWORD", "another-secret"}};
    record.stages.push_back({command, "built-in", true, 0});
    record.recent_history = {"OPENAI_API_KEY=unit-test-key", "OTHER_TOKEN=secret-value", "pwd"};
    const Json request = Json::parse(build_explain_ai_request(record, true, "gpt-4.1-mini", "unit-test-key"));
    expect(request.at("model") == "gpt-4.1-mini" && request.at("store") == false, "model and storage options");
    expect(request.at("text").at("format").at("type") == "json_schema"
           && request.at("text").at("format").at("strict") == true, "strict structured outputs");
    expect(request.at("max_output_tokens") == 1800, "detailed output token ceiling");
    const string context_text = request.at("input").get<string>();
    expect(context_text.find("unit-test-key") == string::npos && context_text.find("secret-value") == string::npos
           && context_text.find("hidden") == string::npos && context_text.find("other-private-value") == string::npos && context_text.find("another-secret") == string::npos, "keys and common assignments are redacted");
    const Json context = Json::parse(context_text);
    expect(context.at("execution").at("stages")[0].at("redirections")[1].at("source_fd") == 1,
           "ordered descriptor operations are preserved in API context");
    ExecutionExplanation huge = record;
    huge.pipeline.source = string(30000, 'x');
    huge.recent_history = vector<string>(100, string(30000, 'h'));
    huge.stages = vector<ExplainedStage>(100, record.stages[0]);
    huge.children = vector<ExecutionExplanation>(100, record);
    const auto bounded = Json::parse(build_explain_ai_request(huge, false, "gpt-4.1-mini", "unit-test-key"));
    expect(bounded.at("input").get<string>().size() <= 8000, "compact context is bounded");

    const Json explanation = {{"summary", "Summary"}, {"steps", {"Step"}},
                              {"expected_behavior", "Expected"}, {"caveats", Json::array()}};
    AiCommandExplanation parsed;
    string error;
    expect(parse_explain_ai_response(envelope(explanation.dump()).dump(), parsed, error)
           && parsed.summary == "Summary" && parsed.steps.size() == 1, "actual Responses output envelope parsing");
    expect(!parse_explain_ai_response("not JSON", parsed, error), "reject invalid response JSON");
    expect(!parse_explain_ai_response(envelope("{}").dump(), parsed, error), "validate structured schema locally");
    auto incomplete = envelope(explanation.dump()); incomplete["status"] = "incomplete";
    expect(!parse_explain_ai_response(incomplete.dump(), parsed, error), "reject truncated model output");
    auto refusal = envelope(explanation.dump());
    refusal["output"][1]["content"] = Json::array({{{"type", "refusal"}, {"refusal", "No"}}});
    expect(!parse_explain_ai_response(refusal.dump(), parsed, error), "handle model refusal");
    auto wrong_type = explanation; wrong_type["steps"] = "not an array";
    expect(!parse_explain_ai_response(envelope(wrong_type.dump()).dump(), parsed, error), "reject invalid field types");

    unsetenv("OPENAI_API_KEY");
    expect(capture(record).find("set OPENAI_API_KEY") != string::npos, "missing key falls back locally");
    setenv("OPENAI_API_KEY", "unit-test-key", 1);
    setenv("MYSHELL_EXPLAIN_MODEL", "gpt-4.1-mini", 1);
    char log_path[] = "/tmp/myshell-ai-test-XXXXXX";
    const int log = mkstemp(log_path);
    if (log < 0) return 2;
    close(log);
    setenv("MYSHELL_AI_TEST_LOG", log_path, 1);
    setenv("MYSHELL_AI_TEST_MODE", "success", 1);
    const string first = capture(record);
    const string second = capture(record);
    if (first.find("A fixture explanation") == string::npos) cerr << "Fixture transport diagnostic: " << first;
    expect(first.find("A fixture explanation") != string::npos && first.find("Recorded shell status: 0") != string::npos,
           "HTTPS subprocess transmits a valid request and renders structured output");
    expect(second.find("requesting explanation") == string::npos, "repeated inspection uses cache");
    ifstream logs(log_path); string line; int calls = 0;
    while (getline(logs, line)) ++calls;
    expect(calls == 1, "cached explanation avoids a second API call");
    setenv("MYSHELL_AI_TEST_MODE", "failure", 1); record.note = "HTTP failure test";
    expect(capture(record).find("HTTP 401") != string::npos, "HTTP error fallback");
    setenv("MYSHELL_AI_TEST_MODE", "malformed", 1); record.note = "malformed test";
    expect(capture(record).find("malformed") != string::npos, "malformed structured output fallback");
    setenv("MYSHELL_AI_TEST_MODE", "oversized", 1); record.note = "size limit test";
    expect(capture(record).find("size limit") != string::npos, "response transport size limit");
    setenv("OPENAI_API_KEY", "bad\nkey", 1);
    expect(capture(record).find("invalid API key format") != string::npos, "reject header injection in key");
    unlink(log_path);
    if (failures) return 1;
    cout << "All AI explanation fixture tests passed\n";
    return 0;
}

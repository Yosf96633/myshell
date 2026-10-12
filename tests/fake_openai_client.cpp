#include "third_party/nlohmann/json.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

using Json = nlohmann::json;
using namespace std;
int main(int argc, char** argv) {
    if (argc != 4 || string(argv[1]) != "--disable" || string(argv[2]) != "--config"
        || string(argv[3]) != "-") return 3;
    const string config{istreambuf_iterator<char>(cin), istreambuf_iterator<char>()};
    if (config.find("url = \"https://api.openai.com/v1/responses\"") == string::npos) return 41;
    if (config.find("Authorization: Bearer unit-test-key") == string::npos) return 42;
    const size_t data = config.find("data = ");
    if (data == string::npos) return 5;
    const size_t end = config.find('\n', data);
    const Json request = Json::parse(Json::parse(config.substr(data + 7, end - data - 7)).get<string>());
    if (request.at("store") != false || request.at("text").at("format").at("strict") != true) return 6;
    if (const char* log = getenv("MYSHELL_AI_TEST_LOG")) {
        ofstream output(log, ios::app); output << "request\n";
    }
    const char* setting = getenv("MYSHELL_AI_TEST_MODE");
    const string mode = setting == nullptr ? "success" : setting;
    if (mode == "failure") { cout << "{}\nMYSHELL_HTTP:401"; return 0; }
    if (mode == "malformed") { cout << "not JSON\nMYSHELL_HTTP:200"; return 0; }
    if (mode == "oversized") { cout << string(300000, 'x'); return 0; }
    const Json explanation = {{"summary", "A fixture explanation"}, {"steps", {"Read the input", "Write the output"}},
                              {"expected_behavior", "Expected behavior from the supplied command"},
                              {"caveats", {"No internal program trace is available"}}};
    const Json envelope = {{"status", "completed"}, {"output", Json::array({
        {{"type", "message"}, {"content", Json::array({
            {{"type", "output_text"}, {"text", explanation.dump()}}
        })}}
    })}};
    cout << envelope.dump() << "\nMYSHELL_HTTP:200";
}

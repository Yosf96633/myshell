#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

namespace {
int failures = 0;
void expect(bool condition, const string& message) {
    if (!condition) { cerr << "FAIL: " << message << '\n'; ++failures; }
}
string read_file(const fs::path& path) {
    ifstream input(path);
    return {istreambuf_iterator<char>(input), istreambuf_iterator<char>()};
}
struct Result { int status; string output; };
Result run_shell(const string& executable, const fs::path& directory, const string& commands) {
    const auto input_path = directory / "input";
    const auto output_path = directory / "output";
    { ofstream input(input_path); input << commands; }
    const pid_t child = fork();
    if (child == 0) {
        if (chdir(directory.c_str()) != 0
            || freopen(input_path.c_str(), "r", stdin) == nullptr
            || freopen(output_path.c_str(), "w", stdout) == nullptr
            || dup2(STDOUT_FILENO, STDERR_FILENO) < 0) _exit(125);
        execl(executable.c_str(), executable.c_str(), nullptr);
        _exit(126);
    }
    if (child < 0) return {-1, "fork failed"};
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return {-1, "wait failed"};
    }
    return {WIFEXITED(status) ? WEXITSTATUS(status) : -1, read_file(output_path)};
}
bool contains(const Result& result, const string& text) {
    return result.output.find(text) != string::npos;
}
size_t count(const string& text, const string& pattern) {
    size_t result = 0;
    for (size_t pos = 0; (pos = text.find(pattern, pos)) != string::npos; pos += pattern.size()) ++result;
    return result;
}
} // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    unsetenv("OPENAI_API_KEY"); // Never make paid requests in integration tests.
    const string executable = fs::absolute(argv[1]);
    char temporary[] = "/tmp/myshell-explain-tests-XXXXXX";
    const char* created = mkdtemp(temporary);
    if (created == nullptr) return 2;
    const fs::path directory(created);
    auto run = [&](const string& commands) { return run_shell(executable, directory, commands); };

    auto result = run("explain\n");
    expect(result.status == 1 && contains(result, "no previous execution"), "empty execution history");

    result = run("explain cd /tmp\nexplain\nexplain\npwd\n");
    expect(result.status == 0 && count(result.output, "Working directory:") == 3,
           "cd executes in the shell and repeated inspections retain its original record");
    expect(contains(result, "-> \"/tmp\""), "cd reports observed directory change");

    result = run("explain /bin/sh -c 'printf once >> counter'\nexplain\nexplain --verbose\n");
    expect(result.status == 0 && read_file(directory / "counter") == "once",
           "saved execution is never rerun");

    result = run("alias hi='echo hello'\nexplain hi 'world | quoted'\n");
    expect(result.status == 0 && contains(result, "Alias expansion:")
           && contains(result, "\"echo\" \"hello\" \"world | quoted\""),
           "alias expansion and quoted argument survive prefix execution");

    result = run("f() { echo first; /bin/sh -c 'exit 17'; }\nexplain f\nexplain\nexit $?\n");
    expect(result.status == 17 && contains(result, "[shell function]")
           && contains(result, "Function step:") && contains(result, "status 17"),
           "function steps and failure status are saved; explain preserves $?");

    result = run("f() { alias hi='echo old'; hi; alias hi='echo new'; }\nexplain f\n");
    expect(contains(result, "\"echo\" \"old\"") && !contains(result, "\"echo\" \"new\" [built-in]"),
           "function trace keeps execution-time alias expansion");

    result = run("explain echo 'a b' | cat > piped 2>&1\n");
    expect(result.status == 0 && read_file(directory / "piped") == "a b\n",
           "prefix covers entire pipeline and explanations do not enter redirected output");
    expect(contains(result, "Pipe:") && contains(result, "at this point: file \"piped\""),
           "pipe and descriptor duplication are explained");

    result = run("explain /bin/false | /bin/true\n");
    expect(result.status == 0 && contains(result, "Status: 1 (nonzero)")
           && contains(result, "Status: 0 (success)") && contains(result, "final command"),
           "pipeline records individual statuses without changing final-stage semantics");

    result = run("explain /bin/sh -c 'printf err >&2; printf out' 2>&1 > separate\n");
    expect(result.status == 0 && read_file(directory / "separate") == "out"
           && contains(result, "at this point: inherited shell output"),
           "duplication before output redirection keeps original destination");
    result = run("explain /bin/sh -c 'printf err >&2; printf out' > combined 2>&1\n");
    expect(result.status == 0 && read_file(directory / "combined") == "errout"
           && contains(result, "at this point: file \"combined\""),
           "duplication after output redirection follows new destination");

    result = run("explain echo first > appended\nexplain echo second >> appended\nexplain cat < appended\n");
    expect(read_file(directory / "appended") == "first\nsecond\n"
           && contains(result, "Append standard output") && contains(result, "Read standard input"),
           "input and append redirections execute and are described");

    result = run("explain echo value 3> extra 3>&-\n");
    expect(result.status == 0 && contains(result, "Close file descriptor 3"), "descriptor close");

    result = run("explain echo value > /no-such-myshell-explain-directory/output\nexplain\n");
    expect(result.status == 1 && contains(result, "Result: status 1 (nonzero)"), "redirection failure");

    result = run("explain definitely-not-a-myshell-command\nexplain\n");
    expect(result.status == 127 && contains(result, "status 127"), "unknown external command");

    result = run("explain TEST_EXPLAIN=value /bin/sh -c 'test \"$TEST_EXPLAIN\" = value'\n");
    expect(result.status == 0 && contains(result, "Set \"TEST_EXPLAIN\"=\"value\" for this command"),
           "command-scoped assignment remains an assignment after prefix removal");
    result = run("explain TEST_EXPLAIN=persistent\n/bin/sh -c 'test \"$TEST_EXPLAIN\" = persistent'\n");
    expect(result.status == 0, "assignment-only command retains persistent semantics");

    result = run("explain --verbose -- echo '$HOME' \"$HOME\"\n");
    expect(result.status == 0 && contains(result, "Expanded argument count: 2"),
           "verbose prefix and end-of-options preserve expansion and literal quoting");

    result = run("explain cd /tmp | cat\npwd\n");
    expect(result.status == 0 && contains(result, "shell state changes stay in that child")
           && !contains(result, "Working directory:"), "pipeline cd keeps normal child-process semantics");

    result = run("explain /bin/true &\n");
    expect(result.status == 0 && contains(result, "completion status not recorded here"),
           "background launch does not claim completed success");

    result = run("echo 'unfinished\nexplain\n");
    expect(result.status == 2 && contains(result, "nothing executed"), "syntax error replaces stale record");

    result = run("echo saved\nexplain > report\nexplain\n");
    expect(result.status == 0 && read_file(directory / "report").find("Explain: \"echo saved\"") != string::npos
           && contains(result, "Explain: \"echo saved\""), "inspection redirection preserves saved record");

    result = run("echo saved\nexplain 2> inspection-errors\nexplain\n");
    expect(count(result.output, "Explain: \"echo saved\"") == 2,
           "descriptor-only redirection redirects inspection rather than becoming a target");
    result = run("explain > leading-output echo hello\n");
    expect(result.status == 0 && read_file(directory / "leading-output") == "hello\n"
           && contains(result, "Explain:"), "leading redirections belong to target command");
    result = run("explain cd /no-such-myshell-explain-directory\nexplain\n");
    expect(result.status == 1 && count(result.output, "Note:") == 2
           && contains(result, "Working directory unchanged"), "cd failure reason is saved");

    result = run("explain -d /bin/ls -l\n");
    expect(result.status == 0 && contains(result, "Detailed execution:")
           && contains(result, "show a detailed listing"), "detail explains execution and familiar program flags");
    result = run("explain -v echo hello | cat > diagram-file 2>&1\nexplain -b\n");
    expect(result.status == 0 && read_file(directory / "diagram-file") == "hello\n"
           && count(result.output, "ASCII flow") == 2 && contains(result, "pipe 1 -> 2"),
           "visual prefix and -b inspection preserve execution and produce diagrams");
    result = run("explain -dv /bin/sh -c 'printf once >> visual-counter'\nexplain -vd\n");
    expect(result.status == 0 && read_file(directory / "visual-counter") == "once"
           && count(result.output, "Detailed execution:") == 2 && count(result.output, "ASCII flow") == 2,
           "combined flags and inspection execute the action exactly once");
    result = run("explain -v /bin/sh -c 'echo out; echo err >&2' 2>&1 > visual-order\n");
    expect(result.status == 0 && contains(result, "errors fd 2 ---> [shell output]")
           && read_file(directory / "visual-order") == "out\n", "diagram captures descriptor-order destination");
    result = run("explain --ai -dv /bin/false\nexplain --ai -d\nexit $?\n");
    expect(result.status == 1 && count(result.output, "set OPENAI_API_KEY") == 2,
           "AI failure preserves command status and saved execution");
    result = run("explain -unknown /bin/sh -c 'printf bad > should-not-exist'\n");
    expect(result.status == 2 && !fs::exists(directory / "should-not-exist"),
           "invalid explain flags never execute their target");

    result = run("help explain\ntype explain\n");
    expect(result.status == 0 && contains(result, "explain is a shell builtin"), "builtin registration and help");

    fs::remove_all(directory);
    if (failures) { cerr << failures << " explain test(s) failed\n"; return 1; }
    cout << "All explain integration tests passed\n";
    return 0;
}

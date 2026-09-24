#include "myshell/tokenizer.hpp"
#include "myshell/shell_state.hpp"

#include <unistd.h>

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace std;

namespace {

int failure_count = 0;

void expect(bool condition, const string& description) {
    if (!condition) {
        cerr << "FAIL: " << description << '\n';
        ++failure_count;
    }
}

optional<ParsedCommand> command_from(
    const string& source,
    const string& description) {
    ParseResult result = parse_command(source);
    expect(!result.has_error(), description + " should not report an error");
    expect(result.command.has_value(), description + " should produce a command");
    return move(result.command);
}

optional<ParsedPipeline> pipeline_from(
    const string& source,
    const string& description) {
    PipelineParseResult result = parse_pipeline(source);
    expect(!result.has_error(), description + " should not report an error");
    expect(result.pipeline.has_value(), description + " should produce a pipeline");
    return move(result.pipeline);
}

void expect_command_error(const string& source, const string& description) {
    const ParseResult result = parse_command(source);
    expect(result.has_error(), description + " should report an error");
    expect(!result.command.has_value(), description + " should not produce a command");
}

void expect_pipeline_error(const string& source, const string& description) {
    const PipelineParseResult result = parse_pipeline(source);
    expect(result.has_error(), description + " should report an error");
    expect(!result.pipeline.has_value(), description + " should not produce a pipeline");
}

void test_empty_input() {
    const ParseResult empty = parse_command("");
    expect(!empty.has_error(), "empty input should not report an error");
    expect(!empty.command.has_value(), "empty input should not produce a command");

    const PipelineParseResult whitespace = parse_pipeline(" \t  ");
    expect(!whitespace.has_error(), "whitespace should not report an error");
    expect(!whitespace.pipeline.has_value(), "whitespace should not produce a pipeline");
}

void test_words_quotes_and_escapes() {
    auto basic = command_from("echo hello world", "basic words");
    if (basic) {
        expect(basic->name == "echo", "basic command name");
        expect(basic->arguments == vector<string>({"hello", "world"}),
               "basic command arguments");
    }

    auto quoted = command_from(
        "echo 'two words' \"three words\" \"\"", "quoted words");
    if (quoted) {
        expect(quoted->arguments == vector<string>({"two words", "three words", ""}),
               "quotes should preserve grouping and empty arguments");
    }

    auto escaped = command_from("echo one\\ two \\|", "escaped characters");
    if (escaped) {
        expect(escaped->arguments == vector<string>({"one two", "|"}),
               "backslashes should preserve escaped characters");
    }
}

void test_parameter_expansion() {
    setenv("MYSHELL_TEST_VALUE", "expanded value", 1);

    auto expanded = command_from(
        "echo \"$MYSHELL_TEST_VALUE\" ${MYSHELL_TEST_VALUE}",
        "environment expansion");
    if (expanded) {
        expect(expanded->arguments
                   == vector<string>({"expanded value", "expanded value"}),
               "environment variables should expand");
    }

    auto literal = command_from(
        "echo '$MYSHELL_TEST_VALUE'", "single-quoted variable");
    if (literal) {
        expect(literal->arguments == vector<string>({"$MYSHELL_TEST_VALUE"}),
               "single quotes should suppress expansion");
    }

    auto process_id = command_from("echo $$", "process ID expansion");
    if (process_id) {
        expect(process_id->arguments == vector<string>({to_string(getpid())}),
               "$$ should expand to the parser process ID");
    }

    set_shell_last_status(37);
    auto exit_status = command_from("echo $?", "exit status expansion");
    if (exit_status) {
        expect(exit_status->arguments == vector<string>({"37"}),
               "$? should expand to the latest shell status");
    }

    unsetenv("MYSHELL_TEST_MISSING");
    auto missing = command_from(
        "echo $MYSHELL_TEST_MISSING \"$MYSHELL_TEST_MISSING\"",
        "unset variable expansion");
    if (missing) {
        expect(missing->arguments == vector<string>({""}),
               "quoted unset variables should preserve an empty argument");
    }

    unsetenv("MYSHELL_TEST_VALUE");
}

void test_redirections() {
    auto parsed = command_from(
        "cat 3<input 4>>output 2>&1 5>&- <&7", "descriptor redirections");
    if (parsed) {
        expect(parsed->name == "cat", "redirection command name");
        expect(parsed->arguments.empty(), "redirections should not become arguments");
        expect(parsed->redirections.size() == 5, "all redirections should be retained");
        if (parsed->redirections.size() == 5) {
            const auto& input = parsed->redirections[0];
            expect(input.target_fd == 3 && input.type == RedirectionType::input
                       && input.path == "input",
                   "explicit input descriptor");

            const auto& append = parsed->redirections[1];
            expect(append.target_fd == 4 && append.type == RedirectionType::append
                       && append.path == "output",
                   "explicit append descriptor");

            const auto& duplicate_output = parsed->redirections[2];
            expect(duplicate_output.target_fd == 2
                       && duplicate_output.type == RedirectionType::duplicate
                       && duplicate_output.source_fd == 1,
                   "output descriptor duplication");

            const auto& close = parsed->redirections[3];
            expect(close.target_fd == 5 && close.type == RedirectionType::close,
                   "descriptor closing");

            const auto& duplicate_input = parsed->redirections[4];
            expect(duplicate_input.target_fd == 0
                       && duplicate_input.type == RedirectionType::duplicate
                       && duplicate_input.source_fd == 7,
                   "default input descriptor duplication");
        }
    }

    auto defaults = command_from("echo hello > file", "default output redirection");
    if (defaults && defaults->redirections.size() == 1) {
        expect(defaults->redirections[0].target_fd == 1
                   && defaults->redirections[0].type == RedirectionType::output,
               "output redirection should default to stdout");
    }

    auto spaced_number = command_from("echo 2 > file", "spaced descriptor-like word");
    if (spaced_number && spaced_number->redirections.size() == 1) {
        expect(spaced_number->arguments == vector<string>({"2"}),
               "a spaced number should remain an argument");
        expect(spaced_number->redirections[0].target_fd == 1,
               "a spaced redirection should still target stdout");
    }

    expect_command_error("echo >", "missing redirection operand");
    expect_command_error("echo 2>&word", "non-numeric duplicated descriptor");
    expect_command_error("echo 2>&\"\"", "empty duplicated descriptor");
}

void test_command_errors() {
    expect_command_error("echo 'unfinished", "unclosed single quote");
    expect_command_error("echo \"unfinished", "unclosed double quote");
    expect_command_error("echo trailing\\", "trailing escape");
    expect_command_error("echo ${}", "empty parameter name");
    expect_command_error("echo ${BAD-NAME}", "invalid parameter name");
    expect_command_error("echo ${MISSING", "missing parameter brace");
}

void test_pipelines() {
    auto pipeline = pipeline_from(
        "printf x|tr x y|cat > output", "three-stage pipeline");
    if (pipeline) {
        expect(pipeline->commands.size() == 3, "pipeline should have three stages");
        if (pipeline->commands.size() == 3) {
            expect(pipeline->commands[0].name == "printf", "first pipeline command");
            expect(pipeline->commands[1].name == "tr", "middle pipeline command");
            expect(pipeline->commands[2].name == "cat", "final pipeline command");
            expect(pipeline->commands[2].redirections.size() == 1,
                   "final pipeline redirection");
        }
    }

    auto literal_pipes = pipeline_from(
        "echo 'a|b' \\| \"c|d\" | cat", "quoted and escaped pipes");
    if (literal_pipes) {
        expect(literal_pipes->commands.size() == 2,
               "only an unquoted pipe should split the pipeline");
        if (literal_pipes->commands.size() == 2) {
            expect(literal_pipes->commands[0].arguments
                       == vector<string>({"a|b", "|", "c|d"}),
                   "literal pipes should remain arguments");
        }
    }

    expect_pipeline_error("| cat", "leading pipe");
    expect_pipeline_error("echo hello |", "trailing pipe");
    expect_pipeline_error("echo hello || cat", "empty pipeline stage");
    expect_pipeline_error("echo 'hello | cat", "unclosed quote in pipeline");
}

} // namespace

int main() {
    test_empty_input();
    test_words_quotes_and_escapes();
    test_parameter_expansion();
    test_redirections();
    test_command_errors();
    test_pipelines();

    if (failure_count != 0) {
        cerr << failure_count << " tokenizer/parser test(s) failed\n";
        return 1;
    }
    cout << "All tokenizer/parser tests passed\n";
    return 0;
}

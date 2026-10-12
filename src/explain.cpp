#include "myshell/explain.hpp"

#include "myshell/builtins.hpp"
#include "myshell/functions.hpp"
#include "myshell/history.hpp"
#include "myshell/explain_options.hpp"
#include "myshell/explain_ai.hpp"
#include "myshell/shell_state.hpp"

#include <unistd.h>

#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <map>
#include <sstream>
#include <utility>

using namespace std;

namespace {
ExecutionExplanation* active = nullptr;
optional<ExecutionExplanation> last_execution;

string current_directory() {
    char* directory = getcwd(nullptr, 0);
    if (directory == nullptr) return "(unavailable)";
    string result(directory);
    free(directory);
    return result;
}

// Keep control characters in command arguments from corrupting the report.
string quote(const string& value) {
    string result = "\"";
    for (unsigned char byte : value) {
        switch (byte) {
        case '\n': result += "\\n"; break;
        case '\r': result += "\\r"; break;
        case '\t': result += "\\t"; break;
        case '\\': result += "\\\\"; break;
        case '"': result += "\\\""; break;
        default:
            if (byte < 32 || byte == 127) {
                ostringstream escaped;
                escaped << "\\x" << hex << setw(2) << setfill('0') << int(byte);
                result += escaped.str();
            } else {
                result += static_cast<char>(byte);
            }
        }
    }
    return result + '"';
}

string command_words(const ParsedCommand& command) {
    string result = quote(command.name);
    for (const auto& argument : command.arguments) result += " " + quote(argument);
    return result;
}

string descriptor(int fd) {
    switch (fd) {
    case 0: return "standard input (fd 0)";
    case 1: return "standard output (fd 1)";
    case 2: return "standard error (fd 2)";
    default: return "file descriptor " + to_string(fd);
    }
}

string purpose(const ExplainedStage& stage) {
    if (!stage.observed) return "not dispatched";
    if (stage.kind == "shell function") return "runs the function's stored commands";
    if (stage.command.name.empty()) return "applies assignments and redirections";
    if (stage.kind == "built-in") {
        static const map<string, string> descriptions = {
            {"cd", "changes the shell's working directory"},
            {"pwd", "prints the working directory"},
            {"echo", "prints its arguments"},
            {"alias", "defines or displays aliases"},
            {"declare", "displays shell functions"},
            {"unset", "removes shell functions"},
            {"history", "displays session command history"},
            {"type", "reports how command names resolve"},
            {"hash", "displays or changes cached executable paths"},
            {"help", "displays built-in help"},
            {"jobs", "lists jobs"},
            {"fg", "continues a job in the foreground"},
            {"bg", "continues a job in the background"},
            {"exit", "requests that the shell exit"},
            {"explain", "describes the saved execution"},
        };
        if (stage.command.name == "exec") {
            return stage.command.arguments.empty()
                ? "applies persistent file descriptor redirections"
                : "replaces the shell with the requested executable; a successful replacement cannot report afterward";
        }
        const auto found = descriptions.find(stage.command.name);
        if (found != descriptions.end()) return found->second;
    }
    // These are descriptions of familiar utilities, not claims about their
    // internals. A path or arbitrary executable gets only a structural report.
    if (stage.kind == "external executable" && stage.command.name.find('/') == string::npos) {
        static const map<string, string> descriptions = {
            {"cat", "normally copies files or standard input to standard output"},
            {"grep", "normally selects lines matching a pattern (options can change this)"},
            {"ls", "normally lists directory contents"},
            {"sort", "normally sorts input lines"},
            {"wc", "normally counts input lines, words, or bytes"},
            {"head", "normally prints the beginning of input"},
            {"tail", "normally prints the end of input"},
            {"tr", "normally translates or deletes input characters"},
            {"tee", "normally copies input to standard output and files"},
            {"printf", "normally prints formatted arguments"},
        };
        const auto found = descriptions.find(stage.command.name);
        if (found != descriptions.end()) return found->second;
    }
    return "runs the executable with the shown arguments";
}

map<int, string> final_destinations(const ExecutionExplanation& record, size_t index) {
    map<int, string> streams = {
        {0, index == 0 ? "shell input" : "pipe " + to_string(index) + " -> " + to_string(index + 1)},
        {1, index + 1 == record.stages.size() ? "shell output"
            : "pipe " + to_string(index + 1) + " -> " + to_string(index + 2)},
        {2, "shell errors"},
    };
    for (const auto& redirect : record.stages[index].command.redirections) {
        switch (redirect.type) {
        case RedirectionType::input:
            streams[redirect.target_fd] = "file " + quote(redirect.path) + " (read)"; break;
        case RedirectionType::output:
            streams[redirect.target_fd] = "file " + quote(redirect.path) + " (replace)"; break;
        case RedirectionType::append:
            streams[redirect.target_fd] = "file " + quote(redirect.path) + " (append)"; break;
        case RedirectionType::duplicate: {
            const auto source = streams.find(redirect.source_fd);
            streams[redirect.target_fd] = source == streams.end()
                ? "inherited fd " + to_string(redirect.source_fd) : source->second;
            break;
        }
        case RedirectionType::close: streams[redirect.target_fd] = "closed"; break;
        }
    }
    return streams;
}

string argument_meaning(const ExplainedStage& stage, const string& argument) {
    if (stage.kind == "shell function") return "passed to the function (this shell does not expand positional parameters)";
    if (stage.command.name == "cd" && stage.kind == "built-in") return "directory to make current; relative paths start from the previous working directory";
    if (stage.command.name == "echo" && stage.kind == "built-in") return "literal text printed by this shell's echo builtin";
    string name = stage.command.name;
    const size_t slash = name.find_last_of('/');
    if (slash != string::npos) name = name.substr(slash + 1);
    static const map<string, map<string, string>> options = {
        {"ls", {{"-l", "usual ls option: show a detailed listing"}, {"-a", "usual ls option: include hidden entries"},
                {"-h", "usual ls option: human-readable sizes"}, {"-R", "usual ls option: recursively list directories"}}},
        {"grep", {{"-i", "usual grep option: ignore letter case"}, {"-n", "usual grep option: include line numbers"},
                  {"-v", "usual grep option: select nonmatching lines"}, {"-E", "usual grep option: extended regular expressions"},
                  {"-F", "usual grep option: fixed-string patterns"}, {"-c", "usual grep option: count matching lines"}}},
        {"cat", {{"-n", "usual cat option: number output lines"}, {"-b", "usual cat option: number nonempty lines"}}},
        {"sort", {{"-n", "usual sort option: numeric order"}, {"-r", "usual sort option: reverse order"},
                  {"-u", "usual sort option: remove duplicate output lines"}}},
        {"wc", {{"-l", "usual wc option: count lines"}, {"-w", "usual wc option: count words"},
                {"-c", "usual wc option: count bytes"}}},
    };
    if (stage.kind == "external executable") {
        const auto command = options.find(name);
        if (command != options.end()) {
            const auto option = command->second.find(argument);
            if (option != command->second.end()) return option->second;
        }
        return "passed to the executable; program-specific interpretation is not traced";
    }
    return "passed to the builtin after shell expansion and quote removal";
}

void print_stage_detail(const ExecutionExplanation& record, size_t index, const string& indent) {
    const auto& stage = record.stages[index];
    const auto& command = stage.command;
    cout << indent << "     Detailed execution:\n";
    if (!stage.observed) {
        cout << indent << "       Dispatch was not reached; the shown syntax is not proof that the command ran.\n";
        return;
    }
    const bool child = record.pipeline.background || record.stages.size() > 1;
    if (child) {
        cout << indent << "       This stage runs in a child of the shell. All pipeline stages share a job process group.\n"
             << indent << (record.stages.size() > 1
                 ? "       Pipes carry bytes between concurrently running stages; they do not pass argument lists.\n"
                 : "       Background execution returns to the prompt without waiting for completion.\n");
    } else if (stage.kind == "external executable") {
        cout << indent << "       The shell launches a child process and waits for its foreground job to finish or stop.\n";
    } else {
        cout << indent << "       This command runs in the current shell; directory and function state changes can persist.\n";
    }
    if (stage.kind == "external executable") {
        cout << indent << "       Executable lookup: " << (command.name.find('/') == string::npos
            ? "search the effective PATH or reuse the command cache.\n"
            : "use the supplied pathname directly, without PATH lookup.\n");
    }
    cout << indent << "       Expanded argument count: " << command.arguments.size() << ".\n";
    for (size_t i = 0; i < command.arguments.size(); ++i) {
        cout << indent << "       Argument " << i + 1 << ": " << quote(command.arguments[i])
             << " - " << argument_meaning(stage, command.arguments[i]) << ".\n";
    }
    if (command.arguments.empty()) cout << indent << "       No arguments were supplied after the command name.\n";
    if (!command.redirections.empty()) {
        cout << indent << "       Redirections apply left to right after pipe connections, before the command body runs.\n"
             << indent << "       Opening an output file may create or truncate it even if the command later fails.\n";
    }
    cout << indent << "       Final descriptor setup (requested):\n";
    for (const auto& [fd, destination] : final_destinations(record, index)) {
        cout << indent << "         " << descriptor(fd) << " -> " << destination << '\n';
    }
    if (!child && command.name != "exec" && !command.redirections.empty())
        cout << indent << "       Command redirections are temporary; the shell restores its descriptors afterward.\n";
    if (!command.environment_assignments.empty())
        cout << indent << "       Leading assignments " << (command.name.empty()
            ? "remain in the shell environment.\n" : "are scoped to this command and restored afterward in the parent.\n");
    if (stage.kind == "shell function") {
        cout << indent << "       The function body is parsed when called, so its expansions use execution-time state.\n";
        if (child) cout << indent << "       Internal child function steps are not transferred back to this record.\n";
    }
    if (command.name == "jobs" && stage.kind == "built-in")
        cout << indent << "       jobs lists tracked running/stopped jobs; its own status does not describe every listed job.\n";
    if ((command.name == "fg" || command.name == "bg") && stage.kind == "built-in")
        cout << indent << "       fg gives the selected job the terminal and waits; bg resumes it without taking the terminal.\n";
    if (stage.status) cout << indent << "       Recorded status " << *stage.status
        << ": " << (*stage.status == 0 ? "the command reported success; this is not a trace of all its internal actions.\n"
                                       : "a nonzero result; consult any recorded note or the command's own error output.\n");
}

void print_visual(const ExecutionExplanation& record, const string& indent = "") {
    cout << indent << "ASCII flow (requested connections; recorded results below):\n";
    if (record.stages.empty()) {
        cout << indent << "  [input] ---> [" << quote(record.note) << "]\n";
        return;
    }
    cout << indent << (record.pipeline.background ? "  [background job]\n" : "  [foreground execution]\n");
    for (size_t i = 0; i < record.stages.size(); ++i) {
        const auto& stage = record.stages[i];
        string label = "#" + to_string(i + 1) + " " + command_words(stage.command) + " [" + stage.kind + "]";
        if (label.size() > 96) label = label.substr(0, 93) + "...";
        const string border(label.size() + 2, '-');
        cout << indent << "  +" << border << "+\n"
             << indent << "  | " << label << " |\n"
             << indent << "  +" << border << "+\n";
        for (const auto& [fd, destination] : final_destinations(record, i)) {
            cout << indent << "    " << (fd == 0 ? "input  " : fd == 1 ? "output " : fd == 2 ? "errors " : "extra  ")
                 << "fd " << fd << (fd == 0 ? " <--- " : " ---> ") << '[' << destination << "]\n";
        }
        cout << indent << "    status: " << (stage.status ? to_string(*stage.status) : "not recorded") << '\n';
        if (i + 1 < record.stages.size()) cout << indent << "             |\n" << indent << "             v\n";
    }
    if (record.directory_before != record.directory_after)
        cout << indent << "  [cwd " << quote(record.directory_before) << "] ---> [cwd "
             << quote(record.directory_after) << "]\n";
    if (record.pipeline.background) cout << indent << "  [launch status " << record.status << "; completion not recorded]\n";
    else cout << indent << "  [shell status " << record.status
              << (record.job_completed ? "]\n" : "; job incomplete]\n");
    for (const auto& child : record.children) {
        cout << indent << "  Function step:\n";
        print_visual(child, indent + "    ");
    }
}

void print_record(const ExecutionExplanation& record, bool verbose, const string& indent) {
    cout << indent << "Explain: " << quote(record.pipeline.source) << '\n';
    if (!record.note.empty()) cout << indent << "  Note: " << quote(record.note) << '\n';
    if (verbose && !record.stages.empty()) {
        cout << indent << "  Shown words have variable expansion and quote removal applied; "
             << "quoted or escaped pipe/redirection characters remain literal arguments.\n";
    }
    for (size_t i = 0; i < record.stages.size(); ++i) {
        const auto& stage = record.stages[i];
        const auto& original = record.pipeline.commands[i];
        const auto& command = stage.command;
        cout << indent << "  " << i + 1 << ". " << command_words(command)
             << " [" << stage.kind << "]: " << purpose(stage) << ".\n";
        if (original.name != command.name || original.arguments != command.arguments) {
            cout << indent << "     Alias expansion: " << command_words(original)
                 << " -> " << command_words(command) << ".\n";
        }
        if (record.pipeline.commands.size() > 1 || record.pipeline.background) {
            if (stage.kind == "built-in" || stage.kind == "shell function") {
                cout << indent << "     Runs in a child process; shell state changes stay in that child.\n";
            }
        }
        for (const auto& [name, value] : command.environment_assignments) {
            cout << indent << "     Set " << quote(name) << '=' << quote(value)
                 << (command.name.empty() ? " in this shell.\n" : " for this command.\n");
        }
        map<int, string> destinations = {
            {0, i == 0 ? "inherited shell input" : "pipe from command " + to_string(i)},
            {1, i + 1 == record.stages.size() ? "inherited shell output"
                                            : "pipe to command " + to_string(i + 2)},
            {2, "inherited shell errors"},
        };
        if (i + 1 < record.stages.size()) {
            cout << indent << "     Pipe: connects standard output (fd 1) to command "
                 << i + 2 << "'s standard input (fd 0), before redirections.\n";
        }
        for (const auto& redirection : command.redirections) {
            cout << indent << "     ";
            const string target = descriptor(redirection.target_fd);
            switch (redirection.type) {
            case RedirectionType::input:
                destinations[redirection.target_fd] = "file " + quote(redirection.path);
                cout << "Read " << target << " from " << quote(redirection.path); break;
            case RedirectionType::output:
                destinations[redirection.target_fd] = "file " + quote(redirection.path);
                cout << "Write " << target << " to " << quote(redirection.path)
                     << " (create or replace contents)"; break;
            case RedirectionType::append:
                destinations[redirection.target_fd] = "file " + quote(redirection.path);
                cout << "Append " << target << " to " << quote(redirection.path)
                     << " (create if needed)"; break;
            case RedirectionType::duplicate: {
                const auto found = destinations.find(redirection.source_fd);
                const string destination = found == destinations.end()
                    ? "inherited descriptor " + to_string(redirection.source_fd) : found->second;
                destinations[redirection.target_fd] = destination;
                cout << "Make " << target << " use the same open stream as "
                     << descriptor(redirection.source_fd) << " at this point: " << destination;
                break;
            }
            case RedirectionType::close:
                destinations[redirection.target_fd] = "closed";
                cout << "Close " << target; break;
            }
            cout << ".\n";
        }
        if (stage.status) {
            cout << indent << "     Status: " << *stage.status
                 << (*stage.status == 0 ? " (success).\n" : " (nonzero).\n");
        }
        if (verbose) {
            print_stage_detail(record, i, indent);
        }
    }
    size_t shown = 0;
    for (const auto& child : record.children) {
        if (!verbose && shown == 6) {
            cout << indent << "  More function steps saved; use explain --verbose to see them.\n";
            break;
        }
        cout << indent << "  Function step:\n";
        print_record(child, verbose, indent + "    ");
        ++shown;
    }
    if (record.directory_before != record.directory_after) {
        cout << indent << "  Working directory: " << quote(record.directory_before)
             << " -> " << quote(record.directory_after) << ".\n";
    } else if (!record.stages.empty() && record.stages[0].command.name == "cd") {
        cout << indent << "  Working directory unchanged: " << quote(record.directory_after) << ".\n";
    }
    if (record.pipeline.background) {
        cout << indent << "  Result: " << (record.status == 0
            ? "launched in the background; completion status not recorded here."
            : "background launch failed (status " + to_string(record.status) + ").") << '\n';
    } else if (!record.job_completed) {
        cout << indent << "  Result: job stopped or incomplete; reported shell status "
             << record.status << ".\n";
    } else {
        cout << indent << "  Result: status " << record.status
             << (record.status == 0 ? " (success)" : " (nonzero)");
        if (record.stages.size() > 1) cout << "; pipeline status comes from the final command";
        cout << ".\n";
    }
}
} // namespace

ExplanationFrame::ExplanationFrame(const ParsedPipeline& pipeline) : parent_(active) {
    record_.pipeline = pipeline;
    record_.directory_before = current_directory();
    // Save history with the execution so later inspection requests are stable.
    const size_t history_end = command_history.empty() ? 0 : command_history.size() - 1;
    const size_t history_start = history_end > 3 ? history_end - 3 : 0;
    for (size_t i = history_start; i < history_end; ++i) record_.recent_history.push_back(command_history[i]);
    for (const auto& command : pipeline.commands) {
        record_.stages.push_back({command, "not dispatched", false, nullopt});
    }
    inspection_ = !pipeline.explain_requested && !pipeline.commands.empty()
        && pipeline.commands.front().name == "explain";
    active = &record_;
}

ExplanationFrame::~ExplanationFrame() { active = parent_; }

void ExplanationFrame::finish(int status) {
    record_.status = status == EXIT_SIGNAL ? shell_requested_exit_status() : status;
    record_.directory_after = current_directory();
    if (record_.job_completed && !record_.pipeline.background && record_.stages.size() == 1
        && !record_.stages[0].status) record_.stages[0].status = record_.status;
    if (record_.pipeline.explain_requested) {
        print_record(record_, record_.pipeline.explain_verbose, "");
        if (record_.pipeline.explain_visual) print_visual(record_);
        if (record_.pipeline.explain_ai) print_ai_explanation(record_, record_.pipeline.explain_verbose);
    }
    if (!inspection_) {
        if (parent_ != nullptr) parent_->children.push_back(move(record_));
        else last_execution = move(record_);
    }
}

void observe_explained_command(const ParsedCommand& command, size_t stage) {
    if (active == nullptr || stage >= active->stages.size()) return;
    string kind = command.name.empty() ? "shell state"
        : is_shell_function(command.name) ? "shell function"
        : is_builtin(command.name) ? "built-in" : "external executable";
    active->stages[stage] = {command, move(kind), true, nullopt};
}

void observe_explained_statuses(const vector<optional<int>>& statuses, bool completed) {
    if (active == nullptr) return;
    active->job_completed = completed;
    for (size_t i = 0; i < statuses.size() && i < active->stages.size(); ++i)
        active->stages[i].status = statuses[i];
}

void observe_explained_error(const string& error) {
    if (active != nullptr) active->note = error;
}

void remember_explanation_note(const string& source, const string& note, int status) {
    ExecutionExplanation record;
    record.pipeline.source = source;
    record.note = note;
    record.status = status;
    record.directory_before = record.directory_after = current_directory();
    last_execution = move(record);
}

int builtin_explain(const vector<string>& arguments) {
    ExplainOptions options;
    bool options_ended = false;
    for (const auto& argument : arguments) {
        const auto result = options_ended ? ExplainOptionResult::operand : parse_explain_option(argument, options);
        if (result == ExplainOptionResult::end) { options_ended = true; continue; }
        if (result == ExplainOptionResult::help) {
            cout << "Usage: " << explain_usage << '\n'
                 << "  -d, --detail, --verbose  Detailed explanation and all saved function steps.\n"
                 << "  -v, -b, --visual         ASCII diagram of commands and file descriptors.\n"
                 << "  -a, --ai                Add an OpenAI explanation (requires OPENAI_API_KEY).\n"
                 << "Without a command, inspect the saved execution without rerunning it.\n";
            return 0;
        }
        if (result != ExplainOptionResult::accepted) {
            cerr << "explain: usage: " << explain_usage << '\n';
            return 2;
        }
    }
    if (!last_execution) {
        cerr << "explain: no previous execution to explain\n";
        return 1;
    }
    print_record(*last_execution, options.detail, "");
    if (options.visual) print_visual(*last_execution);
    if (options.ai) print_ai_explanation(*last_execution, options.detail);
    // An explanation failure never changes the status of the inspected action.
    return shell_last_status();
}

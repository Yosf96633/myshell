#include "myshell/explain.hpp"

#include "myshell/builtins.hpp"
#include "myshell/functions.hpp"
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
            cout << indent << "     Expanded argument count: " << command.arguments.size()
                 << "; redirections apply left to right after pipe connections.\n";
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
    if (record_.pipeline.explain_requested) print_record(record_, record_.pipeline.explain_verbose, "");
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
    bool verbose = false;
    if (arguments == vector<string>{"--help"}) {
        cout << "Usage: explain [--verbose] [--] [command ...]\n"
             << "Run and explain the entire command/pipeline, or explain the last execution without rerunning it.\n";
        return 0;
    }
    if (arguments == vector<string>{"--verbose"}
        || arguments == vector<string>{"--verbose", "--"}) verbose = true;
    else if (!arguments.empty() && arguments != vector<string>{"--"}) {
        cerr << "explain: usage: explain [--verbose] [--] [command ...]\n";
        return 2;
    }
    if (!last_execution) {
        cerr << "explain: no previous execution to explain\n";
        return 1;
    }
    print_record(*last_execution, verbose, "");
    // Inspection preserves $? from the command being inspected.
    return shell_last_status();
}

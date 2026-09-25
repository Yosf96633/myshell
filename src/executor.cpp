#include "myshell/executor.hpp"

#include "myshell/builtins.hpp"
#include "myshell/exec_builtin.hpp"
#include "myshell/functions.hpp"
#include "myshell/job_control.hpp"
#include "myshell/path_search.hpp"
#include "myshell/redirection.hpp"
#include "myshell/shell_state.hpp"
#include "myshell/tokenizer.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using namespace std;

namespace {

constexpr int maximum_function_depth = 64;

struct EnvironmentBackup {
    string name;
    optional<string> value;
};

bool assigns_path(const ParsedCommand& command) {
    for (const auto& [name, value] : command.environment_assignments) {
        (void)value;
        if (name == "PATH") {
            return true;
        }
    }
    return false;
}

void restore_environment(const vector<EnvironmentBackup>& backups) {
    for (auto backup = backups.rbegin(); backup != backups.rend(); ++backup) {
        if (backup->value) {
            setenv(backup->name.c_str(), backup->value->c_str(), 1);
        } else {
            unsetenv(backup->name.c_str());
        }
    }
}

bool apply_environment(
    const ParsedCommand& command,
    vector<EnvironmentBackup>* backups,
    string& error) {
    for (const auto& [name, value] : command.environment_assignments) {
        if (backups != nullptr) {
            const char* previous = getenv(name.c_str());
            backups->push_back({
                name, previous == nullptr ? optional<string>{} : optional<string>{previous}});
        }
        if (setenv(name.c_str(), value.c_str(), 1) != 0) {
            error = name + ": " + strerror(errno);
            if (backups != nullptr) {
                restore_environment(*backups);
                backups->clear();
            }
            return false;
        }
    }
    return true;
}

int record_status(int status) {
    if (status != EXIT_SIGNAL) {
        set_shell_last_status(status);
    }
    return status;
}

bool reset_child_signals() {
    struct sigaction default_action {};
    default_action.sa_handler = SIG_DFL;
    sigemptyset(&default_action.sa_mask);
    for (int signal_number : {SIGINT, SIGQUIT, SIGTSTP, SIGTTIN, SIGTTOU}) {
        if (sigaction(signal_number, &default_action, nullptr) != 0) {
            return false;
        }
    }
    return true;
}

int wait_status_to_exit_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
}

int wait_for_process(pid_t pid, int* raw_status = nullptr) {
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            cerr << "myshell: waitpid: " << strerror(errno) << '\n';
            return 1;
        }
    }
    if (raw_status != nullptr) {
        *raw_status = status;
    }
    return wait_status_to_exit_status(status);
}

[[noreturn]] void exec_external_command(
    const ParsedCommand& command,
    const CommandResolution& resolution) {
    if (!resolution.path) {
        if (resolution.error_number == ENOENT || resolution.error_number == ENOTDIR) {
            cerr << command.name << ": command not found\n" << flush;
            _exit(127);
        }
        cerr << command.name << ": " << strerror(resolution.error_number)
             << '\n' << flush;
        _exit(126);
    }

    vector<char*> arguments;
    arguments.push_back(const_cast<char*>(resolution.path->c_str()));
    for (const auto& argument : command.arguments) {
        arguments.push_back(const_cast<char*>(argument.c_str()));
    }
    arguments.push_back(nullptr);

    execv(resolution.path->c_str(), arguments.data());
    const int exec_error = errno;
    cerr << command.name << ": " << strerror(exec_error) << '\n' << flush;
    _exit(exec_error == ENOENT ? 127 : 126);
}

int run_external_command(const ParsedCommand& command, const string& source) {
    optional<CommandResolution> resolution;
    if (!assigns_path(command)) {
        resolution = resolve_command_for_execution(command.name);
    }
    cout.flush();
    cerr.flush();

    const pid_t pid = fork();
    if (pid < 0) {
        cerr << command.name << ": fork failed: " << strerror(errno) << '\n';
        return 1;
    }
    if (pid == 0) {
        if (setpgid(0, 0) < 0) {
            cerr << command.name << ": cannot create process group: "
                 << strerror(errno) << '\n' << flush;
            _exit(1);
        }
        if (!reset_child_signals()) {
            cerr << "myshell: cannot reset child signals: "
                 << strerror(errno) << '\n' << flush;
            _exit(1);
        }
        string environment_error;
        if (!apply_environment(command, nullptr, environment_error)) {
            cerr << command.name << ": " << environment_error << '\n' << flush;
            _exit(1);
        }
        string error;
        if (!apply_redirections(command.redirections, error)) {
            cerr << command.name << ": " << error << '\n' << flush;
            _exit(1);
        }
        if (!resolution) {
            path_cache.clear();
            resolution = resolve_command_for_execution(command.name);
        }
        exec_external_command(command, *resolution);
    }
    if (setpgid(pid, pid) < 0 && errno != EACCES && errno != ESRCH) {
        const int group_error = errno;
        kill(pid, SIGTERM);
        wait_for_process(pid);
        cerr << command.name << ": cannot create process group: "
             << strerror(group_error) << '\n';
        return 1;
    }
    if (!give_terminal_to(pid)) {
        const int terminal_error = errno;
        kill(-pid, SIGTERM);
        wait_for_process(pid);
        reclaim_shell_terminal();
        cerr << command.name << ": cannot give terminal to command: "
             << strerror(terminal_error) << '\n';
        return 1;
    }
    const int job_id = register_job(
        pid, {pid}, source.empty() ? command.name : source, false);
    return wait_for_job(job_id);
}

int run_function(const string& name, int function_depth) {
    if (function_depth >= maximum_function_depth) {
        cerr << name << ": maximum function recursion depth exceeded\n";
        return 1;
    }

    // Copy the definition so `unset -f` during execution cannot invalidate it.
    const ShellFunction function = shell_functions.at(name);
    int status = 0;
    for (const auto& command : function.commands) {
        PipelineParseResult parsed = parse_pipeline(command.source);
        if (parsed.has_error() || !parsed.pipeline) {
            cerr << name << ": cannot parse stored function command: "
                 << parsed.error << '\n';
            return record_status(2);
        }
        status = execute_pipeline(move(*parsed.pipeline), function_depth + 1);
        if (status == EXIT_SIGNAL) {
            return status;
        }
    }
    return status;
}

int dispatch_in_current_process(ParsedCommand command, int function_depth) {
    if (command.name.empty()) {
        return 0;
    }
    if (command.name == "exec") {
        return run_exec_builtin(command);
    }
    if (is_shell_function(command.name)) {
        return run_function(command.name, function_depth);
    }
    if (is_builtin(command.name)) {
        return run_builtin(command.name, command.arguments);
    }
    return -1;
}

int run_parent_command_with_redirections(
    ParsedCommand command,
    int function_depth) {
    cout.flush();
    cerr.flush();

    const bool persistent_environment = command.name.empty();
    vector<EnvironmentBackup> environment_backups;
    string environment_error;
    if (!apply_environment(
            command,
            persistent_environment ? nullptr : &environment_backups,
            environment_error)) {
        cerr << (command.name.empty() ? "myshell" : command.name)
             << ": " << environment_error << '\n';
        return 1;
    }
    if (assigns_path(command)) {
        path_cache.clear();
    }

    vector<DescriptorBackup> backups;
    string error;
    if (!apply_redirections_transactionally(command.redirections, backups, error)) {
        cerr << (command.name.empty() ? "myshell" : command.name)
             << ": " << error << '\n';
        restore_environment(environment_backups);
        return 1;
    }
    command.redirections.clear();
    const bool path_was_assigned = assigns_path(command);
    const int status = dispatch_in_current_process(move(command), function_depth);

    cout.flush();
    cerr.flush();
    restore_descriptors(backups);
    restore_environment(environment_backups);
    if (path_was_assigned && !persistent_environment) {
        path_cache.clear();
    }
    cout.clear();
    cerr.clear();
    return status;
}

void close_pipes(const vector<array<int, 2>>& pipes) {
    for (const auto& pipe_fds : pipes) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
    }
}

[[noreturn]] void run_pipeline_stage(
    ParsedCommand command,
    int function_depth,
    size_t index,
    const vector<array<int, 2>>& pipes,
    pid_t process_group) {
    if (setpgid(0, process_group) < 0) {
        cerr << "myshell: cannot join job process group: "
             << strerror(errno) << '\n' << flush;
        _exit(1);
    }
    if (!reset_child_signals()) {
        cerr << "myshell: cannot reset child signals: "
             << strerror(errno) << '\n' << flush;
        _exit(1);
    }

    if (index > 0 && dup2(pipes[index - 1][0], STDIN_FILENO) < 0) {
        cerr << "myshell: pipe input: " << strerror(errno) << '\n' << flush;
        _exit(1);
    }
    if (index < pipes.size() && dup2(pipes[index][1], STDOUT_FILENO) < 0) {
        cerr << "myshell: pipe output: " << strerror(errno) << '\n' << flush;
        _exit(1);
    }
    close_pipes(pipes);

    string environment_error;
    if (!apply_environment(command, nullptr, environment_error)) {
        cerr << (command.name.empty() ? "myshell" : command.name)
             << ": " << environment_error << '\n' << flush;
        _exit(1);
    }
    if (assigns_path(command)) {
        path_cache.clear();
    }

    string error;
    if (!apply_redirections(command.redirections, error)) {
        cerr << (command.name.empty() ? "myshell" : command.name)
             << ": " << error << '\n' << flush;
        _exit(1);
    }
    command.redirections.clear();

    const int status = dispatch_in_current_process(command, function_depth);
    if (status >= 0) {
        cout.flush();
        cerr.flush();
        _exit(status == EXIT_SIGNAL ? shell_requested_exit_status() : status);
    }
    exec_external_command(command, resolve_command_for_execution(command.name));
}

} // namespace

int execute_command(
    ParsedCommand command,
    int function_depth,
    const string& source) {
    string alias_error;
    if (!expand_aliases(command, alias_error)) {
        cerr << "myshell: alias: " << alias_error << '\n';
        return record_status(2);
    }
    if (command.empty()) {
        return record_status(0);
    }

    if (command.name == "exec") {
        const bool persistent_environment = command.arguments.empty();
        vector<EnvironmentBackup> backups;
        string error;
        if (!apply_environment(
                command, persistent_environment ? nullptr : &backups, error)) {
            cerr << "exec: " << error << '\n';
            return record_status(1);
        }
        if (assigns_path(command)) {
            path_cache.clear();
        }
        const int status = run_exec_builtin(command);
        restore_environment(backups);
        if (assigns_path(command) && !persistent_environment) {
            path_cache.clear();
        }
        return record_status(status);
    }
    if (command.name.empty()
        || is_shell_function(command.name)
        || is_builtin(command.name)) {
        return record_status(
            run_parent_command_with_redirections(move(command), function_depth));
    }
    return record_status(run_external_command(command, source));
}

int execute_pipeline(ParsedPipeline pipeline, int function_depth) {
    if (pipeline.empty()) {
        return record_status(0);
    }
    if (pipeline.commands.size() == 1 && !pipeline.background) {
        return execute_command(
            move(pipeline.commands.front()), function_depth, pipeline.source);
    }

    for (auto& command : pipeline.commands) {
        string alias_error;
        if (!expand_aliases(command, alias_error)) {
            cerr << "myshell: alias: " << alias_error << '\n';
            return record_status(2);
        }
    }

    vector<array<int, 2>> pipes(pipeline.commands.size() - 1);
    size_t opened_pipes = 0;
    for (; opened_pipes < pipes.size(); ++opened_pipes) {
        if (pipe(pipes[opened_pipes].data()) < 0) {
            const int pipe_error = errno;
            for (size_t i = 0; i < opened_pipes; ++i) {
                close(pipes[i][0]);
                close(pipes[i][1]);
            }
            cerr << "myshell: pipe: " << strerror(pipe_error) << '\n';
            return record_status(1);
        }
    }

    cout.flush();
    cerr.flush();
    vector<pid_t> children;
    children.reserve(pipeline.commands.size());
    pid_t process_group = 0;
    bool terminal_given = false;
    for (size_t i = 0; i < pipeline.commands.size(); ++i) {
        const pid_t pid = fork();
        if (pid < 0) {
            const int fork_error = errno;
            close_pipes(pipes);
            if (process_group > 0) {
                kill(-process_group, SIGTERM);
            }
            for (pid_t child : children) {
                wait_for_process(child);
            }
            if (terminal_given) {
                reclaim_shell_terminal();
            }
            cerr << "myshell: fork: " << strerror(fork_error) << '\n';
            return record_status(1);
        }
        if (pid == 0) {
            run_pipeline_stage(
                move(pipeline.commands[i]), function_depth, i, pipes, process_group);
        }
        if (process_group == 0) {
            process_group = pid;
        }
        if (setpgid(pid, process_group) < 0 && errno != EACCES && errno != ESRCH) {
            const int group_error = errno;
            close_pipes(pipes);
            kill(-process_group, SIGTERM);
            children.push_back(pid);
            for (pid_t child : children) {
                wait_for_process(child);
            }
            if (terminal_given) {
                reclaim_shell_terminal();
            }
            cerr << "myshell: setpgid: " << strerror(group_error) << '\n';
            return record_status(1);
        }
        if (i == 0 && !pipeline.background) {
            if (!give_terminal_to(process_group)) {
                const int terminal_error = errno;
                close_pipes(pipes);
                kill(-process_group, SIGTERM);
                children.push_back(pid);
                for (pid_t child : children) {
                    wait_for_process(child);
                }
                reclaim_shell_terminal();
                cerr << "myshell: tcsetpgrp: " << strerror(terminal_error) << '\n';
                return record_status(1);
            }
            terminal_given = true;
        }
        children.push_back(pid);
    }
    close_pipes(pipes);

    const int job_id = register_job(
        process_group,
        children,
        pipeline.source.empty() ? pipeline.commands.front().name : pipeline.source,
        pipeline.background);
    if (pipeline.background) {
        return record_status(0);
    }
    return record_status(wait_for_job(job_id));
}

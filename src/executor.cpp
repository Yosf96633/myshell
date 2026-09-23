#include "myshell/executor.hpp"

#include "myshell/builtins.hpp"
#include "myshell/functions.hpp"
#include "myshell/path_search.hpp"

#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <vector>

using namespace std;

namespace {

constexpr int maximum_function_depth = 64;

int run_external_command(const string& command, const vector<string>& args) {
    const auto resolved = resolve_command(command);
    if (!resolved) {
        cerr << command << ": command not found\n";
        return 127;
    }

    const pid_t pid = fork();
    if (pid == -1) {
        cerr << command << ": fork failed\n";
        return 1;
    }

    if (pid == 0) {
        // The interactive shell ignores these signals; external commands must not.
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);

        vector<char*> c_args;
        c_args.push_back(const_cast<char*>(resolved->c_str()));
        for (const auto& argument : args) {
            c_args.push_back(const_cast<char*>(argument.c_str()));
        }
        c_args.push_back(nullptr);

        execv(c_args[0], c_args.data());
        cerr << command << ": exec failed\n";
        _exit(126);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
        cerr << command << ": wait failed\n";
        return 1;
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return 1;
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
        status = execute_command(command.parsed, function_depth + 1);
        if (status == EXIT_SIGNAL) {
            return status;
        }
    }
    return status;
}

} // namespace

int execute_command(ParsedCommand command, int function_depth) {
    string alias_error;
    if (!expand_aliases(command, alias_error)) {
        cerr << "myshell: alias: " << alias_error << '\n';
        return 2;
    }
    if (command.empty()) {
        return 0;
    }

    if (is_shell_function(command.name)) {
        return run_function(command.name, function_depth);
    }
    if (is_builtin(command.name)) {
        return run_builtin(command.name, command.arguments);
    }
    return run_external_command(command.name, command.arguments);
}

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
        status = execute_command(command.words, function_depth + 1);
        if (status == EXIT_SIGNAL) {
            return status;
        }
    }
    return status;
}

} // namespace

int execute_command(vector<string> tokens, int function_depth) {
    expand_aliases(tokens);
    if (tokens.empty()) {
        return 0;
    }

    const string command = tokens.front();
    const vector<string> args(tokens.begin() + 1, tokens.end());

    if (is_shell_function(command)) {
        return run_function(command, function_depth);
    }
    if (is_builtin(command)) {
        return run_builtin(command, args);
    }
    return run_external_command(command, args);
}

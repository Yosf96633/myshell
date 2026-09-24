#include "myshell/exec_builtin.hpp"

#include "myshell/path_search.hpp"
#include "myshell/redirection.hpp"

#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

int run_exec_builtin(const ParsedCommand& command) {
    string executable;
    if (!command.arguments.empty()) {
        const CommandResolution resolution =
            resolve_command_for_execution(command.arguments.front());
        if (!resolution.path) {
            if (resolution.error_number == ENOENT
                || resolution.error_number == ENOTDIR) {
                cerr << "exec: " << command.arguments.front() << ": not found\n";
                return 127;
            }
            cerr << "exec: " << command.arguments.front() << ": "
                 << strerror(resolution.error_number) << '\n';
            return 126;
        }
        executable = *resolution.path;
    }

    // Do not let text buffered before `exec` leak into a newly redirected stream.
    cout.flush();
    cerr.flush();

    vector<DescriptorBackup> backups;
    string error;
    if (!apply_redirections_transactionally(command.redirections, backups, error)) {
        cerr << "exec: " << error << '\n';
        return 1;
    }

    if (command.arguments.empty()) {
        discard_descriptor_backups(backups);
        return 0;
    }

    vector<char*> argv;
    argv.push_back(executable.data());
    for (size_t i = 1; i < command.arguments.size(); ++i) {
        argv.push_back(const_cast<char*>(command.arguments[i].c_str()));
    }
    argv.push_back(nullptr);

    struct sigaction default_action {};
    struct sigaction previous_interrupt {};
    struct sigaction previous_quit {};
    default_action.sa_handler = SIG_DFL;
    sigemptyset(&default_action.sa_mask);

    if (sigaction(SIGINT, &default_action, &previous_interrupt) < 0) {
        const int signal_error = errno;
        restore_descriptors(backups);
        cerr << "exec: cannot reset SIGINT: " << strerror(signal_error) << '\n';
        return 126;
    }
    if (sigaction(SIGQUIT, &default_action, &previous_quit) < 0) {
        const int signal_error = errno;
        sigaction(SIGINT, &previous_interrupt, nullptr);
        restore_descriptors(backups);
        cerr << "exec: cannot reset SIGQUIT: " << strerror(signal_error) << '\n';
        return 126;
    }

    execv(executable.c_str(), argv.data());
    const int exec_error = errno;
    sigaction(SIGQUIT, &previous_quit, nullptr);
    sigaction(SIGINT, &previous_interrupt, nullptr);
    restore_descriptors(backups);
    cerr << "exec: " << command.arguments.front() << ": "
         << strerror(exec_error) << '\n';
    return exec_error == ENOENT ? 127 : 126;
}

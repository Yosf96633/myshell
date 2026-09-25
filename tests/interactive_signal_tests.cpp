#include <poll.h>
#include <pty.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

using namespace std;

namespace {

constexpr int io_timeout_ms = 5000;

bool write_all(int descriptor, const string& value) {
    size_t written = 0;
    while (written < value.size()) {
        const ssize_t result = write(
            descriptor, value.data() + written, value.size() - written);
        if (result > 0) {
            written += static_cast<size_t>(result);
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }
    return true;
}

bool read_until(int descriptor, const string& expected, string& pending_output) {
    const auto deadline = chrono::steady_clock::now()
        + chrono::milliseconds(io_timeout_ms);

    while (chrono::steady_clock::now() < deadline) {
        const size_t match = pending_output.find(expected);
        if (match != string::npos) {
            pending_output.erase(0, match + expected.size());
            return true;
        }

        const auto remaining = chrono::duration_cast<chrono::milliseconds>(
            deadline - chrono::steady_clock::now());
        pollfd input{descriptor, POLLIN, 0};
        const int poll_result = poll(
            &input, 1, static_cast<int>(max<int64_t>(1, remaining.count())));
        if (poll_result < 0 && errno == EINTR) {
            continue;
        }
        if (poll_result <= 0) {
            break;
        }

        char buffer[1024];
        const ssize_t count = read(descriptor, buffer, sizeof(buffer));
        if (count > 0) {
            pending_output.append(buffer, static_cast<size_t>(count));
        } else if (count < 0 && errno == EINTR) {
            continue;
        } else {
            break;
        }
    }

    cerr << "timed out waiting for terminal output containing `"
         << expected << "'\n";
    return false;
}

int wait_for_exit(pid_t process) {
    const auto deadline = chrono::steady_clock::now()
        + chrono::milliseconds(io_timeout_ms);
    int status = 0;
    while (chrono::steady_clock::now() < deadline) {
        const pid_t result = waitpid(process, &status, WNOHANG);
        if (result == process) {
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
            if (WIFSIGNALED(status)) {
                return 128 + WTERMSIG(status);
            }
            return -1;
        }
        if (result < 0 && errno != EINTR) {
            return -1;
        }
        this_thread::sleep_for(chrono::milliseconds(10));
    }

    kill(process, SIGKILL);
    waitpid(process, &status, 0);
    return -1;
}

struct ShellSession {
    pid_t process = -1;
    int terminal = -1;
    string pending_output;
};

ShellSession start_shell(const string& shell_path) {
    int terminal = -1;
    winsize size{}; // A zero size skips the decorative banner.
    const pid_t process = forkpty(&terminal, nullptr, nullptr, &size);
    if (process == 0) {
        execl(shell_path.c_str(), shell_path.c_str(), nullptr);
        _exit(126);
    }
    return {process, terminal, {}};
}

bool finish_session(ShellSession session, int expected_status) {
    if (session.process <= 0) {
        if (session.terminal >= 0) {
            close(session.terminal);
        }
        cerr << "could not start shell under a pseudo-terminal\n";
        return false;
    }
    const int actual_status = wait_for_exit(session.process);
    close(session.terminal);
    if (actual_status != expected_status) {
        cerr << "shell exited with " << actual_status
             << ", expected " << expected_status << '\n';
        return false;
    }
    return true;
}

bool test_prompt_interrupt(const string& shell_path) {
    ShellSession session = start_shell(shell_path);
    bool ok = session.process > 0
        && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "partial\003");
    ok = ok && read_until(session.terminal, "^C\r\n", session.pending_output);
    ok = ok && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "exit\n");
    return finish_session(session, ok ? 130 : -1) && ok;
}

bool test_foreground_signal(
    const string& shell_path,
    char control_character,
    int expected_status) {
    ShellSession session = start_shell(shell_path);
    bool ok = session.process > 0
        && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(
        session.terminal, "/bin/sh -c 'while :; do :; done'\n");
    ok = ok && read_until(
        session.terminal, "Args : while :; do :; done", session.pending_output);
    this_thread::sleep_for(chrono::milliseconds(100));
    ok = ok && write_all(session.terminal, string(1, control_character));
    ok = ok && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "exit\n");
    return finish_session(session, ok ? expected_status : -1) && ok;
}

bool test_prompt_quit_is_ignored(const string& shell_path) {
    ShellSession session = start_shell(shell_path);
    bool ok = session.process > 0
        && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, string(1, static_cast<char>(28)));
    ok = ok && write_all(session.terminal, "exit 0\n");
    return finish_session(session, ok ? 0 : -1) && ok;
}

bool test_background_and_foreground(const string& shell_path) {
    ShellSession session = start_shell(shell_path);
    bool ok = session.process > 0
        && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "/bin/sleep 1 &\n");
    ok = ok && read_until(session.terminal, "[1] ", session.pending_output);
    ok = ok && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "jobs\n");
    ok = ok && read_until(
        session.terminal, "Running\t/bin/sleep 1", session.pending_output);
    ok = ok && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "fg %1\n");
    ok = ok && read_until(session.terminal, "/bin/sleep 1\r\n", session.pending_output);
    ok = ok && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "exit 0\n");
    return finish_session(session, ok ? 0 : -1) && ok;
}

bool test_stop_background_resume_and_interrupt(const string& shell_path) {
    ShellSession session = start_shell(shell_path);
    bool ok = session.process > 0
        && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(
        session.terminal, "/bin/sh -c 'while :; do :; done'\n");
    ok = ok && read_until(
        session.terminal, "Args : while :; do :; done", session.pending_output);
    this_thread::sleep_for(chrono::milliseconds(100));
    ok = ok && write_all(session.terminal, string(1, static_cast<char>(26)));
    ok = ok && read_until(
        session.terminal,
        "Stopped\t/bin/sh -c 'while :; do :; done'",
        session.pending_output);
    ok = ok && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "bg %1\n");
    ok = ok && read_until(
        session.terminal,
        "Running\t/bin/sh -c 'while :; do :; done'",
        session.pending_output);
    ok = ok && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "fg %1\n");
    ok = ok && read_until(
        session.terminal,
        "/bin/sh -c 'while :; do :; done'\r\n",
        session.pending_output);
    this_thread::sleep_for(chrono::milliseconds(100));
    ok = ok && write_all(session.terminal, string(1, static_cast<char>(3)));
    ok = ok && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "exit\n");
    return finish_session(session, ok ? 130 : -1) && ok;
}

bool test_pipeline_process_group(const string& shell_path) {
    const string command =
        "/bin/sh -c 'while :; do :; done' | /bin/cat";
    ShellSession session = start_shell(shell_path);
    bool ok = session.process > 0
        && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, command + "\n");
    ok = ok && read_until(
        session.terminal, "Args : while :; do :; done", session.pending_output);
    this_thread::sleep_for(chrono::milliseconds(100));
    ok = ok && write_all(session.terminal, string(1, static_cast<char>(26)));
    ok = ok && read_until(
        session.terminal, "Stopped\t" + command, session.pending_output);
    ok = ok && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "fg %1\n");
    ok = ok && read_until(session.terminal, command + "\r\n", session.pending_output);
    this_thread::sleep_for(chrono::milliseconds(100));
    ok = ok && write_all(session.terminal, string(1, static_cast<char>(3)));
    ok = ok && read_until(session.terminal, "$ ", session.pending_output);
    ok = ok && write_all(session.terminal, "exit\n");
    return finish_session(session, ok ? 130 : -1) && ok;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        cerr << "usage: interactive_signal_tests /path/to/myshell\n";
        return 2;
    }

    int failures = 0;
    if (!test_prompt_interrupt(argv[1])) {
        cerr << "FAIL: Ctrl+C should cancel input and set status 130\n";
        ++failures;
    }
    if (!test_foreground_signal(argv[1], static_cast<char>(3), 130)) {
        cerr << "FAIL: Ctrl+C should interrupt the foreground command only\n";
        ++failures;
    }
    if (!test_foreground_signal(argv[1], static_cast<char>(28), 131)) {
        cerr << "FAIL: Ctrl+\\ should quit the foreground command only\n";
        ++failures;
    }
    if (!test_prompt_quit_is_ignored(argv[1])) {
        cerr << "FAIL: Ctrl+\\ should be ignored while reading a prompt\n";
        ++failures;
    }
    if (!test_background_and_foreground(argv[1])) {
        cerr << "FAIL: background jobs should be listed and foregrounded\n";
        ++failures;
    }
    if (!test_stop_background_resume_and_interrupt(argv[1])) {
        cerr << "FAIL: stopped jobs should resume with bg and fg\n";
        ++failures;
    }
    if (!test_pipeline_process_group(argv[1])) {
        cerr << "FAIL: every stage of a pipeline should share one job group\n";
        ++failures;
    }

    if (failures != 0) {
        return 1;
    }
    cout << "All interactive signal tests passed\n";
    return 0;
}

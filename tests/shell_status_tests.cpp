#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using namespace std;

namespace {

struct StatusCase {
    string description;
    string input;
    int expected_status;
};

bool write_all(int descriptor, const string& input) {
    size_t written = 0;
    while (written < input.size()) {
        const ssize_t result = write(
            descriptor, input.data() + written, input.size() - written);
        if (result > 0) {
            written += static_cast<size_t>(result);
        } else if (result < 0 && errno == EINTR) {
            continue;
        } else if (result < 0 && errno == EPIPE) {
            return true;
        } else {
            cerr << "test harness write failed: " << strerror(errno) << '\n';
            return false;
        }
    }
    return true;
}

int run_shell(const string& shell_path, const string& input) {
    int input_pipe[2];
    if (pipe(input_pipe) != 0) {
        cerr << "test harness pipe failed: " << strerror(errno) << '\n';
        return -1;
    }

    const int null_output = open("/dev/null", O_WRONLY);
    if (null_output < 0) {
        cerr << "test harness open failed: " << strerror(errno) << '\n';
        close(input_pipe[0]);
        close(input_pipe[1]);
        return -1;
    }

    const pid_t child = fork();
    if (child < 0) {
        cerr << "test harness fork failed: " << strerror(errno) << '\n';
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(null_output);
        return -1;
    }
    if (child == 0) {
        if (dup2(input_pipe[0], STDIN_FILENO) < 0
            || dup2(null_output, STDOUT_FILENO) < 0
            || dup2(null_output, STDERR_FILENO) < 0) {
            _exit(125);
        }
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(null_output);
        execl(shell_path.c_str(), shell_path.c_str(), nullptr);
        _exit(126);
    }

    close(input_pipe[0]);
    close(null_output);
    const bool write_succeeded = write_all(input_pipe[1], input);
    close(input_pipe[1]);
    if (!write_succeeded) {
        return -1;
    }

    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            cerr << "test harness waitpid failed: " << strerror(errno) << '\n';
            return -1;
        }
    }
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        cerr << "usage: shell_status_tests /path/to/myshell\n";
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);

    const vector<StatusCase> cases = {
        {"successful command", "/bin/true\n", 0},
        {"failed command", "/bin/false\n", 1},
        {"command not found", "definitely-not-a-myshell-command\n", 127},
        {"non-executable path", "/\n", 126},
        {"syntax error", "echo 'unfinished\n", 2},
        {"pipeline final command", "/bin/true | /bin/sh -c 'exit 9'\n", 9},
        {"signal termination", "/bin/sh -c 'kill -TERM $$'\n", 143},
        {"exit uses previous status", "/bin/false\nexit\n", 1},
        {"explicit exit status", "exit 42\n", 42},
        {"negative exit status", "exit -1\n", 255},
        {"invalid exit status", "exit nope\n", 2},
        {"too many exit arguments", "exit 1 2\n", 1},
        {"status expansion", "/bin/sh -c 'exit 23'\nexit $?\n", 23},
        {"function-time status expansion",
         "finish() { exit $?; }\n/bin/sh -c 'exit 31'\nfinish\n", 31},
    };

    int failures = 0;
    for (const auto& test : cases) {
        const int actual = run_shell(argv[1], test.input);
        if (actual != test.expected_status) {
            cerr << "FAIL: " << test.description << " returned " << actual
                 << ", expected " << test.expected_status << '\n';
            ++failures;
        }
    }

    if (failures != 0) {
        cerr << failures << " shell status test(s) failed\n";
        return 1;
    }
    cout << "All shell status tests passed\n";
    return 0;
}

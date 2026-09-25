#include "myshell/job_control.hpp"

#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <csignal>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

using namespace std;

namespace {

struct JobProcess {
    pid_t pid = -1;
    int status = 0;
    bool completed = false;
    bool stopped = false;
};

struct Job {
    int id = 0;
    pid_t process_group = -1;
    vector<JobProcess> processes;
    string command;
    bool stopped_notification_printed = false;
    termios terminal_modes{};
    bool have_terminal_modes = false;
};

vector<Job> jobs;
int next_job_id = 1;
bool interactive = false;
pid_t shell_process_group = -1;
termios shell_terminal_modes{};
bool have_shell_terminal_modes = false;

Job* find_job(int id) {
    auto job = find_if(jobs.begin(), jobs.end(), [id](const Job& candidate) {
        return candidate.id == id;
    });
    return job == jobs.end() ? nullptr : &*job;
}

bool job_completed(const Job& job) {
    return all_of(job.processes.begin(), job.processes.end(), [](const auto& process) {
        return process.completed;
    });
}

bool job_stopped(const Job& job) {
    bool has_stopped_process = false;
    for (const auto& process : job.processes) {
        if (!process.completed && !process.stopped) {
            return false;
        }
        has_stopped_process = has_stopped_process || process.stopped;
    }
    return has_stopped_process;
}

const char* job_state(const Job& job) {
    if (job_completed(job)) {
        return "Done";
    }
    if (job_stopped(job)) {
        return "Stopped";
    }
    return "Running";
}

void update_process_status(Job& job, pid_t pid, int status) {
    auto process = find_if(
        job.processes.begin(), job.processes.end(), [pid](const auto& candidate) {
            return candidate.pid == pid;
        });
    if (process == job.processes.end()) {
        return;
    }

    process->status = status;
    if (WIFSTOPPED(status)) {
        process->stopped = true;
    } else if (WIFCONTINUED(status)) {
        process->stopped = false;
    } else if (WIFEXITED(status) || WIFSIGNALED(status)) {
        process->completed = true;
        process->stopped = false;
    }
}

int process_exit_status(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    if (WIFSTOPPED(status)) {
        return 128 + WSTOPSIG(status);
    }
    return 1;
}

void report_signal_status(int status) {
    if (!interactive || !WIFSIGNALED(status)) {
        return;
    }

    const int signal_number = WTERMSIG(status);
    if (signal_number == SIGINT) {
        cout << '\n' << flush;
        return;
    }

    const char* description = strsignal(signal_number);
    cerr << (description == nullptr ? "Terminated" : description);
#ifdef WCOREDUMP
    if (WCOREDUMP(status)) {
        cerr << " (core dumped)";
    }
#endif
    cerr << '\n' << flush;
}

int final_job_status(const Job& job) {
    if (job.processes.empty()) {
        return 1;
    }
    return process_exit_status(job.processes.back().status);
}

void print_job(const Job& job, const char* state = nullptr) {
    cout << '[' << job.id << "] " << (state == nullptr ? job_state(job) : state)
         << "\t" << job.command << '\n';
}

void erase_job(int id) {
    jobs.erase(remove_if(jobs.begin(), jobs.end(), [id](const Job& job) {
        return job.id == id;
    }), jobs.end());
}

optional<int> parse_job_id(const vector<string>& arguments, const string& command) {
    if (arguments.size() > 1) {
        cerr << command << ": usage: " << command << " [job]\n";
        return nullopt;
    }
    if (jobs.empty()) {
        cerr << command << ": no current job\n";
        return nullopt;
    }
    if (arguments.empty()) {
        return jobs.back().id;
    }

    string value = arguments.front();
    if (!value.empty() && value.front() == '%') {
        value.erase(value.begin());
    }
    int id = 0;
    const auto parsed = from_chars(value.data(), value.data() + value.size(), id);
    if (value.empty() || parsed.ec != errc{} || parsed.ptr != value.data() + value.size()
        || id <= 0 || find_job(id) == nullptr) {
        cerr << command << ": " << arguments.front() << ": no such job\n";
        return nullopt;
    }
    return id;
}

} // namespace

void initialize_job_control() {
    interactive = isatty(STDIN_FILENO);
    if (!interactive) {
        return;
    }

    struct sigaction default_stop {};
    default_stop.sa_handler = SIG_DFL;
    sigemptyset(&default_stop.sa_mask);
    if (sigaction(SIGTTIN, &default_stop, nullptr) < 0) {
        cerr << "myshell: cannot configure job-control signals: "
             << strerror(errno) << '\n';
        interactive = false;
        return;
    }

    shell_process_group = getpgrp();
    pid_t foreground_group = tcgetpgrp(STDIN_FILENO);
    if (foreground_group < 0) {
        interactive = false;
        return;
    }
    while (foreground_group != shell_process_group) {
        if (kill(-shell_process_group, SIGTTIN) < 0 && errno != EINTR) {
            interactive = false;
            return;
        }
        shell_process_group = getpgrp();
        foreground_group = tcgetpgrp(STDIN_FILENO);
        if (foreground_group < 0) {
            interactive = false;
            return;
        }
    }

    struct sigaction ignore_terminal_stop {};
    ignore_terminal_stop.sa_handler = SIG_IGN;
    sigemptyset(&ignore_terminal_stop.sa_mask);
    if (sigaction(SIGTTOU, &ignore_terminal_stop, nullptr) < 0) {
        cerr << "myshell: cannot configure terminal handoff: "
             << strerror(errno) << '\n';
        interactive = false;
        return;
    }

    const pid_t shell_pid = getpid();
    if (setpgid(shell_pid, shell_pid) < 0 && errno != EACCES && errno != EPERM) {
        cerr << "myshell: cannot create shell process group: "
             << strerror(errno) << '\n';
        interactive = false;
        return;
    }
    shell_process_group = getpgrp();
    if (tcsetpgrp(STDIN_FILENO, shell_process_group) < 0) {
        cerr << "myshell: cannot claim terminal: " << strerror(errno) << '\n';
        interactive = false;
        return;
    }
    have_shell_terminal_modes =
        tcgetattr(STDIN_FILENO, &shell_terminal_modes) == 0;
}

bool give_terminal_to(pid_t process_group) {
    return !interactive || tcsetpgrp(STDIN_FILENO, process_group) == 0;
}

void reclaim_shell_terminal() {
    if (!interactive) {
        return;
    }
    while (tcsetpgrp(STDIN_FILENO, shell_process_group) < 0 && errno == EINTR) {
    }
    if (have_shell_terminal_modes) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &shell_terminal_modes);
    }
}

int register_job(
    pid_t process_group,
    const vector<pid_t>& processes,
    const string& command,
    bool background) {
    Job job;
    job.id = next_job_id++;
    job.process_group = process_group;
    job.command = command;
    for (pid_t process : processes) {
        job.processes.push_back({process});
    }
    const int id = job.id;
    jobs.push_back(move(job));
    if (background) {
        cout << '[' << id << "] " << process_group << '\n';
    }
    return id;
}

int wait_for_job(int job_id) {
    Job* job = find_job(job_id);
    if (job == nullptr) {
        return 1;
    }

    while (!job_completed(*job) && !job_stopped(*job)) {
        int status = 0;
        const pid_t process = waitpid(
            -job->process_group, &status, WUNTRACED | WCONTINUED);
        if (process < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno != ECHILD) {
                cerr << "myshell: waitpid: " << strerror(errno) << '\n';
            } else {
                for (auto& remaining : job->processes) {
                    if (!remaining.completed) {
                        remaining.status = 0;
                        remaining.completed = true;
                        remaining.stopped = false;
                    }
                }
            }
            break;
        }
        update_process_status(*job, process, status);
    }

    job = find_job(job_id);
    if (job == nullptr) {
        reclaim_shell_terminal();
        return 1;
    }
    if (interactive && job_stopped(*job)) {
        job->have_terminal_modes =
            tcgetattr(STDIN_FILENO, &job->terminal_modes) == 0;
    }
    reclaim_shell_terminal();
    const int result = final_job_status(*job);
    if (job_completed(*job)) {
        report_signal_status(job->processes.back().status);
        erase_job(job_id);
    } else if (job_stopped(*job)) {
        job->stopped_notification_printed = true;
        print_job(*job, "Stopped");
    }
    return result;
}

void update_background_jobs() {
    for (auto& job : jobs) {
        while (true) {
            int status = 0;
            const pid_t process = waitpid(
                -job.process_group, &status, WNOHANG | WUNTRACED | WCONTINUED);
            if (process > 0) {
                update_process_status(job, process, status);
                continue;
            }
            if (process < 0 && errno == EINTR) {
                continue;
            }
            break;
        }
    }

    for (auto& job : jobs) {
        if (job_completed(job)) {
            print_job(job, "Done");
        } else if (job_stopped(job) && !job.stopped_notification_printed) {
            print_job(job, "Stopped");
            job.stopped_notification_printed = true;
        }
    }
    jobs.erase(remove_if(jobs.begin(), jobs.end(), [](const Job& job) {
        return job_completed(job);
    }), jobs.end());
}

int builtin_jobs(const vector<string>& arguments) {
    if (!arguments.empty()) {
        cerr << "jobs: usage: jobs\n";
        return 2;
    }
    update_background_jobs();
    for (const auto& job : jobs) {
        print_job(job);
    }
    return 0;
}

int builtin_fg(const vector<string>& arguments) {
    update_background_jobs();
    const optional<int> id = parse_job_id(arguments, "fg");
    if (!id) {
        return 1;
    }
    Job* job = find_job(*id);
    if (job == nullptr) {
        return 1;
    }
    cout << job->command << '\n';
    job->stopped_notification_printed = false;
    if (!give_terminal_to(job->process_group)) {
        cerr << "fg: cannot give terminal to job: " << strerror(errno) << '\n';
        reclaim_shell_terminal();
        return 1;
    }
    if (job->have_terminal_modes) {
        tcsetattr(STDIN_FILENO, TCSADRAIN, &job->terminal_modes);
    }
    if (kill(-job->process_group, SIGCONT) < 0 && errno != ESRCH) {
        cerr << "fg: cannot continue job: " << strerror(errno) << '\n';
        reclaim_shell_terminal();
        return 1;
    }
    for (auto& process : job->processes) {
        process.stopped = false;
    }
    return wait_for_job(*id);
}

int builtin_bg(const vector<string>& arguments) {
    update_background_jobs();
    const optional<int> id = parse_job_id(arguments, "bg");
    if (!id) {
        return 1;
    }
    Job* job = find_job(*id);
    if (job == nullptr) {
        return 1;
    }
    if (kill(-job->process_group, SIGCONT) < 0) {
        cerr << "bg: cannot continue job: " << strerror(errno) << '\n';
        return 1;
    }
    for (auto& process : job->processes) {
        process.stopped = false;
    }
    job->stopped_notification_printed = false;
    print_job(*job, "Running");
    return 0;
}

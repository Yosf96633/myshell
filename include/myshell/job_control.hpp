#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

// Prepare an interactive shell to own its terminal and manage foreground
// process groups. Non-interactive shells keep job tracking but skip terminal
// handoff.
void initialize_job_control();

// Transfer the controlling terminal between the shell and a job.
bool give_terminal_to(pid_t process_group);
void reclaim_shell_terminal();

// Register a newly launched process group. The final PID must be the process
// whose status determines the pipeline status.
int register_job(
    pid_t process_group,
    const std::vector<pid_t>& processes,
    const std::string& command,
    bool background);

// Wait for a foreground job until it completes or stops.
int wait_for_job(int job_id);

// Reap and announce background state changes before displaying a prompt.
void update_background_jobs();

// Implement the interactive job-control built-ins.
int builtin_jobs(const std::vector<std::string>& arguments);
int builtin_fg(const std::vector<std::string>& arguments);
int builtin_bg(const std::vector<std::string>& arguments);

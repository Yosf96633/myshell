#include "myshell/shell_state.hpp"

namespace {

int last_status = 0;
int requested_exit_status = 0;

int normalize_status(int status) {
    status %= 256;
    return status < 0 ? status + 256 : status;
}

} // namespace

int shell_last_status() {
    return last_status;
}

void set_shell_last_status(int status) {
    last_status = normalize_status(status);
}

int shell_requested_exit_status() {
    return requested_exit_status;
}

void set_shell_requested_exit_status(int status) {
    requested_exit_status = normalize_status(status);
}

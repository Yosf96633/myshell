#pragma once

// Status of the most recently completed command, in the shell range 0-255.
int shell_last_status();
void set_shell_last_status(int status);

// Status selected by the `exit` builtin.
int shell_requested_exit_status();
void set_shell_requested_exit_status(int status);

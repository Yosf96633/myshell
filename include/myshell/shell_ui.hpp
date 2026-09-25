#pragma once

// Ignore interactive interrupt/quit signals in the shell itself. Foreground
// child processes restore the default dispositions before they execute.
void configure_interactive_signal_handling();

// Clear the visible terminal and draw a one-time launch banner.
void show_launch_screen();

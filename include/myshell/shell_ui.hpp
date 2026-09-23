#pragma once

// Owns the interactive shell's terminal screen and restores it on exit.
class ShellUi {
public:
    ShellUi();
    ~ShellUi();

    ShellUi(const ShellUi&) = delete;
    ShellUi& operator=(const ShellUi&) = delete;

    void refresh(bool force = false);

private:
    bool active_ = false;
    int columns_ = 0;
    int rows_ = 0;
    long last_clock_second_ = -1;
};

// Called while the interactive reader waits for a key.
void refresh_shell_header();

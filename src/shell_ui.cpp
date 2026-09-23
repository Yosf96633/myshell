#include "myshell/shell_ui.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <ctime>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

using namespace std;

namespace {

ShellUi* active_ui = nullptr;
constexpr string_view title = "YOSF'S SHELL";
constexpr int banner_height = 5;
constexpr int divider_row = banner_height + 1;
constexpr int command_start_row = divider_row + 1;
using Glyph = array<string_view, banner_height>;

Glyph glyph_for(char letter) {
    switch (letter) {
    case 'Y': return {"# #", "# #", " # ", " # ", " # "};
    case 'O': return {"###", "# #", "# #", "# #", "###"};
    case 'S': return {"###", "#  ", "###", "  #", "###"};
    case 'F': return {"###", "#  ", "## ", "#  ", "#  "};
    case 'H': return {"# #", "# #", "###", "# #", "# #"};
    case 'E': return {"###", "#  ", "## ", "#  ", "###"};
    case 'L': return {"#  ", "#  ", "#  ", "#  ", "###"};
    case '\'': return {"#", "#", " ", " ", " "};
    default: return {" ", " ", " ", " ", " "};
    }
}

const array<string, banner_height>& banner_lines() {
    static const array<string, banner_height> lines = [] {
        array<string, banner_height> result;
        for (size_t letter = 0; letter < title.size(); ++letter) {
            const Glyph glyph = glyph_for(title[letter]);
            for (int row = 0; row < banner_height; ++row) {
                if (letter > 0) {
                    result[row] += ' ';
                }
                result[row] += glyph[row];
            }
        }
        return result;
    }();
    return lines;
}

bool terminal_size(int& columns, int& rows) {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0
        || size.ws_col == 0 || size.ws_row < command_start_row + 1) {
        return false;
    }
    columns = size.ws_col;
    rows = size.ws_row;
    return true;
}

string current_datetime(time_t now) {
    tm local_time{};
    if (localtime_r(&now, &local_time) == nullptr) {
        return {};
    }

    char buffer[80];
    if (strftime(buffer, sizeof(buffer), "%A %d/%m/%Y %I:%M:%S %p", &local_time) == 0) {
        return {};
    }
    return buffer;
}

void draw_header(int columns, time_t now, bool redraw_banner) {
    const auto& banner = banner_lines();
    const string datetime = current_datetime(now);
    const bool fits_beside_banner =
        static_cast<int>(banner.front().size() + datetime.size() + 3) <= columns;

    // Save the command cursor while painting above the scroll area.
    cout << "\x1b" "7";
    if (redraw_banner) {
        for (int row = 0; row < banner_height; ++row) {
            cout << "\x1b[" << row + 1 << ";1H\x1b[2K\x1b[1;36m"
                 << banner[row].substr(0, static_cast<size_t>(max(0, columns - 1)))
                 << "\x1b[0m";
        }
    }

    if (columns > static_cast<int>(banner.front().size()) + 1) {
        cout << "\x1b[1;" << banner.front().size() + 2 << "H\x1b[K";
    }
    cout << "\x1b[" << divider_row << ";1H\x1b[2K";
    if (fits_beside_banner) {
        cout << "\x1b[90m"
             << string(static_cast<size_t>(max(0, columns - 1)), '-')
             << "\x1b[0m"
             << "\x1b[1;" << columns - static_cast<int>(datetime.size()) << "H"
             << "\x1b[2;37m" << datetime << "\x1b[0m";
    } else {
        const string visible = datetime.substr(0, static_cast<size_t>(max(0, columns - 1)));
        cout << "\x1b[" << columns - static_cast<int>(visible.size()) << "G"
             << "\x1b[2;37m" << visible << "\x1b[0m";
    }
    cout << "\x1b" "8" << flush;
}

} // namespace

ShellUi::ShellUi() {
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)
        || !terminal_size(columns_, rows_)) {
        return;
    }

    active_ = true;
    active_ui = this;

    // Interactive Ctrl+C/Ctrl+\\ should stop a command, not the shell UI.
    struct sigaction ignore_signal{};
    ignore_signal.sa_handler = SIG_IGN;
    sigemptyset(&ignore_signal.sa_mask);
    sigaction(SIGINT, &ignore_signal, nullptr);
    sigaction(SIGQUIT, &ignore_signal, nullptr);

    // The alternate screen starts empty and returns the previous terminal view on exit.
    cout << "\x1b[?1049h\x1b[2J\x1b[H" << flush;
    const auto& banner = banner_lines();
    const size_t visible_width = min(
        banner.front().size(), static_cast<size_t>(columns_ - 1));
    for (size_t visible = 3; visible < visible_width + 3; visible += 3) {
        for (int row = 0; row < banner_height; ++row) {
            cout << "\x1b[" << row + 1 << ";1H\x1b[2K\x1b[1;36m"
                 << banner[row].substr(0, min(visible, visible_width))
                 << "\x1b[0m";
        }
        cout << flush;
        this_thread::sleep_for(chrono::milliseconds(25));
    }

    cout << "\x1b[" << command_start_row << ';' << rows_ << 'r'
         << "\x1b[" << command_start_row << ";1H" << flush;
}

ShellUi::~ShellUi() {
    if (!active_) {
        return;
    }
    active_ui = nullptr;
    cout << "\x1b[r\x1b[?1049l" << flush;
}

void ShellUi::refresh(bool force) {
    if (!active_) {
        return;
    }

    int columns = 0;
    int rows = 0;
    if (!terminal_size(columns, rows)) {
        return;
    }

    const bool resized = columns != columns_ || rows != rows_;
    if (resized) {
        columns_ = columns;
        rows_ = rows;
    }
    if (resized || force) {
        cout << "\x1b" "7\x1b[" << command_start_row << ';' << rows_ << "r\x1b" "8" << flush;
    }

    const time_t now = time(nullptr);
    if (resized || force || static_cast<long>(now) != last_clock_second_) {
        last_clock_second_ = static_cast<long>(now);
        draw_header(columns_, now, resized || force);
    }
}

void refresh_shell_header() {
    if (active_ui != nullptr) {
        active_ui->refresh();
    }
}

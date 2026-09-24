#include "myshell/shell_ui.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <ctime>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

using namespace std;

namespace {

constexpr string_view title = "YOSF'S SHELL";
constexpr int banner_height = 5;
using Glyph = array<string_view, banner_height>;
using Banner = array<string, banner_height>;

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

Banner make_banner(string_view words) {
    Banner lines;
    for (size_t letter = 0; letter < words.size(); ++letter) {
        const Glyph glyph = glyph_for(words[letter]);
        for (int row = 0; row < banner_height; ++row) {
            if (letter > 0) {
                lines[row] += ' ';
            }
            for (char pixel : glyph[row]) {
                lines[row] += pixel == '#' ? "##" : "  ";
            }
        }
    }
    return lines;
}

bool terminal_size(int& columns, int& rows) {
    winsize size{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &size) != 0
        || size.ws_col == 0 || size.ws_row < 4) {
        return false;
    }
    columns = size.ws_col;
    rows = size.ws_row;
    return true;
}

string current_datetime() {
    const time_t now = time(nullptr);
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

void animate_banner(const Banner& banner, int first_row, int columns) {
    const size_t visible_width = min(
        banner.front().size(), static_cast<size_t>(columns - 1));

    for (size_t visible = 3; visible < visible_width + 3; visible += 3) {
        for (int row = 0; row < banner_height; ++row) {
            cout << "\x1b[" << first_row + row << ";1H\x1b[2K\x1b[1;36m"
                 << banner[row].substr(0, min(visible, visible_width))
                 << "\x1b[0m";
        }
        cout << flush;
        this_thread::sleep_for(chrono::milliseconds(18));
    }
}

void draw_launch_screen(int columns, int rows) {
    int date_row = 2;
    const Banner full = make_banner(title);

    if (static_cast<int>(full.front().size()) < columns && rows >= 8) {
        animate_banner(full, 1, columns);
        date_row = 6;
    } else {
        const Banner first = make_banner("YOSF'S");
        const Banner second = make_banner("SHELL");
        const size_t widest_word = max(first.front().size(), second.front().size());

        if (static_cast<int>(widest_word) < columns && rows >= 13) {
            animate_banner(first, 1, columns);
            animate_banner(second, 6, columns);
            date_row = 11;
        } else {
            const string compact = "## YOSF'S SHELL ##";
            cout << "\x1b[1;1H\x1b[1;36m"
                 << compact.substr(0, static_cast<size_t>(columns - 1))
                 << "\x1b[0m" << flush;
        }
    }

    const string datetime = current_datetime();
    const string visible = datetime.substr(0, static_cast<size_t>(columns - 1));
    cout << "\x1b[" << date_row << ';' << columns - static_cast<int>(visible.size())
         << "H\x1b[2;37m" << visible << "\x1b[0m";
    cout << "\x1b[" << date_row + 1 << ";1H\x1b[90m"
         << string(static_cast<size_t>(columns - 1), '-') << "\x1b[0m";
    cout << "\x1b[" << date_row + 2 << ";1H" << flush;
}

} // namespace

void show_launch_screen() {
    int columns = 0;
    int rows = 0;
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)
        || !terminal_size(columns, rows)) {
        return;
    }

    // Interactive Ctrl+C/Ctrl+\\ should stop a command, not the shell.
    struct sigaction ignore_signal{};
    ignore_signal.sa_handler = SIG_IGN;
    sigemptyset(&ignore_signal.sa_mask);
    struct sigaction previous_interrupt {};
    if (sigaction(SIGINT, &ignore_signal, &previous_interrupt) != 0) {
        cerr << "myshell: cannot configure SIGINT handling: "
             << strerror(errno) << '\n';
    } else if (sigaction(SIGQUIT, &ignore_signal, nullptr) != 0) {
        const int signal_error = errno;
        sigaction(SIGINT, &previous_interrupt, nullptr);
        cerr << "myshell: cannot configure SIGQUIT handling: "
             << strerror(signal_error) << '\n';
    }

    // Use the regular screen so the banner scrolls like command output.
    cout << "\x1b[r\x1b[2J\x1b[H" << flush;
    draw_launch_screen(columns, rows);
}

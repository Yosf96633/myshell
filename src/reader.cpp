#include "myshell/reader.hpp"
#include "myshell/history.hpp"

#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

using namespace std;

namespace {

bool read_was_interrupted = false;

class RawTerminal {
public:
    RawTerminal() {
        if (tcgetattr(STDIN_FILENO, &original_) != 0) {
            cerr << "myshell: cannot read terminal settings: "
                 << strerror(errno) << '\n';
            return;
        }

        termios raw = original_;
        raw.c_lflag &= static_cast<tcflag_t>(~(ICANON | ECHO | ISIG));
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;
        enabled_ = tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0;
        if (!enabled_) {
            cerr << "myshell: cannot enable raw terminal mode: "
                 << strerror(errno) << '\n';
        }
    }

    ~RawTerminal() {
        if (enabled_ && tcsetattr(STDIN_FILENO, TCSANOW, &original_) != 0) {
            cerr << "myshell: cannot restore terminal settings: "
                 << strerror(errno) << '\n';
        }
    }

    bool enabled() const {
        return enabled_;
    }

private:
    termios original_{};
    bool enabled_ = false;
};

bool read_byte(char& character) {
    while (true) {
        const ssize_t bytes_read = read(STDIN_FILENO, &character, 1);
        if (bytes_read == 1) {
            return true;
        }
        if (bytes_read == 0) {
            return false;
        }
        if (errno != EINTR) {
            cerr << "myshell: read: " << strerror(errno) << '\n';
            return false;
        }
    }
}

void redraw_line(const string& prompt, const string& line, size_t cursor) {
    cout << '\r' << prompt << line << "\x1b[K";

    const size_t characters_to_move_left = line.size() - cursor;
    if (characters_to_move_left > 0) {
        cout << "\x1b[" << characters_to_move_left << 'D';
    }
    cout << flush;
}

} // namespace

optional<string> read_line(const string& prompt) {
    read_was_interrupted = false;
    cout << prompt << flush;   // print prompt, force it to show now

    if (!isatty(STDIN_FILENO)) {
        string line;
        if (!getline(cin, line)) {
            if (cin.bad()) {
                cerr << "myshell: failed to read input\n";
            }
            return nullopt;
        }
        return line;
    }

    RawTerminal terminal;
    if (!terminal.enabled()) {
        string line;
        if (!getline(cin, line)) {
            if (cin.bad()) {
                cerr << "myshell: failed to read input\n";
            }
            return nullopt;
        }
        return line;
    }

    string line;
    string draft;
    size_t cursor = 0;
    size_t history_index = command_history.size();

    while (true) {
        char character;
        if (!read_byte(character)) {
            return nullopt;
        }

        if (character == '\r' || character == '\n') {
            cout << '\n';
            return line;
        }

        if (character == 4) { // Ctrl+D
            if (line.empty()) {
                return nullopt;
            }
            if (cursor < line.size()) {
                line.erase(cursor, 1);
                redraw_line(prompt, line, cursor);
            }
            continue;
        }

        if (character == 3) { // Ctrl+C
            cout << "^C\n";
            read_was_interrupted = true;
            return string{};
        }

        if (character == 127 || character == '\b') {
            if (cursor > 0) {
                line.erase(cursor - 1, 1);
                --cursor;
                redraw_line(prompt, line, cursor);
            }
            continue;
        }

        if (character == '\x1b') {
            char bracket;
            char key;
            if (!read_byte(bracket)
                || (bracket != '[' && bracket != 'O')
                || !read_byte(key)) {
                continue;
            }

            if (key == 'A') { // Up
                if (!command_history.empty() && history_index > 0) {
                    if (history_index == command_history.size()) {
                        draft = line;
                    }
                    --history_index;
                    line = command_history[history_index];
                    cursor = line.size();
                    redraw_line(prompt, line, cursor);
                }
            } else if (key == 'B') { // Down
                if (history_index < command_history.size()) {
                    ++history_index;
                    line = history_index == command_history.size()
                        ? draft
                        : command_history[history_index];
                    cursor = line.size();
                    redraw_line(prompt, line, cursor);
                }
            } else if (key == 'C') { // Right
                if (cursor < line.size()) {
                    ++cursor;
                    cout << "\x1b[C" << flush;
                }
            } else if (key == 'D') { // Left
                if (cursor > 0) {
                    --cursor;
                    cout << "\x1b[D" << flush;
                }
            }
            continue;
        }

        const unsigned char byte = static_cast<unsigned char>(character);
        if (byte >= 32 && byte != 127) {
            line.insert(cursor, 1, character);
            ++cursor;
            redraw_line(prompt, line, cursor);
        }
    }
}

bool last_read_was_interrupted() {
    return read_was_interrupted;
}

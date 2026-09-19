#include "myshell/reader.hpp"
#include <iostream>

using namespace std;

optional<string> read_line(const string& prompt) {
    cout << prompt << flush;   // print prompt, force it to show now

    string line;
    if (!getline(cin, line)) {
        // getline fails when there's no more input (Ctrl+D was pressed)
        return nullopt;   // the "empty box" — signals "no input"
    }

    return line;   // wraps `line` in the optional "box" automatically
}
#include "myshell/reader.hpp"
#include "myshell/tokenizer.hpp"
#include "myshell/prompt.hpp"
#include <iostream>
using namespace std;
int main() {
    while (true) {   // repeat forever until we break

        auto line = read_line(build_prompt());

        if (!line) {           // Ctrl+D was pressed
            cout << "\n";
            break;              // exit the loop, end the program
        }

        if (line->empty()) {   // user just pressed Enter
            continue;           // skip to next loop iteration, re-prompt
        }

        vector<string> tokens = tokenize(*line);

        cout << "parsed " << tokens.size() << " token(s):";
        for (const auto& t : tokens) {
            cout << " [" << t << "]";
        }
        cout << "\n";
    }

    return 0;
}
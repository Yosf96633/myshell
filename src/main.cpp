#include "myshell/reader.hpp"
#include "myshell/tokenizer.hpp"
#include <iostream>

int main() {
    while (true) {   // repeat forever until we break

        auto line = read_line("myshell> ");

        if (!line) {           // Ctrl+D was pressed
            std::cout << "\n";
            break;              // exit the loop, end the program
        }

        if (line->empty()) {   // user just pressed Enter
            continue;           // skip to next loop iteration, re-prompt
        }

        std::vector<std::string> tokens = tokenize(*line);

        std::cout << "parsed " << tokens.size() << " token(s):";
        for (const auto& t : tokens) {
            std::cout << " [" << t << "]";
        }
        std::cout << "\n";
    }

    return 0;
}
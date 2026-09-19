#include "myshell/tokenizer.hpp"
#include <iostream>
using namespace std;

int main() {
   string line = "ls -la /tmp";   // hardcoded for now, no real input yet

    vector<string> tokens = tokenize(line);

    cout << "parsed " << tokens.size() << " token(s):";
    for (const auto& t : tokens) {
        cout << " [" << t << "]";
    }
    cout << "\n";

    return 0;
}
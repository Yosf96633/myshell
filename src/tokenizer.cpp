#include "myshell/tokenizer.hpp"
#include <iostream>
#include <sstream>

using namespace std;

vector<string> tokenize (const string& line){

    vector<string> tokens;

    istringstream stream(line);   // lets us pull words out of `line` one at a time
    string word;
    while (stream >> word) {           // each loop: grab the next word
        tokens.push_back(word);        // add it to our list
    }

    return tokens;
}


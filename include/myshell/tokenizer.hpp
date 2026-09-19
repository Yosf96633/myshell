#pragma once   // prevents this file from being read twice by accident

#include <string>
#include <vector>
using namespace std;

// This line just says: "somewhere, there's a function called tokenize
// that takes a string and gives back a list of strings."
vector<string> tokenize(const string& line);
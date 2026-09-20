#pragma once
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

using namespace std;

// The type every builtin function must match:
// takes the argument list (NOT including "cd" itself), returns exit status.
using BuiltinFunc = function<int(const vector<string>&)>;

// The single lookup table: command name -> function
extern unordered_map<string, BuiltinFunc> builtins;

// Returns true if `name` is a registered builtin.
bool is_builtin(const string& name);

// Runs the builtin named `name`, passing `args`. Returns its exit status.
int run_builtin(const string& name, const vector<string>& args);
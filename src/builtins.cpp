#include "myshell/builtins.hpp"
#include <unistd.h>   // chdir
#include <iostream>

using namespace std;

// The actual cd implementation
int builtin_cd(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "cd: missing argument\n";
        return 1;
    }

    if (chdir(args[0].c_str()) != 0) {
        std::cerr << "cd: no such directory: " << args[0] << "\n";
        return 1;
    }

    return 0;
}

// The map itself, populated with every builtin we support
std::unordered_map<std::string, BuiltinFunc> builtins = {
    {"cd", builtin_cd}
};

bool is_builtin(const std::string& name) {
    return builtins.count(name) > 0;   // count() = 1 if key exists, 0 if not
}

int run_builtin(const std::string& name, const std::vector<std::string>& args) {
    return builtins[name](args);
}
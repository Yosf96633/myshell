#include "myshell/builtins.hpp"
#include <unistd.h>   // chdir
#include <iostream>
#include <cstdlib>      // getenv
#include <climits>      // PATH_MAX
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

// The actual pwd implementation
int builtin_pwd(const std::vector<std::string>&) {
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {
        cerr << "pwd: error getting current directory\n";
        return 1;
    }
    cout << cwd << endl;
    return 0;
}

// Print all arguments separated by spaces
int builtin_echo(const std::vector<std::string>& args) {
    for (size_t i = 0; i < args.size(); ++i) {
        if (i > 0) {
            cout << ' ';
        }
        cout << args[i];
    }
    cout << '\n';
    return 0;
}

// The actual exit implementation
int builtin_exit(const std::vector<std::string>&) {
    return EXIT_SIGNAL;
}

// The map itself, populated with every builtin we support
std::unordered_map<std::string, BuiltinFunc> builtins = {
    {"cd", builtin_cd},
    {"pwd", builtin_pwd},
    {"echo", builtin_echo},
    {"exit", builtin_exit},
};

bool is_builtin(const std::string& name) {
    return builtins.count(name) > 0;   // count() = 1 if key exists, 0 if not
}

int run_builtin(const std::string& name, const std::vector<std::string>& args) {
    return builtins[name](args);
}

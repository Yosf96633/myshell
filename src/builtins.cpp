#include "myshell/builtins.hpp"
#include "myshell/path_search.hpp"
#include <unistd.h>   // chdir
#include <algorithm>
#include <iostream>
#include <cstdlib>      // getenv
#include <climits>      // PATH_MAX
#include <iomanip>
using namespace std;

namespace {

vector<string> sorted_cache_names() {
    vector<string> names;
    names.reserve(path_cache.size());
    for (const auto& [name, entry] : path_cache) {
        (void)entry;
        names.push_back(name);
    }
    sort(names.begin(), names.end());
    return names;
}

void print_hash_table(bool reusable) {
    const auto names = sorted_cache_names();

    if (reusable) {
        for (const auto& name : names) {
            cout << "builtin hash -p " << path_cache.at(name).path
                 << ' ' << name << '\n';
        }
        return;
    }

    if (names.empty()) {
        cerr << "hash: hash table empty\n";
        return;
    }

    cout << "hits\tcommand\n";
    for (const auto& name : names) {
        const auto& entry = path_cache.at(name);
        cout << setw(4) << entry.hit_count << '\t' << entry.path << '\n';
    }
}

int hash_name_not_found(const string& name) {
    cerr << "hash: " << name << ": not found\n";
    return 1;
}

int builtin_hash(const vector<string>& args) {
    bool delete_names = false;
    bool reusable = false;
    bool reset = false;
    bool show_paths = false;
    bool set_path = false;
    string supplied_path;
    vector<string> names;

    bool parsing_options = true;
    for (size_t i = 0; i < args.size(); ++i) {
        const string& arg = args[i];

        if (parsing_options && arg == "--") {
            parsing_options = false;
            continue;
        }

        if (!parsing_options || arg.size() < 2 || arg[0] != '-' || arg == "-") {
            names.insert(names.end(), args.begin() + static_cast<ptrdiff_t>(i), args.end());
            break;
        }

        for (size_t option_index = 1; option_index < arg.size(); ++option_index) {
            switch (arg[option_index]) {
            case 'd':
                delete_names = true;
                break;
            case 'l':
                reusable = true;
                break;
            case 'r':
                reset = true;
                break;
            case 't':
                show_paths = true;
                break;
            case 'p':
                set_path = true;
                if (option_index + 1 < arg.size()) {
                    supplied_path = arg.substr(option_index + 1);
                    option_index = arg.size();
                } else if (i + 1 < args.size()) {
                    supplied_path = args[++i];
                } else {
                    cerr << "hash: -p: option requires an argument\n";
                    return 1;
                }
                break;
            default:
                cerr << "hash: -" << arg[option_index] << ": invalid option\n"
                     << "hash: usage: hash [-lr] [-p pathname] [-dt] [name ...]\n";
                return 2;
            }
        }
    }

    if (reset) {
        path_cache.clear();
    }

    // Bash gives -t priority over -p, and -p priority over -d.
    if (show_paths) {
        if (names.empty()) {
            cerr << "hash: -t: option requires an argument\n";
            return 1;
        }

        int status = 0;
        for (const auto& name : names) {
            auto cached = path_cache.find(name);
            if (cached == path_cache.end()) {
                status = hash_name_not_found(name);
                continue;
            }

            ++cached->second.hit_count;
            if (names.size() > 1) {
                cout << name << '\t';
            }
            cout << cached->second.path << '\n';
        }
        return status;
    }

    if (set_path && !names.empty()) {
        for (const auto& name : names) {
            path_cache[name] = {supplied_path, 0};
        }
        return 0;
    }

    if (delete_names) {
        if (names.empty()) {
            cerr << "hash: -d: option requires an argument\n";
            return 1;
        }

        int status = 0;
        for (const auto& name : names) {
            if (path_cache.erase(name) == 0) {
                status = hash_name_not_found(name);
            }
        }
        return status;
    }

    if (!names.empty()) {
        int status = 0;
        for (const auto& name : names) {
            // Like Bash, a builtin name needs no PATH entry and is skipped.
            if (is_builtin(name)) {
                continue;
            }
            if (!cache_command(name)) {
                status = hash_name_not_found(name);
            }
        }
        return status;
    }

    if (!reset) {
        print_hash_table(reusable);
    }
    return 0;
}

} // namespace

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
    {"hash", builtin_hash},
};

bool is_builtin(const std::string& name) {
    return builtins.count(name) > 0;   // count() = 1 if key exists, 0 if not
}

int run_builtin(const std::string& name, const std::vector<std::string>& args) {
    auto builtin = builtins.find(name);
    if (builtin == builtins.end()) {
        return 1;
    }
    return builtin->second(args);
}

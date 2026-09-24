#include "myshell/builtins.hpp"
#include "myshell/exec_builtin.hpp"
#include "myshell/functions.hpp"
#include "myshell/history.hpp"
#include "myshell/path_search.hpp"
#include "myshell/tokenizer.hpp"
#include <unistd.h>   // chdir
#include <fnmatch.h>  // fnmatch
#include <algorithm>
#include <iostream>
#include <cstdlib>      // getenv
#include <climits>      // PATH_MAX
#include <iomanip>
#include <iterator>
#include <unordered_set>
using namespace std;

namespace {

unordered_map<string, string> aliases;

string shell_quote(const string& value) {
    string quoted = "'";
    for (char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted += character;
        }
    }
    quoted += '\'';
    return quoted;
}

vector<string> sorted_alias_names() {
    vector<string> names;
    names.reserve(aliases.size());
    for (const auto& [name, value] : aliases) {
        (void)value;
        names.push_back(name);
    }
    sort(names.begin(), names.end());
    return names;
}

void print_alias(const string& name) {
    cout << "alias " << name << '=' << shell_quote(aliases.at(name)) << '\n';
}

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
            // Builtins and functions need no PATH entry and are skipped.
            if (is_builtin(name) || is_shell_function(name)) {
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

int builtin_alias(const vector<string>& args) {
    bool print_all = args.empty();
    size_t argument_index = 0;

    while (argument_index < args.size()) {
        const string& arg = args[argument_index];
        if (arg == "--") {
            ++argument_index;
            break;
        }
        if (arg.size() < 2 || arg[0] != '-' || arg == "-") {
            break;
        }

        for (size_t i = 1; i < arg.size(); ++i) {
            if (arg[i] != 'p') {
                cerr << "alias: -" << arg[i] << ": invalid option\n"
                     << "alias: usage: alias [-p] [name[=value] ... ]\n";
                return 2;
            }
            print_all = true;
        }
        ++argument_index;
    }

    if (print_all) {
        for (const auto& name : sorted_alias_names()) {
            print_alias(name);
        }
    }

    int status = 0;
    for (; argument_index < args.size(); ++argument_index) {
        const string& arg = args[argument_index];
        const size_t equals = arg.find('=');
        if (equals != string::npos) {
            const string name = arg.substr(0, equals);
            const string value = arg.substr(equals + 1);
            const string invalid_characters = "/$`=|&;()<> \t\r\n'\"";

            if (name.empty() || name.find_first_of(invalid_characters) != string::npos) {
                cerr << "alias: `" << name << "': invalid alias name\n";
                status = 1;
                continue;
            }
            aliases[name] = value;
            continue;
        }

        auto alias = aliases.find(arg);
        if (alias == aliases.end()) {
            cerr << "alias: " << arg << ": not found\n";
            status = 1;
        } else {
            print_alias(arg);
        }
    }

    return status;
}

int builtin_history(const vector<string>& args) {
    if (!args.empty()) {
        cerr << "history: usage: history\n";
        return 2;
    }

    for (size_t i = 0; i < command_history.size(); ++i) {
        cout << setw(5) << i + 1 << "  " << command_history[i] << '\n';
    }
    return 0;
}

int builtin_exec(const vector<string>& args) {
    ParsedCommand command;
    command.name = "exec";
    command.arguments = args;
    command.present = true;
    return run_exec_builtin(command);
}

int builtin_declare(const vector<string>& args) {
    bool show_definitions = false;
    bool show_names = false;
    size_t argument_index = 0;

    while (argument_index < args.size()) {
        const string& arg = args[argument_index];
        if (arg == "--") {
            ++argument_index;
            break;
        }
        if (arg.size() < 2 || arg[0] != '-' || arg == "-") {
            break;
        }

        for (size_t i = 1; i < arg.size(); ++i) {
            if (arg[i] == 'f') {
                show_definitions = true;
            } else if (arg[i] == 'F') {
                show_names = true;
            } else {
                cerr << "declare: -" << arg[i] << ": invalid option\n"
                     << "declare: usage: declare -f|-F [name ...]\n";
                return 2;
            }
        }
        ++argument_index;
    }

    if (!show_definitions && !show_names) {
        cerr << "declare: this shell currently supports only -f and -F\n"
             << "declare: usage: declare -f|-F [name ...]\n";
        return 2;
    }

    vector<string> names(args.begin() + static_cast<ptrdiff_t>(argument_index), args.end());
    const bool requested_specific_names = !names.empty();
    if (names.empty()) {
        names = sorted_function_names();
    }

    int status = 0;
    for (const auto& name : names) {
        const auto function = shell_functions.find(name);
        if (function == shell_functions.end()) {
            status = 1;
            continue;
        }

        if (show_names) {
            if (!requested_specific_names) {
                cout << "declare -f ";
            }
            cout << name << '\n';
        } else {
            print_function_definition(function->second);
        }
    }
    return status;
}

int builtin_unset(const vector<string>& args) {
    bool remove_functions = false;
    size_t argument_index = 0;

    while (argument_index < args.size()) {
        const string& arg = args[argument_index];
        if (arg == "--") {
            ++argument_index;
            break;
        }
        if (arg.size() < 2 || arg[0] != '-' || arg == "-") {
            break;
        }

        for (size_t i = 1; i < arg.size(); ++i) {
            if (arg[i] == 'f') {
                remove_functions = true;
            } else {
                cerr << "unset: -" << arg[i] << ": invalid option\n"
                     << "unset: usage: unset -f [name ...]\n";
                return 2;
            }
        }
        ++argument_index;
    }

    if (!remove_functions) {
        cerr << "unset: this shell currently supports only -f\n"
             << "unset: usage: unset -f [name ...]\n";
        return 2;
    }

    for (; argument_index < args.size(); ++argument_index) {
        remove_shell_function(args[argument_index]);
    }
    return 0;
}

void print_type_alias(const string& name) {
    cout << name << " is aliased to `" << aliases.at(name) << "'\n";
}

int builtin_type(const vector<string>& args) {
    bool show_all = false;
    bool suppress_functions = false;
    bool path_only = false;
    bool force_path = false;
    bool type_word = false;
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
            case 'a':
                show_all = true;
                break;
            case 'f':
                suppress_functions = true;
                break;
            case 'p':
                path_only = true;
                break;
            case 'P':
                force_path = true;
                break;
            case 't':
                type_word = true;
                break;
            default:
                cerr << "type: -" << arg[option_index] << ": invalid option\n"
                     << "type: usage: type [-afptP] name [name ...]\n";
                return 2;
            }
        }
    }

    int status = 0;
    for (const auto& name : names) {
        const bool is_alias = aliases.count(name) > 0;
        const bool is_function = !suppress_functions && is_shell_function(name);
        const bool is_shell_builtin = is_builtin(name);
        const vector<string> paths = show_all
            ? find_all_command_paths(name)
            : vector<string>{};
        const auto first_path = show_all ? optional<string>{} : find_command_path(name);
        const bool has_file = show_all ? !paths.empty() : first_path.has_value();

        if (force_path) {
            if (!has_file) {
                status = 1;
                continue;
            }
            if (show_all) {
                for (const auto& path : paths) {
                    cout << path << '\n';
                }
            } else {
                cout << *first_path << '\n';
            }
            continue;
        }

        if (!is_alias && !is_function && !is_shell_builtin && !has_file) {
            if (!type_word && !path_only) {
                cerr << "type: " << name << ": not found\n";
            }
            status = 1;
            continue;
        }

        if (type_word) {
            if (show_all) {
                if (is_alias) cout << "alias\n";
                if (is_function) cout << "function\n";
                if (is_shell_builtin) cout << "builtin\n";
                for (size_t i = 0; i < paths.size(); ++i) cout << "file\n";
            } else if (is_alias) {
                cout << "alias\n";
            } else if (is_function) {
                cout << "function\n";
            } else if (is_shell_builtin) {
                cout << "builtin\n";
            } else {
                cout << "file\n";
            }
            continue;
        }

        if (path_only) {
            if (show_all) {
                for (const auto& path : paths) {
                    cout << path << '\n';
                }
            } else if (!is_alias && !is_function && !is_shell_builtin && first_path) {
                cout << *first_path << '\n';
            }
            continue;
        }

        if (show_all) {
            if (is_alias) {
                print_type_alias(name);
            }
            if (is_function) {
                cout << name << " is a function\n";
                print_function_definition(shell_functions.at(name));
            }
            if (is_shell_builtin) {
                cout << name << " is a shell builtin\n";
            }
            for (const auto& path : paths) {
                cout << name << " is " << path << '\n';
            }
        } else if (is_alias) {
            print_type_alias(name);
        } else if (is_function) {
            cout << name << " is a function\n";
            print_function_definition(shell_functions.at(name));
        } else if (is_shell_builtin) {
            cout << name << " is a shell builtin\n";
        } else {
            cout << name << " is " << *first_path << '\n';
        }
    }

    return status;
}

struct BuiltinHelp {
    string name;
    string synopsis;
    string summary;
    string details;
};

const vector<BuiltinHelp>& help_topics() {
    static const vector<BuiltinHelp> topics = {
        {"alias", "alias [-p] [name[=value] ...]", "Define or display aliases.",
         "Without arguments or with -p, display aliases in reusable form. "
         "An assignment defines an alias; a name by itself displays it."},
        {"cd", "cd directory", "Change the current working directory.",
         "Change the shell's working directory to DIRECTORY."},
        {"declare", "declare -f|-F [name ...]", "Display shell functions.",
         "Use -f to display definitions or -F to display function names."},
        {"echo", "echo [argument ...]", "Write arguments to standard output.",
         "Display the arguments separated by one space, followed by a newline."},
        {"exit", "exit", "Exit the shell.",
         "Stop the interactive shell."},
        {"exec", "exec [command [argument ...]] [redirection ...]",
         "Replace the shell or modify its file descriptors.",
         "Without a command, apply redirections to the running shell. With a "
         "command, replace the shell process with that executable."},
        {"hash", "hash [-lr] [-p pathname] [-dt] [name ...]",
         "Remember or display program locations.",
         "Options: -d deletes names, -l prints reusable commands, -p assigns a "
         "pathname, -r clears the table, and -t prints cached paths."},
        {"history", "history", "Display the command history.",
         "Display this session's command history with line numbers."},
        {"help", "help [-dms] [pattern ...]", "Display information about builtin commands.",
         "Options: -d prints a short description, -m uses manpage-style output, "
         "and -s prints only the synopsis."},
        {"pwd", "pwd", "Print the current working directory.",
         "Write the absolute pathname of the current working directory."},
        {"type", "type [-afptP] name [name ...]", "Display information about command type.",
         "Options: -a shows all matches, -f suppresses function lookup, -p prints "
         "a command path, -P forces PATH lookup, and -t prints the type word."},
        {"unset", "unset -f [name ...]", "Remove shell functions.",
         "Remove each named function from the current shell."},
    };
    return topics;
}

bool help_topic_matches(const string& pattern, const string& topic_name) {
    const bool has_glob = pattern.find_first_of("*?[") != string::npos;
    return fnmatch(pattern.c_str(), topic_name.c_str(), 0) == 0
        || (!has_glob && topic_name.rfind(pattern, 0) == 0);
}

vector<const BuiltinHelp*> matching_help_topics(const vector<string>& patterns) {
    vector<const BuiltinHelp*> matches;
    for (const auto& topic : help_topics()) {
        if (patterns.empty() || any_of(patterns.begin(), patterns.end(), [&](const string& pattern) {
                return help_topic_matches(pattern, topic.name);
            })) {
            matches.push_back(&topic);
        }
    }
    return matches;
}

int builtin_help(const vector<string>& args) {
    bool description_only = false;
    bool manpage = false;
    bool synopsis_only = false;
    vector<string> patterns;

    bool parsing_options = true;
    for (size_t i = 0; i < args.size(); ++i) {
        const string& arg = args[i];
        if (parsing_options && arg == "--") {
            parsing_options = false;
            continue;
        }
        if (!parsing_options || arg.size() < 2 || arg[0] != '-' || arg == "-") {
            patterns.insert(patterns.end(), args.begin() + static_cast<ptrdiff_t>(i), args.end());
            break;
        }

        for (size_t option_index = 1; option_index < arg.size(); ++option_index) {
            switch (arg[option_index]) {
            case 'd': description_only = true; break;
            case 'm': manpage = true; break;
            case 's': synopsis_only = true; break;
            default:
                cerr << "help: -" << arg[option_index] << ": invalid option\n"
                     << "help: usage: help [-dms] [pattern ...]\n";
                return 2;
            }
        }
    }

    int status = 0;
    for (const auto& pattern : patterns) {
        const bool found = any_of(help_topics().begin(), help_topics().end(),
            [&](const BuiltinHelp& topic) {
                return help_topic_matches(pattern, topic.name);
            });
        if (!found) {
            cerr << "help: no help topics match `" << pattern
                 << "'. Try `help help'.\n";
            status = 1;
        }
    }

    const auto matches = matching_help_topics(patterns);
    if (matches.empty()) {
        return status;
    }

    if (patterns.empty() && !description_only && !manpage && !synopsis_only) {
        cout << "myShell builtins (use `help name' for details):\n";
        for (const auto* topic : matches) {
            cout << "  " << topic->synopsis << '\n';
        }
        return status;
    }

    for (size_t i = 0; i < matches.size(); ++i) {
        const auto& topic = *matches[i];
        if (i > 0 && manpage) {
            cout << '\n';
        }

        if (manpage) {
            cout << "NAME\n    " << topic.name << " - " << topic.summary
                 << "\n\nSYNOPSIS\n    " << topic.synopsis
                 << "\n\nDESCRIPTION\n    " << topic.details << '\n';
        } else if (synopsis_only) {
            cout << topic.name << ": " << topic.synopsis << '\n';
        } else if (description_only) {
            cout << topic.name << " - " << topic.summary << '\n';
        } else {
            cout << topic.name << ": " << topic.synopsis << '\n'
                 << "    " << topic.summary << '\n'
                 << "    " << topic.details << '\n';
        }
    }
    return status;
}

} // namespace

bool expand_aliases(ParsedCommand& command, string& error) {
    unordered_set<string> expanded_names;

    while (!command.empty()) {
        auto alias = aliases.find(command.name);
        if (alias == aliases.end() || expanded_names.count(alias->first) > 0) {
            break;
        }

        expanded_names.insert(alias->first);
        ParseResult replacement = parse_command(alias->second);
        if (replacement.has_error()) {
            error = replacement.error;
            return false;
        }

        vector<string> trailing_arguments = move(command.arguments);
        vector<Redirection> trailing_redirections = move(command.redirections);
        if (!replacement.command) {
            if (trailing_arguments.empty()) {
                command = {};
                command.redirections = move(trailing_redirections);
                command.present = !command.redirections.empty();
                break;
            }
            command.name = move(trailing_arguments.front());
            command.arguments.assign(
                make_move_iterator(trailing_arguments.begin() + 1),
                make_move_iterator(trailing_arguments.end()));
            command.redirections = move(trailing_redirections);
            continue;
        }

        command = move(*replacement.command);
        command.arguments.insert(
            command.arguments.end(),
            make_move_iterator(trailing_arguments.begin()),
            make_move_iterator(trailing_arguments.end()));
        command.redirections.insert(
            command.redirections.end(),
            make_move_iterator(trailing_redirections.begin()),
            make_move_iterator(trailing_redirections.end()));
    }
    return true;
}

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
    {"declare", builtin_declare},
    {"pwd", builtin_pwd},
    {"echo", builtin_echo},
    {"exit", builtin_exit},
    {"exec", builtin_exec},
    {"hash", builtin_hash},
    {"history", builtin_history},
    {"type", builtin_type},
    {"unset", builtin_unset},
    {"alias", builtin_alias},
    {"help", builtin_help},
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

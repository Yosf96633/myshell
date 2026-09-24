#include "myshell/path_search.hpp"
#include <cerrno>
#include <cstdlib>     // getenv
#include <sys/stat.h>  // stat, S_ISREG
#include <unistd.h>    // access

unordered_map<string, CommandCacheEntry> path_cache;

namespace {

bool is_executable_file(const string& path) {
    struct stat file_info {};
    return stat(path.c_str(), &file_info) == 0
        && S_ISREG(file_info.st_mode)
        && access(path.c_str(), X_OK) == 0;
}

optional<string> search_path(const string& command) {
    // Commands containing a slash are paths, so PATH is not involved and the
    // result should not be entered in the command hash table.
    if (command.find('/') != string::npos) {
        if (is_executable_file(command)) {
            return command;
        }
        return nullopt;
    }

    const char* path_env = getenv("PATH");
    if (path_env == nullptr) {
        return nullopt;
    }

    const string path_list(path_env);
    size_t component_start = 0;
    while (true) {
        const size_t component_end = path_list.find(':', component_start);
        string dir = path_list.substr(component_start, component_end - component_start);

        // An empty PATH component means the current directory.
        if (dir.empty()) {
            dir = ".";
        }

        string full_path = dir + "/" + command;
        if (is_executable_file(full_path)) {
            return full_path;
        }

        if (component_end == string::npos) {
            break;
        }
        component_start = component_end + 1;
    }

    return nullopt;
}

CommandResolution search_path_for_execution(const string& command) {
    if (command.find('/') != string::npos) {
        struct stat file_info {};
        if (stat(command.c_str(), &file_info) != 0) {
            return {nullopt, errno};
        }
        if (!S_ISREG(file_info.st_mode)) {
            return {nullopt, EACCES};
        }
        if (access(command.c_str(), X_OK) != 0) {
            return {nullopt, errno};
        }
        return {command, 0};
    }

    const char* path_env = getenv("PATH");
    if (path_env == nullptr) {
        return {nullopt, ENOENT};
    }

    int remembered_error = ENOENT;
    const string path_list(path_env);
    size_t component_start = 0;
    while (true) {
        const size_t component_end = path_list.find(':', component_start);
        string dir = path_list.substr(component_start, component_end - component_start);
        if (dir.empty()) {
            dir = ".";
        }

        const string candidate = dir + "/" + command;
        struct stat file_info {};
        if (stat(candidate.c_str(), &file_info) == 0) {
            if (S_ISREG(file_info.st_mode) && access(candidate.c_str(), X_OK) == 0) {
                return {candidate, 0};
            }
            remembered_error = EACCES;
        } else if (errno != ENOENT && errno != ENOTDIR) {
            remembered_error = errno;
        }

        if (component_end == string::npos) {
            break;
        }
        component_start = component_end + 1;
    }
    return {nullopt, remembered_error};
}

} // namespace

optional<string> resolve_command(const string& command) {
    // 1. Check the cache first
    auto cached = path_cache.find(command);
    if (cached != path_cache.end()) {
        ++cached->second.hit_count;
        return cached->second.path;
    }

    // 2. Not cached — search PATH and count this first command lookup as a hit.
    auto resolved = search_path(command);
    if (resolved && command.find('/') == string::npos) {
        path_cache[command] = {*resolved, 1};
    }
    return resolved;
}

CommandResolution resolve_command_for_execution(const string& command) {
    auto cached = path_cache.find(command);
    if (cached != path_cache.end()) {
        ++cached->second.hit_count;
        return {cached->second.path, 0};
    }

    CommandResolution resolution = search_path_for_execution(command);
    if (resolution.path && command.find('/') == string::npos) {
        path_cache[command] = {*resolution.path, 1};
    }
    return resolution;
}

optional<string> cache_command(const string& command) {
    auto resolved = search_path(command);
    if (resolved && command.find('/') == string::npos) {
        path_cache[command] = {*resolved, 0};
    }
    return resolved;
}

optional<string> find_command_path(const string& command) {
    auto cached = path_cache.find(command);
    if (cached != path_cache.end()) {
        return cached->second.path;
    }
    return search_path(command);
}

vector<string> find_all_command_paths(const string& command) {
    vector<string> matches;

    if (command.find('/') != string::npos) {
        if (is_executable_file(command)) {
            matches.push_back(command);
        }
        return matches;
    }

    const char* path_env = getenv("PATH");
    if (path_env == nullptr) {
        return matches;
    }

    const string path_list(path_env);
    size_t component_start = 0;
    while (true) {
        const size_t component_end = path_list.find(':', component_start);
        string dir = path_list.substr(component_start, component_end - component_start);
        if (dir.empty()) {
            dir = ".";
        }

        string full_path = dir + "/" + command;
        if (is_executable_file(full_path)) {
            matches.push_back(full_path);
        }

        if (component_end == string::npos) {
            break;
        }
        component_start = component_end + 1;
    }

    return matches;
}

#include "myshell/path_search.hpp"
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

optional<string> cache_command(const string& command) {
    auto resolved = search_path(command);
    if (resolved && command.find('/') == string::npos) {
        path_cache[command] = {*resolved, 0};
    }
    return resolved;
}

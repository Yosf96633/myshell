#pragma once
#include <string>
#include <optional>
#include <unordered_map>
#include <cstddef>
#include <vector>

using namespace std;

struct CommandCacheEntry {
    string path;
    size_t hit_count = 0;
};

// The cache: command name -> its resolved path and lookup count.
extern unordered_map<string, CommandCacheEntry> path_cache;

// Returns the full path to `command` if found (checking cache first, then $PATH),
// or an empty box if it doesn't exist anywhere in $PATH.
optional<string> resolve_command(const string& command);

// Search PATH and remember `command` with a fresh hit count of zero.
// This is used by the `hash name ...` builtin.
optional<string> cache_command(const string& command);

// Locate a command without changing its hit count or adding it to the cache.
// A cached path is preferred when one exists.
optional<string> find_command_path(const string& command);

// Return every executable with this name in PATH, in PATH order.
vector<string> find_all_command_paths(const string& command);

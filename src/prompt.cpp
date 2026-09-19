#include "myshell/prompt.hpp"
#include <unistd.h>     // getcwd, gethostname
#include <cstdlib>      // getenv
#include <climits>      // PATH_MAX

std::string build_prompt() {
    // 1. Get username from environment variable
    const char* user = getenv("USER");
    std::string username = user ? user : "user";

    // 2. Get hostname
    char hostname[256];
    gethostname(hostname, sizeof(hostname));

    // 3. Get current working directory
    char cwd[PATH_MAX];
    getcwd(cwd, sizeof(cwd));

    // ANSI color codes
    const std::string GREEN = "\033[32m";
    const std::string BLUE  = "\033[34m";
    const std::string RESET = "\033[0m";

    // Build: yosf@mint:~$  (green user@host, blue path)
    std::string prompt = GREEN + username + "@" + hostname + RESET
                        + ":" + BLUE + cwd + RESET + "$ ";

    return prompt;
}
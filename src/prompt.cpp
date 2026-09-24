#include "myshell/prompt.hpp"

#include <unistd.h>

#include <cstdlib>
#include <string>

using namespace std;

string build_prompt() {
    const char* user = getenv("USER");
    const string username = user ? user : "user";

    char hostname_buffer[256]{};
    string hostname = "unknown-host";
    if (gethostname(hostname_buffer, sizeof(hostname_buffer)) == 0) {
        hostname_buffer[sizeof(hostname_buffer) - 1] = '\0';
        hostname = hostname_buffer;
    }

    string cwd = "?";
    if (char* current_directory = getcwd(nullptr, 0)) {
        cwd = current_directory;
        free(current_directory);
    }

    const string green = "\033[32m";
    const string blue = "\033[34m";
    const string reset = "\033[0m";
    return green + username + "@" + hostname + reset
        + ":" + blue + cwd + reset + "$ ";
}

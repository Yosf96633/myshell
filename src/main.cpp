#include "myshell/reader.hpp"
#include "myshell/tokenizer.hpp"
#include "myshell/prompt.hpp"
#include "myshell/builtins.hpp"
#include "myshell/path_search.hpp"
#include <unistd.h>   // fork, execv
#include <sys/wait.h> // waitpid
#include <iostream>
using namespace std;
int main()
{
    while (true)
    { // repeat forever until we break

        auto line = read_line(build_prompt());

        if (!line)
        { // Ctrl+D was pressed
            cout << "\n";
            break; // exit the loop, end the program
        }

        if (line->empty())
        {             // user just pressed Enter
            continue; // skip to next loop iteration, re-prompt
        }

        vector<string> tokens = tokenize(*line);
        string command = tokens[0];
        vector<string> args(tokens.begin() + 1, tokens.end()); // everything except tokens[0]

        if (is_builtin(command))
        {
            int result = run_builtin(command, args);
            if (result == EXIT_SIGNAL)
            {
                break; // main() decides when/how to actually stop — cleanup would go here later
            }
        }
        else
        {
            auto resolved = resolve_command(command);

            if (!resolved)
            {
                cerr << command << ": command not found\n";
            }
            else
            {
                pid_t pid = fork();

                if (pid == -1)
                {
                    cerr << "fork failed\n";
                }
                else if (pid == 0)
                {
                    // --- child process ---

                    // Build the char* argv[] array execv requires
                    vector<char *> c_args;
                    c_args.push_back(const_cast<char *>(resolved->c_str())); // argv[0] — full resolved path
                    for (const auto &a : args)
                    {
                        c_args.push_back(const_cast<char *>(a.c_str()));
                    }
                    c_args.push_back(nullptr); // required terminator

                    execv(c_args[0], c_args.data()); // execv, not execvp — path already resolved by us

                    // Only reached if execv failed
                    cerr << command << ": exec failed\n";
                    exit(1);
                }
                else
                {
                    // --- parent process ---
                    int status;
                    waitpid(pid, &status, 0);
                }
            }
        }
    }

    return 0;
}
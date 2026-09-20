#include "myshell/reader.hpp"
#include "myshell/tokenizer.hpp"
#include "myshell/prompt.hpp"
#include "myshell/builtins.hpp"
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
            cout << "not a builtin (yet): " << command << "\n";
        }

        for(const auto& t : tokens){
            cout<<"[ " << t << " ]";
            
        }
        cout<<"\n";
    }

    return 0;
}
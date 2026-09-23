#include "myshell/reader.hpp"
#include "myshell/tokenizer.hpp"
#include "myshell/prompt.hpp"
#include "myshell/builtins.hpp"
#include "myshell/executor.hpp"
#include "myshell/functions.hpp"
#include "myshell/history.hpp"
#include <iostream>
#include <utility>
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

        add_history_entry(*line);

        string definition_error;
        const auto definition_result = register_function_definition(*line, definition_error);
        if (definition_result == FunctionDefinitionResult::syntax_error)
        {
            cerr << "syntax error: " << definition_error << '\n';
            continue;
        }
        if (definition_result == FunctionDefinitionResult::registered)
        {
            continue;
        }

        vector<string> tokens = tokenize(*line);
        if (tokens.empty())
        {
            continue;
        }

        const int result = execute_command(move(tokens));
        if (result == EXIT_SIGNAL)
        {
            break; // main() decides when/how to actually stop — cleanup would go here later
        }
    }

    return 0;
}

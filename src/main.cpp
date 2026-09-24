#include "myshell/reader.hpp"
#include "myshell/shell_ui.hpp"
#include "myshell/tokenizer.hpp"
#include "myshell/prompt.hpp"
#include "myshell/builtins.hpp"
#include "myshell/executor.hpp"
#include "myshell/functions.hpp"
#include "myshell/history.hpp"
#include "myshell/shell_state.hpp"
#include <iostream>
#include <utility>
using namespace std;
int main()
{
    int process_status = 0;
    show_launch_screen();
    while (true)
    { // repeat forever until we break

        auto line = read_line(build_prompt());
        if (!line)
        { // Ctrl+D was pressed
            cout << "\n";
            process_status = shell_last_status();
            break; // exit the loop, end the program
        }

        if (line->empty())
        {             // user just pressed Enter
            if (last_read_was_interrupted()) {
                set_shell_last_status(130);
            }
            continue; // skip to next loop iteration, re-prompt
        }

        add_history_entry(*line);

        string definition_error;
        const auto definition_result = register_function_definition(*line, definition_error);
        if (definition_result == FunctionDefinitionResult::syntax_error)
        {
            cerr << "myshell: syntax error: " << definition_error << '\n';
            set_shell_last_status(2);
            continue;
        }
        if (definition_result == FunctionDefinitionResult::registered)
        {
            set_shell_last_status(0);
            continue;
        }

        PipelineParseResult parsed = parse_pipeline(*line);
        if (parsed.has_error())
        {
            cerr << "myshell: syntax error: " << parsed.error << '\n';
            set_shell_last_status(2);
            continue;
        }
        if (!parsed.pipeline)
        {
            continue;
        }
        for (const auto& command : parsed.pipeline->commands) {
            cout<<"Parsed command name : "<<command.name<<endl;
            cout<<"Parsed command present : "<<command.present<<endl;
            cout<<"Parsed error : "<<parsed.error<<endl;
            for (auto const& args:command.arguments){
                cout<<"Args : "<<args<<endl;
            }
            cout<<endl;
        }
        const int result = execute_pipeline(move(*parsed.pipeline));
        if (result == EXIT_SIGNAL)
        {
            process_status = shell_requested_exit_status();
            break; // main() decides when/how to actually stop — cleanup would go here later
        }
        set_shell_last_status(result);
    }

    return process_status;
}

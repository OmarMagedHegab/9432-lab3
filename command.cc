
/*
 * 
 *
 * Template file.
 * You will need to add more code here to execute the command table.
 *
 * NOTE: You are responsible for fixing any bugs this code may have!
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <time.h>
#include <glob.h>
#include <wordexp.h>
#include <vector>
#include <string>
#include <cctype>
#include <iostream>
#include <sstream>
#include "command.h"
#include "tokenizer.h"

void parse(std::vector<Token> &tokens)
{
    Command &currentCommand = Command::_currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    for (size_t i = 0; i < tokens.size(); i++)
    {
        Token token = tokens[i];

        bool isRedirection =
            token.type == TOKEN_REDIRECT ||
            token.type == TOKEN_APPEND ||
            token.type == TOKEN_INPUT ||
            token.type == TOKEN_ERROR ||
            token.type == TOKEN_REDIRECT_AND_ERROR;

        if (isRedirection)
        {
            if (i + 1 >= tokens.size() ||
                (tokens[i + 1].type != TOKEN_ARGUMENT &&
                 tokens[i + 1].type != TOKEN_COMMAND))
            {
                fprintf(stderr, "Syntax error: expected filename after %s\n", token.value.c_str());

                for (int j = 0; j < currentSimpleCommand->_numberOfArguments; j++)
                {
                    free(currentSimpleCommand->_arguments[j]);
                }

                free(currentSimpleCommand->_arguments);
                delete currentSimpleCommand;

                currentCommand.clear();
                return;
            }

            bool redirectsOutput =
                token.type == TOKEN_REDIRECT ||
                token.type == TOKEN_APPEND ||
                token.type == TOKEN_REDIRECT_AND_ERROR;

            bool redirectsInput =
                token.type == TOKEN_INPUT;

            bool redirectsError =
                token.type == TOKEN_ERROR ||
                token.type == TOKEN_REDIRECT_AND_ERROR;

            if ((redirectsOutput && currentCommand._outFile != NULL) ||
                (redirectsInput && currentCommand._inputFile != NULL) ||
                (redirectsError && currentCommand._errFile != NULL))
            {
                fprintf(stderr, "Syntax error: repeated redirection for the same stream\n");

                for (int j = 0; j < currentSimpleCommand->_numberOfArguments; j++)
                {
                    free(currentSimpleCommand->_arguments[j]);
                }

                free(currentSimpleCommand->_arguments);
                delete currentSimpleCommand;

                currentCommand.clear();
                return;
            }
        }

        switch (token.type)
        {
            case TOKEN_COMMAND:
            case TOKEN_ARGUMENT:
                currentSimpleCommand->insertArgument(strdup(token.value.c_str()));
                break;

            case TOKEN_PIPE:
                if (currentSimpleCommand->_numberOfArguments == 0)
                {
                    fprintf(stderr, "Syntax error: missing command before pipe\n");
                    free(currentSimpleCommand->_arguments);
                    delete currentSimpleCommand;
                    currentCommand.clear();
                    return;
                }
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentSimpleCommand = new SimpleCommand();
                break;

            case TOKEN_REDIRECT:
                currentCommand._outFile = strdup(tokens[i + 1].value.c_str());
                i++;
                break;

            case TOKEN_APPEND:
                currentCommand._outFile = strdup(tokens[i + 1].value.c_str());
                currentCommand._append = 1;
                i++;
                break;

            case TOKEN_INPUT:
                currentCommand._inputFile = strdup(tokens[i + 1].value.c_str());
                i++;
                break;

            case TOKEN_ERROR:
                currentCommand._errFile = strdup(tokens[i + 1].value.c_str());
                i++;
                break;

            case TOKEN_REDIRECT_AND_ERROR:
                currentCommand._outFile = strdup(tokens[i + 1].value.c_str());
                currentCommand._errFile = strdup(tokens[i + 1].value.c_str());
                currentCommand._out_error = 1;
                currentCommand._append = 1;
                i++;
                break;

            case TOKEN_BACKGROUND:
                if (currentSimpleCommand->_numberOfArguments == 0 ||
                    i + 1 >= tokens.size() ||
                    tokens[i + 1].type != TOKEN_EOF)
                {
                    fprintf(stderr, "Syntax error: & must follow a command and end the line\n");

                    for (int j = 0; j < currentSimpleCommand->_numberOfArguments; j++)
                    {
                        free(currentSimpleCommand->_arguments[j]);
                    }

                    free(currentSimpleCommand->_arguments);
                    delete currentSimpleCommand;

                    currentCommand.clear();
                    return;
                }

                currentCommand._background = 1;
                break;

            case TOKEN_EOF:
                if (currentSimpleCommand->_numberOfArguments == 0 &&
                    currentCommand._numberOfSimpleCommands > 0)
                {
                    fprintf(stderr, "Syntax error: missing command after pipe\n");
                    free(currentSimpleCommand->_arguments);
                    delete currentSimpleCommand;
                    currentCommand.clear();
                    return;
                }

                if (currentSimpleCommand->_numberOfArguments > 0)
                {
                    currentCommand.insertSimpleCommand(currentSimpleCommand);
                }
                else
                {
                    free(currentSimpleCommand->_arguments);
                    delete currentSimpleCommand;
                }

                if (currentCommand._numberOfSimpleCommands > 0)
                {
                    currentCommand.execute();
                }
                else
                {
                    currentCommand.clear();
                }

                return;
        }
    }
}     

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfAvailableArguments == _numberOfArguments + 1)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(_arguments,
                                      _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _arguments[_numberOfArguments + 1] = NULL;

    _numberOfArguments++;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _numberOfSimpleCommands = 0;
    _simpleCommands = (SimpleCommand **)
        malloc(_numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _append = 0;
    _out_error = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfAvailableSimpleCommands == _numberOfSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(_simpleCommands,
                                                    _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));
    }

    _simpleCommands[_numberOfSimpleCommands] = simpleCommand;
    _numberOfSimpleCommands++;
}

void Command::clear()
{
    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            free(_simpleCommands[i]->_arguments[j]);
        }

        free(_simpleCommands[i]->_arguments);
        delete _simpleCommands[i];
    }

    if (_outFile)
    {
        free(_outFile);
    }

    if (_inputFile)
    {
        free(_inputFile);
    }

    if (_errFile)
    {
        free(_errFile);
    }

    _numberOfSimpleCommands = 0;
    _outFile = 0;
    _inputFile = 0;
    _errFile = 0;
    _background = 0;
    _append = 0;
    _out_error = 0;
}

void Command::print()
{
    printf("\n\n");
    printf("              COMMAND TABLE                \n");
    printf("\n");
    printf("  #   Simple Commands\n");
    printf("  --- ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
        {
            printf("\"%s\" \t", _simpleCommands[i]->_arguments[j]);
        }
    }

    printf("\n\n");
    printf("  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n", _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default", _errFile ? _errFile : "default", _out_error == 1 ? _errFile : "default"
           ,_background ? "YES" : "NO");
    printf("\n\n");
}

void Command::execute()
{
    print();

    if (_numberOfSimpleCommands == 0) {
        return;
    }

    if (strcmp(_simpleCommands[0]->_arguments[0], "exit") == 0) {
        printf("Good bye!!\n");
        exit(0);
    }

    if (strcmp(_simpleCommands[0]->_arguments[0], "cd") == 0) {
        if (_simpleCommands[0]->_arguments[1] != NULL) {
            chdir(_simpleCommands[0]->_arguments[1]);
        } else {
            chdir(getenv("HOME"));
        }
        
        clear();
        return;
    }

    int original_keyboard = dup(0);
    int original_screen   = dup(1);
    int original_error    = dup(2);

    int current_input;
    if (_inputFile != NULL) {
        current_input = open(_inputFile, O_RDONLY);
    } else {
        current_input = dup(original_keyboard);
    }

    int child_id;

    for (int i = 0; i < _numberOfSimpleCommands; i++) {
        
        dup2(current_input, 0);
        close(current_input); 

        int current_output;

        if (i == _numberOfSimpleCommands - 1) {
            
            if (_outFile != NULL) {
                if (_append == 1) {
                    current_output = open(_outFile, O_WRONLY | O_CREAT | O_APPEND, 0666);
                } else {
                    current_output = open(_outFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                }
            } else {
                current_output = dup(original_screen);
            }

            if (_errFile != NULL) {
                int error_output;
                if (_append == 1) {
                    error_output = open(_errFile, O_WRONLY | O_CREAT | O_APPEND, 0666);
                } else {
                    error_output = open(_errFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
                }
                dup2(error_output, 2); 
                close(error_output);
            }

        } 
        else {
            int my_pipe[2];
            pipe(my_pipe);
            
            current_output = my_pipe[1]; 
            current_input  = my_pipe[0]; 
        }

        dup2(current_output, 1);
        close(current_output);

        child_id = fork();

        if (child_id == 0) {
            execvp(_simpleCommands[i]->_arguments[0], _simpleCommands[i]->_arguments);
            perror("Command not found");
            exit(1);
        }
    } 

    dup2(original_keyboard, 0);
    dup2(original_screen, 1);
    dup2(original_error, 2);
    
    close(original_keyboard);
    close(original_screen);
    close(original_error);

    if (_background == 0) {
        waitpid(child_id, NULL, 0);
    }

    clear();
}
         
    
void Command::prompt()
{
    std::string input;

    while (true)
    {
        printf("myshell>");
        fflush(stdout);

        if (!std::getline(std::cin, input))
        {
            break;
        }

        std::vector<Token> tokens = tokenize(input);
        parse(tokens);
    }
}
Command Command::_currentCommand;
SimpleCommand *Command::_currentSimpleCommand;

int main()
{
    Command::_currentCommand.prompt();
    return 0;
}

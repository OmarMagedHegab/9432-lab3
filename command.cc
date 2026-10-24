
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
#include <errno.h>

void parse(std::vector<Token> &tokens)
{
    Command &currentCommand = Command::_currentCommand;
    SimpleCommand *currentSimpleCommand = new SimpleCommand();

    auto syntaxError = [&](const char *message)
    {
        fprintf(stderr, "Syntax error: %s\n", message);
        for (int j = 0; j < currentSimpleCommand->_numberOfArguments; j++)
            free(currentSimpleCommand->_arguments[j]);
        free(currentSimpleCommand->_arguments);
        delete currentSimpleCommand;
        currentCommand.clear();
    };

    std::vector<Token> mergedTokens;
    bool inQuotes = false;
    std::string currentQuotedString = "";
    Token mergedTokenTemplate;

    for (size_t i = 0; i < tokens.size(); i++)
    {
        if (!inQuotes)
        {
            if ((tokens[i].type == TOKEN_ARGUMENT || tokens[i].type == TOKEN_COMMAND) &&
                tokens[i].value.front() == '"')
            {
                if (tokens[i].value.back() == '"' && tokens[i].value.length() > 1)
                {
                    Token t = tokens[i];
                    t.value = t.value.substr(1, t.value.length() - 2); 
                    mergedTokens.push_back(t);
                }
                else
                {
                    inQuotes = true;
                    mergedTokenTemplate = tokens[i];
                    currentQuotedString = tokens[i].value.substr(1); 
                }
            }
            else
            {
                mergedTokens.push_back(tokens[i]);
            }
        }
        else
        {
            if (tokens[i].value.back() == '"')
            {
                currentQuotedString += " " + tokens[i].value.substr(0, tokens[i].value.length() - 1);
                mergedTokenTemplate.value = currentQuotedString;
                mergedTokens.push_back(mergedTokenTemplate);
                inQuotes = false;
            }
            else
            {
                currentQuotedString += " " + tokens[i].value;
            }
        }
    }

    for (size_t i = 0; i < mergedTokens.size(); i++)
    {
        Token token = mergedTokens[i];
        bool isRedirection =
            token.type == TOKEN_REDIRECT || token.type == TOKEN_APPEND ||
            token.type == TOKEN_INPUT || token.type == TOKEN_ERROR ||
            token.type == TOKEN_REDIRECT_AND_ERROR;

        if (isRedirection)
        {
            if (i + 1 >= mergedTokens.size() ||
                (mergedTokens[i + 1].type != TOKEN_ARGUMENT &&
                 mergedTokens[i + 1].type != TOKEN_COMMAND))
            {
                std::string message = "expected filename after " + token.value;
                syntaxError(message.c_str());
                return;
            }

            bool redirectsOutput = token.type == TOKEN_REDIRECT ||
                token.type == TOKEN_APPEND || token.type == TOKEN_REDIRECT_AND_ERROR;
            bool redirectsInput = token.type == TOKEN_INPUT;
            bool redirectsError = token.type == TOKEN_ERROR ||
                token.type == TOKEN_REDIRECT_AND_ERROR;

            if ((redirectsOutput && currentCommand._outFile != NULL) ||
                (redirectsInput && currentCommand._inputFile != NULL) ||
                (redirectsError && currentCommand._errFile != NULL))
            {
                syntaxError("repeated redirection for the same stream");
                return;
            }
        }

        switch (token.type)
        {
            case TOKEN_COMMAND:
            case TOKEN_ARGUMENT:
            {
                if (token.value.find('*') != std::string::npos || token.value.find('?') != std::string::npos)
                {
                    glob_t glob_result;
                    memset(&glob_result, 0, sizeof(glob_t));
                    
                    int return_value = glob(token.value.c_str(), GLOB_NOCHECK, NULL, &glob_result);
                    
                    if (return_value == 0)
                    {
                        for (size_t k = 0; k < glob_result.gl_pathc; ++k)
                        {
                            currentSimpleCommand->insertArgument(strdup(glob_result.gl_pathv[k]));
                        }
                    }
                    else
                    {
                        currentSimpleCommand->insertArgument(strdup(token.value.c_str()));
                    }
                    globfree(&glob_result);
                }
                else
                {
                    currentSimpleCommand->insertArgument(strdup(token.value.c_str()));
                }
                break;
            }

            case TOKEN_PIPE:
                if (currentSimpleCommand->_numberOfArguments == 0)
                {
                    syntaxError("missing command before pipe");
                    return;
                }
                currentCommand.insertSimpleCommand(currentSimpleCommand);
                currentSimpleCommand = new SimpleCommand();
                break;

            case TOKEN_REDIRECT:
                currentCommand._outFile = strdup(mergedTokens[++i].value.c_str());
                break;

            case TOKEN_APPEND:
                currentCommand._outFile = strdup(mergedTokens[++i].value.c_str());
                currentCommand._append = 1;
                break;

            case TOKEN_INPUT:
                currentCommand._inputFile = strdup(mergedTokens[++i].value.c_str());
                break;

            case TOKEN_ERROR:
                currentCommand._errFile = strdup(mergedTokens[++i].value.c_str());
                break;

            case TOKEN_REDIRECT_AND_ERROR:
                currentCommand._outFile = strdup(mergedTokens[++i].value.c_str());
                currentCommand._errFile = strdup(mergedTokens[i].value.c_str());
                currentCommand._out_error = 1;
                currentCommand._append = 1;
                break;

            case TOKEN_BACKGROUND:
                if (currentSimpleCommand->_numberOfArguments == 0 ||
                    i + 1 >= mergedTokens.size() || mergedTokens[i + 1].type != TOKEN_EOF)
                {
                    syntaxError("& must follow a command and end the line");
                    return;
                }
                currentCommand._background = 1;
                break;

            case TOKEN_EOF:
                if (currentSimpleCommand->_numberOfArguments == 0 &&
                    currentCommand._numberOfSimpleCommands > 0)
                {
                    syntaxError("missing command after pipe");
                    return;
                }
                if (currentSimpleCommand->_numberOfArguments > 0)
                    currentCommand.insertSimpleCommand(currentSimpleCommand);
                else
                {
                    free(currentSimpleCommand->_arguments);
                    delete currentSimpleCommand;
                }

                if (currentCommand._numberOfSimpleCommands > 0)
                    currentCommand.execute();
                else
                    currentCommand.clear();
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
    if (_numberOfSimpleCommands == 0)
    {
        clear();
        return;
    }

    print();
    fflush(stdout);

    auto changeDirectory = [](SimpleCommand *command) -> int
    {
        if (command->_numberOfArguments > 2)
        {
            fprintf(stderr, "cd: too many arguments\n");
            return 1;
        }
        const char *directory = command->_arguments[1];
        if (directory == NULL) directory = getenv("HOME");
        if (directory == NULL)
        {
            fprintf(stderr, "cd: HOME is not set\n");
            return 1;
        }
        if (chdir(directory) == -1)
        {
            perror("cd");
            return 1;
        }
        return 0;
    };

    int current_input = -1;
    int final_output = -1;
    int error_output = -1;

    auto closeDescriptors = [&]()
    {
        if (current_input != -1) close(current_input);
        if (final_output != -1) close(final_output);
        if (error_output != -1) close(error_output);
        current_input = final_output = error_output = -1;
    };

    auto executionError = [&](const char *message)
    {
        perror(message);
        closeDescriptors();
        clear();
    };

    if (_inputFile != NULL)
        current_input = open(_inputFile, O_RDONLY);
    else
        current_input = dup(STDIN_FILENO);

    if (current_input == -1)
    {
        executionError(_inputFile != NULL ? _inputFile : "dup input");
        return;
    }

    if (_outFile != NULL)
    {
        int flags = O_WRONLY | O_CREAT;
        flags |= (_append == 1) ? O_APPEND : O_TRUNC;
        final_output = open(_outFile, flags, 0666);
    }
    else
        final_output = dup(STDOUT_FILENO);

    if (final_output == -1)
    {
        executionError(_outFile != NULL ? _outFile : "dup output");
        return;
    }

    if (_out_error == 1)
        error_output = dup(final_output);
    else if (_errFile != NULL)
        error_output = open(_errFile, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    else
        error_output = dup(STDERR_FILENO);

    if (error_output == -1)
    {
        executionError(_errFile != NULL ? _errFile : "dup error");
        return;
    }

    bool isExit = strcmp(_simpleCommands[0]->_arguments[0], "exit") == 0;
    bool isCd = strcmp(_simpleCommands[0]->_arguments[0], "cd") == 0;

    if (_numberOfSimpleCommands == 1 && (isExit || isCd))
    {
        int original_keyboard = dup(STDIN_FILENO);
        int original_screen = dup(STDOUT_FILENO);
        int original_error = dup(STDERR_FILENO);

        if (original_keyboard == -1 || original_screen == -1 || original_error == -1)
        {
            perror("dup");
            if (original_keyboard != -1) close(original_keyboard);
            if (original_screen != -1) close(original_screen);
            if (original_error != -1) close(original_error);
            closeDescriptors();
            clear();
            return;
        }

        bool redirected =
            dup2(current_input, STDIN_FILENO) != -1 &&
            dup2(final_output, STDOUT_FILENO) != -1 &&
            dup2(error_output, STDERR_FILENO) != -1;

        if (!redirected) perror("dup2");
        else if (isExit) printf("Good bye!!\n");
        else changeDirectory(_simpleCommands[0]);

        fflush(stdout);
        fflush(stderr);
        if (dup2(original_keyboard, STDIN_FILENO) == -1) perror("restore input");
        if (dup2(original_screen, STDOUT_FILENO) == -1) perror("restore output");
        if (dup2(original_error, STDERR_FILENO) == -1) perror("restore error");
        close(original_keyboard);
        close(original_screen);
        close(original_error);
        closeDescriptors();
        clear();

        if (isExit && redirected) exit(0);
        return;
    }

    std::vector<pid_t> children;
    bool executionFailed = false;

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        int next_pipe[2] = {-1, -1};
        bool hasNextCommand = i < _numberOfSimpleCommands - 1;

        if (hasNextCommand && pipe(next_pipe) == -1)
        {
            perror("pipe");
            executionFailed = true;
            break;
        }

        pid_t child_id = fork();
        if (child_id == -1)
        {
            perror("fork");
            if (next_pipe[0] != -1) close(next_pipe[0]);
            if (next_pipe[1] != -1) close(next_pipe[1]);
            executionFailed = true;
            break;
        }

        if (child_id == 0)
        {
            signal(SIGINT, SIG_DFL);
            if (next_pipe[0] != -1) close(next_pipe[0]);
            int current_output = hasNextCommand ? next_pipe[1] : final_output;

            if (dup2(current_input, STDIN_FILENO) == -1 ||
                dup2(current_output, STDOUT_FILENO) == -1 ||
                dup2(error_output, STDERR_FILENO) == -1)
            {
                perror("dup2");
                _exit(1);
            }

            if (next_pipe[1] != -1) close(next_pipe[1]);
            closeDescriptors();
            SimpleCommand *command = _simpleCommands[i];
            if (strcmp(command->_arguments[0], "exit") == 0) _exit(0);
            if (strcmp(command->_arguments[0], "cd") == 0)
                _exit(changeDirectory(command));

            execvp(command->_arguments[0], command->_arguments);
            perror(command->_arguments[0]);
            _exit(127);
        }

        children.push_back(child_id);
        close(current_input);
        current_input = -1;
        if (hasNextCommand)
        {
            close(next_pipe[1]);
            current_input = next_pipe[0];
        }
    }

    closeDescriptors();
    if (executionFailed)
    {
        for (size_t j = 0; j < children.size(); j++)
            kill(children[j], SIGTERM);
    }

    if (_background == 0 || executionFailed)
    {
        for (size_t j = children.size(); j > 0; j--)
        {
            pid_t result;
            do
            {
                result = waitpid(children[j - 1], NULL, 0);
            }
            while (result == -1 && errno == EINTR);
            if (result == -1) perror("waitpid");
        }
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

extern "C" void sigchld_handler(int sig) {
    int pid;
    int status;
    int saved_errno = errno;

    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        int fd = open("process.log", O_WRONLY | O_CREAT | O_APPEND, 0666);
        if (fd >= 0) {
            char log_msg[100];
            int len = snprintf(log_msg, sizeof(log_msg), "Background process %d terminated.\n", pid);
            write(fd, log_msg, len);
            close(fd);
        }
    }
    errno = saved_errno;
}

int main()
{
    if (signal(SIGINT, SIG_IGN) == SIG_ERR) {
        perror("signal");
        return 1;
    }
    
    if (signal(SIGCHLD, sigchld_handler) == SIG_ERR) {
        perror("signal");
        return 1;
    }

    Command::_currentCommand.prompt();
    return 0;
}

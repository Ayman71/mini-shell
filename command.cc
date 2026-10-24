#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <string.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>
#include <vector>
#include <string>
#include <iostream>
#include "command.h"
#include "tokenizer.h"

using namespace std;

static char *copyString(const string &text)
{
    char *copy = (char *)malloc(text.length() + 1);

    if (copy != NULL)
        strcpy(copy, text.c_str());

    return copy;
}

static void childFinished(int signalNumber)
{
    int status;
    pid_t child;

    (void)signalNumber;

    while ((child = waitpid(-1, &status, WNOHANG)) > 0)
    {
        char message[80];
        char number[20];
        int length = 0;
        int n = 0;
        pid_t value = child;

        const char start[] = "Child process ";
        while (start[length] != '\0')
        {
            message[length] = start[length];
            length++;
        }

        if (value == 0)
        {
            number[n++] = '0';
        }
        else
        {
            char reversed[20];
            int r = 0;

            while (value > 0 && r < 19)
            {
                reversed[r++] = '0' + (value % 10);
                value /= 10;
            }

            while (r > 0)
                number[n++] = reversed[--r];
        }

        for (int i = 0; i < n; i++)
            message[length++] = number[i];

        const char ending[] = " terminated\n";
        for (int i = 0; ending[i] != '\0'; i++)
            message[length++] = ending[i];

        int fd = open("myshell.log", O_WRONLY | O_CREAT | O_APPEND, 0644);
        if (fd >= 0)
        {
            write(fd, message, length);
            close(fd);
        }
    }
}

void parse(vector<Token> &tokens)
{
    Command command;
    SimpleCommand *simpleCommand = new SimpleCommand();
    bool expectingFile = false;
    int redirectionType = 0;
    bool invalid = false;

    for (unsigned int i = 0; i < tokens.size(); i++)
    {
        Token token = tokens[i];

        if (token.type == TOKEN_EOF)
            break;

        if (expectingFile)
        {
            if (token.type != TOKEN_EOF &&
                token.type != TOKEN_PIPE &&
                token.type != TOKEN_REDIRECT &&
                token.type != TOKEN_APPEND &&
                token.type != TOKEN_INPUT &&
                token.type != TOKEN_ERROR &&
                token.type != TOKEN_REDIRECT_AND_ERROR &&
                token.type != TOKEN_BACKGROUND)
            {
                char *fileName = copyString(token.value);

                if (fileName == NULL)
                {
                    cerr << "Error: not enough memory.\n";
                    invalid = true;
                    break;
                }

                if (redirectionType == 1)
                    command._outFile = fileName;
                else if (redirectionType == 2)
                    command._inputFile = fileName;
                else if (redirectionType == 3)
                    command._errFile = fileName;
                else if (redirectionType == 4)
                {
                    command._outFile = copyString(token.value);
                    command._errFile = fileName;
                    command._out_error = 1;
                }

                expectingFile = false;
                redirectionType = 0;
            }
            else
            {
                cerr << "Syntax error: missing file name after redirection.\n";
                invalid = true;
                break;
            }

            continue;
        }

        if (token.type == TOKEN_PIPE)
        {
            if (simpleCommand->_numberOfArguments == 0)
            {
                cerr << "Syntax error: empty command before or after pipe.\n";
                invalid = true;
                break;
            }

            command.insertSimpleCommand(simpleCommand);
            simpleCommand = new SimpleCommand();
        }
        else if (token.type == TOKEN_REDIRECT)
        {
            expectingFile = true;
            redirectionType = 1;
        }
        else if (token.type == TOKEN_APPEND)
        {
            expectingFile = true;
            redirectionType = 1;
            command._append = 1;
        }
        else if (token.type == TOKEN_INPUT)
        {
            expectingFile = true;
            redirectionType = 2;
        }
        else if (token.type == TOKEN_ERROR)
        {
            expectingFile = true;
            redirectionType = 3;
        }
        else if (token.type == TOKEN_REDIRECT_AND_ERROR)
        {
            expectingFile = true;
            redirectionType = 4;
            command._append = 1;
        }
        else if (token.type == TOKEN_BACKGROUND)
        {
            command._background = 1;

            if (i + 1 < tokens.size() && tokens[i + 1].type != TOKEN_EOF)
            {
                cerr << "Syntax error: '&' must be at the end of the command.\n";
                invalid = true;
                break;
            }
        }
        else
        {
            char *argument = copyString(token.value);

            if (argument == NULL)
            {
                cerr << "Error: not enough memory.\n";
                invalid = true;
                break;
            }

            simpleCommand->insertArgument(argument);
        }
    }

    if (expectingFile)
    {
        cerr << "Syntax error: missing file name after redirection.\n";
        invalid = true;
    }

    if (!invalid && simpleCommand->_numberOfArguments == 0 &&
        command._numberOfSimpleCommands == 0)
    {
        delete simpleCommand;
        command.clear();
        return;
    }

    if (!invalid)
    {
        if (simpleCommand->_numberOfArguments == 0)
        {
            cerr << "Syntax error: command is missing.\n";
            invalid = true;
        }
        else
        {
            command.insertSimpleCommand(simpleCommand);
        }
    }

    if (invalid)
    {
        delete simpleCommand;
        command.clear();
        return;
    }

    command.execute();
}

SimpleCommand::SimpleCommand()
{
    _numberOfAvailableArguments = 5;
    _numberOfArguments = 0;
    _arguments = (char **)malloc(_numberOfAvailableArguments * sizeof(char *));
    if (_arguments != NULL)
        _arguments[0] = NULL;
}

void SimpleCommand::insertArgument(char *argument)
{
    if (_numberOfArguments + 1 >= _numberOfAvailableArguments)
    {
        _numberOfAvailableArguments *= 2;
        _arguments = (char **)realloc(
            _arguments, _numberOfAvailableArguments * sizeof(char *));
    }

    _arguments[_numberOfArguments] = argument;
    _numberOfArguments++;
    _arguments[_numberOfArguments] = NULL;
}

Command::Command()
{
    _numberOfAvailableSimpleCommands = 1;
    _numberOfSimpleCommands = 0;
    _simpleCommands = (SimpleCommand **)malloc(
        _numberOfAvailableSimpleCommands * sizeof(SimpleCommand *));

    _outFile = NULL;
    _inputFile = NULL;
    _errFile = NULL;
    _out_error = 0;
    _append = 0;
    _background = 0;
}

void Command::insertSimpleCommand(SimpleCommand *simpleCommand)
{
    if (_numberOfSimpleCommands == _numberOfAvailableSimpleCommands)
    {
        _numberOfAvailableSimpleCommands *= 2;
        _simpleCommands = (SimpleCommand **)realloc(
            _simpleCommands,
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
            free(_simpleCommands[i]->_arguments[j]);

        free(_simpleCommands[i]->_arguments);
        delete _simpleCommands[i];
    }

    if (_outFile != NULL)
        free(_outFile);
    if (_inputFile != NULL)
        free(_inputFile);
    if (_errFile != NULL)
        free(_errFile);

    _numberOfSimpleCommands = 0;

    _outFile = NULL;
    _inputFile = NULL;
    _errFile = NULL;
    _out_error = 0;
    _append = 0;
    _background = 0;
}

void Command::print()
{
    printf("\n              COMMAND TABLE\n");
    printf("  #   Simple Commands\n");
    printf("  ----------------------------------------------------------\n");

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        printf("  %-3d ", i);
        for (int j = 0; j < _simpleCommands[i]->_numberOfArguments; j++)
            printf("\"%s\" ", _simpleCommands[i]->_arguments[j]);
        printf("\n");
    }

    printf("\n  Output       Input        Error        Err&Out       Background\n");
    printf("  ------------ ------------ ------------ ------------ ------------\n");
    printf("  %-12s %-12s %-12s %-12s %-12s\n",
           _outFile ? _outFile : "default",
           _inputFile ? _inputFile : "default",
           _errFile ? _errFile : "default",
           _out_error ? _errFile : "default",
           _background ? "YES" : "NO");
}

void Command::execute()
{
    if (_numberOfSimpleCommands == 0)
    {
        clear();
        return;
    }

    if (_numberOfSimpleCommands == 1 &&
        strcmp(_simpleCommands[0]->_arguments[0], "exit") == 0)
    {
        clear();
        printf("Good bye!!\n");
        exit(0);
    }

    if (_numberOfSimpleCommands == 1 &&
        strcmp(_simpleCommands[0]->_arguments[0], "cd") == 0)
    {
        const char *directory = _simpleCommands[0]->_arguments[1];

        if (directory == NULL)
            directory = getenv("HOME");

        if (directory == NULL || chdir(directory) != 0)
            perror("cd");

        clear();
        return;
    }

    print();

    int previousInput = -1;
    vector<pid_t> childPids;

    for (int i = 0; i < _numberOfSimpleCommands; i++)
    {
        int pipeFds[2] = {-1, -1};

        if (i < _numberOfSimpleCommands - 1)
        {
            if (pipe(pipeFds) == -1)
            {
                perror("pipe");
                if (previousInput != -1)
                    close(previousInput);
                clear();
                return;
            }
        }

        pid_t child = fork();

        if (child == -1)
        {
            perror("fork");
            if (previousInput != -1)
                close(previousInput);
            if (pipeFds[0] != -1)
                close(pipeFds[0]);
            if (pipeFds[1] != -1)
                close(pipeFds[1]);
            clear();
            return;
        }

        if (child == 0)
        {
            signal(SIGINT, SIG_DFL);

            if (previousInput != -1)
            {
                dup2(previousInput, STDIN_FILENO);
                close(previousInput);
            }

            if (pipeFds[1] != -1)
                dup2(pipeFds[1], STDOUT_FILENO);

            if (pipeFds[0] != -1)
                close(pipeFds[0]);
            if (pipeFds[1] != -1)
                close(pipeFds[1]);

            if (i == 0 && _inputFile != NULL)
            {
                int fd = open(_inputFile, O_RDONLY);
                if (fd == -1)
                {
                    perror(_inputFile);
                    exit(1);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            if (i == _numberOfSimpleCommands - 1 && _outFile != NULL)
            {
                int flags = O_WRONLY | O_CREAT;
                if (_append)
                    flags |= O_APPEND;
                else
                    flags |= O_TRUNC;

                int fd = open(_outFile, flags, 0644);
                if (fd == -1)
                {
                    perror(_outFile);
                    exit(1);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            }

            if (_errFile != NULL)
            {
                int flags = O_WRONLY | O_CREAT;
                if (_append)
                    flags |= O_APPEND;
                else
                    flags |= O_TRUNC;

                int fd = open(_errFile, flags, 0644);
                if (fd == -1)
                {
                    perror(_errFile);
                    exit(1);
                }
                dup2(fd, STDERR_FILENO);
                close(fd);
            }

            if (_out_error && _errFile != NULL)
                dup2(STDOUT_FILENO, STDERR_FILENO);

            execvp(_simpleCommands[i]->_arguments[0],
                   _simpleCommands[i]->_arguments);

            perror(_simpleCommands[i]->_arguments[0]);
            exit(1);
        }

        childPids.push_back(child);

        if (previousInput != -1)
            close(previousInput);

        if (pipeFds[1] != -1)
            close(pipeFds[1]);

        previousInput = pipeFds[0];
    }

    if (previousInput != -1)
        close(previousInput);

    if (!_background)
    {
        for (unsigned int i = 0; i < childPids.size(); i++)
        {
            int status;
            while (waitpid(childPids[i], &status, 0) == -1 && errno == EINTR)
            {
                /*wait*/
            }
        }
    }

    clear();
}

void Command::prompt()
{
    signal(SIGINT, SIG_IGN);
    signal(SIGCHLD, childFinished);

    string input;

    while (true)
    {
        cout << "myshell> ";
        cout.flush();

        if (!getline(cin, input))
        {
            cout << "\n";
            break;
        }

        vector<Token> tokens = tokenize(input);
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

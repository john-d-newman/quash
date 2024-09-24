#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <sys/stat.h>

#define MAX_COMMAND_LENGTH 100
#define MAX_ARGUMENTS 30

// Structure to store information about background jobs
struct Job
{
    pid_t pid;
    char command[MAX_COMMAND_LENGTH];
    int completed;
    int job_id; // Unique job ID
    struct Job *next;
    char status[20];
};

struct Job *jobs_list = NULL; // Initialize the list of background jobs
int next_job_id = 1;          // Initialize the next job ID

// Function to add a new background job to the list
void add_job(pid_t pid, char input[MAX_COMMAND_LENGTH])
{
    int i = 0;
    while (input[i] != '&')
    {
        if (input[i] == '\0')
        {
            input[i] = ' ';
        }
        i++;
    }
    input[i] = '\0';

    struct Job *new_job = (struct Job *)malloc(sizeof(struct Job));
    new_job->pid = pid;
    strncpy(new_job->command, input, sizeof(new_job->command));
    new_job->completed = 0;
    new_job->job_id = next_job_id++;
    strncpy(new_job->status, "Running", sizeof(new_job->status));
    printf("Background job started: [%i] %d %s\n", new_job->job_id, new_job->pid, new_job->command);
    new_job->next = jobs_list;

    jobs_list = new_job;
}

// Function to update and report the status of background jobs
void update_jobs_status()
{
    struct Job *job = jobs_list;
    while (job != NULL)
    {
        int status;

        pid_t result = waitpid(job->pid, &status, WNOHANG); // Check job status without blocking
        if (result == -1)
        {
            strncpy(job->status, "Terminated", sizeof(job->status));
        }
        else if (result == 0)
        {
            strncpy(job->status, "Running", sizeof(job->status));
        }
        else
        {
            // The job has completed
            job->completed = 1;
            if (strcmp(job->status, "Terminated") != 0)
            {
                strncpy(job->status, "Completed", sizeof(job->status));
            }
        }
        job = job->next;
    }

    // Clean up completed jobs
    struct Job *current = jobs_list;
    struct Job *prev = NULL;

    while (current != NULL)
    {

        if (current->completed || strcmp(current->status, "Terminated") == 0)
        {
            if (strcmp(current->status, "Completed") == 0)
            {
                printf("%s: [%i] %d %s\n", current->status, current->job_id, current->pid, current->command);
            }

            if (prev == NULL)
            {
                jobs_list = current->next;
            }
            else
            {
                prev->next = current->next;
            }
            if (!(strcmp(current->status, "Terminated") == 0))
            {
                free(current);
            }
            current = prev ? prev->next : jobs_list;
        }
        else
        {
            prev = current;
            current = current->next;
        }
    }
}

// Function to handle built-in commands
int handle_builtin(char **args, int in_fd, int out_fd, int identify)
{
    if (identify == 0 || identify == 5)
    {
        if (in_fd != 0)
        {
            if (dup2(in_fd, 0) == -1)
            {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
        }

        if (out_fd != 1)
        {
            if (dup2(out_fd, 1) == -1)
            {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
        }
    }

    if (strcmp(args[0], "echo") == 0)
    {
        for (int i = 1; args[i] != NULL; i++)
        {
            if (args[i][0] == '$')
            {
                // Check if the argument is an environment variable
                char *env_var_name = args[i] + 1; // Skip the '$' character
                char *env_var_value = getenv(env_var_name);
                if (env_var_value != NULL)
                {
                    printf("%s ", env_var_value);
                }
                else
                {
                    fprintf(stderr, "echo: Undefined environment variable: %s\n", env_var_name);
                }
            }
            else
            {
                printf("%s ", args[i]);
            }
        }
        printf("\n");
    }

    else if (strcmp(args[0], "export") == 0)
    {
        if (args[1] != NULL)
        {
            char *env_var = args[1];
            if (strchr(env_var, '=') != NULL)
            {
                char *var_name = strtok(env_var, "=");
                char *var_value = strtok(NULL, "=");

                setenv(var_name, var_value, 1); // Update the environment variable
            }
            else
            {
                fprintf(stderr, "export: invalid argument\n");
            }
        }
        else
        {
            fprintf(stderr, "export: missing argument\n");
        }
    }

    else if (strcmp(args[0], "cd") == 0)
    {
        if (args[1] == NULL)
        {
            fprintf(stderr, "cd: missing directory\n");
        }
        else if (chdir(args[1]) == 0)
        {
            char *new_pwd = getcwd(NULL, 0);
            if (new_pwd != NULL)
            {
                setenv("PWD", new_pwd, 1);
                free(new_pwd);
            }
            else
            {
                perror("getcwd");
            }
        }
        else
        {
            perror("chdir");
        }
    }

    else if (strcmp(args[0], "pwd") == 0)
    {
        char cwd[1024];
        if (getcwd(cwd, sizeof(cwd)) != NULL)
        {
            printf("%s\n", cwd);
        }
        else
        {
            perror("getcwd");
        }
    }

    else if (strcmp(args[0], "jobs") == 0)
    {
        update_jobs_status();
        struct Job *job = jobs_list;
        struct Job *jobs[MAX_COMMAND_LENGTH];
        int start = 0;
        int index = 0;
        int total = 0;
        while (job != NULL)
        {
            if (!job->completed)
            {
                jobs[index++] = job;
            }
            job = job->next;
        }
        total = index;
        index--;

        while (start < index)
        {
            struct Job *tmp = jobs[start];
            jobs[start] = jobs[index];
            jobs[index] = tmp;
            start++;
            index--;
        }
        for (int i = 0; i < total; i++)
        {
            printf("[%d] %d %s\n", jobs[i]->job_id, jobs[i]->pid, jobs[i]->command);
        }
    }
    else if (strcmp(args[0], "kill") == 0)
    {

        long long int pid = strtoll(args[2], NULL, 0);
        int signum = atoi(args[1]);
        struct Job *job = jobs_list;

        while (job != NULL)
        {
            if (job->pid == pid)
            {
                strncpy(job->status, "Terminated", sizeof(job->status));
                job->completed = 1;
                break;
            }
            job = job->next;
        }

        update_jobs_status();
        if (kill(pid, signum) == -1)
        {
            perror("kill");
        }
    }
    else
    {

        return 0; // not a built-in command
    }
    if (identify == 0 || identify == 5)
    {
        if (in_fd != 0)
        {
            close(in_fd);
        }

        if (out_fd != 1)
        {
            close(out_fd);
        }
    }
    return 1; // Command was a built-in command
}

// Function to expand environment variables in a string
char *expand_environment_variables(char *input)
{
    char expanded[10 * MAX_COMMAND_LENGTH]; // Allocate space for possible expansion

    const char *token = input;
    char *output = expanded; // Pointer to the current position in the output buffer

    while (*token)
    {
        if (*token == '$' && (token == input || token[-1] != '\\'))
        {
            const char *start = token + 1;
            const char *end = start;

            // Parse the variable name
            while (*end && ((*end >= 'a' && *end <= 'z') || (*end >= 'A' && *end <= 'Z') || (*end >= '0' && *end <= '9') || *end == '_'))
            {
                end++;
            }

            if (end > start)
            {
                size_t var_name_length = end - start;
                char var_name[var_name_length + 1];
                strncpy(var_name, start, var_name_length);
                var_name[var_name_length] = '\0';

                char *var_value = getenv(var_name);
                if (var_value != NULL)
                {
                    size_t var_value_length = strlen(var_value);
                    strncpy(output, var_value, var_value_length);
                    output += var_value_length;
                    token = end;
                }
                else
                {
                    // If the environment variable doesn't exist, keep the original text
                    while (start < end)
                    {
                        *output = *start;
                        output++;
                        start++;
                    }
                }
            }
            else
            {
                // If there is no variable name, keep the original character
                *output = *token;
                output++;
                token++;
            }
        }
        else
        {
            *output = *token;
            output++;
            token++;
        }
    }

    *output = '\0'; // Null-terminate the expanded string
    strcpy(input, expanded);

    return input;
}

// Function to execute a command with input and output redirection
void execute_command(char **args, int in_fd, int out_fd, int final, int background, int start, char input[MAX_COMMAND_LENGTH])
{
    pid_t pid = fork();

    if (pid < 0)
    {
        perror("fork");
        exit(EXIT_FAILURE);
    }

    if (pid == 0)
    { // Child process
        if (in_fd != 0)
        {

            if (dup2(in_fd, 0) == -1)
            {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(in_fd);
        }

        if (out_fd != 1)
        {
            if (dup2(out_fd, 1) == -1)
            {
                perror("dup2");
                exit(EXIT_FAILURE);
            }
            close(out_fd);
        }

        if (strstr(" echo export cd pwd quit exit jobs kill ", args[0]) != NULL)
        {
            handle_builtin(args, in_fd, out_fd, 1);
            exit(EXIT_SUCCESS);
        }
        else
        {
            execvp(args[0], args);
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }
    else
    { // Parent process
        if (background == 0)
        {
            int status;
            waitpid(pid, &status, 0);
        }
        if (background == 1 && start == 1)
        {
            add_job(pid, input);
        }

        if (in_fd != 0)
        {
            close(in_fd);
        }
        if (out_fd != 1)
        {
            close(out_fd);
        }
    }
}

int tokenize_input(char *input, char **args, int arg_count)
{
    char *token;
    int inside_single_quote = 0;
    int inside_double_quote = 0;
    int comment = 0;

    // Split the input using whitespace as the initial delimiter
    token = strtok(input, " ");

    while (token != NULL)
    {
        // Check if this token starts a comment
        if (!inside_single_quote && !inside_double_quote && token[0] == '#')
        {
            // Ignore the rest of the line and break
            break;
        }

        // Iterate through the characters in the token
        for (int i = 0; token[i] != '\0'; i++)
        {
            if (token[0] == '\'' && inside_double_quote == 0 && !comment)
            {
                inside_single_quote = 1;
            }
            else if (token[0] == '"' && !inside_single_quote && !comment)
            {
                inside_double_quote = 1;
            }
            else if (token[i] == '#' && !inside_single_quote && !inside_double_quote)
            {
                // If # is found outside of quotes, it starts a comment
                comment = 1;
                // Ignore the rest of the line and break
                break;
            }
        }

        // If not inside a comment, add the token to the args
        if (!comment)
        {
            if (inside_double_quote)
            {
                // Remove quotes from the token
                if (token[0] == '"')
                {
                    memmove(token, token + 1, strlen(token));
                    // Check if the last character is a quote and remove it
                    if (token[strlen(token) - 1] == '"')
                    {
                        token[strlen(token) - 1] = '\0';
                        inside_double_quote = !inside_double_quote;
                    }
                }

                args[arg_count] = token;
                while (inside_double_quote)
                {
                    int i = 0;
                    int j = 0;
                    token = strtok(NULL, " ");

                    if (token[strlen(token) - 1] == '"')
                    {
                        while (args[arg_count][i] != '\0')
                        {
                            i++;
                        }
                        args[arg_count][i++] = ' ';
                        while (token[j] != '\0')
                        {
                            args[arg_count][i] = token[j];
                            i++;
                            j++;
                        }

                        args[arg_count][strlen(args[arg_count]) - 2] = '\0';
                        inside_double_quote = !inside_double_quote;
                        continue;
                    }
                    else
                    {
                        while (args[arg_count][i] != '\0')
                        {
                            i++;
                        }
                        args[arg_count][i++] = ' ';
                        while (token[j] != '\0')
                        {
                            args[arg_count][i] = token[j];
                            i++;
                            j++;
                        }
                        args[arg_count][i] = '\0';
                    }
                }
                arg_count++;
            }
            else if (inside_single_quote)
            {
                // Remove quotes from the token
                if (token[0] == '\'')
                {
                    memmove(token, token + 1, strlen(token));
                    // Check if the last character is a quote and remove it
                    if (token[strlen(token) - 1] == '\'')
                    {
                        token[strlen(token) - 1] = '\0';
                        inside_single_quote = !inside_single_quote;
                    }
                }
                args[arg_count] = token;
                while (inside_single_quote)
                {
                    int i = 0;
                    int j = 0;
                    token = strtok(NULL, " ");
                    if (token[strlen(token) - 1] == '\'')
                    {
                        while (args[arg_count][i] != '\0')
                        {
                            i++;
                        }
                        args[arg_count][i++] = ' ';
                        while (token[j] != '\0')
                        {
                            args[arg_count][i] = token[j];
                            i++;
                            j++;
                        }
                        args[arg_count][strlen(args[arg_count]) - 2] = '\0';
                        inside_single_quote = !inside_single_quote;
                        continue;
                    }
                    else
                    {
                        while (args[arg_count][i] != '\0')
                        {
                            i++;
                        }
                        args[arg_count][i++] = ' ';
                        while (token[j] != '\0')
                        {
                            args[arg_count][i] = token[j];
                            i++;
                            j++;
                        }
                        args[arg_count][i] = '\0';
                    }
                }
                arg_count++;
            }
            else
            {
                args[arg_count++] = token;
            }
        }
        else
        {
            // If inside a comment, break to stop further processing
            break;
        }

        // Get the next token
        token = strtok(NULL, " ");
    }

    // Ensure the last element is NULL to indicate the end of the argument list
    args[arg_count] = NULL;
    return arg_count;
}

int main()
{
    // hold input data in this and a copy for job creation
    char input[MAX_COMMAND_LENGTH];
    char input_copy[MAX_COMMAND_LENGTH];
    // save stdin and out file descriptors so we can reuse them
    int saved_stdout = dup(1);
    int saved_stdin = dup(0);
    // define a string of all the built in commands to check for them later
    char *builtin = " echo export cd pwd quit exit jobs kill ";
    // start command
    printf("Welcome...\n");

    while (1)
    {
        // reset stdin and out if they got messed up
        if (dup2(saved_stdin, 0) == -1)
        {
            perror("dup2");
            exit(EXIT_FAILURE);
        }

        if (dup2(saved_stdout, 1) == -1)
        {
            perror("dup2");
            exit(EXIT_FAILURE);
        }

        // update background jobs to see if any finished
        update_jobs_status();
        // clean stdout so input isn't messed up accidentally
        fflush(stdout);
        // Here we go boys
        printf("[QUASH]$ ");

        // Read user input
        if (fgets(input, sizeof(input), stdin) == NULL)
        {
            perror("fgets");
            exit(EXIT_FAILURE);
        }

        // Remove the newline character at the end
        input[strcspn(input, "\n")] = '\0';

        if (input[0] == '\0')
        {
            continue;
        }
        // copy input to copy for use in job
        strcpy(input_copy, input);

        // Tokenize the user input into arguments, handling comments and quoted sections
        // arguments will be held in args
        char *args[MAX_ARGUMENTS];
        // number of arguments found
        int arg_count = 0;
        // if the command should run in the background
        int background = 0;

        // tokenize the string into arguments, this returns the number of arguments found
        // input is expanded so evnironment varibales are expanded, still don't know whether they want the full value for jobs, but too late.
        arg_count = tokenize_input(expand_environment_variables(input), args, arg_count);

        // if there is more than one argument
        if (arg_count > 1)
        {
            // check if the last argument is the background symbol
            if (strcmp(args[arg_count - 1], "&") == 0)
            {
                // if it is, say its background, get rid of the symbol, and correct the arg count
                background = 1;
                args[arg_count - 1] = '\0';
                arg_count -= 1;
            }
        }

        // if there are no arguments, go back to the start
        if (arg_count == 0)
        {
            continue;
        }

        int done = 0;
        // if the first argument is quit or exit, end the process byt setting the done value
        if ((strcmp(args[0], "quit") == 0) || (strcmp(args[0], "exit") == 0))
        {
            // if there is another command then complain and contiue
            if (args[1] != NULL)
            {
                printf("%s does not require additional arguments\n", args[0]);
                continue;
            }
            // otherwise set done
            else
            {
                done = 1;
            }
        }
        // if done flag is set, then end the while loop
        if (done == 1)
        {
            break;
        }

        // Check for pipe symbols
        int pipe_positions[MAX_ARGUMENTS]; // To store positions of pipe symbols
        int num_pipes = 0;                 // number of pipes found
        int redirected = 0;                // if the command was redirected by pipes so its not run twice

        // check all the arguments
        for (int i = 0; i < arg_count; i++)
        {
            if (strcmp(args[i], "|") == 0)
            { // if its a pipe
                if (num_pipes < MAX_ARGUMENTS)
                {
                    pipe_positions[num_pipes] = i; // set index as a pipe position
                    num_pipes++;                   // increase number of pipes
                }
                else
                {
                    fprintf(stderr, "Too many pipes in the command\n"); // otherwise complain about it
                    exit(EXIT_FAILURE);                                 // and exit
                }
            }
        }

        if (num_pipes > 0)
        {                   // if there are pipes
            redirected = 1; // say this is redirected so its not done again
            // Handle piped commands
            int pipes_fd[num_pipes][2]; // create num_pipes pipes
            int command_start = 0;      // refrence for where pipe starts
            int command_end = 0;        // the end

            for (int i = 0; i <= num_pipes; i++)
            {
                if (i < num_pipes)
                {
                    command_end = pipe_positions[i];
                }
                else
                {
                    command_end = arg_count;
                }

                char *command_args[MAX_ARGUMENTS];
                int command_args_count = 0;

                for (int j = command_start; j < command_end; j++)
                {
                    command_args[command_args_count] = args[j];
                    command_args_count++;
                }

                command_args[command_args_count] = NULL;

                if (i < num_pipes)
                {
                    if (pipe(pipes_fd[i]) == -1)
                    {
                        perror("pipe");
                        exit(EXIT_FAILURE);
                    }
                }
                int redirect_in = 0;  // File descriptor for input redirection
                int redirect_out = 1; // File descriptor for output redirection

                for (int i = 0; i < command_args_count; i++)
                {
                    if (strcmp(command_args[i], ">") == 0)
                    {
                        if (args[i + 1] != NULL)
                        {
                            redirect_out = open(command_args[i + 1], O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                            if (redirect_out == -1)
                            {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            // Terminate the command before redirection
                            command_args[i] = NULL;
                            command_args[i + 1] = NULL;
                            command_args_count--;
                            command_args_count--;
                            break;
                        }
                        else
                        {
                            fprintf(stderr, "Missing filename for output redirection\n");
                            break;
                        }
                    }
                    else if (strcmp(command_args[i], ">>") == 0)
                    {
                        if (command_args[i + 1] != NULL)
                        {
                            redirect_out = open(command_args[i + 1], O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                            if (redirect_out == -1)
                            {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            // Terminate the command before redirection
                            command_args[i] = NULL;
                            command_args[i + 1] = NULL;
                            command_args_count--;
                            command_args_count--;
                            break;
                        }
                        else
                        {
                            fprintf(stderr, "Missing filename for output redirection\n");
                            break;
                        }
                    }
                    else if (strcmp(command_args[i], "<") == 0)
                    {
                        if (command_args[i + 1] != NULL)
                        {
                            redirect_in = open(command_args[i + 1], O_RDONLY);
                            if (redirect_in == -1)
                            {
                                perror("open");
                                exit(EXIT_FAILURE);
                            }
                            // Terminate the command before redirection
                            command_args[i] = NULL;
                            command_args[i + 1] = NULL;
                            command_args_count--;
                            command_args_count--;
                        }
                        else
                        {
                            fprintf(stderr, "Missing filename for input redirection\n");
                            break;
                        }
                    }
                }

                if (i == 0)
                {
                    // For the first command, set the input to stdin
                    if (strstr(builtin, command_args[0]) != NULL)
                    {
                        handle_builtin(command_args, redirect_in, pipes_fd[i][1], 3);
                    }
                    else
                    {
                        execute_command(command_args, redirect_in, pipes_fd[i][1], 0, background, 1, input_copy);
                    }
                }
                else if (i == num_pipes)
                {
                    // For the last command, set the output to stdout
                    if (strstr(builtin, command_args[0]) != NULL)
                    {
                        handle_builtin(args, pipes_fd[i - 1][0], redirect_out, 4);
                    }
                    else
                    {
                        execute_command(command_args, pipes_fd[i - 1][0], redirect_out, 1, background, 0, input_copy);
                    }
                }
                else
                {
                    // For intermediate commands, set both input and output
                    if (strstr(builtin, command_args[0]) != NULL)
                    {
                        handle_builtin(args, pipes_fd[i - 1][0], pipes_fd[i][1], 5);
                    }
                    else
                    {
                        execute_command(command_args, pipes_fd[i - 1][0], pipes_fd[i][1], 0, background, 0, input_copy);
                    }
                }
                if (redirect_in > 0)
                {
                    close(redirect_in);
                }
                if (redirect_out > 1)
                {
                    close(redirect_out);
                }
                command_start = command_end + 1;
            }
            for (size_t i = 0; i < num_pipes; i++)
            {
                // Close the pipe file descriptors
                close(pipes_fd[i][0]);
                close(pipes_fd[i][1]);
            }
        }
        else
        {
            // If there are no pipes, execute the command as usual
            int redirect_in = 0;  // File descriptor for input redirection
            int redirect_out = 1; // File descriptor for output redirection

            for (int i = 0; i < arg_count; i++)
            {
                int index = i + 1;
                if (strcmp(args[i], ">") == 0)
                {
                    if (args[i + 1] != NULL)
                    {
                        redirect_out = open(args[index], O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                        if (redirect_out == -1)
                        {
                            perror("open");
                            exit(EXIT_FAILURE);
                        }
                        // Terminate the command before redirection
                        args[i] = NULL;
                        args[i + 1] = NULL;
                        break;
                    }
                    else
                    {
                        fprintf(stderr, "Missing filename for output redirection\n");
                        break;
                    }
                }
                else if (strcmp(args[i], ">>") == 0)
                {
                    if (args[i + 1] != NULL)
                    {
                        redirect_out = open(args[i + 1], O_WRONLY | O_APPEND | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
                        if (redirect_out == -1)
                        {
                            perror("open");
                            exit(EXIT_FAILURE);
                        }
                        // Terminate the command before redirection
                        args[i] = NULL;
                        args[i + 1] = NULL;
                        break;
                    }
                    else
                    {
                        fprintf(stderr, "Missing filename for output redirection\n");
                        break;
                    }
                }
                else if (strcmp(args[i], "<") == 0)
                {
                    if (args[i + 1] != NULL)
                    {
                        redirect_in = open(args[i + 1], O_RDONLY);
                        if (redirect_in == -1)
                        {
                            perror("open");
                            exit(EXIT_FAILURE);
                        }
                        // Terminate the command before redirection
                        args[i] = NULL;
                        args[i + 1] = NULL;
                        i++;
                    }
                    else
                    {
                        fprintf(stderr, "Missing filename for input redirection\n");
                        break;
                    }
                }
            }

            // Execute the command
            if (redirected == 0)
            {
                if (strstr(builtin, args[0]) != NULL)
                {
                    handle_builtin(args, redirect_in, redirect_out, 0);
                }
                else
                {
                    execute_command(args, redirect_in, redirect_out, 1, background, 1, input_copy);
                }
            }

            if (redirect_in > 0)
            {
                close(redirect_in);
            }
            if (redirect_out > 1)
            {
                close(redirect_out);
            }
        }
    }

    close(saved_stdin);
    close(saved_stdout);
    update_jobs_status();

    struct Job *job = jobs_list;
    while (job != NULL)
    {
        struct Job *next = job->next;
        free(job);
        job = next;
    }

    return 0;
}
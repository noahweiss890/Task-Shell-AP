#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include "stdio.h"
#include "errno.h"
#include "stdlib.h"
#include "unistd.h"
#include <string.h>
#include <signal.h>

#define MAX_ARGS 20

void sigint_handler(int sig) {
    printf("You Typed Control-C!\n");
}

#define MAX_SIZE 30
#define MAX_STR_SIZE 1024

typedef struct {
    char data[MAX_SIZE][MAX_STR_SIZE];
    int front;
    int rear;
    int count;
} CircularBuffer;

void initialize(CircularBuffer* buffer) {
    buffer->front = 0;
    buffer->rear = -1;
    buffer->count = 0;
}

void add(CircularBuffer* buffer, const char* str) {
    if (buffer->count == MAX_SIZE) {
        buffer->front = (buffer->front + 1) % MAX_SIZE;
        buffer->count--;
    }
    buffer->rear = (buffer->rear + 1) % MAX_SIZE;
    strcpy(buffer->data[buffer->rear], str);
    buffer->count++;
}

const char* get(CircularBuffer* buffer, int index) {
    if (index >= buffer->count) {
        return NULL;
    }
    return buffer->data[(buffer->rear - index) % MAX_SIZE];
}

struct variable {
    char name[MAX_STR_SIZE];
    char value[MAX_STR_SIZE];
    int global;
    struct variable *next;
};

struct command_list {
    char command[MAX_STR_SIZE];
    struct command_list *next;
};

void add_if_command(struct command_list **head, const char *command) {
    struct command_list *new_command = malloc(sizeof(struct command_list));
    if (new_command == NULL) {
        perror("malloc");
        return;
    }
    strcpy(new_command->command, command);
    new_command->next = NULL;

    if (*head == NULL) {
        *head = new_command;
    } else {
        struct command_list *current = *head;
        while (current->next != NULL) {
            current = current->next;
        }
        current->next = new_command;
    }
}

void free_if_commands(struct command_list *head) {
    while (head != NULL) {
        struct command_list *next = head->next;
        free(head);
        head = next;
    }
}

int execute_pipeline(char ***commands, int num_commands) {
    int fd[2];
    pid_t pid;
    int status;
    int i;

    /* Create the first pipe */
    if (pipe(fd) == -1) {
        perror("pipe");
        exit(EXIT_FAILURE);
    }

    /* Fork off the first child */
    pid = fork();

    if (pid == -1) {
        perror("fork");
        exit(EXIT_FAILURE);
    } else if (pid == 0) {
        /* First child: connect stdout to the write end of the pipe */
        if (dup2(fd[1], STDOUT_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }

        /* Close unused file descriptors */
        close(fd[0]);
        close(fd[1]);

        /* Execute the first command */
        if(!strcmp(commands[0][0], "if")) {
            execvp(commands[0][1], commands[0] + 1);
        }
        else {
            execvp(commands[0][0], commands[0]);
        }
        perror("execvp");
        exit(EXIT_FAILURE);
    }
    
    /* Fork off the rest of the children */
    for (i = 1; i < num_commands - 1; i++) {
        
        /* Child: connect stdin to the read end of the previous pipe */
        if (dup2(fd[0], STDIN_FILENO) == -1) {
            perror("dup2");
            exit(EXIT_FAILURE);
        }
        
        close(fd[0]);
        close(fd[1]);
        
         /* Create the next pipe */
        if (pipe(fd) == -1) {
            perror("pipe");
            exit(EXIT_FAILURE);
        }
        
        /* Fork off a child */
        pid = fork();

        if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        } else if (pid == 0) {

            /* Child: connect stdout to the write end of the current pipe */
            if (dup2(fd[1], STDOUT_FILENO) == -1) {
                perror("dup2");
                exit(EXIT_FAILURE);
            }

            /* Close unused file descriptors */
            close(fd[0]);
            close(fd[1]);

            /* Execute the next command */
            execvp(commands[i][0], commands[i]);
            perror("execvp");
            exit(EXIT_FAILURE);
        }
    }

    /* Last child: connect stdin to the read end of the previous pipe */
    if (dup2(fd[0], STDIN_FILENO) == -1) {
        perror("dup2");
        exit(EXIT_FAILURE);
    }

    /* Close unused file descriptors */
    close(fd[0]);
    close(fd[1]);

    /* Execute the last command */
    execvp(commands[num_commands - 1][0], commands[num_commands - 1]);
    perror("execvp");
    exit(EXIT_FAILURE);
}

void add_variable (struct variable **var_list, char *name, char *value, int global) {
    struct variable *var = malloc(sizeof(struct variable));
    strcpy(var->name, name);
    strcpy(var->value, value);
    var->global = global;
    var->next = *var_list;
    *var_list = var;
}

const char* get_variable (struct variable *var_list, char *name) {
    struct variable *var = var_list;
    while (var != NULL) {
        if (!strcmp(var->name, name)) {
            return var->value;
        }
        var = var->next;
    }
    return NULL;
}

enum if_states {
    PRE_IF,
    PRE_THEN,
    IN_THEN,
    IN_ELSE,
    POST_FI
};

int main() {
    char command[1024];
    char last_command[1024] = "";
    char *token;
    char outfile[MAX_STR_SIZE];
    char infile[MAX_STR_SIZE];
    int arg_count, fd, amper, redirect_out, redirect_in, err_redirect, append_redirect, status, pid;
    char prompt[64];
    char **argv = NULL;
    char ***pipe_split = NULL;
    int pipes_count;
    CircularBuffer buffer;
    initialize(&buffer);
    int history_current = -1;
    int print_prompt = 1;
    int if_state = PRE_IF;
    int if_result = 0;
    struct command_list *if_commands = NULL;

    struct variable *var_list = NULL;
    argv = malloc(sizeof(char*) * (MAX_ARGS + 1));

    signal(SIGINT, sigint_handler);

    strcpy(prompt, "hello");

    while (1)
    {
        amper = 0;
        redirect_out = 0;
        redirect_in = 0;
        err_redirect = 0;
        append_redirect = 0;
        arg_count = 0;
        pipes_count = 0;
        outfile[0] = '\0';
        infile[0] = '\0';

        if (if_state == POST_FI) {
            if (if_commands == NULL) {
                if_state = PRE_IF;
            }
            else {
                struct command_list *current = if_commands;
                if_commands = if_commands->next;
                strcpy(command, current->command);
                free(current);
            }
        }

        if (print_prompt && if_state == PRE_IF) {
            printf("%s: ", prompt);
        }
        if (if_state != POST_FI) {
            fgets(command, 1024, stdin);
            command[strlen(command) - 1] = '\0';
        }

        if (if_state == PRE_THEN) {
            if (!strcmp(command, "then")) {
                if_state = IN_THEN;
            }
            else {
                printf("Expected 'then' after 'if'\n");
                if_state = PRE_IF;
                free_if_commands(if_commands);
            }
            continue;
        }

        if (if_state == IN_THEN) {
            if (!strcmp(command, "else")) {
                if_state = IN_ELSE;
                continue;
            }
            else if (!strcmp(command, "fi")) {
                if_state = POST_FI;
                continue;
            }
            if (!if_result) {
                add_if_command(&if_commands, command);
            }
            continue;
        }

        if (if_state == IN_ELSE) {
            if (!strcmp(command, "fi")) {
                if_state = POST_FI;
                continue;
            }
            if (if_result) {
                add_if_command(&if_commands, command);
            }
            continue;
        }

        if (!strcmp(command, "")) {
            if (history_current > -1) {
                strcpy(command, get(&buffer, history_current));
                history_current = -1;
            }
        }

        if (command[0] == '\033' && command[1] == '[') {
            if(command[2] == 'A') {
                if (buffer.rear - (history_current + 1) >= buffer.front) {
                    printf("\033[1A");//line up
                    printf("\x1b[2K");//delete line
                    history_current++;
                    print_prompt = 0;
                    printf("%s: %s", prompt, get(&buffer, history_current));
                }
                else {
                    printf("No more history UP\n");
                    print_prompt = 1;
                }
            }
            else if (command[2] == 'B') {
                if (history_current > -1) {
                    printf("\033[1A");//line up
                    printf("\x1b[2K");//delete line
                    history_current--;
                    print_prompt = 0;
                    printf("%s: %s", prompt, get(&buffer, history_current));
                }
                else {
                    printf("No more history DOWN\n");
                    print_prompt = 1;
                }
            }
            continue;
        }
        else {
            history_current = -1;
            print_prompt = 1;
            if (strcmp(command, "")) {
                add(&buffer, command);
            }
        }

        if (!strcmp(command, "!!")) {
            if (strlen(last_command) == 0) {
                continue;
            }
            strcpy(command, last_command);
        }
        else {
            strcpy(last_command, command);
        }

        if (!strcmp(command, "quit")) {
            break;
        }

        /* parse command line */
        token = strtok(command, " ");
        while (token != NULL)
        {
            argv[arg_count] = token;
            token = strtok(NULL, " ");
            arg_count++;
            if (token && ! strcmp(token, "|")) {
                pipes_count++;
            }
        }
        argv[arg_count] = NULL;

        /* Is command empty */
        if (argv[0] == NULL) {
            continue;
        }

        if (!strcmp(argv[0], "if")) {
            if (if_state != PRE_IF) {
                printf("Syntax error\n");
                if_state = PRE_IF;
                continue;
            }
            if_state = PRE_THEN;
            for (int i = 0; i < arg_count - 1; i++) {
                argv[i] = argv[i+1];
            }
            argv[arg_count - 1] = NULL;
            arg_count--;
            redirect_out = 1;
            err_redirect = 1;
            strcpy(outfile, "/dev/null");
        }

        pipe_split = malloc((pipes_count+1) * sizeof(char**));
        int j = 0;
        for (int k = 0; k < pipes_count+1; k++)
        {
            int ind = 0;
            pipe_split[k] = malloc(MAX_ARGS * sizeof(char*));
            while (j < MAX_ARGS-1 && argv[j] != NULL && strcmp(argv[j], "|") != 0) {
                pipe_split[k][ind] = argv[j];
                j++;
                ind++;
            }
            pipe_split[k][ind] = NULL;
            j++;
        }

        /* Does command line end with & */
        if (!strcmp(argv[arg_count - 1], "&"))
        {
            amper = 1;
            argv[arg_count - 1] = NULL;
            arg_count--;
        }

        if (arg_count > 1 && !strcmp(argv[arg_count - 2], ">"))
        {
            redirect_out = 1;
            argv[arg_count - 2] = NULL;
            strcpy(outfile, argv[arg_count - 1]);
        }
        else if (arg_count > 1 && !strcmp(argv[arg_count - 2], "2>"))
        {
            err_redirect = 1;
            argv[arg_count - 2] = NULL;
            strcpy(outfile, argv[arg_count - 1]);
        }
        else if (arg_count > 1 && !strcmp(argv[arg_count - 2], ">>"))
        {
            append_redirect = 1;
            argv[arg_count - 2] = NULL;
            strcpy(outfile, argv[arg_count - 1]);
        }
        else if (arg_count > 1 && !strcmp(argv[arg_count - 2], "<")) {
            redirect_in = 1;
            argv[arg_count - 2] = NULL;
            strcpy(infile, argv[arg_count - 1]);
        }

        if (arg_count == 3 && !strcmp(argv[0], "prompt") && !strcmp(argv[1], "="))
        {
            strcpy(prompt, argv[2]);
            if_result = 0;
            for (int k = 0; k < pipes_count+1; k++) {
                free(pipe_split[k]);
            }
            free(pipe_split);
            continue;
        }

        if(!strcmp(argv[0], "cd")) {
            if(argv[1] == NULL) {
                chdir(getenv("HOME"));
            } else {
                chdir(argv[1]);
            }
            if_result = 0;
            for (int k = 0; k < pipes_count+1; k++) {
                free(pipe_split[k]);
            }
            free(pipe_split);
            continue;
        }

        if(!strcmp(argv[0], "echo") && !strcmp(argv[1], "$?") && arg_count == 2) {
            printf("%d\n", status);
            if_result = 0;
            for (int k = 0; k < pipes_count+1; k++) {
                free(pipe_split[k]);
            }
            free(pipe_split);
            continue;
        }

        if(!strcmp(argv[0], "echo")) {
            if (argv[1][0] == '$') {
                const char *var = get_variable(var_list, argv[1]+1);
                if (var != NULL) {
                    printf("%s\n", var);
                    if_result = 0;
                    for (int k = 0; k < pipes_count+1; k++) {
                        free(pipe_split[k]);
                    }
                    free(pipe_split);
                    continue;
                }
            }
        }

        if (argv[0][0] == '$' && !strcmp(argv[1], "=")) {
            add_variable(&var_list, argv[0]+1, argv[2], 1);
            if_result = 0;
            for (int k = 0; k < pipes_count+1; k++) {
                free(pipe_split[k]);
            }
            free(pipe_split);
            continue;
        }

        if (!strcmp(argv[0], "read")) {
            char value[1024];
            fgets(value, 1024, stdin);
            value[strlen(value) - 1] = '\0';
            add_variable(&var_list, argv[1], value, 1);
            if_result = 0;
            for (int k = 0; k < pipes_count+1; k++) {
                free(pipe_split[k]);
            }
            free(pipe_split);
            continue;
        }
        
        /* for commands not part of the shell command language */

        pid = fork();
        
        if (pid == 0)
        {
            signal(SIGINT, SIG_DFL);

            /* redirection of IO ? */
            if (redirect_out)
            {
                fd = creat(outfile, 0660);
                close(STDOUT_FILENO);
                dup(fd);
                close(fd);
                /* stdout is now redirected */
            }
            if (err_redirect)
            {
                fd = creat(outfile, 0660);
                close(STDERR_FILENO);
                dup(fd);
                close(fd);
                /* stderr is now redirected */
            }
            if (append_redirect)
            {
                fd = open(outfile, O_WRONLY | O_APPEND | O_CREAT, 0660);
                close(STDOUT_FILENO);
                dup(fd);
                close(fd);
                /* stdout is now redirected */
            }
            if (redirect_in)
            {
                fd = open(infile, O_RDONLY, 0660);
                close(STDIN_FILENO);
                dup(fd);
                close(fd);
                /* stdin is now redirected */
            }

            // execvp(argv[0], argv);
            if (pipes_count > 0) {
                execute_pipeline(pipe_split, pipes_count + 1);
            }
            else {
                execvp(argv[0], argv);
            }
        }
        /* parent continues here */
        if (amper == 0) {
            wait(&status);
            if (if_state == PRE_THEN) {
                if_result = status;
            }
        }

        for (int k = 0; k < pipes_count+1; k++) {
            free(pipe_split[k]);
        }
        free(pipe_split);
    }
    free(argv);
    struct variable *curr = var_list;
    while (curr != NULL) {
        struct variable* next = curr->next;
        free(curr);
        curr = next;
    }
}

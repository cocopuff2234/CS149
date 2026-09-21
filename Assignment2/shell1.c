#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>

// include standard input/output library
#include <stdio.h>

// include string to use strlen()
#include <string.h>

// define maxline since we arent using apue.h
#define MAXLINE 1024

// define maxargs, the max number of words/files in a command
#define MAXARGS 100

int
main(void)
{
    char buf[MAXLINE];

    printf("%% ");	/* print prompt (printf requires %% to print %) */
    fflush(stdout);

    while (fgets(buf, MAXLINE, stdin) != NULL) {
        if (buf[strlen(buf) - 1] == '\n') {
            buf[strlen(buf) - 1] = 0; /* replace newline with null */
        }

        // store separated words
        char *args[MAXARGS];

        // initialize a count for the number of arguments typed
        int arg_count = 0;

        // strtok tokenizes the input, stored in token
        char *token = strtok(buf, " \t");

        // iterate through input that is not null
        while (token != NULL && arg_count < MAXARGS - 1) {
            // store token
            args[arg_count] = token;

            // increment count
            arg_count++;

            // get next token
            token = strtok(NULL, " \t");
        }

        // end of argument array
        args[arg_count] = NULL;

        // edge case: user only pressed Enter
        if (arg_count == 0) {
            printf("%% ");
            fflush(stdout);
            continue;
        }

        // initialize child count at 0
        int child_count = 0;

        /*
         * Normally, each filename gets one child process.
         * If the user enters only "./countnames", create one child anyway
         * so countnames can read names from standard input.
         */
        int job_count = arg_count - 1;

        if (arg_count == 1) {
            job_count = 1;
        }

        // iterate through input files and make one child per file
        for (int i = 0; i < job_count; i++) {
            // start child process
            pid_t pid = fork();

            // check if fork has failed
            if (pid < 0) {
                perror("fork failed");
            }
            else if (pid == 0) {
                // store names for this child's output/error files
                char out_name[64];
                char err_name[64];

                // use this child's PID to create names such as 12345.out
                snprintf(out_name, sizeof(out_name), "%d.out", (int)getpid());
                snprintf(err_name, sizeof(err_name), "%d.err", (int)getpid());

                // create PID.out and PID.err
                int out_fd = open(out_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                int err_fd = open(err_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                if (out_fd < 0 || err_fd < 0) {
                    perror("open failed");

                    if (out_fd >= 0) {
                        close(out_fd);
                    }

                    if (err_fd >= 0) {
                        close(err_fd);
                    }

                    _exit(1);
                }

                // make printf() write into PID.out instead of the terminal
                if (dup2(out_fd, STDOUT_FILENO) < 0) {
                    perror("dup2 stdout failed");
                    _exit(1);
                }

                // make fprintf(stderr, ...) write into PID.err
                if (dup2(err_fd, STDERR_FILENO) < 0) {
                    perror("dup2 stderr failed");
                    _exit(1);
                }

                // stdout and stderr now point to the files, so close originals
                close(out_fd);
                close(err_fd);

                /*
                 * When filenames exist, child_args becomes:
                 * "./countnames", "names1.txt", NULL
                 *
                 * With no filenames, it becomes:
                 * "./countnames", NULL
                 * so countnames reads from stdin.
                 */
                char *child_args[3];

                child_args[0] = args[0];

                if (arg_count == 1) {
                    child_args[1] = NULL;
                }
                else {
                    child_args[1] = args[i + 1];
                }

                child_args[2] = NULL;

                // replace this child process with countnames
                execvp(child_args[0], child_args);

                // execvp only returns if it failed
                perror("execvp failed");
                _exit(127);
            }
            else {
                // parent successfully created one child
                child_count++;
            }
        }

        // wait only after every child has been created
        for (int i = 0; i < child_count; i++) {
            if (wait(NULL) < 0) {
                perror("wait failed");
            }
        }

        printf("%% ");
        fflush(stdout);
    }

    return 0;
}
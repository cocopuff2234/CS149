/**
 * Description: Minimal shell for A6. For each command line it forks ONE
 * child, redirects the child's stdout/stderr to PID.out and PID.err, and
 * execs the command with all of its arguments, e.g.
 *     % ./countnames names1.txt names2.txt names3.txt
 * runs a single countnames process (which creates one THREAD per file; the
 * parallelism now lives inside countnames, not in the shell). When the
 * child has finished, the shell reports its exit status and prints the
 * contents of PID.err and PID.out.
 * Author names: Erik Thompson, Ryuto Kawabata
 * Author emails: erik.thompson@sjsu.edu, ryuto.kawabata@sjsu.edu
 * Last modified date: 11/16/2026
 * Creation date: 09/20/2026
 **/

#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// define maxline since we arent using apue.h
#define MAXLINE 1024

// define maxargs, the max number of words in a command
#define MAXARGS 100

/*
 * report_child_status - print how a finished child ended.
 */
static void
report_child_status(pid_t pid, int status)
{
    if (WIFEXITED(status)) {
        printf("child %d exited with status %d\n",
               (int)pid, WEXITSTATUS(status));
    }
    else if (WIFSIGNALED(status)) {
        printf("child %d was killed by signal %d\n",
               (int)pid, WTERMSIG(status));
    }
    else {
        printf("child %d ended abnormally\n", (int)pid);
    }
}

/*
 * print_file - copy the contents of the file `name` to the stream `out`.
 * Used to show PID.out / PID.err after the child has finished.
 */
static void
print_file(const char *name, FILE *out)
{
    FILE *in = fopen(name, "r");

    if (in == NULL) {
        return;
    }

    char buf[4096];
    size_t n;

    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        fwrite(buf, 1, n, out);
    }

    fclose(in);
}

int
main(void)
{
    char buf[MAXLINE];

    printf("%% ");  /* print prompt (printf requires %% to print %) */
    fflush(stdout);

    while (fgets(buf, MAXLINE, stdin) != NULL) {
        if (buf[strlen(buf) - 1] == '\n') {
            buf[strlen(buf) - 1] = 0; /* replace newline with null */
        }

        // split the line into words: args[0] is the command, the rest are
        // its arguments (for countnames: the input files)
        char *args[MAXARGS];
        int arg_count = 0;
        char *token = strtok(buf, " \t");

        while (token != NULL && arg_count < MAXARGS - 1) {
            args[arg_count] = token;
            arg_count++;
            token = strtok(NULL, " \t");
        }
        args[arg_count] = NULL;

        // edge case: user only pressed Enter (or typed only whitespace)
        if (arg_count == 0) {
            printf("%% ");
            fflush(stdout);
            continue;
        }

        // check the command exists before forking (same as A2)
        if (access(args[0], X_OK) != 0) {
            fprintf(stderr, "error: cannot execute %s\n", args[0]);
            printf("%% ");
            fflush(stdout);
            continue;
        }

        // make sure nothing buffered in the parent leaks into the child
        fflush(stdout);
        fflush(stderr);

        /*
         * ONE child for the whole command line. In A2-A5 the shell forked
         * one child per file; in A6 countnames itself creates one thread
         * per file, so the shell passes all the files to a single process.
         */
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            printf("%% ");
            fflush(stdout);
            continue;
        }

        if (pid == 0) {
            /* ---------- child ---------- */

            // create PID.out and PID.err (same as A2)
            char out_name[64];
            char err_name[64];
            snprintf(out_name, sizeof(out_name), "%d.out", (int)getpid());
            snprintf(err_name, sizeof(err_name), "%d.err", (int)getpid());

            int out_fd = open(out_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
            int err_fd = open(err_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);

            if (out_fd < 0 || err_fd < 0) {
                perror("open failed");
                if (out_fd >= 0) close(out_fd);
                if (err_fd >= 0) close(err_fd);
                _exit(1);
            }

            // printf() in the child now goes to PID.out, stderr to PID.err
            if (dup2(out_fd, STDOUT_FILENO) < 0) {
                perror("dup2 stdout failed");
                _exit(1);
            }
            if (dup2(err_fd, STDERR_FILENO) < 0) {
                perror("dup2 stderr failed");
                _exit(1);
            }
            close(out_fd);
            close(err_fd);

            // replace this child with the command, passing ALL arguments
            execvp(args[0], args);

            // execvp only returns if it failed (goes to PID.err)
            perror("execvp failed");
            _exit(127);
        }

        /* ---------- parent ---------- */

        // wait for the (single) child and report how it ended
        int status;
        pid_t done = waitpid(pid, &status, 0);

        if (done < 0) {
            perror("waitpid failed");
        }
        else {
            report_child_status(done, status);

            /*
             * The counts are in PID.out (and warnings/errors in PID.err).
             * Print them so the user sees the result without leaving the
             * shell; the files stay on disk as well.
             */
            char name[64];

            snprintf(name, sizeof(name), "%d.err", (int)done);
            print_file(name, stderr);
            fflush(stderr);

            snprintf(name, sizeof(name), "%d.out", (int)done);
            print_file(name, stdout);
        }

        printf("%% ");
        fflush(stdout);
    }

    return 0;
}

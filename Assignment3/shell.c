/**
 * Description: Shell that runs countnames on input files. One child process
 * is created per input file (all run in parallel), each child's stdout and
 * stderr are redirected to PID.out and PID.err, and every child sends its
 * (name, count) results back to the shell through a shared pipe. After all
 * children finish, the shell sums the counts over all files and prints the
 * totals to stdout.
 * Author names: Erik Thompson, Ryuto Kawabata
 * Author emails: erik.thompson@sjsu.edu, ryuto.kawabata@sjsu.edu
 * Last modified date: 10/03/2026
 * Creation date: 10/01/2026
 **/

#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// define maxline since we arent using apue.h
#define MAXLINE 1024

// define maxargs, the max number of words/files in a command
#define MAXARGS 100

// max length of a name; must match countnames.c
#define MAX_NAME_LEN 30

/*
 * Record received from each child through the pipe (must match countnames.c).
 * sizeof(NameCountData) is far below PIPE_BUF (4096), so one write() of a
 * record is atomic even when several children share the same pipe.
 */
typedef struct {
    char name[MAX_NAME_LEN + 1];
    int count;
} NameCountData;

/* Linked list node used by the parent to sum counts over all children */
struct Total {
    char name[MAX_NAME_LEN + 1];
    int count;
    struct Total *next;
};

/*
 * add_total - add `count` to the running total for `name`, creating a new
 * node if this is the first time we see the name.
 */
static void
add_total(struct Total **head, const char *name, int count)
{
    struct Total *node = *head;

    // look for an existing node with this name
    while (node != NULL && strcmp(node->name, name) != 0) {
        node = node->next;
    }

    if (node != NULL) {
        node->count += count;
        return;
    }

    // first time we see this name: create a node at the front of the list
    node = malloc(sizeof(struct Total));
    if (node == NULL) {
        perror("malloc failed");
        return;
    }
    strncpy(node->name, name, MAX_NAME_LEN);
    node->name[MAX_NAME_LEN] = '\0';
    node->count = count;
    node->next = *head;
    *head = node;
}

/*
 * read_record - read exactly one NameCountData from fd.
 * read() on a pipe may return fewer bytes than asked, so loop until the
 * whole struct has arrived. Returns 1 on success, 0 on EOF, -1 on error.
 */
static int
read_record(int fd, NameCountData *data)
{
    char *p = (char *)data;
    size_t got = 0;

    while (got < sizeof(*data)) {
        ssize_t n = read(fd, p + got, sizeof(*data) - got);
        if (n == 0) {
            return 0;          // EOF: every write end has been closed
        }
        if (n < 0) {
            return -1;
        }
        got += n;
    }
    return 1;
}

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

        // split the line into words
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

        // check the command exists before forking (see A2)
        if (access(args[0], X_OK) != 0) {
            fprintf(stderr, "error: cannot execute %s\n", args[0]);
            printf("%% ");
            fflush(stdout);
            continue;
        }

        // one child per filename; with no filename, one child reading stdin
        int job_count = arg_count - 1;
        if (arg_count == 1) {
            job_count = 1;
        }

        /*
         * Create ONE pipe for this command, before forking, so that every
         * child inherits it. pipe_fd[0] = read end (parent), pipe_fd[1] =
         * write end (children).
         */
        int pipe_fd[2];
        if (pipe(pipe_fd) < 0) {
            perror("pipe failed");
            printf("%% ");
            fflush(stdout);
            continue;
        }

        // the write end as a string, passed to countnames as an argument
        char pipe_arg[16];
        snprintf(pipe_arg, sizeof(pipe_arg), "%d", pipe_fd[1]);

        // count of children actually created
        int child_count = 0;

        // make sure nothing buffered in the parent leaks into the children
        fflush(stdout);
        fflush(stderr);

        // iterate through input files and make one child per file
        for (int i = 0; i < job_count; i++) {
            pid_t pid = fork();

            if (pid < 0) {
                perror("fork failed");
            }
            else if (pid == 0) {
                /* ---------- child ---------- */

                // the child only writes to the pipe, so close the read end
                close(pipe_fd[0]);

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

                /*
                 * child_args becomes:
                 *   "./countnames", "names1.txt", "<pipe write fd>", NULL
                 * or, with no filename ("-" means read stdin):
                 *   "./countnames", "-", "<pipe write fd>", NULL
                 * The pipe fd stays open across exec, so countnames can
                 * write() its results to it.
                 */
                char *child_args[4];
                child_args[0] = args[0];
                child_args[1] = (arg_count == 1) ? "-" : args[i + 1];
                child_args[2] = pipe_arg;
                child_args[3] = NULL;

                execvp(child_args[0], child_args);

                // execvp only returns if it failed (goes to PID.err)
                perror("execvp failed");
                _exit(127);
            }
            else {
                /* ---------- parent ---------- */
                // do NOT wait here: keep forking so children run in parallel
                child_count++;
            }
        }

        /*
         * The parent never writes, so close its write end NOW. Otherwise
         * read() would never see EOF, because the parent itself would still
         * count as a potential writer.
         */
        close(pipe_fd[1]);

        /*
         * Read records from the pipe until EOF (all children have exited or
         * closed their write ends) and sum the counts per name. We read
         * BEFORE waiting so that a child with many names cannot block on a
         * full pipe while the parent is stuck in wait().
         */
        struct Total *totals = NULL;
        NameCountData data;
        int r;

        while ((r = read_record(pipe_fd[0], &data)) > 0) {
            data.name[MAX_NAME_LEN] = '\0';   // defensive: ensure termination
            add_total(&totals, data.name, data.count);
        }
        if (r < 0) {
            perror("read from pipe failed");
        }
        close(pipe_fd[0]);

        /*
         * Collect every child. wait() returns whichever child finishes
         * first; it returns -1 when there are no children left.
         */
        int status;
        pid_t done;
        while ((done = wait(&status)) > 0) {
            report_child_status(done, status);
        }

        // print the combined counts over all files, then free the list
        struct Total *node = totals;
        while (node != NULL) {
            printf("%s: %d\n", node->name, node->count);
            struct Total *temp = node;
            node = node->next;
            free(temp);
        }

        printf("%% ");
        fflush(stdout);
    }

    return 0;
}

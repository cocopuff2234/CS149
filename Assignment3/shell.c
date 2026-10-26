/**
 * Description: Shell that runs countnames on input files. One child process
 * is created per input file (all run in parallel), each child's stdout and
 * stderr are redirected to PID.out and PID.err, and every child writes its
 * (name, count) results into its assigned region of shared memory. After all
 * children finish, the shell will sum the counts over all files and print
 * the totals to stdout.
 * Author names: Erik Thompson, Ryuto Kawabata
 * Author emails: erik.thompson@sjsu.edu, ryuto.kawabata@sjsu.edu
 * Last modified date: 10/03/2026
 * Creation date: 10/01/2026
 **/

#define _POSIX_C_SOURCE 200809L

#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
// new headers needed, mman gives us mmap()
#include <sys/mman.h>
#include <sys/stat.h>

// define maxline since we arent using apue.h
#define MAXLINE 1024

// define maxargs, the max number of words/files in a command
#define MAXARGS 100

// max length of a name; must match countnames.c
#define MAX_NAME_LEN 30

// max number of names in a region of memory
#define MAX_NAMES 1000

// name for shared memory object
#define SHM_NAME "/countnames_shared_memory"

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
         * Shared memory: each child gets a region, and final is a sum of all
         * of them.
         */
        size_t region_size = MAX_NAMES * sizeof(NameCountData);
        size_t total_size = (job_count + 1) * region_size;
        size_t summation_offset = job_count * region_size;

        /*
         * Shared memory: create shared memory object.
         */
        int shm_fd = shm_open(
            SHM_NAME,
            O_CREAT | O_RDWR,
            0600
        );

        if (shm_fd == -1) {
            perror("shm_open failed");
            printf("%% ");
            fflush(stdout);
            continue;
        }

        // set size of shared memory object
        if (ftruncate(shm_fd, total_size) == -1) {
            perror("ftruncate failed");
            close(shm_fd);
            shm_unlink(SHM_NAME);
            printf("%% ");
            fflush(stdout);
            continue;
        }

        // map shared memory object to shell address space
        void *GLOBAL = mmap(
            NULL,
            total_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            shm_fd,
            0
        );

        if (GLOBAL == MAP_FAILED) {
            perror("mmap failed");
            close(shm_fd);
            shm_unlink(SHM_NAME);
            printf("%% ");
            fflush(stdout);
            continue;
        }

        /*
         * Make sure shm_fd remains open after execvp().
         */
        int fd_flags = fcntl(shm_fd, F_GETFD);

        if (fd_flags == -1) {
            perror("fcntl F_GETFD failed");
            munmap(GLOBAL, total_size);
            close(shm_fd);
            shm_unlink(SHM_NAME);
            printf("%% ");
            fflush(stdout);
            continue;
        }

        if (fcntl(shm_fd, F_SETFD, fd_flags & ~FD_CLOEXEC) == -1) {
            perror("fcntl F_SETFD failed");
            munmap(GLOBAL, total_size);
            close(shm_fd);
            shm_unlink(SHM_NAME);
            printf("%% ");
            fflush(stdout);
            continue;
        }

        // initialize NameCountData to empty
        NameCountData *global_data = (NameCountData *)GLOBAL;

        for (size_t i = 0;
             i < total_size / sizeof(NameCountData);
             i++) {
            global_data[i].name[0] = '\0';
            global_data[i].count = 0;
        }

        /*
         * Convert shared-memory information into strings.
         */
        char shm_fd_arg[32];
        char total_size_arg[32];
        char max_names_arg[32];

        snprintf(shm_fd_arg,
                 sizeof(shm_fd_arg),
                 "%d",
                 shm_fd);

        snprintf(total_size_arg,
                 sizeof(total_size_arg),
                 "%zu",
                 total_size);

        snprintf(max_names_arg,
                 sizeof(max_names_arg),
                 "%d",
                 MAX_NAMES);

        // count of children actually created
        int child_count = 0;

        // make sure nothing buffered in the parent leaks into the children
        fflush(stdout);
        fflush(stderr);

        // iterate through input files and make one child per file
        for (int i = 0; i < job_count; i++) {
            /*
             * Each child receives its own region inside GLOBAL.
             */
            size_t child_offset = i * region_size;

            char child_offset_arg[32];

            snprintf(child_offset_arg,
                     sizeof(child_offset_arg),
                     "%zu",
                     child_offset);

            pid_t pid = fork();

            if (pid < 0) {
                perror("fork failed");
            }
            else if (pid == 0) {
                /* ---------- child ---------- */

                // create PID.out and PID.err (same as A2)
                char out_name[64];
                char err_name[64];

                snprintf(out_name,
                         sizeof(out_name),
                         "%d.out",
                         (int)getpid());

                snprintf(err_name,
                         sizeof(err_name),
                         "%d.err",
                         (int)getpid());

                int out_fd = open(
                    out_name,
                    O_WRONLY | O_CREAT | O_TRUNC,
                    0644
                );

                int err_fd = open(
                    err_name,
                    O_WRONLY | O_CREAT | O_TRUNC,
                    0644
                );

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
                 * countnames arguments:
                 *
                 * countnames input_file shm_fd total_size offset max_names
                 */
                char *child_args[7];

                child_args[0] = args[0];
                child_args[1] = (arg_count == 1) ? "-" : args[i + 1];
                child_args[2] = shm_fd_arg;
                child_args[3] = total_size_arg;
                child_args[4] = child_offset_arg;
                child_args[5] = max_names_arg;
                child_args[6] = NULL;

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
         * Collect every child. wait() returns whichever child finishes
         * first.
         */
        int status;
        pid_t done;
        int waited = 0;

        while (waited < child_count &&
               (done = wait(&status)) > 0) {
            report_child_status(done, status);
            waited++;
        }

        /*
         * Aggregate all child regions into the summation region.
         */
        NameCountData *summation =
            (NameCountData *)((char *)GLOBAL + summation_offset);

        int summation_count = 0;

        for (int child = 0; child < job_count; child++) {
            NameCountData *child_data =
                (NameCountData *)((char *)GLOBAL +
                                  child * region_size);

            /*
             * Read this child's records until an empty name is found.
             */
            for (int i = 0; i < MAX_NAMES; i++) {
                if (child_data[i].name[0] == '\0') {
                    break;
                }

                int found = 0;

                /*
                 * Search for this name in the summation region.
                 */
                for (int j = 0; j < summation_count; j++) {
                    if (strcmp(summation[j].name,
                               child_data[i].name) == 0) {
                        summation[j].count += child_data[i].count;
                        found = 1;
                        break;
                    }
                }

                /*
                 * If the name was not found, add a new record.
                 */
                if (!found) {
                    if (summation_count >= MAX_NAMES - 1) {
                        fprintf(stderr,
                                "error: summation region is full\n");
                        break;
                    }

                    memset(&summation[summation_count],
                           0,
                           sizeof(NameCountData));

                    strncpy(summation[summation_count].name,
                            child_data[i].name,
                            MAX_NAME_LEN);

                    summation[summation_count]
                        .name[MAX_NAME_LEN] = '\0';

                    summation[summation_count].count =
                        child_data[i].count;

                    summation_count++;
                }
            }
        }

        /*
         * Mark the end of the summation records.
         */
        summation[summation_count].name[0] = '\0';
        summation[summation_count].count = 0;

        /*
         * Print the final combined results.
         */
        for (int i = 0; i < summation_count; i++) {
            printf("%s: %d\n",
                   summation[i].name,
                   summation[i].count);
        }

        /*
         * Clean up the shared-memory mapping after all children finish.
         */
        munmap(GLOBAL, total_size);
        close(shm_fd);
        shm_unlink(SHM_NAME);

        printf("%% ");
        fflush(stdout);
    }

    return 0;
}

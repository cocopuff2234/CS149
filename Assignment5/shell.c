/**
 * Description: Shell that runs countnames on input files. One child process
 * is created per input file (all run in parallel), each child's stdout and
 * stderr are redirected to PID.out and PID.err, and each child writes its
 * (name, count) results into its own region of a shared-memory object.
 * After all children finish, the shell sums every region into the final
 * summation region and prints the totals to stdout.
 * Author names: Erik Thompson, Ryuto Kawabata
 * Author emails: erik.thompson@sjsu.edu, ryuto.kawabata@sjsu.edu
 * Last modified date: 10/26/2026
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
// mman gives us shm_open() / mmap()
#include <sys/mman.h>
#include <sys/stat.h>

// define maxline since we arent using apue.h
#define MAXLINE 1024

// define maxargs, the max number of words/files in a command
#define MAXARGS 100

// max length of a name; must match countnames.c
#define MAX_NAME_LEN 30

// max number of records in one region of shared memory
#define MAX_NAMES 1000

// shared memory object name; the PID keeps two shells from colliding
#define SHM_NAME_FMT "/countnames_shm_%d"

/*
 * Record stored in shared memory (must match countnames.c).
 * The shared object holds (job_count + 1) regions of MAX_NAMES records:
 * region i belongs to child i, the last region is the summation. A region
 * ends at the first record whose name is empty.
 */
typedef struct {
    char name[MAX_NAME_LEN + 1];
    int count;
} NameCountData;

/*
 * add_total - add `count` to the record for `name` in the summation region,
 * inserting a new record if the name is not there yet. The last slot is
 * always kept empty as the terminator.
 */
static void
add_total(NameCountData *summation, const char *name, int count)
{
    int i = 0;

    while (i < MAX_NAMES - 1 && summation[i].name[0] != '\0') {
        if (strcmp(summation[i].name, name) == 0) {
            summation[i].count += count;
            return;
        }

        i++;
    }

    if (i == MAX_NAMES - 1) {
        fprintf(stderr, "error: summation region is full\n");
        return;
    }

    strncpy(summation[i].name, name, MAX_NAME_LEN);
    summation[i].name[MAX_NAME_LEN] = '\0';
    summation[i].count = count;
    summation[i + 1].name[0] = '\0';
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
    char shm_name[64];

    snprintf(shm_name, sizeof(shm_name), SHM_NAME_FMT, (int)getpid());

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
         * Shared memory layout: one region per child plus one summation
         * region at the end.
         */
        size_t region_size = MAX_NAMES * sizeof(NameCountData);
        size_t total_size = (job_count + 1) * region_size;

        // create a fresh shared memory object (drop any stale one first)
        shm_unlink(shm_name);

        int shm_fd = shm_open(shm_name, O_CREAT | O_EXCL | O_RDWR, 0600);

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
            shm_unlink(shm_name);
            printf("%% ");
            fflush(stdout);
            continue;
        }

        // map shared memory object into the shell address space
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
            shm_unlink(shm_name);
            printf("%% ");
            fflush(stdout);
            continue;
        }

        /*
         * shm_open() sets FD_CLOEXEC; clear it so the descriptor survives
         * execvp() and the children can mmap() the same object.
         */
        int fd_flags = fcntl(shm_fd, F_GETFD);

        if (fd_flags == -1 ||
            fcntl(shm_fd, F_SETFD, fd_flags & ~FD_CLOEXEC) == -1) {
            perror("fcntl failed");
            munmap(GLOBAL, total_size);
            close(shm_fd);
            shm_unlink(shm_name);
            printf("%% ");
            fflush(stdout);
            continue;
        }

        // every region starts empty (terminator in slot 0)
        NameCountData *global_data = (NameCountData *)GLOBAL;

        for (size_t i = 0; i < total_size / sizeof(NameCountData); i++) {
            global_data[i].name[0] = '\0';
            global_data[i].count = 0;
        }

        /*
         * Convert shared-memory information into strings for execvp().
         */
        char shm_fd_arg[32];
        char total_size_arg[32];
        char max_names_arg[32];

        snprintf(shm_fd_arg, sizeof(shm_fd_arg), "%d", shm_fd);
        snprintf(total_size_arg, sizeof(total_size_arg), "%zu", total_size);
        snprintf(max_names_arg, sizeof(max_names_arg), "%d", MAX_NAMES);

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

                // create PID.out and PID.err (same as A2)
                char out_name[64];
                char err_name[64];

                snprintf(out_name, sizeof(out_name), "%d.out", (int)getpid());
                snprintf(err_name, sizeof(err_name), "%d.err", (int)getpid());

                int out_fd = open(out_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                int err_fd = open(err_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);

                if (out_fd < 0 || err_fd < 0) {
                    perror("open failed");
                    _exit(1);
                }

                // printf() in the child now goes to PID.out, stderr to PID.err
                if (dup2(out_fd, STDOUT_FILENO) < 0 ||
                    dup2(err_fd, STDERR_FILENO) < 0) {
                    perror("dup2 failed");
                    _exit(1);
                }

                close(out_fd);
                close(err_fd);

                /*
                 * countnames arguments:
                 *
                 * countnames input_file shm_fd total_size region_index
                 * max_names
                 */
                char region_index_arg[32];

                snprintf(region_index_arg, sizeof(region_index_arg), "%d", i);

                char *child_args[7];

                child_args[0] = args[0];
                child_args[1] = (arg_count == 1) ? "-" : args[i + 1];
                child_args[2] = shm_fd_arg;
                child_args[3] = total_size_arg;
                child_args[4] = region_index_arg;
                child_args[5] = max_names_arg;
                child_args[6] = NULL;

                execvp(child_args[0], child_args);

                // execvp only returns if it failed (goes to PID.err)
                perror("execvp failed");
                _exit(127);
            }

            /* ---------- parent: do NOT wait, keep forking ---------- */
        }

        /*
         * Collect every child. wait() returns whichever child finishes
         * first and -1 once there are no children left.
         */
        int status;
        pid_t done;

        while ((done = wait(&status)) > 0) {
            report_child_status(done, status);
        }

        /*
         * All children are done: add every child's region into the
         * summation region (the last one).
         */
        NameCountData *summation = global_data + (size_t)job_count * MAX_NAMES;

        for (int i = 0; i < job_count; i++) {
            NameCountData *region = global_data + (size_t)i * MAX_NAMES;

            for (int j = 0; j < MAX_NAMES && region[j].name[0] != '\0'; j++) {
                add_total(summation, region[j].name, region[j].count);
            }
        }

        // print the final combined results
        for (int i = 0; i < MAX_NAMES && summation[i].name[0] != '\0'; i++) {
            printf("%s: %d\n", summation[i].name, summation[i].count);
        }

        // clean up the shared-memory mapping and object
        munmap(GLOBAL, total_size);
        close(shm_fd);
        shm_unlink(shm_name);

        printf("%% ");
        fflush(stdout);
    }

    return 0;
}

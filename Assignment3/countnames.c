/**
 * Description: Counts the occurrences of each name in a file using a hash
 * table. For Assignment 3, when a pipe file descriptor is given as the
 * second argument, each (name, count) pair is also sent to the parent shell
 * through the pipe as a NameCountData struct.
 * Author names: Erik Thompson, Ryuto Kawabata
 * Author emails: erik.thompson@sjsu.edu, ryuto.kawabata@sjsu.edu
 * Last modified date: 10/03/2026
 * Creation date: 10/01/2026
 **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/*** Define table size (a prime number spreads hash values more evenly) ***/
#define TABLE_SIZE 101

/*** Max length of a name (chars), and the size of the line read buffer. ***/
#define MAX_NAME_LEN 30
#define LINE_BUF_LEN 256

/*** Record sent to the parent through the pipe (must match shell.c) ***/
typedef struct {
    char name[MAX_NAME_LEN + 1];   /* +1 for the terminating '\0' */
    int count;
} NameCountData;

/*** Define hash table entry, track name, count, and next pointer ***/
struct Entry {
    char name[MAX_NAME_LEN + 1];
    int count;
    struct Entry *next;            /* next entry in the same bucket (chaining) */
};

/* Hash function. Converts name into hash table index */
unsigned int hash(const char *name) {
    unsigned int value = 0;
    for (int i = 0; name[i] != '\0'; i++) {
        value = (value * 31) + (unsigned char) name[i];
    }
    return value % TABLE_SIZE;
}

/*
 * Usage:
 *   ./countnames                 read names from stdin, print counts
 *   ./countnames FILE            read names from FILE, print counts
 *   ./countnames FILE PIPE_FD    same, and also write one NameCountData per
 *                                name to file descriptor PIPE_FD ("-" as FILE
 *                                means stdin)
 */
int main(int argc, char *argv[]) {
    FILE *file = stdin;   /* default: read names from stdin */
    int pipe_fd = -1;     /* -1 means "no pipe, print only" */

    if (argc > 3) {
        return 1;
    }

    /* Optional 2nd argument: write end of the pipe to the parent shell */
    if (argc == 3) {
        pipe_fd = atoi(argv[2]);
    }

    /* Optional 1st argument: input file ("-" keeps stdin) */
    if (argc >= 2 && strcmp(argv[1], "-") != 0) {
        file = fopen(argv[1], "r");
        if (file == NULL) {
            /* Same message/stream as the sample code in the assignment. */
            printf("error: cannot open file\n");
            return 1;
        }
    }

    /* Start empty */
    struct Entry *table[TABLE_SIZE] = {NULL};
    char line[LINE_BUF_LEN] = {0};
    int line_number = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        line_number++;

        /* Replace the trailing newline with NULL; it is not part of the name */
        line[strcspn(line, "\n")] = '\0';

        /* Empty line: warn on stderr and do not count it */
        if (strlen(line) == 0) {
            fprintf(stderr, "Warning - Line %d is empty.\n", line_number);
            continue;
        }

        /* Look for the name in its bucket */
        unsigned int index = hash(line);
        struct Entry *entry = table[index];
        while (entry != NULL && strcmp(entry->name, line) != 0) {
            entry = entry->next;
        }

        /* If entry, or name, is found, increment its count */
        if (entry != NULL) {
            entry->count++;
        } else {
            /* New name: allocate an entry and push it on the front of the bucket */
            struct Entry *new_entry = malloc(sizeof(struct Entry));
            if (new_entry == NULL) {
                if (file != stdin) {
                    fclose(file);
                }
                return 1;
            }
            strncpy(new_entry->name, line, MAX_NAME_LEN);
            new_entry->name[MAX_NAME_LEN] = '\0';
            new_entry->count = 1;
            new_entry->next = table[index];
            table[index] = new_entry;
        }
    }

    /* Print every entry (to stdout, i.e. PID.out when run from the shell),
     * send it to the parent through the pipe if we have one, and free it */
    for (int i = 0; i < TABLE_SIZE; i++) {
        struct Entry *entry = table[i];
        while (entry != NULL) {
            printf("%s: %d\n", entry->name, entry->count);

            if (pipe_fd >= 0) {
                NameCountData data;
                memset(&data, 0, sizeof(data));          /* no garbage bytes */
                strncpy(data.name, entry->name, MAX_NAME_LEN);
                data.count = entry->count;

                /* sizeof(data) < PIPE_BUF, so this write is atomic: records
                 * from different children never interleave in the pipe */
                if (write(pipe_fd, &data, sizeof(data)) != sizeof(data)) {
                    fprintf(stderr, "error: write to pipe failed\n");
                }
            }

            struct Entry *temp = entry;
            entry = entry->next;
            free(temp);
        }
    }

    if (pipe_fd >= 0) {
        close(pipe_fd);
    }
    if (file != stdin) {
        fclose(file);
    }
    return 0;
}

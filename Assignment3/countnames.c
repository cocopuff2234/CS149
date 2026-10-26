/**
 * Description: Counts the occurrences of each name in a file using a hash
 * table. Each child writes its (name, count) results into its assigned
 * region of shared memory.
 * Author names: Erik Thompson, Ryuto Kawabata
 * Author emails: erik.thompson@sjsu.edu, ryuto.kawabata@sjsu.edu
 * Last modified date: 10/03/2026
 * Creation date: 10/01/2026
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

/*** Define table size (a prime number spreads hash values more evenly) ***/
#define TABLE_SIZE 101

/*** Max length of a name (chars), and the size of the line read buffer. ***/
#define MAX_NAME_LEN 30
#define LINE_BUF_LEN 256

/*** Maximum number of records in one shared-memory region ***/
#define MAX_NAMES 1000

/*** Record stored in shared memory; must match shell.c ***/
typedef struct {
    char name[MAX_NAME_LEN + 1];   /* +1 for the terminating '\0' */
    int count;
} NameCountData;

/*** Define hash table entry, track name, count, and next pointer ***/
struct Entry {
    char name[MAX_NAME_LEN + 1];
    int count;
    struct Entry *next;            /* next entry in the same bucket */
};

/* Hash function. Converts name into hash table index */
unsigned int
hash(const char *name)
{
    unsigned int value = 0;

    for (int i = 0; name[i] != '\0'; i++) {
        value = (value * 31) + (unsigned char)name[i];
    }

    return value % TABLE_SIZE;
}

/*
 * Usage:
 *   ./countnames                 read names from stdin, print counts
 *   ./countnames FILE            read names from FILE, print counts
 *
 * When started by shell.c:
 *
 *   ./countnames FILE SHM_FD TOTAL_SIZE OFFSET MAX_NAMES
 *
 * The child reconnects to shared memory and writes its results into the
 * region beginning at OFFSET.
 */
int
main(int argc, char *argv[])
{
    FILE *file = stdin;

    int shared_mode = 0;
    int shm_fd = -1;
    size_t total_size = 0;
    size_t child_offset = 0;
    int max_names = 0;

    void *GLOBAL = NULL;
    NameCountData *shared_data = NULL;

    /*
     * Standalone usage supports zero or one argument.
     * Shared-memory usage receives five additional arguments.
     */
    if (argc != 1 && argc != 2 && argc != 6) {
        fprintf(stderr,
                "Usage: %s [FILE]\n"
                "   or: %s FILE SHM_FD TOTAL_SIZE OFFSET MAX_NAMES\n",
                argv[0],
                argv[0]);
        return 1;
    }

    /*
     * Shared-memory arguments:
     *
     * argv[1] = input filename
     * argv[2] = shared-memory file descriptor
     * argv[3] = total shared-memory size
     * argv[4] = child region offset
     * argv[5] = maximum number of records
     */
    if (argc == 6) {
        shared_mode = 1;

        shm_fd = atoi(argv[2]);
        total_size = strtoul(argv[3], NULL, 10);
        child_offset = strtoul(argv[4], NULL, 10);
        max_names = atoi(argv[5]);

        if (shm_fd < 0 ||
            total_size == 0 ||
            max_names <= 0 ||
            child_offset >= total_size) {
            fprintf(stderr, "Invalid shared-memory arguments\n");
            return 1;
        }

        /*
         * Reconnect to the shared-memory object after execvp().
         */
        GLOBAL = mmap(
            NULL,
            total_size,
            PROT_READ | PROT_WRITE,
            MAP_SHARED,
            shm_fd,
            0
        );

        if (GLOBAL == MAP_FAILED) {
            perror("mmap failed");
            return 1;
        }

        /*
         * Find this child's assigned region inside GLOBAL.
         */
        shared_data =
            (NameCountData *)((char *)GLOBAL + child_offset);
    }

    /* Optional input file: "-" means stdin */
    if (argc >= 2 && strcmp(argv[1], "-") != 0) {
        file = fopen(argv[1], "r");

        if (file == NULL) {
            printf("error: cannot open file\n");

            if (shared_mode) {
                munmap(GLOBAL, total_size);
                close(shm_fd);
            }

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
            fprintf(stderr,
                    "Warning - Line %d is empty.\n",
                    line_number);
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
        }
        else {
            /* New name: allocate an entry and push it on the front of the bucket */
            struct Entry *new_entry =
                malloc(sizeof(struct Entry));

            if (new_entry == NULL) {
                perror("malloc failed");

                if (file != stdin) {
                    fclose(file);
                }

                if (shared_mode) {
                    munmap(GLOBAL, total_size);
                    close(shm_fd);
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

    /*
     * Print every entry to stdout and write every result into this child's
     * assigned shared-memory region.
     */
    int shared_index = 0;

    for (int i = 0; i < TABLE_SIZE; i++) {
        struct Entry *entry = table[i];

        while (entry != NULL) {
            printf("%s: %d\n", entry->name, entry->count);

            if (shared_mode) {
                /*
                 * Leave one slot available for the terminating empty record.
                 */
                if (shared_index >= max_names - 1) {
                    fprintf(stderr,
                            "error: too many unique names for shared region\n");

                    struct Entry *temp = entry;
                    entry = entry->next;
                    free(temp);

                    for (int j = i; j < TABLE_SIZE; j++) {
                        struct Entry *remaining = table[j];

                        while (remaining != NULL) {
                            struct Entry *next = remaining->next;
                            free(remaining);
                            remaining = next;
                        }
                    }

                    fclose(file);
                    munmap(GLOBAL, total_size);
                    close(shm_fd);
                    return 1;
                }

                NameCountData *record =
                    &shared_data[shared_index];

                memset(record, 0, sizeof(NameCountData));
                strncpy(record->name,
                        entry->name,
                        MAX_NAME_LEN);
                record->name[MAX_NAME_LEN] = '\0';
                record->count = entry->count;

                shared_index++;
            }

            struct Entry *temp = entry;
            entry = entry->next;
            free(temp);
        }
    }

    /*
     * A name with length zero marks the end of valid data in this child's
     * shared-memory region.
     */
    if (shared_mode) {
        shared_data[shared_index].name[0] = '\0';
        shared_data[shared_index].count = 0;
    }

    if (file != stdin) {
        fclose(file);
    }

    if (shared_mode) {
        munmap(GLOBAL, total_size);
        close(shm_fd);
    }

    return 0;
}

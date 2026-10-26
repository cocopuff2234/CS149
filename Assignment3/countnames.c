/**
 * Description: Counts the occurrences of each name in a file using a hash
 * table. Each child directly updates the shared summation region.
 * Author names: Erik Thompson, Ryuto Kawabata
 * Author emails: erik.thompson@sjsu.edu, ryuto.kawabata@sjsu.edu
 * Last modified date: 10/26/2026
 * Creation date: 10/01/2026
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>

/*** Define table size (a prime number spreads hash values more evenly) ***/
#define TABLE_SIZE 101

/*** Max length of a name (chars), and the size of the line read buffer. ***/
#define MAX_NAME_LEN 30
#define LINE_BUF_LEN 256

/*** Maximum number of records in one shared-memory region ***/
#define MAX_NAMES 1000

/*** Record stored in shared memory; must match shell.c ***/
typedef struct {
    char name[MAX_NAME_LEN + 1];
    int count;
} NameCountData;

/*** Define hash table entry, track name, count, and next pointer ***/
struct Entry {
    char name[MAX_NAME_LEN + 1];
    int count;
    struct Entry *next;
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
 * Free every entry in the hash table.
 */
static void
free_table(struct Entry *table[])
{
    for (int i = 0; i < TABLE_SIZE; i++) {
        struct Entry *entry = table[i];

        while (entry != NULL) {
            struct Entry *next = entry->next;
            free(entry);
            entry = next;
        }
    }
}

/*
 * Usage:
 *   ./countnames                 read names from stdin, print counts
 *   ./countnames FILE            read names from FILE, print counts
 *
 * When started by shell.c:
 *
 *   ./countnames FILE SHM_FD TOTAL_SIZE SUMMATION_OFFSET MAX_NAMES SEM_NAME
 */
int
main(int argc, char *argv[])
{
    FILE *file = stdin;

    int shared_mode = 0;
    int shm_fd = -1;
    size_t total_size = 0;
    size_t summation_offset = 0;
    int max_names = 0;

    void *GLOBAL = NULL;
    NameCountData *summation = NULL;
    sem_t *lock = NULL;

    /*
     * Standalone usage supports zero or one argument.
     * Shared-memory usage receives six additional arguments.
     */
    if (argc != 1 && argc != 2 && argc != 7) {
        fprintf(stderr,
                "Usage: %s [FILE]\n"
                "   or: %s FILE SHM_FD TOTAL_SIZE "
                "SUMMATION_OFFSET MAX_NAMES SEM_NAME\n",
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
     * argv[4] = summation region offset
     * argv[5] = maximum number of records
     * argv[6] = semaphore name
     */
    if (argc == 7) {
        shared_mode = 1;

        shm_fd = atoi(argv[2]);
        total_size = strtoul(argv[3], NULL, 10);
        summation_offset = strtoul(argv[4], NULL, 10);
        max_names = atoi(argv[5]);

        if (shm_fd < 0 ||
            total_size == 0 ||
            max_names <= 0 ||
            summation_offset >= total_size) {
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
         * Find the shared summation region inside GLOBAL.
         */
        summation =
            (NameCountData *)((char *)GLOBAL + summation_offset);

        /*
         * Open the semaphore created by shell.c.
         */
        lock = sem_open(argv[6], 0);

        if (lock == SEM_FAILED) {
            perror("sem_open failed");
            munmap(GLOBAL, total_size);
            close(shm_fd);
            return 1;
        }
    }

    /* Optional input file: "-" means stdin */
    if (argc >= 2 && strcmp(argv[1], "-") != 0) {
        file = fopen(argv[1], "r");

        if (file == NULL) {
            printf("error: cannot open file\n");

            if (shared_mode) {
                sem_close(lock);
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

        /* Replace the trailing newline with NULL */
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

        while (entry != NULL &&
               strcmp(entry->name, line) != 0) {
            entry = entry->next;
        }

        /* If the name is found, increment its count */
        if (entry != NULL) {
            entry->count++;
        }
        else {
            /* New name: allocate an entry and add it to the bucket */
            struct Entry *new_entry =
                malloc(sizeof(struct Entry));

            if (new_entry == NULL) {
                perror("malloc failed");

                if (file != stdin) {
                    fclose(file);
                }

                free_table(table);

                if (shared_mode) {
                    sem_close(lock);
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
     * Print every entry to stdout and add every result directly to the
     * shared summation region.
     */
    int shared_error = 0;

    for (int i = 0; i < TABLE_SIZE && !shared_error; i++) {
        struct Entry *entry = table[i];

        while (entry != NULL) {
            printf("%s: %d\n",
                   entry->name,
                   entry->count);

            if (shared_mode) {
                /*
                 * Only one child may search or modify the summation region
                 * at a time.
                 */
                if (sem_wait(lock) == -1) {
                    perror("sem_wait failed");
                    shared_error = 1;
                    break;
                }

                int updated = 0;

                /*
                 * Search for an existing matching name.
                 */
                for (int j = 0; j < max_names - 1; j++) {
                    if (summation[j].name[0] == '\0') {
                        break;
                    }

                    if (strcmp(summation[j].name,
                               entry->name) == 0) {
                        summation[j].count += entry->count;
                        updated = 1;
                        break;
                    }
                }

                /*
                 * If the name was not found, insert it into the first
                 * available slot.
                 */
                if (!updated) {
                    int inserted = 0;

                    for (int j = 0; j < max_names - 1; j++) {
                        if (summation[j].name[0] == '\0') {
                            strncpy(summation[j].name,
                                    entry->name,
                                    MAX_NAME_LEN);

                            summation[j].name[MAX_NAME_LEN] = '\0';
                            summation[j].count = entry->count;

                            inserted = 1;
                            break;
                        }
                    }

                    if (!inserted) {
                        fprintf(stderr,
                                "error: summation region is full\n");
                        shared_error = 1;
                    }
                }

                /*
                 * Allow another child to access the summation region.
                 */
                if (sem_post(lock) == -1) {
                    perror("sem_post failed");
                    shared_error = 1;
                }
            }

            entry = entry->next;
        }
    }

    if (file != stdin) {
        fclose(file);
    }

    free_table(table);

    if (shared_mode) {
        sem_close(lock);
        munmap(GLOBAL, total_size);
        close(shm_fd);
    }

    return shared_error ? 1 : 0;
}

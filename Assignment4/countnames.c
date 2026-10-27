/**
 * Description: Counts the occurrences of each name in a file using a hash
 * table. When started by shell.c, each child also writes its results into
 * its own region of a shared-memory object; the shell sums the regions.
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

/*** Define table size (a prime number spreads hash values more evenly) ***/
#define TABLE_SIZE 101

/*** Max length of a name (chars), and the size of the line read buffer. ***/
#define MAX_NAME_LEN 30
#define LINE_BUF_LEN 256

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
 *   ./countnames FILE SHM_FD TOTAL_SIZE REGION_INDEX MAX_NAMES
 */
int
main(int argc, char *argv[])
{
    FILE *file = stdin;

    int shared_mode = 0;
    int shm_fd = -1;
    size_t total_size = 0;
    int region_index = 0;
    int max_names = 0;

    void *GLOBAL = NULL;
    NameCountData *region = NULL;

    /*
     * Standalone usage supports zero or one argument.
     * Shared-memory usage receives four additional arguments.
     */
    if (argc != 1 && argc != 2 && argc != 6) {
        fprintf(stderr,
                "Usage: %s [FILE]\n"
                "   or: %s FILE SHM_FD TOTAL_SIZE REGION_INDEX MAX_NAMES\n",
                argv[0],
                argv[0]);
        return 1;
    }

    /*
     * Shared-memory arguments:
     *
     * argv[1] = input filename ("-" means stdin)
     * argv[2] = shared-memory file descriptor
     * argv[3] = total shared-memory size
     * argv[4] = index of this child's region
     * argv[5] = maximum number of records per region
     */
    if (argc == 6) {
        shared_mode = 1;

        shm_fd = atoi(argv[2]);
        total_size = strtoul(argv[3], NULL, 10);
        region_index = atoi(argv[4]);
        max_names = atoi(argv[5]);

        size_t region_size = (size_t)max_names * sizeof(NameCountData);

        /*
         * The region must lie inside the object and must not be the last
         * region, which is reserved for the summation.
         */
        if (shm_fd < 0 ||
            total_size == 0 ||
            max_names <= 0 ||
            region_index < 0 ||
            ((size_t)region_index + 2) * region_size > total_size) {
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

        // this child's own region inside GLOBAL
        region = (NameCountData *)GLOBAL + (size_t)region_index * max_names;
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
     * Print every entry to stdout and, in shared mode, copy it into this
     * child's own region. The last slot is kept free for the terminator.
     */
    int n = 0;
    int region_full = 0;

    for (int i = 0; i < TABLE_SIZE; i++) {
        struct Entry *entry = table[i];

        while (entry != NULL) {
            printf("%s: %d\n",
                   entry->name,
                   entry->count);

            if (shared_mode) {
                if (n < max_names - 1) {
                    strncpy(region[n].name, entry->name, MAX_NAME_LEN);
                    region[n].name[MAX_NAME_LEN] = '\0';
                    region[n].count = entry->count;
                    n++;
                }
                else {
                    region_full = 1;
                }
            }

            entry = entry->next;
        }
    }

    if (shared_mode) {
        // terminate this child's region
        region[n].name[0] = '\0';

        if (region_full) {
            fprintf(stderr, "error: shared-memory region is full\n");
        }
    }

    if (file != stdin) {
        fclose(file);
    }

    free_table(table);

    if (shared_mode) {
        munmap(GLOBAL, total_size);
        close(shm_fd);
    }

    return region_full ? 1 : 0;
}

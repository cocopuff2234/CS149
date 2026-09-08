/**
 * Description: This program counts the occurrences of each name in a file using a hash table.
 * Author names: Erik Thompson, Ryuto Kawabata
 * Author emails: erik.thompson@sjsu.edu, ryuto.kawabata@sjsu.edu
 * Last modified date: 09/08/2026
 * Creation date: 09/07/2026
 **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*** Define table size (a prime number spreads hash values more evenly) ***/
#define TABLE_SIZE 101

/*** Max length of a name (chars), and the size of the line read buffer.
 *   The line buffer is much larger than a name so that a line that is
 *   longer than expected is still read in one fgets() call, instead of
 *   being split into two "lines". ***/
#define MAX_NAME_LEN 30
#define LINE_BUF_LEN 256

/*** Define hash table entry, track name, count, and next pointer ***/
struct Entry {
    char name[MAX_NAME_LEN + 1];   /* +1 for the terminating '\0' */
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

int main(int argc, char *argv[]) {
    FILE *file = stdin;   /* default: read names from stdin */

    if (argc == 2) {
        file = fopen(argv[1], "r");
        if (file == NULL) {
            /* Same message/stream as the sample code in the assignment. */
            printf("error: cannot open file\n");
            return 1;
        }
    } else if (argc > 2) {
        return 1;
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
            /* Copy at most MAX_NAME_LEN chars and always terminate the string */
            strncpy(new_entry->name, line, MAX_NAME_LEN);
            new_entry->name[MAX_NAME_LEN] = '\0';
            new_entry->count = 1;
            new_entry->next = table[index];
            table[index] = new_entry;
        }
    }

    /* Print every entry and free it as we go */
    for (int i = 0; i < TABLE_SIZE; i++) {
        struct Entry *entry = table[i];
        while (entry != NULL) {
            printf("%s: %d\n", entry->name, entry->count);
            struct Entry *temp = entry;
            entry = entry->next;
            free(temp);
        }
    }

    if (file != stdin) {
        fclose(file);
    }
    return 0;
}

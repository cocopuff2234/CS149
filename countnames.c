/**
 * Description: This program counts the occurrences of each name in a file using a hash table.
 * Author names: Erik Thompson, Ryuto Kawabata
 * Author emails: erik.thompson@sjsu.edu, ryuto.kawabata@sjsu.edu
 * Last modified date: 09/07/2026
 * Creation date: 09/07/2026
 **/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*** Define table size ***/
#define TABLE_SIZE 101

/*** Define hash table entry, track name, count, and next pointer*/
struct Entry {
    char name[31];
    int count;
    struct Entry *next;
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
    FILE *file = stdin;

    if (argc == 2) {
        file = fopen(argv[1], "r");

        if (file == NULL) {
            fprintf(stderr, "error: cannot open file\n");
            return 1;
        }
    } else if (argc > 2) {
        return 1;
    }

    /* Start empty */
    struct Entry *table[TABLE_SIZE] = {NULL};
    char line[32] = {0};
    int line_number = 0;

    while (fgets(line, sizeof(line), file) != NULL) {
        line_number++;

        line[strcspn(line, "\n")] = 0;

        if (strlen(line) == 0) {
            fprintf(stderr, "Warning - Line %d is empty.\n", line_number);
            continue;
        }

        unsigned int index = hash(line);
        struct Entry *entry = table[index];

        while (entry != NULL && strcmp(entry->name, line) != 0) {
            entry = entry->next;
        }

        /* If entry, or name, is found, increment its count */
        if (entry != NULL) {
            entry->count++;
        } else {
            struct Entry *new_entry = malloc(sizeof(struct Entry));

            if (new_entry == NULL) {
                if (file != stdin) {
                    fclose(file);
                }

                return 1;
            }

            strcpy(new_entry->name, line);
            new_entry->count = 1;
            new_entry->next = table[index];
            table[index] = new_entry;
        }
    }

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
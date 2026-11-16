/**
 * Description: Multi-threaded countnames (A6). Counts how many times each
 *              name appears in the input files. One thread is created per
 *              input file (thread 1 reads the first file, thread 2 the
 *              second, ...); all threads update ONE shared, dynamically
 *              allocated hash table (malloc/realloc/strdup, from A5), and a
 *              mutex makes every update of the table atomic. After all
 *              threads have been joined, the totals are printed to stdout
 *              and all memory is freed.
 * Author names: Erik Thompson, Ryuto Kawabata
 * Author emails: erik.thompson@sjsu.edu, ryuto.kawabata@sjsu.edu
 * Last modified date: 11/17/2026
 * Creation date: 09/07/2026
 *
 * Compile: gcc -D_REENTRANT -pthread -o countnames countnames.c -Wall -Werror
 * Usage:   ./countnames FILE1 [FILE2 [FILE3 ...]]   one thread per file
 *          ./countnames                             one thread reading stdin
 **/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

/*** The hash table starts with this many buckets and grows with realloc ***/
#define HASH_INITIAL_SIZE 10

/*
 * ---- Shared hash table (same structure as A5) ----
 *
 *   table ----> [ bucket 0 ] -> node -> node -> NULL
 *               [ bucket 1 ] -> NULL
 *               [   ...    ]
 *
 * `table` is a malloc()'d array of bucket heads (NameCountData **). Every
 * node is malloc()'d and owns a strdup()'d copy of its name. When the number
 * of names exceeds the number of buckets, the array is doubled with
 * realloc() and every node is rehashed.
 */
typedef struct NameCountData {
    char *name;                    /* strdup()'d, free()'d in hash_free() */
    int count;
    struct NameCountData *next;    /* next node in the same bucket */
} NameCountData;

typedef struct {
    NameCountData **table;         /* malloc/realloc'd array of bucket heads */
    size_t capacity;               /* number of buckets */
    size_t size;                   /* number of distinct names stored */
} HashTable;

/*
 * The ONE table shared by all threads, and the mutex that protects it.
 * RULE: every read or write of `totals` (the bucket array, any node, size,
 * capacity) happens while holding `totals_lock`. The hash_* functions below
 * do not lock by themselves; their callers do.
 */
static HashTable totals;
static pthread_mutex_t totals_lock = PTHREAD_MUTEX_INITIALIZER;

/*
 * hash_code - radix hash of a string: c0 + c1*k + c2*k^2 + ... with k = 31.
 * Pure function of its argument, so it needs no lock.
 */
static unsigned long
hash_code(const char *s)
{
    unsigned long h = 0;

    for (; *s != '\0'; s++) {
        h = h * 31 + (unsigned char)*s;
    }

    return h;
}

/*
 * hash_init - allocate an empty table with HASH_INITIAL_SIZE buckets.
 * Called once from main() before any thread exists.
 * Returns 0 on success, -1 if the allocation fails.
 */
static int
hash_init(HashTable *ht)
{
    ht->capacity = HASH_INITIAL_SIZE;
    ht->size = 0;

    /* calloc = malloc + zero fill, so every bucket starts as NULL */
    ht->table = calloc(ht->capacity, sizeof(NameCountData *));
    if (ht->table == NULL) {
        perror("malloc failed");
        return -1;
    }

    return 0;
}

/*
 * hash_grow - double the number of buckets with realloc() and move every
 * node into its new bucket. CALLER MUST HOLD totals_lock: while this runs
 * every chain is temporarily taken apart, so no other thread may look at
 * the table. Returns 0 on success; on failure the old table is left intact.
 */
static int
hash_grow(HashTable *ht)
{
    size_t old_capacity = ht->capacity;
    size_t new_capacity = old_capacity * 2;

    /* Step 1: unlink every node into one temporary list */
    NameCountData *all = NULL;

    for (size_t i = 0; i < old_capacity; i++) {
        NameCountData *node = ht->table[i];

        while (node != NULL) {
            NameCountData *next = node->next;
            node->next = all;
            all = node;
            node = next;
        }
    }

    /* Step 2: realloc the bucket array (never assign the result directly:
     * if it is NULL the old block is still valid and must not be lost) */
    NameCountData **bigger = realloc(ht->table,
                                     new_capacity * sizeof(NameCountData *));
    if (bigger == NULL) {
        perror("realloc failed");

        /* put the nodes back into the old table so nothing is lost */
        while (all != NULL) {
            NameCountData *next = all->next;
            size_t b = hash_code(all->name) % old_capacity;
            all->next = ht->table[b];
            ht->table[b] = all;
            all = next;
        }

        return -1;
    }

    ht->table = bigger;
    ht->capacity = new_capacity;

    /* Step 3: clear ALL buckets (realloc does not zero the new part) */
    for (size_t i = 0; i < new_capacity; i++) {
        ht->table[i] = NULL;
    }

    /* Step 4: rehash every node into the bigger table */
    while (all != NULL) {
        NameCountData *next = all->next;
        size_t b = hash_code(all->name) % new_capacity;
        all->next = ht->table[b];
        ht->table[b] = all;
        all = next;
    }

    return 0;
}

/*
 * hash_add - add `count` to `name`; if the name is new, strdup() it into a
 * new malloc()'d node (growing the table first when it is full).
 * CALLER MUST HOLD totals_lock. Returns 0 on success, -1 on failure.
 */
static int
hash_add(HashTable *ht, const char *name, int count)
{
    size_t b = hash_code(name) % ht->capacity;

    /* existing name: just add to its count */
    for (NameCountData *node = ht->table[b]; node != NULL; node = node->next) {
        if (strcmp(node->name, name) == 0) {
            node->count += count;
            return 0;
        }
    }

    /* new name: keep load factor <= 1 by growing before inserting */
    if (ht->size + 1 > ht->capacity) {
        if (hash_grow(ht) < 0) {
            return -1;
        }
        b = hash_code(name) % ht->capacity;   /* capacity changed */
    }

    NameCountData *node = malloc(sizeof(NameCountData));
    if (node == NULL) {
        perror("malloc failed");
        return -1;
    }

    /* strdup() mallocs a private copy: the caller's line buffer will be
     * overwritten by its next getline() */
    node->name = strdup(name);
    if (node->name == NULL) {
        perror("strdup failed");
        free(node);
        return -1;
    }

    node->count = count;
    node->next = ht->table[b];
    ht->table[b] = node;
    ht->size++;

    return 0;
}

/*
 * hash_print - print every "name: count" to stdout (bucket order).
 * Called from main() after all threads have been joined.
 */
static void
hash_print(const HashTable *ht)
{
    for (size_t i = 0; i < ht->capacity; i++) {
        for (NameCountData *node = ht->table[i]; node != NULL; node = node->next) {
            printf("%s: %d\n", node->name, node->count);
        }
    }
}

/*
 * hash_free - release every node, every strdup()'d name and the bucket
 * array. Called from main() after all threads have been joined.
 */
static void
hash_free(HashTable *ht)
{
    for (size_t i = 0; i < ht->capacity; i++) {
        NameCountData *node = ht->table[i];

        while (node != NULL) {
            NameCountData *next = node->next;
            free(node->name);
            free(node);
            node = next;
        }
    }

    free(ht->table);
    ht->table = NULL;
    ht->capacity = 0;
    ht->size = 0;
}

/*
 * What each thread is told to do, and what it reports back. One of these
 * exists per thread, so the threads never share it (no lock needed).
 */
typedef struct {
    int id;                 /* 1 for the first file, 2 for the second, ... */
    const char *filename;   /* file to read, or NULL to read stdin */
    int failed;             /* set to 1 by the thread if something went wrong */
    long lines_counted;     /* number of names this thread added */
} ThreadArg;

/*
 * count_file - thread start routine. Reads one file line by line and adds
 * every name to the shared table. Only the update of the shared table is
 * done under the mutex; opening the file, reading a line and trimming it
 * are done without the lock, so the threads really run in parallel.
 */
static void *
count_file(void *arg_ptr)
{
    ThreadArg *arg = (ThreadArg *) arg_ptr;
    const char *label = (arg->filename != NULL) ? arg->filename : "stdin";
    FILE *file = stdin;

    if (arg->filename != NULL) {
        file = fopen(arg->filename, "r");

        if (file == NULL) {
            /* only this thread gives up; the others keep running */
            fprintf(stderr, "error: cannot open file %s\n", arg->filename);
            arg->failed = 1;
            return NULL;
        }
    }

    /* getline() allocates and grows this buffer; it belongs to this thread
     * only, is reused for every line and freed once after the loop */
    char *line = NULL;
    size_t line_cap = 0;
    int line_number = 0;

    while (getline(&line, &line_cap, file) != -1) {
        line_number++;

        /* Replace the trailing newline with NULL; it is not part of the name */
        line[strcspn(line, "\n")] = '\0';

        /* Empty line: warn on stderr and do not count it */
        if (line[0] == '\0') {
            fprintf(stderr, "Warning - file %s line %d is empty.\n",
                    label, line_number);
            continue;
        }

        /*
         * ---- critical section ----
         * The table is shared by all threads, so searching it, bumping a
         * counter, inserting a node and growing/rehashing the bucket array
         * must not be interleaved with another thread doing the same.
         */
        pthread_mutex_lock(&totals_lock);
        int rc = hash_add(&totals, line, 1);
        pthread_mutex_unlock(&totals_lock);
        /* ---- end of critical section ---- */

        if (rc < 0) {
            arg->failed = 1;
            break;
        }

        arg->lines_counted++;
    }

    free(line);

    if (file != stdin) {
        fclose(file);
    }

    return NULL;
}

int
main(int argc, char *argv[])
{
    /* one thread per file; with no file, one thread that reads stdin */
    int thread_count = (argc > 1) ? argc - 1 : 1;

    if (hash_init(&totals) < 0) {
        return 1;
    }

    pthread_t *threads = malloc((size_t) thread_count * sizeof(pthread_t));
    ThreadArg *args = calloc((size_t) thread_count, sizeof(ThreadArg));
    int *started = calloc((size_t) thread_count, sizeof(int));

    if (threads == NULL || args == NULL || started == NULL) {
        perror("malloc failed");
        free(threads);
        free(args);
        free(started);
        hash_free(&totals);
        return 1;
    }

    int exit_status = 0;

    /*
     * Create ALL threads first (thread i gets the i-th file) and only then
     * join them, so they all run at the same time. Joining inside this loop
     * would run the files one after another.
     */
    for (int i = 0; i < thread_count; i++) {
        args[i].id = i + 1;
        args[i].filename = (argc > 1) ? argv[i + 1] : NULL;

        int rc = pthread_create(&threads[i], NULL, count_file, &args[i]);

        if (rc != 0) {
            fprintf(stderr, "error: pthread_create failed for thread %d: %s\n",
                    i + 1, strerror(rc));
            exit_status = 1;
            continue;
        }

        started[i] = 1;
    }

    /* Wait for every thread that was started */
    for (int i = 0; i < thread_count; i++) {
        if (!started[i]) {
            continue;
        }

        int rc = pthread_join(threads[i], NULL);

        if (rc != 0) {
            fprintf(stderr, "error: pthread_join failed for thread %d: %s\n",
                    i + 1, strerror(rc));
            exit_status = 1;
        }

        if (args[i].failed) {
            exit_status = 1;
        }
    }

    /*
     * All threads are gone, so main is the only one touching the table
     * from here on: no lock is needed to print and free it.
     */
    hash_print(&totals);
    hash_free(&totals);
    pthread_mutex_destroy(&totals_lock);

    free(threads);
    free(args);
    free(started);

    return exit_status;
}

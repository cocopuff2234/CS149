# CS 149 - Assignment 5: parallel countnames with a dynamic hash table

**Students:** Erik Thompson, Ryuto Kawabata

A5 extends A4. The children are unchanged in spirit: `shell` forks **one
child per file argument**, all children run in parallel, each child
redirects its stdout/stderr to `PID.out`/`PID.err` and writes its
`(name, count)` results into its own region of the `GLOBAL` shared memory
segment created with `mmap()`. What changes is **where the parent sums the
results**: instead of a fixed-size summation region of 1024 records, the
parent aggregates into a **dynamically allocated hash table** —
`malloc`'d with 10 buckets, grown with `realloc` as names arrive, names
stored as `char *` via `strdup` — so there is no longer any limit on the
number of distinct names. Everything is `free`'d afterwards; valgrind
reports no leaks in the parent or in any child.

```
% ./countnames test/names1.txt test/names2.txt
child 285 exited with status 0
child 284 exited with status 0
Tom Wu: 4
Jenn Xu: 2
```

Files:

| File | Purpose |
|---|---|
| `shell.c` | the shell: prompt, tokenize, create GLOBAL with `shm_open`/`mmap`, fork/dup2/execvp per file, wait, aggregate the child regions into a malloc/realloc'd hash table, print, free |
| `countnames.c` | counts names in one file (or stdin) with `getline` + `strdup`; prints `name: count` to stdout and, when started by the shell, writes the counts into its region of GLOBAL |
| `test/names1.txt`, `test/names2.txt` | given test files |
| `test/names3.txt` | own test (overlapping names across 3 files) |
| `test/names_long.txt` | own test (96 lines, 91 distinct names — forces several `realloc`s) |
| `test/names_long_redundant1.txt` ... `3.txt` | own test (the same names split over 3 files) |

## How to compile

```
gcc -o countnames countnames.c -Wall -Werror
gcc -o shell shell.c -Wall -Werror
```

Both compile with no warnings or errors. For valgrind add `-g` so the
reports show line numbers:

```
gcc -g -o countnames countnames.c -Wall -Werror
gcc -g -o shell shell.c -Wall -Werror
```

Note: the zyLab "Run" button tries to link every `.c` file into one program
and fails with "multiple definition of main". Compile from the Console with
the commands above instead.

## How to run

Start the shell, then type a command at the `%` prompt. Press `Ctrl-D` on an
empty line to exit the shell.

```
$ ./shell
% ./countnames test/names1.txt test/names2.txt
child 285 exited with status 0
child 284 exited with status 0
Tom Wu: 4
Jenn Xu: 2
%
```

To check for memory leaks in the parent **and** the children in one go:

```
$ echo "./countnames test/names_long.txt test/names1.txt" | \
      valgrind --leak-check=full --trace-children=yes ./shell
```

The parent's report appears on the terminal; each child's report is in its
`PID.err` file (because the child's stderr is redirected there). All of them
must say `in use at exit: 0 bytes in 0 blocks` and `ERROR SUMMARY: 0 errors`.

Notes:

- PIDs are assigned by the OS, so the numbers in the examples below will
  differ on every run.
- The order of names in the totals is hash-table bucket order, not
  alphabetical, and changes when the table grows. Pipe through `sort` to
  compare with `cat files... | grep -v '^$' | sort | uniq -c`.
- When the hash table grows, the shell prints one line per `realloc` to
  **stderr** (`[shell] hash table grown: 10 -> 20 buckets`). This never
  mixes with the results on stdout; use `2> /dev/null` to hide it.
- `countnames` can still be run on its own (`./countnames test/names1.txt`).
- To remove the result files between runs: `rm -f *.out *.err`

## Test cases

### 1. Two given files (`test/names1.txt`, `test/names2.txt`)

`names1.txt` = `Tom Wu`, `Tom Wu`, (empty line), `Tom Wu`; `names2.txt` =
`Jenn Xu`, `Jenn Xu`, `Tom Wu`.

Tests: the basic case. Two children, two regions, parent sums them in the
hash table (`Tom Wu` 3 + 1 = 4, `Jenn Xu` 0 + 2 = 2). Two distinct names fit
in the initial 10 buckets, so no `realloc` happens. `names1.txt` has an
empty line 3, so its `.err` gets a warning.

```
% ./countnames test/names1.txt test/names2.txt
child 285 exited with status 0
child 284 exited with status 0
Tom Wu: 4
Jenn Xu: 2
```

```
$ cat 284.out            $ cat 284.err
Tom Wu: 3                Warning - Line 3 is empty.

$ cat 285.out            $ cat 285.err
Jenn Xu: 2               (empty)
Tom Wu: 1
```

### 2. Three files with overlapping names (`names1.txt names2.txt names3.txt`) — own test

`names3.txt` = `Jenn Xu`, `Alexa`, `Tom Wu`, `Alexa`.

Tests: aggregation across three regions; a name seen in one, two and three
files (`Tom Wu` 3 + 1 + 1 = 5, `Jenn Xu` 0 + 2 + 1 = 3, `Alexa` 2). Also
checks that repeated names hit the *same* hash node (count is added, no
duplicate node is created).

```
% ./countnames test/names1.txt test/names2.txt test/names3.txt
child 288 exited with status 0
child 289 exited with status 0
child 290 exited with status 0
Tom Wu: 5
Alexa: 2
Jenn Xu: 3
```

### 3. Many distinct names (`test/names_long.txt`) — own test

96 lines with 91 distinct "first last" names; five names (`Andrew Lee`,
`Avery Adams`, `Avery Taylor`, `Jonathan Lee`, `Nathan Davis`) appear twice.

Tests: (a) the "a hundred names" requirement; (b) **`realloc`** — 91 names
do not fit in 10 buckets, so the table grows 10 → 20 → 40 → 80 → 160 (four
`realloc`s, each followed by a rehash of every node) and every count must
survive every rehash; (c) the five repeated names must end up as one node
with count 2 each, not two nodes; (d) correctness is verified against
`uniq -c`.

```
% ./countnames test/names_long.txt
[shell] hash table grown: 10 -> 20 buckets
[shell] hash table grown: 20 -> 40 buckets
[shell] hash table grown: 40 -> 80 buckets
[shell] hash table grown: 80 -> 160 buckets
child 301 exited with status 0
John Smith: 1
Emily Davis: 1
... (91 lines in total; the five repeated names show ": 2") ...
```

Check (from the real terminal):

```
$ echo "./countnames test/names_long.txt" | ./shell 2>/dev/null \
    | grep -v '^child\|^%' | sort > got.txt
$ sort test/names_long.txt | uniq -c \
    | awk '{c=$1; $1=""; sub(/^ /,""); print $0": "c}' | sort > expected.txt
$ diff got.txt expected.txt && echo SAME
SAME
$ grep ': 2' got.txt
Andrew Lee: 2
Avery Adams: 2
Avery Taylor: 2
Jonathan Lee: 2
Nathan Davis: 2
```

### 3b. Same names split over three files (`test/names_long_redundant1.txt` `2.txt` `3.txt`) — own test

Tests: three children each produce many names, and the same name arrives
from several regions; the parent must merge them into one hash node per
name while the table is growing. Expected totals are
`cat test/names_long_redundant1.txt test/names_long_redundant2.txt test/names_long_redundant3.txt | grep -v '^$' | sort | uniq -c`
(fill in the exact lines from your run).

### 4. Missing file + good file (`test/nope.txt`, `test/names1.txt`)

Tests: a child whose `fopen()` fails prints `error: cannot open file` to its
`PID.out` and exits with status **1**; its region stays empty (terminator at
index 0) and contributes nothing, so the table only contains the good file.

```
% ./countnames test/nope.txt test/names1.txt
child 294 exited with status 0
child 293 exited with status 1
Tom Wu: 3
```

### 5. No memory leaks (valgrind)

Tests: every `malloc`/`calloc`/`realloc`/`strdup`/`getline` allocation is
freed, in the parent (hash table: nodes, names, bucket array) and in the
children (hash table nodes, names, `getline` buffer).

```
$ echo "./countnames test/names_long.txt test/names1.txt" | \
      valgrind --leak-check=full --trace-children=yes ./shell > /dev/null
==322== Command: ./shell
==322==     in use at exit: 0 bytes in 0 blocks
==322== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
$ grep -E 'Command|in use at exit|ERROR SUMMARY' *.err
334.err:==334== Command: ./countnames test/names_long.txt ...
334.err:==334==     in use at exit: 0 bytes in 0 blocks
334.err:==334== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)
335.err: ... same for the other child ...
```

### 6. Command that does not exist / empty command line

Tests: as in A2–A4, `./nonexistent file` prints `error: cannot execute
./nonexistent` without forking or allocating; an empty line just re-prints
the prompt.

## Design

**Shared memory (unchanged from A4).** `GLOBAL` = `job_count` regions of
1024 fixed-size records `{ char name[31]; int count; }`; child *i* writes
region *i* and terminates it with an empty name. The A4 summation region is
gone — the parent aggregates in the hash table instead.

**Parent hash table.** The table is the double-pointer structure from the
slides:

```c
typedef struct NameCountData {
    char *name;                   /* strdup()'d */
    int count;
    struct NameCountData *next;   /* chaining on collision */
} NameCountData;

NameCountData **table;            /* malloc'd array of bucket heads */
```

`hash_init` allocates the bucket array with `calloc(10, sizeof(NameCountData *))`
(malloc plus zero-fill, so every bucket starts `NULL`). The hash function is
the radix hashCode from the assignment, `c0 + c1·31 + c2·31² + …`, with the
prime k = 31; the bucket is `hash % capacity`.

`hash_add(name, count)` walks the bucket's chain with `strcmp`; if the name
exists it adds to `count`, otherwise it `malloc`s a node, `strdup`s the
name into it and pushes it on the front of the chain. The `strdup` is
essential: the name it was given points into the mmap'd child region, which
is unmapped right after aggregation, so the node must own its own copy.

**realloc.** Before inserting a new name, if `size + 1 > capacity` (load
factor would exceed 1) `hash_grow` is called. It unlinks every node into a
temporary list, `realloc`s the bucket array to twice the size, sets *all*
buckets to `NULL` (realloc does not zero the new part), and re-inserts
every node using `hash % new_capacity` — a node's bucket changes when the
capacity changes, which is why a plain `realloc` without a rehash would
lose names. The return value of `realloc` is stored in a temporary first:
if it is `NULL` the old array is still valid and the nodes are put back,
so a failed `realloc` never leaks or corrupts the table.

**free.** `hash_free` walks every chain, `free`s each node's `name` and then
the node, and finally `free`s the bucket array. It runs after every command,
so leaving the shell with `Ctrl-D` leaves nothing allocated.

**countnames (child).** Lines are now read with `getline(&line, &cap, file)`
with `line = NULL` initially, so there is no fixed line length; the buffer
is reused for every line and `free`d once after the loop. Each new name is
`strdup`'d into the child's own hash table (the A1 chained table, now with
`char *name`), and `free`d together with its node after printing.

# Lessons learned

- A hash table with chaining is just an array of pointers plus linked
  lists; `NameCountData **` is the array, `table[i]` is a list head. Once
  that clicked, "double pointer" stopped being scary.
- `realloc` on the bucket array is not enough on its own: every node's
  bucket index depends on the capacity, so the whole table has to be
  rehashed after growing. Forgetting that made names "disappear" (they
  were still in memory, in the wrong bucket).
- Never write `p = realloc(p, n)`: if it fails `p` becomes `NULL` and the
  old block is leaked. Use a temporary and check it.
- `strdup` mallocs for you; you must `free` the result exactly once and
  must **not** malloc a buffer for it beforehand (that would leak).
- `getline` with a `NULL` pointer allocates and grows its own buffer; it is
  reused across calls and freed once at the end — which also means the
  previous line's contents are gone after the next call, hence `strdup`.
- Ownership matters: a string living in the mmap'd region is only valid
  until `munmap`; the hash node needs its own copy.
- `valgrind --trace-children=yes` follows `exec`; because the children's
  stderr is redirected, their reports end up in `PID.err`, not on screen.
- Load factor ≤ 1 keeps chains short on average, so lookup stays close to
  O(1) even though the table starts tiny (10 buckets).

# References

- Assignment 5 description and the A5 template code folder (hash table /
  linked list structs) on Canvas.
- Course slides on the double-pointer hash table:
  https://docs.google.com/presentation/d/1nj0bY0EKYOCzolYXlEzxSgbpw-rKMC-k7mTKBXk5qYs/edit#slide=id.gab3d7a2387_0_0
- How to free memory from `getline`:
  https://stackoverflow.com/questions/42478868/how-do-i-properly-free-memory-related-to-getline-function
- Why `getline` takes `char **`:
  https://stackoverflow.com/questions/5744393/why-is-the-first-argument-of-getline-a-pointer-to-pointer-char-instead-of-c
- `fgets`/`getline` null-termination:
  https://stackoverflow.com/questions/1660228/does-fgets-always-terminate-the-char-buffer-with-0
  https://stackoverflow.com/questions/13521961/is-returned-string-null-terminated-from-getline
- `strdup`: https://www.geeksforgeeks.org/strdup-strdndup-functions-c/
- valgrind on forked/exec'd children:
  https://stackoverflow.com/questions/301576/how-do-i-tell-valgrind-to-memcheck-forked-processes
- Radix hashCode / Java `String.hashCode()` with k = 31.
- Linux man pages: `man 3 malloc` (malloc/calloc/realloc/free),
  `man 3 strdup`, `man 3 getline`, `man 2 mmap`, `man 3 shm_open`,
  `man 2 wait`

# Acknowledgements

- Class lectures and the assignment description.
- Our A4 submission, which this assignment extends.

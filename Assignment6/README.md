# CS 149 - Assignment 6: multi-threaded countnames

**Students:** Erik Thompson, Ryuto Kawabata

A6 replaces the child processes of A2–A5 with **threads**. `countnames` is
now a single process that creates **one thread per input file** (thread 1
reads the first file, thread 2 the second, ...). All threads update **one
shared hash table** — the dynamically allocated table from A5
(`malloc`/`realloc`, `char *` names via `strdup`) — and a **mutex** makes
every update of that table atomic. When all threads have been joined, the
totals are printed to stdout and all memory is freed. There is no `fork`,
`wait`, pipe or `mmap` any more.

```
$ ./countnames test/names1.txt test/names2.txt
Warning - file test/names1.txt line 3 is empty.
Tom Wu: 4
Jenn Xu: 2
```

Files:

| File | Purpose |
|---|---|
| `countnames.c` | the whole assignment: threads (`pthread_create`/`pthread_join`), the shared hash table and the mutex that protects it |
| `shell.c` | minimal shell, kept for consistency with A2–A5: forks **one** child per command line, redirects it to `PID.out`/`PID.err`, then prints those files |
| `test/names1.txt`, `test/names2.txt` | given test files |
| `test/names3.txt` | own test (overlapping names across 3 files) |
| `test/threads1.txt`, `test/threads2.txt`, `test/threads3.txt` | own test (3 × 2000 lines, 300 distinct names, heavy overlap — makes the threads contend for the table) |

## How to compile

```
gcc -D_REENTRANT -pthread -o countnames countnames.c -Wall -Werror
gcc -o shell shell.c -Wall -Werror
```

Both compile with no warnings or errors. (`countnames.c` also compiles with
the plain `gcc -o countnames countnames.c -Wall -Werror` on the zyLab glibc,
where pthreads are part of libc.) For valgrind add `-g`.

Note: the zyLab "Run" button tries to link every `.c` file into one program
and fails with "multiple definition of main". Compile from the Console with
the commands above instead.

## How to run

`countnames` is meant to be run directly from bash:

```
$ ./countnames FILE1 [FILE2 [FILE3]]     # one thread per file
$ ./countnames                           # no file: one thread reads stdin (Ctrl-D ends)
```

Counts go to **stdout**; warnings and errors go to **stderr**:

```
$ ./countnames test/names1.txt test/names2.txt 2> /dev/null   # counts only
$ ./countnames test/names1.txt test/names2.txt > /dev/null    # messages only
$ echo $?                                                     # exit status
```

Exit status: `0` if every file was read, `1` if at least one file could not
be opened (the other files are still counted and printed).

It can also be run through the shell, in which case the single child's
output lands in `PID.out` / `PID.err` and the shell prints both files:

```
$ ./shell
% ./countnames test/names1.txt test/names2.txt
Warning - file test/names1.txt line 3 is empty.
child 501 exited with status 0
Tom Wu: 4
Jenn Xu: 2
%
```

Notes:

- The order of the printed names is hash-table bucket order, not
  alphabetical. Pipe through `sort` to compare with
  `cat files... | grep -v '^$' | sort | uniq -c`.
- The order of the **stderr** lines (warnings/errors from different
  threads) can change from run to run, because the threads really run at
  the same time. The counts on stdout never change.

## Test cases

### 1. Two given files (`test/names1.txt`, `test/names2.txt`)

`names1.txt` = `Tom Wu`, `Tom Wu`, (empty line), `Tom Wu`; `names2.txt` =
`Jenn Xu`, `Jenn Xu`, `Tom Wu`.

Tests: the basic case from the assignment. Two threads; `Tom Wu` is counted
by both threads into the **same** hash node (3 + 1 = 4). The empty line 3 of
`names1.txt` is reported on stderr with its file name and not counted.

```
$ ./countnames test/names1.txt test/names2.txt
Warning - file test/names1.txt line 3 is empty.
Tom Wu: 4
Jenn Xu: 2
$ echo $?
0
```

### 2. Three files with overlapping names (`names1.txt names2.txt names3.txt`) — own test

`names3.txt` = `Jenn Xu`, `Alexa`, `Tom Wu`, `Alexa`.

Tests: three threads (the maximum we assume). A name updated by three
threads (`Tom Wu` 3 + 1 + 1 = 5), by two (`Jenn Xu` 2 + 1 = 3) and by one
(`Alexa` 2).

```
$ ./countnames test/names1.txt test/names2.txt test/names3.txt 2> /dev/null
Tom Wu: 5
Alexa: 2
Jenn Xu: 3
```

### 3. Many names, heavy contention (`test/threads1.txt`, `threads2.txt`, `threads3.txt`) — own test

Each file has 2000 lines drawn at random from the same 300 "first last"
names, so the three threads constantly hit the same nodes, and the table
has to grow 10 → 20 → 40 → 80 → 160 → 320 buckets (`realloc` + rehash)
**while the other threads are trying to use it**.

Tests: (a) the mutex — without it, counts are lost or the program crashes
(see "Design"); (b) `realloc`/rehash under concurrency; (c) 6000 lines in
total, 300 distinct names; (d) the result must be identical on every run.

```
$ ./countnames test/threads1.txt test/threads2.txt test/threads3.txt | sort > got.txt
$ cat test/threads1.txt test/threads2.txt test/threads3.txt | sort | uniq -c \
    | sed -E 's/^ *([0-9]+) (.*)$/\2: \1/' | sort > expected.txt
$ diff got.txt expected.txt && echo SAME
SAME
$ wc -l < got.txt
300
$ head -3 got.txt
Ada Berners-Lee: 17
Ada Dijkstra: 18
Ada Gosling: 13
```

Repeat it to show the result does not depend on thread scheduling:

```
$ for i in $(seq 20); do ./countnames test/threads1.txt test/threads2.txt test/threads3.txt \
      | sort | diff -q - expected.txt > /dev/null || echo WRONG; done; echo done
done
```

(no `WRONG` lines are printed).

### 4. Missing file + good files (`test/nope.txt`, `test/names1.txt`, `test/names3.txt`)

Tests: the thread for the non-existent file prints an error naming that
file and stops; **the other threads keep running** and their counts are
printed. Exit status is 1.

```
$ ./countnames test/nope.txt test/names1.txt test/names3.txt
Warning - file test/names1.txt line 3 is empty.
error: cannot open file test/nope.txt
Tom Wu: 4
Alexa: 2
Jenn Xu: 1
$ echo $?
1
```

(The two stderr lines may appear in either order.)

### 5. Every file missing

Tests: all threads fail; nothing is counted, nothing is printed on stdout,
no crash, no leak.

```
$ ./countnames test/a.txt test/b.txt
error: cannot open file test/a.txt
error: cannot open file test/b.txt
$ echo $?
1
```

### 6. The same file twice (`test/names2.txt test/names2.txt`)

Tests: two threads read the **same** file at the same time (each has its own
`FILE *`), so every single update collides with the other thread's update
of the same node. The counts must be exactly doubled.

```
$ ./countnames test/names2.txt test/names2.txt
Tom Wu: 2
Jenn Xu: 4
```

### 7. No arguments: names from stdin

Tests: `argc == 1`. One thread reads stdin until EOF.

```
$ printf 'Ann\nBob\n\nAnn\n' | ./countnames
Warning - file stdin line 3 is empty.
Bob: 1
Ann: 2
$ ./countnames < /dev/null
$ echo $?
0
```

### 8. No memory leaks, no data races (valgrind)

Tests: everything that was allocated (bucket array, nodes, `strdup`'d
names, each thread's `getline` buffer, the thread/argument arrays) is freed,
and helgrind finds no unsynchronized access to shared data.

```
$ valgrind --leak-check=full ./countnames test/threads1.txt test/threads2.txt test/threads3.txt > /dev/null
==489==     in use at exit: 0 bytes in 0 blocks
==489==   total heap usage: 622 allocs, 622 frees, 35,803 bytes allocated
==489== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 0 from 0)

$ valgrind --tool=helgrind ./countnames test/threads1.txt test/threads2.txt test/threads3.txt > /dev/null
==494== ERROR SUMMARY: 0 errors from 0 contexts (suppressed: 7248 from 20)
```

### 9. Through the shell

Tests: `shell` passes **all** file arguments to a single `countnames`
process (one PID, one `PID.out`, one `PID.err`) and prints the result.

```
$ ./shell
% ./countnames test/names1.txt test/names2.txt
Warning - file test/names1.txt line 3 is empty.
child 501 exited with status 0
Tom Wu: 4
Jenn Xu: 2
% ./nonexistent x
error: cannot execute ./nonexistent
%
$ ls *.out *.err
501.err  501.out
```

## Design

**Threads.** `main` builds one `ThreadArg { id, filename, failed,
lines_counted }` per file and calls `pthread_create(&threads[i], NULL,
count_file, &args[i])` for **all** files first; only then does it
`pthread_join` them. Joining inside the creation loop would make the files
run one after another (the same mistake as fork/wait/fork/wait in A2). Each
thread has its own `ThreadArg`, its own `FILE *` and its own `getline`
buffer, so none of those need locking; a thread reports failure by setting
`failed` in its own struct, which `main` reads after the join.

**Shared data.** The only thing the threads share is the hash table
`totals` from A5: a `malloc`'d array of bucket heads (`NameCountData **`),
chained nodes that each own a `strdup`'d name, starting at 10 buckets and
doubled with `realloc` (followed by a rehash of every node) whenever the
number of names would exceed the number of buckets.

**Mutex.** One `pthread_mutex_t totals_lock` protects the whole table. The
rule, stated at the top of the file, is that *every* access to `totals`
happens with the lock held. In the thread function the critical section is
exactly one call:

```c
pthread_mutex_lock(&totals_lock);
int rc = hash_add(&totals, line, 1);    /* search, count++ or insert, maybe realloc */
pthread_mutex_unlock(&totals_lock);
```

Opening the file, reading a line with `getline`, stripping the newline and
checking for an empty line all happen **outside** the lock, so the threads
do their I/O in parallel and only queue up for the short table update.

Why the lock is needed: `count += 1` is read–modify–write, so two threads
updating the same name can lose an update; two threads inserting into the
same bucket can lose a node; and worst of all, while one thread is inside
`hash_grow` every chain is temporarily taken apart, so another thread
walking a chain follows stale pointers. We verified this by temporarily
removing the two lock/unlock lines and running three 30,000-line files:
out of 8 runs, 7 crashed or hung and 1 printed wrong totals — none were
correct. With the lock, 30 of 30 runs matched `uniq -c`.

Why one lock instead of one per bucket: a per-bucket lock allows more
parallelism, but our table is resized, and resizing moves nodes between
buckets, which would require holding all bucket locks (or a reader–writer
lock around the whole table). A single mutex is simple to reason about and
the critical section is tiny, so we chose correctness and clarity.

**After the join.** Once every thread has been joined, `main` is the only
thread left, so it prints (`hash_print`) and frees (`hash_free`) the table
without the lock, destroys the mutex and frees the thread arrays.

**Errors.** A thread whose `fopen` fails prints `error: cannot open file
<name>` to stderr, sets `failed` and returns; the others are unaffected.
`main` exits with 1 if any thread failed. Empty lines produce
`Warning - file <name> line <n> is empty.` (the file name is included
because several threads write to stderr at once).

**shell.c.** In A2–A5 the shell created the parallelism (one child per
file). In A6 the parallelism is inside `countnames`, so the shell is reduced
to: tokenize, `fork` one child, `dup2` stdout/stderr onto `PID.out` /
`PID.err`, `execvp` the command with all its arguments, `waitpid`, report
the exit status, and print the two files.

# Lessons learned

- Threads share the whole address space, so sharing data is trivial (no
  pipe, no `mmap`, no `exec` problem) — and that is exactly why it is
  dangerous: nothing stops two threads from changing the same bytes.
- `pthread_create` takes a `void *` argument and the start routine returns
  `void *`; giving each thread its own struct is the clean way to pass
  inputs in and results out without sharing.
- Create all threads, then join all threads. Joining right after creating
  each one makes the program sequential.
- A mutex must cover *every* access to the shared structure, including the
  "read-only" search, because another thread may be rehashing it.
- Keep the critical section as small as possible: lock around the table
  update only, not around file I/O, or the threads stop being parallel.
- Without the lock the program did not just give slightly wrong counts — it
  usually crashed or looped forever, because a `realloc`/rehash in one
  thread invalidated the pointers another thread was following.
- A race does not show up on small inputs (the three tiny files always
  "worked" even without the lock); you need large, overlapping inputs and
  many repetitions, or a tool like helgrind, to see it.
- Each `fprintf` call is atomic with respect to other stdio calls, so lines
  from different threads do not get mixed together, but their order is not
  predictable.
- `valgrind --tool=helgrind` checks for data races the same way
  `--leak-check=full` checks for leaks.

# References

- Assignment 6 description and the sample code under `code/a6` on Canvas.
- Course slides on threads, locks and critical sections.
- OSTEP, ch. 26 (Concurrency: An Introduction), ch. 27 (Thread API), ch. 28
  (Locks) and ch. 29 (Lock-based Concurrent Data Structures):
  https://pages.cs.wisc.edu/~remzi/OSTEP/
- How to free memory from `getline`:
  https://stackoverflow.com/questions/42478868/how-do-i-properly-free-memory-related-to-getline-function
- `strdup`: https://www.geeksforgeeks.org/strdup-strdndup-functions-c/
- Linux man pages: `man 3 pthread_create`, `man 3 pthread_join`,
  `man 3 pthread_mutex_lock`, `man 7 pthreads`, `man 3 getline`,
  `man 3 malloc`
- Valgrind manual, Helgrind: https://valgrind.org/docs/manual/hg-manual.html

# Acknowledgements

- Class lectures and the assignment description.
- Our A5 submission, whose hash table this assignment reuses.

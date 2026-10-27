# CS 149 - Assignment 4: parallel countnames with shared memory (mmap)

**Students:** Erik Thompson, Ryuto Kawabata

A4 extends A3. `shell` still forks **one child process per file argument**,
all children run in parallel, and each child still redirects its stdout to
`PID.out` and its stderr to `PID.err`. What changes is **how the results get
back to the shell**: instead of a pipe, the shell creates one large shared
memory segment (`GLOBAL`) with `mmap()`, divides it into one region per child
plus a summation region, and every child writes its `(name, count)` results
directly into its own region. After all children have finished, the shell
combines the regions into the summation region and prints the totals.

```
% ./countnames test/names1.txt test/names2.txt
child 440 exited with status 0
child 439 exited with status 0
Tom Wu: 4
Jenn Xu: 2
```

Files:

| File | Purpose |
|---|---|
| `shell.c` | the shell: prompt, tokenize, create GLOBAL with `shm_open`/`mmap`, fork/dup2/execvp per file, wait, aggregate the child regions into the summation region, print totals, clean up |
| `countnames.c` | counts names in one file (or stdin); prints `name: count` to stdout and, when started by the shell, also writes the counts into its assigned region of GLOBAL |
| `test/names1.txt`, `test/names2.txt` | given test files |
| `test/names3.txt` | our own test file (see below) |

## How to compile

```
gcc -o countnames countnames.c -Wall -Werror
gcc -o shell shell.c -Wall -Werror
```

Both compile with no warnings or errors. No other files are needed.
(`shm_open`/`sem_open` are in libc on the zyLab glibc, so no `-lrt` or
`-lpthread` is required.)

Note: the zyLab "Run" button tries to link every `.c` file into one program
and fails with "multiple definition of main", because `shell.c` and
`countnames.c` are two separate programs. Compile from the Console with the
two commands above instead.

## How to run

Start the shell, then type a command at the `%` prompt. Press `Ctrl-D` on an
empty line to exit the shell.

```
$ ./shell
% ./countnames test/names1.txt test/names2.txt
child 440 exited with status 0
child 439 exited with status 0
Tom Wu: 4
Jenn Xu: 2
%
```

Each child's own results are still in the current directory (`439.out`,
`439.err`, ...), exactly as in A2/A3. The summed totals are printed by the
shell to the terminal.

Notes:

- PIDs are assigned by the OS, so the numbers in the examples below will
  differ on every run.
- The order of names in the totals is the order in which they were first
  inserted into the summation region (which depends on hash-table order
  inside each child and on which child finished first), so it is not
  alphabetical. Pipe through `sort` to compare with
  `cat files... | grep -v '^$' | sort | uniq -c`.
- The order of the `child N exited ...` lines is the order in which the
  children actually finished, which is how you can see the parallelism.
- `countnames` can still be run on its own (`./countnames test/names1.txt`),
  in which case it behaves exactly as in A1 and only prints to stdout.
- The shared memory object lives under `/dev/shm` while a command runs and
  is removed (`shm_unlink`) when the command finishes. `ls /dev/shm` after
  a run should show nothing left over.
- To remove the result files between runs: `rm -f *.out *.err`

## Test cases

### 1. Two given files (`test/names1.txt`, `test/names2.txt`)

`names1.txt` = `Tom Wu`, `Tom Wu`, (empty line), `Tom Wu`; `names2.txt` =
`Jenn Xu`, `Jenn Xu`, `Tom Wu`.

Tests: the basic A4 case. Two children run in parallel, each writes its own
`PID.out`/`PID.err` **and** its own region of GLOBAL; the parent adds the two
regions into the summation region (`Tom Wu` 3 + 1 = 4, `Jenn Xu` 0 + 2 = 2).
`names1.txt` has an empty line 3, so its `.err` gets a warning; the warning
goes to the `.err` file only, never into shared memory.

```
% ./countnames test/names1.txt test/names2.txt
child 440 exited with status 0
child 439 exited with status 0
Tom Wu: 4
Jenn Xu: 2
```

```
$ cat 439.out            $ cat 439.err
Tom Wu: 3                Warning - Line 3 is empty.

$ cat 440.out            $ cat 440.err
Tom Wu: 1                (empty)
Jenn Xu: 2
```

Check: `cat test/names1.txt test/names2.txt | grep -v '^$' | sort | uniq -c`
gives `2 Jenn Xu` and `4 Tom Wu`.

### 2. Three files with overlapping names (`names1.txt names2.txt names3.txt`) — own test

`names3.txt` = `Jenn Xu`, `Alexa`, `Tom Wu`, `Alexa`.

Tests: aggregation across the maximum of three files, i.e. three child
regions plus the summation region inside one GLOBAL segment. `Tom Wu` appears
in all three files, `Jenn Xu` in two, `Alexa` only in the third, so the parent
must correctly add partial counts from one, two and three regions:
`Tom Wu` 3 + 1 + 1 = 5, `Jenn Xu` 0 + 2 + 1 = 3, `Alexa` 0 + 0 + 2 = 2.
It also checks that no child wrote outside its region: if one had, another
child's counts would be corrupted and the totals would be wrong.

```
% ./countnames test/names1.txt test/names2.txt test/names3.txt
child 538 exited with status 0
child 537 exited with status 0
child 539 exited with status 0
Tom Wu: 5
Jenn Xu: 3
Alexa: 2
```

### 3. Missing file + good file (`test/nope.txt`, `test/names1.txt`)

Tests: a child whose `fopen()` fails prints `error: cannot open file` to its
`PID.out` and exits with status **1** (same exit codes as A1); the parent
reports that status. The failing child writes nothing into its region, so
its region still starts with the all-zero terminator and contributes nothing
to the totals, while the other child's region is aggregated normally.

```
% ./countnames test/nope.txt test/names1.txt
child 540 exited with status 1
child 541 exited with status 0
Tom Wu: 3
```

```
$ cat 540.out
error: cannot open file
```

### 4. Command that does not exist

Tests: the shell checks with `access()` that the command is executable
**before** forking (same as A2). No children, no shared memory, no
`.out`/`.err`.

```
% ./nonexistent test/names1.txt
error: cannot execute ./nonexistent
```

### 5. Empty command line

Tests: pressing Enter (or typing only spaces/tabs) just re-prints the prompt.

### 6. No leftover shared memory

Tests: after any of the commands above has finished, `ls /dev/shm` (run from
the real terminal, after `Ctrl-D`) shows no `countnames_*` object, because
the shell calls `munmap()` and `shm_unlink()` after printing the totals.

## Design

**Memory layout.** `GLOBAL` is an array of

```c
typedef struct { char name[31]; int count; } NameCountData;
```

split into `job_count + 1` regions of `MAX_NAMES` entries each:

```
GLOBAL: | region 0 (child 0) | region 1 (child 1) | ... | summation region |
```

Region `i` is written only by child `i`; the summation region is written
only by the parent. The whole segment is zero-filled before forking, so every
region starts as a sequence of NULL entries, and an entry whose `name[0]` is
`'\0'` marks the end of the valid data in that region. The start of a region
is found with pointer arithmetic on the cast base pointer:
`NameCountData *region = (NameCountData *) GLOBAL + i * MAX_NAMES;`.

**Creating GLOBAL (parent).** For each command line the shell calls
`shm_open()` to create a named shared memory object, `ftruncate()` to give it
`(job_count + 1) * MAX_NAMES * sizeof(NameCountData)` bytes, and `mmap()`
with `MAP_SHARED` to map it. `MAP_SHARED` is what makes writes from one
process visible to the others.

**Surviving exec (the hard part).** After `fork()` the child inherits the
mapping, but `execvp()` replaces the whole address space, so the mapping is
lost. File descriptors, however, survive `exec`. The shell therefore clears
`FD_CLOEXEC` on the shared memory descriptor with `fcntl()` (descriptors from
`shm_open` are close-on-exec by default) and passes the descriptor number, the
total size and the child's region index to `countnames` on its command line:

```
./countnames FILE SHM_FD TOTAL_SIZE REGION_INDEX MAX_NAMES
```

`countnames` then calls `mmap()` again on that descriptor and computes the
address of its own region. It never touches memory outside that region
(the write loop stops at `MAX_NAMES - 1` so the terminator is always
present).

**Child.** Same hash-table counting as A1. In the final loop it `printf`s
each `name: count` (into `PID.out`, as in A2) and copies the same pair into
`region[n++]` of its assigned region, then `munmap`s and exits 0.

**Parent.** The parent never waits inside the fork loop, so all children run
in parallel. After the loop it calls `wait()` until it returns `-1`, printing
each child's exit status or signal. Only then — when no child can still be
writing — does it walk each child region up to its terminator and add every
entry into the summation region (linear search by name, insert at the first
empty slot). Finally it prints the summation region, `munmap`s GLOBAL and
`shm_unlink`s the object.

# Lessons learned

- `mmap()` with `MAP_SHARED` on a shared memory object is the simplest way
  to give several processes the *same* bytes: a write in one process is
  immediately visible in the others, with no copying and no `read`/`write`
  calls, unlike the pipe in A3.
- A memory mapping does not survive `exec`, but file descriptors do. This
  took the longest to understand: right after `fork()` the child can see
  GLOBAL, but after `execvp()` the new program has to re-attach by
  `mmap`-ing the inherited descriptor (or re-opening the object by name).
- Descriptors from `shm_open` have `FD_CLOEXEC` set, so without the
  `fcntl()` call the child's `mmap` fails with `EBADF`.
- Giving every child its own region is what makes this safe without any
  locking in the main version: two processes never write the same bytes,
  and the parent only reads after `wait()` says every child is gone.
- Zero-filling the segment before forking does double duty: it makes every
  region "empty" and it provides the terminator entry for free.
- `void *` from `mmap` has to be cast to `NameCountData *` before `[i]`
  works, and all pointer arithmetic must be done on the cast pointer so it
  advances in units of `sizeof(NameCountData)`, not bytes.
- Shared memory objects are not freed when the process exits; forgetting
  `shm_unlink` leaves files in `/dev/shm` until reboot.
- The parent's stdio buffer is still copied into every child by `fork()`,
  so `fflush(stdout)` before forking is still needed (same lesson as A2).

# References

- Assignment 4 description (GLOBAL layout, NameCountData, termination by a
  NULL name, mmap subspaces, "slightly harder" bonus) on Canvas, including
  the mmap read/write example and the shared-memory-after-exec sample linked
  from it.
- Course slides on fork/wait/exec and on decoding the wait status:
  https://docs.google.com/presentation/d/1tFAJHE88J3ylpWa9CZ5dhcAnSy1giA-YiUEUEIJej0Q/edit#slide=id.g27d9461c96e_1_127
- Course sample code under `code/ipc` (fork, pipe, wait) and `code/a4`
  (hash table / linked list templates)
- Linux man pages: `man 2 mmap`, `man 3 shm_open`, `man 2 ftruncate`,
  `man 2 fcntl` (`FD_CLOEXEC`), `man 7 shm_overview`, `man 2 fork`,
  `man 2 wait`, `man 3 exec`, `man 2 dup2`
- Stevens & Rago, *Advanced Programming in the UNIX Environment*, ch. 8
  (process control), ch. 14.8 (memory-mapped I/O) and ch. 15.9 (POSIX
  shared memory)


# Acknowledgements

- Class lectures and the assignment description.
- Our A3 submission, which this assignment extends.


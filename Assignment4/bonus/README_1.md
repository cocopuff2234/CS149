# CS 149 - Assignment 4 BONUS: children aggregate directly into GLOBAL

**Students:** Erik Thompson, Ryuto Kawabata

This is the "slightly harder solution" from the A4 description. Everything is
the same as our main A4 submission (one child per file, `PID.out`/`PID.err`,
one `GLOBAL` shared memory segment created with `shm_open`/`mmap` and divided
into one region per child plus a summation region), with one difference:

**the children, not the parent, add their results into the summation
region.** Each child first writes its counts into its own region, then takes
a lock, merges those counts into the summation region, and releases the lock.
When `wait()` reports that every child is gone, the parent only prints the
summation region.

## How to compile and run

Identical to the main submission:

```
gcc -o countnames countnames.c -Wall -Werror
gcc -o shell shell.c -Wall -Werror
./shell
% ./countnames test/names1.txt test/names2.txt test/names3.txt
```

The test cases and expected totals are the same as in the main README
(`Tom Wu: 5`, `Jenn Xu: 3`, `Alexa: 2` for the three-file case, etc.); only
the order of the printed names may differ, because it now depends on which
child reached the summation region first.

## Design: why a lock is needed

In the main version only the parent writes to the summation region, after
all children have exited, so there is no concurrent access. In the bonus
version several children can reach the summation region **at the same
time**. Updating a counter is read-modify-write (`count = count + n`): if
child A reads `Tom Wu: 3`, child B reads `Tom Wu: 3`, A writes 4 and B writes
4, one update is lost. The same can happen when two children both insert a
new name into "the first empty slot". This is a race condition, and the
result would be silently wrong, not an error.

We therefore serialize access with a **named POSIX semaphore** created by the
shell with `sem_open(name, O_CREAT, 0600, 1)` (a binary semaphore, i.e. a
mutex). The semaphore name is passed to `countnames` on the command line;
the child opens it with `sem_open(name, 0)`, calls `sem_wait()` before
touching the summation region and `sem_post()` right after. The critical
section is kept as short as possible (only the search-and-add on the
summation region; file reading and hash-table counting happen outside it),
so the children still do almost all of their work in parallel. The shell
`sem_close`s and `sem_unlink`s the semaphore together with the shared memory
after printing.

Because the lock window is tiny compared with reading and counting a file,
the race almost never shows up with three small files even without the
lock — which is exactly why it is dangerous: code that is wrong only
sometimes is still wrong.

# Lessons learned

- Shared memory gives processes the same bytes but no rules about who
  writes when; as soon as two processes may modify the same location, some
  form of mutual exclusion is required.
- A lost update is invisible: the program does not crash, the total is just
  smaller than it should be. Testing against `sort | uniq -c` is the only
  way to notice.
- A POSIX named semaphore initialised to 1 is a mutex that works across
  `fork` *and* `exec`, because it is looked up by name rather than by a
  pointer inherited from the parent.
- Keep the critical section small: lock only around the shared update, not
  around the whole file processing, or the "parallel" program becomes
  sequential again.
- A process that exits while holding a semaphore leaves it locked for
  everyone else, so every path between `sem_wait` and `sem_post` must not
  `return`/`exit`. (`flock()` on the shared memory descriptor is an
  alternative that is released automatically when the process dies.)

# References

- Assignment 4 description, "Slightly harder solution" section, and the
  linked shared-memory-after-exec sample.
- Linux man pages: `man 7 sem_overview`, `man 3 sem_open`, `man 3 sem_wait`,
  `man 3 sem_post`, `man 2 flock`, plus the mmap/shm_open pages cited in the
  main README.
- Stevens & Rago, *Advanced Programming in the UNIX Environment*, ch. 15.10
  (POSIX semaphores).


# Acknowledgements

- Class lectures and the assignment description.
- Our main A4 submission, which this bonus extends.

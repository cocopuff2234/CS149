# CS 149 - Assignment 2: parallel countnames

**Students:** Erik Thompson, Ryuto Kawabata

`shell` is a minimal prompt-based shell. When a command line such as

```
% ./countnames names1.txt names2.txt names2.txt
```

is entered, the shell forks **one child process per file argument** (three
children here) and each child `execvp`s `countnames` on its own file. All
children run in parallel; the parent forks every child first and only then
calls `wait()`, so a slow file never blocks a fast one. Each child redirects
its stdout to `PID.out` and its stderr to `PID.err` (using `dup2`) before
exec'ing, so `countnames` itself needs no changes to write to those files.

Files:

| File | Purpose |
|---|---|
| `shell.c` | the shell: prompt, tokenize, fork/dup2/execvp per file, wait, report exit status/signal |
| `countnames.c` | counts names in one file (or stdin); prints `name: count` to stdout, warnings/errors to stderr |
| `test/names1.txt`, `test/names2.txt` | given test files |
| `test/big.txt`, `test/one.txt`, `test/blanks.txt`, `test/empty.txt` | our own test files (see below) |

## How to compile

All commands below are run from the **root of the repository** (the folder
that contains `Assignment1/` and `Assignment2/`), same as for A1:

```
gcc -o countnames Assignment2/countnames.c -Wall -Werror
gcc -o shell Assignment2/shell.c -Wall -Werror
```

Both compile with no warnings or errors. No other files are needed. This
puts the two executables in the repo root, which is also where the shell
creates the `PID.out` / `PID.err` files.

(Alternatively `cd Assignment2` first and drop the `Assignment2/` prefix from
every command below.)

## How to run

Start the shell, then type a command at the `%` prompt. Press `Ctrl-D` on an
empty line to exit the shell.

```
$ ./shell
% ./countnames Assignment2/test/names1.txt Assignment2/test/names2.txt
child 484 exited with status 0
child 483 exited with status 0
% 
```

After the command finishes, the results are in the current directory:

```
$ cat 483.out 483.err 484.out 484.err
```

Notes:

- The shell is meant to be run **interactively** from a terminal. The
  argument-less form (`./countnames` alone, which reads names from stdin)
  only works interactively because the child inherits the shell's stdin.
- PIDs are assigned by the OS, so the numbers in the examples below will
  differ on every run. `ls -v *.out *.err` lists the files in PID order.
- The order of names inside a `.out` file is hash-table order (same as A1),
  not alphabetical. Pipe through `sort` to compare with `sort | uniq -c`.
- The order of the `child N exited ...` lines is the order in which the
  children actually finished, which is how you can see the parallelism.
- To remove the result files between runs: `rm -f *.out *.err`

## Test cases

### 1. Two given files (`test/names1.txt`, `test/names2.txt`)

Tests: the basic case from the assignment. Two children are spawned, each
writes its own `PID.out`/`PID.err`. `names1.txt` has an empty line 3, so its
`.err` file gets a warning; `names2.txt` has none, so its `.err` is empty.

```
% ./countnames Assignment2/test/names1.txt Assignment2/test/names2.txt
child 484 exited with status 0
child 483 exited with status 0
```

```
$ cat 483.out            $ cat 483.err
Tom Wu: 3                Warning - file Assignment2/test/names1.txt line 3 is empty.

$ cat 484.out            $ cat 484.err
Tom Wu: 1                (empty)
Jenn Xu: 2
```

### 2. Same file twice (`names1.txt names2.txt names2.txt`)

Tests: a file that appears more than once on the command line is processed
by a **separate** child each time, so three children and three pairs of
files are produced, and the two `names2.txt` results are identical.

```
% ./countnames Assignment2/test/names1.txt Assignment2/test/names2.txt Assignment2/test/names2.txt
child 494 exited with status 0
child 496 exited with status 0
child 495 exited with status 0
```

```
494.out: Tom Wu: 3                 494.err: Warning - file Assignment2/test/names1.txt line 3 is empty.
495.out: Tom Wu: 1 / Jenn Xu: 2    495.err: (empty)
496.out: Tom Wu: 1 / Jenn Xu: 2    496.err: (empty)
```

### 3. Slow file + fast file (`test/big.txt`, `test/one.txt`) — own test

`big.txt` has 50,000 lines drawn from six names (including `Tom Wu`, a name
with a space); `one.txt` has a single line.

Tests: (a) the number of names that can be processed is not limited — 50,000
lines are counted correctly (verify with `sort Assignment2/test/big.txt | uniq -c`);
(b) **no bottleneck** — the tiny file's child (509) finishes and is reported
*before* the big file's child (508) even though it was forked second. If the
shell had done fork/wait/fork/wait, 508 would always be reported first.

```
% ./countnames Assignment2/test/big.txt Assignment2/test/one.txt
child 509 exited with status 0
child 508 exited with status 0
```

```
$ cat 508.out
Bob: 8270
Alice: 8243
Dave: 8360
Carol: 8449
Eve: 8438
Tom Wu: 8240
$ cat 509.out
Solo: 1
```

(Both `.err` files are empty.)

### 4. Empty lines + missing file (`test/blanks.txt`, `test/nofile.txt`) — own test

`blanks.txt` contains `Ada Lovelace`, `Grace Hopper`, `Ada Lovelace` with
empty lines at lines 2, 3, 5 and 7. `nofile.txt` does not exist.

Tests: (a) every empty line produces a `Warning - file X line N is empty.`
message in `PID.err` and is not counted; (b) a child whose `fopen()` fails
writes `error: cannot open file Assignment2/test/nofile.txt` to its `PID.err` and exits
with status **1**, which the parent reports — while the other child still
succeeds (status 0). The parent is not affected by a failing child.

```
% ./countnames Assignment2/test/blanks.txt Assignment2/test/nofile.txt
child 520 exited with status 1
child 519 exited with status 0
```

```
$ cat 519.out                 $ cat 519.err
Grace Hopper: 1               Warning - file Assignment2/test/blanks.txt line 2 is empty.
Ada Lovelace: 2               Warning - file Assignment2/test/blanks.txt line 3 is empty.
                              Warning - file Assignment2/test/blanks.txt line 5 is empty.
                              Warning - file Assignment2/test/blanks.txt line 7 is empty.
$ cat 520.out                 $ cat 520.err
(empty)                       error: cannot open file Assignment2/test/nofile.txt
```

### 5. Empty file (`test/empty.txt`) — own test

Tests: a 0-byte file. The child prints nothing, writes nothing to `.err`, and
exits with status 0 (same behaviour as A1).

```
% ./countnames Assignment2/test/empty.txt
child 530 exited with status 0
```

`530.out` and `530.err` both exist and are empty.

### 6. Command that does not exist

Tests: the shell checks with `access()` that the command is executable
**before** forking. If it is not, it prints one error on the terminal and
creates no children and no `.out`/`.err` files.

```
% ./nonexistent Assignment2/test/one.txt
error: cannot execute ./nonexistent
% 
```

### 7. Empty command line

Tests: pressing Enter (or typing only spaces/tabs) just re-prints the prompt.

```
% 
% 
```

### 8. Child killed by a signal (optional, manual)

Tests: the parent distinguishes a normal exit from a signal. Compile a
program that calls `abort()`, run it through the shell, and the parent
prints `child N was killed by signal 6` (SIGABRT) instead of an exit status.

## Design

`shell.c`: read a line with `fgets`, tokenize it with `strtok` into
`args[]`. `args[0]` is the command; every remaining token is one input
file. For each file the parent calls `fork()`. In the child: build the names
`PID.out`/`PID.err` with `getpid()`, `open()` them, `dup2()` them onto
`STDOUT_FILENO`/`STDERR_FILENO`, close the originals, then
`execvp(args[0], {args[0], file, NULL})`. If `execvp` returns, the child
`_exit(127)`s. The parent never waits inside the fork loop; after the loop
it calls `wait(&status)` once per child, in whatever order they finish, and
prints the exit code (`WIFEXITED`/`WEXITSTATUS`) or the terminating signal
(`WIFSIGNALED`/`WTERMSIG`). `fflush(stdout)` is called before forking so
that the buffered prompt is not duplicated into every child's `PID.out`.

`countnames.c`: unchanged hash-table design from A1. The only A2 changes are
the message formats: `error: cannot open file <name>` now goes to stderr
(so it ends up in `PID.err`) and the empty-line warning includes the file
name (`Warning - file <name> line <n> is empty.`).

# Lessons learned

- `fork()` returns twice: 0 in the child, the child's PID in the parent. All
  code after `fork()` runs in both processes until the child calls `exec`.
- The order fork1; wait1; fork2; wait2 runs the children one after another
  and defeats the purpose of parallelism. Forking everything first and then
  waiting lets the OS schedule all children at once; `wait()` returns
  whichever child finishes first.
- `dup2(fd, STDOUT_FILENO)` makes file descriptor 1 point at our file, so
  `printf` in the exec'd program writes into `PID.out` without the program
  knowing. The redirection survives `exec` because file descriptors are
  inherited.
- The parent's stdio buffer is copied into the child by `fork()`. Without
  `fflush(stdout)` before forking, the un-flushed `% ` prompt appeared at
  the top of every `PID.out`.
- Use `_exit()` (not `exit()`) in a child that failed to exec, so the child
  does not flush the parent's stdio buffers or run the parent's `atexit`
  handlers a second time.
- The status returned by `wait()` is not the exit code; it has to be
  decoded with `WIFEXITED`/`WEXITSTATUS`/`WIFSIGNALED`/`WTERMSIG`.
- `execvp` searches `PATH` and takes a `NULL`-terminated argument array, so
  the same code works for `./countnames` and for commands in `PATH`.
- When stderr is redirected before `exec`, an `execvp` failure message goes
  into `PID.err`, not to the terminal. Checking `access(cmd, X_OK)` before
  forking gives a clearer error and avoids leaving empty output files.
- A child reading from stdin shares the stdin *file position* with the
  parent; if the parent's `fgets` has read ahead (as it does on a pipe),
  the child sees nothing. This is why stdin mode only works interactively.

# References

- Course slides on fork/wait/exec and on decoding the wait status:
  https://docs.google.com/presentation/d/1tFAJHE88J3ylpWa9CZ5dhcAnSy1giA-YiUEUEIJej0Q/edit#slide=id.g27d9461c96e_1_127
- Course slides on `strtok`:
  https://docs.google.com/presentation/d/11fwG5voPoGm-1KZAQ-JGaw1uSSSXVL5V8j7dFOuQ2E8/edit#slide=id.g278e77643e5_0_314
- Course sample code under `code/proc`, `code/ipc`, `code/a2_a3`
- GNU parallel (the idea this assignment mimics): https://www.gnu.org/software/parallel
- `perror()` reference: https://www.tutorialspoint.com/c_standard_library/c_function_perror.htm
- Linux man pages: `man 2 fork`, `man 2 wait`, `man 3 exec`, `man 2 dup2`, `man 2 open`, `man 2 access`
- Stevens & Rago, *Advanced Programming in the UNIX Environment*, ch. 8 (process control) — `shell1.c` is adapted from Figure 1.7

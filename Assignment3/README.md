# CS 149 - Assignment 3: parallel countnames with pipes

**Students:** Erik Thompson, Ryuto Kawabata

A3 extends A2. `shell` still forks **one child process per file argument**,
all children run in parallel, and each child still redirects its stdout to
`PID.out` and its stderr to `PID.err`. What is new is that every child also
sends its `(name, count)` results **back to the shell through a pipe**, and
after all children have finished the shell **sums the counts over all files**
and prints the totals to the terminal. For example:

```
% ./countnames test/names1.txt test/names2.txt
child 440 exited with status 0
child 439 exited with status 0
Jenn Xu: 2
Tom Wu: 4
```

Files:

| File | Purpose |
|---|---|
| `shell.c` | the shell: prompt, tokenize, create pipe, fork/dup2/execvp per file, read results from the pipe, wait, sum and print totals |
| `countnames.c` | counts names in one file (or stdin); prints `name: count` to stdout and, when given a pipe fd, also writes one `NameCountData` struct per name to the pipe |
| `test/names1.txt`, `test/names2.txt` | given test files |
| `test/names3.txt` | our own test file (see below) |

## How to compile

```
gcc -o countnames countnames.c -Wall -Werror
gcc -o shell shell.c -Wall -Werror
```

Both compile with no warnings or errors. No other files are needed.

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
Jenn Xu: 2
Tom Wu: 4
%
```

After the command finishes, each child's own results are still in the
current directory (`439.out`, `439.err`, ...), exactly as in A2. The summed
totals are printed by the shell to the terminal.

Notes:

- PIDs are assigned by the OS, so the numbers in the examples below will
  differ on every run.
- The order of names in the totals (and inside each `.out` file) is not
  alphabetical: inside a child it is hash-table order, and in the parent it
  is the order in which records arrived through the pipe. Pipe through
  `sort` to compare with `cat files... | sort | uniq -c`.
- The order of the `child N exited ...` lines is the order in which the
  children actually finished, which is how you can see the parallelism.
- `countnames` can still be run on its own (`./countnames test/names1.txt`),
  in which case it behaves exactly as in A1/A2 and prints to stdout.
- To remove the result files between runs: `rm -f *.out *.err`

## Test cases

### 1. Two given files (`test/names1.txt`, `test/names2.txt`)

`names1.txt` = `Tom Wu`, `Tom Wu`, (empty line); `names2.txt` = `Tom Wu`,
`Jenn Xu`, `Jenn Xu`, `Tom Wu`.

Tests: the basic A3 case. Two children run in parallel, each writes its own
`PID.out`/`PID.err`, and the parent sums the two partial results
(`Tom Wu` 3 + 1 = 4, `Jenn Xu` 0 + 2 = 2). `names1.txt` has an empty line 3,
so its `.err` gets a warning; the warning does **not** go through the pipe.

```
% ./countnames test/names1.txt test/names2.txt
child 440 exited with status 0
child 439 exited with status 0
Jenn Xu: 2
Tom Wu: 4
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

Tests: aggregation across the maximum of three files. `Tom Wu` appears in
all three files, `Jenn Xu` in two, `Alexa` only in the third, so the parent
must correctly add partial counts from one, two and three children:
`Tom Wu` 3 + 1 + 1 = 5, `Jenn Xu` 0 + 2 + 1 = 3, `Alexa` 0 + 0 + 2 = 2. Three
children are spawned and all three records streams share the same pipe.

```
% ./countnames test/names1.txt test/names2.txt test/names3.txt
child 538 exited with status 0
child 537 exited with status 0
child 539 exited with status 0
Alexa: 2
Jenn Xu: 3
Tom Wu: 5
```

### 3. Missing file + good file (`test/nope.txt`, `test/names1.txt`)

Tests: a child whose `fopen()` fails prints `error: cannot open file` to its
`PID.out` and exits with status **1** (same exit codes as A1); the parent
reports that status, and the other child's results are still received and
summed. A failing child sends nothing through the pipe, so the totals only
contain the good file.

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
**before** forking (same as A2). No children, no pipe reads, no `.out`/`.err`.

```
% cat 540.out
error: cannot execute cat
```

### 5. Empty command line

Tests: pressing Enter (or typing only spaces/tabs) just re-prints the prompt.

## Design

**Pipe.** For each command line the parent calls `pipe(pipe_fd)` **before**
forking, so every child inherits both ends. The parent passes the write end
to `countnames` as a third command-line argument
(`./countnames FILE <fd>`), which survives `execvp` because file descriptors
are inherited. We chose this over `dup2`-ing stdout onto the pipe because
stdout is already `dup2`-ed to `PID.out` (A2 requirement) and we wanted to
keep the per-child output files unchanged.

**Child.** Closes the read end (`close(pipe_fd[0])`), redirects stdout/stderr
to `PID.out`/`PID.err` with `dup2`, then `execvp`s countnames. In
`countnames`, the final output loop does `printf` (into `PID.out`) **and**,
if a pipe fd was given, builds a

```c
typedef struct { char name[31]; int count; } NameCountData;
```

for each name and sends it with one `write(pipe_fd, &data, sizeof(data))`.
The struct is zeroed with `memset` first so no garbage bytes travel through
the pipe. `sizeof(NameCountData)` (36 bytes) is far below `PIPE_BUF` (4096),
so each `write` is atomic and records from different children never
interleave; one shared pipe is therefore enough. We did not send a
`MessageHeader` because all records are of the same type (allowed by the
assignment).

**Parent.** After forking all children it closes its own write end
(`close(pipe_fd[1])`); otherwise `read` would never return EOF, since the
parent itself would still count as a potential writer. It then reads
records with `read_record()`, which loops until a full struct has arrived
(a `read` on a pipe may return fewer bytes than requested), and adds each
record to a linked list of totals with `add_total()` (linear search by
name, create a node on first sight). The parent reads **before** it waits:
if it waited first, a child with many names could block on a full pipe
while the parent sat in `wait()` — a deadlock. EOF arrives once every child
has exited (or closed its write end). Only then does the parent loop on
`wait(&status)` until it returns `-1` (no children left), printing each
child's exit status or signal, and finally prints the totals and frees the
list.

**Parallelism (unchanged from A2).** The parent never waits inside the fork
loop, so all children are running at once and `wait()` returns whichever
finishes first.

# Lessons learned

- A pipe is just two file descriptors; `fork()` copies them, and because
  they also survive `exec`, a child program can be told "write to fd 5"
  and it just works. Unix I/O really is "everything is a file descriptor".
- Closing unused pipe ends is not optional. The first version of the
  parent never got EOF from `read()` because it had forgotten to close its
  own write end — the kernel only reports EOF when **every** write end in
  **every** process is closed.
- The order "read everything, then wait" matters. Waiting first can
  deadlock when a child fills the pipe buffer and blocks on `write()`
  while the parent blocks on `wait()`.
- `read()` on a pipe can return fewer bytes than asked for, so receiving a
  struct needs a loop that accumulates until `sizeof(struct)` bytes have
  arrived.
- Writes of at most `PIPE_BUF` bytes are atomic, which is why several
  children can safely share one pipe as long as each record is written in a
  single `write()` call. Writing the name and the count in two separate
  `write()`s would have allowed records from different children to
  interleave.
- Sending a struct through a pipe sends its raw bytes, padding included, so
  zero the struct before filling it; and make sure both programs use the
  exact same struct definition.
- `wait()` returning `-1` is the natural "no more children" signal, so the
  parent does not even need to count how many children it forked.
- Keeping the old `printf` output next to the new pipe output meant the A2
  `PID.out` files still work, and `countnames` is still usable on its own.

# References

- Assignment 3 description (pipes, `NameCountData`, `PIPE_BUF` atomicity,
  closing unused pipe ends) on Canvas.
- Course slides on fork/wait/exec and on decoding the wait status:
  https://docs.google.com/presentation/d/1tFAJHE88J3ylpWa9CZ5dhcAnSy1giA-YiUEUEIJej0Q/edit#slide=id.g27d9461c96e_1_127
- Course slides on `fprintf`/`fopen`:
  https://docs.google.com/presentation/d/11fwG5voPoGm-1KZAQ-JGaw1uSSSXVL5V8j7dFOuQ2E8/edit#slide=id.g14898c52193_1_155
- Course sample code under `code/ipc` (fork, pipe, wait examples)
- `perror()` reference: https://www.tutorialspoint.com/c_standard_library/c_function_perror.htm
- Linux man pages: `man 2 pipe`, `man 7 pipe` (PIPE_BUF, EOF semantics),
  `man 2 read`, `man 2 write`, `man 2 fork`, `man 2 wait`, `man 3 exec`,
  `man 2 dup2`
- Stevens & Rago, *Advanced Programming in the UNIX Environment*, ch. 8
  (process control) and ch. 15 (interprocess communication: pipes)

# Acknowledgements

- Class lectures and the assignment description.
- Our Assignment2 submission, which this assignment extends.

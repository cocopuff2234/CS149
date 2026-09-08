# CS 149 - Assignment 1: countnames

**Students:** Erik Thompson, Ryuto Kawabata

`countnames` reads a list of names (one name per line) from a file given on the
command line, or from stdin if no file is given, and prints how many times each
distinct name appears. Empty lines are skipped and reported on stderr.

## How to compile

```
gcc -o countnames countnames.c -Wall -Werror
```

This produces a single executable named `countnames` with no warnings or errors.

## How to run

With a filename (argc == 2):

```
./countnames test/names.txt
```

From stdin (argc == 1):

```
cat test/names.txt | ./countnames
./countnames < test/names.txt
```

To see only stdout: `./countnames test/names.txt 2> /dev/null`
To see only stderr: `./countnames test/names.txt > /dev/null`
To check the exit code: `echo $?`

## Test cases

### 1. `test/names.txt` (provided)

Tests: the basic case — repeated names, names containing spaces, and empty
lines (line 2 and the trailing empty line) that must be ignored and reported.

```
$ ./countnames test/names.txt
Warning - Line 2 is empty.
Warning - Line 9 is empty.
Nicky: 1
Dave Joe: 2
John Smith: 1
Yuan Cheng Chang: 3
$ echo $?
0
```

(The two `Warning` lines go to stderr; the counts go to stdout. Output order
is hash-table order, not alphabetical.)

### 2. `test/empty.txt` (own test)

Tests: a file with no names at all (0 bytes). The program should print
nothing and exit with status 0.

```
$ ./countnames test/empty.txt
$ echo $?
0
```

### 3. `test/whitespace.txt` (own test)

Contents (`.` shown here for a space):

```
Tom Wu
Tom..Wu
.
..
Tom Wu
```

Tests: whitespace is part of a name. `Tom Wu` (one space) and `Tom  Wu` (two
spaces) are different names, and a line containing only one space or only two
spaces is a valid (non-empty) name, not an empty line.

```
$ ./countnames test/whitespace.txt
  : 1
 : 1
Tom Wu: 2
Tom  Wu: 1
$ echo $?
0
```

### 4. `test/no_trailing_newline.txt` (own test)

Contents: `Ann`, `Bob`, `Ann` with **no** newline after the last line.

Tests: the last line is still read and counted correctly even when the file
does not end with `\n` (fgets returns it without a newline).

```
$ ./countnames test/no_trailing_newline.txt
Bob: 1
Ann: 2
$ echo $?
0
```

### 5. `test/big.txt` (own test)

Contents: 5000 lines, each one of `Alice`, `Bob`, `Carol`, `Dave`, `Eve`.

Tests: the number of input lines is not limited — a few thousand lines are
processed correctly. The result can be verified against
`sort test/big.txt | uniq -c`.

```
$ ./countnames test/big.txt | sort
Alice: 1041
Bob: 1058
Carol: 959
Dave: 976
Eve: 966
```

(Exact counts depend on the generated file; they must match `uniq -c`.)

### 6. Missing file

Tests: fopen() failure. The program prints the exact error message and exits
with status 1.

```
$ ./countnames does_not_exist.txt
error: cannot open file
$ echo $?
1
```

### 7. No arguments, no input

Tests: running with no parameters (e.g. to check the program is installed).

```
$ ./countnames < /dev/null
$ echo $?
0
```

## Design

Names are stored in a hash table with separate chaining. `hash()` maps a name
to one of `TABLE_SIZE` (101) buckets; each bucket is a singly linked list of
`struct Entry { char name[31]; int count; struct Entry *next; }`. For each
input line we hash it, walk only that bucket with `strcmp`, and either
increment the count or `malloc` a new entry and push it on the front of the
list. Entries are allocated dynamically, so the number of distinct names is
not limited to 100. All entries are `free`d after printing.

The output order is the bucket order of the hash table, so it is not
alphabetical; pipe through `sort` to compare with `sort | uniq -c`.

# Lessons learned

- C has no string type: a string is a `char` array ending in `'\0'`, so a
  30-character name needs a 31-byte buffer. `strncpy` does not always
  terminate the string, so we terminate it explicitly.
- `fgets` keeps the trailing `'\n'` in the buffer. It has to be replaced with
  `'\0'` before comparing names, otherwise `"Nicky\n"` and `"Nicky"` would be
  treated as different names. `line[strcspn(line, "\n")] = '\0'` does this in
  one line and is safe even when there is no newline.
- The line buffer should be larger than the longest expected name: with a
  32-byte buffer, a longer-than-expected line is split by `fgets` into two
  "lines", which shifts the line numbers and creates bogus names.
- A hash table turns the name lookup from a scan of every name into a scan of
  one short bucket. Different names can land in the same bucket (a collision),
  which is why each bucket is a linked list and we still need `strcmp`.
- Memory from `malloc` must be released with `free`; freeing while walking a
  list requires saving `next` before freeing the current node.
- `stdin` is just a `FILE *`, so reading from a file and from a pipe can share
  the same loop by pointing `file` at either `fopen(...)` or `stdin`.
- Text files normally end with a newline, and what looks like an empty last
  line in an editor may or may not be a real line; `od -a` shows the truth.
- stdout and stderr are separate streams, so warnings can be redirected away
  with `2> /dev/null` while keeping the results, and the exit code (`echo $?`)
  is independent of what is printed.

# References

- fgets() reference: https://www.tutorialspoint.com/c_standard_library/c_function_fgets.htm
- Removing the trailing newline from fgets: https://aticleworld.com/remove-trailing-newline-character-from-fgets/
- perror() reference: https://www.tutorialspoint.com/c_standard_library/c_function_perror.htm
- Linux man pages: `man fopen`, `man fgets`, `man strcmp`, `man strncpy`

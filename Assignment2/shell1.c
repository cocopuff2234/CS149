#include <sys/wait.h>
// include standard input/output library
#include <stdio.h>
// include string to use strlen()
#include <string.h>

// define maxline since we arent using apue.h
#define MAXLINE 1024
// define maxargs, the max number of words/files in a command
#define MAXARGS 100

int
main(void)
{
	char	buf[MAXLINE];	
	pid_t	pid;
	int		status;

	printf("%% ");	/* print prompt (printf requires %% to print %) */
	while (fgets(buf, MAXLINE, stdin) != NULL) {
		if (buf[strlen(buf) - 1] == '\n')
			buf[strlen(buf) - 1] = 0; /* replace newline with null */

		// wip
		if ((pid = fork()) < 0) {
			err_sys("fork error");
		} else if (pid == 0) {		/* child */
			execlp(buf, buf, (char *)0);
			err_ret("couldn't execute: %s", buf);
			exit(127);
		}

		// store separated words
		char *args[MAXARGS];

		// initialize a count for the number of arguments typed
		int arg_count = 0;

		// strtok tokenizes the input, stored in token
		char *token = strtok(buf, " ");

		// iterate through input that is not null.
		while (token != NULL && arg_count < MAXARGS -1){
			// store token
			args[arg_count] = token;

			// increment count
			arg_count++;

			// get next token
			token = strtok(NULL, " \t");
		}

		// end of argument array
		args[arg_count] = NULL;

		// edge case
		if (arg_count == 0) {
			printf("%% ");
			fflush(stdout);
			continue;
		}


		/* parent */
		if ((pid = waitpid(pid, &status, 0)) < 0)
			err_sys("waitpid error");
		printf("%% ");
	}
	exit(0);
}

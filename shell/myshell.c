
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <signal.h>

#define parallel_symbol "&"
#define pipe_symbol "|"

#define is_parallel_cmd(c) (strcmp(c,parallel_symbol) == 0)
#define handle_error_exit(msg)\
	perror(msg);\
	exit(1);
#define handle_error_return(msg)\
	perror(msg);\
	return(0);

typedef enum { IGNORE, DEFAULT } handle_operation;


/*if the cmd given is a pipe cmd - return the pipe simbol index.
 *else , return -1*/
char is_pipe_cmd(int count, char** arglist)
{
	char** arglist_runner = arglist;
	int idx = 0;
	for (; idx < count; idx++) {
		if (strcmp(*arglist_runner, pipe_symbol) == 0) {
			return idx;
		}
		arglist_runner++;
	}
	return -1;
}

char sigint_handler(handle_operation op)
{
	struct sigaction sh;
	if (op == DEFAULT) {
		sh.sa_handler = SIG_DFL;
	}
	else if (op == IGNORE) {
		sh.sa_handler = SIG_IGN;
	}
	if (sigaction(SIGINT, &sh, 0) == -1) {
		perror(0);
		return 1;
	}
	return 0;
}

int execute_parallel_cmd(int count, char** arglist)
{
	int pid = fork();
	if (pid == -1) {
		handle_error_return("fork failed in execute_parallel_cmd: ")
	}
	/*child process*/
	if (pid == 0) {
		arglist[count - 1] = NULL; /*removing parallel symbol*/
		execvp(arglist[0], arglist);
		handle_error_exit("from execute_parallel_cmd: ")
	}
	/*parent shouldn't wait for child to end*/
	/*this is were zombie_killer is taking action*/
	return 1;
}

int execute_regular_cmd(int count, char** arglist)
{
	int pid = fork();
	if (pid == -1) {
		handle_error_return("fork failed in execute_regular_cmd: ")
	}
	/*child process*/
	if (pid == 0) {
		/*setting the child process to terminate on SIGINT*/
		if (sigint_handler(DEFAULT)) {
			handle_error_exit("from execute_regular_cmd: ")
		}
		execvp(arglist[0], arglist);
		handle_error_exit("from execute_regular_cmd: ")
	}
	/*parent process*/
	else {
		int status;
		if (waitpid(pid, &status, WUNTRACED) == -1){
			if (errno != ECHILD) {
				handle_error_return("wait failed: ")
			}
		}
		return 1;
	}
}

int execute_pipe_cmd(int count, char** arglist, int pipe_sym_idx)
{
	char** write_end_cmd = arglist;
	arglist[pipe_sym_idx] = NULL;
	char** read_end_cmd = arglist + pipe_sym_idx + 1;
	/*initialize pipe*/
	int pipe_fd[2];
	if (pipe(pipe_fd) == -1) {
		handle_error_return("pipe failed: ")
	}
	int pid_write_end = fork();
	if (pid_write_end == -1) {
		handle_error_return("fork failed in execute_regular_cmd: ")
	}
	/*child process*/
	if (pid_write_end == 0) {
		/*setting the child process to terminate on SIGINT*/
		if (sigint_handler(DEFAULT)) {
			handle_error_exit("from execute_regular_cmd: ")
		}
		close(pipe_fd[0]); /*close read end of the pipe*/
		/*change the output of the first process to pipe_fd[1]
		 A loop is needed to allow for the possibility of dup2 being interrupted by a signal */
		while ((dup2(pipe_fd[1], STDOUT_FILENO) == -1) && (errno == EINTR)) {} /*EINTR = if a signal occurred while the system call*/
		close(pipe_fd[1]); /*close write end of the pipe*/
		execvp(write_end_cmd[0], write_end_cmd);
		handle_error_exit("from execute_pipe_cmd: ")
	}
	/*parent process*/
	else {
		int pid_read_end = fork();
		if (pid_read_end == -1) {
			handle_error_return("fork failed in execute_regular_cmd: ")
		}
		/*child process*/
		if (pid_read_end == 0) {
			/*setting the child process to terminate on SIGINT*/
			if (sigint_handler(DEFAULT)) {
				handle_error_exit("from execute_regular_cmd: ")
			}
			close(pipe_fd[1]); /*close write end of the pipe*/
			/*change the input of the second process to pipe_fd[0].
			 A loop is needed to allow for the possibility of dup2 being interrupted by a signal */
			while ((dup2(pipe_fd[0], STDIN_FILENO) == -1) && (errno == EINTR)) {} /*EINTR = if a signal occurred while the system call*/
			close(pipe_fd[0]); /*close read end of the pipe*/
			execvp(read_end_cmd[0], read_end_cmd);
			handle_error_exit("from execute_pipe_cmd: ")
		}
		/*parent process*/
		else {
			close(pipe_fd[0]);
			close(pipe_fd[1]);
			//while (-1 != wait(0)); /*wait for the child processes*/
			if (waitpid(pid_write_end, 0, WUNTRACED) == -1 || waitpid(pid_read_end, 0, WUNTRACED) == -1) {
				if (errno != ECHILD) {
					handle_error_return("wait failed: ")
				}
			}
			return 1;
		}
	}
}

void zombie_killer(int signum)
{
	int errno_before_waitpid = errno;
	/*WNOHANG prevent the handler from blocking*/
	while (waitpid((pid_t)(-1), 0, WNOHANG) > 0) {}
	errno = errno_before_waitpid;
}

char zombie_handler()
{
	/*kill zombie processea by waiting on them*/
	struct sigaction sh;
	sh.sa_handler = &(zombie_killer);
	/*SA_NOCLDSTOP invoke the handler only when a child terminate
	 *SA_RESTART restart the handler if another signal raised*/
	sh.sa_flags = SA_NOCLDSTOP | SA_RESTART;
	if (sigaction(SIGCHLD, &sh, 0) == -1) {
		perror(0);
		return 1;
	}
	return 0;
}

int prepare(void)
{
	/*setting zombie killer*/
	if (zombie_handler()) {
		return 1;
	}
	/*setting parent (shell) to ignore SIGINT*/
	if (sigint_handler(IGNORE)) {
		return 1;
	}
	return 0;
}

int process_arglist(int count, char** arglist)
{
	int status = 1;
	int pipe_idx = is_pipe_cmd(count, arglist);
	if (pipe_idx != -1) {
		status = execute_pipe_cmd(count, arglist, pipe_idx);
	}
	else if (is_parallel_cmd(arglist[count - 1])) {
		status = execute_parallel_cmd(count, arglist);
	}
	else { /*regular execution*/
		status = execute_regular_cmd(count, arglist);
	}
	return status;
}

int finalize(void) {
	return 0;
}
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>

#define NUM_CHILDREN 3
#define BUF_SIZE 256

/*R4: interrupt (Ctrl + C) handling*/

static volatile sig_atomic_t interrupted = 0;

void handle_sigint (int sig)
{
	(void)sig;
	interrupted = 1;
	const char msg[] =
		"\n[PARENT] Interrupt detected (Ctrl + C)! Closing pipes and stopping children...\n";
	ssize_t r = write(STDOUT_FILENO, msg, sizeof(msg) - 1);
	(void)r;
}

/* R3: Child received and displays the message */

void child_work(int id, int read_fd)
{
	char buf[BUF_SIZE];
	ssize_t n;

	/* Child ignores Ctrl + C so only the parent prints the interrupt message*/

	signal(SIGINT, SIG_IGN);


	while ((n = read(read_fd, buf, sizeof(buf) - 1)) != 0) {
		if (n < 0) {
			if (errno == EINTR) continue;
			perror("read");
			break;
		}
		buf[n] = '\0';
		printf("[Child %d | PID %d] Received message: %s", id, getpid(), buf);
		fflush(stdout);
	}

	printf("[Child %d | PID %d] Pipe closed. Exiting.\n", id, getpid());
	close(read_fd);
	exit(0);
}

int main(void)
{
	int pipefd[NUM_CHILDREN][2];
	pid_t pids[NUM_CHILDREN];
	char buffer[BUF_SIZE];
	int i,j;

	/*Install the Ctrl + C handler */

	struct sigaction sa;
	sa.sa_handler = handle_sigint;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = 0;
	sigaction(SIGINT, &sa, NULL);

	printf("Parent PID %d starting. Creating %d children...\n", getpid(), NUM_CHILDREN);


	/* R1:  Fork into multiple children + pipes */

	for (i = 0; i < NUM_CHILDREN; i++) {
		if (pipe(pipefd[i]) == -1) {
			perror("pipe");
			exit(1);
		}

		fflush(stdout);
		pids[i] = fork();
		if (pids[i] < 0) {
			perror("fork");
			exit(1);
		}

		if (pids[i] == 0) {
			for (j = 0; j <= i; j++)
				close(pipefd[j][1]);
			child_work(i + 1, pipefd[i][0]);
		}

		close(pipefd[i][0]);
		pipefd[i][0] = -1;
	}


	/* R2: Parent gets the message from the keyboard*/

	while (!interrupted) {
		printf("\nEnter a message for the children (type 'exit' to quit): ");
		fflush(stdout);

		if (fgets(buffer, sizeof(buffer), stdin) == NULL)
			break;

		if (strcmp(buffer, "exit\n") == 0)
			break;

		/* R1: Message passing parent -> every child through its pipe */

		for (i = 0; i < NUM_CHILDREN; i++) {
			if (write(pipefd[i][1], buffer, strlen(buffer)) == -1)
				perror("write");
		}
		usleep(200000);
	}

	for (i = 0; i < NUM_CHILDREN; i++)
		close(pipefd[i][1]);

	for (i = 0; i<NUM_CHILDREN; i++)
		waitpid(pids[i], NULL, 0);

	if (interrupted)
		printf("[PARENT] Program terminated safely after interrupted.\n");
	else
		printf("[PARENT] All children finished. Program ended normally.\n");

	return 0;
}

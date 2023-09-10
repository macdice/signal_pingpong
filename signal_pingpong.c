#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <unistd.h>

#define NUM_PING_PONGS 1000000

static int self_pipe[2];
static pid_t child_pid;

volatile int x;

/*
 * Sleep for a very short random before the poll() and kill(), hoping to find
 * the timing required for the race case.
 */
static void
random_sleep(void)
{
	/*
	struct timespec ts = {
		.tv_nsec = random() % 10000
	};
	nanosleep(&ts, NULL);
	*/
	int busy_loops = random() % 1000;
	for (int i = 0; i < busy_loops; ++i)
		x = i;	/* something the compiler can't optimise away */
}

static void
child_main(void)
{
	pid_t parent_pid = getppid();
	int rc;
	int received_pongs = 0;

	rc = pipe2(self_pipe, O_NONBLOCK);
	assert(rc == 0);

	while (received_pongs < NUM_PING_PONGS)
	{
		struct pollfd pollfd = {
			.fd = self_pipe[0],
			.events = POLLIN
		};

		/* Send a ping! */
		random_sleep();
		kill(parent_pid, SIGUSR1);

		/* Wait for a pong. */
		random_sleep();
		for (;;) {
			if (poll(&pollfd, 1, -1) == 1) {
				char c;

				if (read(self_pipe[0], &c, 1) == 1) {
					received_pongs++;
					break;
				}
			}
		}
	}
}

static void
start_child(void)
{
	sigset_t mask;
	sigset_t save_mask;

	sigfillset(&mask);
	sigprocmask(SIG_BLOCK, &mask, &save_mask);
	child_pid = fork();
	assert(child_pid >= 0);
	sigprocmask(SIG_SETMASK, &save_mask, NULL);
	if (child_pid == 0) {
	 	/* Close the parent's self-pipe. */
		close(self_pipe[0]);
		close(self_pipe[1]);
		child_main();
		exit(0);
	}
}

static void
sigalrm_handler(int signo)
{
	int save_errno = errno;
	start_child();
	errno = save_errno;
}

static void
sigusr1_handler(int signo)
{
	int save_errno = errno;
	int rc = write(self_pipe[1], ".", 1);
	assert(rc == 1);
	errno = save_errno;
}

int
main(int argc, char *argv[])
{
	struct sigaction sa;
	struct itimerval it;
	int rc;
	int received_pings = 0;
	int status;

	rc = pipe2(self_pipe, O_NONBLOCK);
	assert(rc == 0);

	/* Register a couple of handlers */
	sa.sa_handler = sigalrm_handler;
	sigemptyset(&sa.sa_mask);
	sa.sa_flags = SA_RESTART;
	sigaction(SIGALRM, &sa, NULL);
	sa.sa_handler = sigusr1_handler;
	sigaction(SIGUSR1, &sa, NULL);

	/*
	 * Request SIGALRM quite soon.
	 */
	it.it_interval.tv_sec = 0;
	it.it_interval.tv_usec = 0;
	it.it_value.tv_sec = 0;
	it.it_value.tv_usec = 1;
	setitimer(ITIMER_REAL, &it, NULL);

	while (received_pings < NUM_PING_PONGS)
	{
		struct pollfd pollfd = {
			.fd = self_pipe[0],
			.events = POLLIN
		};

		/* Wait for a ping. */
		random_sleep();
		if (poll(&pollfd, 1, -1) == 1) {
			char c;

			if (read(self_pipe[0], &c, 1) == 1) {
				received_pings++;

				/* Send a pong! */
				random_sleep();
				kill(child_pid, SIGUSR1);
			}
		}
	}

	waitpid(child_pid, &status, 0);
}


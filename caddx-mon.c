#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <signal.h>
#include <errno.h>
#include <sys/wait.h>

#include "caddx.h"
#include "util.h"
#include "/home/ninkid/bin/hex-libc.c"

#define DEFAULT_HOST	"127.0.0.1:1587"

int errline = 0, fg;
static char *notify_proc = NULL;
static uint32_t part_sirened = 0;

static void caddx_signal(int signum)
{
	int to = 10;

	if (signum == SIGCHLD) {
		while (waitpid(-1, NULL, WNOHANG) >= 0 && --to) {}
	} else if (signum == SIGINT)
		quit = 1;
}

static void
usage(void)
{
	printf("\
Usage: caddx-mon [options]\n\
-e ...: Execute ... upon event occurences\n\
        The environment will contain information about the event:\n\
        TYPE: zone or part\n\
        ID: zone or partition number\n\
        EVENT: active/inactive or siren\n\
-f    : Run in foreground\n\
-H ...: Host to connect to\n\
-p ...: Partition to poll\n\
-v    : Increase logging\n\
");
}

static int
proc_notify(const char *type, int _id, const char *event)
{
	int pid;
	char *argv[2], id[16];

	if (!notify_proc)
		return -1;

	argv[0] = notify_proc;
	argv[1] = NULL;

	if ((pid = fork()) < 0) {
		err("%s", strerror(errno));
		return -1;
	}

	if (pid > 0)
		return 0;

	setsid();
	sprintf(id, "%d", _id);
	setenv("TYPE", type, 1);
	setenv("ID", id, 1);
	setenv("EVENT", event, 1);

	if (loglevel == 0) {
		freopen("/dev/null", "r", stdin);
		freopen("/dev/null", "w", stdout);
		freopen("/dev/null", "w", stderr);
	}

	execv(argv[0], argv);
	exit(-1);
}

void
caddx_parse(int fd, uint8_t *buf, uint32_t len)
{
	struct caddx_msg *msg = (struct caddx_msg *)buf;

	switch (msg->type) {
	case CADDX_ZONE_STATUS: {
		struct caddx_zone_status *status = (struct caddx_zone_status *)buf;
		if (len != sizeof(*status))
			goto error;
		if (status->faulted || status->tampered || status->trouble) {
			proc_notify("zone", status->zone + 1, "active");
			warn("zone %d activity\n", status->zone + 1);
		} else {
			proc_notify("zone", status->zone + 1, "inactive");
			warn("zone %d ok\n", status->zone + 1);
		}
		break;
	}
	case CADDX_PART_STATUS: {
		struct caddx_part_status *status = (struct caddx_part_status *)buf;
		if (status->siren_on) {
			if (!(part_sirened & (1 << status->part))) {
				proc_notify("part", status->part + 1, "siren");
				part_sirened |= (1 << status->part);
			}
		} else {
			if (part_sirened & (1 << status->part)) {
				proc_notify("part", status->part + 1, "siren_off");
				part_sirened &= ~(1 << status->part);
			}
		}
		break;
	}
	default:
	error:
		if (loglevel >= 1)
			hexdump(buf, len);
		break;
	}
}

int
main(int argc, char *argv[])
{
	int i, fd = -1, poll_part = 0;
	char *host = strdup(DEFAULT_HOST), *port;
	int part_status_count = 1, part_status_freq = 30;
	uint8_t buf[128], len;
	struct addrinfo gai = { 0 }, *ai, *pai;
	struct sigaction action;

	while ((i = getopt(argc, argv, "e:fH:v")) != -1) {
		switch (i) {
		case 'e': notify_proc = optarg; break;
		case 'f': fg = 1; break;
		case 'H': free(host); host = strdup(optarg); break;
		case 'p': poll_part = strtol(optarg, NULL, 0) - 1; break;
		case 'v': loglevel++; break;
		default: usage(); return -1;
		}
	}

	if ((port = rindex(host, ':')) == NULL)
		ERR(EINVAL);
	*(port++) = 0;

	memset(&action, 0, sizeof(action));
	action.sa_handler = caddx_signal;
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGCHLD, &action, NULL);

	gai.ai_family = AF_UNSPEC;
	gai.ai_socktype = SOCK_STREAM;
	if ((i = getaddrinfo(host, port, (const struct addrinfo *)&gai, &ai)) != 0)
		ERR(i);

	if (!fg) {
		log_syslog = 1;
		if ((i = fork()) < 0)
			ERR(errno);
		if (i != 0)
			goto error;
		setsid();
	}

	for (pai = ai; pai; pai = pai->ai_next) {
		if ((fd = socket(pai->ai_family, pai->ai_socktype, pai->ai_protocol)) < 0)
			continue;

		i = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

		if (connect(fd, pai->ai_addr, pai->ai_addrlen) < 0) {
			close(fd);
			continue;
		}
		break;
	}
	freeaddrinfo(ai);
	if (!pai) {
		if (!errno)
			errno = EINVAL;
		ERR(errno);
	}

	while (!quit) {
		fd_set fds;
		struct timeval tv;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);

		tv.tv_sec = 1;
		tv.tv_usec = 0;
		if ((i = select(fd + 1, &fds, NULL, NULL, &tv)) < 0) {
			if (errno == EINTR)
				continue;
			ERR(-errno);
		}

		if (i == 0) {
			if (!--part_status_count) {
				buf[0] = 2;
				buf[1] = CADDX_PART_STATUS_REQ,
				buf[2] = poll_part;
				full_write(fd, buf, 3, 1);
				part_status_count = part_status_freq;
			}
			continue;
		}

		if (full_read(fd, &len, 1, 1) != 1)
			ERR(-EIO);
		if (len > sizeof(buf))
			ERR(-EIO);
		if (full_read(fd, buf, len, 1) != len)
			ERR(-EIO);
		caddx_parse(fd, buf, len);
	}

 error:
	if (host) free(host);
	if (fd >= 0) close(fd);
	if (errno && errline) {
		err("%s: error: %s @%d\n", __func__, strerror(errno), errline);
		errno = errline = 0;
		return -1;
	}
 
	return 0;
}

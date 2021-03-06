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
#ifdef HEXDUMP
#include "hex-libc.c"
#endif

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
-P ...: Use PIN for primary function\n\
-p ...: Partition to poll or perform function on (default 1)\n\
-v    : Increase logging\n\
-x ...: Primary function\n\
     0: Turn off sounder/alarm\n\
     1: Disarm\n\
     2: Arm in away mode\n\
     3: Arm in stay mode\n\
     4: Cancel\n\
     5: Initiate auto-arm\n\
     6: Start walk-test mode\n\
     6: Stop walk-test mode\n\
-X ...: Secondary function\n\
     0: Stay\n\
     1: Chime\n\
     2: Exit\n\
     3: Bypass Interiors\n\
     4: Fire Panic\n\
     5: Medical Panic\n\
     6: Police Panic\n\
     7: Smoke Detector reset\n\
     8: Auto callback download\n\
     9: Manual pickup download\n\
    10: Enable silent exit\n\
    11: Perform test\n\
    12: Group bypass\n\
    13: Aux function 1\n\
    14: Aux function 2\n\
    15: Start keypad sounder\n\
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
#ifdef HEXDUMP
		if (loglevel >= 1)
			hexdump(buf, len);
#endif
		break;
	}
}

static int
caddx_rx_pkt(int fd, uint8_t *buf, uint8_t *maxlen)
{
	uint8_t len = 0;

	errno = 0;
	if (full_read(fd, &len, 1, 1) != 1)
		ERR(-EIO);
	if (len > *maxlen)
		ERR(-EIO);
	if (full_read(fd, buf, len, 1) != len)
		ERR(-EIO);
	*maxlen = len;

	/* FALLTHROUGH */
 error:
	if (errno)
		return -1;
	else return 0;
}

static int
caddx_rx_pkt_type(int fd, uint8_t *buf, uint8_t *maxlen, uint8_t type)
{
	uint32_t _maxlen = *maxlen;
	struct caddx_msg *msg = (struct caddx_msg *)buf;
	msg->type = 0x3f;
	while (!quit && msg->type != type) {
		*maxlen = _maxlen;
		if (caddx_rx_pkt(fd, buf, maxlen) < 0)
			return -1;
	}
	return 0;
}

static int
caddx_zone_status(int fd, int zone, uint8_t *buf, uint8_t *maxlen)
{
	struct caddx_zone_status_req *req = (struct caddx_zone_status_req *)&buf[1];
	errno = 0;
	buf[0] = sizeof(*req);
		memset(req, 0, sizeof(*req));
	req->msg.type = CADDX_ZONE_STATUS_REQ;
	req->zone = zone;
	if (full_write(fd, buf, 1 + sizeof(*req), 1) < 0)
		ERR(errno);
	if (caddx_rx_pkt_type(fd, buf, maxlen, CADDX_ZONE_STATUS) < 0)
		ERR(errno);

	/* FALLTHROUGH */
 error:
	if (errno)
		return -1;
	else return 0;
}

int
main(int argc, char *argv[])
{
	int i, fd = -1, poll_part = 0, pri_fn = -1, sec_fn = -1, pin = -1;
	int do_status = 0;
	struct timeval tv;
	int bypass = -1, no_bypass = -1;
	char *host = strdup(DEFAULT_HOST), *port;
	int part_status_count = 1, part_status_freq = 30;
	uint8_t buf[128], len;
	struct addrinfo gai = { 0 }, *ai, *pai;
	struct sigaction action;

	while ((i = getopt(argc, argv, "B:b:e:fH:P:svX:x:")) != -1) {
		switch (i) {
		case 'b': bypass = strtol(optarg, NULL, 0) - 1; fg = 1; break;
		case 'B': no_bypass = strtol(optarg, NULL, 0) - 1; fg = 1; break;
		case 'e': notify_proc = optarg; break;
		case 'f': fg = 1; break;
		case 'H': free(host); host = strdup(optarg); break;
		case 'P': pin = strtol(optarg, NULL, 10); break;
		case 'p': poll_part = strtol(optarg, NULL, 0) - 1; break;
		case 's': do_status = 1; fg = 1; break;
		case 'v': loglevel++; break;
		case 'X': sec_fn = strtol(optarg, NULL, 0); fg = 1; break;
		case 'x': pri_fn = strtol(optarg, NULL, 0); fg = 1; break;
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

	tv.tv_sec = 1;
	tv.tv_usec = 0;
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	if (pri_fn >= 0) {
		if (pin >= 0) {
			struct caddx_keypad_func0 func = {{ 0 }};
			func.msg.type = CADDX_KEYPAD_FUNC0;
			func.msg.ack = 1;
			if (pin > 9999) {
				func.pin1 = (pin / 100000) % 10;
				func.pin2 = (pin /  10000) % 10;
				func.pin3 = (pin /   1000) % 10;
				func.pin4 = (pin /    100) % 10;
				func.pin5 = (pin /     10) % 10;
				func.pin6 = (pin /      1) % 10;
			} else {
				func.pin1 = (pin / 1000) % 10;
				func.pin2 = (pin /  100) % 10;
				func.pin3 = (pin /   10) % 10;
				func.pin4 = (pin /    1) % 10;
			}
			func.function = pri_fn;
			func.part = (1 << poll_part);
			buf[0] = sizeof(func);
			if (full_write(fd, buf, 1, 1) < 0)
				ERR(errno);
			if (full_write(fd, (uint8_t *)&func, sizeof(func), 1) < 0)
				ERR(errno);
			goto error;
		} else {
			struct caddx_keypad_func0_nopin func = {{ 0 }};
			func.msg.type = CADDX_KEYPAD_FUNC0_NOPIN;
			func.msg.ack = 1;
			func.function = pri_fn;
			func.part = (1 << poll_part);
			buf[0] = sizeof(func);
			if (full_write(fd, buf, 1, 1) < 0)
				ERR(errno);
			if (full_write(fd, (uint8_t *)&func, sizeof(func), 1) < 0)
				ERR(errno);
			goto error;
		}
	} else if (sec_fn >= 0) {
		struct caddx_keypad_func1 func = {{ 0 }};
		func.msg.type = CADDX_KEYPAD_FUNC1;
		func.msg.ack = 1;
		func.function = sec_fn;
		func.part = (1 << poll_part);
		buf[0] = sizeof(func);
		if (full_write(fd, buf, 1, 1) < 0)
			ERR(errno);
		if (full_write(fd, (uint8_t *)&func, sizeof(func), 1) < 0)
			ERR(errno);
		goto error;
	} else if (bypass >= 0 || no_bypass >= 0) {
		struct caddx_zone_status *status = (struct caddx_zone_status *)buf;
		struct caddx_bypass_toggle *toggle = (struct caddx_bypass_toggle *)&buf[1];
		uint8_t zone = (bypass >= 0) ? bypass : no_bypass;
		len = sizeof(buf);
		if (caddx_zone_status(fd, zone, buf, &len) < 0)
			ERR(errno);
		if ((bypass >= 0 && status->bypassed) ||
		    (no_bypass >= 0 && !status->bypassed))
			goto error;

		buf[0] = sizeof(*toggle);
		memset(toggle, 0, sizeof(*toggle));
		toggle->msg.type = CADDX_BYPASS_TOGGLE;
		toggle->zone = zone;
		if (full_write(fd, buf, 1 + sizeof(*toggle), 1) < 0)
			ERR(errno);

		len = sizeof(buf);
		if (caddx_zone_status(fd, zone, buf, &len) < 0)
			ERR(errno);
		if ((bypass >= 0 && status->bypassed) ||
		    (no_bypass >= 0 && !status->bypassed))
			goto error;

		printf("could not (un)bypass\n");
		ERR(EIO);
	} else if (do_status) {
		struct caddx_part_status *status = (struct caddx_part_status *)buf;
		struct caddx_part_status_req *req = (struct caddx_part_status_req *)&buf[1];

		buf[0] = sizeof(*req);
		memset(req, 0, sizeof(*req));
		req->msg.type = CADDX_PART_STATUS_REQ;
		req->part = poll_part;
		if (full_write(fd, buf, 1 + sizeof(*req), 1) < 0)
			ERR(errno);

		len = sizeof(buf);
		if (caddx_rx_pkt_type(fd, buf, &len, CADDX_PART_STATUS) < 0)
			ERR(errno);

		if (status->exit1) {
			printf("Arming (exit1).\n");
		} else if (status->exit2) {
			printf("Arming (exit2).\n");
		} else if (status->entryguard && status->armed)
			printf("Armed (in stay mode).\n");
		else if (status->armed)
			printf("Armed.\n");
		else printf ("Not armed.\n");

		goto error;
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
			ERR(errno);
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

		len = sizeof(buf);
		if (caddx_rx_pkt(fd, buf, &len) < 0)
			ERR(errno);
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

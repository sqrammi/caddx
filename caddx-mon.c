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

#include "caddx.h"
#include "util.h"
#include "/home/ninkid/bin/hex-libc.c"

#define DEFAULT_HOST	"127.0.0.1:1587"

int errline = 0;

static void caddx_signal(int signum)
{
	if (signum == SIGINT)
		quit = 1;
}

static void
usage(void)
{
	printf("\
Usage: caddx-mon [options]\n\
-H ...: Host to connect to\n\
");
}

void
caddx_parse(int fd, uint8_t *buf, uint32_t len)
{
	switch (buf[0] & CADDX_MSG_MASK) {
	case CADDX_ZONE_STATUS:
		if (len != 8)
			goto error;
		if (buf[6] & CADDX_ZONE_ACTIVITY)
			err("zone %d activity\n", buf[1] + 1);
		else err("zone %d ok\n", buf[1] + 1);
		break;
	default:
	error:
		hexdump(buf, len);
		break;
	}
}

int
main(int argc, char *argv[])
{
	int i, fd = -1;
	char *host = strdup(DEFAULT_HOST), *port;
	uint8_t buf[128], len;
	struct addrinfo gai = { 0 }, *ai, *pai;
	struct sigaction action = { 0 };

	while ((i = getopt(argc, argv, "H:")) != -1) {
		switch (i) {
		case 'H': free(host); host = strdup(optarg); break;
		default: usage(); return -1;
		}
	}

	if ((port = rindex(host, ':')) == NULL)
		ERR(EINVAL);
	*(port++) = 0;

	action.sa_handler = caddx_signal;
	sigaction(SIGINT, &action, NULL);

	gai.ai_family = AF_UNSPEC;
	gai.ai_socktype = SOCK_STREAM;
	if ((i = getaddrinfo(host, port, (const struct addrinfo *)&gai, &ai)) != 0)
		ERR(i);

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
		if (full_read(fd, &len, 1, 1) < 0)
			ERR(-EIO);
		if (len > sizeof(buf))
			ERR(-EIO);
		if (full_read(fd, buf, len, 1) < 0)
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
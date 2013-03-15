#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <getopt.h>
#include <termios.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/select.h>
#include <signal.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>

#include "caddx.h"
#include "util.h"

#define DEFAULT_TTYNAME	"/dev/ttyUSB0"
#define DEFAULT_BAUD	38400
#define DEFAULT_LISTEN	"127.0.0.1:1587"

int errline = 0;

struct caddx_client {
	int fd;
	struct sockaddr addr;
	socklen_t addr_len;
	struct caddx_client *next;
};

static int baud = DEFAULT_BAUD;
static int synced = 0, sync_count = 1, sync_freq = 10;
static int fg = 0;
static struct caddx_client *clients = NULL;

/* CADDX Binary Protocol:
 * Byte: Description
 *  Bit: Description
 *
 * 0: Start character (CADDX_START)
 * 1: Length (not incl. escaped bytes and checksum)
 * 2:
 *  7: Ack required
 *  6: Reserved
 *  0-5: Message type
 * ... Message
 * N-1: Fletcher sum1
 * N: Fletcher sum2
 */

static int
caddx_tx(int fd, uint8_t *msg, uint32_t len)
{
	uint8_t *p = NULL;
	uint32_t escs = 0, i, j = 0;
	uint16_t cksum;

	for (p = msg; p < msg + len; p++)
		if (*p == CADDX_START || *p == CADDX_START - 1)
			escs++;
	if (!(p = malloc(2 + len + escs + 2)))
		ERR(ENOMEM);
	p[0] = CADDX_START;
	p[1] = len;
	memcpy(p + 2, msg, len);
	cksum = fletcher_cksum(p + 1, len + 1);

	if (escs) {
		for (i = 0; i < len; i++) {
			p[2 + i + j] = msg[i];
			if (msg[i] == CADDX_START) {
				p[2 + i +   j++]--;
				p[2 + i +     j] = CADDX_START ^ CADDX_START_ESC;
			} else if (msg[i] == CADDX_START - 1) {
				p[2 + i + (++j)] = (CADDX_START - 1) ^ CADDX_START_ESC;
			}
		}
	}

	p[2 + len + escs + 0] = cksum >> 8;
	p[2 + len + escs + 1] = cksum & 0xff;
	len = 2 + len + escs + 2;

#if 0
	printf("^^ tx:\n");
	hexdump(p, len);
#endif

	if (write(fd, p, len) != len)
		ERR(EIO);

	/* FALLTHROUGH */
 error:
	if (p) free(p);
	if (errno)
		return -1;
	return 0;
}

static void
caddx_rm_client(struct caddx_client *cl)
{
	struct caddx_client *l;

	warn("rm client %d\n", cl->fd);
	close(cl->fd); /* TODO: Check retval? */

	if (cl == clients) {
		clients = cl->next;
		return;
	}
	for (l = clients; l && l->next != cl; l = l->next) {}
	if (!l || l->next != cl)
		err("corrupt client list %p/%p", l, l ? l->next : NULL);
	l->next = cl->next;
	free(cl);
}

static int
caddx_rx_bytes(int fd, uint8_t *buf, uint32_t len)
{
	uint32_t _len = len;
	for (; len; len--) {
		if (full_read(fd, buf, 1, 0) < 0)
			return -1;
		debug("  read %02x\n", *buf);
		if (*buf == CADDX_START - 1) {
			if (full_read(fd, buf, 1, 0) < 0)
				return -1;
			debug("  read %02x\n", *buf);
			*buf ^= CADDX_START_ESC;
		}
		buf++;
	}
	return _len;
}

#define _CADDX_RX(len, add) do { \
	debug("read %d @%d\n", len, __LINE__); \
	if (caddx_rx_bytes(fd, buf + done, len) != len) \
		ERR(errno); \
	done += (add); \
} while (0)
#define CADDX_RX(len) do { \
	typeof(len) __len = (len); \
	_CADDX_RX(__len, __len); \
} while (0)

static int
caddx_rx_pkt(int fd, uint8_t *buf, uint32_t maxlen)
{
	int done = 0, len, _len;
	uint16_t cksum;
	struct caddx_client *cl;
	struct caddx_msg *msg;

	buf[0] = 0;
	while (buf[0] != CADDX_START && !quit)
		_CADDX_RX(1, 0);

	if (quit)
		ERR(EINTR);

	CADDX_RX(1);
	_len = len = buf[0];

	if (len > maxlen - 3)
		ERR(EINVAL);

	CADDX_RX(len);
	_CADDX_RX(2, 0);

	cksum = fletcher_cksum(buf, _len + 1);

	if (!buf[0])
		goto error;

	if (cksum >> 8 != buf[done] || (cksum & 0xff) != buf[done + 1]) {
		uint8_t nak = CADDX_NAK;
		caddx_tx(fd, &nak, 1);
		warn("bad cksum: %02x%02x vs %04x\n", buf[done], buf[done + 1 ], cksum);
		ERR(EPROTO);
	}

	msg = (struct caddx_msg *)(buf + 1);
	if (msg->ack) {
		uint8_t ack = CADDX_ACK;
		caddx_tx(fd, &ack, 1);
	}

	for (cl = clients; cl; cl = cl->next)
		if (full_write(cl->fd, buf, 1 + buf[0], 1) < 0)
			caddx_rm_client(cl);

	/* FALLTHROUGH */
 error:
	if (errno)
		return -1;
	return 0;
}

struct baud_rate {
	uint32_t target;
	uint32_t def;
};

static const struct baud_rate baud_rates[] =
{
        { 115200, B115200 },
        {  57600,  B57600 },
        {  38400,  B38400 },
        {  19200,  B19200 },
        {   9600,   B9600 },
        {   4800,   B4800 },
        {   2400,   B2400 },
        {   1200,   B1200 },
        {    300,    B300 },
};

static struct termios tio_old;

static int
serial_init(int fd)
{
	struct termios tio;
	uint32_t i;

	tcgetattr(fd, &tio_old);

	for (i = 0; i < ARRAY_SIZE(baud_rates); i++)
		if (baud_rates[i].target == baud)
			break;
	if (i == ARRAY_SIZE(baud_rates))
		ERR(EINVAL);
	i = baud_rates[i].def;

	memset(&tio, 0, sizeof(tio));
	tio.c_cflag = i | CS8 | CLOCAL | CREAD; /* TODO: Make CS8 modifiable */
	tio.c_iflag = IGNPAR; /* TODO: Make modifiable */
	tio.c_cc[VTIME] = 0;
	tio.c_cc[VMIN] = 1;

	if (tcflush(fd, TCIFLUSH) < 0)
		ERR(errno);

	if (tcsetattr(fd, TCSANOW, &tio) < 0)
		ERR(errno);

	/* FALLTHROUGH */
 error:
	if (errno)
		return -1;
	return 0;
}

static int
caddx_parse(int fd, uint8_t *buf, uint32_t len)
{
	struct caddx_msg *msg = (struct caddx_msg *)buf;

	switch (msg->type) {
	case CADDX_IFACE_CFG:
		if (len != 11)
			return -1;
		err("NX version %.*s up, caps: %02x %02x %02x %02x %02x %02x\n", 4, buf + 1,
			buf[5], buf[6], buf[7], buf[8], buf[9], buf[10]);
		synced = 1;
		break;
	}
	return 0;
}

static void
usage(void)
{
	printf("\
Usage: caddx [flags]\n\
-b ...: Baud (default " __str(DEFAULT_BAUD) ")\n\
-f    : Run in foreground\n\
-l ...: Listen to HOST:PORT (default " DEFAULT_LISTEN ")\n\
-t ...: TTY name (default " DEFAULT_TTYNAME ")\n\
-v    : Increase verbosity\n\
");
}

static void caddx_signal(int signum)
{
	if (signum == SIGINT)
		quit = 1;
}

static int
can_read(int fd)
{
	fd_set rfds;
	struct timeval tv = { 0 };
	FD_ZERO(&rfds);
	FD_SET(fd, &rfds);
	return select(fd + 1, &rfds, NULL, NULL, &tv) > 0;
}

static int
handle_connect(int sfd)
{
	struct timeval timeout = { 0 };
	struct caddx_client *cl = malloc(sizeof(*cl)), *p;
	if (!cl)
		ERR(ENOMEM);

	memset(cl, 0, sizeof(*cl));
	if ((cl->fd = accept(sfd, &cl->addr, &cl->addr_len)) < 0)
		ERR(errno);

	timeout.tv_sec = 5;
	setsockopt(cl->fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
	setsockopt(cl->fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

	if (!clients)
		clients = cl;
	else {
		for (p = clients; p->next; p = p->next) {}
		p->next = cl;
	}
	warn("add client %d\n", cl->fd);

	/* FALLTHROUGH */
 error:
	if (errno) {
		if (cl) free(cl);
		return -1;
	}
	return 0;
}

static int
client_read(int fd, struct caddx_client *cl)
{
	uint8_t len, buf[128];

	if (full_read(cl->fd, &len, 1, 1) < 0 ||
	    len > sizeof(buf)) {
		err("failed read %d\n", cl->fd);
		caddx_rm_client(cl);
		return -1;
	}

	if (full_read(cl->fd, buf, len, 1) < 0) {
		err("failed read2 %d\n", cl->fd);
		caddx_rm_client(cl);
		return -1;
	}

	caddx_tx(fd, buf, len);
	return 0;
}

int
main(int argc, char *argv[])
{
	int fd = -1, i, sfd = -1, max_fd = 0;
	char *ttyname = DEFAULT_TTYNAME, *listen_to = strdup(DEFAULT_LISTEN), *port;
	uint8_t buf[128];
	fd_set fds;
	struct timeval tv;
	struct sigaction action;
	struct addrinfo gai = { 0 }, *ai, *pai;

	while ((i = getopt(argc, argv, "b:fhl:t:v")) != -1) {
		switch (i) {
		case 'b': baud = strtol(optarg, NULL, 0); break;
		case 'f': fg = 1; break;
		case 'l': free(listen_to); listen_to = strdup(optarg); break;
		case 't': ttyname = optarg; break;
		case 'v': loglevel++; break;
		default: usage(); exit(-1);
		}
	}

	memset(&action, 0, sizeof(action));
	action.sa_handler = caddx_signal;
	sigaction(SIGINT, &action, NULL);

	if ((fd = open(ttyname, O_RDWR | O_NOCTTY)) < 0)
		ERR(errno);

	if (serial_init(fd) < 0)
		ERR(errno);

	if ((port = rindex(listen_to, ':')) == NULL)
		ERR(EINVAL);
	*(port++) = 0;

	gai.ai_family = AF_UNSPEC;
	gai.ai_socktype = SOCK_STREAM;
	if ((i = getaddrinfo(listen_to, port, (const struct addrinfo *)&gai, &ai)) != 0)
		ERR(i);

	for (pai = ai; pai; pai = pai->ai_next) {
		if ((sfd = socket(pai->ai_family, pai->ai_socktype, pai->ai_protocol)) < 0)
			continue;

		i = 1;
		setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &i, sizeof(i));

		if (bind(sfd, pai->ai_addr, pai->ai_addrlen) < 0) {
 sock_error:
			close(sfd);
			continue;
		}

		if (listen(sfd, 5) < 0)
			goto sock_error;
		break;
	}
	freeaddrinfo(ai);
	if (!pai) {
		if (!errno)
			errno = EINVAL;
		ERR(errno);
	}

	if (!fg) {
		log_syslog = 1;
		if ((i = fork()) < 0)
			ERR(errno);
		if (i != 0)
			goto error;
		setsid();
	}

	while (!quit) {
		struct caddx_client *cl = clients;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		FD_SET(sfd, &fds);
		max_fd = fd;
		if (sfd > fd)
			max_fd = sfd;
		for (cl = clients; cl; cl = cl->next) {
			FD_SET(cl->fd, &fds);
			if (cl->fd > max_fd)
				max_fd = cl->fd;
		}

		tv.tv_sec = 1;
		tv.tv_usec = 0;
		i = select(max_fd + 1, &fds, NULL, NULL, &tv);
		if (can_read(fd)) {
			caddx_rx_pkt(fd, buf, sizeof(buf));
			caddx_parse(fd, buf + 1, buf[0]);
		}
		if (can_read(sfd)) {
			handle_connect(sfd);
			errno = errline = 0;
		}
		for (cl = clients; cl; cl = cl->next)
			if (can_read(cl->fd))
				client_read(fd, cl);

		if (!synced && !--sync_count) {
			uint8_t sync = CADDX_IFACE_CFG_REQ;
			info("sync\n");
			caddx_tx(fd, &sync, 1);
			sync_count = sync_freq;
		}
	}


	/* FALLTHROUGH */
 error:
	if (listen_to) free(listen_to);
	if (fd >= 0) close(fd);
	if (sfd >= 0) close(sfd);
	if (errno && errline) {
		err("%s: error: %s @%d\n", __func__, strerror(errno), errline);
		errno = errline = 0;
		return -1;
	}
	return 0;
}

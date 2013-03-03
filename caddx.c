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

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif
#define __str_1(x...)		#x
#define __str(x...)		__str_1(x)

#define DEFAULT_TTYNAME	"/dev/ttyUSB0"
#define DEFAULT_BAUD	38400

int errline = 0;
#define ERR(val) do { errno = (val); errline = __LINE__; goto error; } while (0)

static int baud = DEFAULT_BAUD;

#define CADDX_START		0x7e
#define CADDX_START_ESC		0x20

#define CADDX_IFACE_CFG_REQ	0x21

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
 * N: Fletcher sum1
 */

static uint16_t
fletcher_cksum(uint8_t *data, uint32_t len)
{
	uint8_t sum1 = 0, sum2 = 0;
	uint32_t i;
	for (i = 0; i < len; i++) {
		if (255 - sum1 < data[i])
			sum1++;
		sum1 += data[i];
		if (sum1 == 255)
			sum1 = 0;
		if (255 - sum2 < sum1)
			sum2++;
		sum2 += sum1;
		if (sum2 == 255)
			sum2 = 0;
	}
	return (sum1 << 8) | sum2;
}

#include "/home/ninkid/bin/hex-libc.c"

static int
caddx_tx(int fd, uint8_t *msg, uint32_t len, uint32_t *out_len)
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

	printf("^^ tx:\n");
	hexdump(p, len);

	if (write(fd, p, len) != len)
		ERR(EIO);

	/* FALLTHROUGH */
 error:
	if (p) free(p);
	if (errno)
		return -1;
	return 0;
}

static int
full_read(int fd, uint8_t *buf, uint32_t len)
{
	int done = 0, i;

	while (len) {
		if ((i = read(fd, buf + done, 1)) <= 0) {
			if (errno == EAGAIN || errno == EINTR)
				continue;
			if (i == 0)
				errno = EIO;
			return -1;
		}
		done += i;
	}
	return done;
}

#define CADDX_RX(len) do { \
	fprintf(stderr, "read %d @%d\n", len, __LINE__); \
	if (full_read(fd, buf + done, len) < 0) \
		ERR(errno); \
} while (0)

static int
caddx_rx(int fd, uint8_t *buf, uint32_t maxlen)
{
	int done = 0, len;
	uint16_t cksum;

	buf[0] = 0;
	while (buf[0] != CADDX_START)
		CADDX_RX(1);

	CADDX_RX(1);
	len = buf[0];

	if (len > maxlen - 3)
		ERR(EINVAL);

	for (done = 1; len; len--, done++) {
		CADDX_RX(1);
		if (buf[done] == CADDX_START - 1) {
			CADDX_RX(1);
			buf[done] ^= CADDX_START_ESC;
		}
	}

	CADDX_RX(2);
	cksum = fletcher_cksum(buf, len + 1);

	printf("^^ rx: %x\n", cksum);
	hexdump(buf, len + 3);

	/* FALLTHROUGH */
 error:
	if (errno)
		return -1;
	return 0;
}

static void
usage(void)
{
	printf("\
Usage: caddx [flags]\n\
-b ...: baud (default " __str(DEFAULT_BAUD) ")\n\
-t ...: tty name (default " DEFAULT_TTYNAME ")\n\
");
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

printf("^^ ser %d\n", i);
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

int
main(int argc, char *argv[])
{
	int fd = -1, i;
	char *ttyname = DEFAULT_TTYNAME;
	uint8_t msg[] = { CADDX_IFACE_CFG_REQ };
//	uint8_t msg[] = { 0x84, 0x09, 0x7e, 0x10, 0x58, 0x01, 0x00 };
	uint8_t *m = NULL, buf[100];
	uint32_t len;

	while ((i = getopt(argc, argv, "t:")) != -1) {
		switch (i) {
		case 't': ttyname = optarg; break;
		default: usage(); exit(-1);
		}
	}

	if ((fd = open(ttyname, O_RDWR | O_NOCTTY)) < 0)
		ERR(errno);

	if (serial_init(fd) < 0)
		ERR(errno);

	if (caddx_tx(fd, msg, sizeof(msg), &len) < 0)
		ERR(errno);

	if (caddx_rx(fd, buf, sizeof(buf)) < 0)
		ERR(errno);

	/* FALLTHROUGH */
 error:
	if (m) free(m);
	if (fd >= 0) close(fd);
	if (errno) {
		fprintf(stderr, "%s: error: %s @%d\n", __func__, strerror(errno), errline);
		errno = errline = 0;
		return -1;
	}
	return 0;
}

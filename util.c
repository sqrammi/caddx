#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <syslog.h>
#include <errno.h>

int loglevel = 0;
int log_syslog = 0;
int quit = 0;

void
message(int level, const char *fmt, ...)
{
	char buf[128];
	va_list ap;
	int syslog_level = 0;

	if (level > loglevel)
		return;

	switch (level) {
	case 0: syslog_level = LOG_ERR; break;
	case 1: syslog_level = LOG_WARNING; break;
	case 2: syslog_level = LOG_INFO; break;
	default: syslog_level = LOG_DEBUG; break;
	}

	buf[sizeof(buf) - 1] = 0;
	va_start(ap, fmt);
	if (log_syslog)
		vsyslog(syslog_level, fmt, ap);
	else vprintf(fmt, ap);
	va_end(ap);
}

int
full_write(int fd, uint8_t *buf, uint32_t len, int eagain_quit)
{
	int done = 0, i;

	while (len && !quit) {
		if ((i = write(fd, buf + done, len)) < 0) {
			if (errno == EINTR ||
			    (!eagain_quit && errno == EAGAIN))
				continue;
			return -1;
		}
		if (eagain_quit && !i)
			return -1;
		done += i;
		len -= i;
	}
	return done;
}

int
full_read(int fd, uint8_t *buf, uint32_t len, int eagain_quit)
{
	int done = 0, i;

	while (len && !quit) {
		if ((i = read(fd, buf + done, len)) < 0) {
			if (errno == EINTR ||
			    (!eagain_quit && errno == EAGAIN))
				continue;
			return -1;
		}
		if (eagain_quit && !i)
			return -1;
		done += i;
		len -= i;
	}
	return done;
}

uint16_t
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

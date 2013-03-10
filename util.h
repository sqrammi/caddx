#ifndef __CADDX_UTIL_H_
#define __CADDX_UTIL_H_

extern int log_syslog;
extern int loglevel;
extern int quit;

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))
#endif

#define ERR(val) do { errno = (val); if (!errline) errline = __LINE__; goto error; } while (0)

#define __str_1(x...)		#x
#define __str(x...)		__str_1(x)

#define BIT(x) (1 << (x))

void message(int level, const char *fmt, ...);
#define err(fmt...)	message(0, fmt)
#define warn(fmt...)	message(1, fmt)
#define info(fmt...)	message(2, fmt)
#define debug(fmt...)	message(3, fmt)

int full_write(int fd, uint8_t *buf, uint32_t len, int eagain_quit);
int full_read(int fd, uint8_t *buf, uint32_t len, int eagain_quit);
uint16_t fletcher_cksum(uint8_t *data, uint32_t len);

#endif /* __CADDX_UTIL_H_ */

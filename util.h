#ifndef __CADDX_UTIL_H_
#define __CADDX_UTIL_H_

extern int log_syslog;
extern int loglevel;
extern int quit;

void msg(int level, const char *fmt, ...);
#define err(fmt...)	msg(0, fmt)
#define warn(fmt...)	msg(1, fmt)
#define info(fmt...)	msg(2, fmt)
#define debug(fmt...)	msg(3, fmt)

int full_write(int fd, uint8_t *buf, uint32_t len, int eagain_quit);
int full_read(int fd, uint8_t *buf, uint32_t len, int eagain_quit);
uint16_t fletcher_cksum(uint8_t *data, uint32_t len);

#endif /* __CADDX_UTIL_H_ */

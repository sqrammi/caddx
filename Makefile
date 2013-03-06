ifeq ($(wildcard .config),)
include .config.in
else
include .config
endif

PROGRAMS += caddx caddx-mon

CC=$(CROSS_COMPILE)gcc

all: $(PROGRAMS)
ifdef POSTBUILD
	$(POSTBUILD)
endif

caddx: caddx.o util.o
	$(CC) $^ $(LDFLAGS) -o $@

caddx-mon: caddx-mon.o util.o
	$(CC) $^ $(LDFLAGS) -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $^ -o $@

clean:
	rm -f *.o $(PROGRAMS)

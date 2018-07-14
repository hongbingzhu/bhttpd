CC=gcc
OPTS=-Wall -Wextra -std=gnu99 -g -O0 -o bhttpd
SRCS=bhttpd.c netlibs.c httplibs.c strlibs.c

all:
	$(CC) $(SRCS) $(OPTS)
clean:
	rm bhttpd
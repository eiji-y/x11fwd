CFLAGS=-O3

all: x11fwd

install:
	install x11fwd /usr/local/bin/x11fwd

clean:
	rm -f x11fwd

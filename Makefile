BINDIR =	/usr/local/sbin
MANDIR =	/usr/local/man/man8
CC =		cc
CFLAGS =	-std=gnu99 -Wall -O2

all:		micro_httpd

micro_httpd:	micro_httpd.o
	$(CC) micro_httpd.o $(LDFLAGS) -o micro_httpd

micro_httpd.o:	micro_httpd.c
	$(CC) $(CFLAGS) -c micro_httpd.c

install:	all
	mkdir -p /usr/local/man/man8/
	rm -f $(BINDIR)/micro_httpd
	cp micro_httpd $(BINDIR)/micro_httpd
	rm -f $(MANDIR)/micro_httpd.8
	cp micro_httpd.8 $(MANDIR)/micro_httpd.8

clean:
	rm -f micro_httpd *.o core core.* *.core

.DEFAULT_GOAL := all
.PHONY: all clean install uninstall tncattach

RM ?= rm
INSTALL ?= install
CC ?= gcc
CFLAGS ?= -Wall -std=gnu11 -static-libgcc
LDFLAGS ?= 
PREFIX ?= /usr/local

all: tncattach
rebuild: clean all

clean:
	@echo "Cleaning tncattach build..."
	$(RM) -f tncattach

tncattach:
	@echo "Making tncattach..."
	@echo "Compiling with: $(CC)"
	$(CC) $(CFLAGS) $(LDFLAGS) tncattach.c Serial.c TCP.c KISS.c TAP.c -o tncattach

install:
	@echo "Installing tncattach..."
	$(INSTALL) -d $(DESTDIR)/$(PREFIX)/bin
	$(INSTALL) -Dm755 tncattach $(DESTDIR)/$(PREFIX)/bin/tncattach
	@echo "Installing man page..."
	gzip -9 tncattach.8
	$(INSTALL) -d $(DESTDIR)/$(PREFIX)/share/man/man8
	$(INSTALL) -Dm644 tncattach.8.gz $(DESTDIR)/$(PREFIX)/share/man/man8/tncattach.8.gz

uninstall:
	@echo "Uninstalling tncattach"
	$(RM) $(DESTDIR)/$(PREFIX)/bin/tncattach
	$(RM) $(DESTDIR)/$(PREFIX)/share/man/man8/tncattach.8.gz

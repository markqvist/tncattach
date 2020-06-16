.DEFAULT_GOAL := all
.PHONY: all clean install uninstall tncattach

compiler = gcc
flags = -std=gnu11 -lm

all: tncattach
rebuild: clean all

clean:
	@echo "Cleaning tncattach build..."
	@rm -f tncattach

tncattach:
	@echo "Making tncattach..."
	@echo "Compiling with: ${compiler}"
	${compiler} ${flags} tncattach.c Serial.c Tcp.c KISS.c TAP.c -o tncattach -Wall

install:
	@echo "Installing tncattach..."
	chmod a+x tncattach
	cp ./tncattach /usr/local/sbin/

uninstall:
	@echo "Uninstalling tncattach"
	rm /usr/local/sbin/tncattach
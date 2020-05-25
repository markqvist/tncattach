.DEFAULT_GOAL := all
.PHONY: all clean install tncattach

compiler = gcc
flags = -lm

all: tncattach
rebuild: clean all

clean:
	@echo "Cleaning tncattach build..."
	rm tncattach

tncattach:
	@echo "Making tncattach..."
	@echo "Compiling with: ${compiler}"
	${compiler} ${flags} tncattach.c Serial.c KISS.c TAP.c -o tncattach -Wall

install: all
	@echo "Installing tncattach..."
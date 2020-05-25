.DEFAULT_GOAL := all
.PHONY: all clean install

compiler = gcc
flags = -lm

all: tncattach

clean:
	@echo "Cleaning tncattach build..."
	rm tncattach

tncattach:
	@echo "Making tncattach..."
	@echo "Compiling with: ${compiler}"
	${compiler} ${flags} tncattach.c -o tncattach

install: all
	@echo "Installing tncattach..."
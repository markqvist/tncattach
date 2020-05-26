#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include "Constants.h"

int open_port(char* port);
int close_port(int fd);
bool setup_port(int fs, int speed);
bool set_port_blocking(int fd, bool should_block);
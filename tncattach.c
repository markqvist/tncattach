#include <stdlib.h>
#include <stdbool.h>
#include <signal.h>
#include "Serial.h"
#include "KISS.h"
#include "TAP.h"

#define SERIAL_BUFFER_SIZE 512

int attached_port;
uint8_t serial_buffer[512];

void signal_handler(int signal) {
	printf("\r\nClosing serial port...\r\n");
	close_port(attached_port);
	exit(0);
}

void read_loop(tnc) {
	bool should_continue = true;
	while (should_continue) {
		int len = read(attached_port, serial_buffer, sizeof(serial_buffer));
		if (len > 0) {
			for (int i = 0; i < len; i++) {
				kiss_serial_read(serial_buffer[i]);
			}
		} else {
			printf("Error: Could not read from serial port, exiting now\r\n");
			close_port(attached_port);
			exit(1);
		}
	}
}

int main() {
	signal(SIGINT, signal_handler);

	attached_port = open_port("/dev/tty.usbserial-");
	if (setup_port(attached_port, 115200)) {
		printf("Port open\r\n");
		read_loop(attached_port);
	}
	
	return 0;
}
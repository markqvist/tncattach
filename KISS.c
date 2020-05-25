#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include "KISS.h"
#include "Serial.h"

int frame_len;
bool IN_FRAME;
bool ESCAPE;

uint8_t kiss_command = CMD_UNKNOWN;
uint8_t frame_buffer[MAX_PAYLOAD];
uint8_t write_buffer[MAX_PAYLOAD*2+3];

void kiss_frame_received(int frame_len) {
	
}

void kiss_serial_read(uint8_t sbyte) {
	if (IN_FRAME && sbyte == FEND && kiss_command == CMD_DATA) {
	    IN_FRAME = false;
	    kiss_frame_received(frame_len);
	} else if (sbyte == FEND) {
	    IN_FRAME = true;
	    kiss_command = CMD_UNKNOWN;
	    frame_len = 0;
	} else if (IN_FRAME && frame_len < MAX_PAYLOAD) {
	    // Have a look at the command byte first
	    if (frame_len == 0 && kiss_command == CMD_UNKNOWN) {
	    	// Strip of port nibble
	        kiss_command = sbyte & 0x0F;
	    } else if (kiss_command == CMD_DATA) {
	        if (sbyte == FESC) {
	            ESCAPE = true;
	        } else {
	            if (ESCAPE) {
	                if (sbyte == TFEND) sbyte = FEND;
	                if (sbyte == TFESC) sbyte = FESC;
	                ESCAPE = false;
	            }

	            if (frame_len < MAX_PAYLOAD) {
	            	frame_buffer[frame_len++] = sbyte;
	            }
	        }
	    }
	}
}

int kiss_write_frame(int serial_port, uint8_t* buffer, int frame_len) {
	int write_len = 0;
	write_buffer[write_len++] = FEND;
	write_buffer[write_len++] = CMD_DATA;
	for (int i = 0; i < frame_len; i++) {
		uint8_t byte = buffer[i];
		if (byte == FEND) {
			write_buffer[write_len++] = FESC;
			write_buffer[write_len++] = TFEND;
		} else if (byte == FESC) {
			write_buffer[write_len++] = FESC;
			write_buffer[write_len++] = TFESC;
		} else {
			write_buffer[write_len++] = byte;
		}
	}
	write_buffer[write_len++] = FEND;

	return write(serial_port, write_buffer, write_len);
}
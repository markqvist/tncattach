#include "Serial.h"

int open_port(char* port) {
	int fd;
	fd = open(port, O_RDWR | O_NOCTTY | O_SYNC | O_NDELAY);

	if (fd == -1) {
		perror("The serial port could not be opened");
	} else {
		fcntl(fd, F_SETFL, 0);
	}

	return fd;
}

int close_port(int fd) {
	return close(fd);
}

void set_speed(void *tty_s, int speed) {
	cfsetospeed(tty_s, speed);
	cfsetispeed(tty_s, speed);
}

bool setup_port(int fd, int speed) {
	struct termios tty;
	if (tcgetattr(fd, &tty) != 0) {
		perror("Error setting port speed, could not read port parameters");
		return false;
	}

	switch (speed) {
		case 0:
			set_speed(&tty, B0);
			break;
		case 50:
			set_speed(&tty, B50);
			break;
		case 75:
			set_speed(&tty, B75);
			break;
		case 110:
			set_speed(&tty, B110);
			break;
		case 134:
			set_speed(&tty, B134);
			break;
		case 150:
			set_speed(&tty, B150);
			break;
		case 200:
			set_speed(&tty, B200);
			break;
		case 300:
			set_speed(&tty, B300);
			break;
		case 600:
			set_speed(&tty, B600);
			break;
		case 1200:
			set_speed(&tty, B1200);
			break;
		case 2400:
			set_speed(&tty, B2400);
			break;
		case 4800:
			set_speed(&tty, B4800);
			break;
		case 9600:
			set_speed(&tty, B9600);
			break;
		case 19200:
			set_speed(&tty, B19200);
			break;
		case 38400:
			set_speed(&tty, B38400);
			break;
		case 57600:
			set_speed(&tty, B57600);
			break;
		case 115200:
			set_speed(&tty, B115200);
			break;
		case 230400:
			set_speed(&tty, B230400);
			break;
		default:
			printf("Error: Invalid port speed %d specified", speed);
			return false;
	}

	// Set 8-bit characters, no parity, one stop bit
	tty.c_cflag |= CS8;
	tty.c_cflag &= ~PARENB;
	tty.c_cflag &= ~CSTOPB;

	// Disable hardware flow control
	tty.c_cflag &= ~CRTSCTS;

	// Enable reading and ignore modem
	// control lines
	tty.c_cflag |= CREAD | CLOCAL;

	// Disable canonical mode, echo
	// and signal characters.
	tty.c_lflag &= ~ICANON;
	tty.c_lflag &= ~ECHO;
	tty.c_lflag &= ~ECHOE;
	tty.c_lflag &= ~ECHONL;
	tty.c_lflag &= ~ISIG;

	// Disable processing of input,
	// just pass the raw data.
	tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL);

	// Disable XON/XOFF software flow control.
	tty.c_iflag &= ~(IXON | IXOFF | IXANY);

	// Disable processing output bytes
	// and new line conversions
	tty.c_oflag &= ~OPOST;
	tty.c_oflag &= ~ONLCR;

	// Block forever until at least one byte is read.
	tty.c_cc[VMIN]   = 1;
	tty.c_cc[VTIME]  = 0;

	// TODO: Check these
	// Prevent conversion of tabs to spaces (NOT PRESENT IN LINUX)
	// tty.c_oflag &= ~OXTABS;
	// Prevent removal of C-d chars (0x004) in output (NOT PRESENT IN LINUX)
	// tty.c_oflag &= ~ONOEOT;

	if (tcsetattr(fd, TCSANOW, &tty) != 0) {
		perror("Could not configure serial port parameters");
		return false;
	} else {
		return true;
	}
}

bool set_port_blocking(int fd, bool should_block) {
	struct termios tty;
	memset(&tty, 0, sizeof tty);

	if (tcgetattr(fd, &tty) != 0) {
		perror("Error configuring port blocking behaviour, could not read port parameters");
		return false;
	} else {
		// TODO: Implement this correctly
		if (should_block) {
			// Block forever until at least one byte is read.
			tty.c_cc[VMIN]   = 1;
			tty.c_cc[VTIME]  = 0;
		} else {
			// Never block, always return immediately with
			// whatever is available.
			tty.c_cc[VMIN]   = 0;
			tty.c_cc[VTIME]  = 0;
		}
		if (tcsetattr(fd, TCSANOW, &tty) != 0) {
			perror("Could not set port parameters while configuring blocking behaviour");
			return false;
		} else {
			return true;
		}
	}
}
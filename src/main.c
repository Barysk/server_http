#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_PORT 8080
#define ZERO 0

int main(int argc, char **argv) {
	int port = DEFAULT_PORT;

	if (argc >= 2) {
		char* p_end;
		errno = ZERO;

		long value = strtol(argv[1], &p_end, 10);

		if (errno == ERANGE || value < 1 || value > 65535 || p_end == argv[1] || *p_end != '\0') {
			fprintf(stderr, "Provided port '%s' is invalid.\n", argv[1]);
			return 1;
		}

		port = (int)value;
	}

	printf("cerver not implemented yet! But currently provided port is %d\n", port);

	return 0;
}

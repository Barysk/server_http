#include <stdio.h>
#include <stdlib.h>

#define DEFAULT_PORT 8080

int main(int argc, char **argv) {
	int port = DEFAULT_PORT;

	if (argc >= 2) {
		port = atoi(argv[1]);
	}

	printf("cerver not implemented yet! But currently provided port is %d\n", port);

	return 0;
}

#include <stdio.h>
#include "../lib/libgolden.h"

int main()
{
	golden_operations ops = {0};
	int fd = 0;

	if (libgolden_init() < 0)
	{
		printf("Error in libgolden_init\n");
		return -1;
	}

	fd = libgolden_create_device("MyDevice", 1, 8192, &ops);

	printf("Got back fd: %d\n", fd);	

	return 0;
}

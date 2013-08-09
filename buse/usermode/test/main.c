#include <stdio.h>
int goldengate_open(int fd, int oflag);
ssize_t goldengate_read(int fd, void *buf, size_t nbyte);
size_t goldengate_write(int fd, const void *buf, size_t count);
int goldengate_close(int fd);
int goldengate_test(void);
int main()
{
	int x;

	x= goldengate_test();
	printf("Valx=%d\n",x);

	return 0;
}

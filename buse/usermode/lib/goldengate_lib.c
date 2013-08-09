#include <stdio.h>
#include<stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include "common.h"
#include "goldengate_lib.h"
int fd;

int goldengate_open(void)
{
        GoldenRequest req = {0};
        fd = open("/dev/goldenchar", O_RDONLY);

        req.request_type = GOLDENCHAR_REQUEST_NEW_DEVICE;
        strcpy(req.sel.NewDeviceRequest.device_name, "HelloBD");
        req.sel.NewDeviceRequest.capacity = 10;
        req.sel.NewDeviceRequest.minors = 1;

        printf("Current uid: %d, pid: %d\n", getuid(), getpid());

        if (ioctl(fd, GOLDENCHAR_IOCTL_REQUEST, &req) != 0)
                printf("No can do...");

        printf("Got back fd: %d", req.sel.NewDeviceRequest.fd);

        int test_dev = open("/dev/HelloBD", O_RDWR);

        printf("Got test_dev = %d\n", test_dev);
        if (test_dev < 0)
                printf("Could not open the test device\n");
        else
                printf("YAY! Opened the device successfully\n");

        printf("Current uid: %d\n", getuid());
return 0;
}
ssize_t goldengate_read(void *buf, size_t nbyte)
{
	GoldenRequest req = {0};

	req.request_type = GOLDENCHAR_DEVICE_REQUEST;

	if (ioctl(fd, GOLDENCHAR_IOCTL_REQUEST, &req) != 0)
		printf("No can do...");
	return 0;
}

size_t goldengate_write(const void *buf, size_t count)
{
        GoldenRequest req = {0};

        req.request_type = GOLDENCHAR_DEVICE_REQUEST;

        if (ioctl(fd, GOLDENCHAR_IOCTL_REQUEST, &req) != 0)
                printf("No can do...");
return 0;
}

int goldengate_close(void)
{
        GoldenRequest req = {0};

        req.request_type = GOLDENCHAR_REQUEST_REMOVE_DEVICE;

        if (ioctl(fd, GOLDENCHAR_IOCTL_REQUEST, &req) != 0)
                printf("No can do...");
return 0;
}

int goldengate_test(void)
{
return 55;
}

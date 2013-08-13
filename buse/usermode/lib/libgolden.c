#include "libgolden.h"
#include <string.h>

int goldenchar_fd = 0;

int libgolden_init()
{
	goldenchar_fd = open(DEVFS_GOLDENCHAR_PATH);

	if (goldenchar_fd <= 0)
		return -1;

	return 0;
}

int libgolden_create_device(char* name, int minors, int capacity, golden_operations* ops)
{
	GoldenRequest request = {0};

	request.request_type = GOLDENCHAR_REQUEST_NEW_DEVICE;

	strcpy(request.sel.NewDeviceRequest.device_name, name);

	request.sel.NewDeviceRequest.capacity = capacity;
	request.sel.NewDeviceRequest.minors = minors;
	
	if (ioctl(goldenchar_fd, GOLDENCHAR_IOCTL_REQUEST, &request) < 0)
		return -1;

	return request.sel.NewDeviceRequest.fd;
}

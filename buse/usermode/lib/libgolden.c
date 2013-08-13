#include "libgolden.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

int goldenchar_fd = 0;

typedef struct device_list
{
	int fd;
	golden_operations ops;
	struct device_list* next;	
} device_list;

typedef struct internal_context
{
	int goldenchar_fd;
	device_list* device_list;
} internal_context;

internal_context _context = {0};

int libgolden_init()
{
	_context.goldenchar_fd = open(DEVFS_GOLDENCHAR_PATH, O_RDWR);

	if (_context.goldenchar_fd == -1)
		return -1;

	return 0;
}

int libgolden_register_device(char* name, int minors, int capacity, golden_operations* ops)
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

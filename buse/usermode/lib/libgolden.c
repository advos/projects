#include "libgolden.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>

int goldenchar_fd = 0;

typedef struct device_node
{
	int fd;
	golden_operations ops;
	struct device_node* next;	
} device_node;

typedef struct internal_context
{
	int goldenchar_fd;
	device_node dev_list;
} internal_context;

internal_context _context = {0};

static int add_device_to_list(int fd, golden_operations* ops);
static device_node* find_device_by_fd(int fd);
static int dispatch_request(device_node* dev, GoldenRequest* request);

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
	
	if (ioctl(_context.goldenchar_fd, GOLDENCHAR_IOCTL_REQUEST, &request) < 0)
		return -1;

	add_device_to_list(request.sel.NewDeviceRequest.fd, ops);

	return request.sel.NewDeviceRequest.fd;
}

int libgolden_loop(int fd)
{
	device_node* device = find_device_by_fd(fd);
	GoldenRequest request = {0};	

	if (device == NULL)
		return -1;

	while(1)
	{
		memset(&request, 0, sizeof(GoldenRequest));

		request.request_type = GOLDENCHAR_DEVICE_REQUEST;
		request.sel.DeviceIoRequest.fd = device->fd;

		if (!ioctl(_context.goldenchar_fd, GOLDENCHAR_IOCTL_REQUEST, &request) < 0)
			return -1;

		if (dispatch_request(device, &request) < 0)
			return -1;

		if (!ioctl(_context.goldenchar_fd, GOLDENCHAR_IOCTL_REQUEST, &request) < 0)
			return -1;
	}
	
	return 0;	
}

static int dispatch_request(device_node* device, GoldenRequest* request)
{
	switch(request->operation)
	{
		case BLOCK_DEVICE_OPEN:
			return device->ops->open(request);
		case BLOCK_DEVICE_CLOSE:
			return device->ops->close(request);
		case BLOCK_DEVICE_READ:
			return device->ops->read(request);
		case BLOCK_DEVICE_WRITE:
			return device->ops->write(request);
		default:
			return -1;
	}

	return -1;	
}

static int add_device_to_list(int fd, golden_operations* ops)
{
	device_node* current_dev = &_context.dev_list;
	device_node* next_dev = _context.dev_list.next;

	while (next_dev != NULL)
	{
		current_dev = next_dev;
		next_dev = current_dev->next;
	}

	current_dev->next = malloc(sizeof(device_node));
	
	if (current_dev->next == NULL)
		return -1;

	memset(current_dev->next, 0, sizeof(device_node));

	current_dev->next->fd = fd;

	memcpy(&current_dev->next->ops, ops, sizeof(golden_operations));

	return 0;	
}

static device_node* find_device_by_fd(int fd)
{
	device_node* dev = _context.dev_list.next;

	while (dev != NULL)
	{
		if (dev->fd == fd)
			return dev; 
	}

	return NULL;
}

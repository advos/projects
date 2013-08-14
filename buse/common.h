#ifndef __COMMON_H__
#define __COMMON_H__

#define MAX_NAME 32

#define GOLDENCHAR_STR "goldenchar"
#define DEVFS_GOLDENCHAR_PATH "/dev/" GOLDENCHAR_STR
#define GOLDEN_DEV_CHAR_CLASS "GoldenGateClass2"

typedef enum GoldenCharIoctl
{
GOLDENCHAR_IOCTL_REQUEST,
GOLDENCHAR_IOCTL_DEBUG,
} GoldenCharIoctl;

typedef enum GoldenCharRequestType
{
GOLDENCHAR_REQUEST_NEW_DEVICE,
GOLDENCHAR_REQUEST_REMOVE_DEVICE,
GOLDENCHAR_DEVICE_REQUEST,
GOLDENCHAR_DEVICE_COMPLETE_REQUEST,
} GoldenCharRequestType;

typedef enum BlockDeviceOperation
{
	BLOCK_DEVICE_OPEN,
	BLOCK_DEVICE_READ,
	BLOCK_DEVICE_WRITE,
	BLOCK_DEVICE_CLOSE,
} BlockDeviceOperation;

typedef struct IoDescriptor
{
	int length;
	char* data;
} IoDescriptor;

typedef struct GoldenRequest
{
GoldenCharRequestType request_type;
union sel
{
	struct NewDeviceRequest
	{
		char device_name[MAX_NAME];
		int capacity; //In sector units
		int minors;
		int fd;
	} NewDeviceRequest;

	struct RemoveDeviceRequest
	{
		int fd;
	} RemoveDeviceRequest;

	//This struct is filled out by the driver to represent an I/O Operation
	struct DeviceIoRequest
	{
		int fd;
		int request_id;
		int operation;
		IoDescriptor descriptor;
	} DeviceIoRequest;

	} sel;
} GoldenRequest;

#define MAX_SIZE_BUFFER  60000
#endif

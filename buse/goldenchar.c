#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/delay.h>
#include "golden.h"
#include "goldenchar.h"
#include "utils.h"

static int goldenchar_open(struct inode* ind, struct file* filep);
static int goldenchar_release(struct inode* ind, struct file* filep);
static long goldenchar_ioctl(struct file* filep, unsigned int num, unsigned long ptr);

static int handle_request(struct filel* filep, GoldenRequest* request);
static int setup_new_device(struct file* filep, GoldenRequest* request);
static int remove_device(struct file* filep, GoldenRequest* request);

static GoldenBlock* search_for_block_device_by_fd(int fd, kuid_t uid, pid_t pid);
static void golden_block_assign_fd(GoldenBlock* block, kuid_t uid, pid_t pid);

static int get_io(GoldenRequest* request);

static void sanitize_request(GoldenRequest* request);

static const struct file_operations goldenchar_fops = {
	.owner = THIS_MODULE,
	.open = goldenchar_open,
	.unlocked_ioctl = goldenchar_ioctl,
	.release = goldenchar_release
};

static GoldenGate* _golden;

static int goldenchar_open(struct inode* ind, struct file* filep)
{
	return 0;
}

static int goldenchar_release(struct inode* ind, struct file* filep)
{
	return 0;
}

static long goldenchar_ioctl(struct file* filep, unsigned int num, unsigned long ptr)
{
	GoldenRequest* request = NULL;

	request = kmalloc(sizeof(*request), GFP_KERNEL);
	if (request == NULL)
		return -ENOMEM;

	if (copy_from_user(request, (void __user *)ptr, sizeof(*request)))
		return -EFAULT;
	
	printk(KERN_ALERT "Golden Char: ioctl called");

	switch(num)
	{
		case GOLDENCHAR_IOCTL_REQUEST:
			return handle_request(filep, request);
		case GOLDENCHAR_IOCTL_DEBUG:
			break;
		default:
			break;
	}

	return 0;
}

static int handle_request(struct file* filep, GoldenRequest* request)
{
	sanitize_request(request);

	switch(request->request_type)
	{
		case GOLDENCHAR_REQUEST_NEW_DEVICE:
			printk(KERN_ALERT "Golden Char: Asked to create a new device");
			return setup_new_device(filep, request);
		case GOLDENCHAR_REQUEST_REMOVE_DEVICE:
			printk(KERN_ALERT "Golden Char: Asked to remove device");
			return remove_device(filep, request);
		case GOLDENCHAR_DEVICE_REQUEST:
			printk(KERN_ALERT "Golden Char: Checking for I/O Operation for current process!");
			return get_io(request);
		default:
			printk(KERN_ALERT "Golden Char: Unknown request type!");
	}
	return 0;
}

static int get_io(GoldenRequest* request)
{
	int fd = request->sel.DeviceIoRequest.fd;
	struct RequestNode* next_request = NULL;

	GoldenBlock* block_dev = search_for_block_device_by_fd(fd, current_uid(), task_pid_nr(current));

	if (block_dev == NULL)
		return -1;

	next_request = golden_block_pop_request(block_dev);
	while (next_request == NULL)
	{
		printk(KERN_ALERT "get_io(): No request given, sleeping for a minute\n");
		msleep(60 * 1000);
		next_request = golden_block_pop_request(block_dev);
	}

	//Put the request in the usermode pending list
	golden_block_add_request_to_usermode_pending_list(block_dev, next_request);

	//Now fill out parts for usermode
	request->sel.DeviceIoRequest.request_id = next_request->request_id;

	return generate_usermode_request_from_request(golden_request, next_request);	
}

static int generate_usermode_request_from_request(GoldenRequest* golden_request, struct request* io_request)
{
	struct bio_vec *bv;
	struct req_iterator iter;
	char* buffer;
	int direction = rq_data_dir(io_request);
	unsigned int total_buffer_length = 0;
	unsigned int offset = 0;
	int status = -ENOMEM;

	IoDescriptor* current_descriptor = NULL;
	
	golden_request->sel.DeviceIoRequest.direction = direction;
	
	golden_request->sel.DeviceIoRequest.descriptor = kzalloc(sizeof(IoDescriptor), GFP_USER);

	if (golden_request->sel.DeviceIoRequest.descriptor == NULL)
		goto err;

	current_descriptor = golden_request->sel.DeviceIoRequest.descriptor;

	rq_for_each_segment(bv, io_request, iter)
		total_buffer_length += bv->bv_len;

	golden_request->sel.DeviceIoRequest.descriptor->length = total_buffer_length;
	golden_request->sel.DeviceIoRequest.descriptor->data = kzalloc(total_buffer_length, GFP_USER);

	if (golden_request->sel.DeviceIoRequest.descriptor->data == NULL)
		goto err;

	offset = 0;
	rq_for_each_segment(bv, io_request, iter)
	{
		char * buffer = page_address(bv->bv_page) + bv->bv_offset;

		if (direction == WRITE)
			memcpy(golden_request->sel.DeviceIoRequest.descriptor->data + offset, buffer, bv->bv_len);
		offset += bv->bv_len;
	}

	return 0;
err:
	if (golden_request->sel.DeviceIoRequest.descriptor != NULL)
	{
		if (golden_request->sel.DeviceIoRequest.descriptor->data != NULL)
			kfree(golden_request->sel.DeviceIoRequest.descriptor->data);

		kfree(golden_request->sel.DeviceIoRequest.descriptor);
	}

	return status;
}

static void sanitize_request(GoldenRequest* request)
{
	switch (request->request_type)
	{
		case GOLDENCHAR_REQUEST_NEW_DEVICE:
			string_truncate(request->sel.NewDeviceRequest.device_name, sizeof(request->sel.NewDeviceRequest.device_name));
			break;
		default:
			break;
	}
}

static int setup_new_device(struct file* filep, GoldenRequest* request)
{
	GoldenBlock* block = NULL;
	int err = 0;

	printk(KERN_ALERT "Golden Char: setup_new_device");

	err = golden_block_create(request->sel.NewDeviceRequest.device_name, request->sel.NewDeviceRequest.capacity, request->sel.NewDeviceRequest.minors, &block);

	printk(KERN_ALERT "Golden Char: after golden_block_create. err = %d", err);

	if (err < 0)
		return err;

	if (block == NULL)
		return -ENOMEM;

	golden_block_assign_fd(block, current_uid(), task_pid_nr(current));

	if (down_interruptible(&_golden->golden_lock) == 0)
	{
		list_add(&(block->list),&(_golden->gblock_list_head.list));
		up(&_golden->golden_lock);
		request->sel.NewDeviceRequest.fd = block->internal_fd;
	}
	else
	{
		//golden_block_destroy(block);
		return -EINTR;
	}

	return 0;
}

static void golden_block_assign_fd(GoldenBlock* block, kuid_t uid, pid_t pid)
{
	int max_fd = 1;
	struct list_head* iter;

	list_for_each(iter, &(_golden->gblock_list_head.list))
	{
		GoldenBlock* temp = list_entry(iter, GoldenBlock, list);

		if (uid_eq(uid, temp->owner_uid) && (temp->owner_pid == pid) && (temp->internal_fd > max_fd))
			max_fd = temp->internal_fd;
	}

	block->internal_fd = max_fd + 1;
}

static GoldenBlock* search_for_block_device_by_fd(int fd, kuid_t uid, pid_t pid)
{
	struct list_head* iter;

	list_for_each(iter, &(_golden->gblock_list_head.list))
	{
		GoldenBlock* temp = list_entry(iter, GoldenBlock, list);

		if ((temp->internal_fd == fd) && uid_eq(uid, temp->owner_uid) && (temp->owner_pid == pid)
		return temp;
	}

	return NULL;
}

static int remove_device(struct file* filep, GoldenRequest* request)
{
	//We need to think what to do with device removing
	return 0;
	/*
	   GoldenBlock* block = NULL;
	   list_head iter;

	   printk(KERN_ALERT "Golden Char: remove_device");

	   list_for_each(&iter, &(_golden->gblock_list_head.list))
	   {
	   GoldenBlock* temp = list_entry(&iter, GoldenBlock, list);

	   if (temp->internal_fd == request->sel.RemoveDeviceRequest.fd && uid_eq(current_uid(), temp->owner_uid) && (temp->owner_pid == task_pid_nr(current)))
	   {
	//This is our device, remove it
	if (down_interruptable(&_golden->golden_lock) == 0)
	{
	list_del(&(temp->list));
	up(&_golden->golden_lock);
	return 0;
	}
	else
	return -EINTR;
	}
	}

	return -EINVAL;

	 */
}

int setup_goldenchar_device(GoldenGate* golden, GoldenChar* gchar)
{
	int retval = 0;
	dev_t dev_no = {0};

	_golden = golden;

	memset(gchar, 0, sizeof(*gchar));

	if (alloc_chrdev_region(&dev_no, 0, 1, GOLDENCHAR_STR) < 0)
	{
		retval = -EBUSY;
		goto cleanup;
	}

	gchar->dev_no = dev_no;

	gchar->dev_class = class_create(THIS_MODULE, GOLDEN_DEV_CHAR_CLASS);

	if (gchar->dev_class == NULL)
	{
		retval = -ENOMEM;
		goto cleanup;
	}

	gchar->dev_cdev = cdev_alloc();
	//gchar->dev_cdev.owner = THIS_MODULE;

	if (gchar->dev_cdev == NULL)
	{
		retval = -ENOMEM;
		goto cleanup;
	}

	cdev_init(gchar->dev_cdev, &goldenchar_fops);

	if (cdev_add(gchar->dev_cdev, dev_no, 1) < 0)
	{
		retval = -ENODEV;
		goto cleanup;
	}

	gchar->device_st = device_create(gchar->dev_class, NULL, dev_no, NULL, GOLDENCHAR_STR);
	if (gchar->device_st == NULL)
	{
		retval = -ENODEV;
		goto cleanup;
	}

cleanup:
	if (retval < 0)
		free_goldenchar_device(gchar);

	return retval;
}

void free_goldenchar_device(GoldenChar* gchar)
{
	if (gchar->dev_class != NULL)
	{
		class_destroy(gchar->dev_class);
		gchar->dev_class = NULL;
	}

	unregister_chrdev_region(gchar->dev_no, 1);
}

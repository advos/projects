#include <linux/cred.h>
#include <linux/uidgid.h>
#include <linux/syscalls.h>
#include "golden.h"
#include "goldenchar.h"
#include "utils.h"

static int goldenchar_open(struct inode* ind, struct file* filep);
static int goldenchar_release(struct inode* ind, struct file* filep);
static long goldenchar_ioctl(struct file* filep, unsigned int num, unsigned long ptr);

static int handle_request(struct file* filep, GoldenRequest* request);
static int setup_new_device(struct file* filep, GoldenRequest* request);
static int remove_device(struct file* filep, GoldenRequest* request);

static void golden_block_assign_fd(GoldenBlock* block, kuid_t uid, pid_t pid);

static void sanitize_request(GoldenRequest* request);

static GoldenGate* _golden;

static const struct file_operations goldenchar_fops = {
	.owner = THIS_MODULE,
	.open = goldenchar_open,
	.unlocked_ioctl = goldenchar_ioctl,
	.release = goldenchar_release
};

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
	GoldenRequest* request = (GoldenRequest*)ptr;

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
		default:
			printk(KERN_ALERT "Golden Char: Unknown request type!");
	}
	return 0;
}

static void sanitize_request(GoldenRequest* request)
{
	switch (request->request_type)
	{
		case GOLDENCHAR_REQUEST_NEW_DEVICE:
			string_truncate(request->sel.NewDeviceRequest.device_name, sizeof(request->sel.NewDeviceRequest.device_name));
			string_replace(request->sel.NewDeviceRequest.device_name, sizeof(request->sel.NewDeviceRequest.device_name), '/', '_');
			string_replace(request->sel.NewDeviceRequest.device_name, sizeof(request->sel.NewDeviceRequest.device_name), '.', '_');
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

	my_chmod(DEVFS_GOLDENCHAR_PATH, 0777, 1);

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

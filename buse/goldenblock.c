#include <linux/module.h>
#include <linux/delay.h>
#include "goldenblock.h"
#define SECTOR_SIZE 512

static int goldenblock_open(struct block_device* bdev, fmode_t mode);
static int goldenblock_release(struct gendisk* gd, fmode_t mode);

void goldenblock_request(struct request_queue* queue);
void golden_block_list_initialize(GoldenBlock* block);
int golden_block_create(char* name, int capacity, int minors, GoldenBlock** out);

RequestNode* golden_block_pop_request(GoldenBlock* block_dev);


static const struct block_device_operations goldenblock_ops = {
       .owner = THIS_MODULE,
       .open = goldenblock_open,
       .release = goldenblock_release
}

static int goldenblock_open(struct block_device* bdev, fmode_t mode)
{
	GoldenBlock* block = bdev->bd_disk->private_data;

	printk(KERN_ALERT "goldenblock_open(): Called");

	if (!uid_eq(current_uid(), block->owner_uid))
	{
		printk(KERN_ALERT "goldenblock_open(): Permission denied. Opener is not the owner");
		return -1;
	}

	return 0;
}

static int goldenblock_release(struct gendisk* gd, fmode_t mode)
{
	printk(KERN_ALERT "goldenblock_release(): Called");
	return 0;
}

void goldenblock_request(struct request_queue* queue)
{
    struct request *req;

    /* Gets the current request from the dispatch queue */
    while ((req = blk_fetch_request(queue)) != NULL)
	{
		RequestNode* request_node = kzalloc(sizeof(*request_node), GFP_KERNEL);
		if (request_node == NULL)
		{
			__blk_end_request_all(req, -ENOMEM);
			continue;
		}
	
		request_node->req = req;
		list_add(&(request_node->list), &(((GoldenBlock*)req->rq_disk->private_data)->request_list));
	}
}


void golden_block_list_initialize(GoldenBlock* block)
{
	INIT_LIST_HEAD(&(block->list));
}

int golden_block_create(char* name, int capacity, int minors, GoldenBlock** out)
{
	int err = 0;
	GoldenBlock* block = kzalloc(sizeof(GoldenBlock), GFP_KERNEL);

	printk(KERN_ALERT "golden_block_create(): Starting");

	if (block == NULL)
	{
		err = -ENOMEM;
		goto end;
	}

	block->owner_uid = current_uid();
	printk(KERN_ALERT "golden_block_create: setting current uid to %d", block->owner_uid);
	block->refcount = 0;

	INIT_LIST_HEAD(&(block->request_list));

	block->major = register_blkdev(0, name);
	if (block->major < 0)
	{
		err = -ENOMEM;
		goto end;
	}

	printk(KERN_ALERT "golden_block_create(): registered block device");
	spin_lock_init(&(block->request_queue_lock));
	spin_lock_init(&(block->goldenblock_lock));

	block->gd = alloc_disk(minors);

	if (block->gd == NULL)
	{
		err = -ENOMEM;
		goto end;
	}

	block->gd->major = block->major;
	block->gd->first_minor = 1;
	block->gd->fops = &goldenblock_ops;
	block->gd->private_data = block;
	strcpy(block->gd->disk_name, name);
	set_capacity(block->gd, capacity * SECTOR_SIZE);

	block->gd->queue = blk_init_queue(goldenblock_request, &block->request_queue_lock);
	if (block->gd->queue == NULL)
	{
		err = -ENOMEM;
		goto end;
	}

	printk(KERN_ALERT "golden_block_create(): About to add_disk. minors=%d, major=%d, first_minor=%d", block->gd->minors, block->gd->major, block->gd->first_minor);

	add_disk(block->gd);
	*out = block;
end:
	if (err < 0)
	{
		if (block->gd != NULL)
			del_gendisk(block->gd);
		if (block != NULL);
		kfree(block);
	}

	return err;	
}

RequestNode* golden_block_pop_request(GoldenBlock* block_dev)
{
	RequestNode* node = NULL;

	if (list_empty(&block_dev->request_list))
		return NULL;

	node = list_entry(block_dev->request_list.next, RequestNode, next);

	list_del(block_dev->request_list.next);

	return node;
}

static void golden_block_add_request_to_usermode_pending_list(GoldenBlock* block_dev, RequestNode* request_node)
{
	struct list_head iter;
	int max_request_id = 1;

	list_for_each(iter, &(block_dev->pending_for_usermode_request_list))
	{
		RequestNode* node = list_entry(iter, RequestNode, next);

		if (node->request_id > max_request_id)
			max_request_id = node->request_id;
	}

	request_node->request_id = max_request_id + 1;

	list_add(&(request_node->next), &(block_dev->pending_for_usermode_request_list));
}

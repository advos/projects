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

/*
static int goldenblock_handle_request(struct request *req)
{
 
    int dir = rq_data_dir(req);
    sector_t start_sector = blk_rq_pos(req);
    unsigned int sector_cnt = blk_rq_sectors(req);
 
    struct bio_vec *bv;
    struct req_iterator iter;
 
    sector_t sector_offset;
    unsigned int sectors;
    u8 *buffer;
 
    int ret = 0;
 
    sector_offset = 0;
    rq_for_each_segment(bv, req, iter)
    {
        buffer = page_address(bv->bv_page) + bv->bv_offset;
        if (bv->bv_len % RB_SECTOR_SIZE != 0)
        {
            printk(KERN_ERR "rb: Should never happen: "
                "bio size (%d) is not a multiple of RB_SECTOR_SIZE (%d).\n"
                "This may lead to data truncation.\n",
                bv->bv_len, RB_SECTOR_SIZE);
            ret = -EIO;
        }
        sectors = bv->bv_len / RB_SECTOR_SIZE;
        printk(KERN_DEBUG "rb: Sector Offset: %lld; Buffer: %p; Length: %d sectors\n",
            sector_offset, buffer, sectors);
        if (dir == WRITE) // Write to the device 
        {
            device_write(start_sector + sector_offset, buffer, bv->bv_len);//sectors);
        }
        else // Read from the device 
        {
            device_read(start_sector + sector_offset, buffer, bv->bv_len);//sectors);
        }
        sector_offset += sectors;
    }
    if (sector_offset != sector_cnt)
    {
        printk(KERN_ERR "rb: bio info doesn't match with the request info");
        ret = -EIO;
    }

    return ret;
}
*/

void goldenblock_request(struct request_queue* queue)
{
    struct request *req;

    /* Gets the current request from the dispatch queue */
    while ((req = blk_fetch_request(queue)) != NULL)
	{
		RequestNode* request_node = kzalloc(sizeof(*request_node), GFP_KERNEL);
		if (request_node == NULL)
			return; //Isn't supposed to happen. But if it does - it is very bad
	
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
	spin_lock_init(&(block->lock));

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

	block->gd->queue = blk_init_queue(goldenblock_request, &block->lock);
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

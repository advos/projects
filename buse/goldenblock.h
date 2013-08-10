#ifndef __GOLDENBLOCK_H__
#define __GOLDENBLOCK_H__

#include <linux/cred.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/blkdev.h>
#include "common.h"

#define GOLDEN_DEV_BLOCK_CLASS "GoldenGateClass2"

typedef struct RequestNode
{
	struct request* req;
	struct list_head next;
} RequestNode;

typedef struct GoldenBlock
{
	char name[MAX_NAME];
	kuid_t owner_uid;
	pid_t owner_pid;
	int internal_fd; //The key we use to search for the right goldenblock. Returned to user as a token for requesting operations on the device
	int major;
	spinlock_t lock;
	struct gendisk* gd;
	struct request_queue_t* queue;
	struct list_head list;
	struct list_head request_list;
	int refcount;
} GoldenBlock;

void golden_block_list_initialize(GoldenBlock* block);

int golden_block_create(char* name, int capacity, int minors, GoldenBlock** out);
void golden_block_destroy(GoldenBlock* block);
#define RB_SECTOR_SIZE 512
#endif

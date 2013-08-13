#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include "golden.h"
#include "goldenchar.h"
#include "goldenblock.h"

MODULE_LICENSE("Dual BSD/GPL");

static GoldenGate _golden;

static int golden_init(void)
{	
int retval = 0;

printk(KERN_ALERT "Golden Gate: Initializing...\n");

memset(&_golden, 0, sizeof(_golden));

sema_init(&(_golden.golden_lock), 1);

if (setup_goldenchar_device(&_golden, &_golden.gchar) < 0)
{
printk(KERN_ALERT "Golden Gate: Failed initializing Golden Char!\n");
retval = -1;
goto cleanup;
}

golden_block_list_initialize(&_golden.gblock_list_head);

cleanup:
if (retval < 0)
{
free_goldenchar_device(&_golden.gchar);
}

return 0;
}

static void golden_exit(void)
{
printk(KERN_ALERT "Golden Gate: Going Down!\n");
//free_goldenchar_device(&_golden.gchar);
}

module_init(golden_init);
module_exit(golden_exit);

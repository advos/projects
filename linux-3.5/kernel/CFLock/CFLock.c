#include <linux/init.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/semaphore.h>
#include <asm/errno.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <asm/uaccess.h> 
#include <linux/kernel.h> 
#include <linux/types.h>
#include <linux/CFLock.h>
#include <linux/sched.h>
//#include <asm/cmpxchg.h>




MODULE_LICENSE("Dual BSD/GPL");
#define CACHE_FREE_NUM_CORES 48
#define CACHE_FREE_LOCK_INIT_TERM_DBG_FLAG  1
#define CACHE_FREE_LOCK_IOCTL_DBG_FLAG	2


static int cache_free_lock_debug = (CACHE_FREE_LOCK_INIT_TERM_DBG_FLAG|CACHE_FREE_LOCK_IOCTL_DBG_FLAG);

//#define DBG_MSG(__flag, __fmt...)

#define DBG_MSG(__flag, __fmt...) \
do{\
	if(cache_free_lock_debug & __flag) \
		printk(KERN_ALERT __fmt); \
}while(0);
 
struct cache_free_lock_dev{
	dev_t dev;
	struct cdev cfl_cdev;
	
	void* lock;//ptr for lock
	unsigned long flags;
	enum CFL_LOCK_TYPE lock_type;
};


//define the lock operation contract
struct cfl_lops{
  int (*cfl_create)(struct cache_free_lock_dev *cfl_dev);
  int (*cfl_lock)(struct cache_free_lock_dev *cfl_dev, void *cookie);
  int (*cfl_unlock)(struct cache_free_lock_dev *cfl_dev, void *cooke);
  int (*cfl_destroy)(struct cache_free_lock_dev *cfl_dev);
};

//generic (wrapper) lock ops
static int cfl_generic_create(struct cache_free_lock_dev *cfl_dev, enum CFL_LOCK_TYPE lock_type);
static int cfl_generic_lock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_generic_unlock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_generic_destroy(struct cache_free_lock_dev *cfl_dev);

//ticket lock ops:
static int cfl_ticket_create(struct cache_free_lock_dev *cfl_dev);
static int cfl_ticket_lock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_ticket_unlock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_ticket_destroy(struct cache_free_lock_dev *cfl_dev);

//array lock ops:
static int cfl_arr_create(struct cache_free_lock_dev *cfl_dev);
static int cfl_arr_lock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_arr_unlock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_arr_destroy(struct cache_free_lock_dev *cfl_dev);

//mcs lock ops: per thread qnode
static int cfl_per_thread_mcs_create(struct cache_free_lock_dev *cfl_dev);
static int cfl_per_thread_mcs_lock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_per_thread_mcs_unlock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_per_thread_mcs_destroy(struct cache_free_lock_dev *cfl_dev);

//mcs lock ops: no qnode
static int cfl_nmcs_create(struct cache_free_lock_dev *cfl_dev);
static int cfl_nmcs_lock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_nmcs_unlock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_nmcs_destroy(struct cache_free_lock_dev *cfl_dev);
//mcs lock ops: per CPU qnode
static int cfl_per_cpu_mcs_create(struct cache_free_lock_dev *cfl_dev);
static int cfl_per_cpu_mcs_lock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_per_cpu_mcs_unlock(struct cache_free_lock_dev *cfl_dev, void *);
static int cfl_per_cpu_mcs_destroy(struct cache_free_lock_dev *cfl_dev);


//lock register fops struct
struct cfl_lops cfl_lock_ops[] =
{
	{cfl_ticket_create, cfl_ticket_lock, cfl_ticket_unlock, cfl_ticket_destroy},
	
	{cfl_arr_create, cfl_arr_lock, cfl_arr_unlock, cfl_arr_destroy},
	
	{cfl_per_thread_mcs_create, cfl_per_thread_mcs_lock, cfl_per_thread_mcs_unlock, cfl_per_thread_mcs_destroy},
	{cfl_nmcs_create, cfl_nmcs_lock, cfl_nmcs_unlock, cfl_nmcs_destroy},	
	{cfl_per_cpu_mcs_create, cfl_per_cpu_mcs_lock, cfl_per_cpu_mcs_unlock, cfl_per_cpu_mcs_destroy},
	

};

#define CFL_OPS_EXECUTE(__type, __func, __res, __action, __args...)\
	__res = -EINVAL; \
	if(__type < CFL_MAX_T) \
		__res = cfl_lock_ops[__type].__func(__args); \
	if(__res != 0) \
		__action;

static struct cache_free_lock_dev *cfl_dev = NULL;
static int cache_free_lock_open(struct inode *inode, struct file *filp);
static int cache_free_lock_release(struct inode *inode, struct file *filp);
static long cache_free_lock_unlock_ioctl (struct file *, unsigned int cmd, unsigned long arg);


static const struct file_operations cache_free_lock_fops = {
  .owner = THIS_MODULE,
  .open = cache_free_lock_open,
  .release = cache_free_lock_release,
  .unlocked_ioctl = cache_free_lock_unlock_ioctl,
};

static int cache_free_lock_open(struct inode *inode, struct file *filp)
{
  struct cache_free_lock_dev *c_f_lock_dev;
  
  c_f_lock_dev = container_of(inode->i_cdev, struct cache_free_lock_dev, cfl_cdev);
  filp->private_data = c_f_lock_dev;
 
  DBG_MSG(CACHE_FREE_LOCK_INIT_TERM_DBG_FLAG, "cache_free_lock_open\n");
  
  return 0;
}
  
static int cache_free_lock_release(struct inode *inode, struct file *filp)
{
	
  struct cache_free_lock_dev *c_f_lock_dev;  
  c_f_lock_dev = container_of(inode->i_cdev, struct cache_free_lock_dev, cfl_cdev);

  DBG_MSG(CACHE_FREE_LOCK_INIT_TERM_DBG_FLAG, "cache_free_lock_release\n");
 
  return 0;
}

static long cache_free_lock_unlock_ioctl (struct file * pfile, unsigned int cmd, unsigned long arg)
{
  int res;	

  struct cache_free_lock_dev *c_f_lock_dev = (struct cache_free_lock_dev *)pfile->private_data;

	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cache_free_lock_unlock_ioctl cmd 0x%x arg=%lu\n", cmd, arg);

	switch(cmd)
	{
		case IOCTL_CREATE_LOCK:
			res = cfl_generic_create(c_f_lock_dev, arg);
			if(res != 0)
				goto __err;
		break;
		case IOCTL_DOWN_LOCK:
				res = cfl_generic_lock(c_f_lock_dev, &arg);
			if(res != 0)
				goto __err;
		break;
		case IOCTL_UP_LOCK:
			res = cfl_generic_unlock(c_f_lock_dev, &arg);
			if(res != 0)
				goto __err;		
		break;
		case IOCTL_DESTROY_LOCK:
			res = cfl_generic_destroy(c_f_lock_dev);
			if(res != 0)
				goto __err;		
		break;
		default:
			DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cache_free_lock_unlock_ioctl illegal cmd=0x%x\n", cmd);

	}
  return 0;
  
__err:
	return res;
}

static int cfl_generic_create(struct cache_free_lock_dev *cfl_dev, enum CFL_LOCK_TYPE lock_type)
{
	int res;
	if(cfl_dev->lock != NULL)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_create failed - device exist !!\n");
		res = -EINVAL;
		goto __err;
	}
	//call specific lock device
	CFL_OPS_EXECUTE(lock_type, cfl_create, res, goto __err, cfl_dev);
	cfl_dev->lock_type = lock_type;	
	
	//some debug trace
	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_create OK, cfl_dev->lock=%p\n", cfl_dev->lock);
	return 0;

__err:
	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_create failed res=%d\n", res);
	return res;		
}

static int cfl_generic_lock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	int res;
	
	if(cfl_dev->lock == NULL)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_lock failed - device not exist !!\n");
		res = -EINVAL;
		goto __err;
	}
	
	//call specific lock device
	CFL_OPS_EXECUTE(cfl_dev->lock_type, cfl_lock, res, goto __err, cfl_dev, cookie);
	
	//some debug trace
	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_lock OK\n");
	return 0;

__err:
	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_lock failed res=%d\n", res);
	return res;		
}

static int cfl_generic_unlock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	int res;
	
	if(cfl_dev->lock == NULL)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_unlock failed - device not exist !!\n");
		res = -EINVAL;
		goto __err;
	}

	//call specific unlock device
	CFL_OPS_EXECUTE(cfl_dev->lock_type, cfl_unlock, res, goto __err, cfl_dev, cookie);
	
	//some debug trace
	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_unlock OK\n");
	return 0;

__err:
	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_unlock failed res=%d\n", res);
	return res;		
}


static int cfl_generic_destroy(struct cache_free_lock_dev *cfl_dev)
{
	int res;
	
	if(cfl_dev->lock == NULL)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_destroy failed - device not exist !!\n");
		res = -EINVAL;
		goto __err;
	}

	//call specific unlock device
	CFL_OPS_EXECUTE(cfl_dev->lock_type, cfl_destroy,res, goto __err, cfl_dev);
	//return to defaults
	cfl_dev->lock = NULL;
	cfl_dev->lock_type = 0;
	
	//some debug trace
	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_destroy OK\n");
	return 0;

__err:
	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_generic_destroy failed res=%d\n", res);
	return res;		
}


/*************************************TICKET LOCK SECTION*************************************/
static int cfl_ticket_create(struct cache_free_lock_dev *cfl_dev)
{
	int res = 0;
	spinlock_t *spinlock = NULL;
	
	spinlock = kmalloc(sizeof(spinlock_t), GFP_KERNEL);
	if (NULL == spinlock)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_ticket_create failed - out of mem !!\n");
		res = -ENOMEM;
		goto __err;
	}
 
	spin_lock_init(spinlock);
	cfl_dev->lock = spinlock;

	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_ticket_create spin_lock_init - %p!!\n", spinlock);

	return 0;
	
__err:  
	return res;
}

static int cfl_ticket_lock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	int res = 0;
	
	spinlock_t *spinlock = (spinlock_t *)cfl_dev->lock;
	if(spinlock == NULL)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "lock_ticket_lock failed - device not exist !!\n");
		res = -EINVAL;
		goto __err;
	}

 	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_ticket_lock spin_lock_init - %p!!\n", spinlock);
	
//	spin_lock_irqsave(spin_lock, cfl_dev->flags);
	spin_lock(spinlock);
	return 0;
	
__err:
	return res;
}

static int cfl_ticket_unlock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	int res = 0;
	
	spinlock_t *spinlock = (spinlock_t *)cfl_dev->lock;
	if(spinlock == NULL)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "lock_ticket_lock failed - device not exist !!\n");
		res = -EINVAL;
		goto __err;
	}
	
//	spin_unlock_irqrestore(spinlock, cfl_dev->flags);
	spin_unlock(spinlock);
	return 0;
	
__err:
	return res;
}

static int cfl_ticket_destroy(struct cache_free_lock_dev *cfl_dev)
{
	int res = 0;
	
	spinlock_t *spinlock = (spinlock_t *)cfl_dev->lock;
	if(spinlock == NULL)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_ticket_destroy failed - device not exist !!\n");
		res = -EINVAL;
		goto __err;
	}
	
	kfree(spinlock);
	
	return 0;
	
__err:
	return res;
}


/*************************************Array LOCK SECTION*************************************/

#define ARRSIZE 32
#define CACHE_LINE_SIZE		64
union cache_free_lock_arr{
    volatile char cache_line_size[CACHE_LINE_SIZE];
    volatile bool locked;
};

typedef union cache_free_lock_arr cache_free_lock_arr_t;
typedef union cache_free_lock_arr* cache_free_lock_arr_p;

struct arr_lock{
	cache_free_lock_arr_t arr [ARRSIZE];
	atomic_t index;
	int curr;
};
typedef struct arr_lock  arr_lock_t;

static inline void arr_lock_init(arr_lock_t* lock);
static inline void arr_lock(arr_lock_t* lock);
static inline void arr_unlock(arr_lock_t* lock);


static int cfl_arr_create(struct cache_free_lock_dev *cfl_dev)
{
	int res = 0;
	arr_lock_t *lock = NULL;
	
	lock = kmalloc(sizeof(arr_lock_t), GFP_KERNEL);
	if (NULL == lock)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_arr_create failed - out of mem !!\n");
		res = -ENOMEM;
		goto __err;
	}
 
	arr_lock_init(lock);
	cfl_dev->lock = lock;

	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_arr_create spin_lock_init - %p!!\n", lock);

	return 0;
	
__err:  
	return res;	
}

static int cfl_arr_lock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	arr_lock_t* lock = cfl_dev->lock;
	arr_lock(lock);
	return 0;
}

static int cfl_arr_unlock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	arr_lock_t* lock = cfl_dev->lock;
	arr_unlock(lock);
	
	return 0;
}

static int cfl_arr_destroy(struct cache_free_lock_dev *cfl_dev)
{
	arr_lock_t* lock = cfl_dev->lock;
	if(lock)
		kfree(lock);
		
	return 0;	
}

static inline void arr_lock_init(arr_lock_t* lock)
{
	memset(lock, 0, sizeof(arr_lock_t));
    atomic_set(&lock->index, 0);
    lock->curr = 0;
    lock->arr[0].locked = 1;
}

static inline void arr_lock(arr_lock_t* lock)
{
	int curr = 0;
	
	curr = (atomic_add_return(1, &lock->index)-1) % 32;
	while(lock->arr[curr].locked == 0){barrier();rep_nop();}
	lock->arr[curr].locked = 0;//for next time
	lock->curr = curr;
    
}

static inline void arr_unlock(arr_lock_t* lock)
{
	lock->arr[(lock->curr + 1) % 32].locked = 1; 
}


/*************************************TICKET LOCK SECTION*************************************/


//Generis MCS Section - all types use this basic operations:
#define MCS_LOCK_UNLOCKED (mcs_lock_t) NULL
#define MCS_NODE_UNLOCKED (mcs_node_t) {NULL, 0}

#define mcs_lock_init(x)	do { *(x) = MCS_LOCK_UNLOCKED; } while(0)
#define mcs_is_locked(x)	((*(mcs_note_t*)(x))!=NULL)
#define mcs_unlock_wait(x)	do { barrier(); } while(mcs_is_locked(x))

#define mcs_lock_irq(x,y) \
	do { local_irq_disable(); mcs_lock(x,y); } while(0)
#define mcs_unlock_irq(x,y) \
	do { mcs_unlock(x,y); local_irq_enable(); } while(0)
#define mcs_lock_irqsave(x,y,flags) \
	do { local_irq_save(flags); mcs_lock(x,y); } while(0)
#define mcs_unlock_irqrestore(x,y,flags) \
	do { mcs_unlock(x,y); local_irq_restore(flags); } while(0)

static inline void mcs_lock(mcs_lock_t* lock,  mcs_node_t* instance)
{
	mcs_node_t* before; 
	instance->next = NULL;
	before = xchg((mcs_node_t**)lock,instance);
	if (before != NULL)	{
		instance->locked = 1;
		before->next = instance;
		while(instance->locked){barrier();rep_nop();}
	}
}

static inline int mcs_trylock(mcs_lock_t* lock, mcs_node_t* instance)
{
	if(NULL == cmpxchg(lock,NULL,instance))
		return 1;
	return 0;
}

static inline void mcs_unlock(mcs_lock_t* lock, mcs_node_t* instance)
{
	if(instance->next == NULL) {
		if(instance == cmpxchg(lock,instance,NULL))
			return;
		while(instance->next == NULL){barrier();rep_nop();}
	}
	instance->next->locked = 0;
}




//mcs per cpu ops:
//use per cpu qnode (instance) to add to wait list - problematic when thread switch running CPU

//mcs_node_t per_cpu_node[CACHE_FREE_NUM_CORES];
//#define my__get_cpu_var per_cpu_node[get_cpu()]
DEFINE_PER_CPU(mcs_node_t, cache_free_lock_mcs_node);

static int inline cfl_per_cpu_mcs_create(struct cache_free_lock_dev *cfl_dev)
{
	int res = 0;
	mcs_lock_t *lock = NULL;

	
	lock = kmalloc(sizeof(mcs_lock_t), GFP_KERNEL);
	if (NULL == lock)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_per_cpu_mcs_create failed - out of mem !!\n");
		res = -ENOMEM;
		goto __err;
	}
 
	mcs_lock_init(lock);
	cfl_dev->lock = (void *)lock;

	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_per_cpu_mcs_create spin_lock_init - %p!!\n", lock);

	return 0;
	
__err:  
	return res;	
}

static int inline cfl_per_cpu_mcs_lock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	mcs_lock_t *lock = cfl_dev->lock;
	//mcs_node_t *instance = &__get_cpu_var(cache_free_lock_mcs_node);
	mcs_node_t *instance; 

	preempt_disable();
	
	//instance = &my__get_cpu_var;
	instance = &__get_cpu_var(cache_free_lock_mcs_node);
	*instance = MCS_NODE_UNLOCKED;	
	mcs_lock(lock, instance);
	return 0;
}

static int inline __cfl_per_cpu_mcs_unlock(struct cache_free_lock_dev *cfl_dev)
{
	mcs_node_t *instance = &__get_cpu_var(cache_free_lock_mcs_node);
	//mcs_node_t *instance = &my__get_cpu_var;
	mcs_lock_t *lock = cfl_dev->lock;
	
	mcs_unlock(lock, instance);
	return 0;
}

static int inline cfl_per_cpu_mcs_unlock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	__cfl_per_cpu_mcs_unlock(cfl_dev);
	preempt_enable();
	return 0;
}

static int inline cfl_per_cpu_mcs_destroy(struct cache_free_lock_dev *cfl_dev)
{
	mcs_lock_t *lock = cfl_dev->lock;
	if(lock)
		kfree((void *)lock);		
	return 0;	
}


//no qnode (instance) ops:
//use on stack qnode for lock MCS and extra flag (common for at most on extra thread) to signal nmcs lock state

struct nmcs_lock_t{
	mcs_lock_t queue;
	volatile int flag;
};

typedef struct nmcs_lock_t nmcs_lock_t;

#define NMCS_LOCK_UNLOCKED (nmcs_lock_t) {MCS_LOCK_UNLOCKED,0}

#define nmcs_lock_init(x)	do { *(x) = NMCS_LOCK_UNLOCKED; } while(0)
#define nmcs_is_locked(x)	((x)->flag != 0)
#define nmcs_unlock_wait(x)	do { barrier(); } while(nmcs_is_locked(x))

static inline void nmcs_lock(nmcs_lock_t* lock)
{
	mcs_node_t instance = MCS_NODE_UNLOCKED;
	mcs_lock(&lock->queue, &instance);
	while(lock->flag){barrier();rep_nop();}
	lock->flag =1;
	mcs_unlock(&lock->queue, &instance);	
}

static inline void nmcs_unlock(nmcs_lock_t* lock)
{
	lock->flag = 0;
}

static int cfl_nmcs_create(struct cache_free_lock_dev *cfl_dev)
{
	int res = 0;
	nmcs_lock_t *lock = NULL;
	
	lock = kmalloc(sizeof(nmcs_lock_t), GFP_KERNEL);
	if (NULL == lock)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_mcs_create failed - out of mem !!\n");
		res = -ENOMEM;
		goto __err;
	}
 
	nmcs_lock_init(lock);
	cfl_dev->lock = lock;

	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_mcs_create spin_lock_init - %p!!\n", lock);

	return 0;
	
__err:  
	return res;	
}

static int cfl_nmcs_lock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	nmcs_lock_t* lock = cfl_dev->lock;
	nmcs_lock(lock);
	return 0;
}

static int cfl_nmcs_unlock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	nmcs_lock_t* lock = cfl_dev->lock;
	nmcs_unlock(lock);
	
	return 0;
}

static int cfl_nmcs_destroy(struct cache_free_lock_dev *cfl_dev)
{
	nmcs_lock_t* lock = cfl_dev->lock;
	if(lock)
		kfree(lock);
		
	return 0;	
}


//mcs per thread ops:
//use per thread qnode (instance) to add to wait list - the same qnode (instace) can be used to lock diffrent locks not simultanity (no need to be also)

static int inline cfl_per_thread_mcs_create(struct cache_free_lock_dev *cfl_dev)
{
	int res = 0;
	mcs_lock_t *lock = NULL;

	
	lock = kmalloc(sizeof(mcs_lock_t), GFP_KERNEL);
	if (NULL == lock)
	{
		DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_mcs_create failed - out of mem !!\n");
		res = -ENOMEM;
		goto __err;
	}
 
	mcs_lock_init(lock);
	cfl_dev->lock = (void *)lock;
	
	DBG_MSG(CACHE_FREE_LOCK_IOCTL_DBG_FLAG, "cfl_mcs_create spin_lock_init - %p!!\n", lock);

	return 0;
	
__err:  
	return res;	
}

static int inline cfl_per_thread_mcs_lock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	mcs_node_t *thread_node;
	mcs_lock_t *lock;
	struct task_struct *task; 
	
	
	preempt_disable();

	lock = cfl_dev->lock;
	task = get_current(); 
	
	thread_node = &task->mcs_node;
	*thread_node = MCS_NODE_UNLOCKED;	
	mcs_lock(lock, thread_node);
	
	return 0;
}

static int inline __cfl_per_thread_mcs_unlock(mcs_lock_t *lock, mcs_node_t *instance)
{
	mcs_unlock(lock, instance);
	return 0;
}

static int inline cfl_per_thread_mcs_unlock(struct cache_free_lock_dev *cfl_dev, void *cookie)
{
	mcs_node_t *thread_node;
	mcs_lock_t *lock = cfl_dev->lock;
	struct task_struct *task = get_current(); 
	thread_node = &task->mcs_node;

	__cfl_per_thread_mcs_unlock(lock, thread_node);
	preempt_enable();
	return 0;
}

static int inline cfl_per_thread_mcs_destroy(struct cache_free_lock_dev *cfl_dev)
{
	mcs_lock_t *lock = cfl_dev->lock;
	if(lock)
		kfree((void *)lock);		
	return 0;	
}




static int __init cache_free_lock_init(void)
{
  int res;
   
  cfl_dev = kmalloc(sizeof(struct cache_free_lock_dev), GFP_KERNEL);
  if (NULL == cfl_dev)
  {
    res = -ENOMEM;
    goto __err;
  }
  memset(cfl_dev, 0, sizeof(struct cache_free_lock_dev));

	cfl_dev->dev = MKDEV(CFL_MAJOR, 0);
	res = register_chrdev_region(cfl_dev->dev, 1, "cflock00");

//  res = alloc_chrdev_region(&cfl_dev->dev, 0, 1, "cflock00");
  if (res < 0)
    goto __register_failed;

  DBG_MSG(CACHE_FREE_LOCK_INIT_TERM_DBG_FLAG, "cflock00 registered, major=%d minor=%d\n", MAJOR(cfl_dev->dev) , MINOR(cfl_dev->dev));
  
  cdev_init(&cfl_dev->cfl_cdev, &cache_free_lock_fops);

  res = cdev_add(&cfl_dev->cfl_cdev, cfl_dev->dev, 1);
  if (res < 0)
    goto __cdev_add_fail;
  
  return 0;
  
__cdev_add_fail:
__register_failed:
  unregister_chrdev_region(cfl_dev->dev, 1);
  kfree(cfl_dev);
__err:
  DBG_MSG(CACHE_FREE_LOCK_INIT_TERM_DBG_FLAG, "cache_free_lock_init failed, res=%d\n", res);

  return res;
  
}

static void __exit cache_free_lock_exit(void)
{
  
  cdev_del(&cfl_dev->cfl_cdev);
  unregister_chrdev_region(cfl_dev->dev, 1);	
  kfree(cfl_dev);
  
  DBG_MSG(CACHE_FREE_LOCK_INIT_TERM_DBG_FLAG, "cache_free_lock_exit\n");

}

module_init(cache_free_lock_init);
module_exit(cache_free_lock_exit);


#ifndef CFLOCK_H
#define CFLOCK_H

#define CFL_MAJOR	240

enum CFL_LOCK_TYPE{
	CFL_TICKET_T,
	CFL_ARR_T,

	CFL_PER_THREAD_MCS_T,	
	CFL_NMCS_T,
	CFL_PER_CPU_MCS_T,
	
	CFL_MAX_T
};

struct cache_free_lock_mcs
{
	volatile struct cache_free_lock_mcs *next;
	volatile bool locked;
};
typedef struct cache_free_lock_mcs* volatile mcs_lock_t;
typedef struct cache_free_lock_mcs mcs_node_t;


#define IOCTL_CREATE_LOCK 	_IOR(CFL_MAJOR, 0, enum CFL_LOCK_TYPE)
#define IOCTL_DOWN_LOCK 	_IOWR(CFL_MAJOR, 1, unsigned long)
#define IOCTL_UP_LOCK 		_IOWR(CFL_MAJOR, 2, unsigned long)
#define IOCTL_DESTROY_LOCK 	_IOR(CFL_MAJOR, 3, int)

#endif

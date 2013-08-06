
/*
 *  CFLockBenchmark.c - the process to evaluate diffrent locks
 */

/* 
 * device specifics, such as ioctl numbers and the
 * major device file. 
 */
 




#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <time.h>
#include <sched.h>

#include <fcntl.h>  	/* open */
#include <unistd.h>		/* exit */
#include <sys/ioctl.h>		/* ioctl */

typedef int bool;
#include <linux/CFLock.h>


#define DEVICE_FILE_NAME "/dev/CFLock"
#define NUM_OF_THREADS 100
#define NUM_OF_LOCK_UNLOCK_LOOP	500
#define NUM_OF_CORES 4
#define TESTER_CNT_VAL (NUM_OF_LOCK_UNLOCK_LOOP*NUM_OF_THREADS)
#define SLEEP_TIME_BEFORE_RUN	1
#define GET_EXEC_TIME(func,time)       \
({                                     \
    int _current = CFLOCK_GetTime();   \
    typeof(func) _err = func;          \
    time = CFLOCK_GetTime() - _current - SLEEP_TIME_BEFORE_RUN*1000;\
    _err;                              \
})

typedef struct{
	enum CFL_LOCK_TYPE lock_type;
	char lock_name[50];
	int time;
	int avg;
}CFLOCK_bench_mark_descr_t;

/* 
 * Functions for the ioctl calls 
 */

int CFLOCK_ioctl_create_lock(int file_desc, enum CFL_LOCK_TYPE type)
{
	int ret_val;

	ret_val = ioctl(file_desc, IOCTL_CREATE_LOCK, type);

	if (ret_val < 0) {
		printf("CFLOCK_ioctl_create_lock failed:%d\n", ret_val);
		exit(-1);
	}
	
	return 0;
}

int CFLOCK_ioctl_down_lock(int file_desc, mcs_node_t **instance)
{
	int ret_val;

	ret_val = ioctl(file_desc, IOCTL_DOWN_LOCK, instance);

	if (ret_val < 0) {
		printf("CFLOCK_ioctl_down_lock failed:%d\n", ret_val);
		exit(-1);
	}	
	return 0;
}

int CFLOCK_ioctl_up_lock(int file_desc, mcs_node_t **instance)
{
	int ret_val;

	ret_val = ioctl(file_desc, IOCTL_UP_LOCK, instance);

	if (ret_val < 0) {
		printf("CFLOCK_ioctl_up_lock failed:%d\n", ret_val);
		exit(-1);
	}	
	return 0;
}

int CFLOCK_ioctl_destroy_lock(int file_desc)
{
	int ret_val;

	ret_val = ioctl(file_desc, IOCTL_DESTROY_LOCK, 0);

	if (ret_val < 0) {
		printf("CFLOCK_ioctl_destroy_lock failed:%d\n", ret_val);
		exit(-1);
	}	
	return 0;
}

static int tester = 0;
static int cpu=0;
void *CFLOCK_thread_lock_func( void *cookie )
{
	int file_desc = *(int *)cookie;
	int i = 0;
	int tmp;
	int ret_val;	
	struct sched_param param;
	int priority = 1;
	int  mask;
	mcs_node_t instance;
	mcs_node_t *ins = &instance;
	
	
	/* sched_priority will be the priority of the thread */
	param.sched_priority = priority;

	/* scheduling parameters of target thread */	
	ret_val = pthread_setschedparam(pthread_self(), SCHED_FIFO, &param); 
	if (ret_val < 0) {
		printf("thread_lock_func:pthread_setschedparam failed:%d\n", ret_val);
		exit(-1);
	}
	
	mask = (cpu%NUM_OF_CORES)+1;
	cpu++;
	
	ret_val = pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);
	if (ret_val < 0) {
		printf("thread_lock_func:pthread_setaffinity_np failed:%d\n", ret_val);
		exit(-1);
	}	
	sleep(SLEEP_TIME_BEFORE_RUN);

	for(i=0;i<NUM_OF_LOCK_UNLOCK_LOOP;i++)
	{
		ret_val |= CFLOCK_ioctl_down_lock(file_desc, &ins);
		tmp = tester;
//		ret_val |= sched_yield();	
		tester = tmp + 1;
		ret_val |= CFLOCK_ioctl_up_lock(file_desc, &ins);
	}
	
	if (ret_val < 0) {
		printf("thread_lock_func:CFLOCK_ioctl_up_lock failed:%d\n", ret_val);
		exit(-1);
	}	
	
	return NULL;
}

int CFLOCK_benchmark_execute(enum CFL_LOCK_TYPE type)
{
	int file_desc;
    pthread_t thread[NUM_OF_THREADS];
	int i=0;
	int res = 0;
	
	file_desc = open(DEVICE_FILE_NAME, 0);
	if (file_desc < 0) {
		printf("Can't open device file: %s\n", DEVICE_FILE_NAME);
		exit(-1);
	}

	res = CFLOCK_ioctl_create_lock(file_desc, type);
	if(res < 0) {
		printf("Can't create lock device: %d\n", type);
		exit(-1);		
	}
	
	cpu=tester = 0; 
	for(i=0;i<NUM_OF_THREADS;i++)
		pthread_create( &thread[i], NULL, CFLOCK_thread_lock_func, (void*) &file_desc);

	for(i=0;i<NUM_OF_THREADS;i++)
		pthread_join( thread[i], NULL);

    res = CFLOCK_ioctl_destroy_lock(file_desc);
	if(res < 0) {
		printf("Can't destroy lock device: %d\n", type);
		exit(-1);		
	}

	res = close(file_desc);
	if(res < 0) {
		printf("Can't close lock device: %d\n", type);
		exit(-1);		
	}

	if(tester != TESTER_CNT_VAL) {
		printf("Tester value not correct: %d instead of %d\n", tester, TESTER_CNT_VAL);
		exit(-1);		
	}
	
	return 0;
}

int CFLOCK_GetTime()
{
	static int first = 1;
	struct timespec tv;
	static struct timespec base;
	/* first time - get the offset */
	if (first)
	{
		clock_gettime(CLOCK_MONOTONIC,&base);
		first = 0;
	};
	clock_gettime(CLOCK_MONOTONIC,&tv);
	return((tv.tv_sec-base.tv_sec) * 1000 + (tv.tv_nsec-base.tv_nsec)/ 1000000);
};

/* 
 * Main - benchmark process 
 */

int main()
{
	int i = 0;
	int res = 0;
	int loop = 0;

	CFLOCK_bench_mark_descr_t lock_device_arr [] = 
	{
		{CFL_TICKET_T, "ticket lock", 0},
		{CFL_NMCS_T, "nmcs lock", 0},
		{CFL_PER_CPU_MCS_T, "per cpu mcs lock", 0},
		{CFL_PER_THREAD_MCS_T, "per thread mcs lock", 0},		
	};
	
_start:	
	for(i=0;i<(sizeof(lock_device_arr)/sizeof(CFLOCK_bench_mark_descr_t));i++)
	{
		res = GET_EXEC_TIME(CFLOCK_benchmark_execute(lock_device_arr[i].lock_type), lock_device_arr[i].time);
		if(res < 0) {
			printf("Failed CFLOCK_benchmark_execute: %d\n", lock_device_arr[i].lock_type);
			exit(-1);		
		}
		lock_device_arr[i].avg = (loop == 0)?lock_device_arr[i].time:(((loop*lock_device_arr[i].avg) + lock_device_arr[i].time)*1000/(loop+1)/1000);
		printf("%d) %s\t curr time[ms]=%d\t avg[ms]=%d\n", loop, lock_device_arr[i].lock_name, lock_device_arr[i].time, lock_device_arr[i].avg);
	}
	loop++;
if(loop < 10) {goto _start;}
	return 0;
}

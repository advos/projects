#ifndef _LINUX_SANDBOX_CHAR
#define _LINUX_SANDBOX_CHAR

#include "sandbox_algorithm.h"

#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/cdev.h>

#include <asm/uaccess.h> // copy_from_user()
#include <asm/bitops.h> //bitmap

#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/fcntl.h> /* O_ACCMODE */
#include <linux/list.h> /* O_ACCMODE */

struct sandbox_class * get_sandbox(unsigned long sandbox_id);
void sandbox_set_jail(struct sandbox_class * sandbox, const char * jail_dir);
void sandbox_set_strip_files(struct sandbox_class * sandbox, bool strip_files);
void init_sandbox(struct sandbox_class * sandbox);
void _clear_pointer(void * ptr);
void sandbox_set_syscall_bit(struct sandbox_class * sandbox, unsigned int syscall_num, bool bitval);
int find_file(struct sandbox_class * sandbox, const char * filename);
int find_ip(struct sandbox_class * sandbox, const char * ip);

struct sandbox_struct {
  int opens;
  int releases;
  int writes;
  int reads;
  char *buff;
	char* answer; //output buff
  int buffsize;
  int size;
  struct semaphore sem;
  dev_t dev;
  struct cdev my_cdev;
};


#endif /* _LINUX_SANDBOX_CHAR */

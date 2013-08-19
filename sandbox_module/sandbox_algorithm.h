#ifndef _LINUX_SANDBOX_ALGORITHM
#define _LINUX_SANDBOX_ALGORITHM

#include <linux/kernel.h>
#include <linux/bitops.h>
#include <linux/types.h>

/* didn't find from where to import NR_SYSCALLS */
#define NUM_OF_SYSCALLS 351
#define IP_MAX 25
#define FILE_MAX 255

struct file_exception {
  char * filename;
  size_t len;
  struct list_head list;
};

struct ip_exception {
  char * ip_address;
  struct list_head list;
};

/* holds the sandbox configuration */
struct sandbox_class {
  /* file settings */
  char * fs_root;
  bool strip_files;
  bool disallow_files_by_default;
  struct file_exception * file_list;
  
  bool disallow_ips_by_default;
  struct ip_exception * ip_list;
  
  /* 
     this bitmap disallows certain syscalls from running.
     if the bit syscalls[SYSCALL_NUM] == 1 then 
     the system call identified by SYSCALL_NUM is disabled.
  */
  DECLARE_BITMAP(syscalls, NUM_OF_SYSCALLS);
};

/* exported data structures */
extern struct sandbox_class * sandbox_list;

#endif /* _LINUX_SANDBOX_ALGORITHM */

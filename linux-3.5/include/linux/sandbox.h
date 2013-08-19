#ifndef _LINUX_SANDBOX
#define _LINUX_SANDBOX

#include <linux/kernel.h>
#include <linux/sched.h>

#define NUM_SANDBOXES 10
#define BLOCK_SYSCALL 1
#define SYSCALL_OK 0

/* holds the sandbox module event handlers */
struct sandbox_algorithm_ops {
  /* this handler is called when the user process calls
     sys_switch_sandbox in order to run in a sandboxed environment.
     
     this function should close open resources and leave open only allowed
     resources according to the sandbox configuration. 
  */
  int (*enter_callback) (unsigned long sandbox_id);

  /* this handler is called when the user process invokes a system call.
     the sandbox mechanism then decides whether to allow or block the syscall.
  */
  int (*syscall_callback) (int syscall_num);

  /* this handler is called when the user process attempts to open a file.
  */
  int (*open_callback) (const char * __user filename);

  /* this handler is called when the user process attempts invoke connect.
  */
  int (*connect_callback) (void);

  /* this handler is called when the user process attempts invoke bind.
  */
  int (*bind_callback) (void);
	
	/* this handler called when user try to deliver a signal from sandboxed process
	*/
	int (*kill_callback) (int sig_number,unsigned long sandboxid_signaling,unsigned long sandbox_id_signaled);
};

extern struct sandbox_algorithm_ops * sandbox_algorithm;

/* exported functions */
extern asmlinkage int sandbox_block_syscall(int syscall_num);

#endif /* _LINUX_SANDBOX */

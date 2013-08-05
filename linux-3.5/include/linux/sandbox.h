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
  int (*enter_callback) (struct task_struct * t, unsigned long sandbox_id);

  /* this handler is called when the user process invokes a system call.
     the sandbox mechanism then decides whether to allow or block the syscall.
  */
  int (*syscall_callback) (int syscall_num);
};

extern struct sandbox_algorithm_ops * sandbox_algorithm;

/* exported functions */
extern asmlinkage int sandbox_block_syscall(int syscall_num);

#endif /* _LINUX_SANDBOX */

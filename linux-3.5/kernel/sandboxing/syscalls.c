#include <linux/sandbox.h>
#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/linkage.h>
#include <linux/syscalls.h>
#include <linux/bitops.h>
#include <linux/compiler.h>
#include <linux/sched.h>

SYSCALL_DEFINE0(get_sandbox_id)
{
  return current->sandbox_id;
}

SYSCALL_DEFINE1(switch_sandbox, unsigned long, sandbox_id)
{
  int ret = 0;

  if (0 != current->sandbox_id) {
    printk(KERN_ALERT "switch_sandbox() is available only to sandbox 0 processes.\n");
    return -EINVAL;
  }

  if (NUM_SANDBOXES <= sandbox_id) {
    printk(KERN_ALERT "No such sandbox.\n");
    return -EINVAL;
  }

  /* first, enter_callback() strips the task from unwarrented resources */
  if (sandbox_algorithm->enter_callback) {
    ret = sandbox_algorithm->enter_callback(sandbox_id);
  }
  
  return ret;
}

asmlinkage int sandbox_block_syscall(int syscall_num)
{
  if (0 != current->sandbox_id) {
    printk(KERN_ALERT "SPECIAL: sandbox_block_syscall(%d) with sandbox (%ld)\n", 
	   syscall_num, current->sandbox_id);
  }

  if (sandbox_algorithm->syscall_callback) {
    return sandbox_algorithm->syscall_callback(syscall_num);
  }
  return SYSCALL_OK;
}


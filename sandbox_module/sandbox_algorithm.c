#include <linux/init.h>
#include <linux/module.h>
#include <linux/export.h>

#include <linux/kernel.h>
#include <linux/printk.h>
#include <linux/syscalls.h>
#include <linux/unistd.h>
#include <linux/slab.h> /* kmalloc, kfree */

#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/fdtable.h>

#include <linux/types.h>
#include <linux/string.h> /* strncmp, strnlen */
#include <linux/limits.h> /* PATH_MAX */
#include <linux/bitops.h>
#include <linux/bitmap.h>

#include <asm/uaccess.h> /* copy_to_user */

#include <linux/sandbox.h>
#include "sandbox_algorithm.h"

MODULE_LICENSE("Dual BSD/GPL");

#define current_sandbox (sandbox_list[current->sandbox_id])

/*******************************************************************************
  module private data
*******************************************************************************/
/* this array holds each sandbox configuration */
static struct sandbox_class sandbox_array[NUM_SANDBOXES];
struct sandbox_class * sandbox_list = sandbox_array;

/*******************************************************************************
  kernel operations
*******************************************************************************/
static void _chroot_jail(const char __user * new_root)
{
  int res = 0;

  res = sys_chdir(new_root);
  printk(KERN_ALERT "sys_chdir() returned %d\n", res);
  res = sys_chroot(new_root);
  printk(KERN_ALERT "sys_chroot() returned %d\n", res);
}

void _strip_files(void)
{
  struct file * filp;
  struct fdtable *fdt;
  unsigned int i;
  
  fdt = files_fdtable(current->files);
  for (i = 0; i < fdt->max_fds; i++) {
    filp = fdt->fd[i];
    if (filp) {
      sys_close(i);
    }
  }
}


static int _compare_entries(struct file_exception * entry, 
			    const char * filename, __kernel_size_t len)
{
  if (len != entry->len) {
    return (int)false;
  }
    
  /* filename lengths are equal */
  if (0 == strncmp(filename, entry->filename, len)) {
    return (int)true;
  }

  return (int)false;
}

static int find_file(struct sandbox_class * sandbox, const char * filename)
{
  struct list_head * tmp;
  struct file_exception * entry = NULL;
  __kernel_size_t len = strnlen(filename, PATH_MAX);
  
  if (NULL == sandbox->file_list) {
    return (int)false;
  }
  
  /* first member */
  entry = sandbox->file_list;
  if (_compare_entries(entry, filename, len)) {
    return (int)true;
  }
  
  list_for_each(tmp, &sandbox->file_list->list) {
    entry = list_entry(tmp, struct file_exception, list);
    if (_compare_entries(entry, filename, len)) {
      return (int)true;
    }
  }
  return (int)false;
}

static int _allow_only_for_sandbox0(void)
{
  /* returns 0 sandbox 0, 1 for every other sandbox */
  return (!(0 == current->sandbox_id));
}

/*******************************************************************************
  kernel hooks
*******************************************************************************/
static int sandbox_enter(unsigned long sandbox_id)
{
  struct sandbox_class * sandbox = &sandbox_list[sandbox_id];
  
  if (sandbox->strip_files) {
    _strip_files();
  }

  /* trap the process in a chroot jail */
  if (NULL != sandbox->fs_root) {
    printk(KERN_ALERT "doing _chroot_jail(%s)\n", sandbox->fs_root);
    _chroot_jail(sandbox->fs_root);
  }

  /* then we change the sandbox id in the task struct.
     that is because after changing the sandbox id many
     os operations are blocked by the sandbox hooks. 
  */
  current->sandbox_id = sandbox_id;

  return 0;
}

static int sandbox_syscall(int syscall_num)
{
  if (0 == current->sandbox_id) {
    return SYSCALL_OK;
  }
  printk("called sandbox_syscall(%d) in sandbox(%ld)\n", syscall_num, current->sandbox_id);
  if (unlikely(test_bit(syscall_num, current_sandbox.syscalls))) {
    printk("bit %d is set in current sandbox (%ld)\n", syscall_num, current->sandbox_id);
    return BLOCK_SYSCALL;
  }
  return SYSCALL_OK;
}

static int sandbox_open(const char * __user filename)
{
  struct sandbox_class * sandbox = &sandbox_list[current->sandbox_id];
  bool file_found = false;
  int retval = 0;

  file_found = (bool) find_file(sandbox, filename);
  if (sandbox->disallow_files_by_default) {
    /* if we disallow files by default, than if the file is NOT found, it is disallowed */
    retval = (int) (file_found);
  } else {
    /* files are allowed by default, hence if the file is NOT in the list, we allow it. */
    retval = (int) (!file_found);
  }

  return retval;
}

static int sandbox_connect(void)
{
  return _allow_only_for_sandbox0();
}

static int sandbox_bind(void)
{
  return _allow_only_for_sandbox0();
}

/*******************************************************************************
  sandbox administration (exported to char device module)
*******************************************************************************/
static struct sandbox_class * get_sandbox(unsigned long sandbox_id)
{
  if (NUM_SANDBOXES <= sandbox_id) {
    return NULL;
  }
  return &sandbox_list[sandbox_id];
}

static void sandbox_set_jail(struct sandbox_class * sandbox, const char * jail_dir)
{
  sandbox->fs_root = (char *) jail_dir;
}

static void sandbox_set_strip_files(struct sandbox_class * sandbox, bool strip_files)
{
  sandbox->strip_files = strip_files;
}

static void sandbox_set_syscall_bit(struct sandbox_class * sandbox, unsigned int syscall_num, bool bitval)
{
  if (bitval) {
    set_bit(syscall_num, sandbox->syscalls);
  } else {
    clear_bit(syscall_num, sandbox->syscalls);
  }
}

void _clear_pointer(void * ptr)
{
  if (NULL != ptr) {
    kfree(ptr);
  }
  ptr = NULL;
}

/* init_sandbox() should load the data structure
   with the default and most permissive configuration */
static void init_sandbox(struct sandbox_class * sandbox) {
  _clear_pointer(sandbox->fs_root);
  _clear_pointer(sandbox->file_list);
  _clear_pointer(sandbox->ip_list);

  sandbox->strip_files = 0;
  sandbox->disallow_files_by_default = false;
  sandbox->disallow_ips_by_default = false;
  
  bitmap_zero(sandbox->syscalls, NUM_OF_SYSCALLS);
}

/* i currently init sandbox number 1 to deny 
   one system call (getpid) for testing */ 
#define BLOCKED_SYSCALL (20)
static void init_limited_sandbox(struct sandbox_class * sandbox) 
{ 
  const char * jail = "/var/jail/";
  const char * allowed_a = "a.txt";
  const char * allowed_b = "b.txt";
  const char * allowed_c = "c.txt";
  
  size_t ex_file_size = strnlen(allowed_a, PATH_MAX);
  struct file_exception * file_a = kmalloc(sizeof(struct file_exception), GFP_KERNEL);
  struct file_exception * file_b = kmalloc(sizeof(struct file_exception), GFP_KERNEL);
  struct file_exception * file_c = kmalloc(sizeof(struct file_exception), GFP_KERNEL);
  
  sandbox_set_jail(sandbox, jail);
  sandbox_set_strip_files(sandbox, true);
  sandbox_set_syscall_bit(sandbox, BLOCKED_SYSCALL, true);

  file_a->filename = (char *) allowed_a;
  file_a->len = ex_file_size;
  INIT_LIST_HEAD(&file_a->list);

  file_b->filename = (char *) allowed_b;
  file_b->len = ex_file_size;
  list_add(&file_b->list, &file_a->list);

  file_c->filename = (char *) allowed_c;
  file_c->len = ex_file_size;
  list_add(&file_c->list, &file_a->list);

  sandbox->disallow_files_by_default = true;
  sandbox->file_list = file_a;
}

void init_sandbox_list(void) {
  int i = 0;

  for(i=0; i < NUM_SANDBOXES; i++) {
    init_sandbox(get_sandbox(i));
  }
  /* temp */
  init_limited_sandbox(get_sandbox(1));
}

/*******************************************************************************
  module init and exit
*******************************************************************************/
static int __init sandbox_init(void)
{
  init_sandbox_list();

  sandbox_algorithm->enter_callback = sandbox_enter;
  sandbox_algorithm->syscall_callback = sandbox_syscall;
  sandbox_algorithm->open_callback = sandbox_open;
  sandbox_algorithm->connect_callback = sandbox_connect;
  sandbox_algorithm->bind_callback = sandbox_bind;

  printk(KERN_ALERT "sandbox module loaded.\n");
  return 0;
}


static void __exit sandbox_exit(void)
{
  sandbox_algorithm->enter_callback = NULL;
  sandbox_algorithm->syscall_callback = NULL;
  sandbox_algorithm->open_callback = NULL;
  sandbox_algorithm->connect_callback = NULL;
  sandbox_algorithm->bind_callback = NULL;
  printk(KERN_ALERT "sandbox module unloaded.\n");
}

module_init(sandbox_init);
module_exit(sandbox_exit);

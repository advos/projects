#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/netdevice.h>

#include <linux/init.h>         /* Needed for the macros */
#include <linux/netfilter.h>
#include <linux/netfilter_ipv4.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <net/ip.h>
#include <linux/gfp.h>
#include <linux/string.h>
#include <linux/if.h>
#include <linux/in.h>
#include <linux/tcp.h>
#include <linux/udp.h>
#include <linux/timer.h>
#include <linux/jiffies.h>

#include <linux/semaphore.h>

#ifndef __SFUSION_H__
#define __SFUSION_H__

#define DEVICE_NAME "sfusion"
#define BUFFER_SIZE 1000

typedef struct device_list device_list;
typedef struct rule_list rule_list;

int device_init(void);
void device_exit(void);
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);

#define WORD_SEPARATOR ' '
#define NEWLINE_SEPARATOR '\n'
#define EOL_SEPARATOR '\0'
#define VALUE_SEPARATOR ','
#define SUBNET_SEPARATOR '.'
#define CIDR_SEPARATOR '/'

static ssize_t get_next_word(const char *input, char *output, int *input_length, int *input_offset, const int separator);
static char *strclone(const char *str);
static bool strmatch(const char* str1, const char* str2);
static ssize_t strcount(const char *str,const ssize_t length, const int separator);
static unsigned int loadbalance_hook(unsigned int hooknum,
									struct sk_buff *skb,
									const struct net_device *in,
									const struct net_device *out,
									int (*okfn)(struct sk_buff *));
static int init_netfilter_loadbalancer_hook(void);
static bool should_loadbalance(struct sk_buff *skb);
static struct net_device* get_new_device(void);
void set_bandwidth(unsigned long data);
static void update_device_counters(struct sk_buff *skb);

#endif

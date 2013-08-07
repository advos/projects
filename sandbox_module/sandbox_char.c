#include "sandbox_algorithm.h"
#include <linux/init.h>
#include <linux/module.h>

#include <linux/fs.h>
#include <linux/cdev.h>

#include <asm/uaccess.h> // copy_from_user()

#include <linux/kernel.h> /* printk() */
#include <linux/slab.h> /* kmalloc() */
#include <linux/fs.h> /* everything... */
#include <linux/errno.h> /* error codes */
#include <linux/types.h> /* size_t */
#include <linux/fcntl.h> /* O_ACCMODE */


MODULE_LICENSE("Dual BSD/GPL");

struct sandbox_class * get_sandbox(unsigned long sandbox_id);
void sandbox_set_jail(struct sandbox_class * sandbox, const char * jail_dir);
void sandbox_set_strip_files(struct sandbox_class * sandbox, bool strip_files);
void init_sandbox(struct sandbox_class * sandbox);
void _clear_pointer(void * ptr);

struct sandbox_struct {
  int opens;
  int releases;
  int writes;
  int reads;
  char *buff;
  int buffsize;
  int size;
  dev_t dev;
  struct cdev my_cdev;
};
static char* FAIL_STRING = "The requested sandbox query have been failed";
static char* SUCCESS_STRING = "The requested sandbox query have been succeed";
static struct sandbox_struct *mydev = NULL;


static int sandbox_open(struct inode *inode, struct file *filp)
{
  struct sandbox_struct *mydev;
  
  mydev = container_of(inode->i_cdev, struct sandbox_struct, my_cdev);
  filp->private_data = mydev;
    
  printk(KERN_ALERT "sandbox_open called %d times\n", ++mydev->opens);
  return 0;
}

static ssize_t sandbox_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
  int res, wb;
  struct sandbox_struct *mydev = filp->private_data;
    
  wb = (count < mydev->size - *offp) ? count : mydev->size - *offp;
  
  res = copy_to_user(buff, mydev->buff+*offp, wb);
  if (res == 0)
    printk(KERN_ALERT "sandbox_read called %d timed\n", ++mydev->reads);

  *offp += wb - res;
  res = wb - res;
  
  return res;
}

static int split_to_tokens(const char *request, char **tokens,int input_len, const int separator)
{
	//TODO: check buffer overflow
	char *found;
	int current_token=0;
	int word_len = 0;
	while(1)
	{
		found = strchr(request, separator);
		if(found == NULL)
			word_len = input_len - 1;
		else
			word_len = found - request;	

		if(word_len != 0)
		{
			//copy the token
			memcpy(tokens[current_token], request, word_len);
			//null
			tokens[current_token][word_len] = '\0';
			printk(KERN_ALERT "current token %s", tokens[current_token]);
		}
		current_token+=1;
		if(found == NULL)
			break;		
		request = found+1;
		input_len--;
	}
	return current_token;
}
static void free_memory(char** tokens,const int length)
{
	int i;
	for(i = 0 ; i < length ; i++)
	{
		kfree(tokens[i]);
	}
	kfree(tokens);
}
static void request_processing(struct file *filp)
{
	//TODO: change printk to write in the buffer
	int sandbox_num;
	int num_tokens;
	int i;
  struct sandbox_struct *mydev = filp->private_data;
	struct sandbox_class * class = NULL;
	int length = strlen(mydev->buff);

	char** tokens = kmalloc(length * sizeof(char*), GFP_KERNEL);
	for(i = 0 ; i < length ; i++)
	{
		tokens[i] = kmalloc(length * sizeof(char), GFP_KERNEL);
	}

	//printk(KERN_ALERT "init sandbox request %d",length);
	//return;

	num_tokens = split_to_tokens(mydev->buff,tokens,length,' ');
	
	//process request
	if(!strcmp(tokens[0],"init") && !strcmp(tokens[1],"sandbox") && (num_tokens == 3))
	{
  	sandbox_num = simple_strtol(tokens[2], NULL, 10);
		class = get_sandbox(sandbox_num);
		if(NULL == class)
		{
			printk("sandbox number %d is out of the range",sandbox_num);
			goto failure;
		}
		init_sandbox(class);
		goto success;
	}
	if(!strcmp(tokens[0],"sandbox"))
	{
		//get sandbox by its id
  	sandbox_num = simple_strtol(tokens[1], NULL, 10);
		class = get_sandbox(sandbox_num);
		if(NULL == class)
		{
			printk("sandbox number %d is out of the range",sandbox_num);
			goto failure;
		}
		
		//TODO: macro to check the path, path with whitespaces
		if(num_tokens >= 5 && !strcmp(tokens[2],"jail") && !strcmp(tokens[3],"in"))
		{
			//form path
			sandbox_set_jail(class,tokens[4]);
			goto success;
		}
		if(num_tokens == 4 && !strcmp(tokens[2],"strip") && !strcmp(tokens[3],"files"))
		{
			sandbox_set_strip_files(class,1);
			goto success;
		}
		if(num_tokens == 6 && !strcmp(tokens[2],"set") && !strcmp(tokens[3],"file") && !strcmp(tokens[4],"mode"))
		{
			if(strcmp(tokens[5],"allow"))
			{
				//TODO: correct callback
				goto success;
			}
			if(strcmp(tokens[5],"disallow"))
			{
				//TODO: correct callback
				goto success;
			}
			goto failure;
		}

		
	}
	goto failure;
	
success:
	free_memory(tokens,length);
	printk(KERN_ALERT "%s",SUCCESS_STRING);
	return;
failure:
	free_memory(tokens,length);
	printk(KERN_ALERT "%s",FAIL_STRING);
	return;
}

static ssize_t sandbox_write(struct file *filp, const char __user *buff, size_t count, loff_t *offp)
{
  int res;
  struct sandbox_struct *mydev = filp->private_data;

  if (count + 1 > mydev->buffsize)
  {
    kfree(mydev->buff);
    mydev->buff = kmalloc(count+1, GFP_KERNEL);
    if (NULL == mydev->buff)
    {
      mydev->buffsize = 0;
      return -ENOMEM;
    }
    mydev->buffsize = count + 1;
  }
  
  res = copy_from_user(mydev->buff, buff, count);
  mydev->buff[count] = 0;
  mydev->size = count;
  
	request_processing(filp);
  if (!res) // success
    printk(KERN_ALERT "sandbox_write called %d times. buff: %s\n", ++mydev->writes, mydev->buff);

  return count - res;
}
  
static int sandbox_release(struct inode *inode, struct file *filp)
{
  struct sandbox_struct *mydev = filp->private_data;

  printk(KERN_ALERT "sandbox_release called %d times (buffsize: %d)\n", ++mydev->releases, mydev->buffsize);
  
  return 0;
}


static const struct file_operations sandbox_fops = {
  .owner = THIS_MODULE,
  .open = sandbox_open,
  .read = sandbox_read,
  .write = sandbox_write,
  .release = sandbox_release
};

static int sandbox_init(void)
{
  int res;
  
  mydev = kmalloc(sizeof(*mydev), GFP_KERNEL);
  if (NULL == mydev)
  {
    res = -ENOMEM;
    goto register_failed;
  }
  memset(mydev, 0, sizeof(*mydev));

  res = alloc_chrdev_region(&mydev->dev, 0, 1, "sandbox");
  if (res < 0)
    goto register_failed;

  
  printk(KERN_ALERT "sandbox registered\n");
  cdev_init(&mydev->my_cdev, &sandbox_fops);

  res = cdev_add(&mydev->my_cdev, mydev->dev, 1);
  if (res < 0)
    goto cdev_fail;
    
  return 0;
  
  cdev_fail:
  printk(KERN_ALERT "cdev registration failed... unregistering\n");
  unregister_chrdev_region(mydev->dev, 1);
  register_failed:
  return res;
}

static void sandbox_exit(void)
{
  cdev_del(&mydev->my_cdev);
  printk(KERN_ALERT "sandbox cdev unregistered\n");
  unregister_chrdev_region(mydev->dev, 1);
  printk(KERN_ALERT "sandbox unregistered\n");
  if (mydev->buffsize)
  {
    kfree(mydev->buff);
    mydev->buff = NULL;
    mydev->buffsize = 0;
  }
  kfree(mydev);
}

module_init(sandbox_init);
module_exit(sandbox_exit);


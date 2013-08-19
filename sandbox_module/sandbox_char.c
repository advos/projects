#include "sandbox_char.h"


MODULE_LICENSE("Dual BSD/GPL");


static char* FAIL_STRING = "The requested sandbox query have been failed";
static char* SUCCESS_STRING = "The requested sandbox query have been succeed";
static struct sandbox_struct *mydev = NULL;

/*
*open function for sandbox char device
*/
static int sandbox_open(struct inode *inode, struct file *filp)
{
	struct sandbox_struct *mydev;  	
	mydev = container_of(inode->i_cdev, struct sandbox_struct, my_cdev);

	//update the opens
	down_interruptible(&mydev->sem);

  	filp->private_data = mydev;    
  	printk(KERN_ALERT "sandbox_open called %d times\n", ++mydev->opens);

	up(&mydev->sem);

	return 0;
}

static ssize_t sandbox_read(struct file *filp, char __user *buff, size_t count, loff_t *offp)
{
	int res, wb;

	struct sandbox_struct *mydev = filp->private_data;
	down_interruptible(&mydev->sem);
 
	wb = (count < mydev->size - *offp) ? count : mydev->size - *offp;
  
	res = copy_to_user(buff, mydev->buff+*offp, wb);
	if (res == 0)
	    printk(KERN_ALERT "sandbox_read called %d timed\n", ++mydev->reads);

  	*offp += wb - res;
  	res = wb - res;

	up(&mydev->sem);
  
  	return res;
}
/**
* clear ip list in case of switch of networking mode
*/
static void clear_ip_list(struct sandbox_class* class)
{
	struct list_head * tmp, *q;
	struct ip_exception * entry = NULL;

	if(class->ip_list == NULL)
		goto success;

	//deleting the list need to be performed with this macro
	list_for_each_safe(tmp, q, &class->ip_list->list){
		entry= list_entry(tmp, struct ip_exception, list);
		list_del(tmp);
		kfree(entry->ip_address);
		kfree(entry);
		}

	//delete the head of the list
	tmp = &class->ip_list->list;
	entry = class->ip_list;
	list_del(tmp);
	kfree(entry->ip_address);
	kfree(entry);

success:
	class->ip_list = NULL;
	printk("IP exceptions list have been cleared");
}

/**
* clear file list in case of switch of files mode
*/
static void clear_file_list(struct sandbox_class* class)
{
	struct list_head * tmp, *q;
	struct file_exception * entry = NULL;

	if(class->file_list == NULL)
		goto success;
	
	//deleting the list need to be performed with this macro
	list_for_each_safe(tmp, q, &class->file_list->list){
		entry= list_entry(tmp, struct file_exception, list);
		list_del(tmp);
		kfree(entry->filename);
		kfree(entry);
	}
success:
	class->file_list = NULL;
	printk("files exceptions list have been cleared");
}
/**
*function split the input to tokens
*/
static int split_to_tokens(const char *request, char **tokens,int input_len, const int separator)
{
	//TODO: check buffer overflow
	char *found;
	int current_token=0;
	int word_len = 0;
	while(1)
	{
		//end of the line
		if(request[0] == '\0')
			break;
		//eliminate white spaces
		if(request[0] == separator)
		{
			request++;
			input_len--;
			continue;
		}
		found = strchr(request, separator);
		/*if(found == input_len - 1)
			break;*/
		if(found == NULL)
			word_len = input_len - 1;
		else
			word_len = found - request;	

		if(word_len != 0)
		{
			//copy the token
			memcpy(tokens[current_token], request, word_len);
			printk(KERN_ALERT "current token %s", tokens[current_token]);
			current_token+=1;
		}
		if(found == NULL)
			break;		
		request = found+1;
		input_len--;
	}
	return current_token;

//free tokens memory
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
//list rules of the sandbox by its number
static void sandbox_list_rules(int num, struct sandbox_class* class)
{
	int i;
	char yes = 'Y';
	char no = 'N';
	char chosen;
	bool bitval;
	int offset = 0;
	struct list_head * tmp;
	struct ip_exception * entry = NULL;
	struct file_exception * entry1 = NULL;

	//allocate space to answer
	mydev->answer = kzalloc(8192 * sizeof(char), GFP_KERNEL);

	//sprintf is allowed in the kernel
	offset += sprintf(mydev->answer,"Rules for sandbox number %d:\n",num);

	if(NULL == class->fs_root)
		offset += sprintf(mydev->answer+offset,"The fs root is /\n");
	else
		offset += sprintf(mydev->answer+offset,"The fs root is %s\n",class->fs_root);

	if(class->strip_files)
		chosen = yes;
	else
		chosen = no;
	offset += sprintf(mydev->answer+offset,"Stripping files - %c\n",chosen);
	
	if(class->disallow_files_by_default)
		chosen = yes;
	else
		chosen = no;
	offset += sprintf(mydev->answer+offset,"Disallow file access - %c\n",chosen);
	offset += sprintf(mydev->answer+offset,"Exceptions:\n");

	if(NULL == class->file_list)
		goto ips;
  
  entry1 = class->file_list;
	offset += sprintf(mydev->answer+offset,"%s\n",entry1->filename);
  
  list_for_each(tmp, &class->file_list->list) {
    entry1 = list_entry(tmp, struct file_exception, list);
		offset += sprintf(mydev->answer+offset,"%s\n",entry1->filename);
    }

ips:
	//tmp
	if(class->disallow_ips_by_default)
		chosen = yes;
	else
		chosen = no;

	offset += sprintf(mydev->answer+offset,"Disallow networking access - %c\n",chosen);
	offset += sprintf(mydev->answer+offset,"Exceptions:\n");
	
	if(NULL == class->ip_list)
		goto syscalls;
  
  entry = class->ip_list;
	offset += sprintf(mydev->answer+offset,"%s\n",entry->ip_address);
  
  list_for_each(tmp, &class->ip_list->list) {
    entry = list_entry(tmp, struct ip_exception, list);
		offset += sprintf(mydev->answer+offset,"%s\n",entry->ip_address);
    }

syscalls:
	offset += sprintf(mydev->answer+offset,"System calls:\n");
	for(i = 0 ; i < NUM_OF_SYSCALLS ; i++)
	{
		//TODO: get syscalls names...
		offset += sprintf(mydev->answer+offset,"sys call %d ",i);
		bitval = test_bit(i, class->syscalls);
		if(bitval)
			offset += sprintf(mydev->answer+offset,"disallow \n");
		else
			offset += sprintf(mydev->answer+offset,"allow \n");
     
	}

	mydev->buff = mydev->answer;
	mydev->size = offset;
}
static void request_processing(struct file *filp)
{
	//TODO: change printk to write in the buffer
	int sandbox_num;
	int num_tokens;
	int i;
	char** tokens;
  struct sandbox_struct *mydev = filp->private_data;
	struct sandbox_class * class = NULL;
	//TODO: check buffer overflow
	int length = strlen(mydev->buff);
	
	//TODO: check	
	mydev->buff[length-1] = 0;
 
	tokens = kzalloc(length * sizeof(char*), GFP_KERNEL);
	for(i = 0 ; i < length ; i++)
	{
		tokens[i] = kzalloc(length * sizeof(char), GFP_KERNEL);
	}


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
			sandbox_set_strip_files(class,true);
			goto success;
		}
		if(num_tokens == 6 && !strcmp(tokens[2],"set") && !strcmp(tokens[3],"file") && !strcmp(tokens[4],"mode"))
		{
			//in this case we clear the file list
			clear_file_list(class);
			if(!strcmp(tokens[5],"allow"))
			{
				class->disallow_files_by_default = false;
				goto success;
			}
			if(!strcmp(tokens[5],"disallow"))
			{
				class->disallow_files_by_default = true;
				goto success;
			}
			goto failure;
		}
		if(num_tokens == 6 && !strcmp(tokens[2],"set") && !strcmp(tokens[3],"networking") && !strcmp(tokens[4],"mode"))
		{
			//in this case we clear the exception list
			clear_ip_list(class);
			
			if(!strcmp(tokens[5],"allow"))
			{
				class->disallow_ips_by_default = false;
				goto success;
			}
			if(!strcmp(tokens[5],"disallow"))
			{
				class->disallow_ips_by_default = true;
				goto success;
			}
			goto failure;
		}
		if(num_tokens == 5 && !strcmp(tokens[3],"file"))
		{
			struct file_exception * file_exp;
			char* filename;

			if(strcmp(tokens[2],"allow") && strcmp(tokens[2],"disallow"))
				goto failure;

			file_exp = kzalloc(sizeof(struct file_exception), GFP_KERNEL);
			filename = (char*)kzalloc(FILE_MAX, GFP_KERNEL);
			strncpy(filename,tokens[4], FILE_MAX-1);

			file_exp->filename = filename;
			file_exp->len = strnlen(filename,FILE_MAX);

			if((!strcmp(tokens[2],"allow") && !class->disallow_files_by_default) ||
					(!strcmp(tokens[2],"disallow") && class->disallow_files_by_default))
			{
				if(find_file(class, filename))
				{
					struct list_head * tmp, *q;
					struct file_exception * entry = NULL;
					list_for_each_safe(tmp, q, &class->file_list->list)
					{
						entry= list_entry(tmp, struct file_exception, list);
						if(!strncmp(filename, entry->filename,FILE_MAX))
						{
							list_del(tmp);
							kfree(entry->filename);
							kfree(entry);
						}
					}
					tmp = &class->file_list->list;
					entry = class->file_list;

					if(!strncmp(filename, entry->filename,FILE_MAX))
					{
						if(tmp == tmp->next)
							class->file_list = NULL;
						else 
							class->file_list = list_entry(tmp->next, struct file_exception, list);
						list_del(tmp);
						kfree(entry->filename);
						kfree(entry);
					}
					kfree(filename);
					kfree(file_exp);
					goto success;
				}
				kfree(filename);
				kfree(file_exp);
				goto failure;
			}

			//check if the file already contained in the ip list
			if(find_file(class, filename))
			{
				printk(KERN_ALERT "The file name %s already in the exceptions list",filename);
				kfree(filename);
				kfree(file_exp);
				goto failure;
			}

			if(class->file_list == NULL)
				INIT_LIST_HEAD(&file_exp->list);
			else
				list_add(&file_exp->list, &class->file_list->list);

			class->file_list = file_exp;
			
			goto success;
		}
		if(num_tokens == 5 && !strcmp(tokens[3],"ip"))
		{
			struct ip_exception * ip_exp;
			char* ip_addr;

			if(strcmp(tokens[2],"allow") && strcmp(tokens[2],"disallow"))
				goto failure;

			ip_exp = kzalloc(sizeof(struct ip_exception), GFP_KERNEL);
			ip_addr = (char*)kzalloc(IP_MAX, GFP_KERNEL);
			strncpy(ip_addr,tokens[4], IP_MAX-1);
			ip_exp->ip_address = ip_addr;

			if((!strcmp(tokens[2],"allow") && !class->disallow_ips_by_default) ||
					(!strcmp(tokens[2],"disallow") && class->disallow_ips_by_default))
			{
				if(find_ip(class, ip_addr))
				{
					struct list_head * tmp, *q;
					struct ip_exception * entry = NULL;
					list_for_each_safe(tmp, q, &class->ip_list->list)
					{
						entry= list_entry(tmp, struct ip_exception, list);
						if(!strncmp(ip_addr, entry->ip_address,IP_MAX))
						{
							list_del(tmp);
							kfree(entry->ip_address);
							kfree(entry);
						}
					}
					tmp = &class->ip_list->list;
					entry = class->ip_list;

					if(!strncmp(ip_addr, entry->ip_address,IP_MAX))
					{
						if(tmp == tmp->next)
							class->ip_list = NULL;
						else 
							class->ip_list = list_entry(tmp->next, struct ip_exception, list);
						list_del(tmp);
						kfree(entry->ip_address);
						kfree(entry);
					}
					kfree(ip_addr);
					kfree(ip_exp);
					goto success;
				}
				kfree(ip_addr);
				kfree(ip_exp);
				goto failure;
			}

			//check if the ip already contained in the ip list
			if(find_ip(class, ip_addr))
			{
				printk(KERN_ALERT "The ip address %s already in the exceptions list",ip_addr);
				kfree(ip_addr);
				kfree(ip_exp);
				goto failure;
			}

			if(class->ip_list == NULL)
				INIT_LIST_HEAD(&ip_exp->list);
			else
				list_add(&ip_exp->list, &class->ip_list->list);

			class->ip_list = ip_exp;
			
			goto success;
		}
		if(num_tokens == 5 && !strcmp(tokens[3],"syscall"))
		{
			int syscall_num = simple_strtol(tokens[4], NULL, 10);
			if(syscall_num < NUM_OF_SYSCALLS && syscall_num >= 0)
			{
				bool disallow;
				if(strcmp(tokens[2],"allow") && strcmp(tokens[2],"disallow"))
					goto failure;
				if(!strcmp(tokens[2],"allow"))
				{
					disallow = false;
				}
				if(!strcmp(tokens[2],"disallow"))
				{
					disallow = true;
				}
				sandbox_set_syscall_bit(class,syscall_num,disallow);
				goto success;
			}
			goto failure;
		}
		if(num_tokens == 4 && !strcmp(tokens[2],"list") && !strcmp(tokens[3],"rules"))
		{
			kfree(mydev->buff);
			sandbox_list_rules(sandbox_num,class);
			goto success;
		}
		goto failure;
		
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

	down_interruptible(&mydev->sem);
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

	up(&mydev->sem);

  if (!res) // success
    printk(KERN_ALERT "sandbox_write called %d times. buff: %s\n", ++mydev->writes, mydev->buff);

  return count - res;
}
  
static int sandbox_release(struct inode *inode, struct file *filp)
{
  struct sandbox_struct *mydev = filp->private_data;

	down_interruptible(&mydev->sem);
  printk(KERN_ALERT "sandbox_release called %d times (buffsize: %d)\n", ++mydev->releases, mydev->buffsize);
	up(&mydev->sem);
  
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

	sema_init(&mydev->sem, 1);

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


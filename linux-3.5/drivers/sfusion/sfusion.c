#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/netdevice.h>
 
#define DEVICE_NAME "sfusion"
#define BUFFER_SIZE 1024
#define WORD_separatOR ' '
#define NEWLINE_separatOR '\n'
#define EOL_separatOR '\0'
#define VALUE_separatOR ','
 
MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Rony Fragin & Shai Fogel");
MODULE_DESCRIPTION("Saiyan Fusion Helper.");
MODULE_SUPPORTED_DEVICE(DEVICE_NAME);
 
#pragma region Declerations
typedef struct device_list device_list;
typedef struct rule_list rule_list;
int device_init(void);
void device_exit(void);
static int device_open(struct inode *, struct file *);
static int device_release(struct inode *, struct file *);
static ssize_t get_next_word(const char *input, char *output, int *input_length, int *input_offset, const int separator);
static bool strmatch(const char* str1, const char* str2);
static ssize_t device_read(struct file *, char *, size_t, loff_t *);
static ssize_t device_write(struct file *, const char *, size_t, loff_t *);
#pragma endregion Declerations

module_init(device_init);
module_exit(device_exit);

#pragma region Members
/*
 * A struct that holds a device name
 * and a link to the list header.
 */
struct device_list {
	struct net_device *dev;
	struct list_head list;
};
/*
 * A struct that holds a rule
 * a link to the list header.
 *
 * A rule consists of:
 * - An array of ports that are part of the saiyan fusion.
 * - The type of the ports. Can be "src" or "dest".
 * - The subnet that will be included. e.g. "192.168.0.1/16".
 * - The subnet's type. Can be "src" or "dest".
 * - The protocol. Can be "tcp" or "udp".
 */
struct rule_list {
	int id;
	int *ports;
	char *port_type;
	char *subnet;
	char *subnet_type;
	char *protocol;
	struct list_head list;
};
/*
 * Struct that maps the char device's file operations
 * to their implementation in the code.
 */
static struct file_operations fops = {
	.read = device_read,
	.write = device_write,
	.open = device_open,
	.release = device_release
};
/* Will be initialized with device's major */
static int device_major = -1;
/* Holds the amount of devices that were opened */
static int device_opened = 0;
/* Holds a list of devices that were set by the user */
static device_list mydevs;
/* Holds a list of rules that were set by the user */
static rule_list myrules;
/* Counter for the rules */
static int num_rules = 0;
#pragma endregion Members
 
/**
 * device_init		-	Initialize the char device.
 */ 
int device_init(void) {
	// Set a new available device major.
	device_major = register_chrdev(0, DEVICE_NAME, &fops);
	// Device major wasn't set.
	if(device_major < 0) {
		printk(KERN_ALERT "sfusion: cannot obtain major number %d.\n", device_major);
		return device_major;
	}
	printk(KERN_INFO "sfusion: sfusion loaded with major %d.\n", device_major);
	return 0;
}
/**
 * device_exit		-	Destroy the char device.
 */
void device_exit(void) {
	// Release the device.
	unregister_chrdev(device_major, DEVICE_NAME);
	printk(KERN_INFO "sufsion: sfusion unloaded.\n");
}
/**
 * device_open		-	Open the device.
 * @nd: Information about the file.
 * @fp: The file.
 * Returns: Action's state. 0 - success.
 */
static int device_open(struct inode *nd, struct file *fp) {
	// Device was already opened.
	// Return that it's busy.
	if(device_opened) return -EBUSY;
	// Increase number of devices that were opened.
	device_opened++;
	// Acquire module.
	try_module_get(THIS_MODULE);
	// Success.
	return 0;
}
/**
 * device_release		-	Release the device.
 * @nd: Information about the file.
 * @fp: The file.
 * Returns: Action's state. 0 - success.
 */
static int device_release(struct inode *nd, struct file *fp) {
	// Decrease number of devices that were opened.
	if(device_opened) device_opened--;
	// Release module.
	module_put(THIS_MODULE);
	// Success.
	return 0;
}
/**
 * device_read		-	Read from the device.
 * @fp: The file.
 * @buff: Buffer that will hold all the response.
 * 		  The response consists of 2 parts:
 *		  - All the devices that are part of the "saiyan fusion".
 *		  - All the rules that were added by the user and are active.
 * Returns: Amount of bytes that were written in the buffer.
 */
static ssize_t device_read(struct file *fp, char *buff, size_t length, loff_t *offset) {
	// TODO: Add message.
	char *msg = "reading from the sfusion device";
	// Write to buffer.
	copy_to_user(buff, msg, strlen(msg));
	// Send amount of bytes that were written.
	return strlen(msg);
}
/**
 * device_write		-	Write to the device.
 * @fp: The file.
 * @buff: Buffer that holds the request passed from the user.
 *		  The request can be one of the following:
 *		  - set_devices eth0,eth1,...
 *		  - add_rule -ports 80,443,445 -port_type src -subnet 192.168.0.0/16
 *			-subnet_type dest -protocol tcp
 *		  - remove_rule 1
 * @length: The amount of bytes that were written to the buffer.
 * @offset: From where should the buffer be read.
 * Returns: Amount of bytes that were read from the buffer.
 */
static ssize_t device_write(struct file *fp, const char *buff, size_t length, loff_t *offset) {
	char *word;
	char *val;
	int word_length = 0;
	int buff_offset = 0;
	int buff_length = length;
	int val_length = 0;
	int word_offset = 0;
	struct net *initnet = &init_net;
	struct net_device *curr_dev;
	struct device_list *tmp_device_list;
	struct rule_list *tmp_rule_list;
	// Allocate space for the words.
	word = kmalloc(length * sizeof(char), GFP_KERNEL);
	// Get the first word.
	word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, WORD_separatOR);
	// There is only one word.
	if(word_length == buff_length)
		goto reading_failed;
	// Check if word equals set_devices.
	if(strmatch(word, "set_devices"))
	{
		// Move to the next word.
		word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, NEWLINE_separatOR);
		// This is the last word.
		if(buff_length != 0)
			goto reading_failed;
		INIT_LIST_HEAD(&mydevs.list);
		// Alocate space for all the values in the word.
		val =  kmalloc(word_length * sizeof(char), GFP_KERNEL);
		// Run until the last value.
		do
		{
			// Move to the first/next value.
			val_length = get_next_word(word + word_offset, val, &word_length, &word_offset, VALUE_separatOR);
			curr_dev = dev_get_by_name(initnet, val);
			tmp_device_list = kmalloc(sizeof(struct device_list), GFP_KERNEL);
			tmp_device_list->dev = curr_dev;
			list_add(&(tmp_device_list->list), &(mydevs.list));
		} while(word_length > 0);
		// Free alocated space for the values.
		kfree(val);
	}
	else if(strmatch(word, "add_rule"))
	{
		INIT_LIST_HEAD(&myrules.list);
		tmp_rule_list = kmalloc(sizeof(struct rule_list), GFP_KERNEL);
		// Run until the last value.
		do
		{
			// Move to the next word.
			word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, WORD_separatOR);
			
			// Check if word equals -ports.
			if(strmatch(word, "-ports"))
			{
				printk(KERN_INFO "sfusion: text1 %s.\n", word);
			}
			// Check if word equals -port_type.
			else if(strmatch(word, "-port_type"))
			{
				// Move to the next word.
				word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, WORD_separatOR);
				if(strmatch(word, "src"))
				{
					tmp_rule_list->port_type = "src";
					printk(KERN_INFO "sfusion: text21 %s.\n", word);
				}
				else if(strmatch(word, "dest"))
				{
					tmp_rule_list->port_type = "dest";
					printk(KERN_INFO "sfusion: text22 %s.\n", word);
				}
				else
					goto reading_failed;
			}
			// Check if word equals -subnet.
			else if(strmatch(word, "-subnet"))
			{
				printk(KERN_INFO "sfusion: text3 %s.\n", word);
			}

			// Check if word equals -subnet_type.
			else if(strmatch(word, "-subnet_type"))
			{
				// Move to the next word.
				word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, WORD_separatOR);
				if(strmatch(word, "src"))
				{
					printk(KERN_INFO "sfusion: text41 %s.\n", word);
					tmp_rule_list->subnet_type = "src";
				}
				else if(strmatch(word, "dest"))
				{
					printk(KERN_INFO "sfusion: text42 %s.\n", word);
					tmp_rule_list->subnet_type = "dest";
				}
				else
					goto reading_failed;
			}
			// Check if word equals -protocol.
			else if(strmatch(word, "-protocol"))
			{
				printk(KERN_INFO "sfusion: text5 %s.\n", word);
			}
		} while(buff_length > 0);
	}
	else if(strmatch(word, "remove_rule"))
	{

	}
	else
		goto reading_failed;
	goto success;
	reading_failed:
	printk(KERN_INFO "sfusion: Illegal string was entered.\n");
	success:
	// Free alocated space for the words.
	kfree(word);
	struct list_head *pos;
	list_for_each(pos, &mydevs.list) {
		tmp_device_list = list_entry(pos, struct device_list, list);
		printk(KERN_INFO "sfusion: dev %s", tmp_device_list->dev->name);
	}
	return length;
}
/**
 * get_next_word		-	Gets the next word in the buffer
 *							according to the given separator.
 * @input: The buffer that will be traversed.
 * @output: The word that was found.
 * @input_length: The buffer's valid length.
 * @input_offset: The location from which we will start the search.
 * @separator: The char that separates the words.
 * Returns: The size of the word that was found.
 */
static ssize_t get_next_word(const char *input, char *output, int *input_length, int *input_offset, const int separator)
{
	// Find the first occurrence of the separator.
	char *found = strnchr(input, (*input_length), separator);
	// Will hold the word's size.
	int word_size = 0;
	// Nothing was found - probably the last word.
	if(found == NULL)
	{
		// Set the word size to be the whole input's length.
		word_size = (*input_length) - 1;
	}
	else
	{
		// Get word's length.
		word_size = found - input;
	}
	
	//printk(KERN_DEBUG "sfusion: input %s.\n", input);
	//printk(KERN_DEBUG "sfusion: offset %d input_length %d separator %c.\n", word_size, length, separator);
	//printk(KERN_DEBUG "sfusion: found %s.\n", found);
	
	// Copy the word to the output.
	memcpy(output, input, word_size);
	// Add null terminator to the output string.
	output[word_size] = '\0';
	// Shorten input length, excluding the
	// word that we just found.
	(*input_length) -= word_size + 1;
	// Move input's location to start from
	// the end of the word that we found.
	(*input_offset) += word_size + 1;
	// Return the word's size.
	return word_size + 1;
}
/**
 * strmatch				-	Returns true if strings match and
 *							false otherwise.
 * @str1: The first string.
 * @str2: The second string.
 * Returns: True if match and False otherwise.
 */
static bool strmatch(const char* str1, const char* str2)
{
	return (strcmp(str1, str2) == 0);
}

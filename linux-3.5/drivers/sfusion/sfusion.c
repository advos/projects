#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <asm/uaccess.h>
#include <linux/cdev.h>
#include <linux/netdevice.h>
#include "sfusion.h"

module_init(device_init);
module_exit(device_exit);

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
static int for_eof = 0;
 
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
 * @length: Size of the buffer.
 * @offset: Where to start writing.
 * Returns: Amount of bytes that were written in the buffer.
 */
static ssize_t device_read(struct file *fp, char *buff, size_t length, loff_t *offset) {
	struct list_head *pos;
	struct device_list *tmp_device_list;
	struct rule_list *tmp_rule_list;
	int i, j, k;
	char msg[BUFFER_SIZE];
	
	// Change to EOF and don't run forever.
	for_eof = (for_eof + 1) % 2;	
	// Check if devices list isn't empty.
	if(list_empty(&mydevs.list) == 0)
	{
		j = 0;
		// Add text to the response.
		strcat(msg, "set_devices ");
		// Run on the list.
		list_for_each(pos, &mydevs.list) {
			j++;
			// Get node.
			tmp_device_list = list_entry(pos, struct device_list, list);
			// Add device to the response.
			sprintf(msg, "%s%s", msg, tmp_device_list->dev->name);
		}
		strcat(msg, "\n");
	}
	pos = NULL;
	// Check if rules list isn't empty.
	if(!list_empty(&myrules.list))
	{
		k = 0;
		// Add text to the response.
		strcat(msg, "add_rule ");
		// Run on the list.
		list_for_each(pos, &myrules.list) {
			k++;
			// Get node.
			tmp_rule_list = list_entry(pos, struct rule_list, list);
			// Add text to the response.
			strcat(msg, "-ports ");
			// Run on the ports.
			// Array size is at first element.
			for(i = 1; i <= (tmp_rule_list->ports)[0]; i++)
			{
				// Add port to the response.
				sprintf(msg, "%s%d ", msg, (tmp_rule_list->ports)[i]);
			
			}
			// Add the rest of the elements to the response.
			sprintf(msg, "%s -ports_type %s ", msg, tmp_rule_list->port_type);
			sprintf(msg, "%s -subnet_type %s ", msg, tmp_rule_list->subnet_type);
			sprintf(msg, "%s -protocol %s\n", msg, tmp_rule_list->protocol);
		}
	}
	printk(KERN_DEBUG "sfusion: devs %d , rules %d\n", j, k);
	// If it was requested twice, return 0.
	if(for_eof == 0)
		return 0;
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
	int num_ports = 0;
	int count = 0;
	long rule_to_del = 0;
	int *ports;
	struct net *initnet = &init_net;
	struct net_device *curr_dev;
	struct device_list *tmp_device_list;
	struct rule_list *tmp_rule_list;
	struct list_head *pos;
	// Initialize a list that will hold the devices.
	INIT_LIST_HEAD(&mydevs.list);
	// Initialize a list that will hold the rules.
	INIT_LIST_HEAD(&myrules.list);
	// Allocate space for the words.
	word = kmalloc(length * sizeof(char), GFP_KERNEL);
	// Get the first word.
	word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, WORD_SEPARATOR);
	// There is only one word.
	if(word_length == buff_length)
		goto too_short_failed;
	// Check if word equals to set_devices.
	if(strmatch(word, "set_devices"))
	{
		// Move to the next word.
		word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, NEWLINE_SEPARATOR);
		// This isn't the last word.
		if(buff_length != 0)
			goto unknown_word_failed;
		// Allocate space for all the values in the word.
		val =  kmalloc(word_length * sizeof(char), GFP_KERNEL);
		// Run until the last value.
		do
		{
			// Move to the first/next value.
			val_length = get_next_word(word + word_offset, val, &word_length, &word_offset, VALUE_SEPARATOR);
			// Get the device of the specified name.
			curr_dev = dev_get_by_name(initnet, val);
			if(curr_dev == NULL)
				goto dev_null_failed;
			// Allocate a node in the list.
			tmp_device_list = kmalloc(sizeof(struct device_list), GFP_KERNEL);
			// Add device to the node.
			tmp_device_list->dev = curr_dev;
			// Add node to the list.
			list_add(&(tmp_device_list->list), &(mydevs.list));
		} while(word_length > 0);
		// Free allocated space for the values.
		kfree(val);
	}
	// Check if word equals to add_rule.
	else if(strmatch(word, "add_rule"))
	{
		// Allocate a node in the list.
		tmp_rule_list = kmalloc(sizeof(struct rule_list), GFP_KERNEL);
		// Run until the last value.
		do
		{
			// Move to the next word.
			word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, WORD_SEPARATOR);
			// Check if word equals to -ports.
			if(strmatch(word, "-ports"))
			{
				printk(KERN_INFO "sfusion: text1 %s.\n", word);
				// Move to the next word.
				word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, WORD_SEPARATOR);
				// Count the number of ports.
				num_ports = strcount(word, word_length, VALUE_SEPARATOR);
				// Initialize port array.
				ports = kmalloc((num_ports + 1) * sizeof(int), GFP_KERNEL);
				// Save size in the first cell.
				ports[0] = num_ports;
				// Allocate space for all the values in the word.
				val =  kmalloc(word_length * sizeof(char), GFP_KERNEL);
				count = 1;
				// Run until the last value.
				do
				{
					// Move to the first/next value.
					val_length = get_next_word(word + word_offset, val, &word_length, &word_offset, VALUE_SEPARATOR);
					// Convert string to int
					// and save in the array.
					if(kstrtol(val, 10, &(ports[count])) != 0)
						goto port_not_int_failed;
					count++;
				} while(word_length > 0);
				printk(KERN_INFO "portsss: %d\n", ports[0]);
				// Save port array.
				tmp_rule_list->ports = ports;
				// Free allocated space for the values.
				kfree(val);
			}
			// Check if word equals to -port_type.
			else if(strmatch(word, "-port_type"))
			{
				// Move to the next word.
				word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, WORD_SEPARATOR);
				// Check if word equals to src or dest.
				if(strmatch(word, "src") || strmatch(word, "dest"))
				{
					// Set port type.
					tmp_rule_list->port_type = strclone(word);
					printk(KERN_INFO "sfusion: text2 %s.\n", word);
				}
				// Unknown word.
				else
					goto unknown_word_failed;
			}
			// Check if word equals to -subnet.
			else if(strmatch(word, "-subnet"))
			{
				printk(KERN_INFO "sfusion: text3 %s.\n", word);
			}
			// Check if word equals to -subnet_type.
			else if(strmatch(word, "-subnet_type"))
			{
				// Move to the next word.
				word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, WORD_SEPARATOR);
				// Check if word equals to src or dest.
				if(strmatch(word, "src") || strmatch(word, "dest"))
				{
					// Set subnet type.
					tmp_rule_list->subnet_type = strclone(word);
					printk(KERN_INFO "sfusion: text4 %s.\n", word);
				}
				// Unknown word.
				else
					goto unknown_word_failed;
			}
			// Check if word equals to -protocol.
			else if(strmatch(word, "-protocol"))
			{
				// Move to the next word.
				word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, WORD_SEPARATOR);
				// Check if word equals to tcp or udp.
				if(strmatch(word, "tcp") || strmatch(word, "udp"))
				{
					// Set subnet type.
					tmp_rule_list->protocol = strclone(word);
					printk(KERN_INFO "sfusion: text5 %s.\n", word);
				}
				// Unknown word.
				else
					goto unknown_word_failed;
			}
		} while(buff_length > 0);
		// Check that all elements were filled.
		if(tmp_rule_list->ports != NULL
		   && tmp_rule_list->port_type != NULL
		   && tmp_rule_list->subnet != NULL
		   && tmp_rule_list->subnet_type != NULL
		   && tmp_rule_list->protocol != NULL)
		{
			// Insert id.
			tmp_rule_list->id = num_rules;
			num_rules++;
			// Add node to the list.
			list_add(&(tmp_rule_list->list), &(myrules.list));			
		}
	}
	// Check if word equals to remove_rule.
	else if(strmatch(word, "remove_rule"))
	{
		// Move to the next word.
		word_length = get_next_word(buff + buff_offset, word, &buff_length, &buff_offset, NEWLINE_SEPARATOR);
		// This isn't the last word.
		if(buff_length != 0)
			goto unknown_word_failed;
		// Convert string to int.
		if(kstrtol(word, 10, &rule_to_del) != 0)
			goto rule_not_int_failed;
		// Go over the rules.
		list_for_each(pos, &myrules.list) {
			tmp_rule_list = list_entry(pos, struct rule_list, list);
			// Found rule.
			if(tmp_rule_list->id == rule_to_del)
				// Delete it.
				list_del(&tmp_rule_list->list);
		}
	}
	else
		goto unknown_word_failed;
	goto success;
	port_not_int_failed:
	printk(KERN_ALERT "sfusion: One of the ports wasn't an integer.\n");
	// Free allocated space for the ports.
	kfree(ports);
	// Free allocated space for the values.
	kfree(val);
	// Free allocated space for the words.
	kfree(word);
	return -1;
	rule_not_int_failed:
	printk(KERN_ALERT "sfusion: The rule isn't an integer.\n");
	// Free allocated space for the words.
	kfree(word);
	return -1;
	too_short_failed:
	printk(KERN_ALERT "sfusion: Some parameters are missing in the input.\n");
	// Free allocated space for the words.
	kfree(word);
	return -1;
	unknown_word_failed:
	printk(KERN_ALERT "sfusion: Unknown parameter was entered..\n");
	// Free allocated space for the words.
	kfree(word);
	return -1;
	dev_null_failed:
	printk(KERN_INFO "sfusion: Unknown device was entered.\n");
	// Free allocated space for the values.
	kfree(val);
	// Free allocated space for the words.
	kfree(word);
	return -1;
	success:
	// Free allocated space for the words.
	kfree(word);
	return length;
}

/**
 * strcount				-	Find the number of occurrences of
 *						a certain char.
 * @str: The input string.
 * @length: The string's length.
 * @seperator: The char that we want to find.
 * Returns: Number of occurrences.
 */
static ssize_t strcount(const char *str,const ssize_t length, const int separator)
{
	int i = 1;
	int str_len = length;
	// Find the first occurrence of the separator.
	char *found = strnchr(str, str_len, separator);
	//printk(KERN_INFO "sfusion: found %s len: %d", found, str_len);
	char *new_pos = found;
	char *old_pos = str;
	// Find all the rest.
	while (new_pos != NULL) {
		// Shorten string according to our current location.
		str_len -= (new_pos - old_pos);
		i++;
		old_pos = new_pos;
		new_pos = strnchr(old_pos + 1, str_len, separator);
	}
	// Return the number of occurrences.
	return i;
}
/**
 * strmatch				-	Returns true if strings match and
 *						false otherwise.
 * @str1: The first string.
 * @str2: The second string.
 * Returns: True if match and False otherwise.
 */
static bool strmatch(const char* str1, const char* str2)
{
	return (strcmp(str1, str2) == 0);
}
/**
 * strclone				-	Clones the string.
 * @str1: The input string.
 * Returns: The cloned string.
 */
static char *strclone(const char *str)
{
	// Don't forget the /0.
	int len = strlen(str) + 1;
	char *res = kmalloc(len * sizeof(char), GFP_KERNEL);
	memcpy(res, str, len);
	return res;
}
/**
 * get_next_word		-	Gets the next word in the buffer
 *					according to the given separator.
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

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Rony Fragin & Shai Fogel");
MODULE_DESCRIPTION("Saiyan Fusion Helper.");
MODULE_SUPPORTED_DEVICE(DEVICE_NAME);

#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/namei.h>
#include <linux/sched.h> 

#include "utils.h"

#define UMODE_NO_PERM_MASK 0xfe00 


void string_truncate(char* str, int size)
{
	int i = 0;
	for (i = 0; (i < size) && (str[i] != '\0'); ++i);

	if (i == size)
		str[size - 1] = '\0';
}

void string_replace(char* str, int size, char replace, char by)
{
	int i = 0;

	for (i = 0; (i < size) && (str[i] != '\0'); ++i)
		if (str[i] == replace)
      			str[i] = by;
}

//This assumes nothing about the identity of the user. 
//Used to change permissions from unknown context.
int my_chown(char* pathname, kuid_t uid, kgid_t gid)
{
	struct path result_path = {0};
	struct iattr attributes = {0};
	int err = 0;

	printk(KERN_ALERT "my_chown: starting for %s.\nSetting permissions for uid=%d, gid=%d\n", pathname, uid, gid);
	err = kern_path(pathname, LOOKUP_FOLLOW, &result_path);
	if (err < 0)
	{
		printk(KERN_ALERT "my_chmod: user_path_at failed with error=%d\n", err);
		return err;
	}

	mutex_lock(&result_path.dentry->d_inode->i_mutex);

	attributes.ia_valid = ATTR_UID | ATTR_GID;
	attributes.ia_uid = uid;
	attributes.ia_gid = gid;

	//Ugly trick to change the uid/gid directly
	result_path.dentry->d_inode->i_uid = uid;
	result_path.dentry->d_inode->i_gid = gid;

	//We do notify_change just it case. Won't hurt us :P
	err = notify_change(result_path.dentry, &attributes);

	mutex_unlock(&result_path.dentry->d_inode->i_mutex);

	return err;
}

//This assumes current user has permissions to do chmod
int my_chmod(char* pathname, unsigned short mode)
{
	struct path result_path = {0};
	struct iattr attributes = {0};
	int err = 0;
	printk(KERN_ALERT "my_chmod: starting for %s\n", pathname);
	err = kern_path(pathname, LOOKUP_FOLLOW, &result_path);
	if (err < 0)
	{
		printk(KERN_ALERT "my_chmod: user_path_at failed with error=%d\n", err);
		return err;
	}

	mutex_lock(&result_path.dentry->d_inode->i_mutex);

	attributes.ia_valid = ATTR_MODE;
	attributes.ia_mode = (result_path.dentry->d_inode->i_mode & UMODE_NO_PERM_MASK) + mode;
	printk(KERN_ALERT "my_chmod: got ia_mode=%x\n", attributes.ia_mode);
 
	err = notify_change(result_path.dentry, &attributes);
 
	mutex_unlock(&result_path.dentry->d_inode->i_mutex);

	return err;
}  

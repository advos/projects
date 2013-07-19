#ifndef __UTILS_H__
#define __UTILS_H__

#include <linux/types.h>
#include <linux/uidgid.h>

void string_truncate(char* str, int size);
void string_replace(char* str, int size, char replace, char by);
int my_chmod(char* pathname, umode_t mode, int elevate_mode_flag);
int my_chown(char* pathname, kuid_t uid, kgid_t gid);

#endif

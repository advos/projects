#ifndef __SFUSION_H__
#define __SFUSION_H__

#define DEVICE_NAME "sfusion"
#define BUFFER_SIZE 4096
 
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

static ssize_t get_next_word(const char *input, char *output, int *input_length, int *input_offset, const int separator);
static char *strclone(const char *str);
static bool strmatch(const char* str1, const char* str2);
static ssize_t strcount(const char *str,const ssize_t length, const int separator);


#endif

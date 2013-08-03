#ifndef __GOLDENCHAR_H__
#define __GOLDENCHAR_H__

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/device.h>
#include <linux/cdev.h>

#include "common.h"
#include "goldenblock.h"

#define GOLDENCHAR_STR "my_goldenchar"
#define DEVFS_GOLDENCHAR_PATH "/dev/" GOLDENCHAR_STR
#define GOLDEN_DEV_CHAR_CLASS "GoldenGateClass2"

typedef struct GoldenGate GoldenGate;

typedef struct GoldenChar
{
dev_t dev_no;
struct class* dev_class;
struct cdev* dev_cdev;
struct device* device_st;
} GoldenChar;

int setup_goldenchar_device(GoldenGate* golden, GoldenChar* gchar);
void free_goldenchar_device(GoldenChar* gchar);

#endif

#ifndef __LIBGOLDEN_H__
#define __LIBGOLDEN_H__

#include "common.h"

typedef int (*golden_open_function)(GoldenRequest* request);
typedef int (*golden_close_function)(GoldenRequest* request);
typedef int (*golden_read_function)(GoldenRequest* request);
typedef int (*golden_write_function)(GoldenRequest* request);

typedef struct golden_operations
{
	golden_open_function open;
	golden_close_function close;
	golden_read_function read;
	golden_write_function write;	
} golden_operations;

int libgolden_init();

int libgolden_register_device(char* name, int minors, int capacity, golden_operations* ops);


#endif

#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>

int main(int argc, char* argv[])
{
	char* endptr = NULL;
	int fd; 
	unsigned long locality_size;
	
	if(argc < 2)
	{
		printf("Error: no parameter was given!\n");
		return -1;
	}
	
	
	locality_size = strtoul(argv[1], &endptr, 10);
	if((*endptr))
	{
		printf("Error: conversion error\n");
		return 0;
	}
	fd = open("/dev/phase_shifts_char_device", O_RDONLY);
	if(fd < 0)
	{
		perror("/dev/phase_shifts_char_device");
		return 0;
	}
	
	if(ioctl(fd, 0, &locality_size) == -1)
	{
		perror("ioctl");
	}
	else
	{
		printf("success\n");
	}
	close(fd);
	
	return 0;
}

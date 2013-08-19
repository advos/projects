# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
FILE_TO_COMPILE = sandbox_algorithm
ifneq ($(KERNELRELEASE),)

	obj-m := $(FILE_TO_COMPILE).o

# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
	KERNELDIR ?= ../linux-3.5
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

endif

clean:
	rm -rf *~ $(FILE_TO_COMPILE).ko $(FILE_TO_COMPILE).mod.o .$(FILE_TO_COMPILE).o.cmd Module.symvers .$(FILE_TO_COMPILE).ko.cmd .$(FILE_TO_COMPILE).mod.o.cmd $(FILE_TO_COMPILE).mod.c $(FILE_TO_COMPILE).o modules.order .tmp_versions
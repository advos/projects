# If KERNELRELEASE is defined, we've been invoked from the
# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
obj-m := CFLock.o
# Otherwise we were called directly from the command
# line; invoke the kernel build system.
else
KERNELDIR := ~/linux-3.5/
PWD := $(shell pwd)
default:
  $(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

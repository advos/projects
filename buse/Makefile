# If KERNELRELEASE is defined, we've been invoked from the

# kernel build system and can use its language.
ifneq ($(KERNELRELEASE),)
	obj-m := goldengate.o
	goldengate-objs := golden.o goldenchar.o goldenblock.o utils.o
else
	KERNELDIR ?= /home/lamer/projects/linux-3.8.5
	#KERNELDIR ?= /home/arik/Project/linux-3.8/ #/home/arik/kernel/linux-3.5
#KERNELDIR ?= /home/arik/Project/git/projects/linux-3.5#/home/arik/kernel/linux-3.5
	PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules
endif

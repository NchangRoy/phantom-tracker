ifneq ($(KERNELRELEASE),)

obj-m := host_ivshmem.o

ccflags-y += -I$(src)/../../include

else

KERNELDIR ?= /lib/modules/$(shell uname -r)/build

.PHONY: default clean modules_install

default:
	$(MAKE) -C $(KERNELDIR) M=$$PWD modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$$PWD modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=$$PWD clean

endif
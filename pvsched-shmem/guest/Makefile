ifneq ($(KERNELRELEASE),)

obj-m := guest_ivshmem.o

ccflags-y += -I$(src)/../../include

else

KERNEL_RELEASE := $(shell uname -r)
KERNELDIR ?= /lib/modules/$(KERNEL_RELEASE)/build

# TODO: Remove this check after scripts/lib/cloudinit.sh installs the required kernel headers.
ifeq ($(wildcard $(KERNELDIR)/Makefile),)
$(error Kernel build tree not found at $(KERNELDIR). Install headers for $(KERNEL_RELEASE) (for Debian/Ubuntu: apt install linux-headers-$(KERNEL_RELEASE)) or override KERNELDIR=/path/to/kernel/build)
endif

.PHONY: default clean modules_install

default:
	$(MAKE) -C $(KERNELDIR) M=$$PWD modules

modules_install:
	$(MAKE) -C $(KERNELDIR) M=$$PWD modules_install

clean:
	$(MAKE) -C $(KERNELDIR) M=$$PWD clean
endif
/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Contributors:
 *   Human: Himadri Chhaya-Shailesh
 *   AI: Claude Sonnet 4.6, ChatGPT-5.5
 */

#include <linux/cdev.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/module.h>

/* grep for the following string in dmesg for debugging */
#define HOST_IVSHMEM_NAME "host_ivshmem"

/* Maximum number of backends supported at the same time */
#define HOST_IVSHMEM_MAX_DEVS 1024U

/*
 * When we create a character device, userspace accesses it via a /dev node.
 * 
 * Internally, the kernel identifies that node by a device number.
 * - The major number selects the driver (this module).
 * - The minor number selects one device instance managed by that driver.
 *   (The per-VM ivshmem-plain backends are the device instances in this case.)
 * 
 *  We fill the following structure with a dynamically allocated major number
 *  and a minor number assigned to each device instance. This information is
 *  kept globally because the module initialization, device creation, and
 *  module exit functions all need to access it.
 */
static dev_t host_ivshmem_devt;

/*
 * sysfs is a virtual filesystem mounted at /sys, which exposes kernel objects
 * to userspace as files and directories. This is useful for us to manage the
 * per-vm ivshemem backends from our userspace scripts.
 * 
 * Creating the following class provides a common directory for all devices
 * managed by this driver at /sys/class/host_ivshmem and a common prefix for
 * all device nodes at /dev/host_ivshmem*.
 */
static struct class *host_ivshmem_class;

static int __init host_ivshmem_init(void)
{
    int ret;

    /* 
     * Reserve a range of character-device numbers for this module
     * The kernel chooses a free major number for us and reserves
     * HOST_IVSHMEM_MAX_DEVS minor numbers starting at minor 0.
     * 
     * Later, the per-VM ivshmem backends will use the minor numbers in this
     * range, and userspace will access them via the corresponding
     * /dev/host_ivshmem* nodes and manage them via /sys/class/host_ivshmem.
     */
    ret = alloc_chrdev_region(&host_ivshmem_devt, 0,
				  HOST_IVSHMEM_MAX_DEVS,
				  HOST_IVSHMEM_NAME);

	if (ret)
		return ret;

	host_ivshmem_class = class_create(HOST_IVSHMEM_NAME);
	if (IS_ERR(host_ivshmem_class)) {
		ret = PTR_ERR(host_ivshmem_class);
		unregister_chrdev_region(host_ivshmem_devt,
					 HOST_IVSHMEM_MAX_DEVS);
		return ret;
	}

	pr_info("host_ivshmem: loaded major=%u\n",
		MAJOR(host_ivshmem_devt));
	return 0;
    
}

static void __exit host_ivshmem_exit(void)
{
    class_destroy(host_ivshmem_class);
	unregister_chrdev_region(host_ivshmem_devt, HOST_IVSHMEM_MAX_DEVS);
	pr_info("host_ivshmem: unloaded\n");
}

module_init(host_ivshmem_init);
module_exit(host_ivshmem_exit);

MODULE_AUTHOR("Himadri Chhaya-Shailesh");
MODULE_DESCRIPTION("Host-pvsched-shmem using QEMU ivshmem-plain devices");
MODULE_LICENSE("GPL");
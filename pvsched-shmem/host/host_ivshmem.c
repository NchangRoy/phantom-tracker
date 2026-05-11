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
#include <linux/errno.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/limits.h>

/* grep for the following string in dmesg for debugging */
#define HOST_IVSHMEM_NAME "host_ivshmem"

/* Maximum number of backends supported at the same time */
#define HOST_IVSHMEM_MAX_DEVS 1024U

/*
 * For now, each backend owns exactly two pages:
 *   page 0: host-to-guest communication
 *   page 1: guest-to-host communication
 */
#define HOST_IVSHMEM_SIZE (2 * PAGE_SIZE)

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

/*
 * One static ivshmem backend instance for initial testing.
 *
 * cdev  - Connects this backend to file_operations.
 * dev   - Represents this backend in sysfs and enables /dev node creation
 * mem   - Kernel memory backing the shared-memory region
 * size  - Size of the shared-memory region
 * minor - Minor number assigned to this backend
 */
struct host_ivshmem_backend {
	struct cdev cdev;
	struct device *dev;
	void *mem;
	size_t size;
	int minor;
};

static struct host_ivshmem_backend backend = {
	.size = HOST_IVSHMEM_SIZE,
	.minor = 0,
};

static int host_ivshmem_open(struct inode *inode, struct file *file)
{
	if (iminor(inode) != backend.minor)
		return -ENODEV;

	/* Save the backend pointer to be later used by the fops */
	file->private_data = &backend;
	return 0;
}

static int host_ivshmem_release(struct inode *inode, struct file *file)
{
	file->private_data = NULL;
	return 0;
}

static int host_ivshmem_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct host_ivshmem_backend *backend = file->private_data;
	unsigned long len = vma->vm_end - vma->vm_start;

	if (!backend)
		return -ENODEV;

	/*
	 * Mapping must start at the beginning of the backend memory.
	 * All users are expected to mmap the entire shared-memory region.
	 */
	if (vma->vm_pgoff != 0 || len != backend->size)
		return -EINVAL;

	if (!(vma->vm_flags & VM_SHARED))
		return -EINVAL;

	return remap_vmalloc_range(vma, backend->mem, 0);
}

/* Minimal file operations for testing the static backend */
static const struct file_operations host_ivshmem_fops = {
	.owner = THIS_MODULE,
	.open = host_ivshmem_open,
	.release = host_ivshmem_release,
	.llseek = noop_llseek,
	.mmap = host_ivshmem_mmap,
};

static int host_ivshmem_create_static_backend(void)
{
	dev_t devt = MKDEV(MAJOR(host_ivshmem_devt), backend.minor);
	int ret;

	/* Allocate zero-filled, page-backed memory */
	backend.mem = vmalloc_user(backend.size);
	if (!backend.mem)
		return -ENOMEM;

	/* Bind this backend's device number to our fops. */
	cdev_init(&backend.cdev, &host_ivshmem_fops);
	backend.cdev.owner = THIS_MODULE;

	ret = cdev_add(&backend.cdev, devt, 1);
	if (ret)
		goto err_free_mem;

	backend.dev = device_create(host_ivshmem_class, NULL, devt, &backend,
					    "host_ivshmem%d", backend.minor);
	if (IS_ERR(backend.dev)) {
		ret = PTR_ERR(backend.dev);
		backend.dev = NULL;
		goto err_del_cdev;
	}

	pr_info("host_ivshmem: created /dev/host_ivshmem%d size=%zu\n",
		backend.minor, backend.size);
	return 0;

err_del_cdev:
	cdev_del(&backend.cdev);
err_free_mem:
	vfree(backend.mem);
	backend.mem = NULL;
	return ret;
}

static void host_ivshmem_destroy_static_backend(void)
{
	dev_t devt = MKDEV(MAJOR(host_ivshmem_devt), backend.minor);

	if (backend.dev) {
		device_destroy(host_ivshmem_class, devt);
		backend.dev = NULL;
	}

	cdev_del(&backend.cdev);

	vfree(backend.mem);
	backend.mem = NULL;
}

static int __init host_ivshmem_init(void)
{
    int ret;

    /* 
     * Reserve a range of character-device numbers for this module.
     * The kernel chooses a free major number for us and reserves
     * HOST_IVSHMEM_MAX_DEVS minor numbers starting at 0.
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
		goto err_unregister_chrdev;
	}

	ret = host_ivshmem_create_static_backend();

	if (ret)
		goto err_destroy_class;

	pr_info("host_ivshmem: loaded major=%u\n",
		MAJOR(host_ivshmem_devt));
	return 0;

err_destroy_class:
	class_destroy(host_ivshmem_class);
	host_ivshmem_class = NULL;
err_unregister_chrdev:
	unregister_chrdev_region(host_ivshmem_devt, HOST_IVSHMEM_MAX_DEVS);
	return ret;    
}

static void __exit host_ivshmem_exit(void)
{
	host_ivshmem_destroy_static_backend();
	class_destroy(host_ivshmem_class);
	unregister_chrdev_region(host_ivshmem_devt, HOST_IVSHMEM_MAX_DEVS);
	pr_info("host_ivshmem: unloaded\n");
}

module_init(host_ivshmem_init);
module_exit(host_ivshmem_exit);

MODULE_AUTHOR("Himadri Chhaya-Shailesh");
MODULE_DESCRIPTION("Host-pvsched-shmem using QEMU ivshmem-plain devices");
MODULE_LICENSE("GPL");
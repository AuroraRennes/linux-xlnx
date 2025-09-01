// SPDX-License-Identifier: GPL-2.0

#include "ksight.h"
#include <linux/atomic.h>
#include <linux/cdev.h>
#include <linux/errno.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_reserved_mem.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>

#define DRIVER_NAME "ksight"

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("ksight");
MODULE_DESCRIPTION("Ksight DMA ring driver for LSM events");

/* Control-block of the ring buffer */
struct ksight_ring_ctrl {
    u32 prod;        /* Producer head */
    u32 cons_copro;  /* Consumer head (co-processor) */
    u32 cons_user;   /* Consumer head (direct access from user-space) */
    u32 size;        /* Total number of slots in the ring buffer (po2) */
    u32 mask;        /* size - 1 to speed wraps*/
    u32 dropped;     /* Count of events dropped due to full buffer */
    u32 flags;       /* Bit flags, for configuration, e.g. bit0=user tap enabled */
};

/* Ksight shared memory, control structure and buffer */
struct ksight_shm {
    struct ksight_ring_ctrl ctrl;
    struct ksight_tag_event slots[];
};

/* Definition of the buffer, its base address and size */
static struct ksight_shm *shm;
static void __iomem *shm_phys_base;
static size_t shm_size;
static dev_t ksight_devt;
static struct cdev ksight_cdev;
static struct platform_device *ksight_pdev;

/* sysfs attributes */
static phys_addr_t shm_phys_addr;
bool ksight_enabled;
EXPORT_SYMBOL_GPL(ksight_enabled);
static DEFINE_MUTEX(ksight_enable_lock);

static DECLARE_WAIT_QUEUE_HEAD(buf_wq);

static struct class *ksight_class; /* Kernel class for /dev and sysfs */
static struct device *ksight_dev;  /* Device for user-space interaction */

/* ----------------------
 * Ring buffer push
 * ---------------------- */
void ksight_push_event(const struct ksight_tag_event *ev)
{
    /* Early bailout in case LSM hooks are used before buffer initialization */
    if (!shm) return;
    /* Memory barrier to access producer index */
    u32 prod = smp_load_acquire(&shm->ctrl.prod);
    u32 next = prod + 1;
    /* Memory barrier to access copro index */
    u32 min_cons = smp_load_acquire(&shm->ctrl.cons_copro);

    /* Oldest-event dropping policy */
    if (next - min_cons > shm->ctrl.size) {
        shm->ctrl.dropped++;
        min_cons++;
        smp_store_release(&shm->ctrl.cons_copro, min_cons);
    }

    /* Store the tag event */
    shm->slots[prod & shm->ctrl.mask] = *ev;
    smp_store_release(&shm->ctrl.prod, next);
    /* Notice the wait queue in ksight_read */
    wake_up_interruptible(&buf_wq);
}
EXPORT_SYMBOL_GPL(ksight_push_event);

/* ----------------------
 * Character device
 * ---------------------- */
static ssize_t ksight_read(struct file *f, char __user *buf, size_t len, loff_t *ppos)
{
    size_t n = 0;

    /* Early bailout to avoid lock */
    if (len < sizeof(struct ksight_tag_event))
        return -EINVAL;

    /* Memory barrier for producer and USER consumer */
    u32 prod = smp_load_acquire(&shm->ctrl.prod);
    u32 cons = READ_ONCE(shm->ctrl.cons_user);
    /* If no new events, wait for a wake up from the wait queue */
    if (prod == cons) {
        if (f->f_flags & O_NONBLOCK)
            return -EAGAIN;
        /* Wait until the producer advances beyond latest consumer */
        if(wait_event_interruptible(buf_wq, smp_load_acquire(&shm->ctrl.prod) != cons))
            return -ERESTARTSYS;
        prod = smp_load_acquire(&shm->ctrl.prod);
    }
    /* Copy all new events to the user-space */
    while (n < len / sizeof(struct ksight_tag_event) && cons != prod) {
        struct ksight_tag_event ev = shm->slots[cons & shm->ctrl.mask];
        if (copy_to_user(buf + n * sizeof(ev), &ev, sizeof(ev)))
            return -EFAULT;
        cons++;
        n++;
    }

    smp_store_release(&shm->ctrl.cons_user, cons);
    return n * sizeof(struct ksight_tag_event);
}

static const struct file_operations ksight_fops = {
    .owner = THIS_MODULE,
    .read  = ksight_read,
};

/* ----------------------
 * sysfs attributes
 * ---------------------- */


/* Ring physical address */
static ssize_t ring_phys_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%pa\n", &shm_phys_addr);
}
static DEVICE_ATTR_RO(ring_phys);

/* Enable flag */
static ssize_t enable_show(struct device *dev,
                           struct device_attribute *attr, char *buf)
{
    return sprintf(buf, "%d\n", ksight_enabled ? 1 : 0);
}

static ssize_t enable_store(struct device *dev,
                            struct device_attribute *attr, const char *buf, size_t count)
{
    unsigned long val;

    /* Check unsigned int */
    if (kstrtoul(buf, 0, &val))
        return -EINVAL;

    mutex_lock(&ksight_enable_lock);
    ksight_enabled = !!val; // 1 if nonzero, 0 otherwise
    mutex_unlock(&ksight_enable_lock);

    return count;
}
static DEVICE_ATTR_RW(enable);

/* ----------------------
 * Platform probe/remove
 * ---------------------- */

 static int ksight_probe(struct platform_device *pdev)
{
    struct reserved_mem *rmem;
    struct device_node *np;
    int ret;

    /* Find reserved memory from DT via phandle,
     * Needed as the ksight-shm shared memory used
     * a reference to the reserved memory part.
     *
     * Device tree excerpt:
     * / {
        reserved-memory {
            #address-cells= <2>;
            #size-cells= <2>;
            ranges;
            ksight_buffer: ksight_buffer@60400000 {
                compatible = "shared-dma-pool";
                no-map;                                 // Incompatible with reusable
                reg = <0x0 0x60400000 0x0 0x04000000>;  // Address, size
                label = "ksight_buffer";                // Label to use
            };
        };

        ksight-shm@60400000 {
            compatible = "aurora,ksight-shm";          // Name used in the driver
            device-name = "ksight-shm0";               // Name of the buffer
            size = <0x04000000>;                       // 64MiB
            memory-region = <&ksight_buffer>;          // Link to the reserved-memory defined earlier
        };
    }; */

    np = of_parse_phandle(pdev->dev.of_node, "memory-region", 0);
    if (!np) {
        dev_err(&pdev->dev, "ksight: no memory-region property\n");
        return -EINVAL;
    }

    rmem = of_reserved_mem_lookup(np); // Get the reserved memory from the above node
    of_node_put(np);                   // Frees the reference to the node
    if (!rmem) {
        dev_err(&pdev->dev, "ksight: cannot find reserved memory\n");
        return -ENODEV;
    }

    shm_phys_addr = rmem->base;
    shm_size      = rmem->size;

    /* Map reserved memory into kernel VA */
    shm_phys_base = memremap(rmem->base, rmem->size, MEMREMAP_WB);
    if (!shm_phys_base) {
        dev_err(&pdev->dev, "ksight: memremap failed\n");
        return -ENOMEM;
    }

    /* Initialize the ring buffer */
    shm = (struct ksight_shm *)shm_phys_base;
    memset(shm, 0, shm_size);

    shm->ctrl.size = (u32)((shm_size - sizeof(struct ksight_ring_ctrl)) /
                           sizeof(struct ksight_tag_event));
    shm->ctrl.mask = shm->ctrl.size - 1;

    /* Character device, allocate region */
    ret = alloc_chrdev_region(&ksight_devt, 0, 1, "ksight");
    if (ret)
        goto err_unmap;

    /* Initialize character device */
    cdev_init(&ksight_cdev, &ksight_fops);
    ksight_cdev.owner = THIS_MODULE;
    ret = cdev_add(&ksight_cdev, ksight_devt, 1);
    if (ret)
        goto err_unregister;

    pr_info("ksight: cdev added, major=%u minor=%u\n", MAJOR(ksight_devt), MINOR(ksight_devt));

    /* Create ksight class */
    ksight_class = class_create(THIS_MODULE, "ksight");
    if (IS_ERR(ksight_class)) {
        ret = PTR_ERR(ksight_class);
        goto err_cdev;
    }

    ksight_dev = device_create(ksight_class, NULL, ksight_devt, NULL, "ksight");
    if (IS_ERR(ksight_dev)) {
        ret = PTR_ERR(ksight_dev);
        goto err_class;
    }

    /* Sysfs attribute for debug */
    ret = device_create_file(ksight_dev, &dev_attr_ring_phys);
    if (ret) {
        dev_err(&pdev->dev, "failed to create ring_phys sysfs attr\n");
        goto err_dev;
    }

    ret = device_create_file(ksight_dev, &dev_attr_enable);
    if (ret) {
        dev_err(&pdev->dev, "failed to create enable sysfs attribute\n");
    }


    dev_info(&pdev->dev,
             "ksight DMA ring initialized phys=%pa virt=%p size=%u entries\n",
             &shm_phys_addr, shm_phys_base, shm->ctrl.size);

    return 0;

err_dev:
    device_destroy(ksight_class, ksight_devt);
err_class:
    class_destroy(ksight_class);
err_cdev:
    cdev_del(&ksight_cdev);
err_unregister:
    unregister_chrdev_region(ksight_devt, 1);
err_unmap:
    memunmap(shm_phys_base);
    return ret;
}

static int ksight_remove(struct platform_device *pdev)
{
    device_remove_file(ksight_dev, &dev_attr_ring_phys);
    device_destroy(ksight_class, ksight_devt);
    class_destroy(ksight_class);
    cdev_del(&ksight_cdev);
    unregister_chrdev_region(ksight_devt, 1);
    memunmap(shm_phys_base);
    return 0;
}

/* ----------------------
 * Device tree match and driver registration
 * ---------------------- */
static const struct of_device_id ksight_of_match[] = {
    { .compatible = "aurora,ksight-shm" },
    { }
};
MODULE_DEVICE_TABLE(of, ksight_of_match);

static struct platform_driver ksight_platform_driver = {
    .probe  = ksight_probe,
    .remove = ksight_remove,
    .driver = {
        .name = DRIVER_NAME,
        .of_match_table = ksight_of_match,
        .owner = THIS_MODULE,
    },
};


/* ----------------------
 * Platform device registration
 * ---------------------- */

static int __init ksight_module_init(void)
{
    int ret;

    /* Register the platform driver first */
    ret = platform_driver_register(&ksight_platform_driver);
    if (ret)
        return ret;

    /* Register the platform device to trigger the probe */
    ksight_pdev = platform_device_register_simple("ksight-shm", -1, NULL, 0);
    if (IS_ERR(ksight_pdev)) {
        platform_driver_unregister(&ksight_platform_driver);
        return PTR_ERR(ksight_pdev);
    }

    return 0;
}

static void __exit ksight_module_exit(void)
{
    platform_device_unregister(ksight_pdev);
    platform_driver_unregister(&ksight_platform_driver);
}

module_init(ksight_module_init);
module_exit(ksight_module_exit);
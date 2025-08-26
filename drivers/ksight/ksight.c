// SPDX-License-Identifier: GPL-2.0

#include "ksight.h"

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
    struct tag_event slots[];
};

/* Definition of the buffer, its base address and size */
static struct ksight_shm *shm;
static void __iomem *shm_phys_base;
static size_t shm_size;
static phys_addr_t shm_phys_addr;

static DECLARE_WAIT_QUEUE_HEAD(buf_wq);

static struct class *ksight_class; /* Kernel class for /dev and sysfs */
static struct device *ksight_dev;  /* Device for user-space interaction */

/* ----------------------
 * Ring buffer push
 * ---------------------- */
void ksight_push_event(const struct tag_event *ev)
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
    while (n == 0) {
        /* Memory barrier for producer and USER consumer */
        u32 prod = smp_load_acquire(&shm->ctrl.prod);
        u32 cons = READ_ONCE(shm->ctrl.cons_user);
        /* If no new events, wait for a wake up from the wait queue */
        if (prod == cons) {
            if (f->f_flags & O_NONBLOCK)
                return -EAGAIN;
            wait_event_interruptible(buf_wq, smp_load_acquire(&shm->ctrl.prod) != cons);
            continue;
        }
        /* Copy all new events to the user-space */
        while (n < len / sizeof(struct tag_event) && cons != prod) {
            struct tag_event ev = shm->slots[cons & shm->ctrl.mask];
            if (copy_to_user(buf + n * sizeof(ev), &ev, sizeof(ev)))
                return -EFAULT;
            cons++; n++;
        }
        smp_store_release(&shm->ctrl.cons_user, cons);
    }
    return n * sizeof(struct tag_event);
}

static const struct file_operations ksight_fops = {
    .owner = THIS_MODULE,
    .read  = ksight_read,
};

/* ----------------------
 * sysfs expose ring physical base
 * ---------------------- */

static ssize_t ring_phys_show(struct device *dev,
                              struct device_attribute *attr, char *buf)
{
    return sysfs_emit(buf, "%pa\n", &shm_phys_addr);
}
static DEVICE_ATTR_RO(ring_phys);

/* ----------------------
 * Platform probe / remove
 * ---------------------- */
static int ksight_probe(struct platform_device *pdev)
{
    struct resource *res;
    int ret;

    /* Fetch the first memory region associated with the device */
    res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    if (!res) {
        dev_err(&pdev->dev, "no memory resource\n");
        return -ENODEV;
    }

    /* Map it */
    shm_size = resource_size(res);
    shm_phys_addr = res->start;
    shm_phys_base = memremap(res->start, shm_size, MEMREMAP_WT);
    if (!shm_phys_base) return -ENOMEM;

    /* Initialize buffer, size and mask */
    shm = (struct ksight_shm *)shm_phys_base;
    memset(shm, 0, shm_size);
    shm->ctrl.size = (shm_size - sizeof(struct ksight_ring_ctrl)) / sizeof(struct tag_event);
    shm->ctrl.mask = shm->ctrl.size - 1;

    /* Create the character device */
    ksight_class = class_create(THIS_MODULE, "ksight");
    if (IS_ERR(ksight_class))
        return -ENOMEM;
    ksight_dev = device_create(ksight_class, NULL, 0, NULL, "ksight");
    if (IS_ERR(ksight_dev))
        return -ENOMEM;

    /* Register the cleanup action on error */
    ret = devm_add_action_or_reset(&pdev->dev, (void(*)(void*))class_destroy, ksight_class);
    if (ret) return ret;

    /* Create the sysfs attribute for physical address */
    ret = device_create_file(ksight_dev, &dev_attr_ring_phys);
    if (ret) {
        dev_err(&pdev->dev, "failed to create sysfs attr\n");
        return ret;
    }

    dev_info(&pdev->dev, "ksight DMA ring initialized, size=%u entries\n", shm->ctrl.size);
    return 0;
}

static int ksight_remove(struct platform_device *pdev)
{
    device_remove_file(ksight_dev, &dev_attr_ring_phys);
    device_destroy(ksight_class, 0);
    class_destroy(ksight_class);
    if (shm_phys_base)
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

module_platform_driver(ksight_platform_driver);

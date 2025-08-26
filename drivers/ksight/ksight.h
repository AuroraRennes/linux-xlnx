/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KSIGHT_H
#define _LINUX_KSIGHT_H

#include <linux/module.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/types.h>
#include <linux/sysfs.h>
#include <linux/atomic.h>

struct tag_event {
	u32 pid;
	u32 tid;
	u64 timestamp_ns;
	unsigned long addr_start;
	unsigned long addr_end;
	u32 tag_id;
	u32 op_type;    /* 0=read,1=write,2=recv,3=send */
} __packed;

void ksight_push_event(const struct tag_event *ev);

#endif /* _LINUX_KSIGHT_H */
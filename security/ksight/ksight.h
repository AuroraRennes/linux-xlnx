/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KSIGHT_H
#define _LINUX_KSIGHT_H

#define HEALTHCHECK 0

#if HEALTHCHECK
#include <linux/atomic.h>
#endif
#include <linux/init.h>
#include <linux/lsm_hooks.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/security.h>
#include <linux/socket.h>
#include <linux/uio.h>

struct tag_event {
	u32 pid;
	u32 tid;
	u64 timestamp_ns;
	unsigned long addr_start;
	unsigned long addr_end;
	u32 tag_id;
	u32 op_type;    /* 0=read,1=write,2=recv,3=send */
} __packed;

#if HEALTHCHECK
void ksight_push_event(const struct tag_event *ev);
#else
extern void ksight_push_event(const struct tag_event *ev);
#endif

extern bool ksight_enabled;

#endif /* _LINUX_KSIGHT_H */

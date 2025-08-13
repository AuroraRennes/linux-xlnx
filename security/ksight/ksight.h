/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_KSIGHT_H
#define _LINUX_KSIGHT_H

#include <linux/types.h>

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

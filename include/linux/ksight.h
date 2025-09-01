/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _KSIGHT_H
#define _KSIGHT_H

#include <linux/types.h>

/* -----------------------
 * Ksight objects
 * -----------------------
 *
 * Ksight monitors the information flows between different
 * types of objects in the kernel. To differenciate them, we
 * use this enumeration based on Laurent Georget's work.
 */

enum ksight_obj_type {
    KS_OBJ_NONE,
    KS_OBJ_FILE,
    KS_OBJ_PIPE,
    KS_OBJ_MEM,
    KS_OBJ_SHM,
    KS_OBJ_MQUEUE_SYSV,
    KS_OBJ_MQUEUE_POSIX,
    KS_OBJ_SOCKET,
};


struct ksight_object {
    u32 ksight_obj_type;
    u32 pid;        /* valid if OBJ_MEM */
	u32 tid;
    u64 id;         /* inode, pipe id, shmid, or base address */
    u64 size;       /* region size (bytes) */
};


struct ksight_tag_event {
	struct ksight_object src;
	struct ksight_object dst;
	u64 timestamp;
};

#endif /* _KSIGHT_H */
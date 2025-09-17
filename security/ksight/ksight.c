// SPDX-License-Identifier: GPL-2.0

#include "ksight.h"
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/lsm_hooks.h>
#include <linux/mm.h>
#include <linux/rwsem.h>
#include <linux/sched/mm.h>
#include <linux/socket.h>
#include <linux/types.h>
#include <linux/uio.h>

/* -----------------------
 * LSM hook implementations
 * -----------------------
 *
 * Ksight LSM hooks for system call information flows. Each hooked
 * system call triggers the send of a tag event in Ksight shared
 * memory, with corresponding information for the co-processor
 * to update shadow memory.
 */

/* socket_recvmsg: called after the kernel receives into the buffer.
 */
static int ksight_socket_recvmsg(struct socket *sock, struct msghdr *msg,
				 int size, int flags)
{
	struct ksight_tag_event ev;
	struct iovec iov;
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long addr;

	/* Bailout if ksight is not enabled */
	if (!READ_ONCE(ksight_enabled))
		return 0;

	/* Bailout if pid is not the expected one */
	if (traced_pid != 0 && task_tgid_nr(current) != traced_pid)
		return 0;


	/* Bailout if there is no message */
	if (!msg || !msg->msg_iter.count || msg->msg_iter.count == 0)
		return 0;

	/* Bailout if iov is not a valid user-space buffer */
	if (!(iter_is_iovec(&msg->msg_iter) || iter_is_ubuf(&msg->msg_iter)))
		return 0;

	/* Extract the base address from the io vector */
	iov = iov_iter_iovec(&msg->msg_iter);
	addr = (u64)iov.iov_base;

	/* Lookup the VMA containing the user buffer */
	mm = current->mm;
	if (!mm)
		return 0;

	down_read(&mm->mmap_lock);
	vma = find_vma(mm, addr);
	if (!vma || addr < vma->vm_start) {
		up_read(&mm->mmap_lock);
		return 0;
	}

	/* Source = socket */
	ev.src.ksight_obj_type = KS_OBJ_SOCKET;
	ev.src.pid  = 0;         /* Not relevant */
	ev.src.tid  = 0;         /* Not relevant */
	ev.src.id   = (u64)sock; /* Socket pointer */
	ev.src.size = 0;         /* Not relevant */

	/* Destination = user buffer */
	ev.dst.ksight_obj_type = KS_OBJ_MEM;
	ev.dst.pid = (u32)task_tgid_nr(current);  /* process */
	ev.dst.tid = (u32)task_pid_nr(current);   /* thread */
	ev.dst.id     = (u64)vma->vm_start;
	ev.dst.size   = iov.iov_len;

	up_read(&mm->mmap_lock);

	ev.timestamp = ktime_get_ns();

	ksight_push_event(&ev);
	return 0;
}

/* socket_sendmsg: called before kernel sends from user buffer.
 */
static int ksight_socket_sendmsg(struct socket *sock, struct msghdr *msg,
				 int size)
{
	struct ksight_tag_event ev;
	struct iovec iov;
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long addr;


	/* Bailout if ksight is not enabled */
	if (!READ_ONCE(ksight_enabled))
		return 0;

	/* Bailout if pid is not the expected one */
	if (traced_pid != 0 && task_tgid_nr(current) != traced_pid)
		return 0;

	/* Bailout if there is no message */
	if (!msg || !msg->msg_iter.count || msg->msg_iter.count == 0)
		return 0;

	/* Bailout if iov is not a valid user-space buffer */
	if (!(iter_is_iovec(&msg->msg_iter) || iter_is_ubuf(&msg->msg_iter)))
		return 0;

	/* Extract the base address from the io vector */
	iov = iov_iter_iovec(&msg->msg_iter);
	addr = (u64)iov.iov_base;

	/* Lookup the VMA containing the user buffer */
	mm = current->mm;
	if (!mm)
		return 0;

	down_read(&mm->mmap_lock);
	vma = find_vma(mm, addr);
	if (!vma || addr < vma->vm_start) {
		up_read(&mm->mmap_lock);
		return 0;
	}
	/* Source = user buffer */
	ev.src.ksight_obj_type = KS_OBJ_MEM;
	ev.src.pid = (u32)task_tgid_nr(current);  /* process */
	ev.src.tid = (u32)task_pid_nr(current);   /* thread */
	ev.src.id     = (u64)vma->vm_start;
	ev.src.size   = iov.iov_len;

	/* Destination = socket */
	ev.dst.ksight_obj_type = KS_OBJ_SOCKET;
	ev.dst.pid  = 0;         /* Not relevant */
	ev.dst.tid  = 0;         /* Not relevant */
	ev.dst.id   = (u64)sock; /* Socket pointer */
	ev.dst.size = 0;         /* Not relevant */

	up_read(&mm->mmap_lock);

	ev.timestamp = ktime_get_ns();

	ksight_push_event(&ev);
	return 0;
}

static int ksight_vfs_readfile(struct file *file, char __user *buf, ssize_t ret)
{
	struct ksight_tag_event ev;

	/* Bailout if ksight is not enabled */
	if (!READ_ONCE(ksight_enabled))
		return 0;

	/* Bailout if pid is not the expected one */
	if (traced_pid != 0 && task_tgid_nr(current) != traced_pid)
		return 0;

	/* Bailout if looking at the character device */
	if (file->f_inode->i_rdev == ksight_get_devno())
		return 0;

	/* Source = file */
	ev.src.ksight_obj_type = KS_OBJ_FILE;
	ev.src.pid    = 0; /* Not relevant */
	ev.src.tid    = 0; /* Not relevant */
	ev.src.id     = (u64)file;
	ev.src.size   = 0; /* Not relevant */

	/* Destination = user memory */
	ev.dst.ksight_obj_type = KS_OBJ_MEM;
	ev.dst.pid = (u32)task_tgid_nr(current);  /* process */
	ev.dst.tid = (u32)task_pid_nr(current);   /* thread */
	ev.dst.id   = (u64)buf;
	ev.dst.size = ret;


	ev.timestamp = ktime_get_ns();

	ksight_push_event(&ev);
	return 0;
}


static int ksight_vfs_writefile(struct file *file, const char __user *buf, ssize_t ret)
{
	struct ksight_tag_event ev;

	/* Bailout if ksight is not enabled */
	if (!READ_ONCE(ksight_enabled))
		return 0;

	/* Bailout if pid is not the expected one */
	if (traced_pid != 0 && task_tgid_nr(current) != traced_pid)
		return 0;

	/* Bailout if looking at the character device */
	if (file->f_inode->i_rdev == ksight_get_devno())
		return 0;

	/* Source = user memory */
	ev.src.ksight_obj_type = KS_OBJ_MEM;
	ev.src.pid  = (u32)task_tgid_nr(current);
	ev.src.tid  = (u32)task_pid_nr(current);
	ev.src.id   = (u64)buf;
	ev.src.size = ret;

	/* Destination = file */
	ev.dst.ksight_obj_type = KS_OBJ_FILE;
	ev.dst.pid    = 0;
	ev.dst.tid    = 0;
	ev.dst.id     = (u64)file;
	ev.dst.size   = 0;

	ev.timestamp = ktime_get_ns();

	ksight_push_event(&ev);
	return 0;
}



/* LSM hook list */
static struct security_hook_list ksight_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(socket_recvmsg, ksight_socket_recvmsg),
	LSM_HOOK_INIT(socket_sendmsg, ksight_socket_sendmsg),
	LSM_HOOK_INIT(vfs_readfile, ksight_vfs_readfile),
	LSM_HOOK_INIT(vfs_writefile, ksight_vfs_writefile),
};

/* Init */
static __init int ksight_lsm_init(void)
{
	security_add_hooks(ksight_hooks, ARRAY_SIZE(ksight_hooks), "ksight");
	pr_info("ksight_lsm: registered\n");
	return 0;
}

DEFINE_LSM(ksight) = {
	.name = "ksight",
	.init = ksight_lsm_init,
};
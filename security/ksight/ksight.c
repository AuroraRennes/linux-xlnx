// SPDX-License-Identifier: GPL-2.0

#include "ksight.h"
#include <linux/init.h>
#include <linux/lsm_hooks.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
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

	/* Bailout if ksight is not enabled */
	if (!READ_ONCE(ksight_enabled))
		return 0;

	/* Bailout if there is no message */
	if (!msg || !msg->msg_iter.count || msg->msg_iter.count == 0)
		return 0;

	/* Bailout if iov is not a valid user-space buffer */
	if (!(iter_is_iovec(&msg->msg_iter) || iter_is_ubuf(&msg->msg_iter)))
		return 0;

	iov = iov_iter_iovec(&msg->msg_iter);

	/* Source = socket */
	ev.src.ksight_obj_type = KS_OBJ_SOCKET;
	ev.src.pid  = 0;         /* Not relevant */
	ev.src.tid  = 0;         /* Not relevant */
	ev.src.id   = (u64)sock; /* Socket pointer */
	ev.src.size = 0;         /* Not relevant */

	/* Destination = user buffer */
	ev.dst.ksight_obj_type = KS_OBJ_MEM;
	ev.dst.pid    = (u32)task_pid_nr(current);
	ev.dst.tid    = (u32)task_tgid_nr(current);
	ev.dst.id     = (u64)iov.iov_base;
	ev.dst.size   = size; // TODO: or iov.iov_len?

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

	/* Bailout if ksight is not enabled */
	if (!READ_ONCE(ksight_enabled))
		return 0;

	/* Bailout if there is no message */
	if (!msg || !msg->msg_iter.count || msg->msg_iter.count == 0)
		return 0;

	/* Bailout if iov is not a valid user-space buffer */
	if (!(iter_is_iovec(&msg->msg_iter) || iter_is_ubuf(&msg->msg_iter)))
		return 0;

	iov = iov_iter_iovec(&msg->msg_iter);

	/* Source = user buffer */
	ev.src.ksight_obj_type = KS_OBJ_MEM;
	ev.src.pid    = (u32)task_pid_nr(current);
	ev.src.tid    = (u32)task_tgid_nr(current);
	ev.src.id     = (u64)iov.iov_base;
	ev.src.size   = size; // TODO: or iov.iov_len?

	/* Destination = socket */
	ev.dst.ksight_obj_type = KS_OBJ_SOCKET;
	ev.dst.pid  = 0;         /* Not relevant */
	ev.dst.tid  = 0;         /* Not relevant */
	ev.dst.id   = (u64)sock; /* Socket pointer */
	ev.dst.size = 0;         /* Not relevant */

	ev.timestamp = ktime_get_ns();

	ksight_push_event(&ev);
	return 0;
}

/* LSM hook list */
static struct security_hook_list ksight_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(socket_recvmsg, ksight_socket_recvmsg),
	LSM_HOOK_INIT(socket_sendmsg, ksight_socket_sendmsg),
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
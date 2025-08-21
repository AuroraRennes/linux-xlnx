// SPDX-License-Identifier: GPL-2.0
#include "ksight.h"

/* -----------------------
 * LSM hook implementations
 * -----------------------
 *
 * Minimal examples of LSM hooks for read/write and send/recv.
 * For file read/write you should implement file_read_iter/file_write_iter
 * hooks with careful handling of scatter/gather iov_iter cases.
 */

/* socket_recvmsg: called after the kernel receives into the buffer.
 */
static int ksight_socket_recvmsg(struct socket *sock, struct msghdr *msg,
				 int size, int flags)
{
	struct tag_event ev;
	struct iovec iov;

	if (!msg || !msg->msg_iter.count || msg->msg_iter.count == 0)
		return 0;

	iov = iov_iter_iovec(&msg->msg_iter);

	ev.pid = (u32)task_pid_nr(current);
	ev.tid = (u32)task_tgid_nr(current);
	ev.timestamp_ns = ktime_get_ns();
	ev.addr_start = (unsigned long)iov.iov_base;
	ev.addr_end = ev.addr_start + size;
	ev.tag_id = 0x00000001;
	ev.op_type = 2; /* recv */

	ksight_push_event(&ev);
	return 0;
}

/* socket_sendmsg: called before kernel sends from user buffer.
 */
static int ksight_socket_sendmsg(struct socket *sock, struct msghdr *msg,
				 int size)
{
	struct tag_event ev;
	struct iovec iov;

	if (!msg || !msg->msg_iter.count || msg->msg_iter.count == 0)
		return 0;

	iov = iov_iter_iovec(&msg->msg_iter);

	ev.pid = (u32)task_pid_nr(current);
	ev.tid = (u32)task_tgid_nr(current);
	ev.timestamp_ns = ktime_get_ns();
	ev.addr_start = (unsigned long)iov.iov_base;
	ev.addr_end = ev.addr_start + iov.iov_len;
	ev.tag_id = 0x00000001;
	ev.op_type = 3; /* send */

	ksight_push_event(&ev);
	return 0;
}

/* LSM hook list */
static struct security_hook_list ksight_hooks[] __ro_after_init = {
	LSM_HOOK_INIT(socket_recvmsg, ksight_socket_recvmsg),
	LSM_HOOK_INIT(socket_sendmsg, ksight_socket_sendmsg),
};

/* Public API for driver */
void ksight_push_event(const struct tag_event *ev)
{
	/* Placeholder — driver will override or connect this at runtime */
}
EXPORT_SYMBOL_GPL(ksight_push_event);

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
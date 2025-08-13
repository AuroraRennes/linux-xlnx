// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/lsm_hooks.h>
#include <linux/security.h>
#include <linux/ktime.h>
#include <linux/ksight.h>

/* Core LSM hook handlers */
static int ksight_socket_recvmsg(struct socket *sock, struct msghdr *msg,
                                 int size, int flags)
{
	struct tag_event ev;

	if (!msg || !msg->msg_iter.count)
		return 0;

	ev.pid = task_pid_nr(current);
	ev.tid = task_tgid_nr(current);
	ev.timestamp_ns = ktime_get_ns();
	ev.addr_start = 0;
	ev.addr_end = 0;
	ev.tag_id = 0x1;
	ev.op_type = 2;

	ksight_push_event(&ev);
	return 0;
}

static int ksight_socket_sendmsg(struct socket *sock, struct msghdr *msg,
                                 int size)
{
	struct tag_event ev;

	if (!msg || !msg->msg_iter.count)
		return 0;

	ev.pid = task_pid_nr(current);
	ev.tid = task_tgid_nr(current);
	ev.timestamp_ns = ktime_get_ns();
	ev.addr_start = 0;
	ev.addr_end = 0;
	ev.tag_id = 0x1;
	ev.op_type = 3;

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

security_initcall(ksight_lsm_init);

/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SECURITY_KSIGHT_H
#define _SECURITY_KSIGHT_H

#include <linux/ksight.h>
#include <linux/kdev_t.h>

extern void ksight_push_event(const struct ksight_tag_event *ev);
extern dev_t ksight_get_devno(void);

extern bool ksight_enabled;

#endif /* _SECURITY_KSIGHT_H */

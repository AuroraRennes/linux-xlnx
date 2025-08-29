/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SECURITY_KSIGHT_H
#define _SECURITY_KSIGHT_H

#include <linux/ksight.h>

extern void ksight_push_event(const struct tag_event *ev);

extern bool ksight_enabled;

#endif /* _SECURITY_KSIGHT_H */

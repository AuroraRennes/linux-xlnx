/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _DRIVERS_KSIGHT_H
#define _DRIVERS_KSIGHT_H

#include <linux/ksight.h>

void ksight_push_event(const struct tag_event *ev);

#endif /* _DRIVERS_KSIGHT_H */
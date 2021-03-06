/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright (C) 2015-2018 Jason A. Donenfeld <Jason@zx2c4.com>. All Rights Reserved.
 */

#ifndef _WG_RATELIMITER_H
#define _WG_RATELIMITER_H

#include <linux/skbuff.h>

static int ratelimiter_init(void);
static void ratelimiter_uninit(void);
static bool ratelimiter_allow(struct sk_buff *skb, struct net *net);

#ifdef DEBUG
static bool ratelimiter_selftest(void);
#endif

#endif /* _WG_RATELIMITER_H */

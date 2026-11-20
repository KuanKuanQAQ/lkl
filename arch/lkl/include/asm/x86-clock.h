/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LKL_X86_CLOCK_H
#define _LKL_X86_CLOCK_H

#include <linux/types.h>
#include <asm/host_ops.h>

int lkl_arch_clock_init(void);
unsigned long long lkl_clock_read_counter(void);
void lkl_arch_clock_seed(struct lkl_clock_bootstrap *sample);
void lkl_arch_clock_timekeeper_ready(void);
int lkl_clock_is_ready(void);
u64 lkl_ktime_get_mono_fast_ns(void);
u64 lkl_ktime_get_raw_fast_ns(void);
u64 lkl_ktime_get_real_fast_ns(void);

#endif

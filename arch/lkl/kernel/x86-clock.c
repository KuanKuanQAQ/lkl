// SPDX-License-Identifier: GPL-2.0
/*
 * Privileged x86 KVM clock backend. Only the hardware ABI is shared with the
 * host: no host clocksource, timekeeper, per-CPU variables or time functions.
 * CPU tags are opaque RDTSCP TSC_AUX values discovered at startup.
 */
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/math64.h>
#include <linux/timekeeping.h>
#include <asm/x86-clock.h>

#define MAX_CLOCK_CPUS 256
#define KVM_SYSTEM_TIME_MSR 0x4b564d01
#define PVCLOCK_STABLE 1

/* KVM pvclock ABI, not a host kernel data structure. */
struct lkl_pvclock {
	u32 version, pad0;
	u64 tsc_timestamp, system_time;
	u32 tsc_to_system_mul;
	s8 tsc_shift;
	u8 flags, pad[2];
} __packed;

struct clock_cpu {
	u32 tag;
	const struct lkl_pvclock *pv;
};

static struct clock_cpu clock_cpus[MAX_CLOCK_CPUS];
static unsigned int clock_cpu_count;
static struct lkl_clock_bootstrap origin;
static bool timekeeper_ready;

static inline void clock_cpuid(u32 leaf, u32 *a, u32 *b, u32 *c, u32 *d)
{
	asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d)
		     : "0"(leaf), "2"(0) : "memory");
}

static inline u64 clock_rdtscp(u32 *tag)
{
	u32 lo, hi;

	asm volatile("rdtscp; lfence" : "=a"(lo), "=d"(hi), "=c"(*tag)
		     :: "memory");
	return ((u64)hi << 32) | lo;
}

static inline void clock_read_barrier(void)
{
	/* LKL is configured UP, but EVL readers run on multiple physical CPUs. */
	asm volatile("lfence" ::: "memory");
}

static const struct lkl_pvclock *clock_find_cpu(u32 tag)
{
	unsigned int n;

	for (n = 0; n < clock_cpu_count; n++)
		if (clock_cpus[n].tag == tag)
			return clock_cpus[n].pv;
	/* Unsupported hotplug/tag changes must not silently corrupt time. */
	panic("LKL clock: unregistered TSC_AUX %u", tag);
}

unsigned long long notrace lkl_clock_read_counter(void)
{
	const struct lkl_pvclock *pv;
	u64 tsc, stamp, base, delta;
	u32 tag, sampled_tag, version, mult;
	s8 shift;
	u8 flags;

	for (;;) {
		clock_rdtscp(&tag);
		pv = clock_find_cpu(tag);
		version = READ_ONCE(pv->version);
		if (version & 1)
			continue;
		clock_read_barrier();
		tsc = clock_rdtscp(&sampled_tag);
		stamp = READ_ONCE(pv->tsc_timestamp);
		base = READ_ONCE(pv->system_time);
		mult = READ_ONCE(pv->tsc_to_system_mul);
		shift = READ_ONCE(pv->tsc_shift);
		flags = READ_ONCE(pv->flags);
		clock_read_barrier();
		if (tag == sampled_tag && version == READ_ONCE(pv->version))
			break;
	}
	if (unlikely(!(flags & PVCLOCK_STABLE) || !mult || shift < -31 || shift > 31))
		panic("LKL clock: KVM revoked the stable pvclock contract");
	delta = tsc - stamp;
	if (shift < 0)
		delta >>= -shift;
	else
		delta <<= shift;
	return base + mul_u64_u32_shr(delta, mult, 32);
}

static int clock_setup_cpu(void *unused)
{
	const struct lkl_pvclock *pv;
	u32 a, b, c, d, lo, hi, tag, version, mult;
	s8 shift;
	u8 flags;
	u64 physical;
	unsigned int n;

	clock_cpuid(0x80000000, &a, &b, &c, &d);
	if (a < 0x80000001)
		return -ENODEV;
	clock_cpuid(0x80000001, &a, &b, &c, &d);
	if (!(d & (1U << 27))) /* RDTSCP */
		return -ENODEV;
	clock_cpuid(0x40000000, &a, &b, &c, &d);
	if (a < 0x40000001 || b != 0x4b4d564b || c != 0x564b4d56 || d != 0x4d)
		return -ENODEV;
	clock_cpuid(0x40000001, &a, &b, &c, &d);
	if (!(a & (1U << 3)) || !(a & (1U << 24)))
		return -ENODEV;

	asm volatile("rdmsr" : "=a"(lo), "=d"(hi) : "c"(KVM_SYSTEM_TIME_MSR));
	physical = ((u64)hi << 32) | lo;
	if (!(physical & 1))
		return -ENODEV;
	pv = lkl_ops->clock_map_ram(physical & ~1ULL, sizeof(*pv));
	if (!pv || clock_cpu_count == MAX_CLOCK_CPUS)
		return -ENOSPC;
	clock_rdtscp(&tag);
	for (n = 0; n < clock_cpu_count; n++)
		if (clock_cpus[n].tag == tag)
			return -EINVAL;
	/* Reject unsupported startup data before publishing a usable clock. */
	for (;;) {
		version = READ_ONCE(pv->version);
		if (version & 1)
			continue;
		clock_read_barrier();
		mult = READ_ONCE(pv->tsc_to_system_mul);
		shift = READ_ONCE(pv->tsc_shift);
		flags = READ_ONCE(pv->flags);
		clock_read_barrier();
		if (version == READ_ONCE(pv->version))
			break;
	}
	if (!(flags & PVCLOCK_STABLE) || !mult || shift < -31 || shift > 31)
		return -ENODEV;
	clock_cpus[clock_cpu_count++] = (struct clock_cpu){ .tag = tag, .pv = pv };
	/* The host invokes callbacks serially, so the new entry is now visible. */
	lkl_clock_read_counter();
	return 0;
}

int lkl_arch_clock_init(void)
{
	int ret;

	if (!lkl_ops->clock_for_each_cpu || !lkl_ops->clock_map_ram ||
	    !lkl_ops->clock_bootstrap)
		return -ENODEV;
	clock_cpu_count = 0;
	ret = lkl_ops->clock_for_each_cpu(clock_setup_cpu, NULL);
	if (ret || !clock_cpu_count)
		return ret ?: -ENODEV;
	ret = lkl_ops->clock_bootstrap(lkl_clock_read_counter, &origin);
	if (ret)
		return ret;
	if (!origin.real_ns || origin.uncertainty_ns > 100000)
		return -ERANGE;
	return 0;
}

void lkl_arch_clock_seed(struct lkl_clock_bootstrap *sample)
{
	u64 now = lkl_clock_read_counter();
	u64 elapsed = now - origin.counter_ns;

	*sample = origin;
	sample->counter_ns = now;
	sample->mono_ns += elapsed;
	sample->raw_ns += elapsed;
	sample->real_ns += elapsed;
}

void lkl_arch_clock_timekeeper_ready(void)
{
	WRITE_ONCE(timekeeper_ready, true);
	pr_info("independent pvclock: cpus=%u bootstrap_uncertainty_ns=%llu, local timekeeper ready\n",
		clock_cpu_count, origin.uncertainty_ns);
}

int lkl_clock_is_ready(void)
{
	return READ_ONCE(timekeeper_ready);
}

/* These entry points never acquire LKL's virtual CPU or invoke hostops. */
u64 notrace lkl_ktime_get_mono_fast_ns(void)
{
	return ktime_get_mono_fast_ns();
}

u64 notrace lkl_ktime_get_raw_fast_ns(void)
{
	return ktime_get_raw_fast_ns();
}

u64 notrace lkl_ktime_get_real_fast_ns(void)
{
	return ktime_get_real_fast_ns();
}

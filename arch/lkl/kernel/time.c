#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched/clock.h>
#include <linux/delay.h>
#include <asm/host_ops.h>


static unsigned long long boot_time;

#ifdef CONFIG_LKL_X86_PVCLOCK
#include <asm/x86-clock.h>
static unsigned long long lkl_time_read(void)
{
	return lkl_clock_read_counter();
}
#else
static unsigned long long lkl_time_read(void)
{
	return lkl_ops->time();
}
#endif

void __ndelay(unsigned long nsecs)
{
	unsigned long long start = lkl_time_read();

	while (lkl_time_read() < start + nsecs)
		cpu_relax();
}

void __udelay(unsigned long usecs)
{
	__ndelay(usecs * NSEC_PER_USEC);
}

void __const_udelay(unsigned long xloops)
{
	__udelay(xloops / 0x10c7ul);
}

void calibrate_delay(void)
{
}

void read_persistent_clock64(struct timespec64 *ts)
{
#ifdef CONFIG_LKL_X86_PVCLOCK
	struct lkl_clock_bootstrap sample;

	lkl_arch_clock_seed(&sample);
	*ts = ns_to_timespec64(sample.real_ns);
#else
	*ts = ns_to_timespec64(lkl_time_read());
#endif
}

/*
 * Scheduler clock - returns current time in nanosec units.
 *
 */
unsigned long long sched_clock(void)
{
	if (!boot_time)
		return 0;

	return lkl_time_read() - boot_time;
}

static u64 clock_read(struct clocksource *cs)
{
	return lkl_time_read();
}

static struct clocksource clocksource = {
#ifdef CONFIG_LKL_X86_PVCLOCK
	.name = "lkl-pvclock",
#else
	.name = "lkl",
#endif
	.rating = 499,
	.read	= clock_read,
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
	.mask	= CLOCKSOURCE_MASK(64),
};

#ifdef CONFIG_LKL_X86_PVCLOCK
/* Select the hardware source before the first timekeeper snapshot. */
struct clocksource * __init clocksource_default_clock(void)
{
	static bool configured;

	if (!configured) {
		__clocksource_update_freq_hz(&clocksource, NSEC_PER_SEC);
		configured = true;
	}
	return &clocksource;
}
#endif

static void *timer;

static int timer_irq;

static void timer_fn(void)
{
	lkl_trigger_irq(timer_irq);
}

static int clockevent_set_state_shutdown(struct clock_event_device *evt)
{
	if (timer) {
		lkl_ops->timer_free(timer);
		timer = NULL;
	}

	return 0;
}

static int clockevent_set_state_oneshot(struct clock_event_device *evt)
{
	timer = lkl_ops->timer_alloc(timer_fn);
	if (!timer)
		return -ENOMEM;

	return 0;
}

static irqreturn_t timer_irq_handler(int irq, void *dev_id)
{
	struct clock_event_device *dev = (struct clock_event_device *)dev_id;

	dev->event_handler(dev);

	return IRQ_HANDLED;
}

static int clockevent_next_event(unsigned long ns,
				 struct clock_event_device *evt)
{
	return lkl_ops->timer_set_oneshot(timer, ns);
}

static struct clock_event_device clockevent = {
	.name			= "lkl",
	.features		= CLOCK_EVT_FEAT_ONESHOT,
	.set_state_oneshot	= clockevent_set_state_oneshot,
	.set_next_event		= clockevent_next_event,
	.set_state_shutdown	= clockevent_set_state_shutdown,
};

void __init time_init(void)
{
	int ret;
	unsigned long timer_irq_flags = IRQF_NOBALANCING | IRQF_TIMER;

	if (!lkl_ops->timer_alloc || !lkl_ops->timer_free ||
	    !lkl_ops->timer_set_oneshot ||
	    (!IS_ENABLED(CONFIG_LKL_X86_PVCLOCK) && !lkl_ops->time)) {
		pr_err("lkl: no time or timer support provided by host\n");
		return;
	}

	timer_irq = lkl_get_free_irq("timer");
	ret = request_irq(timer_irq, timer_irq_handler, timer_irq_flags,
			  "timer", &clockevent);
	if (ret)
		pr_err("lkl: failed to request timer irq %u\n", timer_irq);

	ret = clocksource_register_khz(&clocksource, 1000000);
	if (ret)
		pr_err("lkl: unable to register clocksource\n");

	clockevents_config_and_register(&clockevent, NSEC_PER_SEC, 1, ULONG_MAX);

	boot_time = lkl_time_read();
	pr_info("lkl: time and timers initialized (irq%d)\n", timer_irq);
}

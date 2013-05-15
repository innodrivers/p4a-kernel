/*
 * linux/arch/arm/mach-p4a/time.c
 *
 * Copyright (c) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clockchips.h>
#include <asm/mach/time.h>		//struct sys_timer
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/clocksource.h>
#include <mach/hardware.h>
#include <mach/p4a-regs.h>

/* ---------------P4 Timer Registers----------------- */
#define P4T_M0R		0x00		/* Timer Match Register 0 */
#define P4T_M1R		0x04		/* Timer Match Register 1 */
#define P4T_CTLR	0x08		/* Timer Control Register */
#define P4T_CNTR	0x0c		/* Timer Counter Register */
#define P4T_ISR		0x10		/* Timer Interrupt Status Register */
#define P4T_ICR		0x14		/* Timer Interrupt Clear Register */
#define P4T_IER		0x18		/* Timer Interrupt Enable Register */

/* P4T_CTLR register bitfields */
#define P4T_CTRL_EN		(0x1<<0)
#define P4T_CTRL_RSTMR0	(0x1<<1)
#define P4T_CTRL_RSTMR1	(0x1<<2)
#define P4T_CTRL_CLRCNT	(0x1<<3)

/* P4T_IER register bitfields */
#define P4T_IER_MR0		(0x1<<0)
#define P4T_IER_MR1		(0x1<<1)
/* P4T_ICR register bitfields */
#define P4T_ICR_MR0		(0x1<<0)
#define P4T_ICR_MR1		(0x1<<1)

/* --------------- Timer1/2 Registers----------------- */
#define TM0R		0x00
#define TM1R		0x04
#define TCR			0x08
#define TCNTR		0x0c

/* TCR register bitfield */
#define TCR_EN		(0x1<<0)
#define TCR_RSTMR0	(0x1<<1)
#define TCR_RSTMR1	(0x1<<2)
#define TCR_CLRCNT	(0x1<<3)

/*--------------------------------------------------------*/
#define P4TIMER_REG(off)		(P4A_P4TIMER_BASE + (off))
#define TIMER2_REG(off)			(P4A_TIMER2_BASE + (off))

static inline void p4a_timer_writel(int offset, unsigned long val)
{
	writel(val, P4TIMER_REG(offset));
}

static inline unsigned long p4a_timer_readl(int offset)
{
	return readl(P4TIMER_REG(offset));
}

static inline void timer2_writel(int offset, unsigned long val)
{
	writel(val, TIMER2_REG(offset));
}

static inline unsigned long timer2_readl(int offset)
{
	return readl(TIMER2_REG(offset));
}

/*
 * IRQ handler for the timer.
 */
static irqreturn_t timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;
	volatile unsigned int val = 0;

	val = p4a_timer_readl(P4T_ISR);		// clear interrupt

	p4a_timer_writel(P4T_ICR, P4T_ICR_MR0);
	
	if (evt->event_handler)
		evt->event_handler(evt);

	return IRQ_HANDLED;
}

static void p4a_clkevt_mode(enum clock_event_mode mode, struct clock_event_device *evt)
{
	switch (mode) {
	case CLOCK_EVT_MODE_PERIODIC:
		p4a_timer_writel(P4T_CTLR, ~P4T_CTRL_EN);
		p4a_timer_writel(P4T_M0R, CLOCK_TICK_RATE/HZ);
		p4a_timer_writel(P4T_CTLR, (P4T_CTRL_CLRCNT | P4T_CTRL_RSTMR0 | P4T_CTRL_EN));
		
		p4a_timer_writel(P4T_IER, P4T_IER_MR0);	// enable MR0
		break;

	case CLOCK_EVT_MODE_ONESHOT:
		break;

	case CLOCK_EVT_MODE_UNUSED:
	case CLOCK_EVT_MODE_SHUTDOWN:
		p4a_timer_writel(P4T_CTLR, ~P4T_CTRL_EN);
		break;

	case CLOCK_EVT_MODE_RESUME:
		break;

	default:
		break;
	}
}

static int p4a_clkevt_next(unsigned long cycles, struct clock_event_device* evt)
{
	// writing the value has immediate effect
	p4a_timer_writel(P4T_CTLR, ~P4T_CTRL_EN);
	p4a_timer_writel(P4T_M0R, cycles);
	p4a_timer_writel(P4T_CTLR, (P4T_CTRL_CLRCNT | P4T_CTRL_EN));

	return 0;
}

static struct clock_event_device p4a_clkevt = {
	.name		= "P4 timer",
	.features	= CLOCK_EVT_FEAT_PERIODIC,
	.shift		= 10,
	.rating		= 200,
	.set_mode	= p4a_clkevt_mode,
	.set_next_event = p4a_clkevt_next,
};

static cycle_t p4a_timer2_read(struct clocksource *cs)
{
	return timer2_readl(TCNTR);
}

static struct clocksource p4a_clksrc = {
	.name	= "timer2",
	.rating	= 200,
	.read	= p4a_timer2_read,
	.mask	= CLOCKSOURCE_MASK(32),
	.shift	= 10,
	.flags	= CLOCK_SOURCE_IS_CONTINUOUS,
};

static struct irqaction p4a_timer_irqaction = {
	.name		= "P4A Timer Tick",
	.flags		= IRQF_DISABLED | IRQF_TIMER,
	.handler	= timer_interrupt,
	.dev_id		= &p4a_clkevt,
};

static void __init p4a_systimer_init(void)
{
	struct clk* clk;
	unsigned long rate;

	clk = clk_get_sys(NULL, "P4TIMER_CLK");
	BUG_ON(IS_ERR(clk));
	clk_enable(clk);
	rate = clk_get_rate(clk);

	WARN(rate != CLOCK_TICK_RATE, "Actual P4 Timer rate %ld, other than %d\n", \
			rate, CLOCK_TICK_RATE);

	/* disable timer for init */
	p4a_timer_writel(P4T_CTLR, 0);
	p4a_timer_writel(P4T_IER, 0);
	p4a_timer_readl(P4T_ISR);
	p4a_timer_writel(P4T_ICR, ~0x0);

	p4a_clkevt.mult = div_sc(CLOCK_TICK_RATE, NSEC_PER_SEC, p4a_clkevt.shift);
	p4a_clkevt.max_delta_ns = clockevent_delta2ns(0xffffffff, &p4a_clkevt);
	p4a_clkevt.min_delta_ns = clockevent_delta2ns(0xf, &p4a_clkevt);
	p4a_clkevt.cpumask = cpumask_of(0);

	// Register irq and clockevents
	if (setup_irq(IRQ_TIMER_P4, &p4a_timer_irqaction)) {
		printk(KERN_ERR "Failed to reigster timer IRQ!\n");
		BUG();
	}

	clockevents_register_device(&p4a_clkevt);

	// timer2 is the free running clocksource
	clk = clk_get_sys(NULL, "TIMER_CLK");
	BUG_ON(IS_ERR(clk));
	clk_enable(clk);
	rate = clk_get_rate(clk);

	timer2_writel(TCR, TCR_CLRCNT);	// Clear time counter
	timer2_writel(TM0R, 0);

	p4a_clksrc.mult = clocksource_hz2mult(rate, p4a_clksrc.shift); 
	if (clocksource_register(&p4a_clksrc)) {
		printk(KERN_ERR "Failed to register clocksource!\n");
		BUG();
	}
	timer2_writel(TCR, TCR_EN);	//Enable
}

struct sys_timer p4a_timer = {
	.init		= p4a_systimer_init,
};


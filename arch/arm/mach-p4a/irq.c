/*
 * linux/arch/arm/mach-p4a/irq.c
 *
 *  Copyright (C) 2013 Innofidei Inc.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/io.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <mach/hardware.h>
#include <mach/p4a-regs.h>

/* AIC Registers */
#define	INT_RAW			(0x00)
#define INT_MASK		(0x04)
#define INT_VALID		(0x08)
#define FIQ_IRQ_SEL		(0x0c)
#define FIQ_VECTOR		(0x10)
#define IRQ_VECTOR		(0x14)
#define FIQ_SEL_P(x)	(0x18 + ((x)<<2))		// x range from 0 to 3.
#define FIQ_ENABLE		(0x28)
#define IRQ_ENABLE		(0x2c)
#define IRQ_SEL_P(x)	(0x40 + ((x)<<2))		// x range from 0 to 31


/* Peripheral IRQ Control */
#define PERI_REG(off)			(P4A_PERI_BASE + (off))

#define COMM_INT_STATUS_REG		PERI_REG(0x23<<2)
#define COMM_INT_CLEAR_REG		PERI_REG(0x24<<2)
#ifdef CONFIG_P4A_CPU1
#define COMM_INT_ENABLE_REG		PERI_REG(0x25<<2)
#elif defined(CONFIG_P4A_CPU2)
#define COMM_INT_ENABLE_REG		PERI_REG(0x28<<2)
#endif

#define TIMER_INT_STATUS_REG	PERI_REG(0x20<<2)
#define TIMER_INT_CLEAR_REG		PERI_REG(0x21<<2)
#ifdef CONFIG_P4A_CPU1
#define TIMER_INT_ENABLE_REG	PERI_REG(0x22<<2)
#elif defined(CONFIG_P4A_CPU2)
#define TIMER_INT_ENABLE_REG	PERI_REG(0x27<<2)
#endif


/*
 * p4a timer irq
 */
static void p4a_timer_demux_handler(unsigned int irq, struct irq_desc* desc)
{
	unsigned int status;

	status = readl(TIMER_INT_STATUS_REG) & readl(TIMER_INT_ENABLE_REG);

	if(status) {
		irq = IRQ_TIMER_BASE;
		desc = irq_desc + irq;
		do {
			if(status & 1)
			  desc->handle_irq(irq, desc);
			irq++;
			desc++;
			status >>= 1;
		} while(status);
	}
}

static void p4a_timer_ack_irq(unsigned int irq)
{
	unsigned int status;

	status = readl(TIMER_INT_CLEAR_REG);
	status |= (1 << (irq - IRQ_TIMER_BASE));
	writel(status, TIMER_INT_CLEAR_REG);
}

static void p4a_timer_mask_irq(unsigned int irq)
{
	unsigned int status;

	status = readl(TIMER_INT_ENABLE_REG);
	status &= ~(1 << (irq - IRQ_TIMER_BASE));
	writel(status, TIMER_INT_ENABLE_REG);
}

static void p4a_timer_umask_irq(unsigned int irq)
{
	unsigned int status;

	status = readl(TIMER_INT_ENABLE_REG);
	status |= (1 << (irq - IRQ_TIMER_BASE));
	writel(status, TIMER_INT_ENABLE_REG);
}

static struct irq_chip p4a_timer_chip = {
	.name = "TIMER",
	.ack = p4a_timer_ack_irq,
	.mask = p4a_timer_mask_irq,
	.unmask = p4a_timer_umask_irq,
};

static void __init p4a_timer_irq_setup(void)
{
	int irq;

	writel(0, TIMER_INT_ENABLE_REG); /* diable all timer interrupts */
#ifdef CONFIG_P4A_CPU1
	writel(~0x0, TIMER_INT_CLEAR_REG); /* clear status on all timers */
#endif

	for (irq = IRQ_TIMER_BASE; irq < (IRQ_TIMER_BASE + NR_IRQS_TIMER); irq++) {
		set_irq_chip(irq, &p4a_timer_chip);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}

	set_irq_chained_handler(IRQ_TIMER, p4a_timer_demux_handler);
}

/*
 * p4a peripheral common irq
 */
static void p4a_peri_comm_demux_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned int status;
	unsigned long flags;
	
	local_irq_save(flags);

	status = readl(COMM_INT_STATUS_REG) & readl(COMM_INT_ENABLE_REG);
	if ( status ) {
		irq = IRQ_PERI(0);
		desc = irq_desc + irq;
		do {
			if ( status & 1 )
				desc->handle_irq(irq, desc);
			irq++;
			desc++;
			status >>= 1;
		} while(status);
	}

	local_irq_restore(flags);
}

static void p4a_peri_comm_ack_irq(unsigned int irq)
{
	unsigned int status;
	unsigned long flags;
	
	local_irq_save(flags);

	status = readl(COMM_INT_CLEAR_REG);
	status |= (1 << (irq - IRQ_PERI(0)));
	writel( status, COMM_INT_CLEAR_REG);

	local_irq_restore(flags);
}

static void p4a_peri_comm_mask_irq(unsigned int irq)
{
	unsigned int status;
	unsigned long flags;
	
	local_irq_save(flags);

	status = readl(COMM_INT_ENABLE_REG);
	status &= ~(1 << (irq - IRQ_PERI(0)));
	writel( status , COMM_INT_ENABLE_REG);

	local_irq_restore(flags);
}

static void p4a_peri_comm_unmask_irq(unsigned int irq)
{
	unsigned int status;
	unsigned long flags;

	local_irq_save(flags);

	status = readl(COMM_INT_ENABLE_REG);
	status |= (1 << (irq - IRQ_PERI(0)));
	writel( status, COMM_INT_ENABLE_REG);

	local_irq_restore(flags);
}

static struct irq_chip p4a_peri_comm_chip = {
	.name	= "PERIPHERAL",
	.ack	= p4a_peri_comm_ack_irq,
	.mask	= p4a_peri_comm_mask_irq,
	.unmask	= p4a_peri_comm_unmask_irq,
};


static void __init p4a_peri_comm_irq_setup(void)
{
	int i;
	unsigned int irq;

	writel(0, COMM_INT_ENABLE_REG);
#ifdef CONFIG_P4A_CPU1
	writel(0xFFFFFFFF, COMM_INT_CLEAR_REG);
#endif

	for (i = 0; i < NR_IRQS_PERI; i++ ) {
		irq = IRQ_PERI(i);

		set_irq_chip(irq, &p4a_peri_comm_chip);
		set_irq_handler(irq, handle_level_irq);
		set_irq_flags(irq, IRQF_VALID);
	}
	
	/* Install handler for perial edge detect interrupts */
	set_irq_chained_handler(IRQ_PERI_COMM, p4a_peri_comm_demux_handler);
}


static void p4a_aic_ack_irq(unsigned int irq)
{
}

static void p4a_aic_mask_irq(unsigned int irq)
{
	unsigned long val;
	u32 mask_bit = (1 << (irq & 0x1f));
	void __iomem * aic_base = get_irq_chip_data(irq);
	
	val = readl(aic_base + INT_MASK);
	/* disable interrupt on AIC */
	writel((val | mask_bit), aic_base + INT_MASK);
}

static void p4a_aic_unmask_irq(unsigned int irq)
{
	unsigned long val;
	u32 mask_bit = (1 << (irq & 0x1f));
	void __iomem * aic_base = get_irq_chip_data(irq);

	val = readl(aic_base + INT_MASK);
	/* enable interrupt on AIC */
	writel((val & ~mask_bit), aic_base + INT_MASK);
}


static struct irq_chip p4a_aic_chip = {
	.name	= "AIC",
	.ack	= p4a_aic_ack_irq,
	.mask	= p4a_aic_mask_irq,
	.unmask	= p4a_aic_unmask_irq,
};


static void aic_reset(void __iomem* aic_base)
{
	int i;

	/* reset AIC */
	writel(0xFFFFFFFF, aic_base + INT_MASK);
	writel(0x0, aic_base + FIQ_IRQ_SEL);	/* every interrupt source generate IRQ interrupt to ARM core.*/
	writel(0x0, aic_base + FIQ_ENABLE);		/* disable FIQ interrupt sources */
	writel(0x0, aic_base + IRQ_ENABLE);		/* disable IRQ interrupt sources */

	for (i=0; i<4; i++)
		writel(0x0, aic_base + FIQ_SEL_P(i));

	for (i=0; i<32; i++)
		writel(0x0, aic_base + IRQ_SEL_P(i));

}

/*
 * Initialize the AIC interrupt controller.
 */
static void __init p4a_aic_init(void __iomem* aic_base, unsigned int irq_start, unsigned int num_irqs)
{
	unsigned int i;
	unsigned long aiier = 0;

	aic_reset(aic_base);

	/* configure interrupt source */
	for (i=0; i<num_irqs; i++) {
		set_irq_chip_and_handler(irq_start + i, &p4a_aic_chip, handle_level_irq);
		set_irq_chip_data(irq_start + i, (void*)aic_base);
		set_irq_flags(irq_start + i, IRQF_VALID);

		writel(i, aic_base + IRQ_SEL_P(i));

		aiier |= (1<<i);
	}


	writel(aiier, aic_base + IRQ_ENABLE);	/* enable interrupt source */
}



void __init p4a_irq_init(void)
{
	p4a_aic_init(P4A_AIC_BASE, AIC_IRQ(0), NR_AIC_IRQS);

	/* setup peripheral common irq */
	p4a_peri_comm_irq_setup();

	/* setup timer irq */
	p4a_timer_irq_setup();
}

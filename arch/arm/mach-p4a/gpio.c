/*
 * linux/arch/arm/mach-p4a/gpio.c
 *
 * Copyright (C) 2013 Innofidei Inc.
 * 
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/dma-mapping.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <asm/irq.h>
#include <asm/mach/irq.h>
#include <mach/irqs.h>
#include <asm/gpio.h>

#include <mach/p4a-regs.h>

/*--------------------------------------------------------*/
/* GPIO Bank1 registers */
#define GPIO1_DIR				0x40
#define GPIO1_INPUT				0x44
#define GPIO1_OUTPUT_SET		0x48
#define GPIO1_OUTPUT_CLR		0x4c
#define GPIO1_OUTPUT			0x50

#define GPIO1_EDGE_SEL			0x00
#ifdef CONFIG_P4A_CPU2
#define GPIO1_INT_EN			0x10
#elif defined(CONFIG_P4A_CPU1)
#define GPIO1_INT_EN			0x04
#endif
#define GPIO1_INT_CLR			0x08
#define GPIO1_INT_STATUS		0x0c


/* GPIO Bank2-6 Registers */
#define GPIO_DIR		0x00
#define GPIO_INPUT		0x04
#define GPIO_OUTPUT_SET	0x08
#define GPIO_OUTPUT_CLR	0x0c
#define GPIO_OUTPUT		0x10

#define GPIO_EDGE_SEL	0x14
#ifdef CONFIG_P4A_CPU1
#define GPIO_INT_EN		0x18
#elif defined(CONFIG_P4A_CPU2)
#define GPIO_INT_EN		0x24
#endif
#define GPIO_INT_CLR	0x1c
#define GPIO_INT_STATUS	0x20

/*----------------------------------------------------*/

#define GPIO_bit(x)	(1 << ((x) & 0x1f))

struct p4a_gpio_reg {
	u8		dir;
	u8		input_level;
	u8		output_set;
	u8		output_clear;
	u8		output_level;
	u8		edge_sel;
	u8		int_enable;
	u8		int_clear;
	u8		int_status;
};

struct p4a_gpio_chip {
	struct gpio_chip chip;
	void __iomem *regbase;
	struct p4a_gpio_reg	*regoff;
};

static struct p4a_gpio_reg p4a_gpio1_reg = {
	.dir			= GPIO1_DIR,
	.input_level	= GPIO1_INPUT,
	.output_set		= GPIO1_OUTPUT_SET,
	.output_clear	= GPIO1_OUTPUT_CLR,
	.output_level	= GPIO1_OUTPUT,
	.edge_sel		= GPIO1_EDGE_SEL,
	.int_enable		= GPIO1_INT_EN,
	.int_clear		= GPIO1_INT_CLR,
	.int_status		= GPIO1_INT_STATUS,
};

static struct p4a_gpio_reg p4a_gpio2_6_reg = {
	.dir			= GPIO_DIR,
	.input_level	= GPIO_INPUT,
	.output_set		= GPIO_OUTPUT_SET,
	.output_clear	= GPIO_OUTPUT_CLR,
	.output_level	= GPIO_OUTPUT,
	.edge_sel		= GPIO_EDGE_SEL,
	.int_enable		= GPIO_INT_EN,
	.int_clear		= GPIO_INT_CLR,
	.int_status		= GPIO_INT_STATUS,
};

static int p4a_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
	struct p4a_gpio_chip *p4a;
	void __iomem* reg;
	unsigned long flags;

	u32 mask = (1<<offset);
	u32 tmp;

	p4a = container_of(chip, struct p4a_gpio_chip, chip);

	local_irq_save(flags);

	reg = p4a->regbase + p4a->regoff->dir;
	tmp = readl(reg);
	tmp |= mask;
	writel(tmp, reg);

	local_irq_restore(flags);

	return 0;
}

static int p4a_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
	struct p4a_gpio_chip *p4a;
	void __iomem *reg;

	unsigned long flags;
	u32 mask = (1<<offset);
	u32 tmp;

	p4a = container_of(chip, struct p4a_gpio_chip, chip);

	local_irq_save(flags);

	//set direction output
	reg = p4a->regbase + p4a->regoff->dir;
	tmp = readl(reg);
	tmp &= ~mask;
	writel(tmp, reg);

	//set output level
	if (value == 0)
		reg = p4a->regbase + p4a->regoff->output_clear;
	else
		reg = p4a->regbase + p4a->regoff->output_set;
	
	tmp = readl(reg);
	tmp |= mask;
	writel(tmp, reg);

	local_irq_restore(flags);

	return 0;
}

static int p4a_gpio_get(struct gpio_chip *chip, unsigned offset)
{
	struct p4a_gpio_chip *p4a;
	u32 mask = 1 << offset;
	void __iomem* reg;
	
	p4a = container_of(chip, struct p4a_gpio_chip, chip);
	
	reg = p4a->regbase + p4a->regoff->dir;
	if (readl(reg) & mask)	//pin is input
		reg = p4a->regbase + p4a->regoff->input_level;
	else
		reg = p4a->regbase + p4a->regoff->output_level;

	return !!(readl(reg) & mask);
}

static void p4a_gpio_set(struct gpio_chip *chip, unsigned offset, int value)
{
	struct p4a_gpio_chip *p4a;
	unsigned long flags;
	u32 mask = 1 << offset;
	u32 tmp;
	volatile void __iomem *addr;
	
	p4a = container_of(chip, struct p4a_gpio_chip, chip);

	local_irq_save(flags);
	if (value)
		addr = p4a->regbase + p4a->regoff->output_set;
	else
		addr = p4a->regbase + p4a->regoff->output_clear;

	tmp = readl(addr);
	writel((tmp | mask), addr);

	local_irq_restore(flags);
}

static struct p4a_gpio_chip p4a_gpio_chips[] = {
	[0] = {
		.regbase = P4A_GPIO1_BASE,
		.regoff	= &p4a_gpio1_reg,
		.chip = {
			.label = "gpio-1",
			.direction_input = p4a_gpio_direction_input,
			.direction_output = p4a_gpio_direction_output,
			.get = p4a_gpio_get,
			.set = p4a_gpio_set,
			.base = 0,
			.ngpio = 32,
		},
	},
	[1] = {
		.regbase = P4A_GPIO2_BASE,
		.regoff	= &p4a_gpio2_6_reg,
		.chip = {
			.label = "gpio-2",
			.direction_input = p4a_gpio_direction_input,
			.direction_output = p4a_gpio_direction_output,
			.get = p4a_gpio_get,
			.set = p4a_gpio_set,
			.base = 32,
			.ngpio = 32,
		},
	},
	[2] = {
		.regbase = P4A_GPIO3_BASE,
		.regoff	= &p4a_gpio2_6_reg,
		.chip = {
			.label = "gpio-3",
			.direction_input = p4a_gpio_direction_input,
			.direction_output = p4a_gpio_direction_output,
			.get = p4a_gpio_get,
			.set = p4a_gpio_set,
			.base = 64,
			.ngpio = 32,
		},
	},
	[3] = {
		.regbase = P4A_GPIO4_BASE,
		.regoff	= &p4a_gpio2_6_reg,
		.chip = {
			.label = "gpio-4",
			.direction_input = p4a_gpio_direction_input,
			.direction_output = p4a_gpio_direction_output,
			.get = p4a_gpio_get,
			.set = p4a_gpio_set,
			.base = 96,
			.ngpio = 32,
		},
	},
	[4] = {
		.regbase = P4A_GPIO5_BASE,
		.regoff	= &p4a_gpio2_6_reg,
		.chip = {
			.label = "gpio-5",
			.direction_input = p4a_gpio_direction_input,
			.direction_output = p4a_gpio_direction_output,
			.get = p4a_gpio_get,
			.set = p4a_gpio_set,
			.base = 128,
			.ngpio = 32,
		},
	},
	[5] = {
		.regbase = P4A_GPIO6_BASE,
		.regoff	= &p4a_gpio2_6_reg,
		.chip = {
			.label = "gpio-6",
			.direction_input = p4a_gpio_direction_input,
			.direction_output = p4a_gpio_direction_output,
			.get = p4a_gpio_get,
			.set = p4a_gpio_set,
			.base = 160,
			.ngpio = 32,
		},
	},
};
#define P4A_GPIO_CHIP_NUM		ARRAY_SIZE(p4a_gpio_chips)

static void p4a_gpio_irq_handler(unsigned int irq, struct irq_desc *desc)
{
	unsigned long flags;
	unsigned long status;
	struct p4a_gpio_chip * gpio_chip;
	int i;

	local_irq_save(flags);

	for (i=0; i<P4A_GPIO_CHIP_NUM; i++) {
		gpio_chip = &p4a_gpio_chips[i]; 

		status = readl(gpio_chip->regbase + gpio_chip->regoff->int_status) & 
					readl(gpio_chip->regbase + gpio_chip->regoff->int_enable);
		if (status) {
			irq = GPIO_TO_IRQ(gpio_chip->chip.base);
			desc = irq_desc + irq; 
			do {
				if (status & 1)
					desc->handle_irq(irq, desc);
				irq++;
				desc++;
				status >>= 1;
			} while (status);
		}
	}

	local_irq_restore(flags);
}

static void p4a_gpio_ack_irq(unsigned int irq)
{
	unsigned long flags;
	void __iomem* addr;
	struct p4a_gpio_chip* gpio_chip = get_irq_chip_data(irq);
	int gpio = irq - GPIO_TO_IRQ(gpio_chip->chip.base);
	
	local_irq_save(flags);

	addr = gpio_chip->regbase + gpio_chip->regoff->int_clear;

	writel( GPIO_bit(gpio), addr);

	local_irq_restore(flags);
}

static void p4a_gpio_mask_irq(unsigned int irq)
{
	unsigned long flags;
	void __iomem* addr;
	struct p4a_gpio_chip* gpio_chip = get_irq_chip_data(irq);
	int gpio = irq - GPIO_TO_IRQ(gpio_chip->chip.base);
	
	local_irq_save(flags);

	addr = gpio_chip->regbase + gpio_chip->regoff->int_enable;

	writel( readl(addr) & ~GPIO_bit(gpio), addr);

	local_irq_restore(flags);
}

static void p4a_gpio_unmask_irq(unsigned int irq)
{
	unsigned long flags;
	void __iomem* addr;
	struct p4a_gpio_chip* gpio_chip = get_irq_chip_data(irq);
	int gpio;
	
	gpio = irq - GPIO_TO_IRQ(gpio_chip->chip.base);

	local_irq_save(flags);

	addr = gpio_chip->regbase + gpio_chip->regoff->int_enable;

	writel( readl(addr) | GPIO_bit(gpio), addr);

	local_irq_restore(flags);
}

static int p4a_gpio_set_type_irq(unsigned int irq, unsigned int type)
{
	unsigned long flags;
	struct p4a_gpio_chip* gpio_chip = get_irq_chip_data(irq);
	int gpio = irq - GPIO_TO_IRQ(gpio_chip->chip.base);
	void __iomem* addr;
	u32 val;

	local_irq_save(flags);

	addr = gpio_chip->regbase + gpio_chip->regoff->edge_sel;

	val = readl(addr);

	/* P4A not support EDGE_BOTH */
	if (type & IRQ_TYPE_EDGE_RISING)
		val |= GPIO_bit(gpio);
	else if (type & IRQ_TYPE_EDGE_FALLING)
		val &= ~GPIO_bit(gpio);
	
	writel(val, addr);

	local_irq_restore(flags);

	return 0;
}

static struct irq_chip p4a_gpio_irqchip = {
	.name	= "GPIO",
	.ack	= p4a_gpio_ack_irq,
	.mask	= p4a_gpio_mask_irq,
	.unmask	= p4a_gpio_unmask_irq,
	.set_type	= p4a_gpio_set_type_irq,
};

static void __init p4a_gpio_irq_init(struct p4a_gpio_chip *gpio_chip)
{
	int i;
	int irq;
	int gpio, num;

	gpio = gpio_chip->chip.base;
	num = gpio_chip->chip.ngpio;

	for (i = 0; i < num; i++, gpio++) {
		irq = GPIO_TO_IRQ(gpio);

		set_irq_chip_and_handler(irq, &p4a_gpio_irqchip, handle_edge_irq);
		set_irq_chip_data(irq, gpio_chip);
		set_irq_flags(irq, IRQF_VALID);
	}

}

void __init p4a_gpio_init(void)
{
	int i;
	int num_chips = P4A_GPIO_CHIP_NUM;

	for (i=0; i<num_chips; i++) {
		struct p4a_gpio_chip *gpio_chip = &p4a_gpio_chips[i]; 

		/* first disable GPIO interrupts */
		writel(0x0, gpio_chip->regbase + gpio_chip->regoff->int_enable);
#ifdef CONFIG_P4A_CPU1
		writel(0xffffffff, gpio_chip->regbase + gpio_chip->regoff->int_clear);
#endif

		gpiochip_add(&gpio_chip->chip);

		p4a_gpio_irq_init(gpio_chip);
	}

	set_irq_chained_handler(IRQ_GPIO, p4a_gpio_irq_handler);
}

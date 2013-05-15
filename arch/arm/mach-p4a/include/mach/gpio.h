/*
 * include/asm-arm/arch-p4a/gpio.h
 *
 *  Copyright (C) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#ifndef __ASM_ARCH_P4A_GPIO_H
#define __ASM_ARCH_P4A_GPIO_H

#include <asm/irq.h>
#include <asm/errno.h>

#define ARCH_NR_GPIOS	(32 * 6)

#include <asm-generic/gpio.h>

static inline int gpio_get_value(unsigned gpio)
{
	return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	__gpio_set_value(gpio, value);
}

static inline int gpio_cansleep(unsigned gpio)
{
	return __gpio_cansleep(gpio);
}

static inline int gpio_to_irq(unsigned gpio)
{
	return GPIO_TO_IRQ(gpio);
}

static inline int irq_to_gpio(unsigned irq)
{
	int tmp;

	tmp = IRQ_TO_GPIO(irq);
	if (tmp >= 0 && tmp < ARCH_NR_GPIOS)
		return tmp;

	return -EIO;
}


#endif	/* __ASM_ARCH_P4A_GPIO_H */

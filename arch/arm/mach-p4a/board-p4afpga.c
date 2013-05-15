/*
 * arch/arm/mach-p4a/p4afpga.c
 *
 * Innofidei P4A FPGA board
 *
 * (C) Copyright 2013, Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/irq.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include <linux/gpio.h>
#include "generic.h"

#define SZ_M(x)		((x) << 20)
static struct mtd_partition p4afpga_nand_partitions[] = {
	{
		.name	= "kernel",
		.offset	= SZ_M(32),
		.size	= SZ_M(6),
	},
	{
		.name	= "rootfs",
		.offset	= SZ_M(38),
		.size	= SZ_M(16),
	},
	{	/* overlay with kernel and rootfs */
		.name	= "firmware",
		.offset	= SZ_M(32),
		.size	= SZ_M(22),
	},
	{
		.name	= "recovery",
		.offset	= SZ_M(52),
		.size	= SZ_M(8),
	},
};

static struct p4a_nand_platdata p4afpga_nand_data = {
	.partitions	= p4afpga_nand_partitions,
	.nr_parts	= ARRAY_SIZE(p4afpga_nand_partitions),
};

static struct p4a_mbnand_platdata p4afpga_mbnand_data = {
	.partitions	= p4afpga_nand_partitions,
	.nr_parts	= ARRAY_SIZE(p4afpga_nand_partitions),
};

static void __init p4afpga_nand_init(void)
{
	p4a_nand_init(&p4afpga_nand_data);
	p4a_mbnand_init(&p4afpga_mbnand_data);
}

static void p4afpga_init_irq(void)
{
	p4a_clk_init();
	p4a_irq_init();
	p4a_gpio_init();
}

static void board_p4afpga_init(void)
{
	p4a_devices_init();

	/* UART devices */
	p4a_serial_init(0);
	p4a_serial_init(1);
	p4a_serial_init(2);

	p4afpga_nand_init();
}


MACHINE_START(P4AFPGA, "p4afpga")	// Innofidei P4A FPGA Board
	/* Maintainer : Innofidei Inc. */
	.boot_params	= PHYS_OFFSET + 0x100,
	.map_io			= p4a_map_io,
	.timer			= &p4a_timer,
	.init_irq		= p4afpga_init_irq,
	.init_machine	= board_p4afpga_init,
MACHINE_END


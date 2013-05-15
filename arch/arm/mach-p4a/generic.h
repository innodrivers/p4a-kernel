/*
 * linux/arch/arm/mach-p4a/generic.h
 *
 *  Copyright (c) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <mach/p4a_nand.h>
#include <mach/p4a_mbnand.h>
#include <mach/p4a_ether.h>
#include <mach/iomux.h>
#include <linux/gpio.h>
#include <linux/i2c.h>

extern struct sys_timer p4a_timer;
extern void __init p4a_map_io(void);
extern void __init p4a_clk_init(void);
extern void __init p4a_irq_init(void);
extern void __init p4a_gpio_init(void);
extern void __init p4a_devices_init(void);

extern int __init p4a_usb_musb_init(int id);
extern int __init p4a_mmc_init(int id);
extern int __init p4a_serial_init(int id);
extern int __init p4a_nand_init(struct p4a_nand_platdata* pdata);
extern int __init p4a_mbnand_init(struct p4a_mbnand_platdata *data);
extern int __init p4a_ether_init(struct p4a_ether_platdata* data);
extern int __init p4a_register_i2c_bus(int bus_id, u32 clkrate,struct i2c_board_info const *info,unsigned len);

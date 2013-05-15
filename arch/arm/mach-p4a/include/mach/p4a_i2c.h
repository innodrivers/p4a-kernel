/*
 * arch/arm/mach-p4a/include/mach/p4a_i2c.h
 *
 * Copyright (C) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */
#ifndef __P4A_I2C_ADAPTER__
#define __P4A_I2C_ADAPTER__

struct p4a_i2c_platform_data{
	char	clk_name[16];
	u32		clkrate;	/* bus speed in KHz */
};

#endif

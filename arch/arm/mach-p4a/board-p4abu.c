/*
 * arch/arm/mach-p4a/p4abu.c
 *
 * Innofidei P4A bring-up board
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
#include <linux/memblock.h>
#include <linux/delay.h>
#include <asm/mach-types.h>
#include <mach/hardware.h>
#include <asm/mach/arch.h>
#include <asm/mach/map.h>
#include "generic.h"

#define ETH_PHY_RESET_GPIO		(2)

#define SZ_M(x)		((x) << 20)
static struct mtd_partition p4abu_nand_partitions[] = {
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

static struct p4a_nand_platdata p4abu_nand_data = {
	.partitions	= p4abu_nand_partitions,
	.nr_parts	= ARRAY_SIZE(p4abu_nand_partitions),
};

static struct p4a_mbnand_platdata p4abu_mbnand_data = {
	.partitions	= p4abu_nand_partitions,
	.nr_parts	= ARRAY_SIZE(p4abu_nand_partitions),
};

static void p4abu_ether_phy_reset(void)
{
	gpio_set_value(ETH_PHY_RESET_GPIO, 0);
	msleep(50);
	gpio_set_value(ETH_PHY_RESET_GPIO, 1);
}

static struct p4a_ether_platdata p4abu_ether_pdata = {
	.phyaddr	= 0x1,
	.ifmode		= NIBBLE_MODE,
	.hard_reset	= p4abu_ether_phy_reset,
};

static struct i2c_board_info __initdata p4abu_i2c0_devices[] = {
	{
		I2C_BOARD_INFO("sh1106-i2c", 0x3d),		/* SH1106 132x64 Dot Matrix OLED*/
		.platform_data = NULL,
	},
};

static struct i2c_board_info __initdata p4abu_i2c1_devices[] = {
	{
		I2C_BOARD_INFO("hym8563", 0x51),		/* HYM8563 RTC */
		.platform_data = NULL,
	},
};

static void __init p4abu_add_i2c_devices(void)
{
	/* I2C Master 1 */
	p4a_register_i2c_bus(0, 100, p4abu_i2c0_devices, ARRAY_SIZE(p4abu_i2c0_devices));

	/* I2C Master 2 */
	p4a_register_i2c_bus(1, 100, p4abu_i2c1_devices, ARRAY_SIZE(p4abu_i2c1_devices));
}


static p4a_mux_pin_t p4abu_iomux_cfg[] __initdata = {
	/* SDIO Master 1 */
	MP_SDCLK_PAD_SDM1_CLK,
	MP_SDCMD_PAD_SDM1_CMD,
	MP_SDDAT0_PAD_SDM1_DAT0,
	MP_SDDAT1_PAD_SDM1_DAT1,
	MP_SDDAT2_PAD_SDM1_DAT2,
	MP_SDDAT3_PAD_SDM1_DAT3,

	/* Ether MAC */
	MP_USBDAT7_PAD_ETH_RX_D0,
	MP_USBCLK_PAD_ETH_RX_D1,
	MP_USBDIR_PAD_ETH_RX_D2,
	MP_USBNXT_PAD_ETH_RX_D3,
	MP_USBDAT6_PAD_ETH_RX_CLK,
	MP_USBDAT4_PAD_ETH_RX_DV,
	MP_USBDAT5_PAD_ETH_RX_ER,
	MP_SDDAT4_PAD_ETH_TX_D0,
	MP_SDDAT5_PAD_ETH_TX_D1,
	MP_SDDAT6_PAD_ETH_TX_D2,
	MP_SDDAT7_PAD_ETH_TX_D3,
	MP_CLKOUT_PAD_ETH_TX_CLK,
	MP_USBSTP_PAD_ETH_TX_EN,
	MP_USBPHYRSTN_PAD_ETH_TX_ER,
	MP_USBDAT3_PAD_ETH_COL,
	MP_USBDAT2_PAD_ETH_CRS,
	MP_USBDAT1_PAD_ETH_MDIO,
	MP_USBDAT0_PAD_ETH_MDC,

	/* Nand Controller */
	MP_NFCEN0_PAD_NF_CEN0,
	MP_NFRBN0_PAD_NF_RBN0,
	MP_NFCLE_PAD_NF_CLE,
	MP_NFALE_PAD_NF_ALE,
	MP_NFWRN_PAD_NF_WRN,
	MP_NFRDN_PAD_NF_RDN,
	MP_NFD0_PAD_NF_D0,
	MP_NFD1_PAD_NF_D1,
	MP_NFD2_PAD_NF_D2,
	MP_NFD3_PAD_NF_D3,
	MP_NFD4_PAD_NF_D4,
	MP_NFD5_PAD_NF_D5,
	MP_NFD6_PAD_NF_D6,
	MP_NFD7_PAD_NF_D7,

	/* I2C Master 1 */
	MP_I2CSCL_PAD_IICM1_SCL,
	MP_I2CSDA_PAD_IICM1_SDA,

	/* I2C Master 2 */
	MP_SIMSLVCLK_PAD_IICM2_SCL,
	MP_SIMSLVDAT_PAD_IICM2_SDA,

	/* GPIOs */
	MP_GPIO2_PAD_GPIO2,		/* GPIO2 : ether PHY reset */
};

static void board_p4abu_init(void)
{
	p4a_iomux_config(p4abu_iomux_cfg, ARRAY_SIZE(p4abu_iomux_cfg));

	p4a_devices_init();

	/* UART devices */
//	p4a_serial_init(0);		// UART1
	p4a_serial_init(1);		// UART2
	p4a_serial_init(2);		// UART4W

	/* SD memory card or SDIO device */
	p4a_mmc_init(0);	// enable SDIO Master 1
//	p4a_mmc_init(1);	// enable SDIO Master 2 which conficts with "ether MAC" and "ULPI USB"

	/* ether */
	gpio_request_one(ETH_PHY_RESET_GPIO, GPIOF_OUT_INIT_HIGH, "ETH PHY Reset");
	p4a_ether_init(&p4abu_ether_pdata);
	
	/* nand device */
	p4a_nand_init(&p4abu_nand_data);

	/* mailbox nand device */
	p4a_mbnand_init(&p4abu_mbnand_data);

	/* usb device */
//	p4a_usb_musb_init(0);	// USB_ULPI which conficts with "ether MAC" and "SD Master 2"
	p4a_usb_musb_init(1);	// USB_UTMI

	p4abu_add_i2c_devices();
}

static void p4abu_init_irq(void)
{
	p4a_clk_init();
	p4a_irq_init();
	p4a_gpio_init();
}

MACHINE_START(P4ABU, "p4abu")	// Innofidei P4A Bring up Board
	/* Maintainer : Innofidei Inc. */
	.boot_params	= PHYS_OFFSET + 0x100,
	.map_io			= p4a_map_io,
	.timer			= &p4a_timer,
	.init_irq		= p4abu_init_irq,
	.init_machine	= board_p4abu_init,
MACHINE_END


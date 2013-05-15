/*
 * arch/arm/mach-p4a/include/mach/p4a_nand.h
 *
 * Copyright (c) 2013 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __P4A_NAND_H
#define __P4A_NAND_H

#include <linux/mtd/partitions.h>

/* P4A Nand Flash Controller Registers */
#define NFC_CE			0x04
#define NFC_CMDQ_ENTRY	0x08
#define NFC_CMDQ_CTRL	0x0c
#define NFC_RDATA		0x10
#define NFC_CONFIG		0x14
#define NFC_STATUS		0x18
#define NFC_SPARE_ADDR	0x1c
#define NFC_SPARE_DATA	0x20
#define NFC_DMA_ADDR	0x24

/* NFC_CMDQ_CTRL register bits */
#define NCQCR_SET_LOOP(n)		(((n) & 0x3f) > 64 ? (0) : (n) & 0x3f)		//cmd queeu repeat n times
																			//when n=0, cmdq run 64 times, otherwise run n times
#define NCQCR_RESUME	(0x1<<31)

/* NFC_RDATA register bits */
#define NRDR_DATA_VALID		(0x1 << 31)
#define NRDR_DATA_MASK			(0xff)

/* NFC_STATUS register bits */
#define NSR_INT			(0x1<<0)
#define NSR_CMDQ_DONE	(0x1<<1)
#define NSR_FAIL			(0x1<<2)		//program/erase failed
#define NSR_TIMEOUT		(0x1<<3)
#define NSR_ECC_ERR		(0x1<<4)
#define NSR_DEV_RDY		(0x1<<5)
#define NSR_CUR_STATUS_SHIFT	(8)
#define NSR_CUR_STATUS_MASK	(0xff<<8)

struct p4a_nand_platdata {
	struct mtd_partition *partitions;
	int					nr_parts;
	
};

#endif

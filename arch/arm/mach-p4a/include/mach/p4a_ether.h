/*
 *	linux/arch/arm/mach-p4a/include/mach/p4a_ether.h
 *
 *  Copyright (c) 2013	Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __P4A_ETHER_H__
#define __P4A_ETHER_H__

/**
 * PE-MCXMAC Registers
 */
#define MAC_CFG1		(0x00)	// (0x00 << 2)
#define MAC_CFG2		(0x04)	// (0x01 << 2)
#define IPG_IFG			(0x08)	// (0x02 << 2)
#define HALF_DEPLEX		(0x0c)	// (0x03 << 2)
#define MAX_FRAME_LEN	(0x10)	// (0x04 << 2)
#define TEST_REG		(0x1c)	// (0x07 << 2)
#define MII_CFG			(0x20)	// (0x08 << 2)
#define MII_CMD			(0x24)	// (0x09 << 2)
#define MII_ADDR		(0x28)	// (0x0A << 2)
#define MII_CTRL		(0x2c)	// (0x0B << 2)
#define MII_STATUS		(0x30)	// (0x0C << 2)
#define MII_IND			(0x34)	// (0x0D << 2)
#define IFACE_CTRL		(0x38)	// (0x0E << 2)
#define IFACE_STATUS	(0x3c)	// (0x0F << 2)
#define STA_ADDR1		(0x40)	// (0x10 << 2)
#define STA_ADDR2		(0x44)	// (0x11 << 2)

/**
 * A-MCXFIF Registers
 */
#define AMCXFIF_CFG0			(0x48)	// (0x12 << 2)
#define AMCXFIF_CFG1			(0x4c)	// (0x13 << 2)
#define AMCXFIF_CFG2			(0x50)	// (0x14 << 2)
#define AMCXFIF_CFG3			(0x54)	// (0x15 << 2)
#define AMCXFIF_CFG4			(0x58)	// (0x16 << 2)
#define AMCXFIF_CFG5			(0x5c)	// (0x17 << 2)
#define AMCXFIF_FIFO_ACCESS0	(0x60)	// (0x18 << 2)
#define AMCXFIF_FIFO_ACCESS1	(0x64)	// (0x19 << 2)	
#define AMCXFIF_FIFO_ACCESS2	(0x68)	// (0x1A << 2)	
#define AMCXFIF_FIFO_ACCESS3	(0x6c)	// (0x1B << 2)	
#define AMCXFIF_FIFO_ACCESS4	(0x70)	// (0x1C << 2)	
#define AMCXFIF_FIFO_ACCESS5	(0x74)	// (0x1D << 2)	
#define AMCXFIF_FIFO_ACCESS6	(0x78)	// (0x1E << 2)	
#define AMCXFIF_FIFO_ACCESS7	(0x7c)	// (0x1F << 2)

/**
 * M-AHBE Registers
 */
#define DMATXCTRL		(0x180)
#define DMATXDESC		(0x184)
#define DMATXSTATUS		(0x188)
#define DMARXCTRL		(0x18c)
#define DMARXDESC		(0x190)
#define DMARXSTATUS		(0x194)
#define DMAINTMASK		(0x198)
#define DMAINT			(0x19c)

/*
 * MAC Configuration 1 (MAC_CFG1) Register bit fields
 */
#define MACCFG1_SOFT_RESET		(1<<31)
#define MACCFG1_LOOPBACK		(1<<8)
#define MACCFG1_RECV_ENABLE		(1<<2)
#define MACCFG1_XMIT_ENABLE		(1<<0)

/*
 * MAC Configuration 2 (MAC_CFG2) Register bit fields
 */
#define MACCFG2_PREAMBLE_LEN(x)		(((x) & 0xf) << 12)
#define MACCFG2_IFACE_MASK			(3<<8)
#define MACCFG2_IFACE_NIBBLE		(1<<8)
#define MACCFG2_IFACE_BYTE			(2<<8)
#define MACCFG2_HUGE_FRAME			(1<<5)
#define MACCFG2_LENGTH_CHECK		(1<<4)
#define MACCFG2_PAD_CRC				(1<<2)
#define MACCFG2_CRC_ENABLE			(1<<1)
#define MACCFG2_FULL_DUPLEX			(1<<0)


/*
 * MII_IND Register bit fields
 */
#define MIIIND_NOT_VALID	(1 << 2)
#define MIIIND_SCANNING		(1 << 1)
#define MIIIND_BUSY			(1 << 0)

/*
 * IFACE_CTRL bit fields
 */
#define IFACECTRL_SPEED		(1<<16)

/*
 * IFACE_STATUS bit fields
 */
#define IFACESTA_CLASH			(1<<8)
#define IFACESTA_JABBER			(1<<7)
#define IFACESTA_LINK_OK		(1<<6)
#define IFACESTA_FULL_DUPLEX	(1<<5)
#define IFACESTA_SPEED			(1<<4)
#define IFACESTA_LINK_FAIL		(1<<3)
#define IFACESTA_LOSS_CARRIER	(1<<2)
#define IFACESTA_SQE_ERROR		(1<<1)
#define IFACESTA_JABBER_H		(1<<0)
/*
 * DMATxCtrl Register bit fields
 */
#define DMATXCTRL_TX_EN		(0x01)


/*
 * DMATxStatus Register bit fields
 */
#define DMATXSTATUS_TXPKT_CNT_SHIFT	(16)
#define DMATXSTATUS_TXPKT_CNT_MASK	(0xFF << DMATXSTATUS_TXPKT_CNT_SHIFT)
#define DMATXSTATUS_BUS_ERR			(1<<3)
#define DMATXSTATUS_TX_UNDERRUN		(1<<1)
#define DMATXSTATUS_TXPKT_SENT		(1<<0)
#define DMATXSTATUS_EVENT_MASK		(DMATXSTATUS_BUS_ERR | DMATXSTATUS_TX_UNDERRUN | DMATXSTATUS_TXPKT_SENT)

/*
 * DMARxCtrl Register bit fields
 */
#define DMARXCTRL_RX_EN		(0x01)

/*
 * DMARxStatus Register bit fields
 */
#define DMARXSTATUS_RXPKT_CNT_SHIFT	(16)
#define DMARXSTATUS_RXPKT_CNT_MASK	(0xFF << DMARXSTATUS_RXPKT_CNT_SHIFT)
#define DMARXSTATUS_BUS_ERR			(1<<3)
#define DMARXSTATUS_RX_OVERFLOW		(1<<2)
#define DMARXSTATUS_RXPKT_RECVD		(1<<0)

/*
 * DMAIntrMask Register bit fields
 */
#define DMAINTMASK_RX_BUS_ERR		(1<<7)
#define DMAINTMASK_RX_OVERFLOW		(1<<6)
#define DMAINTMASK_RXPKT_RECVD		(1<<4)
#define DMAINTMASK_TX_BUS_ERR		(1<<3)
#define DMAINTMASK_TX_UNDERRUN		(1<<1)
#define DMAINTMASK_TXPKT_SENT		(1<<0)

/*
 * DMAIntrrupt Register bit fields
 */
#define DMAINT_RX_BUS_ERR		(1<<7)
#define DMAINT_RX_OVERFLOW		(1<<6)
#define DMAINT_RXPKT_RECVD		(1<<4)
#define DMAINT_TX_BUS_ERR		(1<<3)
#define DMAINT_TX_UNDERRUN		(1<<1)
#define DMAINT_TXPKT_SENT		(1<<0)

#define DMAINT_TX_MASK		(DMAINT_TX_BUS_ERR | DMAINT_TX_UNDERRUN | DMAINT_TXPKT_SENT)
#define DMAINT_RX_MASK		(DMAINT_RX_BUS_ERR | DMAINT_RX_OVERFLOW | DMAINT_RXPKT_RECVD)

enum INTERFACE_MODE {
	UNKNOWN_MODE = -1,
	NIBBLE_MODE = 0,	//10/100Mbps MII/RMII/SMII
	BYTE_MODE,			//1000Mbps GMII/TBI
};

struct p4a_ether_platdata {
	int phyaddr;
	int	ifmode;	//interface mode,see enum INTERFACE_MODE
	void (*power_ctrl)(int on);
	void (*hard_reset)(void);
};

extern int __init p4a_add_device_ether(struct p4a_ether_platdata* bd);

#endif	//__P4A_ETHER_H__

/*
 *	linux/drivers/net/arm/p4a_ether.c
 *
 *	Driver for the Innofidei P4A ethernet MAC (Inventra PE-MCXMAC)
 *
 *	Author : jimmy.li <lizhengming@innofidei.com>
 *  Copyright (C) 2010 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Notes : the dma transfer not support unaligned address, need 4-bytes align
 */

/*#define DEBUG*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mii.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/dma-mapping.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/ctype.h>
#include <linux/bitops.h>

#include <asm/io.h>
#include <asm/uaccess.h>

#include <mach/hardware.h>
#include <mach/p4a_ether.h>

#undef NAPI_MODE_ENABLE

#ifdef CONFIG_P4A_ETHER_NAPI
#define  NAPI_MODE_ENABLE
#endif

#define DRV_NAME	"p4a-ether"

#define DRV_VERSION	"1.1"

#define ETHER_P4A_NAPI_WEIGHT	64

#define NUM_TX_DESC		64
#define NUM_RX_DESC		128

#define MAX_FRAME_SIZE	1536

/* Accept MAC address of the form macaddr="00:24:e8:d8:e6:11" */
static char *macaddr = NULL;
static int loopback = 0;
static int ifmode = UNKNOWN_MODE;

struct ether_dma_desc {
	u32	startAddr;
	u32	packetSize;
#define DESC_PS_EMPTY_FLAG			(1<<31)	
#define DESC_PS_PACKETSIZE_MASK		(0xfff)
	u32	nextDesc;
	u32	noused;
};

struct ether_buffer {
	struct sk_buff	*skb;
	void*			buf;		// virtual address
	dma_addr_t		dma;		// physical address
	size_t			length;
};

struct ether_desc_ring {
	void*	desc;		// pointer to the descriptor ring memory
	dma_addr_t	dma;	// physical address of the descriptor ring
	size_t		size;	// length of descriptor ring in bytes
	u32			count;	// number of descriptors in the ring (init at probe stage)

	struct ether_buffer*	buff_info;	// array of buffer information structs

	u32			i_curr;
	u32			i_dirty;
};


struct ether_p4a_private {
	struct platform_device	*pdev;
	struct net_device 		*ndev;
#ifdef NAPI_MODE_ENABLE	
	struct napi_struct		napi;
#endif
	struct mii_if_info		mii;

	struct timer_list	link_check_timer;
	int		linkflag;

	/* TX */
	struct ether_desc_ring		*tx_ring;
	int num_tx_queues;
	u32 i_tq_front;	// current used tx queue index
	u32 i_tq_rear;	// the next free tx queue index
	int	tx_busy;
	spinlock_t	tx_lock;

	/* RX */
	struct ether_desc_ring		rx_ring;
	u32		rx_buf_len;

	int 	phy_addr;
	void __iomem 	*membase;
	struct clk 		*clk;
	int		ifmode;		// 0 : nibble, 1 : byte
	void (*power_ctrl)(int on);
	void (*hard_reset)(void);

	u32		intr_event;

	struct dentry *debugfs_root;
};

#if 0
static inline void dump_data(const u8* data, size_t len, const char* msg)
{
	int i = 0;

	if (msg)
		printk("%s\n", msg);

	for (i=0; i < len; i++) {
		printk("%02x ", data[i]);
		if ( (i+1) % 16 == 0)
			printk("\n");
	}
	
	if ( (i+1) % 16)
		printk("\n");
}
#else
static inline void dump_data(const u8* data, size_t len, const char* msg) {}
#endif

static int setup_tx_resources(struct ether_p4a_private *lp);
static void free_tx_resources(struct ether_p4a_private *lp);
static int alloc_tx_buffers(struct ether_p4a_private *lp);
static void free_tx_buffers(struct ether_p4a_private *lp);

static int setup_rx_resources(struct ether_p4a_private *lp);
static void free_rx_resources(struct ether_p4a_private *lp);
static int alloc_rx_buffers(struct ether_p4a_private *lp);
static void free_rx_buffers(struct ether_p4a_private *lp);

enum _caller {
	CALL_BY_XMIT = 0,
	CALL_BY_TXIRQ,
};
static const char* _str_caller[] = {
	[CALL_BY_XMIT]	= "start_xmit",
	[CALL_BY_TXIRQ] = "tx_irq",
};
static void restart_transmit(struct ether_p4a_private *lp, int caller);

static inline unsigned int rd_regl(struct ether_p4a_private* lp, unsigned int reg_addr)
{
	return readl(lp->membase + reg_addr);
}

static inline void wr_regl(struct ether_p4a_private *lp, unsigned int reg_addr, unsigned int value)
{
	writel(value, lp->membase + reg_addr);
}

static void wait_phy_ready(struct ether_p4a_private *lp)
{
	int timeout = 1000;

	while ((rd_regl(lp, MII_IND) & MIIIND_BUSY) && timeout--) {
		udelay(1);
	}
}

static int mdio_read(struct net_device *dev, int phy_id, int location)
{
	struct ether_p4a_private *lp = netdev_priv(dev);

	wr_regl(lp,	MII_ADDR, (phy_id & 0x1f)<<8 | (location & 0x1f));
	wr_regl(lp, MII_CMD, 0x00);
	wr_regl(lp, MII_CMD, 0x01);	//perform a single read

	wait_phy_ready(lp);

	return rd_regl(lp, MII_STATUS);
}

static void mdio_write(struct net_device *dev, int phy_id, int location, int value)
{
	struct ether_p4a_private *lp = netdev_priv(dev);

	wr_regl(lp, MII_ADDR, (phy_id & 0x1f)<<8 | (location & 0x1f));
	wr_regl(lp, MII_CTRL, (value & 0xffff));

	wait_phy_ready(lp);
}

/*
 * converts the 48-bit Ethernet host  address  asc  from  the
 * standard  hex-digits-and-colons  notation  into  binary data 
 * in network byte order
 */
static u8* parse_addr_aton(const char *asc, u8 *addr)
{
	size_t cnt;

	for (cnt = 0; cnt < ETH_ALEN; ++cnt) {
		unsigned int number;
		char ch;

		ch = tolower(*asc++);
		if ((ch < '0' || ch > '9') && (ch < 'a' || ch > 'f'))
			return NULL;

		number = isdigit(ch) ? (ch - '0') : (ch - 'a' + 10);

		ch = tolower(*asc);
		if ((cnt < 5 && ch != ':') || (cnt == 5 && ch != '\0' && !isspace(ch))) {
			++asc;
			if ((ch < '0' || ch > '9') && (ch < 'a' || ch > 'f'))
				return NULL;
			number <<= 4;
			number += isdigit(ch) ? (ch - '0') : (ch - 'a' + 10);

			ch = *asc;
			if (cnt < 5 && ch != ':')
				return NULL;
		}

		addr[cnt] = (u8)number;	// store result

		++asc;	// skip ':'
	}

	return addr;
}

static void get_mac_address(struct net_device* dev)
{
	u8 hwaddr[ETH_ALEN];

	if (macaddr==NULL || parse_addr_aton(macaddr, hwaddr)==NULL) {
		random_ether_addr(hwaddr);
	}

	memcpy(dev->dev_addr, hwaddr, ETH_ALEN);
}

// program the hardware MAC address from ndev->dev_addr
static void update_mac_address(struct net_device* dev)
{
	struct ether_p4a_private *lp = netdev_priv(dev);
	u8* addr = dev->dev_addr;

	wr_regl(lp, STA_ADDR1, ((addr[0] << 24) | (addr[1] << 16) | (addr[2] << 8) | addr[3]));	
	wr_regl(lp, STA_ADDR2, (addr[4] << 24) | (addr[5] << 16));
}

static void update_linkspeed_register(struct ether_p4a_private *lp, int speed, int duplex)
{
	u32 mac_cfg2 = rd_regl(lp, MAC_CFG2);
	u32 iface_ctrl = rd_regl(lp, IFACE_CTRL);

	dev_info(&lp->pdev->dev, "%s: speed %dMbps, %s duplex\n", lp->ndev->name, (speed == SPEED_100) ? 100 : 10,
				(duplex == DUPLEX_FULL) ? "full" : "half");

	if (speed == SPEED_100) {
		iface_ctrl |= IFACECTRL_SPEED;
	} else {
		iface_ctrl &= ~ IFACECTRL_SPEED;
	}

	if (duplex == DUPLEX_FULL) {
		mac_cfg2 |= MACCFG2_FULL_DUPLEX;
	} else {
		mac_cfg2 &= ~MACCFG2_FULL_DUPLEX;
	}

	wr_regl(lp, MAC_CFG2, mac_cfg2);
	wr_regl(lp, IFACE_CTRL, iface_ctrl);
}

static void check_link_status(struct ether_p4a_private *lp)
{
	struct net_device* ndev = lp->ndev;
	struct platform_device *pdev = lp->pdev;
	unsigned int bmsr, bmcr, lpa, speed, duplex;

	if (!mii_link_ok(&lp->mii) && lp->linkflag) {
		lp->linkflag = 0;
		netif_carrier_off(ndev);
		dev_info(&pdev->dev, "%s : link down\n", ndev->name);
		return ;
	}

	if (lp->linkflag == 1)
		return;
	
	bmsr = mdio_read(ndev, lp->phy_addr, MII_BMSR);
	bmcr = mdio_read(ndev, lp->phy_addr, MII_BMCR);

	if (bmcr & BMCR_ANENABLE) {
		if (!(bmsr & BMSR_ANEGCOMPLETE))
			return;
		lpa = mdio_read(ndev, lp->phy_addr, MII_LPA);

		if ((lpa & LPA_100FULL) || (lpa & LPA_100HALF))
			speed = SPEED_100;
		else
			speed = SPEED_10;

		if ((lpa & LPA_100FULL) || (lpa & LPA_10FULL))
			duplex = DUPLEX_FULL;
		else
			duplex = DUPLEX_HALF;

	} else {
		speed = (bmcr & BMCR_SPEED100) ? SPEED_100 : SPEED_10;
		duplex = (bmcr & BMCR_FULLDPLX) ? DUPLEX_FULL : DUPLEX_HALF;
	}

	update_linkspeed_register(lp, speed, duplex);

	lp->linkflag = 1;
	netif_carrier_on(ndev);
}

static void phy_link_check(unsigned long dev_id)
{
	struct ether_p4a_private *lp = (struct ether_p4a_private*)dev_id;

	check_link_status(lp);
	mod_timer(&lp->link_check_timer, jiffies + msecs_to_jiffies(1000));
}

static void ether_p4a_mac_init(struct ether_p4a_private *lp)
{
	u32	val;

	wr_regl(lp, MAC_CFG1, MACCFG1_SOFT_RESET);
	udelay(100);
	wr_regl(lp, MAC_CFG1, 0);

	wr_regl(lp, MAC_CFG2, 0x00007000);
	wr_regl(lp, IPG_IFG, 0x40605060);		//default value
	wr_regl(lp, HALF_DEPLEX, 0x00A1F037);	//default value
	wr_regl(lp, MAX_FRAME_LEN, MAX_FRAME_SIZE);
	wr_regl(lp, MII_CFG, 0x00000007);		//???
	
	val = MACCFG2_PREAMBLE_LEN(7) /*| MACCFG2_LENGTH_CHECK */| MACCFG2_PAD_CRC | MACCFG2_FULL_DUPLEX;
	if (lp->ifmode == BYTE_MODE)
		val |= MACCFG2_IFACE_BYTE;
	else
		val |= MACCFG2_IFACE_NIBBLE;
	wr_regl(lp, MAC_CFG2, val); 

	val = MACCFG1_RECV_ENABLE | MACCFG1_XMIT_ENABLE;
	if (loopback)
		val |= MACCFG1_LOOPBACK;
	wr_regl(lp, MAC_CFG1, val);

	// AMXCFIF settings
	wr_regl(lp, AMCXFIF_CFG0, (0x1f<<8));	// enable AMCXFIF modules
	wr_regl(lp, AMCXFIF_CFG3, 0x0680ffff);	//???

	val = BIT(16) | BIT(6) | BIT(5) | BIT(4);
	wr_regl(lp, AMCXFIF_CFG4, val);	// set drop frame conditions (corresponds to Receive Statistics Vector)

	val = (rd_regl(lp, AMCXFIF_CFG4) | 0x3ffff) & ~(val); 
	wr_regl(lp, AMCXFIF_CFG5, val);
	
	// reset PHY and enable auto negotiation
	val = mdio_read(lp->ndev, lp->phy_addr, MII_BMCR);
	val |= BMCR_ANENABLE | BMCR_RESET; 
	mdio_write(lp->ndev, lp->phy_addr, MII_BMCR, val);
}

static int ether_p4a_get_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct ether_p4a_private* lp = netdev_priv(dev);

	pr_debug("ether_p4a_get_settings\n");

	return mii_ethtool_gset(&lp->mii, cmd);
}

static int ether_p4a_set_settings(struct net_device *dev, struct ethtool_cmd *cmd)
{
	struct ether_p4a_private* lp = netdev_priv(dev);
	
	pr_debug("ether_p4a_set_settings\n");

	return mii_ethtool_sset(&lp->mii, cmd);
}

static int ether_p4a_nwayreset(struct net_device *dev)
{
	struct ether_p4a_private* lp = netdev_priv(dev);
	
	pr_debug("ether_p4a_nwayreset\n");

	return mii_nway_restart(&lp->mii);
}

static void ether_p4a_get_drvinfo(struct net_device *dev, struct ethtool_drvinfo *info)
{
	strlcpy(info->driver, DRV_NAME, sizeof(info->driver));
	strlcpy(info->version, DRV_VERSION, sizeof(info->version));
	strlcpy(info->bus_info, dev_name(dev->dev.parent), sizeof(info->bus_info));
}

static const struct ethtool_ops ether_p4a_ethtool_ops = {
	.get_settings	= ether_p4a_get_settings,
	.set_settings	= ether_p4a_set_settings,
	.get_drvinfo	= ether_p4a_get_drvinfo,
	.nway_reset	= ether_p4a_nwayreset,
	.get_link	= ethtool_op_get_link,
};


static void 
#ifdef NAPI_MODE_ENABLE
ether_p4a_rx_irq(struct ether_p4a_private *lp, int *work_done, int work_to_do)
#else
ether_p4a_rx_irq(struct ether_p4a_private *lp)
#endif
{
	struct platform_device* pdev = lp->pdev;
	struct ether_desc_ring *rxdr = &lp->rx_ring;
	struct net_device *ndev = lp->ndev;
	struct ether_dma_desc *desc;
	struct ether_buffer* buff_info;
	struct sk_buff *skb;
	size_t packet_size;
	u32 i;
	unsigned int drsr;	//DMA Rx Status Register
#ifdef DEBUG
	u32 recv_cnt = 0;
#endif
	dev_dbg(&pdev->dev, "ether_p4a_rx_irq\n");

	drsr = rd_regl(lp, DMARXSTATUS);
	if (drsr & DMARXSTATUS_RX_OVERFLOW) {
		ndev->stats.rx_over_errors++;
		dev_warn(&pdev->dev, "DMA RX Overflow\n");
	}

	if (drsr & DMARXSTATUS_BUS_ERR) {
		dev_err(&pdev->dev, "DMA RX Bus Error\n");
	}

	dev_dbg(&pdev->dev, "drsr=0x%08x\n", drsr);

	i = rxdr->i_curr;
	desc = (struct ether_dma_desc*)rxdr->desc + i;
	while ( !(desc->packetSize & DESC_PS_EMPTY_FLAG) ) {
		buff_info = &rxdr->buff_info[i];
		skb = buff_info->skb;
		if (skb == NULL) {	// may round back
			dev_info(&pdev->dev, "ring buffer empty!\n");
			break;
		}

#ifdef NAPI_MODE_ENABLE
		if (*work_done >= work_to_do)
			break;
		(*work_done)++;
#endif

#ifdef DEBUG
		recv_cnt++;
#endif
		dma_unmap_single(&pdev->dev, buff_info->dma, buff_info->length, DMA_FROM_DEVICE);
		buff_info->skb = NULL;
		
		wr_regl(lp, DMARXSTATUS, DMARXSTATUS_RXPKT_RECVD);	// reduces the RxPktCount value by one.
		
		packet_size = desc->packetSize & DESC_PS_PACKETSIZE_MASK;
		skb_put(skb, packet_size);
		dev_dbg(&pdev->dev, "(%d)received frame size %d bytes\n", i, packet_size);
		
		dump_data(skb->data, skb->len, "received frame:");

		skb->protocol = eth_type_trans(skb, ndev);

		if ( !(ndev->flags & IFF_PROMISC) && skb->pkt_type == PACKET_OTHERHOST) {
			dev_dbg(&pdev->dev, "drop other host's packet\n");	
			dev_kfree_skb_any(skb);

		} else {
#ifdef NAPI_MODE_ENABLE
			netif_receive_skb(skb);
#else
			netif_rx(skb);
#endif
			ndev->last_rx = jiffies;
			ndev->stats.rx_packets++;
			ndev->stats.rx_bytes += packet_size;
		}

		if (++i == rxdr->count)
			i = 0;
		desc = (struct ether_dma_desc*)rxdr->desc + i;
	}

#ifdef DEBUG
	if (recv_cnt) {
		dev_dbg(&pdev->dev, "received %d, i=%d\n", recv_cnt, i);
	}
#endif

	rxdr->i_curr = i;
	
	alloc_rx_buffers(lp);

	if (drsr & (DMARXSTATUS_BUS_ERR | DMARXSTATUS_RX_OVERFLOW)) {
		wr_regl(lp, DMARXSTATUS, drsr & (DMARXSTATUS_BUS_ERR | DMARXSTATUS_RX_OVERFLOW));	// clear status bits
		dev_info(&pdev->dev, "DMARXSTATUS = %08x\n", rd_regl(lp, DMARXSTATUS));

		dev_info(&pdev->dev, "update DMARxDescriptor\n");
		dev_info(&pdev->dev, "DMARXCTRL = %08x\n", rd_regl(lp, DMARXCTRL));
		// update DMARxDescriptor
		wr_regl(lp, DMARXDESC, lp->rx_ring.dma + sizeof(struct ether_dma_desc) * i);
		//when Rx Overflow or Bus error, the RxEn bit in DMARXCTRL will be clear.
		wr_regl(lp, DMARXCTRL, DMARXCTRL_RX_EN); 	//Rx enable
	}
}

static void ether_p4a_tx_irq(struct ether_p4a_private *lp)
{
	struct net_device *ndev = lp->ndev;
	struct ether_desc_ring *txdr;
	struct ether_dma_desc *desc;
	struct ether_buffer* buff_info;
	struct platform_device* pdev = lp->pdev;
	u32 i, end;
	unsigned int dtsr;	//DMA Tx Status Register
	unsigned long flags;

	dev_dbg(&pdev->dev, "ether_p4a_tx_irq\n");

	dtsr = rd_regl(lp, DMATXSTATUS);
	if (!(dtsr & DMATXSTATUS_EVENT_MASK))	// no interrupts
		return;

	if (dtsr & DMATXSTATUS_BUS_ERR) {
		dev_err(&pdev->dev, "Tx Bus Error\n");
	}
	if (dtsr & DMATXSTATUS_TX_UNDERRUN) {
		dev_dbg(&pdev->dev, "Tx Under Run\n");
	}

	dev_dbg(&pdev->dev, "dtsr=0x%08x\n", dtsr);
	
	spin_lock_irqsave(&lp->tx_lock, flags);
	txdr = &lp->tx_ring[lp->i_tq_front];
	
	if (txdr->i_dirty < txdr->i_curr) {
		i = txdr->i_dirty;
		end = txdr->i_curr;
		desc = (struct ether_dma_desc*)txdr->desc + i;
		
		while (desc->packetSize & DESC_PS_EMPTY_FLAG) {
		
			buff_info = &txdr->buff_info[i];
		
			dev_dbg(&pdev->dev, "(%d %d) transmit one frame done\n", lp->i_tq_front, i);

			wr_regl(lp, DMATXSTATUS, DMATXSTATUS_TXPKT_SENT);	//reduces the TxPktCount value by one
		
			ndev->stats.tx_bytes += buff_info->length;
			ndev->stats.tx_packets++;
			desc->packetSize = DESC_PS_EMPTY_FLAG;

			if (++i == end)
				break;
			desc = (struct ether_dma_desc*)txdr->desc + i;
		}

		txdr->i_dirty = i;  // update i_dirty postion

	} else {
		dev_dbg(&pdev->dev, "tx irq do nothing\n");
	}

	if (dtsr & (DMATXSTATUS_BUS_ERR | DMATXSTATUS_TX_UNDERRUN)) {
		wr_regl(lp, DMATXSTATUS, dtsr & (DMATXSTATUS_BUS_ERR | DMATXSTATUS_TX_UNDERRUN));	// clear status bits
	}

	if (txdr->i_dirty != 0 && txdr->i_dirty == txdr->i_curr) {  // current queue transfer done
		dev_dbg(&pdev->dev, "prepare set tx queue to next!\n");

		if (!(dtsr & DMATXSTATUS_TX_UNDERRUN)) {
			dev_dbg(&pdev->dev, "all frame transmit done, but no underrun occur!(%08x, %08x)\n", dtsr, rd_regl(lp, DMATXSTATUS));

			/* be careful, if Tx Underrun, the DMATXCTRL TxEnable bit will be auto clear, 
			   here we do not detect underrun bit, it may set after later,
			   so before re-enable DMATXCTRL, we disble it manually.
			 */
			wr_regl(lp, DMATXCTRL, 0);
		}

		//reset current queue	
		txdr->i_dirty = txdr->i_curr = 0;

		// set transmit queue to next
		lp->i_tq_front = (lp->i_tq_front + 1) % lp->num_tx_queues;

		lp->tx_busy = 0;
		spin_unlock_irqrestore(&lp->tx_lock, flags);

		restart_transmit(lp, CALL_BY_TXIRQ);
	} else {
		dev_dbg(&pdev->dev, "no ready to set tx queue to next\n");
		spin_unlock_irqrestore(&lp->tx_lock, flags);
	}
}

static irqreturn_t ether_p4a_irq_handler(int irq, void *dev_id)
{
	struct net_device *ndev = (struct net_device*)dev_id;
	struct ether_p4a_private *lp = netdev_priv(ndev);

#ifdef NAPI_MODE_ENABLE
	// disable interrupts
	wr_regl(lp, DMAINTMASK, 0);
	
	if (likely(napi_schedule_prep(&lp->napi))) {
		__napi_schedule(&lp->napi);
	}

#else
	u32 isr = rd_regl(lp, DMAINT);
	
	if (isr & DMAINT_RX_MASK) {
		ether_p4a_rx_irq(lp);
	}
	
	if (isr & DMAINT_TX_MASK) {
		ether_p4a_tx_irq(lp);
	}
#endif

	return IRQ_HANDLED;
}

#ifdef DEBUG
static void dump_tx_buffers(struct ether_p4a_private* lp)
{
	int i, j;
	struct ether_buffer *buff_info;

	printk("Tx Buffers: \n");
	for (i=0; i < lp->num_tx_queues; i++) {
		struct ether_desc_ring* txdr = &lp->tx_ring[i];
		printk("\tQueue%d:\n", i);
		for (j=0; j < txdr->count; j++) {
			buff_info = &txdr->buff_info[j];
			printk("\t\tentry%d: virt:0x%08x\tdma:0x%08x\n",  j, (unsigned int)buff_info->buf, buff_info->dma);
		}
	}
	printk("\n");
}
#endif

static void __free_tx_buffers(struct ether_p4a_private *lp, struct ether_desc_ring* txdr)
{
	struct platform_device* pdev = lp->pdev;
	int i;

	for (i=0; i<txdr->count;) {
		struct ether_buffer *buff_info;

		buff_info = &txdr->buff_info[i++];
		if (buff_info->buf) {
			dma_unmap_single(&pdev->dev, buff_info->dma, PAGE_SIZE, DMA_TO_DEVICE);
			
			free_page((unsigned long)buff_info->buf);

			buff_info->buf = NULL;
		}
		
		if ( i < txdr->count) {
			buff_info = &txdr->buff_info[i++];
			
			buff_info->buf = NULL;
		}	
	}
}

static void free_tx_buffers(struct ether_p4a_private *lp)
{
	int i;

	for (i=0; i<lp->num_tx_queues; i++) {
		__free_tx_buffers(lp, &lp->tx_ring[i]);
	}
}

static int __alloc_tx_buffers(struct ether_p4a_private *lp, struct ether_desc_ring* txdr)
{
	struct platform_device* pdev = lp->pdev;
	int i;

	for (i=0; i<txdr->count; ) {
		struct ether_buffer *buff_info;
		void* page;
		dma_addr_t	dma;

		BUG_ON(MAX_FRAME_SIZE > (PAGE_SIZE>>1));

		page =(void*)__get_free_page(GFP_KERNEL);
		if (!page)
			goto err;

		dma = dma_map_single(&pdev->dev, page, PAGE_SIZE, DMA_TO_DEVICE);
		if (dma_mapping_error(&pdev->dev, dma)) {
			free_page((unsigned long)page);
			goto err;
		}

		buff_info = &txdr->buff_info[i++];
		buff_info->buf = page;
		buff_info->dma = dma;
		
		if ( i < txdr->count) {
			buff_info = &txdr->buff_info[i++];
			buff_info->buf = page + (PAGE_SIZE>>1);
			buff_info->dma = dma + (PAGE_SIZE>>1);
		}
	}

	return 0;
err:
	__free_tx_buffers(lp, txdr);
	return -ENOMEM;
}

static int alloc_tx_buffers(struct ether_p4a_private *lp)
{
	int ret = 0;
	int i;

	for (i=0; i<lp->num_tx_queues; i++) {
		ret = __alloc_tx_buffers(lp, &lp->tx_ring[i]);
		if (ret) {
			for (--i; i>=0; i--) {
				__free_tx_buffers(lp, &lp->tx_ring[i]);
			}
		}
	}

	return ret;
}

static int __setup_tx_resources(struct ether_p4a_private *lp, struct ether_desc_ring* txdr)
{
	struct platform_device* pdev = lp->pdev;
	struct ether_dma_desc* desc;
	size_t size;
	int ret = -ENOMEM;
	int i;

	txdr->count = NUM_TX_DESC;
	size = sizeof(struct ether_buffer) * txdr->count;
	txdr->buff_info = kzalloc(size, GFP_KERNEL);
	if (!txdr->buff_info) {
		goto out;
	}

	txdr->size = txdr->count * sizeof(struct ether_dma_desc);
	txdr->desc = dma_alloc_coherent(&pdev->dev, txdr->size, &txdr->dma, GFP_KERNEL);
	if (!txdr->desc) {
		goto err_free;
	}

	memset(txdr->desc, 0, txdr->size);

	// building the dma desc ring
	for (i=0; i < txdr->count; i++) {
		desc = (struct ether_dma_desc*)txdr->desc + i;
			
		desc->packetSize = DESC_PS_EMPTY_FLAG;
		if (i != txdr->count -1)
			desc->nextDesc = txdr->dma + sizeof(struct ether_dma_desc) * (i + 1);	// point to next descriptor address
		else
			desc->nextDesc = txdr->dma;	// the last descriptor's next point to the first descriptor address
	}

	txdr->i_dirty = txdr->i_curr = 0;

	return 0;

err_free:
	kfree(txdr->buff_info);
	txdr->buff_info = NULL;
out:
	return ret;
}

static void __free_tx_resources(struct ether_p4a_private *lp, struct ether_desc_ring* txdr)
{
	struct platform_device* pdev = lp->pdev;

	kfree(txdr->buff_info);
	txdr->buff_info = NULL;
	
	dma_free_coherent(&pdev->dev, txdr->size, txdr->desc, txdr->dma);
	txdr->desc = NULL;
}

static int setup_tx_resources(struct ether_p4a_private *lp)
{
	int size;
	int ret = -ENOMEM;
	int i;

	size = sizeof(struct ether_desc_ring) * lp->num_tx_queues;
	lp->tx_ring = (struct ether_desc_ring*)kzalloc(size, GFP_KERNEL);
	if (!lp->tx_ring) {
		goto out;
	}

	for (i=0; i<lp->num_tx_queues; i++) {
		ret = __setup_tx_resources(lp, &lp->tx_ring[i]);
		if (ret) {
			for (--i; i >= 0; i--) 
				__free_tx_resources(lp, &lp->tx_ring[i]);
			
			goto err_free_queue;
		}
	}

	spin_lock_init(&lp->tx_lock);
	lp->i_tq_front = 0;
	lp->i_tq_rear = 0;
	lp->tx_busy = 0;
	return 0;

err_free_queue:
	kfree(lp->tx_ring);
	lp->tx_ring = NULL;
out:
	return ret;
}

static void free_tx_resources(struct ether_p4a_private *lp)
{
	int i;

	for (i=0; i<lp->num_tx_queues; i++) {
		__free_tx_resources(lp, &lp->tx_ring[i]);
	}
}

static int alloc_rx_buffers(struct ether_p4a_private *lp)
{
	struct net_device* ndev = lp->ndev;
	struct platform_device* pdev = lp->pdev;
	struct ether_desc_ring* rxdr = &lp->rx_ring;
	struct ether_buffer* buff_info;
	struct ether_dma_desc *desc;
	u32 i;
#ifdef DEBUG
	int alloc_cnt = 0;
#endif

	i = rxdr->i_dirty;
	buff_info = &rxdr->buff_info[i];

	while (!buff_info->skb) {
		struct sk_buff *skb;
		desc = (struct ether_dma_desc*)rxdr->desc + i;
		skb = netdev_alloc_skb(ndev, lp->rx_buf_len);
#ifdef DEBUG
		alloc_cnt++;
#endif
		if (!skb) {
			dev_warn(&pdev->dev, "no more skb could be alloc\n");
			break;
		}

		buff_info->skb = skb;
		buff_info->length = lp->rx_buf_len;
		buff_info->dma = dma_map_single(&pdev->dev, skb->data, buff_info->length, DMA_FROM_DEVICE);

		desc->startAddr = buff_info->dma;
		desc->packetSize = DESC_PS_EMPTY_FLAG;

		if (++i == rxdr->count)
			i = 0;

		buff_info = &rxdr->buff_info[i];
	}
#ifdef DEBUG
	dev_info(&pdev->dev, "alloc %d skbs, range :[%d, %d)\n", alloc_cnt, rxdr->i_dirty, i);
#endif

	rxdr->i_dirty = i;
	return 0;
}

static void free_rx_buffers(struct ether_p4a_private *lp)
{
	struct ether_desc_ring* rxdr = &lp->rx_ring;
	struct ether_buffer* buff_info;
	int i;

	for (i=0; i<rxdr->count; i++) {
		buff_info = &rxdr->buff_info[i];
		if (!buff_info->skb)
			continue;
		kfree_skb(buff_info->skb);
	}
}

static int setup_rx_resources(struct ether_p4a_private *lp)
{
	struct ether_desc_ring* rxdr = &lp->rx_ring;
	struct platform_device* pdev = lp->pdev;
	struct ether_dma_desc* desc;
	int size;
	int i;
	int ret = -ENOMEM;
	
	rxdr->count = NUM_RX_DESC;
	size = sizeof(struct ether_buffer) * rxdr->count;
	rxdr->buff_info = kzalloc(size, GFP_KERNEL);
	if (!rxdr->buff_info) {
		goto out;
	}

	rxdr->size = rxdr->count * sizeof(struct ether_dma_desc);
	rxdr->desc = dma_alloc_coherent(&pdev->dev, rxdr->size, &rxdr->dma, GFP_KERNEL);
	if (!rxdr->desc) {
		goto err_free;
	}

	memset(rxdr->desc, 0, rxdr->size);
	
	for (i=0; i < rxdr->count; i++) {
		desc = (struct ether_dma_desc*)rxdr->desc + i;
		
		desc->packetSize = DESC_PS_EMPTY_FLAG;

		if (i != rxdr->count -1)
			desc->nextDesc = rxdr->dma + sizeof(struct ether_dma_desc) * (i + 1);	// point to next descriptor address
		else
			desc->nextDesc = rxdr->dma;	// the last descriptor's next point to the first descriptor address
	}

	rxdr->i_curr = 0;
	rxdr->i_dirty = 0;

	return 0;

err_free:
	kfree(rxdr->buff_info);
	rxdr->buff_info = NULL;
out:
	return ret;
}

static void free_rx_resources(struct ether_p4a_private *lp)
{
	struct ether_desc_ring* rxdr = &lp->rx_ring;
	struct platform_device* pdev = lp->pdev;

	kfree(rxdr->buff_info);
	rxdr->buff_info = NULL;
	
	dma_free_coherent(&pdev->dev, rxdr->size, rxdr->desc, rxdr->dma);
	rxdr->desc = NULL;
}

static int ether_p4a_up(struct ether_p4a_private *lp)
{
	struct net_device* ndev = lp->ndev;
	int ret;
	unsigned int ds;

	clk_enable(lp->clk);
	update_mac_address(ndev);	// program ethernet address into MAC

	if (lp->power_ctrl)		// power on
		lp->power_ctrl(1);
	
	if (lp->hard_reset)
		lp->hard_reset();

	ether_p4a_mac_init(lp);

	lp->intr_event = DMAINTMASK_RX_BUS_ERR | DMAINTMASK_RX_OVERFLOW | DMAINTMASK_RXPKT_RECVD	\
					| DMAINTMASK_TX_BUS_ERR | DMAINTMASK_TX_UNDERRUN;

	wr_regl(lp, DMAINTMASK, 0);	//disable interrupts
	ds = rd_regl(lp, DMARXSTATUS);
	if (ds) {
		int count = (ds & DMARXSTATUS_RXPKT_CNT_MASK) >> DMARXSTATUS_RXPKT_CNT_SHIFT;
		dev_warn(&lp->pdev->dev, "ether_p4a: when up the DMARXSTATUS = 0x%08x\n", ds);
		while (ds && count--) {
			wr_regl(lp, DMARXSTATUS, ds & (DMARXSTATUS_BUS_ERR | DMARXSTATUS_RX_OVERFLOW | DMARXSTATUS_RXPKT_RECVD));
			ds = rd_regl(lp, DMARXSTATUS);
		}
	}
		
	ds = rd_regl(lp, DMATXSTATUS);
	if (ds) {
		int count = (ds & DMATXSTATUS_TXPKT_CNT_MASK) >> DMATXSTATUS_TXPKT_CNT_SHIFT;
		dev_warn(&lp->pdev->dev, "ether_p4a: when up the DMATXSTATUS = 0x%08x\n", ds);
		while (ds && count--) {
			wr_regl(lp, DMATXSTATUS, ds & (DMATXSTATUS_BUS_ERR | DMATXSTATUS_TX_UNDERRUN | DMATXSTATUS_TXPKT_SENT));
			ds = rd_regl(lp, DMATXSTATUS);
		}
	}

#ifdef NAPI_MODE_ENABLE
	napi_enable(&lp->napi);
#endif

	ret = request_irq(ndev->irq, ether_p4a_irq_handler, 0, ndev->name, ndev);
	if (ret) {
		dev_err(&lp->pdev->dev, "ether_p4a: Can't get irq (%d)\n", ndev->irq);
		return ret;
	}

	wr_regl(lp, DMAINTMASK, lp->intr_event);
	wr_regl(lp, DMARXDESC, lp->rx_ring.dma);	// set dma descriptor address
	wr_regl(lp, DMARXCTRL, DMARXCTRL_RX_EN); 	//Rx enable

	mod_timer(&lp->link_check_timer, jiffies + msecs_to_jiffies(1000));
	netif_start_queue(ndev);

	return 0;
}

static void ether_p4a_down(struct ether_p4a_private *lp)
{
	struct net_device* ndev = lp->ndev;
	
	netif_stop_queue(ndev);
	netif_carrier_off(ndev);
	del_timer_sync(&lp->link_check_timer);
	
#ifdef NAPI_MODE_ENABLE
	napi_disable(&lp->napi);
#endif
	free_irq(ndev->irq, ndev);

	wr_regl(lp, DMATXCTRL, 0); 	//DMA Tx disable
	wr_regl(lp, DMARXCTRL, 0); 	//DMA Rx disable
	wr_regl(lp, MAC_CFG1, 0);	// Tx / Rx en disable
	
	if (lp->power_ctrl)
		lp->power_ctrl(0);

	clk_disable(lp->clk);
}

/*
 * Open the ethernet interface
 */
static int ether_p4a_open(struct net_device *dev)
{
	struct ether_p4a_private *lp = netdev_priv(dev);
	int ret;

	pr_debug("ether_p4a_open\n");

	netif_carrier_off(dev);

	// allocate transmit descriptors
	ret = setup_tx_resources(lp);
	if (ret)
		goto out;
	
	ret = alloc_tx_buffers(lp);
	if (ret)
		goto err_free_tx_res;
	//dump_tx_buffers(lp);

	// allocate receive descriptors
	ret = setup_rx_resources(lp);
	if (ret)
		goto err_free_tx_bufs;

	ret = alloc_rx_buffers(lp);
	if (ret)
		goto err_free_rx_res;

	ret = ether_p4a_up(lp);
	if (ret)
		goto err_free_rx_bufs;

	return 0;

err_free_rx_bufs:
	free_rx_buffers(lp);

err_free_rx_res:
	free_rx_resources(lp);

err_free_tx_bufs:
	free_tx_buffers(lp);

err_free_tx_res:
	free_tx_resources(lp);

out:
	return ret;
}

/*
 * Close the interface
 */
static int ether_p4a_close(struct net_device *dev)
{
	struct ether_p4a_private *lp = netdev_priv(dev);

	dev_dbg(&lp->pdev->dev, "ether_p4a_close\n");

	ether_p4a_down(lp);

	free_rx_buffers(lp);
	free_rx_resources(lp);

	free_tx_buffers(lp);
	free_tx_resources(lp);
	
	return 0;
}

static void restart_transmit(struct ether_p4a_private *lp, int caller)
{
	unsigned long flags;

	spin_lock_irqsave(&lp->tx_lock, flags);

	if (!lp->tx_busy) {
		struct ether_desc_ring *txdr = &lp->tx_ring[lp->i_tq_front];

		wr_regl(lp, DMATXCTRL, 0);

		if (lp->i_tq_rear == lp->i_tq_front) {
			if (txdr->i_curr == 0) {
				//all queues empty, do nothing
				dev_dbg(&lp->pdev->dev, "restart_transmit do nothing when call from %s\n", _str_caller[caller]);
				goto out;
			} else {
				// only one queue has elements, move the i_tq_rear point to the next
				lp->i_tq_rear = (lp->i_tq_rear + 1) % lp->num_tx_queues;
			}
		}

		//start transmiter
		wr_regl(lp, DMATXDESC, txdr->dma);
		wr_regl(lp, DMATXCTRL, DMATXCTRL_TX_EN);	//Tx enable

		lp->tx_busy = 1;
	}
out:
	spin_unlock_irqrestore(&lp->tx_lock, flags);
	return;
}

/*
 * push the @skb into the idle tx queue, and return this tx queue.
 *
 * if there is no idle tx queue available, then return NULL.
 */
static struct ether_desc_ring* __tx_queue_skb(struct ether_p4a_private* lp, struct sk_buff *skb)
{
	struct ether_desc_ring *txdr;
	unsigned long flags;
	struct ether_dma_desc *desc;
	struct ether_buffer *buff_info;
	int i;

	spin_lock_irqsave(&lp->tx_lock, flags);

	txdr = &lp->tx_ring[lp->i_tq_rear]; 

	if (txdr->i_curr >= txdr->count) {
		int new_rear = (lp->i_tq_rear + 1) % lp->num_tx_queues;
		dev_dbg(&lp->pdev->dev, "tx queue(%d) full, move the next(%d)\n", lp->i_tq_rear, new_rear);
		if (new_rear == lp->i_tq_front) {
			spin_unlock_irqrestore(&lp->tx_lock, flags);
			dev_warn(&lp->pdev->dev, "no room to queue skb for transmit!\n");
			return NULL;
		}

		lp->i_tq_rear = new_rear;
		txdr = &lp->tx_ring[new_rear]; 
		if (txdr->i_curr != 0)	//debug
		  dev_warn(&lp->pdev->dev, "the new queue 's i_curr is %d, should be 0\n", txdr->i_curr);
	}

	i = txdr->i_curr;
	desc = (struct ether_dma_desc*)txdr->desc + i;
	buff_info = &txdr->buff_info[i];
	dev_dbg(&lp->pdev->dev, "insert frame into (%d %d)\n", lp->i_tq_rear, i);

#ifdef DEBUG
	if (buff_info->dma != virt_to_dma(&pdev->dev, buff_info->buf)) {	// for debug
		dev_err(&lp->pdev->dev, "ether_p4a_start_xmit : fetal error, the buffer virtual and physical address not match\n"); 
		dev_err(&lp->pdev->dev, "tx queue (%d), entry(%d), virt=%08x, dma=%08x\n",	\
				lp->i_tq_rear, txdr->i_curr, (unsigned int)buff_info->buf, buff_info->dma);
		dump_tx_buffers(lp);
	}
#endif

	// copy skb buffer to our buffer
	skb_copy_from_linear_data(skb, buff_info->buf, skb->len); 
	buff_info->length = skb->len;
	dma_sync_single_for_device(&lp->pdev->dev, buff_info->dma, buff_info->length, DMA_TO_DEVICE);

	dev_kfree_skb_any(skb);

	// fill TX descriptor
	desc->startAddr = buff_info->dma;
	desc->packetSize = buff_info->length & 0xfff;	// also clear the empty flag

	// move i_curr to next position
	txdr->i_curr++;

	spin_unlock_irqrestore(&lp->tx_lock, flags);

	return txdr; 
}


/*
 * Transmit packet.
 */
static int ether_p4a_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ether_p4a_private *lp = netdev_priv(dev); 
	struct platform_device *pdev = lp->pdev;
	int ret = NETDEV_TX_BUSY;

	dev_dbg(&pdev->dev, "ether_p4a_start_xmit\n");

	if (unlikely(skb->len > MAX_FRAME_SIZE)) {
		dev_warn(&pdev->dev, "skb length too long, drop it\n");
		dev->stats.tx_dropped++;
		return NETDEV_TX_OK;
	}

	if (NULL != __tx_queue_skb(lp, skb)) {
		restart_transmit(lp, CALL_BY_XMIT);
		ret = NETDEV_TX_OK;
	}

	return ret;
}

/*
 * Update the current statistics from the internal statistics registers.
 */
static struct net_device_stats *ether_p4a_stats(struct net_device *dev)
{
	pr_debug("ether_p4a_stats\n");
	return &dev->stats;
}

static void ether_p4a_set_multicast_list(struct net_device *dev)
{
	pr_debug("ether_p4a_set_mac_multicast_list\n");
}

static int ether_p4a_set_mac_address(struct net_device *dev, void* address)
{
	struct sockaddr * addr = address;

	pr_debug("ether_p4a_set_mac_address\n");
	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;
	
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	update_mac_address(dev);

	return 0;
}

static int ether_p4a_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	return -EINVAL;
}

#ifdef NAPI_MODE_ENABLE
/*
 * NAPI Rx Polling callback
 */
static int ether_p4a_poll(struct napi_struct *napi, int budget)
{
	struct ether_p4a_private *lp = container_of(napi, struct ether_p4a_private, napi);
	unsigned int work_done = 0;

	dev_dbg(&lp->pdev->dev, "ether_p4a_poll, budget = %d\n", budget);

	ether_p4a_tx_irq(lp);
	ether_p4a_rx_irq(lp, &work_done, budget);

	if (work_done < budget) {
		napi_complete(napi);

		dev_dbg(&lp->pdev->dev, "ether_p4a_poll, re-enable interrupts\n");
		// enable interrupts
		wr_regl(lp, DMAINTMASK, lp->intr_event); 
	}

	return work_done;
}
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
static void ether_p4a_poll_controller(struct net_device *dev)
{
}
#endif

static const struct net_device_ops p4a_netdev_ops = {
	.ndo_open				= ether_p4a_open,
	.ndo_stop				= ether_p4a_close,
	.ndo_start_xmit			= ether_p4a_start_xmit,
	.ndo_get_stats			= ether_p4a_stats,
	.ndo_set_multicast_list	= ether_p4a_set_multicast_list,
	.ndo_set_mac_address	= ether_p4a_set_mac_address,
	.ndo_do_ioctl			= ether_p4a_ioctl,
	.ndo_validate_addr		= eth_validate_addr,
	.ndo_change_mtu			= eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller	= ether_p4a_poll_controller,
#endif
};

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#include <linux/fs.h>
#include <linux/seq_file.h>

static int tx_info_show(struct seq_file *s, void *unused)
{
	int i;
	struct ether_p4a_private *lp = s->private;
	struct ether_desc_ring* txdr;
	struct ether_dma_desc *desc;

	seq_printf(s, "tx info:\n\n");

	seq_printf(s, "i_tq_front = %d, i_tq_rear = %d, busy = %d\n", lp->i_tq_front, lp->i_tq_rear, lp->tx_busy);

	for (i=0; i < lp->num_tx_queues; i++) {
		int k;
		txdr = &lp->tx_ring[i];
		seq_printf(s, "Tx Queue %d:	i_curr = %d, i_dirty = %d\n", i, txdr->i_curr, txdr->i_dirty);
		for (k=0; k<txdr->count; k++) {
			desc = (struct ether_dma_desc*)txdr->desc + k;
			seq_printf(s, "%d : startAddr %08x, packetSize %08x,  nextDesc %08x\n", \
						k, desc->startAddr, desc->packetSize, desc->nextDesc);
		}
	}

	seq_printf(s, "DMATXSTATUS %x\n\n", rd_regl(lp, DMATXSTATUS));
	seq_printf(s, "DMATXDESC %x\n\n", rd_regl(lp, DMATXDESC));
	seq_printf(s, "DMATXCTRL %x\n\n", rd_regl(lp, DMATXCTRL));

	return 0;
}

static int tx_info_open(struct inode *inode, struct file *file)
{
	return single_open(file, tx_info_show, inode->i_private);
}

static const struct file_operations tx_info_fops = {
	.open           = tx_info_open,
	.read           = seq_read,
	.llseek         = seq_lseek,
	.release        = single_release,
};


static int __init ether_p4a_debugfs_init(struct ether_p4a_private *lp)
{
	struct dentry       *root;
	struct dentry       *file;
	int ret;

	root = debugfs_create_dir("p4a_ether", NULL);
	if (IS_ERR(root)) {
		ret = PTR_ERR(root);
		goto err0;
	}

	file = debugfs_create_file("tx_info", S_IRUSR, root, lp, &tx_info_fops);
	if (IS_ERR(file)) {
		ret = PTR_ERR(file);
		goto err1;
	}

	lp->debugfs_root = root;
	return 0;

err1:
	debugfs_remove_recursive(root);
err0:
	return ret;

}
#else
static int __init ether_p4a_debugfs_init(struct ether_p4a_private* lp){return 0;}
#endif	/* CONFIG_DEBUG_FS */

static int __init ether_p4a_probe(struct platform_device *pdev)
{
	struct ether_p4a_private *lp;
	struct net_device *ndev;
	struct resource *memres, *irqres;
	struct mii_if_info* mii;
	int ret;
	struct p4a_ether_platdata* pdata = pdev->dev.platform_data; 
	void __iomem *membase;

	dev_dbg(&pdev->dev, "ether_p4a_probe\n");

	if (unlikely(!pdata)) {
		dev_err(&pdev->dev, "no platform data\n");
		return -EINVAL;
	}
	
	memres = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (unlikely(!memres)) {
		dev_err(&pdev->dev, "no mem resource?\n");
		return -ENODEV;
	}

	irqres = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (unlikely(!irqres)) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return -ENODEV;
	}

	if (!request_mem_region(memres->start, resource_size(memres), "p4a_ether")) {
		dev_err(&pdev->dev, "request_mem_region failed\n");
		return -EBUSY;
	}
	
	ndev = alloc_etherdev(sizeof(struct ether_p4a_private));
	if (unlikely(!ndev)) {
		dev_err(&pdev->dev, "unable to alloc new ethernet\n");
		ret = -ENOMEM;
		goto err_release;
	}

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &p4a_netdev_ops;
	SET_ETHTOOL_OPS(ndev, &ether_p4a_ethtool_ops);
	ndev->irq = irqres->start;
	membase = ioremap(memres->start, resource_size(memres));
	if (!membase) {
		dev_err(&pdev->dev, "ioremap failed\n");
		ret = -ENOMEM;
		goto err_free;
	}
	ndev->base_addr = (unsigned long)membase;

	lp = netdev_priv(ndev);
	lp->phy_addr = pdata->phyaddr;
	lp->linkflag = 0;
	lp->pdev = pdev;
	lp->ndev = ndev;
#ifdef NAPI_MODE_ENABLE	
	netif_napi_add(ndev, &lp->napi, ether_p4a_poll, ETHER_P4A_NAPI_WEIGHT); 
#endif
	lp->num_tx_queues = 4;
	lp->rx_buf_len = MAX_FRAME_SIZE; 
	lp->membase = membase;
	lp->clk = clk_get(&pdev->dev, "ETH_CLK");
	if (unlikely(IS_ERR(lp->clk))) {
		ret = PTR_ERR(lp->clk);
		goto err_iounmap;
	}

	lp->ifmode = pdata->ifmode;
	lp->power_ctrl = pdata->power_ctrl;
	lp->hard_reset = pdata->hard_reset;

	// module params overrides board info
	if (ifmode != UNKNOWN_MODE) {
		lp->ifmode = ifmode;
	}

	mii = &lp->mii;
	mii->dev = ndev;
	mii->phy_id = lp->phy_addr;
	mii->mdio_read = mdio_read;
	mii->mdio_write = mdio_write;
	mii->phy_id_mask = 0x1f;
	mii->reg_num_mask = 0x1f;
	mii->supports_gmii = 0;	//TODO

	setup_timer(&lp->link_check_timer, phy_link_check, (unsigned long)lp);

	get_mac_address(ndev);		// get ethernet address and store it in ndev->dev_addr

	ret = register_netdev(ndev);
	if (ret) {
		goto err_clk_put;
	}

	ether_p4a_debugfs_init(lp);

	platform_set_drvdata(pdev, ndev);

	return 0;

err_clk_put:
	clk_put(lp->clk);

err_iounmap:
#ifdef NAPI_MODE_ENABLE
	netif_napi_del(&lp->napi);
#endif
	iounmap(membase);
	
err_free:
	free_netdev(ndev);	

err_release:
	release_mem_region(memres->start, resource_size(memres));
	return ret;
}

static int __devexit ether_p4a_remove(struct platform_device *pdev)
{
	struct net_device *ndev = platform_get_drvdata(pdev);
	struct ether_p4a_private *lp = netdev_priv(ndev);
	struct resource *memres = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	dev_dbg(&pdev->dev, "ether_p4a_remove\n");

	unregister_netdev(ndev);
	iounmap(lp->membase);
	free_netdev(ndev);
	release_mem_region(memres->start, resource_size(memres)); 
	clk_put(lp->clk);
	return 0;
}

#ifdef CONFIG_PM
static int ether_p4a_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	dev_info(&pdev->dev, "ether_p4a_suspend\n");

	return 0;
}

static int ether_p4a_resume(struct platform_device *pdev)
{
	dev_info(&pdev->dev, "ether_p4a_resume\n");

	return 0;
}

#else
#define ether_p4a_suspend	NULL
#define ether_p4a_resume	NULL
#endif

static struct platform_driver ether_p4a_driver = {
	.remove		= __devexit_p(ether_p4a_remove),
	.suspend	= ether_p4a_suspend,
	.resume		= ether_p4a_resume,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init ether_p4a_init(void)
{
	return platform_driver_probe(&ether_p4a_driver, ether_p4a_probe);
}

static void __exit ether_p4a_exit(void)
{
	platform_driver_unregister(&ether_p4a_driver);
}

module_init(ether_p4a_init)
module_exit(ether_p4a_exit)

MODULE_AUTHOR("Jimmy.li <lizhengming@innofidei.com>");
MODULE_DESCRIPTION("Innofidei P4A Ethernet MAC Driver");
MODULE_LICENSE("GPL");

module_param(macaddr, charp, 0);
module_param(loopback, bool, 0);
module_param(ifmode, int, 0);
MODULE_PARM_DESC(macaddr, "MAC address of the form macaddr=00:24:e8:d8:e6:11");
MODULE_PARM_DESC(loopback, "Enable MAC loopback mode");
MODULE_PARM_DESC(ifmode, "Interface Mode, 0 : Nibble, 1 : Byte");

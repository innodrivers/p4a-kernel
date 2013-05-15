/*
 *	linux/drivers/net/arm/p4a_mbether.c
 *
 *	Driver for the simulation ethernet MAC
 *
 *	Author :
 *         devin.yang <yangfan@innofidei.com>
 *         jimmy.li <lizhengming@innofidei.com>
 *
 *  Copyright (C) 2012 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

/*#define DEBUG*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/ethtool.h>
#include <linux/platform_device.h>

#include <linux/sched.h>
#include <linux/wait.h>
#include <asm/io.h>
#include <asm/uaccess.h>

#include <mach/micproto.h>
#include <mach/micp_cmd.h>


#define DRV_NAME	"p4a-mbether"

//#define NAPI_MODE_ENABLE
//#define GRO_ENABLE
#define MREQ_PREALLOC_ENABLE

/* NAPI options */
#define MAX_RX_PENDING			(128)
#define ETHER_MB_NAPI_WEIGHT	(64)

#define PREALLOC_NUM			(128)
#define MAX_MREQB_USED			(128)
#define LOW_MREQB_USED			(48)

enum MAILBOX_ETHER_CMD {
    MB_NET_LINKUP_IND = 0,
    MB_NET_LINKDOWN_IND,
    MB_NET_IP_PACKET_NEW,
    MB_NET_IP_PACKET_FREE,
};

#define MB_NET_RET_LATEFREE     (0x444c4fa8)        // ascii "HOLD"

static unsigned char fake_src_hwaddr[ETH_ALEN] = {0x0A, 0x0C, 0x29, 0x91, 0x9D, 0x40};

struct ether_mb_private {
	struct net_device *ndev;

	struct device *dev;

#ifdef NAPI_MODE_ENABLE
	struct napi_struct	napi;
	struct sk_buff_head input_pkt_list;
	struct sk_buff_head process_list;
#endif

	atomic_t mreqb_used_count;
#ifdef MREQ_PREALLOC_ENABLE
	struct mreqb *prealloc_mreqb[PREALLOC_NUM];
	unsigned long bitmap_free_mreqb[(PREALLOC_NUM - 1) / 32 + 1];
#endif

	struct dentry *debugfs_root;
};

extern void *p4a_cpu1_mem_p2v(unsigned long address);

#ifdef MREQ_PREALLOC_ENABLE
static void prealloc_mreqb_init(struct ether_mb_private *mp)
{
	int i;

	memset(mp->bitmap_free_mreqb, 0, sizeof(mp->bitmap_free_mreqb));

	for (i = PREALLOC_NUM - 1; i >= 0; i--) {
		mp->prealloc_mreqb[i] = mreqb_alloc(0);
		set_bit(i, (void *)mp->bitmap_free_mreqb);
	}

}

static void prealloc_mreqb_exit(struct ether_mb_private *mp)
{
	int i;

	for (i = 0; i < PREALLOC_NUM; i++) {
		mreqb_free(mp->prealloc_mreqb[i]);
	}
}
#endif

static inline void  _wake_txqueue(struct ether_mb_private *mp)
{
	if (netif_queue_stopped(mp->ndev)) {
		dev_info(&mp->ndev->dev, "wake queue\n");
		netif_wake_queue(mp->ndev);
	}
}

static inline void _stop_txqueue(struct ether_mb_private *mp)
{
	if (!netif_queue_stopped(mp->ndev)) {
		dev_info(&mp->ndev->dev, "stop queue\n");
		netif_stop_queue(mp->ndev);
	}
}

static inline struct mreqb *__get_mreqb(struct ether_mb_private *mp)
{
	struct mreqb *request;

#ifdef MREQ_PREALLOC_ENABLE
	int idx;

	idx = find_first_bit((void *)mp->bitmap_free_mreqb, PREALLOC_NUM);

	if (idx == PREALLOC_NUM) {
		request = mreqb_alloc(0);
		dev_info(&mp->ndev->dev, "no more pre-alloc mreqb \n");
	} else {
		clear_bit(idx, (void *)mp->bitmap_free_mreqb);
		request = mp->prealloc_mreqb[idx];
	}

	request->argv[6] = (unsigned long)mp;
	request->argv[7] = (unsigned long)idx;

#else
	request = mreqb_alloc(0);
#endif

	if (atomic_inc_return(&mp->mreqb_used_count) >= MAX_MREQB_USED) {
		_stop_txqueue(mp);
	}

	return request;
}

static inline void __put_mreqb(struct mreqb *mreqb)
{
#ifdef MREQ_PREALLOC_ENABLE
	int idx;
	struct ether_mb_private *mp;

	mp = (struct ether_mb_private *)(mreqb->argv[6]);
	idx = (int)(mreqb->argv[7]);

	if (idx >= 0 && idx < PREALLOC_NUM) {
		mreqb_reinit(mreqb);
		set_bit(idx, (void *)mp->bitmap_free_mreqb);
	} else {
		mreqb_free(mreqb);
	}

#else
	mreqb_free(mreqb);
#endif

	if (atomic_dec_return(&mp->mreqb_used_count) < LOW_MREQB_USED) {
		_wake_txqueue(mp);
	}
}

/*
 * Open the ethernet interface
 */
static int ether_mb_open(struct net_device *dev)
{
	struct ether_mb_private *mp = netdev_priv(dev);

	atomic_set(&mp->mreqb_used_count, 0);
#ifdef MREQ_PREALLOC_ENABLE
	prealloc_mreqb_init(mp);
#endif

#ifdef NAPI_MODE_ENABLE
	napi_enable(&mp->napi);
#endif

	netif_start_queue(dev);

	return 0;
}

/*
 * Close the interface
 */
static int ether_mb_close(struct net_device *dev)
{
	struct ether_mb_private *mp = netdev_priv(dev);

	netif_stop_queue(dev);

#ifdef NAPI_MODE_ENABLE
	napi_disable(&mp->napi);
#endif

#ifdef MREQ_PREALLOC_ENABLE
	prealloc_mreqb_exit(mp);
#endif

	return 0;
}

static struct sk_buff *mbether_fill_rx_skb(struct net_device *ndev, void *data, unsigned int len)
{
	struct sk_buff *skb;

	skb = netdev_alloc_skb(ndev, (len + ETH_HLEN) + NET_IP_ALIGN + 512);

	if (skb == NULL) {
		dev_err(&ndev->dev, "cannot alloc more rx skb\n");
		return NULL;
	}


	skb_reserve(skb, NET_IP_ALIGN);
#ifdef CONFIG_MARVELL_88W8787
	/* reserve 512Byte for Marvell 8787 wifi driver */
	skb_reserve(skb, 512);
#endif

	/* build MAC header */
	memcpy(skb->data, ndev->dev_addr, ETH_ALEN);
	memcpy(skb->data + ETH_ALEN, fake_src_hwaddr, ETH_ALEN);
	skb->data[ETH_ALEN * 2] = (ETH_P_IP >> 8) & 0x0F;
	skb->data[ETH_ALEN * 2 + 1] = ETH_P_IP & 0x0F;

	memcpy(skb->data + ETH_HLEN, (void *)data, len);

	skb_put(skb, len + ETH_HLEN);
	skb->protocol = eth_type_trans(skb, ndev);

	return skb;
}

static int mbether_recv_ip_packet(struct ether_mb_private *mp, void *ip_buf, size_t len)
{
	struct sk_buff *skb;
	struct net_device *ndev;

	ndev = mp->ndev;

	if (!(ndev->flags & IFF_UP)) {
		dev_err(mp->dev, "receive packet when netif down!\n");
		return -1;
	}

#ifdef NAPI_MODE_ENABLE

	if (skb_queue_len(&mp->input_pkt_list) > MAX_RX_PENDING) {
		dev_err(&ndev->dev, "too more input packet, drop it!\n");
		atomic_long_inc(&ndev->rx_dropped);
		return -1;
	}

#endif

	skb = mbether_fill_rx_skb(ndev, (void *)ip_buf, len);

	if (skb == NULL) {
		return -ENOMEM;
	}

#ifdef NAPI_MODE_ENABLE
	skb_queue_tail(&mp->input_pkt_list, skb);

	if (likely(napi_schedule_prep(&mp->napi))) {
		__napi_schedule(&mp->napi);
	}

#else
	netif_rx(skb);
#endif

	ndev->last_rx = jiffies;
	ndev->stats.rx_packets++;
	ndev->stats.rx_bytes += (len + ETH_HLEN);

	return 0;
}


/* the complete function of mreqb NETIF_TX_REQ */
static void mailbox_xmit_completion(struct mreqb *mreqb)
{
	struct ether_mb_private *mp = (struct ether_mb_private *)mreqb->context;
	struct sk_buff *skb;
	dma_addr_t dma;
	size_t len;

	skb = (struct sk_buff *)MREQB_GET_ARG(mreqb, 0);
	dma = (dma_addr_t)MREQB_GET_ARG(mreqb, 1);
	len = (size_t)MREQB_GET_ARG(mreqb, 2);

	__put_mreqb(mreqb);
	dma_unmap_single(mp->dev, dma, len, DMA_TO_DEVICE);

	if (mreqb->result != MB_NET_RET_LATEFREE) {
		dev_kfree_skb(skb);
	}

}

static int mailbox_xmit_packet(struct ether_mb_private *mp, struct sk_buff *skb)
{
	struct mreqb *request;
	unsigned int len;
	dma_addr_t dma;
	void *buf;
	int status;

	buf = skb->data + ETH_HLEN;
	len = skb->len - ETH_HLEN;

	dma = dma_map_single(mp->dev, buf, len, DMA_TO_DEVICE);

	if (!dma) {
		dev_err(mp->dev, "dma mapping error\n");
		return -ENOMEM;
	}

	request = __get_mreqb(mp);

	MREQB_BIND_CMD(request, NET_REQUEST);
	MREQB_SET_SUBCMD(request, MB_NET_IP_PACKET_NEW);
	MREQB_PUSH_ARG(request, skb);		// use skb as packet id
	MREQB_PUSH_ARG(request, dma);		// packet buf in physical address
	MREQB_PUSH_ARG(request, len);		// packet length
	MREQB_PUSH_CACHE_UPDATE(request, dma, len);

	request->complete = mailbox_xmit_completion;
	request->context = mp;
	request->prio = 1;

	status = mreqb_submit(request);

	return status;
}

/*
 * Transmit packet.
 */
static int ether_mb_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ether_mb_private *mp = netdev_priv(dev);

	mailbox_xmit_packet(mp, skb);

	dev->stats.tx_bytes += skb->len;
	dev->stats.tx_packets++;

	return NETDEV_TX_OK;
}

/*
 * Update the current statistics from the internal statistics registers.
 */
static struct net_device_stats *ether_mb_stats(struct net_device *dev)
{
	return &dev->stats;
}

static void ether_mb_set_multicast_list(struct net_device *dev)
{
}

static int ether_mb_set_mac_address(struct net_device *dev, void *address)
{
	struct sockaddr *addr = address;

	if (!is_valid_ether_addr(addr->sa_data)) {
		return -EADDRNOTAVAIL;
	}

	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);

	return 0;
}

static int ether_mb_ioctl(struct net_device *dev, struct ifreq *rq, int cmd)
{
	return -EINVAL;
}

#ifdef NAPI_MODE_ENABLE
static int ether_mb_poll(struct napi_struct *napi, int budget)
{
	struct ether_mb_private *mp = container_of(napi, struct ether_mb_private, napi);
	unsigned int work = 0;

	while (work < budget) {
		struct sk_buff *skb;
		unsigned int qlen;
		unsigned long flags;

		while ((skb = skb_dequeue(&mp->process_list))) {
#ifdef GRO_ENABLE

			if (mp->ndev->features & NETIF_F_GRO) {
				skb->ip_summed = CHECKSUM_UNNECESSARY;
				napi_gro_receive(napi, skb);
			} else
#endif
			{
				netif_receive_skb(skb);
			}

			if (++work >= budget) {
				return work;
			}
		}

		qlen = skb_queue_len(&mp->input_pkt_list);

		if (qlen) {
			spin_lock_irqsave(&mp->input_pkt_list.lock, flags);
			skb_queue_splice_tail_init(&mp->input_pkt_list, &mp->process_list);
			spin_unlock_irqrestore(&mp->input_pkt_list.lock, flags);

		} else {
			break;
		}
	}

	if (work < budget) {
		napi_complete(napi);
	}

	return work;
}
#endif

#ifdef CONFIG_NET_POLL_CONTROLLER
static void ether_mb_poll_controller(struct net_device *dev) {}
#endif

static const struct net_device_ops mb_netdev_ops = {
	.ndo_open = ether_mb_open,
	.ndo_stop = ether_mb_close,
	.ndo_start_xmit = ether_mb_start_xmit,
	.ndo_get_stats = ether_mb_stats,
	.ndo_set_multicast_list = ether_mb_set_multicast_list,
	.ndo_set_mac_address = ether_mb_set_mac_address,
	.ndo_do_ioctl = ether_mb_ioctl,
	.ndo_validate_addr = eth_validate_addr,
	.ndo_change_mtu = eth_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = ether_mb_poll_controller,
#endif
};

static int do_mbether_request(struct mreqb *reqb, void *priv)
{
	struct ether_mb_private *mp = (struct ether_mb_private *)priv;
	int ret = 0;

	switch (reqb->subcmd) {
	case MB_NET_LINKUP_IND:
		netif_carrier_on(mp->ndev);
		break;

	case MB_NET_LINKDOWN_IND:
		netif_carrier_off(mp->ndev);
		break;

	case MB_NET_IP_PACKET_NEW: {
		unsigned long pkt_id;
		dma_addr_t pkt_dma;
		size_t pkt_sz;
		void *pkt_buf;

		pkt_id = MREQB_GET_ARG(reqb, 0);
		pkt_dma = (dma_addr_t)MREQB_GET_ARG(reqb, 1);
		pkt_sz = (size_t)MREQB_GET_ARG(reqb, 2);

		// Pay attention!!
		// the physical address here is allocated and access by CPU1
		pkt_buf = (void *)p4a_cpu1_mem_p2v(pkt_dma);

		dma_sync_single_for_cpu(mp->dev, pkt_dma, pkt_sz, DMA_FROM_DEVICE);

		ret = mbether_recv_ip_packet(mp, pkt_buf, pkt_sz);

		MREQB_EMPTY_CACHE_UPDATE(reqb);

		break;
	}

	case MB_NET_IP_PACKET_FREE: {
		struct sk_buff *skb;
		int i;
		int num = reqb->argc;

		for (i=0; i<num; i++) {
			skb = (struct sk_buff *)MREQB_GET_ARG(reqb, i);
			dev_kfree_skb(skb);
		}
		break;
	}

	default:
		dev_err(mp->dev, "Unknown mailbox netif command %d\n", reqb->subcmd);
		return -1;
	};

	return ret;
}

static int __init p4a_mbether_probe(struct platform_device *pdev)
{
	struct net_device *ndev;
	struct ether_mb_private *mp;
	unsigned char macaddr[ETH_ALEN];
	int ret;

	ndev = alloc_etherdev(sizeof(struct ether_mb_private));

	if (unlikely(ndev == NULL)) {
		dev_err(&pdev->dev, "unable to alloc new ethernet\n");
		return -ENOMEM;
	}

	mp = netdev_priv(ndev);
	mp->dev = &pdev->dev;
	mp->ndev = ndev;

#ifdef NAPI_MODE_ENABLE
	netif_napi_add(ndev, &mp->napi, ether_mb_poll, ETHER_MB_NAPI_WEIGHT);
	skb_queue_head_init(&mp->input_pkt_list);
	skb_queue_head_init(&mp->process_list);
#endif

	SET_NETDEV_DEV(ndev, &pdev->dev);
	ndev->netdev_ops = &mb_netdev_ops;

	random_ether_addr(macaddr);
	memcpy(ndev->dev_addr, macaddr, sizeof(macaddr));

	ret = register_netdev(ndev);

	if (ret) {
		goto err_free;
	}

	platform_set_drvdata(pdev, ndev);

	mreqb_register_cmd_handler(C_NET_REQUEST, do_mbether_request, mp);

	return 0;

err_free:
#ifdef NAPI_MODE_ENABLE
	netif_napi_del(&mp->napi);
#endif

	free_netdev(ndev);

	return ret;
}

static struct platform_driver p4a_mbether_pdrv = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init p4a_mbether_init(void)
{
	int ret;

	printk("P4A Mailbox Ethernet Driver, (c) 2013 Innofidei Inc.\n");

	ret = platform_driver_probe(&p4a_mbether_pdrv, p4a_mbether_probe);

	if (ret) {
		printk(KERN_ERR "%s: unable to register driver (%d)\n", __FUNCTION__, ret);
	}

	return ret;
}

static void __exit p4a_mbether_exit(void)
{
	platform_driver_unregister(&p4a_mbether_pdrv);
}

module_init(p4a_mbether_init);
module_exit(p4a_mbether_exit);

MODULE_AUTHOR("drivers <drivers@innofidei.com>");
MODULE_DESCRIPTION("Innofidei P4A Mailbox Virtual Ethernet Driver");
MODULE_LICENSE("GPL");


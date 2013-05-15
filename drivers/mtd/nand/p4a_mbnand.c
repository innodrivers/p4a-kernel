/*
 * linux/drivers/mtd/nand/p4a_mbnand.c
 *
 * Innofidei P4A virtual NAND driver via mailbox
 *
 * Author : jimmy.li <lizhengming@innofidei.com>
 *
 * Copyright (c) 2013 Innofidei Inc.
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

/*#define DEBUG */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/cache.h>

#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/nand_ecc.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>

#include <mach/micproto.h>
#include <mach/micp_cmd.h>
#include <mach/p4a_mbnand.h>

#define MB_NAND_NAME	"p4a-mbnand"

#define DATABUF_ALIGN(x)		L1_CACHE_ALIGN((x))
#define ADDRESS_CACHELINE_ALIGNED(x)	(IS_ALIGNED((x), L1_CACHE_BYTES))

static const char *part_probes[] = { "cmdlinepart", NULL };

struct p4a_mbnand_info {
	struct nand_chip	chip;
	struct mtd_info		mtd;
	struct platform_device *pdev;

	struct mtd_partition *parts;
	int nr_parts;

	struct nand_flash_dev *flashdev_table;

	struct list_head free_mreqb;		/* prealloc some mreqb */
	spinlock_t		mreqb_list_lock;

	/* store info when PAGEPROG */
	int seqin_column;
	int seqin_page;

	int nand_status;	/* nand status return by PROGRAM/ERASE operation */

	uint8_t* data_buf;	/* Notice : need across CPUs, so should be cache-line size aligned */
	off_t buf_off;
};

static const size_t mbnand_arg_size[MB_NAND_CMD_MAX] = {
	sizeof(struct mbnand_readid_arg),
	sizeof(struct mbnand_getinfo_arg),
	sizeof(struct mbnand_chipselect_arg),
	sizeof(struct mbnand_readpage_arg),
	sizeof(struct mbnand_writepage_arg),
	sizeof(struct mbnand_eraseblock_arg),
	sizeof(struct mbnand_read_arg),
	sizeof(struct mbnand_write_arg),
	sizeof(struct mbnand_erase_arg)
};

/* get unused mreqb */
static inline struct mreqb* __get_mreqb(struct p4a_mbnand_info* mbnand)
{
	struct mreqb* rq = NULL;
	struct list_head* head = &mbnand->free_mreqb;

	spin_lock(&mbnand->mreqb_list_lock);
	if (!list_empty(head)) {
		rq = list_entry(head->next, struct mreqb, node);
		list_del_init(&rq->node);
	}
	spin_unlock(&mbnand->mreqb_list_lock);

	return rq;
}

/* return mreqb back after use it */ 
static inline void __put_mreqb(struct p4a_mbnand_info* mbnand, struct mreqb* rq)
{
	struct list_head* head = &mbnand->free_mreqb;

	spin_lock(&mbnand->mreqb_list_lock);
	mreqb_reinit(rq);
	list_add_tail(&rq->node, head);
	spin_unlock(&mbnand->mreqb_list_lock);
}

/* prealloc some mreqb(s), call in initialization */
static void mbnand_requests_prealloc(struct p4a_mbnand_info* mbnand)
{
	struct list_head* head = &mbnand->free_mreqb;
	struct mreqb* rq;
	int i;

	spin_lock_init(&mbnand->mreqb_list_lock);
	INIT_LIST_HEAD(head);

#define MREQB_PREALLOC_COUNT	(8)	/* if chip select use sync mode, this count could be less */
	for (i=0; i<MREQB_PREALLOC_COUNT; i++) {
		rq = mreqb_alloc(sizeof(struct mbnand_arg));
		list_add_tail(&rq->node, head);
	}
}

/* destroy prealloced mreqb(s), call in driver remove */
static void mbnand_requests_free(struct p4a_mbnand_info* mbnand)
{
	struct list_head* head = &mbnand->free_mreqb;
	struct mreqb* rq;

	spin_lock(&mbnand->mreqb_list_lock);
	while (!list_empty(head)) {
		rq = list_entry(head->next, struct mreqb, node);
		list_del_init(&rq->node);
		mreqb_free(rq);	
	}
	spin_unlock(&mbnand->mreqb_list_lock);
}

/**
 * @brief apply for a unused mailbox request block for sending specified nand command.
 *
 * @param[in] mbnand - mbnand info
 * @param[in] cmd - nand command id
 *
 * @return 
 *		a partly init mreqb pointer
 */
static struct mreqb* get_mbnand_request(struct p4a_mbnand_info* mbnand, unsigned int cmd)
{
	struct mreqb* rq;

	rq = __get_mreqb(mbnand);
	BUG_ON(rq == NULL);

	MREQB_BIND_CMD(rq, NAND_REQUEST);
	MREQB_SET_SUBCMD(rq, cmd);

	return rq;
}

/**
 * @brief give mailbox request block back after used it.
 *
 * @param[in] mbnand - mbnand info
 * @param[in] rq - the MREQB to be give back
 *
 * @return
 *		none
 */
static void put_mbnand_request(struct p4a_mbnand_info* mbnand, struct mreqb *rq)
{
	__put_mreqb(mbnand, rq);
}


static void __mreqb_async_complete_callback(struct mreqb *rq)
{
	struct p4a_mbnand_info* mbnand = rq->context;

	__put_mreqb(mbnand, rq);
}

static inline int mbnand_request_sanity_check(struct mreqb *rq)
{
	int cmd = rq->cmd;
	int subcmd = rq->subcmd;

	/* sanity check */
	if (cmd != C_NAND_REQUEST || 
			subcmd >= MB_NAND_CMD_MAX || 
			rq->extra_data_size < mbnand_arg_size[subcmd]) {
		printk(KERN_ERR "mbnand request invalid argument! (cmd %d, extra_data_size %d)\n", subcmd, rq->extra_data_size);
		return -1;
	}

	return 0;
}

/**
 * @brief send nand request via mailbox, it is async mode, no wait request complete
 *
 * @param[in] mbnand - mbnand info
 * @param[in] rq - the request
 *
 * @return 
 *		if request check failed, return -1, otherwise return 0.
 */
static int send_mbnand_request_async(struct p4a_mbnand_info* mbnand, struct mreqb *rq)
{
	if (mbnand_request_sanity_check(rq))
		return -1;

	rq->complete = __mreqb_async_complete_callback;
	rq->context = mbnand;

	mreqb_submit(rq);

	return 0;
}

/**
 * @brief send nand request via mailbox, it is sync mode, will be blocked util request complete
 *
 * @param[in] mbnand - mbnand info
 * @param[in] rq - the request
 *
 * @return 
 *		if request success return 0, otherwise return negative code.
 */
static int send_mbnand_request(struct p4a_mbnand_info* mbnand, struct mreqb *rq)
{
	int ret;

	if (mbnand_request_sanity_check(rq))
		return -1;

	ret = mreqb_submit_and_wait(rq, 0);

	return ret;
}

/**
 * @brief send nand readid request via mailbox
 *
 * @param[in] mbnand - mbnand info
 * @param[out] id - nand id buffer
 * @param[in] len - nand id buffer length
 *
 * @return 
 *   if success return 0, otherwise return -1.
 */
static int do_nand_readid(struct p4a_mbnand_info* mbnand, unsigned char *id, int len)
{
	struct mreqb* rq;
	struct mbnand_readid_arg *result;
	int ret;

	rq = get_mbnand_request(mbnand, MB_NAND_READID);

	ret = send_mbnand_request(mbnand, rq);
	if (ret == 0) {	/* successful */
		result = (struct mbnand_readid_arg*)rq->extra_data;

		if (result->len > MAX_NAND_ID_LEN) {	/* invalid */
			ret = -1;
		} else {
			memcpy((void*)id, (void*)&result->id[0], min_t(int, len, result->len));
		}
	}

	put_mbnand_request(mbnand, rq);

	return ret;
}


/**
 * @brief send nand chipselect request via mailbox
 *
 * @param[in] mbnand mbnand info
 * @param[in] chip - chip select id, >=0 select specified chip, -1 de-select chip. 
 *
 * @return 
 *   always return 0.
 */
static int do_nand_chipselect(struct p4a_mbnand_info* mbnand, int chip) 
{
	struct mreqb *rq;
	struct mbnand_chipselect_arg *arg;

	rq = get_mbnand_request(mbnand, MB_NAND_CHIPSELECT);

	arg = (struct mbnand_chipselect_arg*)rq->extra_data;
	arg->cs = chip;

#if 1
	send_mbnand_request_async(mbnand, rq);
#else
	send_mbnand_request(mbnand, rq);

	put_mbnand_request(mbnand, rq);
#endif
	return 0;
}

/**
 * @brief send nand readpage request via mailbox
 *
 * @param[in] rq - mailbox request block
 * @param[in] page - page address to read
 * @param[in] column - column address in page start to read
 * @param[out] buf - pointer to the buffer address that store read data
 * @param[in] len - buffer length in bytes
 *
 * @return 
 *   if success return 0, otherwise return -1.
 */
static int do_nand_readpage(struct p4a_mbnand_info* mbnand, int page, int column, void* buf, size_t len)
{
	struct mreqb* rq;
	struct mbnand_readpage_arg *arg;
	dma_addr_t dma;
	int ret;

	rq = get_mbnand_request(mbnand, MB_NAND_READPAGE); 

	dma = dma_map_single(&mbnand->pdev->dev, buf, len, DMA_FROM_DEVICE);

	arg = (struct mbnand_readpage_arg*)rq->extra_data;
	arg->page = page;
	arg->column = column;
	arg->buf = (unsigned char*)dma;
	arg->len = len;

	dev_dbg(&mbnand->pdev->dev, "%s mreqb %p, arg %p, page %d column %d buf %p len %d\n", __FUNCTION__, rq, arg, page, column, buf, len);

	MREQB_PUSH_CACHE_UPDATE(rq, arg->buf, arg->len);

	ret = send_mbnand_request(mbnand, rq);

	dma_unmap_single(&mbnand->pdev->dev, dma, len, DMA_FROM_DEVICE); 

	put_mbnand_request(mbnand, rq);

	return ret;
}

/**
 * @brief send nand writepage request via mailbox
 *
 * @param[in] mbnand - mbnand info
 * @param[in] page - page address to program
 * @param[in] column - column address in page start to program
 * @param[out] buf - pointer to the buffer address that contains the write data
 * @param[in] len - buffer length in bytes
 * @param[out] status - nand status after program
 *
 * @return 
 *   if success return 0, otherwise return -1.
 */
static int do_nand_writepage(struct p4a_mbnand_info* mbnand, int page, int column, void* buf, size_t len, int *status)
{
	struct mreqb* rq;
	struct mbnand_writepage_arg *arg;
	dma_addr_t dma;
	int ret;

	rq = get_mbnand_request(mbnand, MB_NAND_WRITEPAGE); 

	dma = dma_map_single(&mbnand->pdev->dev, buf, len, DMA_TO_DEVICE);

	arg = (struct mbnand_writepage_arg*)rq->extra_data;
	arg->page = page;
	arg->column = column;
	arg->buf = (unsigned char*)dma;
	arg->len = len;

	MREQB_PUSH_CACHE_UPDATE(rq, arg->buf, arg->len);
	ret = send_mbnand_request(mbnand, rq);
	if (ret == 0) {	/* successful */
		if (status)
			*status = arg->status;
	}

	dma_unmap_single(&mbnand->pdev->dev, dma, len, DMA_TO_DEVICE);

	put_mbnand_request(mbnand, rq);

	return ret;
}

/**
 * @brief send nand eraseblock request via mailbox
 *
 * @param[in] mbnand - mbnand info
 * @param[in] page - the 1st page address of block to erase
 * @param[out] status - nand status after erase
 *
 * @return 
 *   if success return 0, otherwise return -1.
 */
static int do_nand_eraseblock(struct p4a_mbnand_info* mbnand, int page, int *status)
{
	struct mreqb *rq;
	struct mbnand_eraseblock_arg *arg;
	int ret;

	rq = get_mbnand_request(mbnand, MB_NAND_ERASEBLOCK);

	arg = (struct mbnand_eraseblock_arg*)rq->extra_data;
	arg->page = page;

	ret = send_mbnand_request(mbnand, rq);
	if (ret == 0) {
		if (status)
			*status = arg->status;
	}

	put_mbnand_request(mbnand, rq);

	return ret;
}


/* mtd functions */

static uint8_t p4a_mbnand_read_byte(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_mbnand_info *mbnand = chip->priv;
	uint8_t ret;

	ret = *(uint8_t*)(mbnand->data_buf + mbnand->buf_off);
	mbnand->buf_off++;

	return ret;
}

static u16 p4a_mbnand_read_word(struct mtd_info *mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_mbnand_info *mbnand = chip->priv;
	u16 ret;

	ret = *(u16*)(mbnand->data_buf + mbnand->buf_off);
	mbnand->buf_off += 2;

	return ret;
}

static void p4a_mbnand_write_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_mbnand_info *mbnand = chip->priv;
	u16 col = mbnand->buf_off;
	int n = mtd->oobsize + mtd->writesize - col;

	n = min_t(int, n, len);

	memcpy(mbnand->data_buf + col, buf, n);

	mbnand->buf_off += n;
}

static void p4a_mbnand_read_buf(struct mtd_info* mtd, uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_mbnand_info *mbnand = chip->priv;
	u16 col = mbnand->buf_off;
	int n = mtd->oobsize + mtd->writesize - col;

	n = min_t(int, n, len);

	memcpy(buf, mbnand->data_buf + col, n);

	mbnand->buf_off += n;
}

static int p4a_mbnand_verify_buf(struct mtd_info *mtd, const uint8_t *buf, int len)
{
	return 0;
}

/* chip select control, @chip : -1, de-active chip */
static void p4a_mbnand_select_chip(struct mtd_info* mtd, int chip)
{
	struct nand_chip *this = mtd->priv;
	struct p4a_mbnand_info *mbnand = this->priv;

	do_nand_chipselect(mbnand, chip);
}

static int p4a_mbnand_waitfunc(struct mtd_info* mtd, struct nand_chip *this)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_mbnand_info *mbnand = chip->priv;

	if (this->state == FL_WRITING || this->state == FL_ERASING) {
		return mbnand->nand_status;
	}

	return 0;
}

static int p4a_mbnand_dev_ready(struct mtd_info *mtd)
{
	return 1;
}

/* nand command control */
static	void p4a_mbnand_cmdfunc(struct mtd_info *mtd, unsigned command, int column, int page_addr)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_mbnand_info *mbnand = chip->priv;

	dev_dbg(&mbnand->pdev->dev, "%s (cmd = 0x%x, col = 0x%x, page = 0x%x)\n", __FUNCTION__,
				command, column, page_addr);

	switch (command) {
	case NAND_CMD_READOOB:
		column += mtd->writesize;
		/* fall through */
	case NAND_CMD_READ0:
		mbnand->buf_off = 0;
		do_nand_readpage(mbnand,
							page_addr,
							column,
							mbnand->data_buf, 
							mtd->writesize + mtd->oobsize - column);
		break;

	case NAND_CMD_SEQIN:
		mbnand->buf_off = 0;
		mbnand->seqin_column = column;
		mbnand->seqin_page = page_addr;
		break;
	case NAND_CMD_PAGEPROG:
		do_nand_writepage(mbnand, 
							mbnand->seqin_page,
							mbnand->seqin_column,
							mbnand->data_buf,
							mbnand->buf_off,
							&mbnand->nand_status);
		break;

	case NAND_CMD_ERASE1:
		do_nand_eraseblock(mbnand, page_addr, &mbnand->nand_status);
		break;
	case NAND_CMD_ERASE2:
		break;

	case NAND_CMD_READID:
		mbnand->buf_off = 0;
		do_nand_readid(mbnand, mbnand->data_buf, MAX_NAND_ID_LEN);
		break;

	case NAND_CMD_STATUS:
		mbnand->buf_off = 0;
		mbnand->data_buf[0] = NAND_STATUS_WP | NAND_STATUS_READY;	/* not protected, and ready */
		break;
	case NAND_CMD_RESET:
		break;

	default:
		dev_err(&mbnand->pdev->dev, "Non-supportd nand command (%d)\n", command);
		break;
	};

}

/* device management functions */

static int p4a_mbnand_probe(struct platform_device *pdev)
{
	struct p4a_mbnand_platdata *bd;
	struct p4a_mbnand_info *mbnand;
	struct mtd_info *mtd;
	struct nand_chip *this;
	int err = 0;

	bd = pdev->dev.platform_data;
	if (!bd) {
		dev_err(&pdev->dev, "no platform data specified!\n");
		return -ENODEV;
	}

	mbnand = kzalloc(DATABUF_ALIGN(sizeof(struct p4a_mbnand_info)) + \
					NAND_MAX_PAGESIZE + NAND_MAX_OOBSIZE, 
					GFP_KERNEL);
	if (!mbnand) 
		return -ENOMEM;

	mbnand->data_buf = (uint8_t*)(mbnand + 1);
	mbnand->data_buf = (uint8_t*)(DATABUF_ALIGN((unsigned long)mbnand->data_buf));
	mbnand->pdev = pdev;

	/* chain structures */
	this = &mbnand->chip;
	mtd = &mbnand->mtd;
	mtd->priv = this;
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = &pdev->dev;
	mtd->name = MB_NAND_NAME;

	this->priv = mbnand;
	this->dev_ready = p4a_mbnand_dev_ready;
	this->cmdfunc = p4a_mbnand_cmdfunc;
	this->waitfunc = p4a_mbnand_waitfunc;
	this->select_chip = p4a_mbnand_select_chip;
	this->read_byte = p4a_mbnand_read_byte;
	this->read_word = p4a_mbnand_read_word;
	this->read_buf = p4a_mbnand_read_buf;
	this->write_buf = p4a_mbnand_write_buf;
	this->verify_buf = p4a_mbnand_verify_buf;

	this->ecc.mode = NAND_ECC_NONE;

	mbnand_requests_prealloc(mbnand);

	mbnand->flashdev_table = bd->devs;
	mbnand->parts = bd->partitions;
	mbnand->nr_parts = bd->nr_parts;

	if (nand_scan_ident(mtd, 1, mbnand->flashdev_table)) {
		err = -ENODEV;
		goto _scan_ident_failed;
	}

	if (nand_scan_tail(mtd)) {
		err = -ENODEV;
		goto _scan_tail_failed;
	}

	if (mbnand->nr_parts) {
		add_mtd_partitions(mtd, mbnand->parts, mbnand->nr_parts);

	} else {
		/* parse cmdline to get partitions */
		mbnand->nr_parts = parse_mtd_partitions(mtd, part_probes, &mbnand->parts, 0);
		if (mbnand->nr_parts > 0)
			add_mtd_partitions(mtd, mbnand->parts, mbnand->nr_parts);
		else
			add_mtd_device(mtd);
	}

	platform_set_drvdata(pdev, mbnand);

	return 0;

_scan_tail_failed:
_scan_ident_failed:
	mbnand_requests_free(mbnand);

	kfree(mbnand);

	return err;
}

static int p4a_mbnand_remove(struct platform_device *pdev)
{
	struct p4a_mbnand_info *mbnand = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	nand_release(&mbnand->mtd);
	mbnand_requests_free(mbnand);

	kfree(mbnand);

	return 0;
}

/* PM Support */
#ifdef CONFIG_PM

static int p4a_mbnand_suspend(struct platform_device *dev, pm_message_t pm)
{
	return 0;
}

static int p4a_mbnand_resume(struct platform_device *dev)
{
	return 0;
}

#else
#define p4a_mbnand_suspend NULL
#define p4a_mbnand_resume NULL
#endif

/* driver registration */

static struct platform_driver p4a_mb_nand_driver = {
	.probe		= p4a_mbnand_probe,
	.remove		= p4a_mbnand_remove,
	.suspend	= p4a_mbnand_suspend,
	.resume		= p4a_mbnand_resume,
	.driver = {
		.name = MB_NAND_NAME,
		.owner = THIS_MODULE,
	},
};

static int __init p4a_mbnand_init(void)
{
	printk("P4A Mailbox NAND Driver, (c) 2013 Innofidei Inc.\n");

	return platform_driver_register(&p4a_mb_nand_driver);
}

static void __exit p4a_mbnand_exit(void)
{
	platform_driver_unregister(&p4a_mb_nand_driver);
}

module_init(p4a_mbnand_init);
module_exit(p4a_mbnand_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("jimmy.li <lizhengming@innofidei.com>");
MODULE_DESCRIPTION("Mailbox based NAND driver on innofidei P4A platform");

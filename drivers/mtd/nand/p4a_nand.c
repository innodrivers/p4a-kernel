/*
 * drivers/mtd/nand/p4a_nand.c
 *
 *  based on innofidei if30x nand driver.
 *  Copyright (C) 2008 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/slab.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/mtd/mtd.h>
#include <linux/mtd/nand.h>
#include <linux/mtd/partitions.h>

#include <asm/io.h>
#include <asm/sizes.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/clk.h>

#include <mach/hardware.h>
#include <mach/p4a-regs.h>
#include <mach/p4a_nand.h>

#define DRV_NAME	"p4a-nand"

#define MAX_PAGECACHE_SIZE		(NAND_MAX_PAGESIZE + NAND_MAX_OOBSIZE)

struct p4a_nand_host {
	struct nand_chip	chip;
	struct mtd_info		mtd;

	struct device *dev;
	struct clk *clk;
	struct resource *res;
	void __iomem *base;		//nand controller registers base address
	struct p4a_nand_platdata *pdata;

	void *dma_virt;
	dma_addr_t dma_phys;
	int rOffset, wOffset;

	/* related to the page program(SEQIN, PAGEPROG) command */
	unsigned int last_cmd;
	unsigned int column;
	unsigned int page_addr;
};

/*
 * P4A NFC Command Queue command
 */
#define		CMDQ_SET_CMD		(0x1)
#define		CMDQ_SET_ADDR		(0x2)
#define		CMDQ_SET_IADDR		(0x3)	//auto increasing address
#define		CMDQ_SINGLE_WR		(0x4)
#define		CMDQ_SINGLE_RD		(0x5)
#define		CMDQ_RUN_DMA		(0x6)
#define		CMDQ_WAIT_RDY		(0x7)
#define		CMDQ_CHECK_STATUS	(0x8)
#define		CMDQ_END_QUEUE		(0xF)

//CMDQ_RUN_DMA command parameter
#define		CMD_DMA_512BYTES		(0<<0)
#define		CMD_DMA_16BYTES			(1<<0)
#define		CMD_DMA_2048BYTES		(2<<0)
#define		CMD_DMA_64BYTES			(3<<0)

#define		CMD_DMA_WRITE				(0<<2)
#define		CMD_DMA_READ				(1<<2)

#define		CMD_DMA_SPAREBUFF_DISABLE	(0<<3)
#define		CMD_DMA_SPAREBUFF_ENABLE	(1<<3)

#define		CMD_DMA_ECC_ON			(0<<4)
#define		CMD_DMA_ECC_OFF			(1<<4)

typedef enum {
	NRET_NOERR = 0,
	NRET_ERROR,
	NRET_TIMEOUT,
}nfc_status_t;

static inline void p4a_nfc_writel(struct p4a_nand_host *host, int offset, u32 val)
{
	writel(val, host->base + offset);
}

static inline u32 p4a_nfc_readl(struct p4a_nand_host *host, int offset)
{
	return readl(host->base + offset);
}

static inline void p4a_nfc_clearfifo(struct p4a_nand_host *host)
{
	p4a_nfc_writel(host, NFC_RDATA, NRDR_DATA_VALID);
}

/*
 * status check
 */
typedef uint8_t wait_mode_t;
typedef uint32_t wait_cond_t;

#define WAITMODE_AND		((wait_mode_t)0)	// all bits must be set
#define WAITMODE_OR			((wait_mode_t)1)	// any bit must be set

static int wait_condition(struct p4a_nand_host *host, wait_cond_t cond, wait_mode_t	mode, int timeout)
{
	int ret = NRET_NOERR;
	unsigned long nsr;

	while (timeout--) {
		nsr = p4a_nfc_readl(host, NFC_STATUS);

		switch(mode) {
			case WAITMODE_OR:
			{
				if(cond & nsr)
					goto cond_meet;
				break;
			}
			case WAITMODE_AND:
			default:
			{
				if((cond & nsr) == cond)
					goto cond_meet;
				break;
			}
		}
		udelay(1);
	}

	dev_warn(host->dev, "wait 0x%x in NSR to be set timeout!\n", cond);

	ret = NRET_TIMEOUT;
cond_meet:
	return ret;
}

#define wait_cmdQ_done(host)		wait_condition(host, NSR_CMDQ_DONE, WAITMODE_AND, 4000)
#define wait_device_ready(host)		wait_condition(host, NSR_DEV_RDY, WAITMODE_AND, 4000)

typedef struct nfc_cmd_entry {
	union {
		u32 value;
		struct {
			u32	cmd_param:8;
			u32	cmd_type:4;
			u32	reserved:20;

		};
	};
}nfc_cmd_entry_t;

static int p4a_nfc_send_cmdQ(struct p4a_nand_host *host, struct nfc_cmd_entry* entry, int num)
{
	int i;
	int ret = 0;

	// push the command input command queue
	for(i=0; i<num; i++) {
		p4a_nfc_writel(host, NFC_CMDQ_ENTRY, entry[i].value);
	}
	
	// set the command queue counter, and start execute it.
	p4a_nfc_writel(host, NFC_CMDQ_CTRL, NCQCR_SET_LOOP(1));
	
	ret = wait_cmdQ_done(host);
	if (ret) {
		dev_err(host->dev, "wait command queue done timeout!\n");
	}
	
	return ret;
}

static int p4a_nfc_read_id(struct p4a_nand_host *host)
{
	struct nfc_cmd_entry cmds[10];
	int idx;
	int ret;

	dev_dbg(host->dev, "read ID\n");

	memset(cmds, 0, sizeof(struct nfc_cmd_entry) * 10);
	
	idx = 0;
	cmds[idx].cmd_param = NAND_CMD_READID;
	cmds[idx++].cmd_type = CMDQ_SET_CMD;

	cmds[idx].cmd_param = 0;
	cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	
	cmds[idx++].cmd_type = CMDQ_SINGLE_RD;
	cmds[idx++].cmd_type = CMDQ_SINGLE_RD;
	cmds[idx++].cmd_type = CMDQ_SINGLE_RD;
	cmds[idx++].cmd_type = CMDQ_SINGLE_RD;
	cmds[idx++].cmd_type = CMDQ_SINGLE_RD;
	cmds[idx++].cmd_type = CMDQ_SINGLE_RD;
	cmds[idx++].cmd_type = CMDQ_END_QUEUE;

	p4a_nfc_clearfifo(host);
	
	BUG_ON(idx > ARRAY_SIZE(cmds));
	ret = p4a_nfc_send_cmdQ(host, &cmds[0], idx);

	return ret;
}

static int p4a_nand_read_status(struct p4a_nand_host *host)
{
	struct nfc_cmd_entry cmds[5];
	int idx = 0;
	int ret;

	dev_dbg(host->dev, "read status!\n");

	memset(cmds, 0, sizeof(struct nfc_cmd_entry) * 5);
	
	cmds[idx].cmd_param = NAND_CMD_STATUS;
	cmds[idx++].cmd_type = CMDQ_SET_CMD;
	
	cmds[idx++].cmd_type = CMDQ_SINGLE_RD;
	cmds[idx++].cmd_type = CMDQ_END_QUEUE;

	p4a_nfc_clearfifo(host);

	BUG_ON(idx > ARRAY_SIZE(cmds));
	ret = p4a_nfc_send_cmdQ(host, &cmds[0], idx);

	return ret;
}

static int p4a_nand_reset(struct p4a_nand_host *host)
{
	struct nfc_cmd_entry cmds[5];
	int idx = 0;
	int ret;
	
	dev_dbg(host->dev, "nand reset!\n");
	memset(cmds, 0, sizeof(struct nfc_cmd_entry) * 5);
	
	cmds[idx].cmd_param = NAND_CMD_RESET;
	cmds[idx++].cmd_type = CMDQ_SET_CMD;
	
	cmds[idx++].cmd_type = CMDQ_WAIT_RDY;
	
	cmds[idx++].cmd_type = CMDQ_END_QUEUE;
	
	BUG_ON(idx > ARRAY_SIZE(cmds));
	ret = p4a_nfc_send_cmdQ(host, &cmds[0], idx);

	return ret;
}

static int p4a_nfc_erase_block(struct p4a_nand_host *host, int page_addr)
{
	int isLargePage = (host->mtd.writesize > 512) ? 1 : 0;
	struct nfc_cmd_entry cmds[10];
	int idx = 0;
	int ret;

	dev_dbg(host->dev, "erase block (page_addr = 0x%x)\n", page_addr);

	memset(cmds, 0, sizeof(struct nfc_cmd_entry) * 10);

	cmds[idx].cmd_param = NAND_CMD_ERASE1;
	cmds[idx++].cmd_type = CMDQ_SET_CMD;

	cmds[idx].cmd_param = page_addr & 0xFF;
	cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	
	cmds[idx].cmd_param = (page_addr >> 8) & 0xFF;
	cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	
	if ((!isLargePage && host->mtd.size > 0x2000000) || (isLargePage && host->mtd.size >= 0x10000000)) {
		// small page, device size greater than 32MByte
		// or large page, device size equal or greater than 256MByte
		cmds[idx].cmd_param = (page_addr >> 16) & 0xFF;
		cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	}
	
	cmds[idx].cmd_param = NAND_CMD_ERASE2;
	cmds[idx++].cmd_type = CMDQ_SET_CMD;
		
	cmds[idx++].cmd_type = CMDQ_WAIT_RDY;
	cmds[idx++].cmd_type = CMDQ_CHECK_STATUS;
	
	cmds[idx++].cmd_type = CMDQ_END_QUEUE;
	
	BUG_ON(idx > ARRAY_SIZE(cmds));
	ret = p4a_nfc_send_cmdQ(host, &cmds[0], idx);
	
	return 0;
}

static int p4a_nfc_page_program(struct p4a_nand_host *host, int column, int page_addr)
{
	int isLargePage = (host->mtd.writesize > 512) ? 1 : 0;
	struct nfc_cmd_entry cmds[15];
	int idx = 0;
	int ret;

	dev_dbg(host->dev, "page program (page_addr=0x%x, column=%d)\n", page_addr, column);

	BUG_ON(column!=0 && column!=host->mtd.writesize);
	
	memset(cmds, 0, sizeof(struct nfc_cmd_entry) * 15);

	if(!isLargePage) {
		if(column >= 512)	//access spare area
			cmds[idx].cmd_param = NAND_CMD_READOOB;
		else
			cmds[idx].cmd_param = NAND_CMD_READ0;
		cmds[idx++].cmd_type = CMDQ_SET_CMD;
	}

	cmds[idx].cmd_param = NAND_CMD_SEQIN;
	cmds[idx++].cmd_type = CMDQ_SET_CMD;

	// addressing column
	cmds[idx].cmd_param = column & 0xFF;
	cmds[idx++].cmd_type = CMDQ_SET_ADDR;	
	if (isLargePage) {	
		cmds[idx].cmd_param = (column >>8) & 0xFF;
		cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	}
	
	// addressing row
	cmds[idx].cmd_param = page_addr & 0xFF;
	cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	
	cmds[idx].cmd_param = (page_addr >> 8) & 0xFF;
	cmds[idx++].cmd_type = CMDQ_SET_ADDR;

	if ((!isLargePage && host->mtd.size > 0x2000000) || (isLargePage && host->mtd.size >= 0x10000000)) {
		// small page, device size greater than 32MByte
		// or large page, device size equal or greater than 256MByte
		cmds[idx].cmd_param = (page_addr >> 16) & 0xFF;
		cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	}

	host->wOffset = 0;
	if (isLargePage) {
		if (column >= 2048) {	
			// write spare area
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_64BYTES | CMD_DMA_WRITE) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
			host->wOffset = 2048;
		}else {	
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_2048BYTES | CMD_DMA_WRITE) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_64BYTES | CMD_DMA_WRITE) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
		}
	
	}else {
		if (column >= 512) {
			//write spare area
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_16BYTES | CMD_DMA_WRITE) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
			host->wOffset = 512;
		}else {
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_512BYTES | CMD_DMA_WRITE) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_16BYTES | CMD_DMA_WRITE) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
		}
	
	}

	cmds[idx].cmd_param = NAND_CMD_PAGEPROG;
	cmds[idx++].cmd_type = CMDQ_SET_CMD;

	cmds[idx++].cmd_type = CMDQ_WAIT_RDY;
	
	cmds[idx++].cmd_type = CMDQ_CHECK_STATUS;
	
	cmds[idx++].cmd_type = CMDQ_END_QUEUE;
	
	/* set dma start address */
	p4a_nfc_writel(host, NFC_DMA_ADDR, host->dma_phys + host->wOffset);

	BUG_ON(idx > ARRAY_SIZE(cmds));
	ret = p4a_nfc_send_cmdQ(host, &cmds[0], idx);

	dev_dbg(host->dev, "NSR = 0x%08x\n", p4a_nfc_readl(host, NFC_STATUS));

	return ret;
}

static int p4a_nfc_page_read(struct p4a_nand_host *host, int column, int page_addr)
{
	int isLargePage = (host->mtd.writesize > 512) ? 1 : 0;
	struct nfc_cmd_entry cmds[15];
	int idx;
	int ret;

	host->rOffset = column;
	
	BUG_ON(column!=0 && column!=host->mtd.writesize);

	dev_dbg(host->dev, "read page 0x%x, with column=%d\n", page_addr, column);

	memset(cmds, 0, sizeof(struct nfc_cmd_entry) * 15);

	idx = 0;
	if(!isLargePage && column >= 512)
		cmds[idx].cmd_param = NAND_CMD_READOOB;
	else
		cmds[idx].cmd_param = NAND_CMD_READ0;
	cmds[idx++].cmd_type = CMDQ_SET_CMD;
	
	// column address cycle(s)
	cmds[idx].cmd_param = column & 0xFF;
	cmds[idx++].cmd_type = CMDQ_SET_ADDR;	
	if(isLargePage) {
		cmds[idx].cmd_param = (column >> 8) & 0xFF;
		cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	}

	// row address cycles
	cmds[idx].cmd_param = page_addr & 0xFF;
	cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	
	cmds[idx].cmd_param = (page_addr >> 8) & 0xFF;
	cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	
	if ((!isLargePage && host->mtd.size > 0x2000000) || (isLargePage && host->mtd.size >= 0x10000000)) {
		// small page, device size greater than 32MByte
		// or large page, device size equal or greater than 256MByte
		cmds[idx].cmd_param = (page_addr >> 16) & 0xFF;
		cmds[idx++].cmd_type = CMDQ_SET_ADDR;
	}
	
	if (isLargePage) {
		cmds[idx].cmd_param = NAND_CMD_READSTART;
		cmds[idx++].cmd_type = CMDQ_SET_CMD;
	}
	
	cmds[idx++].cmd_type = CMDQ_WAIT_RDY;

	if (isLargePage) {
		if (column >= 2048) {
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_64BYTES |CMD_DMA_READ) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
			host->rOffset -= 2048;

		}else {
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_2048BYTES | CMD_DMA_READ) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
		
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_64BYTES | CMD_DMA_READ) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
		}

	} else {
		if (column >= 512) {
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_16BYTES |CMD_DMA_READ) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
			host->rOffset -= 512;

		}else {
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_512BYTES | CMD_DMA_READ) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
		
			cmds[idx].cmd_param = (CMD_DMA_ECC_OFF | CMD_DMA_16BYTES | CMD_DMA_READ) ;
			cmds[idx++].cmd_type = CMDQ_RUN_DMA;
		}

	}

	cmds[idx++].cmd_type = CMDQ_END_QUEUE;
	
	/* set dma start address */
	p4a_nfc_writel(host, NFC_DMA_ADDR, host->dma_phys);
		
	BUG_ON(idx > ARRAY_SIZE(cmds));
	ret = p4a_nfc_send_cmdQ(host, &cmds[0], idx);
	
	return ret;
}

static uint8_t p4a_nand_read_byte(struct mtd_info* mtd)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_nand_host *host = chip->priv;
	u32 data;

	data = p4a_nfc_readl(host, NFC_RDATA);
	dev_dbg(host->dev, "read byte 0x%x\n", data);

	/* if data valid then return valid data, otherwise return 0. */
	return (data & NRDR_DATA_VALID) ? (data & NRDR_DATA_MASK) : 0;
}

static void p4a_nand_write_buf(struct mtd_info* mtd, const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_nand_host *host = chip->priv;
	
	memcpy(host->dma_virt + host->wOffset, buf, len);
	host->wOffset += len;
}


static void p4a_nand_read_buf(struct mtd_info* mtd, uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_nand_host *host = chip->priv;
	
	memcpy(buf, host->dma_virt + host->rOffset, len);
	host->rOffset += len;
}

static int p4a_nand_verify_buf(struct mtd_info* mtd, const uint8_t *buf, int len)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_nand_host *host = chip->priv;
	int i, offset=host->rOffset;

	for (i=0; i<len; i++)
		if (buf[i] != ((uint8_t*)host->dma_virt)[offset+i])
			return -EFAULT;

	host->rOffset += len;
	return 0;
}

static void p4a_nand_select_chip(struct mtd_info* mtd, int chipnr)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_nand_host *host = chip->priv;

	switch(chipnr){
	case -1:	// deselect chip
		p4a_nfc_writel(host, NFC_CE, 0x1);
		break;
	case 0:
	case 1:
	case 2:
	case 3:
		//p4a support 4 devices
		p4a_nfc_writel(host, NFC_CE, (chipnr << 1));
		break;
	default:
		BUG();
	}
}

static int p4a_nand_dev_ready(struct mtd_info* mtd)
{
	return 1;
}

static void p4a_nand_command(struct mtd_info* mtd, unsigned command, int column, int page_addr)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_nand_host *host = chip->priv;

	dev_dbg(host->dev, "command: 0x%x at address: 0x%x, column:0x%x\n", 
		command, page_addr, column);

	switch (command) {
	case NAND_CMD_READOOB:
		column += mtd->writesize;
	case NAND_CMD_READ0:	
		p4a_nfc_page_read(host, column, page_addr);
		break;
	case NAND_CMD_SEQIN:
		host->last_cmd = command;
		host->column = column;
		host->page_addr = page_addr;

		memset(host->dma_virt, 0xFF, MAX_PAGECACHE_SIZE);	
		host->wOffset = column;
		break;
	case NAND_CMD_PAGEPROG:
		p4a_nfc_page_program(host, host->column, host->page_addr);
		break;
	case NAND_CMD_ERASE1:
		p4a_nfc_erase_block(host, page_addr);
		break;
	case NAND_CMD_ERASE2:
		break;
	case NAND_CMD_READID:
		p4a_nfc_read_id(host);
		break;
	case NAND_CMD_STATUS:
		p4a_nand_read_status(host);
		break;
	case NAND_CMD_RESET:
		p4a_nand_reset(host);
		break;
	default:
		dev_warn(host->dev,"unknown command %d.\n", command);
		break;
	}
}

// wait for command done, applies to erase and program
// return nand status register value
static int p4a_nand_waitfunc(struct mtd_info* mtd, struct nand_chip *this)
{
	struct nand_chip *chip = mtd->priv;
	struct p4a_nand_host *host = chip->priv;
	int status;
	
	if (NRET_NOERR != wait_device_ready(host)) {
		dev_err(host->dev, "wait device ready timeout!\n");
	}
	
	status = (p4a_nfc_readl(host, NFC_STATUS) & NSR_CUR_STATUS_MASK) >> NSR_CUR_STATUS_SHIFT;

	dev_dbg(host->dev, "waitfunc: status=0x%x\n", status);

	return status;
}


#ifdef CONFIG_MTD_PARTITIONS
const char *part_probes[] = { "cmdlinepart", NULL };
#endif


/*
 * Probe for the NAND device.
 */
static int __init p4a_nand_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct resource *iomem;
	struct p4a_nand_host *host;
	struct mtd_info *mtd;
	struct nand_chip *chip;
	struct p4a_nand_platdata *pdata;
	int ret;

#ifdef CONFIG_MTD_PARTITIONS
	struct mtd_partition *partitions = NULL;
	int num_partitions = 0;
#endif

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(dev, "no platform data specifed.\n");
		return -ENODEV;
	}

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!iomem) {
		dev_err(dev, "no memory specified!\n");
		return -ENOMEM;
	}

	/* Allocate memory for the device structure (and zero it) */
	host = kzalloc(sizeof(struct p4a_nand_host), GFP_KERNEL);
	if (!host) {
		dev_err(dev, "failed to alloc host.\n");
		return -ENOMEM;
	}
	host->dev = dev;

	host->clk = clk_get(dev, "NAND_CLK");
	if (IS_ERR(host->clk)) {
		dev_err(dev, "failed to get clock.\n");
		ret = PTR_ERR(host->clk);
		goto err_clk;
	}

	host->res = request_mem_region(iomem->start, resource_size(iomem), DRV_NAME);
	if (!host->res) {
		dev_err(dev, "cannot request memory  region.\n");
		ret = -EBUSY;
		goto err_request;
	}

	host->base = ioremap(iomem->start, resource_size(iomem));
	if (!host->base) {
		dev_err(dev, "failed to remap registers.\n");
		ret = -ENOMEM;
		goto err_ioremap;
	}

	host->dma_virt = dma_alloc_coherent(dev, MAX_PAGECACHE_SIZE, &host->dma_phys, GFP_KERNEL);
	if(!host->dma_virt){
		ret = -ENOMEM;
		goto err_dma_alloc;
	}
	host->pdata = pdata;
	
	/* chain structures */
	chip = &host->chip;
	mtd = &host->mtd;

	mtd->priv = chip;
	mtd->owner = THIS_MODULE;
	mtd->dev.parent = dev;
	mtd->name = DRV_NAME; 

	chip->priv = host;

	chip->waitfunc = p4a_nand_waitfunc;
	chip->select_chip = p4a_nand_select_chip;
	chip->dev_ready = p4a_nand_dev_ready;
	chip->cmdfunc = p4a_nand_command;
	chip->read_byte = p4a_nand_read_byte;
	chip->read_buf = p4a_nand_read_buf;
	chip->write_buf = p4a_nand_write_buf;
	chip->verify_buf = p4a_nand_verify_buf;	

	chip->ecc.mode = NAND_ECC_NONE;
	chip->chip_delay = 20;		/* 20us command delay time */

	/* enable nand clock before access */
	clk_enable(host->clk);

	/* Scan to find existance of the device */
	if (nand_scan(mtd, 1)) {
		dev_err(dev, "nand scan failed\n");
		ret = -ENXIO;
		goto err_scan;
	}

#ifdef CONFIG_MTD_PARTITIONS
#ifdef CONFIG_MTD_CMDLINE_PARTS
	/* get partition table from cmdline */
	num_partitions = parse_mtd_partitions(mtd, part_probes, &partitions, 0);
	if (num_partitions <= 0){
#else
	{
#endif
		/* get partition table from board defined */
		if(host->pdata->partitions && host->pdata->nr_parts > 0) {
			partitions = host->pdata->partitions;
			num_partitions = host->pdata->nr_parts;
		}
	}

	if ((!partitions) || (num_partitions == 0)) {
		dev_warn(dev, "No parititions defined.\n");
	}
#endif

	if (num_partitions > 0) {
		ret = add_mtd_partitions(mtd, partitions, num_partitions);
	} else {
		ret = add_mtd_device(mtd);
	}

	platform_set_drvdata(pdev, host);

	return 0;

err_scan:
	clk_disable(host->clk);
err_dma_alloc:
	iounmap(host->base);
err_ioremap:
	release_mem_region(iomem->start, resource_size(iomem));
err_request:
	clk_put(host->clk);
err_clk:
	kfree(host);

	return ret;
}

/*
 * Remove a NAND device.
 */
static int __devexit p4a_nand_remove(struct platform_device *pdev)
{
	struct p4a_nand_host *host = platform_get_drvdata(pdev);
	struct mtd_info *mtd = &host->mtd;

	platform_set_drvdata(pdev, NULL);

	nand_release(mtd);

	dma_free_coherent(&pdev->dev, MAX_PAGECACHE_SIZE, host->dma_virt, host->dma_phys);
	release_mem_region(host->res->start, resource_size(host->res));
	iounmap(host->base);

	clk_disable(host->clk);
	clk_put(host->clk);
	
	kfree(host);

	return 0;
}

#ifdef CONFIG_PM
static int p4a_nand_suspend(struct platform_device* pdev, pm_message_t pm)
{
	return 0;
}

static int p4a_nand_resume(struct platform_device* pdev)
{
	return 0;
}
#else
#define p4a_nand_suspend	NULL
#define p4a_nand_resume	NULL
#endif

static struct platform_driver p4a_nand_driver = {
	.probe		= p4a_nand_probe,
	.remove		= p4a_nand_remove,
	.suspend	= p4a_nand_suspend,
	.resume		= p4a_nand_resume,
	.driver		= {
		.name	= DRV_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init p4a_nand_init(void)
{
	printk("P4A NAND Driver, Copyright(c) Innofidei Inc.\n");

	return platform_driver_register(&p4a_nand_driver);
}

static void __exit p4a_nand_exit(void)
{
	platform_driver_unregister(&p4a_nand_driver);
}

module_init(p4a_nand_init);
module_exit(p4a_nand_exit);

MODULE_AUTHOR("jimmy.li <lizhengming@innofidei.com>");
MODULE_DESCRIPTION("P4A NAND Controller Driver");
MODULE_LICENSE("GPL");

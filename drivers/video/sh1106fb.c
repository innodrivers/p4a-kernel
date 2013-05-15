/*
 * Framebuffer driver for SH1106 132x64 Dot Matrix OLED.
 *
 * Copyright (C) 2012 Innofidei Inc. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <asm/uaccess.h>
#include <linux/i2c.h>
#include "sh1106fb.h"


#define DRV_NAME "sh1106-i2c"
#define OLED_FB_NAME "sh1106-fb"

#define LED_WIDTH  132
#define LED_HEIGHT 64
#define PAGE_VALUE 8

#define _ALIGN_UP(x, size)    (((x)+((size)-1)) & (~((size)-1)))
#define _ALIGN_DOWN(x, size)  ((x) & (~((size)-1)))



/*
 * @brief Structure containing the framebuffer and layer information.
 */
struct sh1106fb_info {
	struct fb_info *info;
	unsigned long pseudo_palette[16];
	unsigned int palette_size;	/* palette entry count */
	struct i2c_client *led_i2c_client;
	int bpp;
	struct sh1106_drawarea update_area;
};

#define OLEDCMD (0)
#define OLEDDATA (0x40)
struct oled_cmd {
	char cmd[3];
	char len;
};

static int sh1106fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info);
static int sh1106fb_set_par(struct fb_info *info);
static int sh1106fb_ioctl(struct fb_info *info, unsigned int cmd, unsigned long arg);

/* Internal functions */
static void _set_fix(struct fb_info *info);
static int __init _init_fbinfo(struct fb_info *info, struct platform_device *pdev);
static int _map_video_memory(struct fb_info *info);
static void _unmap_video_memory(struct fb_info *info);
static int draw_screen_area(struct sh1106fb_info *info);
static int led_write_cmd(struct sh1106fb_info *info, unsigned char *cmd, int count);
static int led_write_ram(struct sh1106fb_info *info, unsigned char *buf, int count);


static unsigned char data_to_led[64 * 17 + 1];



/*!
 * @brief Framebuffer file operations
 */
static struct fb_ops oled_fb_ops = {
	.owner			= THIS_MODULE,
	.fb_check_var	= sh1106fb_check_var,
	.fb_set_par		= sh1106fb_set_par,
	.fb_ioctl		= sh1106fb_ioctl,
};

static struct oled_cmd initcmd[] = {
	{
		.cmd[0] = OLEDCMD,
		.cmd[1] = 0xE3,			//nop
		.len = 2,
	},
	{
		.cmd[0] = OLEDCMD,
		.cmd[1] = 0xAE,
		.len = 2,
	},
	{
		.cmd[0] = OLEDCMD,	////display divide ratio/osc. freq. mode
		.cmd[1] = 0xd5,
		.cmd[2] = 0x80,
		.len = 3,
	},
	{
		.cmd[0] = OLEDCMD,	 //multiplex ration mode:63
		.cmd[1] = 0xA8,
		.cmd[2] = 0x3F,
		.len = 3,
	},
	{
		.cmd[0] = OLEDCMD,	//Set Display Offset
		.cmd[1] = 0xD3,
		.cmd[2] = 0x00,
		.len = 3,
	},
	{
		.cmd[0] = OLEDCMD,	 //Set Display Start Line
		.cmd[1] = 0x40,
		.len = 2,
	},
	{
		.cmd[0] = OLEDCMD,	//DC-DC Control Mode Set
		.cmd[1] = 0xAD,
		.cmd[2] = 0x8A,
		.len = 3,
	},
	{
		.cmd[0] = OLEDCMD,	 //Set Pump voltage value
		.cmd[1] = 0x32,
		.len = 2,
	},
	{
		.cmd[0] = OLEDCMD,	//Segment Remap     (why reverse?)
		.cmd[1] = 0xA1,
		.len = 2,
	},
	{
		.cmd[0] = OLEDCMD,  //Sst COM Output Scan Direction
		.cmd[1] = 0xC8,
		.len = 2,
	},
	{
		.cmd[0] = OLEDCMD,	//common pads hardware: alternative
		.cmd[1] = 0xDA,
		.cmd[2] = 0x12,
		.len = 3,
	},
	{
		.cmd[0] = OLEDCMD,	//contrast control
		.cmd[1] = 0x81,
		.cmd[2] = 0xBB,
		.len = 3,
	},
	{
		.cmd[0] = OLEDCMD,	//set pre-charge period
		.cmd[1] = 0xD9,
		.cmd[2] = 0x22,
		.len = 3,
	},
	{
		.cmd[0] = OLEDCMD,	//VCOM deselect level mode
		.cmd[1] = 0xDB,
		.cmd[2] = 0x40,
		.len = 3,
	},
	{
		.cmd[0] = OLEDCMD,	  //Set Entire Display On/Off
		.cmd[1] = 0xA4,
		.len = 2,
	},
	{
		.cmd[0] = OLEDCMD,	//Set Normal Display
		.cmd[1] = 0xA6,
		.len = 2,
	},
	{
		.cmd[0] = OLEDCMD,	//Set Display On
		.cmd[1] = 0xAf,
		.len = 2,
	},
	{
		.cmd[0] = OLEDCMD,	//Set Display Start Line
		.cmd[1] = 0x40,
		.len = 2,
	}
};

/**
 * @brief  init the oled hw controller.
 * @param[in]  sh1106
 *
 * @return  Zero on success others on failure
 */
static int oled_init(struct sh1106fb_info *sh1106)
{
	int i;
	int ret = -ENODEV;

	if (unlikely(NULL == sh1106->led_i2c_client)) {
		return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(initcmd); i++) {
		ret = i2c_master_send(sh1106->led_i2c_client, initcmd[i].cmd , initcmd[i].len);

		if (ret < 0) {
			printk("%s %d  have error\n", __func__, __LINE__);
			return -ENODEV;
		}

	}
	return 0;
}
/**
 * @brief check if the fb_var_screeninfo setting is correct. 
 * @param[in] var - Pointer to fb_var_screeninfo to be setting
 *            	info - fb_info
 * @return  Zero on success others on failure
 */
static int sh1106fb_check_var(struct fb_var_screeninfo *var, struct fb_info *info)
{

	struct sh1106fb_info *sh1106 = (struct sh1106fb_info *)info->par;

	if (var->bits_per_pixel != sh1106->bpp) {
		var->bits_per_pixel = sh1106->bpp;
	}

	if (var->xres != LED_WIDTH) {
		var->xres = LED_WIDTH;
	}

	if (var->xres_virtual != var->xres) {
		var->xres_virtual =  var->xres;
	}

	if (var->yres !=  LED_HEIGHT) {
		var->yres = LED_HEIGHT;
	}

	if (var->yres_virtual !=  var->yres) {
		var->yres_virtual = var->yres;
	}


	if (var->xoffset + info->var.xres > info->var.xres_virtual) {
		var->xoffset = info->var.xres_virtual - info->var.xres;
	}

	if (var->yoffset + info->var.yres > info->var.yres_virtual) {
		var->yoffset = info->var.yres_virtual - info->var.yres;
	}

	var->height = -1;
	var->width = -1;
	var->grayscale = 0;

	/* Copy nonstd field to/from sync for fbset usage */
	var->sync |= var->nonstd;
	var->nonstd |= var->sync;

	return 0;
}

/*
 * @brief Alters the hardware state.
 *
 * @param  info -Frame buffer structure that represents a single frame buffer
 *
 * @return Zero on success others on failure
 */
static int sh1106fb_set_par(struct fb_info *info)
{
	unsigned long len;

	_set_fix(info);

	len = info->var.yres_virtual * info->fix.line_length;

	if (len > info->fix.smem_len) {
		if (info->fix.smem_start) {
			_unmap_video_memory(info);
		}

		/* Memory allocation for framebuffer */
		if (_map_video_memory(info)) {
			printk("Unable to allocate fb memory\n");
			return -ENOMEM;
		}
	}

	return 0;
}

/*
 * @brief Ioctl function to support customized ioctl operations.
 *
 * @param info	Framebuffer structure that represents a single frame buffer
 * @param cmd	The command number
 * @param arg	Argument which depends on cmd
 *
 * @return	Negative errno on error, or zero on success.
 */
static int sh1106fb_ioctl(struct fb_info *info, unsigned int cmd,
                         unsigned long arg)
{
	struct sh1106fb_info *sh1106 = (struct sh1106fb_info *)info->par;
	int err = 0;

	switch (cmd) {

	case SH1106FBIO_UPDATE_DRAWAREA:
		if (copy_from_user(&(sh1106->update_area), (u32 __user *)arg, sizeof(struct sh1106_drawarea))) {
			return -EFAULT;
		}

		err = draw_screen_area(sh1106);
		break;

	default:
		err = -EINVAL;
		break;
	}

	return err;
}

static unsigned char get_byte_bit(unsigned char *byte_data, unsigned int offset)
{
	byte_data += offset / 8;
	return !!(*byte_data & (1 << (7 - (offset % 8))));
}

/*
 * @brief 	 draw_screen_area
 *
 * @param[in] info -Frame buffer structure that represents a single frame buffer
 *
 * @return Zero on success others on failure
 */
static int draw_screen_area(struct sh1106fb_info *info)
{
	struct sh1106_drawarea *area = &(info->update_area);
	char *pixel_src;
	unsigned int i, row, col, page_start, page_cnt;
	char cmd[3];
	unsigned char pixel_bit = 0;
	unsigned char pixel_bits = 0;

	unsigned int top_x = area->x;
	unsigned int top_y = area->y;
	unsigned int high = area->height;
	unsigned int width = area->width;
	unsigned int data_led_count = 1;
	int temptopy = top_y;


	if ((top_y >= LED_HEIGHT) || \
	        (top_y + high) >  LED_HEIGHT || \
	        top_x >= LED_WIDTH || \
	        (top_x + width) >  LED_WIDTH || (0 == high) || (0 == width)) {
		return -EINVAL;
	}

	top_y = _ALIGN_DOWN(top_y, PAGE_VALUE);
	high = _ALIGN_UP(high + (temptopy - top_y), PAGE_VALUE);

	//get page address
	pixel_src = info->info->screen_base + top_y * info->info->fix.line_length;
	page_start = top_y / PAGE_VALUE;
	page_cnt = high / PAGE_VALUE;

	for (i = page_start; i < page_start + page_cnt; i++) {

		//set control byte
		cmd[0] = 0x0;
		//set which page for write.
		cmd[1] = 0xb0 + i;
		led_write_cmd(info, cmd, 2);

		//set control byte
		cmd[0] = 0x0;
		//set which column for write from.
		cmd[1] = 0x10 | (top_x >> 4);
		cmd[2] = top_x & 0xF;
		led_write_cmd(info, cmd, 3);

		//prepare pixel data for this page which need to update
		for (col = 0; col < width; col++) {
			pixel_bits = 0;

			for (row = 0; row < PAGE_VALUE; row++) {
				pixel_bit = get_byte_bit(pixel_src + row * info->info->fix.line_length, top_x + col);
				pixel_bits |= pixel_bit << row ;
			}


			data_to_led[data_led_count++] = pixel_bits;
		}

		//set control byte
		data_to_led[0] = 0x40;

		//send page pixel data to led through i2c interface
		led_write_ram(info, &data_to_led[0], width + 1);

		data_led_count = 1;
		pixel_src = pixel_src + PAGE_VALUE * info->info->fix.line_length;
	}

	return 0;
}

/*
 * @brief 	 send oled cmd to i2c.
 *
 * @return Zero on success others on failure
 */
static int led_write_cmd(struct sh1106fb_info *info, unsigned char *cmd, int count)
{
	unsigned int ret;

	if ((count < 1) || (cmd == NULL)) {
		return -EINVAL;
	}

	ret = i2c_master_send(info->led_i2c_client, cmd, count);

	if (ret < 0) {
		return ret;
	}

	return 0;
}
/*
 * @brief 	 send oled data to i2c.
 *
 * @return Zero on success others on failure
 */
static int led_write_ram(struct sh1106fb_info *info, unsigned char *buf, int count)
{
	unsigned int ret;

	if ((count < 1) || (buf == NULL)) {
		return -EINVAL;
	}

	ret = i2c_master_send(info->led_i2c_client, buf, count);

	if (ret < 0) {
		return ret;
	}

	return 0;

}
/*
 * @brief Set fixed framebuffer parameters based on variable settings.
 *
 * @param info	framebuffer information pointer
 * @return	Negative errno on error, or zero on success.
 */
static void _set_fix(struct fb_info *info)
{
	struct fb_fix_screeninfo *fix = &info->fix;
	struct fb_var_screeninfo *var = &info->var;
	struct sh1106fb_info *sh1106 = (struct sh1106fb_info *)info->par;

	if (var->bits_per_pixel >= 16) {
		sh1106->palette_size = 0;
		fix->visual = FB_VISUAL_TRUECOLOR;
	} else {
		sh1106->palette_size = 1 << var->bits_per_pixel;
		fix->visual = FB_VISUAL_PSEUDOCOLOR;
	}

	fix->line_length = (_ALIGN_UP(var->xres_virtual * var->bits_per_pixel, 8)) / 8;	// var->xres_virtual * var->bits_per_pixel / 8
	fix->type = FB_TYPE_PACKED_PIXELS;
	fix->accel = FB_ACCEL_NONE;
	fix->xpanstep = 1;
	fix->ypanstep = 1;
}

/*
 * @brief Initialize framebuffer information structure.
 *
 * @param info	framebuffer information pointer
 * @param pdev	pointer to struct device
 * @return	Negative errno on error, or zero on success.
 */
static int __init _init_fbinfo(struct fb_info *info,
                               struct platform_device *pdev)
{
	struct sh1106fb_info *sh1106 = (struct sh1106fb_info *)info->par;

	info->device = &pdev->dev;
	info->var.activate = FB_ACTIVATE_NOW | FB_ACTIVATE_FORCE;
	info->var.bits_per_pixel = sh1106->bpp;
	info->fbops = &oled_fb_ops;
	info->flags = FBINFO_FLAG_DEFAULT;
	info->pseudo_palette = &sh1106->pseudo_palette;

	return 0;
}

/*
 * @brief Allocate memory for framebuffer.
 *
 * @param info	framebuffer information pointer
 * @return	Negative errno on error, or zero on success.
 */
static int _map_video_memory(struct fb_info *info)
{
	info->fix.smem_len = info->fix.line_length * info->var.yres_virtual;

	info->screen_base = dma_alloc_coherent(info->dev,
	                                    info->fix.smem_len,
										(dma_addr_t *) & info->fix.smem_start,
										GFP_KERNEL);

	if (info->screen_base == 0) {
		printk("Unable to allocate fb memory\n");
		return -EBUSY;
	}

	info->screen_size = info->fix.smem_len;

	/* Clear the screen */
	memset((char *)info->screen_base, 0x0, info->fix.smem_len);

	return 0;
}

/*
 * @brief 			Release 	memory for framebuffer.
 * @param info	framebuffer information pointer
 */
static void _unmap_video_memory(struct fb_info *info)
{
	dma_free_coherent(0, info->fix.smem_len, info->screen_base,
	                  (dma_addr_t) info->fix.smem_start);

	info->screen_base = 0;
	info->fix.smem_start = 0;
	info->fix.smem_len = 0;
}


/*
 * @brief 	Probe routine for the framebuffer driver. It is called during the
 *        driver binding process.
 *
 * @return Appropriate error code to the kernel common code
 */
static int __devinit sh1106fb_probe(struct platform_device *pdev)
{
	struct fb_info *info;
	struct sh1106fb_info *sh1106;
	int ret;

	info = framebuffer_alloc(sizeof(struct sh1106fb_info), &pdev->dev);

	if (NULL == info) {
		return -ENOMEM;
	}

	sh1106 = (struct sh1106fb_info *)info->par;
	sh1106->info = info;
	
	/* init the led param */
	sh1106->bpp = 1;
	info->var.xres = LED_WIDTH;			//_ALIGN_UP(LED_WIDTH, 8);
	info->var.yres = LED_HEIGHT;
	info->var.xres_virtual = info->var.xres;	//_ALIGN_UP(info->var.xres, 8);
	info->var.yres_virtual = info->var.yres;
	info->var.xoffset = 0 ;
	info->var.yoffset = 0 ;

	/* set the i2c param */
	sh1106->led_i2c_client = pdev->dev.platform_data;

	if (_init_fbinfo(info, pdev)) {
		ret = -EINVAL;
		goto failed_free_mem;
	}

	platform_set_drvdata(pdev, info);

	/* init i2c */
	ret = sh1106fb_check_var(&info->var, info);

	if (ret < 0) {
		printk("failed to get suitable mode \n");
		goto failed_free_mem;
	}

	ret = sh1106fb_set_par(info);

	if (ret) {
		goto failed_free_mem;
	}

	oled_init(sh1106);

	ret = register_framebuffer(info);

	if (ret < 0) {
		goto failed_free_mem;
	}

	printk("fb%d: %s fb device registered successfully.\n",
	       info->node, info->fix.id);

	return 0;

failed_free_mem:
	platform_set_drvdata(pdev, NULL);
	framebuffer_release(info);
	return ret;
}

/*
 * @brief Remove routine for the framebuffer driver.
 *
 * @return Appropriate error code to the kernel common code
 */
static int __devexit sh1106fb_remove(struct platform_device *pdev)
{
	struct fb_info *info = (struct fb_info *)platform_get_drvdata(pdev);
	unregister_framebuffer(info);
	_unmap_video_memory(info);

	platform_set_drvdata(pdev, NULL);
	framebuffer_release(info);
	return 0;
}

#ifdef CONFIG_PM
/*
 * Power management hooks. Note that we won't be called from IRQ context,
 * unlike the blank functions above, so we may sleep.
 */

/*
 * @brief Suspends the framebuffer and blanks the screen.
 * Power management support
 */
static int sh1106fb_suspend(struct platform_device *pdev)
{
	return 0;
}

/*
 * @brief Resumes the framebuffer and unblanks the screen.
 * Power management support
 */
static int sh1106fb_resume(struct platform_device *pdev)
{
	return 0;
}

#else
#define sh1106fb_suspend	NULL
#define sh1106fb_resume		NULL
#endif

static struct platform_driver sh1106fb_driver = {
	.probe	= sh1106fb_probe,
	.remove = __devexit_p(sh1106fb_remove),
	.suspend= sh1106fb_suspend,
	.resume = sh1106fb_resume,
	.driver = {
		.name	= OLED_FB_NAME,
		.owner	= THIS_MODULE,
	},
};


static int __devinit sh1106_i2c_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct platform_device *pdev;
	
	pdev = platform_device_alloc(OLED_FB_NAME, -1);

	pdev->dev.platform_data = client;

	return platform_device_add(pdev);
}

static int __devexit sh1106_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id sh1106_i2c_id[] = {
	{DRV_NAME, 0},
	{}
};

static struct i2c_driver sh1106_i2c_driver = {
	.driver = {
		.name = DRV_NAME,
	},
	.probe = sh1106_i2c_probe,
	.remove = __devexit_p(sh1106_i2c_remove),
	.id_table = sh1106_i2c_id,
};

static int __init sh1106fb_init(void)
{
	int ret = 0;

	ret = i2c_add_driver(&sh1106_i2c_driver);

	if (ret < 0) {
		return ret;
	}

	return platform_driver_register(&sh1106fb_driver);
}

static void __exit sh1106fb_exit(void)
{
	platform_driver_unregister(&sh1106fb_driver);
}


module_init(sh1106fb_init);
module_exit(sh1106fb_exit);

MODULE_AUTHOR("Innofidei Inc.");
MODULE_DESCRIPTION("SH1106 OLED framebuffer driver");
MODULE_LICENSE("GPL");


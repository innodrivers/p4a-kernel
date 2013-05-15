/*
 * An i2c driver for the HYM8563 RTC
 * Copyright 2008 Innofidei Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/i2c.h>
#include <linux/bcd.h>
#include <linux/rtc.h>
#include <linux/delay.h>

#define HYM8563_I2C_ADDR	0x51

/* HYM8563 Register Definite */
#define HYM_CNTL1R		0x00
#define HYM_CNTL2R		0x01
#define HYM_SECR		0x02
#define HYM_MINR		0x03
#define HYM_HOURR		0x04
#define HYM_DAYR		0x05
#define HYM_WEEKR		0x06
#define HYM_MONR		0x07
#define HYM_YEARR		0x08
#define HYM_ALM_MINR	0x09
#define HYM_ALM_HOURR	0x0A
#define HYM_ALM_DAYR	0x0B
#define HYM_ALM_WEEKR	0x0C
#define HYM_CLKOUTR		0x0D
#define HYM_TIMERCTLR	0x0E
#define HYM_TIMERCNTR	0x0F

static struct i2c_driver hym8563_driver;

struct hym_time_regs {
	u8	sec;
	u8	min;
	u8	hour;
	u8	day;
	u8	week;
	u8	month;
	u8	year;
};

static int hym8563_get_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct hym_time_regs	tm_regs;
	u8 addr = HYM_SECR;

	struct i2c_msg msgs[2] = {
		{client->addr, 0, 1, &addr},
		{client->addr, I2C_M_RD, sizeof(tm_regs), (u8*)&tm_regs},
	};

	if(i2c_transfer(client->adapter, msgs, 2) != 2) {
		dev_err(&client->dev, "%s: could not read register\n", __FUNCTION__);
		return -EIO;
	}
	
	tm->tm_sec = bcd2bin(tm_regs.sec & 0x7F);
	tm->tm_min = bcd2bin(tm_regs.min & 0x7F);
	tm->tm_hour = bcd2bin(tm_regs.hour & 0x3F);
	tm->tm_mday = bcd2bin(tm_regs.day & 0x3F);
	tm->tm_wday = tm_regs.week & 0x07;
	tm->tm_mon= bcd2bin(tm_regs.month & 0x1F) - 1;
	tm->tm_year = bcd2bin(tm_regs.year);
	if(!(tm_regs.month & 0x80))	// 20XX year
		tm->tm_year += 100;
	return 0;
}

static int hym8563_set_datetime(struct i2c_client *client, struct rtc_time *tm)
{
	struct hym_time_regs tm_regs;
	u8 buffer[8] = {HYM_SECR, 0};
	struct i2c_msg msgs[1] = {
		{client->addr, 0, 8,buffer},
	};

	tm_regs.sec = bin2bcd(tm->tm_sec);
	tm_regs.min = bin2bcd(tm->tm_min);
	tm_regs.hour = bin2bcd(tm->tm_hour);
	tm_regs.day = bin2bcd(tm->tm_mday);
	tm_regs.month = bin2bcd(tm->tm_mon + 1) | 0x80;
	if(tm->tm_year > 100){		//20xx year
		tm_regs.month &= ~0x80;
		tm->tm_year -= 100;
	}
	tm_regs.year = bin2bcd(tm->tm_year);
	tm_regs.week &= ~7;
	tm_regs.week |= tm->tm_wday;

	memcpy((void*)&buffer[1], (void*)&tm_regs, 7);
	if(i2c_transfer(client->adapter, msgs, 1) != 1) {
		dev_err(&client->dev, "%s: could not read register\n", __FUNCTION__);
		return -EIO;	
	}
	

	return 0;
}

static int hym8563_rtc_read_time(struct device *dev, struct rtc_time *tm)
{
	return hym8563_get_datetime(to_i2c_client(dev), tm);
}

static int hym8563_rtc_set_time(struct device *dev, struct rtc_time *tm)
{
	return hym8563_set_datetime(to_i2c_client(dev), tm);
}

static const struct rtc_class_ops hym8563_rtc_ops = {
	.read_time	= hym8563_rtc_read_time,
	.set_time	= hym8563_rtc_set_time,
};

static int hym8563_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	struct rtc_device *rtc;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	rtc = rtc_device_register(hym8563_driver.driver.name,
				  &client->dev, &hym8563_rtc_ops, THIS_MODULE);
	if (IS_ERR(rtc))
		return PTR_ERR(rtc);

	i2c_set_clientdata(client, rtc);

	return 0;

}

static int hym8563_remove(struct i2c_client *client)
{
	struct rtc_device *rtc = i2c_get_clientdata(client);

	if (rtc)
		rtc_device_unregister(rtc);

	return 0;
}

static struct i2c_device_id hym8563_id[] = {
	{ "hym8563", 0 },
	{ }
};

static struct i2c_driver hym8563_driver = {
	.driver		= {
		.name	= "rtc-hym8563",
	},
	.probe = hym8563_probe,
	.remove = hym8563_remove,
	.id_table = hym8563_id,
};

static int __init hym8563_init(void)
{
	return i2c_add_driver(&hym8563_driver);
}

static void __exit hym8563_exit(void)
{
	i2c_del_driver(&hym8563_driver);
}

MODULE_AUTHOR("Jimmy.li<lizhengming@innofidei.com>");
MODULE_DESCRIPTION("HYM8563 RTC driver");
MODULE_LICENSE("GPL");

module_init(hym8563_init);
module_exit(hym8563_exit);

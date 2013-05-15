#
# Makefile for the linux kernel.
#

obj-y		:= io.o irq.o time.o reset.o devices.o

obj-y		+= clock.o clock_data.o iomux.o
obj-y		+= gpio.o
obj-y		+= usb-musb.o

obj-$(CONFIG_P4A_MAILBOX)	+= p4a_mbox.o mailbox.o micproto.o

obj-$(CONFIG_MACH_P4AFPGA)	+= board-p4afpga.o
obj-$(CONFIG_MACH_P4ABU)	+= board-p4abu.o

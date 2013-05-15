#ifndef _SH1106_FB_H
#define _SH1106_FB_H

struct sh1106_drawarea {
	unsigned int x;
	unsigned int y;
	unsigned int height;
	unsigned int width;
};

/* ioctl commands of SH1106 frame buffer device */
#define SH1106FB_IOC_MAGIC	'S'

#define SH1106FBIO_UPDATE_DRAWAREA	_IOW(SH1106FB_IOC_MAGIC, 0x1, struct sh1106_drawarea)

#endif	/* _SH1106_FB_H */

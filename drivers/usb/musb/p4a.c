/*
 * You should have received a copy of the GNU General Public License
 * along with The Inventra Controller Driver for Linux ; if not,
 * write to the Free Software Foundation, Inc., 59 Temple Place,
 * Suite 330, Boston, MA  02111-1307  USA
 *
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/io.h>

#include "musb_core.h"

static irqreturn_t p4a_musb_interrupt(int irq, void *__hci)
{
	unsigned long	flags;
	irqreturn_t	retval = IRQ_NONE;
	struct musb	*musb = __hci;

	spin_lock_irqsave(&musb->lock, flags);

	musb->int_usb = musb_readb(musb->mregs, MUSB_INTRUSB);
	musb->int_tx = musb_readw(musb->mregs, MUSB_INTRTX);
	musb->int_rx = musb_readw(musb->mregs, MUSB_INTRRX);

	if (musb->int_usb || musb->int_tx || musb->int_rx)
		retval = musb_interrupt(musb);

	spin_unlock_irqrestore(&musb->lock, flags);

	return retval;
}

void musb_platform_try_idle(struct musb *musb, unsigned long timeout)
{
}

void musb_platform_enable(struct musb *musb)
{
}

void musb_platform_disable(struct musb *musb)
{
}


int musb_platform_set_mode(struct musb *musb, u8 musb_mode)
{
	u8	devctl = musb_readb(musb->mregs, MUSB_DEVCTL);

	devctl |= MUSB_DEVCTL_SESSION;
	musb_writeb(musb->mregs, MUSB_DEVCTL, devctl);

	return 0;
}

int __init musb_platform_init(struct musb *musb, void *board_data)
{
	int status = 0;

	clk_enable(musb->clock);

	musb->phy_clock = clk_get(musb->controller, "USB_PHY_CLK");
	if (IS_ERR(musb->phy_clock)) {
		status = PTR_ERR(musb->phy_clock);
		dev_err(musb->controller,
			"musb_platform_init get usb phy clock failed with status %d\n", status);
		goto exit0;
	}
	clk_enable(musb->phy_clock);

	usb_nop_xceiv_register();

	musb->xceiv = otg_get_transceiver();
	if (!musb->xceiv) {
		status = -ENODEV;
		goto exit1;
	}
	
	musb->isr = p4a_musb_interrupt;

	return 0;

exit1:
	clk_disable(musb->phy_clock);
	clk_put(musb->phy_clock);
exit0:
	clk_disable(musb->clock);

	return status;
}

int musb_platform_exit(struct musb *musb)
{
    otg_put_transceiver(musb->xceiv);
	usb_nop_xceiv_unregister();

	clk_disable(musb->clock);

	clk_disable(musb->phy_clock);
	clk_put(musb->phy_clock);

	return 0;
}

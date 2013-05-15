/* ir-keytable.c - handle IR scancode->keycode tables
 *
 * Copyright (C) 2009 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */


#include <linux/input.h>
#include <linux/slab.h>
#include "ir-core-priv.h"

/* Sizes are in bytes, 256 bytes allows for 32 entries on x64 */
#define IR_TAB_MIN_SIZE	256
#define IR_TAB_MAX_SIZE	8192

/* FIXME: IR_KEYPRESS_TIMEOUT should be protocol specific */
#define IR_KEYPRESS_TIMEOUT 250

/**
 * ir_create_table() - initializes a scancode table
 * @rc_tab:	the ir_scancode_table to initialize
 * @name:	name to assign to the table
 * @ir_type:	ir type to assign to the new table
 * @size:	initial size of the table
 * @return:	zero on success or a negative error code
 *
 * This routine will initialize the ir_scancode_table and will allocate
 * memory to hold at least the specified number elements.
 */
static int ir_create_table(struct ir_scancode_table *rc_tab,
			   const char *name, u64 ir_type, size_t size)
{
	rc_tab->name = name;
	rc_tab->ir_type = ir_type;
	rc_tab->alloc = roundup_pow_of_two(size * sizeof(struct ir_scancode));
	rc_tab->size = rc_tab->alloc / sizeof(struct ir_scancode);
	rc_tab->scan = kmalloc(rc_tab->alloc, GFP_KERNEL);
	if (!rc_tab->scan)
		return -ENOMEM;

	IR_dprintk(1, "Allocated space for %u keycode entries (%u bytes)\n",
		   rc_tab->size, rc_tab->alloc);
	return 0;
}

/**
 * ir_free_table() - frees memory allocated by a scancode table
 * @rc_tab:	the table whose mappings need to be freed
 *
 * This routine will free memory alloctaed for key mappings used by given
 * scancode table.
 */
static void ir_free_table(struct ir_scancode_table *rc_tab)
{
	rc_tab->size = 0;
	kfree(rc_tab->scan);
	rc_tab->scan = NULL;
}

/**
 * ir_resize_table() - resizes a scancode table if necessary
 * @rc_tab:	the ir_scancode_table to resize
 * @gfp_flags:	gfp flags to use when allocating memory
 * @return:	zero on success or a negative error code
 *
 * This routine will shrink the ir_scancode_table if it has lots of
 * unused entries and grow it if it is full.
 */
static int ir_resize_table(struct ir_scancode_table *rc_tab, gfp_t gfp_flags)
{
	unsigned int oldalloc = rc_tab->alloc;
	unsigned int newalloc = oldalloc;
	struct ir_scancode *oldscan = rc_tab->scan;
	struct ir_scancode *newscan;

	if (rc_tab->size == rc_tab->len) {
		/* All entries in use -> grow keytable */
		if (rc_tab->alloc >= IR_TAB_MAX_SIZE)
			return -ENOMEM;

		newalloc *= 2;
		IR_dprintk(1, "Growing table to %u bytes\n", newalloc);
	}

	if ((rc_tab->len * 3 < rc_tab->size) && (oldalloc > IR_TAB_MIN_SIZE)) {
		/* Less than 1/3 of entries in use -> shrink keytable */
		newalloc /= 2;
		IR_dprintk(1, "Shrinking table to %u bytes\n", newalloc);
	}

	if (newalloc == oldalloc)
		return 0;

	newscan = kmalloc(newalloc, gfp_flags);
	if (!newscan) {
		IR_dprintk(1, "Failed to kmalloc %u bytes\n", newalloc);
		return -ENOMEM;
	}

	memcpy(newscan, rc_tab->scan, rc_tab->len * sizeof(struct ir_scancode));
	rc_tab->scan = newscan;
	rc_tab->alloc = newalloc;
	rc_tab->size = rc_tab->alloc / sizeof(struct ir_scancode);
	kfree(oldscan);
	return 0;
}

/**
 * ir_update_mapping() - set a keycode in the scancode->keycode table
 * @dev:	the struct input_dev device descriptor
 * @rc_tab:	scancode table to be adjusted
 * @index:	index of the mapping that needs to be updated
 * @keycode:	the desired keycode
 * @return:	previous keycode assigned to the mapping
 *
 * This routine is used to update scancode->keycopde mapping at given
 * position.
 */
static unsigned int ir_update_mapping(struct input_dev *dev,
				      struct ir_scancode_table *rc_tab,
				      unsigned int index,
				      unsigned int new_keycode)
{
	int old_keycode = rc_tab->scan[index].keycode;
	int i;

	/* Did the user wish to remove the mapping? */
	if (new_keycode == KEY_RESERVED || new_keycode == KEY_UNKNOWN) {
		IR_dprintk(1, "#%d: Deleting scan 0x%04x\n",
			   index, rc_tab->scan[index].scancode);
		rc_tab->len--;
		memmove(&rc_tab->scan[index], &rc_tab->scan[index+ 1],
			(rc_tab->len - index) * sizeof(struct ir_scancode));
	} else {
		IR_dprintk(1, "#%d: %s scan 0x%04x with key 0x%04x\n",
			   index,
			   old_keycode == KEY_RESERVED ? "New" : "Replacing",
			   rc_tab->scan[index].scancode, new_keycode);
		rc_tab->scan[index].keycode = new_keycode;
		__set_bit(new_keycode, dev->keybit);
	}

	if (old_keycode != KEY_RESERVED) {
		/* A previous mapping was updated... */
		__clear_bit(old_keycode, dev->keybit);
		/* ... but another scancode might use the same keycode */
		for (i = 0; i < rc_tab->len; i++) {
			if (rc_tab->scan[i].keycode == old_keycode) {
				__set_bit(old_keycode, dev->keybit);
				break;
			}
		}

		/* Possibly shrink the keytable, failure is not a problem */
		ir_resize_table(rc_tab, GFP_ATOMIC);
	}

	return old_keycode;
}

/**
 * ir_locate_scancode() - set a keycode in the scancode->keycode table
 * @ir_dev:	the struct ir_input_dev device descriptor
 * @rc_tab:	scancode table to be searched
 * @scancode:	the desired scancode
 * @resize:	controls whether we allowed to resize the table to
 *		accomodate not yet present scancodes
 * @return:	index of the mapping containing scancode in question
 *		or -1U in case of failure.
 *
 * This routine is used to locate given scancode in ir_scancode_table.
 * If scancode is not yet present the routine will allocate a new slot
 * for it.
 */
static unsigned int ir_establish_scancode(struct ir_input_dev *ir_dev,
					  struct ir_scancode_table *rc_tab,
					  unsigned int scancode,
					  bool resize)
{
	unsigned int i;

	/*
	 * Unfortunately, some hardware-based IR decoders don't provide
	 * all bits for the complete IR code. In general, they provide only
	 * the command part of the IR code. Yet, as it is possible to replace
	 * the provided IR with another one, it is needed to allow loading
	 * IR tables from other remotes. So,
	 */
	if (ir_dev->props && ir_dev->props->scanmask)
		scancode &= ir_dev->props->scanmask;

	/* First check if we already have a mapping for this ir command */
	for (i = 0; i < rc_tab->len; i++) {
		if (rc_tab->scan[i].scancode == scancode)
			return i;

		/* Keytable is sorted from lowest to highest scancode */
		if (rc_tab->scan[i].scancode >= scancode)
			break;
	}

	/* No previous mapping found, we might need to grow the table */
	if (rc_tab->size == rc_tab->len) {
		if (!resize || ir_resize_table(rc_tab, GFP_ATOMIC))
			return -1U;
	}

	/* i is the proper index to insert our new keycode */
	if (i < rc_tab->len)
		memmove(&rc_tab->scan[i + 1], &rc_tab->scan[i],
			(rc_tab->len - i) * sizeof(struct ir_scancode));
	rc_tab->scan[i].scancode = scancode;
	rc_tab->scan[i].keycode = KEY_RESERVED;
	rc_tab->len++;

	return i;
}

/**
 * ir_setkeycode() - set a keycode in the scancode->keycode table
 * @dev:	the struct input_dev device descriptor
 * @scancode:	the desired scancode
 * @keycode:	result
 * @return:	-EINVAL if the keycode could not be inserted, otherwise zero.
 *
 * This routine is used to handle evdev EVIOCSKEY ioctl.
 */
static int ir_setkeycode(struct input_dev *dev,
			 const struct input_keymap_entry *ke,
			 unsigned int *old_keycode)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(dev);
	struct ir_scancode_table *rc_tab = &ir_dev->rc_tab;
	unsigned int index;
	unsigned int scancode;
	int retval;
	unsigned long flags;

	spin_lock_irqsave(&rc_tab->lock, flags);

	if (ke->flags & INPUT_KEYMAP_BY_INDEX) {
		index = ke->index;
		if (index >= rc_tab->len) {
			retval = -EINVAL;
			goto out;
		}
	} else {
		retval = input_scancode_to_scalar(ke, &scancode);
		if (retval)
			goto out;

		index = ir_establish_scancode(ir_dev, rc_tab, scancode, true);
		if (index >= rc_tab->len) {
			retval = -ENOMEM;
			goto out;
		}
	}

	*old_keycode = ir_update_mapping(dev, rc_tab, index, ke->keycode);

out:
	spin_unlock_irqrestore(&rc_tab->lock, flags);
	return retval;
}

/**
 * ir_setkeytable() - sets several entries in the scancode->keycode table
 * @dev:	the struct input_dev device descriptor
 * @to:		the struct ir_scancode_table to copy entries to
 * @from:	the struct ir_scancode_table to copy entries from
 * @return:	-ENOMEM if all keycodes could not be inserted, otherwise zero.
 *
 * This routine is used to handle table initialization.
 */
static int ir_setkeytable(struct ir_input_dev *ir_dev,
			  const struct ir_scancode_table *from)
{
	struct ir_scancode_table *rc_tab = &ir_dev->rc_tab;
	unsigned int i, index;
	int rc;

	rc = ir_create_table(&ir_dev->rc_tab,
			     from->name, from->ir_type, from->size);
	if (rc)
		return rc;

	IR_dprintk(1, "Allocated space for %u keycode entries (%u bytes)\n",
		   rc_tab->size, rc_tab->alloc);

	for (i = 0; i < from->size; i++) {
		index = ir_establish_scancode(ir_dev, rc_tab,
					      from->scan[i].scancode, false);
		if (index >= rc_tab->len) {
			rc = -ENOMEM;
			break;
		}

		ir_update_mapping(ir_dev->input_dev, rc_tab, index,
				  from->scan[i].keycode);
	}

	if (rc)
		ir_free_table(rc_tab);

	return rc;
}

/**
 * ir_lookup_by_scancode() - locate mapping by scancode
 * @rc_tab:	the &struct ir_scancode_table to search
 * @scancode:	scancode to look for in the table
 * @return:	index in the table, -1U if not found
 *
 * This routine performs binary search in RC keykeymap table for
 * given scancode.
 */
static unsigned int ir_lookup_by_scancode(const struct ir_scancode_table *rc_tab,
					  unsigned int scancode)
{
	int start = 0;
	int end = rc_tab->len - 1;
	int mid;

	while (start <= end) {
		mid = (start + end) / 2;
		if (rc_tab->scan[mid].scancode < scancode)
			start = mid + 1;
		else if (rc_tab->scan[mid].scancode > scancode)
			end = mid - 1;
		else
			return mid;
	}

	return -1U;
}

/**
 * ir_getkeycode() - get a keycode from the scancode->keycode table
 * @dev:	the struct input_dev device descriptor
 * @scancode:	the desired scancode
 * @keycode:	used to return the keycode, if found, or KEY_RESERVED
 * @return:	always returns zero.
 *
 * This routine is used to handle evdev EVIOCGKEY ioctl.
 */
static int ir_getkeycode(struct input_dev *dev,
			 struct input_keymap_entry *ke)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(dev);
	struct ir_scancode_table *rc_tab = &ir_dev->rc_tab;
	struct ir_scancode *entry;
	unsigned long flags;
	unsigned int index;
	unsigned int scancode;
	int retval;

	spin_lock_irqsave(&rc_tab->lock, flags);

	if (ke->flags & INPUT_KEYMAP_BY_INDEX) {
		index = ke->index;
	} else {
		retval = input_scancode_to_scalar(ke, &scancode);
		if (retval)
			goto out;

		index = ir_lookup_by_scancode(rc_tab, scancode);
	}

	if (index < rc_tab->len) {
		entry = &rc_tab->scan[index];

		ke->index = index;
		ke->keycode = entry->keycode;
		ke->len = sizeof(entry->scancode);
		memcpy(ke->scancode, &entry->scancode, sizeof(entry->scancode));

	} else if (!(ke->flags & INPUT_KEYMAP_BY_INDEX)) {
		/*
		 * We do not really know the valid range of scancodes
		 * so let's respond with KEY_RESERVED to anything we
		 * do not have mapping for [yet].
		 */
		ke->index = index;
		ke->keycode = KEY_RESERVED;
	} else {
		retval = -EINVAL;
		goto out;
	}

	retval = 0;

out:
	spin_unlock_irqrestore(&rc_tab->lock, flags);
	return retval;
}

/**
 * ir_g_keycode_from_table() - gets the keycode that corresponds to a scancode
 * @input_dev:	the struct input_dev descriptor of the device
 * @scancode:	the scancode that we're seeking
 *
 * This routine is used by the input routines when a key is pressed at the
 * IR. The scancode is received and needs to be converted into a keycode.
 * If the key is not found, it returns KEY_RESERVED. Otherwise, returns the
 * corresponding keycode from the table.
 */
u32 ir_g_keycode_from_table(struct input_dev *dev, u32 scancode)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(dev);
	struct ir_scancode_table *rc_tab = &ir_dev->rc_tab;
	unsigned int keycode;
	unsigned int index;
	unsigned long flags;

	spin_lock_irqsave(&rc_tab->lock, flags);

	index = ir_lookup_by_scancode(rc_tab, scancode);
	keycode = index < rc_tab->len ?
			rc_tab->scan[index].keycode : KEY_RESERVED;

	spin_unlock_irqrestore(&rc_tab->lock, flags);

	if (keycode != KEY_RESERVED)
		IR_dprintk(1, "%s: scancode 0x%04x keycode 0x%02x\n",
			   dev->name, scancode, keycode);

	return keycode;
}
EXPORT_SYMBOL_GPL(ir_g_keycode_from_table);

/**
 * ir_keyup() - generates input event to cleanup a key press
 * @ir:         the struct ir_input_dev descriptor of the device
 *
 * This routine is used to signal that a key has been released on the
 * remote control. It reports a keyup input event via input_report_key().
 */
void ir_keyup(struct ir_input_dev *ir)
{
	if (!ir->keypressed)
		return;

	IR_dprintk(1, "keyup key 0x%04x\n", ir->last_keycode);
	input_report_key(ir->input_dev, ir->last_keycode, 0);
	input_sync(ir->input_dev);
	ir->keypressed = false;
}
EXPORT_SYMBOL_GPL(ir_keyup);

/**
 * ir_timer_keyup() - generates a keyup event after a timeout
 * @cookie:     a pointer to struct ir_input_dev passed to setup_timer()
 *
 * This routine will generate a keyup event some time after a keydown event
 * is generated when no further activity has been detected.
 */
static void ir_timer_keyup(unsigned long cookie)
{
	struct ir_input_dev *ir = (struct ir_input_dev *)cookie;
	unsigned long flags;

	/*
	 * ir->keyup_jiffies is used to prevent a race condition if a
	 * hardware interrupt occurs at this point and the keyup timer
	 * event is moved further into the future as a result.
	 *
	 * The timer will then be reactivated and this function called
	 * again in the future. We need to exit gracefully in that case
	 * to allow the input subsystem to do its auto-repeat magic or
	 * a keyup event might follow immediately after the keydown.
	 */
	spin_lock_irqsave(&ir->keylock, flags);
	if (time_is_before_eq_jiffies(ir->keyup_jiffies))
		ir_keyup(ir);
	spin_unlock_irqrestore(&ir->keylock, flags);
}

/**
 * ir_repeat() - notifies the IR core that a key is still pressed
 * @dev:        the struct input_dev descriptor of the device
 *
 * This routine is used by IR decoders when a repeat message which does
 * not include the necessary bits to reproduce the scancode has been
 * received.
 */
void ir_repeat(struct input_dev *dev)
{
	unsigned long flags;
	struct ir_input_dev *ir = input_get_drvdata(dev);

	spin_lock_irqsave(&ir->keylock, flags);

	input_event(dev, EV_MSC, MSC_SCAN, ir->last_scancode);

	if (!ir->keypressed)
		goto out;

	ir->keyup_jiffies = jiffies + msecs_to_jiffies(IR_KEYPRESS_TIMEOUT);
	mod_timer(&ir->timer_keyup, ir->keyup_jiffies);

out:
	spin_unlock_irqrestore(&ir->keylock, flags);
}
EXPORT_SYMBOL_GPL(ir_repeat);

/**
 * ir_keydown() - generates input event for a key press
 * @dev:        the struct input_dev descriptor of the device
 * @scancode:   the scancode that we're seeking
 * @toggle:     the toggle value (protocol dependent, if the protocol doesn't
 *              support toggle values, this should be set to zero)
 *
 * This routine is used by the input routines when a key is pressed at the
 * IR. It gets the keycode for a scancode and reports an input event via
 * input_report_key().
 */
void ir_keydown(struct input_dev *dev, int scancode, u8 toggle)
{
	unsigned long flags;
	struct ir_input_dev *ir = input_get_drvdata(dev);

	u32 keycode = ir_g_keycode_from_table(dev, scancode);

	spin_lock_irqsave(&ir->keylock, flags);

	input_event(dev, EV_MSC, MSC_SCAN, scancode);

	/* Repeat event? */
	if (ir->keypressed &&
	    ir->last_scancode == scancode &&
	    ir->last_toggle == toggle)
		goto set_timer;

	/* Release old keypress */
	ir_keyup(ir);

	ir->last_scancode = scancode;
	ir->last_toggle = toggle;
	ir->last_keycode = keycode;


	if (keycode == KEY_RESERVED)
		goto out;


	/* Register a keypress */
	ir->keypressed = true;
	IR_dprintk(1, "%s: key down event, key 0x%04x, scancode 0x%04x\n",
		   dev->name, keycode, scancode);
	input_report_key(dev, ir->last_keycode, 1);
	input_sync(dev);

set_timer:
	ir->keyup_jiffies = jiffies + msecs_to_jiffies(IR_KEYPRESS_TIMEOUT);
	mod_timer(&ir->timer_keyup, ir->keyup_jiffies);
out:
	spin_unlock_irqrestore(&ir->keylock, flags);
}
EXPORT_SYMBOL_GPL(ir_keydown);

static int ir_open(struct input_dev *input_dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);

	return ir_dev->props->open(ir_dev->props->priv);
}

static void ir_close(struct input_dev *input_dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);

	ir_dev->props->close(ir_dev->props->priv);
}

/**
 * __ir_input_register() - sets the IR keycode table and add the handlers
 *			    for keymap table get/set
 * @input_dev:	the struct input_dev descriptor of the device
 * @rc_tab:	the struct ir_scancode_table table of scancode/keymap
 *
 * This routine is used to initialize the input infrastructure
 * to work with an IR.
 * It will register the input/evdev interface for the device and
 * register the syfs code for IR class
 */
int __ir_input_register(struct input_dev *input_dev,
		      const struct ir_scancode_table *rc_tab,
		      struct ir_dev_props *props,
		      const char *driver_name)
{
	struct ir_input_dev *ir_dev;
	int rc;

	if (rc_tab->scan == NULL || !rc_tab->size)
		return -EINVAL;

	ir_dev = kzalloc(sizeof(*ir_dev), GFP_KERNEL);
	if (!ir_dev)
		return -ENOMEM;

	ir_dev->driver_name = kasprintf(GFP_KERNEL, "%s", driver_name);
	if (!ir_dev->driver_name) {
		rc = -ENOMEM;
		goto out_dev;
	}

	input_dev->getkeycode_new = ir_getkeycode;
	input_dev->setkeycode_new = ir_setkeycode;
	input_set_drvdata(input_dev, ir_dev);
	ir_dev->input_dev = input_dev;

	spin_lock_init(&ir_dev->rc_tab.lock);
	spin_lock_init(&ir_dev->keylock);
	setup_timer(&ir_dev->timer_keyup, ir_timer_keyup, (unsigned long)ir_dev);

	if (props) {
		ir_dev->props = props;
		if (props->open)
			input_dev->open = ir_open;
		if (props->close)
			input_dev->close = ir_close;
	}

	set_bit(EV_KEY, input_dev->evbit);
	set_bit(EV_REP, input_dev->evbit);
	set_bit(EV_MSC, input_dev->evbit);
	set_bit(MSC_SCAN, input_dev->mscbit);

	rc = ir_setkeytable(ir_dev, rc_tab);
	if (rc)
		goto out_name;

	rc = ir_register_class(input_dev);
	if (rc < 0)
		goto out_table;

	if (ir_dev->props)
		if (ir_dev->props->driver_type == RC_DRIVER_IR_RAW) {
			rc = ir_raw_event_register(input_dev);
			if (rc < 0)
				goto out_event;
		}

	rc = ir_register_input(input_dev);
	if (rc < 0)
		goto out_event;

	IR_dprintk(1, "Registered input device on %s for %s remote%s.\n",
		   driver_name, rc_tab->name,
		   (ir_dev->props && ir_dev->props->driver_type == RC_DRIVER_IR_RAW) ?
			" in raw mode" : "");

	/*
	 * Default delay of 250ms is too short for some protocols, expecially
	 * since the timeout is currently set to 250ms. Increase it to 500ms,
	 * to avoid wrong repetition of the keycodes.
	 */
	input_dev->rep[REP_DELAY] = 500;

	return 0;

out_event:
	ir_unregister_class(input_dev);
out_table:
	ir_free_table(&ir_dev->rc_tab);
out_name:
	kfree(ir_dev->driver_name);
out_dev:
	kfree(ir_dev);
	return rc;
}
EXPORT_SYMBOL_GPL(__ir_input_register);

/**
 * ir_input_unregister() - unregisters IR and frees resources
 * @input_dev:	the struct input_dev descriptor of the device

 * This routine is used to free memory and de-register interfaces.
 */
void ir_input_unregister(struct input_dev *input_dev)
{
	struct ir_input_dev *ir_dev = input_get_drvdata(input_dev);

	if (!ir_dev)
		return;

	IR_dprintk(1, "Freed keycode table\n");

	del_timer_sync(&ir_dev->timer_keyup);
	if (ir_dev->props)
		if (ir_dev->props->driver_type == RC_DRIVER_IR_RAW)
			ir_raw_event_unregister(input_dev);

	ir_free_table(&ir_dev->rc_tab);

	ir_unregister_class(input_dev);

	kfree(ir_dev->driver_name);
	kfree(ir_dev);
}
EXPORT_SYMBOL_GPL(ir_input_unregister);

int ir_core_debug;    /* ir_debug level (0,1,2) */
EXPORT_SYMBOL_GPL(ir_core_debug);
module_param_named(debug, ir_core_debug, int, 0644);

MODULE_AUTHOR("Mauro Carvalho Chehab <mchehab@redhat.com>");
MODULE_LICENSE("GPL");

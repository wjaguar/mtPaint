/*	channels.c
	Copyright (C) 2006-2013 Mark Tyler and Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 3 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/


#include "global.h"
#undef _
#define _(X) X

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "canvas.h"
#include "vcode.h"
#include "channels.h"

int overlay_alpha;
int hide_image;
int RGBA_mode;

unsigned char channel_rgb[NUM_CHANNELS][3] = {
	{0, 0, 0},	/* Image */
	{0, 0, 255},	/* Alpha */
	{255, 255, 0},	/* Selection */
	{255, 0, 0}	/* Mask */
};

/* The 0-th value is (255 - global opacity) - i.e., image visibility */
unsigned char channel_opacity[NUM_CHANNELS] = {128, 128, 128, 128};

/* 255 for channels where it's better to see inverse values - like alpha */
unsigned char channel_inv[NUM_CHANNELS] = {0, 255, 0, 0};

/* Default fill values for the channels */
unsigned char channel_fill[NUM_CHANNELS] = {0, 255, 0, 0};

/* Per-channel drawing "colours" */
unsigned char channel_col_[2][NUM_CHANNELS] = {
	{255, 255, 255, 255},	/* A */
	{0, 0, 0, 0}		/* B */
};

/* Channel disable flags */
int channel_dis[NUM_CHANNELS] = {0, 0, 0, 0};

static void **newchan_window;


static void click_newchan_cancel()
{
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
		menu_widgets[MENU_CHAN0 + mem_channel]), TRUE);
		// Stops cancelled new channel showing as selected in the menu

	run_destroy(newchan_window);
	newchan_window = NULL;
}

typedef struct {
	int chan, state, inv, sens, req;
	char **names;
} cchan_dd;

#define chan_new_type (dt->chan + CHN_ALPHA)
#define chan_new_state (dt->state)
static void click_newchan_ok(cchan_dd *dt, void **wdata)
{
	chanlist tlist;
	int i, ii, j = mem_width * mem_height, range, rgb[3];
	unsigned char sq1024[1024], *src, *dest, *tmp;
	unsigned int k;
	double r2, rr;

	run_query(wdata);
	memcpy(tlist, mem_img, sizeof(chanlist));
	if ((chan_new_type == CHN_ALPHA) && (chan_new_state == 3)) i = CMASK_RGBA;
	else i = CMASK_FOR(chan_new_type);
	i = undo_next_core(UC_CREATE, mem_width, mem_height, mem_img_bpp, i);
	if (i)
	{
		click_newchan_cancel();
		memory_errors(i);
		return;
	}

	dest = mem_img[chan_new_type];
	switch (chan_new_state)
	{
	case 0: /* Clear */
		memset(dest, 0, j);
		break;
	case 1: /* Set */
		memset(dest, 255, j);
		break;
	case 2: /* Colour A radius B */
		rgb[0] = mem_col_A24.red;
		rgb[1] = mem_col_A24.green;
		rgb[2] = mem_col_A24.blue;
		range = (mem_col_A24.red - mem_col_B24.red) *
			(mem_col_A24.red - mem_col_B24.red) +
			(mem_col_A24.green - mem_col_B24.green) *
			(mem_col_A24.green - mem_col_B24.green) +
			(mem_col_A24.blue - mem_col_B24.blue) *
			(mem_col_A24.blue - mem_col_B24.blue);
		r2 = 255.0 * 255.0;
		if (range) r2 /= (double)range;
		/* Prepare fast-square-root table */
		for (i = ii = 0; i < 32; i++)
		{
			k = (i + 1) * (i + 1);
			for (; ii < k; ii++) sq1024[ii] = i;
		}
		src = mem_img[CHN_IMAGE];
		if (mem_img_bpp == 1)
		{
			unsigned char p2l[256];
			memset(p2l, 0, 256);
			for (i = 0; i < mem_cols; i++)
			{
				range = (mem_pal[i].red - rgb[0]) *
					(mem_pal[i].red - rgb[0]) +
					(mem_pal[i].green - rgb[1]) *
					(mem_pal[i].green - rgb[1]) +
					(mem_pal[i].blue - rgb[2]) *
					(mem_pal[i].blue - rgb[2]);
				k = rr = r2 * (double)range;
				/* Fast square root */
				if (rr >= (255.0 * 255.0)) ii = 255;
				else if (k < 1024) ii = sq1024[k];
				else
				{
					ii = sq1024[k >> 6] << 3;
					ii = (ii + k / ii) >> 1;
					ii -= ((k - ii * ii) >> 17) & 1;
				}
				p2l[i] = ii ^ 255;
			}
			for (i = 0; i < j; i++)
			{
				dest[i] = p2l[src[i]];
			}
		}
		else
		{
			for (i = 0; i < j; i++)
			{
				range = (src[0] - rgb[0]) * (src[0] - rgb[0]) +
					(src[1] - rgb[1]) * (src[1] - rgb[1]) +
					(src[2] - rgb[2]) * (src[2] - rgb[2]);
				k = rr = r2 * (double)range;
				/* Fast square root */
				if (rr >= (255.0 * 255.0)) ii = 255;
				else if (k < 1024) ii = sq1024[k];
				else
				{
					ii = sq1024[k >> 6] << 3;
					ii += (k - ii * ii) / (ii + ii);
					ii -= ((k - ii * ii) >> 17) & 1;
				}
				dest[i] = ii ^ 255;
				src += 3;
			}
		}
		break;
	case 3: /* Blend A to B */
		if (mem_img_bpp != 3) goto dofail;
		memset(dest, 255, j); /* Start with opaque */
		if (mem_scale_alpha(mem_img[CHN_IMAGE], dest,
			mem_width, mem_height, chan_new_type == CHN_ALPHA))
			goto dofail;
		break;
	case 4: /* Image red */
	case 5: /* Image green */
	case 6: /* Image blue */
		if (mem_img_bpp == 3) /* RGB */
		{
			src = mem_img[CHN_IMAGE] + chan_new_state - 4;
			for (i = 0; i < j; i++)
			{
				dest[i] = *src;
				src += 3;
			}
		}
		else /* Indexed */
		{
			if (chan_new_state == 4) tmp = &mem_pal[0].red;
			else if (chan_new_state == 5) tmp = &mem_pal[0].green;
			else tmp = &mem_pal[0].blue;
			src = mem_img[CHN_IMAGE];
			for (i = 0; i < j; i++)
			{
				dest[i] = *(tmp + src[i] * sizeof(png_color));
			}
		}
		break;
	case 7: /* Alpha */
	case 8: /* Selection */
	case 9: /* Mask */
		i = chan_new_state - 8;
		src = tlist[i < 0 ? CHN_ALPHA : !i ? CHN_SEL : CHN_MASK];
		if (!src) goto dofail;
		memcpy(dest, src, j);
		break;
	default: /* If all else fails */
dofail:
		memset(dest, chan_new_type == CHN_ALPHA ? 255 : 0, j);
		break;
	}

	/* Invert */
	if (dt->inv)
	{
		for (i = 0; i < j; i++) dest[i] ^= 255;
	}
	mem_undo_prepare();

	if (dt->req >= CHN_ALPHA)
	{
		mem_channel = chan_new_type;
		update_stuff(UPD_NEWCH);
	}
	else update_stuff(UPD_ADDCH);
	click_newchan_cancel();
}
#undef chan_new_type
#undef chan_new_state

#define WBbase cchan_dd
static void *cchan_code[] = {
	WINDOWm(_("Create Channel")),
	FRPACK(_("Channel Type"), channames_ + CHN_ALPHA,
		NUM_CHANNELS - CHN_ALPHA, 1, chan),
	IF(sens), INSENS,
	FVBOX(_("Initial Channel State")),
	RPACKD(names, 0, state),
	HSEP,
	CHECK(_("Inverted"), inv),
	WDONE,
	BORDER(OKBOX, 0),
	OKBOX(_("OK"), click_newchan_ok, _("Cancel"), click_newchan_cancel),
	WSHOW
};
#undef WBbase

#if NUM_CHANNELS > CHN_MASK + 1
#error "Not all channels listed in dialog"
#endif

void pressed_channel_create(int channel)
{
	char *names2[] = {
		_("Cleared"),
		_("Set"),
		_("Set colour A radius B"),
		_("Set blend A to B"),
		_("Image Red"),
		_("Image Green"),
		_("Image Blue"),
	/* I still do not agree that we need to hide these ;-) - WJ */
		mem_img[CHN_ALPHA] ? _("Alpha") : "",
		mem_img[CHN_SEL] ? _("Selection") : "",
		mem_img[CHN_MASK] ? _("Mask") : "",
		NULL
		};
	cchan_dd tdata = { channel < CHN_ALPHA ? 0 : channel - CHN_ALPHA,
		0, FALSE, channel >= 0, channel, names2 };

	newchan_window = run_create(cchan_code, &tdata, sizeof(tdata));
}

typedef struct {
	int cc[NUM_CHANNELS], cf[NUM_CHANNELS];
} dchan_dd;

static void click_delete_ok(dchan_dd *dt, void **wdata)
{
	int i, cmask;

	run_query(wdata);
	for (i = cmask = 0; i < NUM_CHANNELS; i++)
		if (dt->cf[i]) cmask |= CMASK_FOR(i);
	if (cmask)
	{
		undo_next_core(UC_DELETE, mem_width, mem_height, mem_img_bpp, cmask);

		update_stuff(UPD_DELCH);
	}

	run_destroy(wdata);
}

#if NUM_CHANNELS > CHN_MASK + 1
#error "Not all channels listed in dialog"
#endif

#define WBbase dchan_dd
static void *dchan_code[] = {
	WINDOWm(_("Delete Channels")),
	IF(cc[CHN_ALPHA]), CHECK(_("Alpha"), cf[CHN_ALPHA]),
	IF(cc[CHN_SEL]),   CHECK(_("Selection"), cf[CHN_SEL]),
	IF(cc[CHN_MASK]),  CHECK(_("Mask"), cf[CHN_MASK]),
	HSEPl(200),
	OKBOX(_("OK"), click_delete_ok, _("Cancel"), NULL),
	WSHOW
};
#undef WBbase

void pressed_channel_delete()
{
	dchan_dd tdata;
	int i, j = FALSE;

	memset(&tdata, 0, sizeof(tdata));
	for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
		if (mem_img[i]) j = tdata.cc[i] = TRUE;
	tdata.cf[mem_channel] = tdata.cc[mem_channel];
	/* If utility channels exist at all */
	if (j) run_create(dchan_code, &tdata, sizeof(tdata));
}

/* Being plugged into update_menus(), this is prone to be called recursively */
void pressed_channel_edit(int state, int channel)
{
	/* Prevent spurious calls */
	if (!state || newchan_window || (channel == mem_channel)) return;

	if (!mem_img[channel]) pressed_channel_create(channel);
	else
	{
		mem_channel = channel;
		update_stuff(UPD_CHAN);
	}
}

void pressed_channel_disable(int state, int channel)
{
	channel_dis[channel] = state;
	update_stuff(UPD_RENDER);
}

static int do_threshold(spin1_dd *dt, void **wdata)
{
	run_query(wdata);
	spot_undo(UNDO_FILT);
	mem_threshold(mem_img[mem_channel], mem_width * mem_height * MEM_BPP, dt->n[0]);
	mem_undo_prepare();
	return (TRUE);
}

void pressed_threshold()
{
	static spin1_dd tdata = {
		{ _("Threshold Channel"), spin1_code, FW_FN(do_threshold) },
		{ 128, 0, 255 } };
	run_create(filterwindow_code, &tdata, sizeof(tdata));
}

void pressed_unassociate()
{
	if (mem_img_bpp == 1) return;
	spot_undo(UNDO_COL);
	mem_demultiply(mem_img[CHN_IMAGE], mem_img[CHN_ALPHA], mem_width * mem_height, 3);
	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

void pressed_channel_toggle(int state, int what)
{
	int *toggle = what ? &hide_image : &overlay_alpha;
	if (*toggle == state) return;
	*toggle = state;
	update_stuff(UPD_RENDER);
}

void pressed_RGBA_toggle(int state)
{
	RGBA_mode = state;
	update_stuff(UPD_MODE);
}

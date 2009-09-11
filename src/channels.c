/*	channels.c
	Copyright (C) 2006 Mark Tyler and Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/


#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"

#include "memory.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "canvas.h"
#include "inifile.h"
#include "mygtk.h"

int overlay_alpha = FALSE;
int hide_image = FALSE;
int RGBA_mode = FALSE;

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
unsigned char channel_col_A[NUM_CHANNELS] = {255, 255, 255, 255};
unsigned char channel_col_B[NUM_CHANNELS] = {0, 0, 0, 0};

/* Channel disable flags */
int channel_dis[NUM_CHANNELS] = {0, 0, 0, 0};

static GtkWidget *newchan_window;
static int chan_new_type, chan_new_state;


static void click_newchan_cancel()
{
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_chann_x[mem_channel]), TRUE);
		// Stops cancelled new channel showing as selected in the menu

	gtk_widget_destroy( newchan_window );
	newchan_window = NULL;
}

static void activate_channel(int chan)
{
	mem_channel = chan;
	pressed_opacity(chan == CHN_IMAGE ? tool_opacity : channel_col_A[chan]);
}

static void click_newchan_ok(GtkButton *button, gpointer user_data)
{
	chanlist tlist;
	int i, ii, j = mem_width * mem_height, range, rgb[3];
	unsigned char sq1024[1024], *src, *dest, *tmp;
	unsigned int k;
	double r2, rr;

	memcpy(tlist, mem_img, sizeof(chanlist));
	if ((chan_new_type == CHN_ALPHA) && (chan_new_state == 3)) i = CMASK_RGBA;
	else i = CMASK_FOR(chan_new_type);
	i = undo_next_core(1, mem_width, mem_height, mem_img_bpp, i);
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
					ii += (k - ii * ii) / (ii + ii);
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
			mem_width, mem_height, chan_new_type == CHN_ALPHA, 0))
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
	if ((gint)user_data >= CHN_ALPHA) activate_channel(chan_new_type);
	canvas_undo_chores();
	click_newchan_cancel();
}

void pressed_channel_create( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	gchar *names2[] = {
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

	GtkAccelGroup* ag = gtk_accel_group_new();
	GtkWidget *frame, *vbox, *vbox2, *hbox, *button;


	chan_new_type = item < CHN_ALPHA ? CHN_ALPHA : item;
	chan_new_state = 0;

	newchan_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Create Channel"),
			GTK_WIN_POS_CENTER, TRUE );

	gtk_signal_connect_object (GTK_OBJECT (newchan_window), "delete_event",
		GTK_SIGNAL_FUNC (click_newchan_cancel), NULL);

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (newchan_window), vbox);

	frame = gtk_frame_new (_("Channel Type"));
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	hbox = wj_radio_pack(channames, -1, 1, chan_new_type, &chan_new_type, NULL);
	gtk_container_add(GTK_CONTAINER(frame), hbox);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	if (item >= 0) gtk_widget_set_sensitive(hbox, FALSE);

	frame = gtk_frame_new (_("Initial Channel State"));
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	vbox2 = wj_radio_pack(names2, -1, 0, chan_new_state, &chan_new_state, NULL);
	gtk_container_add(GTK_CONTAINER(frame), vbox2);
	gtk_container_set_border_width(GTK_CONTAINER(vbox2), 5);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (vbox), hbox, FALSE, FALSE, 0);

	button = gtk_button_new_with_label (_("Cancel"));
	gtk_widget_show (button);
	gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (button), 5);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_newchan_cancel), NULL);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button = gtk_button_new_with_label (_("OK"));
	gtk_widget_show (button);
	gtk_box_pack_start (GTK_BOX (hbox), button, TRUE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (button), 5);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_newchan_ok), (gpointer)item);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);

	gtk_window_set_transient_for( GTK_WINDOW(newchan_window), GTK_WINDOW(main_window) );
	gtk_widget_show(newchan_window);
	gtk_window_add_accel_group(GTK_WINDOW (newchan_window), ag);
}

void pressed_channel_delete( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	int i;
	char txt[128];

	if ( mem_channel == CHN_IMAGE ) return;		// Don't allow deletion of image channel

	snprintf(txt, 120, _("Do you really want to delete the %s channel?"), channames[mem_channel] );
	i = alert_box( _("Warning"), txt, _("No"), _("Yes"), NULL );

	if ( i==2 )
	{
		undo_next_core(4, mem_width, mem_height, mem_img_bpp, CMASK_CURR);
		mem_channel = CHN_IMAGE;

		update_all_views();		// Update images
		init_status_bar();
		update_menus();
	}
}

/* Being plugged into update_menus(), this is prone to be called recursively */
void pressed_channel_edit( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	/* Prevent spurious calls */
	if (!GTK_CHECK_MENU_ITEM(menu_item)->active) return;
	if (newchan_window) return;
	if (item == mem_channel) return;

	if ( marq_status >= MARQUEE_PASTE && mem_clip_bpp == 3)
		pressed_select_none( NULL, NULL );
	// Stop pasting if with RGB paste

	if (!mem_img[item])
	{
		pressed_channel_create(menu_item, user_data, item);
		return;
	}

	activate_channel(item);
	canvas_undo_chores();
}

void pressed_channel_disable( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	channel_dis[item] = GTK_CHECK_MENU_ITEM(menu_item)->active;
	update_all_views();
}

int do_threshold(GtkWidget *spin, gpointer fdata)
{
	int i;

	gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
	i = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin));
	spot_undo(UNDO_FILT);
	mem_threshold(mem_channel, i);

	return TRUE;
}

void pressed_threshold( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	GtkWidget *spin = add_a_spin(128, 0, 255);
	filter_window(_("Threshold Channel"), spin, do_threshold, NULL);
}

void pressed_channel_toggle( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	int *toggle = item ? &hide_image : &overlay_alpha;
	if (*toggle == GTK_CHECK_MENU_ITEM(menu_item)->active) return;
	*toggle = GTK_CHECK_MENU_ITEM(menu_item)->active;
	update_all_views();
}

void pressed_RGBA_toggle( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	RGBA_mode = GTK_CHECK_MENU_ITEM(menu_item)->active;
	inifile_set_gboolean("couple_RGBA", RGBA_mode );
	update_all_views();
}

void pressed_channel_config_overlay()
{
	colour_selector( COLSEL_OVERLAYS );
}

void pressed_channel_load()
{
	if ( mem_channel == CHN_IMAGE ) return;
	file_selector( FS_CHANNEL_LOAD );
}

void pressed_channel_save()
{
	if ( mem_channel == CHN_IMAGE ) return;
	file_selector( FS_CHANNEL_SAVE );
}


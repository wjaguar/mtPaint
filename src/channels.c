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
#include "mygtk.h"

/* !!! Maybe store it in config? !!! */
int overlay_alpha = FALSE;

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

static GtkWidget *newchan_window;
static int chan_new_type, chan_new_state;

static gchar *channames[] = { NULL, _("Alpha"), _("Selection"), _("Mask"), NULL };

static void click_newchan_cancel()
{
	gtk_widget_destroy( newchan_window );
	newchan_window = NULL;
}

static void click_newchan_ok()
{
	chanlist tlist;
	int i, j = mem_width * mem_height;
	unsigned char *src, *dest, *tmp;

	memcpy(tlist, mem_img, sizeof(chanlist));
	if ((chan_new_type == CHN_ALPHA) && (chan_new_state == 3)) i = CMASK_RGBA;
	else i = CMASK_FOR(chan_new_type);
	pen_down = 0; /* Ensure next tool action is treated separately */
	i = undo_next_core(1, mem_width, mem_height, mem_img_bpp, i);
	pen_down = 0;
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

/* !!! Not implemented yet !!! */
		goto dofail;

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
	canvas_undo_chores();
	click_newchan_cancel();
}

static void chan_type_changed(GtkWidget *widget, gpointer name)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;
	chan_new_type = (int) name;
}

static void chan_state_changed(GtkWidget *widget, gpointer name)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;
	chan_new_state = (int) name;
}


void pressed_channel_create( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	static gchar *names2[] = {
		_("Cleared"),
		_("Set"),
		_("Set colour A radius B"),
		_("Set blend A to B"),
		_("Image Red"),
		_("Image Green"),
		_("Image Blue"),
		_("Alpha"),
		_("Selection"),
		_("Mask"),
		NULL
		};

	GtkAccelGroup* ag = gtk_accel_group_new();
	GtkWidget *frame, *vbox, *vbox2, *hbox, *button, *radio;

	int i;


	chan_new_type = item < CHN_ALPHA ? CHN_ALPHA : item;
	chan_new_state = 0;

	newchan_window = gtk_dialog_new_with_buttons(_("Create Channel"),
		GTK_WINDOW(main_window), GTK_DIALOG_DESTROY_WITH_PARENT, NULL);
	gtk_dialog_set_has_separator(GTK_DIALOG(newchan_window), FALSE);

	vbox = GTK_DIALOG(newchan_window)->vbox;

	frame = gtk_frame_new (_("Channel Type"));
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox);
	gtk_container_add (GTK_CONTAINER (frame), hbox);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);

	radio = NULL;
	for (i = 1; channames[i]; i++)
	{
		radio = add_radio_button(channames[i], NULL, radio, hbox, i);
		if (chan_new_type == i)
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), TRUE);
		gtk_signal_connect(GTK_OBJECT(radio), "toggled",
				GTK_SIGNAL_FUNC(chan_type_changed),
				(gpointer)(i));
	}
	if (item >= 0) gtk_widget_set_sensitive(hbox, FALSE);

	frame = gtk_frame_new (_("Initial Channel State"));
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	vbox2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox2);
	gtk_container_add (GTK_CONTAINER (frame), vbox2);
	gtk_container_set_border_width (GTK_CONTAINER (vbox2), 5);

	radio = NULL;
	for (i = 0; names2[i]; i++)
	{
		radio = add_radio_button(names2[i], NULL, radio, vbox2, i + 1);
		if (chan_new_state == i) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), TRUE);
		gtk_signal_connect(GTK_OBJECT(radio), "toggled",
				GTK_SIGNAL_FUNC(chan_state_changed),
				(gpointer)(i));
	}

	/* "action_area" box is unusable because of non-expanding behaviour */
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
		GTK_SIGNAL_FUNC(click_newchan_ok), NULL);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);

	gtk_widget_show(newchan_window);
	gtk_window_add_accel_group(GTK_WINDOW (newchan_window), ag);
}

void pressed_channel_delete( GtkMenuItem *menu_item, gpointer user_data, gint item )
{

/* !!! Need a frontend !!! */

}

/* Being plugged into update_menus(), this is prone to be called recursively */
void pressed_channel_edit( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	/* Prevent spurious calls */
	if (!GTK_CHECK_MENU_ITEM(menu_item)->active) return;
	if (newchan_window) return;
	if (item == mem_channel) return;

	if (!mem_img[item])
	{
		pressed_channel_create(menu_item, user_data, item);
		gtk_dialog_run(GTK_DIALOG(newchan_window));
		if (newchan_window) click_newchan_cancel();
	}

	if (mem_img[item])
	{
		mem_channel = item;
		pressed_opacity(item == CHN_IMAGE ? tool_opacity : channel_col_A[item]);
	}
	canvas_undo_chores();
}

void pressed_channel_disable( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
}

void pressed_channel_alpha_overlay( GtkMenuItem *menu_item )
{
	if (overlay_alpha == GTK_CHECK_MENU_ITEM(menu_item)->active) return;
	overlay_alpha = GTK_CHECK_MENU_ITEM(menu_item)->active;
	update_all_views();
}

void pressed_channel_config_overlay()
{
	colour_selector( COLSEL_OVERLAYS );
}

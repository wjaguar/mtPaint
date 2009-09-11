/*	channels.c
	Copyright (C) 2006 Mark Tyler

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
#include "mygtk.h"


static GtkWidget *newchan_window;
static int chan_new_type, chan_new_state;


static void click_newchan_cancel()
{
	gtk_widget_destroy( newchan_window );
}

static void click_newchan_ok()
{
//printf("%i %i\n", chan_new_type, chan_new_state);

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


void pressed_channel_create()
{
	gchar *names1[] = { _("Alpha"), _("Selection"), _("Mask"), NULL },
		*names2[] = {
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


	chan_new_type = 0;
	chan_new_state = 0;

	newchan_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Create Channel"),
			GTK_WIN_POS_CENTER, TRUE );

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (newchan_window), vbox);

	frame = gtk_frame_new (_("Channel Type"));
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (vbox), frame, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox);
	gtk_container_add (GTK_CONTAINER (frame), hbox);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);

	radio = NULL;
	for (i = 0; names1[i]; i++)
	{
		radio = add_radio_button(names1[i], NULL, radio, hbox, i + 1);
		if (chan_new_type == i) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(radio), TRUE);
		gtk_signal_connect(GTK_OBJECT(radio), "toggled",
				GTK_SIGNAL_FUNC(chan_type_changed),
				(gpointer)(i));
	}

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

	gtk_window_set_transient_for( GTK_WINDOW(newchan_window), GTK_WINDOW(main_window) );
	gtk_widget_show (newchan_window);
	gtk_window_add_accel_group(GTK_WINDOW (newchan_window), ag);
}

void pressed_channel_delete()
{
}

void pressed_channel_edit( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
}

void pressed_channel_disable( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
}

void pressed_channel_alpha_overlay( GtkMenuItem *menu_item )
{
//	gboolean state = GTK_CHECK_MENU_ITEM(menu_item)->active;
}

void pressed_channel_config_overlay()
{
	colour_selector( COLSEL_OVERLAYS );
}

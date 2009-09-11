/*	mygtk.c
	Copyright (C) 2004-2006 Mark Tyler and Dmitry Groshev

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


///	GENERIC WIDGET PRIMITIVES

GtkWidget *add_a_window( GtkWindowType type, char *title, GtkWindowPosition pos, gboolean modal )
{
	GtkWidget *win = gtk_window_new(type);
	gtk_window_set_title(GTK_WINDOW(win), title);
	gtk_window_set_position(GTK_WINDOW(win), pos);
	gtk_window_set_modal(GTK_WINDOW(win), modal);

	return win;
}

GtkWidget *add_a_button( char *text, int bord, GtkWidget *box, gboolean filler )
{
	GtkWidget *button = gtk_button_new_with_label(text);
	gtk_widget_show (button);
	gtk_box_pack_start (GTK_BOX (box), button, filler, filler, 0);
	gtk_container_set_border_width (GTK_CONTAINER (button), bord);

	return button;
}

GtkWidget *add_a_spin( int value, int min, int max )
{
	GtkWidget *spin;
	GtkObject *adj;

	adj = gtk_adjustment_new( value, min, max, 1, 10, 10 );
	spin = gtk_spin_button_new( GTK_ADJUSTMENT (adj), 1, 0 );
	gtk_widget_show(spin);
	gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spin), TRUE);

	return spin;
}

GtkWidget *add_a_table( int rows, int columns, int bord, GtkWidget *box )
{
	GtkWidget *table = gtk_table_new(rows, columns, FALSE);
	gtk_widget_show(table);
	gtk_box_pack_start(GTK_BOX(box), table, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (table), bord);

	return table;
}

GtkWidget *add_a_toggle( char *label, GtkWidget *box, gboolean value )
{
	GtkWidget *tog;

	tog = gtk_check_button_new_with_label( label );
	gtk_widget_show( tog );
	gtk_box_pack_start (GTK_BOX(box), tog, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER ( tog ), 5);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( tog ), value);

	return tog;
}

GtkWidget *add_slider2table(int val, int min, int max, GtkWidget *table,
			int row, int column, int width, int height)
{
	GtkWidget *hscale;

	hscale = gtk_hscale_new (GTK_ADJUSTMENT (gtk_adjustment_new (val, min, max, 1, 1, 0)));
	gtk_widget_show (hscale);
	gtk_table_attach (GTK_TABLE (table), hscale, column, column+1, row, row+1,
		(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		(GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_widget_set_usize (hscale, width, height);
	gtk_scale_set_draw_value (GTK_SCALE (hscale), FALSE);
	gtk_scale_set_digits (GTK_SCALE (hscale), 0);

	return hscale;
}

GtkWidget *add_to_table( char *text, GtkWidget *table, int row, int column, int spacing, int a, int b, int c )
{
	GtkWidget *label;

	label = gtk_label_new ( text );
	gtk_widget_show (label);
	gtk_table_attach (GTK_TABLE (table), label, column, column+1, row, row+1,
		(GtkAttachOptions) (GTK_FILL),
		(GtkAttachOptions) (0), spacing, spacing);
	gtk_label_set_justify(GTK_LABEL (label), a);
	gtk_misc_set_alignment(GTK_MISC (label), b, c);

	return label;
}

void spin_to_table( GtkWidget *table, GtkWidget **spin, int row, int column, int spacing,
	int value, int min, int max )
{
	*spin = add_a_spin( value, min, max );
	gtk_table_attach(GTK_TABLE (table), *spin, column, column+1, row, row+1,
		(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		(GtkAttachOptions) (0), 0, spacing);
}

void add_hseparator( GtkWidget *widget, int xs, int ys )
{
	GtkWidget *sep = gtk_hseparator_new ();
	gtk_widget_show (sep);
	gtk_box_pack_start (GTK_BOX (widget), sep, FALSE, FALSE, 0);
	gtk_widget_set_usize (sep, xs, ys);
}



////	PROGRESS WINDOW

GtkWidget *progress_window = NULL, *progress_bar;
int prog_stop;

gint do_cancel_progress( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	prog_stop = 1;

	return FALSE;
}

gint delete_progress( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	return TRUE;			// This stops the user closing the window via the window manager
}

void progress_init(char *text, int canc)		// Initialise progress window
{
	GtkWidget *vbox6, *button_cancel, *viewport;

	progress_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Please Wait ..."),
		GTK_WIN_POS_CENTER, TRUE );
	gtk_widget_set_usize (progress_window, 400, -2);

	viewport = gtk_viewport_new (NULL, NULL);
	gtk_widget_show(viewport);
	gtk_container_add( GTK_CONTAINER( progress_window ), viewport );
	gtk_viewport_set_shadow_type( GTK_VIEWPORT(viewport), GTK_SHADOW_ETCHED_OUT );

	vbox6 = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vbox6);
	gtk_container_add(GTK_CONTAINER(viewport), vbox6);

	progress_bar = gtk_progress_bar_new ();
	gtk_box_pack_start( GTK_BOX (vbox6), progress_bar, FALSE, FALSE, 0 );
	gtk_progress_set_percentage( GTK_PROGRESS (progress_bar), 0.0 );
	gtk_progress_set_format_string( GTK_PROGRESS (progress_bar), text );
	gtk_progress_set_show_text( GTK_PROGRESS (progress_bar), TRUE );
	gtk_container_set_border_width (GTK_CONTAINER (vbox6), 10);
	gtk_widget_show( progress_bar );

	if ( canc == 1 )
	{
		add_hseparator( vbox6, -2, 10 );
		button_cancel = add_a_button(_("STOP"), 5, vbox6, TRUE);
		gtk_signal_connect(GTK_OBJECT(button_cancel), "clicked",
			GTK_SIGNAL_FUNC(do_cancel_progress), NULL);
	}

	gtk_signal_connect_object (GTK_OBJECT (progress_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_progress), NULL);

	prog_stop = 0;
	gtk_widget_show( progress_window );
}

int progress_update(float val)		// Update progress window
{
	gtk_progress_set_percentage( GTK_PROGRESS (progress_bar), val );

	while (gtk_events_pending()) gtk_main_iteration();

	return prog_stop;
}

void progress_end()			// Close progress window
{
	if ( progress_window != 0 )
	{
		gtk_widget_destroy( progress_window );
		progress_window = NULL;
	}
}



////	ALERT BOX

gint alert_result = 0;

gint alert_reply( GtkWidget *widget, gpointer data )
{
	if ( alert_result == 0 ) alert_result = (gint) data;
	if ( alert_result == 10 ) gtk_widget_destroy( widget );
	
	return FALSE;
}

int alert_box( char *title, char *message, char *text1, char *text2, char *text3 )
{
	GtkWidget *alert, *buttons[3], *label;
	char *butxt[3] = {text1, text2, text3};
	gint i;
	GtkAccelGroup* ag = gtk_accel_group_new();

	alert = gtk_dialog_new();
	gtk_window_set_title( GTK_WINDOW(alert), title );
	gtk_window_set_modal( GTK_WINDOW(alert), TRUE );
	gtk_window_set_position( GTK_WINDOW(alert), GTK_WIN_POS_CENTER );
	gtk_container_set_border_width( GTK_CONTAINER(alert), 6 );
	gtk_signal_connect( GTK_OBJECT(alert), "destroy",
			GTK_SIGNAL_FUNC(alert_reply), (gpointer) 10 );
	
	label = gtk_label_new( message );
	gtk_label_set_line_wrap( GTK_LABEL(label), TRUE );
	gtk_box_pack_start( GTK_BOX(GTK_DIALOG(alert)->vbox), label, TRUE, FALSE, 8 );
	gtk_widget_show( label );

	for ( i=0; i<=2; i++ )
	{
		if ( butxt[i] )
		{
			buttons[i] = add_a_button( butxt[i], 2, GTK_DIALOG(alert)->action_area, TRUE );
			gtk_signal_connect( GTK_OBJECT(buttons[i]), "clicked",
				GTK_SIGNAL_FUNC(alert_reply), (gpointer) (i+1) );
		}
	}
	gtk_widget_add_accelerator (buttons[0], "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_window_set_transient_for( GTK_WINDOW(alert), GTK_WINDOW(main_window) );
	gtk_widget_show(alert);
	alert_result = 0;
	gtk_window_add_accel_group(GTK_WINDOW(alert), ag);

	while ( alert_result == 0 ) gtk_main_iteration();
	if ( alert_result != 10 ) gtk_widget_destroy( alert );

	return alert_result;
}

// Slider-spin combo (practically a new widget class)

GtkWidget *mt_spinslide_new(gint swidth, gint sheight)
{
	GtkWidget *box, *slider, *spin;
	GtkObject *adj;

	adj = gtk_adjustment_new(0, 0, 1, 1, 10, 0);
	box = gtk_hbox_new(FALSE, 0);

	slider = gtk_hscale_new(GTK_ADJUSTMENT(adj));
	gtk_box_pack_start(GTK_BOX(box), slider, swidth < 0, TRUE, 0);
	gtk_widget_set_usize(slider, swidth, sheight);
	gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);
	gtk_scale_set_digits(GTK_SCALE(slider), 0);

	spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, 0);
	gtk_box_pack_start(GTK_BOX(box), spin, swidth >= 0, TRUE, 2);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);

	gtk_widget_show_all(box);
	return (box);
}

void mt_spinslide_set_range(GtkWidget *spinslide, gint minv, gint maxv)
{
	GtkAdjustment *adj;
	
	adj = gtk_range_get_adjustment(GTK_RANGE(((GtkBoxChild*)
		GTK_BOX(spinslide)->children->data)->widget));
	adj->lower = minv;
	adj->upper = maxv;
	gtk_adjustment_changed(adj);
}

gint mt_spinslide_get_value(GtkWidget *spinslide)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON(((GtkBoxChild*)GTK_BOX(spinslide)->children->
		next->data)->widget);
	gtk_spin_button_update(spin);
	return (gtk_spin_button_get_value_as_int(spin));
}

void mt_spinslide_set_value(GtkWidget *spinslide, gint value)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON(((GtkBoxChild*)GTK_BOX(spinslide)->children->
		next->data)->widget);
	gtk_spin_button_set_value(spin, value);
}

/* void handler(GtkAdjustment *adjustment, gpointer user_data); */
void mt_spinslide_connect(GtkWidget *spinslide, GtkSignalFunc handler,
	gpointer user_data)
{
	GtkAdjustment *adj;
	
	adj = gtk_range_get_adjustment(GTK_RANGE(((GtkBoxChild*)
		GTK_BOX(spinslide)->children->data)->widget));
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed", handler, user_data);
}

// Managing batches of radio buttons with minimum of fuss

static void wj_radio_toggle(GtkWidget *btn, gpointer idx)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) return;
	*(int *)gtk_object_get_user_data(GTK_OBJECT(btn->parent)) = (int)idx;
}

/* void handler(GtkWidget *btn, gpointer user_data); */
GtkWidget *wj_radio_pack(char **names, int cnt, int vnum, int idx, int *var,
	GtkSignalFunc handler)
{
	int i, j;
	GtkWidget *box, *wbox, *button = NULL;

	if (!handler) handler = GTK_SIGNAL_FUNC(wj_radio_toggle);
	box = wbox = vnum > 0 ? gtk_hbox_new(FALSE, 0) : gtk_vbox_new(FALSE, 0);
	if (vnum < 2) gtk_object_set_user_data(GTK_OBJECT(wbox), var);

	for (i = j = 0; (i != cnt) && names[i]; i++)
	{
		if (!names[i][0]) continue;
		button = gtk_radio_button_new_with_label_from_widget(
			GTK_RADIO_BUTTON(button), names[i]);
		if ((vnum > 1) && !(j % vnum))
		{
			wbox = gtk_vbox_new(FALSE, 0);
			gtk_box_pack_start(GTK_BOX(box), wbox, TRUE, TRUE, 0);
			gtk_object_set_user_data(GTK_OBJECT(wbox), var);
		}
		gtk_container_set_border_width(GTK_CONTAINER(button), 5);
		gtk_box_pack_start(GTK_BOX(wbox), button, FALSE, TRUE, 0);
		if (i == idx) gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(button), TRUE);
		gtk_signal_connect(GTK_OBJECT(button), "toggled", handler,
			(gpointer)(i));
		j++;
	}
	gtk_widget_show_all(box);

	if (var) *var = idx < i ? idx : 0;
	return (box);
}

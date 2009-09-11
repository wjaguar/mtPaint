/*	otherwindow.c
	Copyright (C) 2004-2006 Mark Tyler

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
#if GTK_MAJOR_VERSION == 1
	#include <unistd.h>
#endif

#include "global.h"

#include "memory.h"
#include "otherwindow.h"
#include "mygtk.h"
#include "mainwindow.h"
#include "viewer.h"
#include "inifile.h"
#include "canvas.h"
#include "png.h"
#include "quantizer.h"
#include "layer.h"
#include "wu.h"
#include "ani.h"
#include "toolbar.h"


///	NEW IMAGE WINDOW

int new_window_type = 0;
GtkWidget *new_window, *new_radio[4];
GtkWidget *spinbutton_height, *spinbutton_width, *spinbutton_cols;


gint delete_new( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gtk_widget_destroy(new_window);

	return FALSE;
}

void do_new_chores()
{
	float old_zoom = can_zoom;

	notify_unchanged();
	update_menus();

	mem_mask_setall(0);		// Clear all mask info
	mem_col_A = 1, mem_col_B = 0;
	tool_pat = 0;
	init_pal();

	can_zoom = -1;
	if ( inifile_get_gboolean("zoomToggle", FALSE) )
		align_size(1);			// Always start at 100%
	else
		align_size(old_zoom);

	set_new_filename( _("Untitled") );
	pressed_opacity( 255 );		// Set opacity to 100% to start with

	update_all_views();
	gtk_widget_queue_draw(drawing_col_prev);
}

int do_new_one(int nw, int nh, int nc, int nt, int bpp)
{
	int res;

	if ( nt != 1) mem_pal_copy( mem_pal, mem_pal_def );
#ifdef U_GUADALINEX
	else mem_scale_pal( 0, 255,255,255, nc-1, 0,0,0 );
#else
	else mem_scale_pal( 0, 0,0,0, nc-1, 255,255,255 );
#endif

	mtMIN( nw, nw, MAX_WIDTH )
	mtMAX( nw, nw, MIN_WIDTH )
	mtMIN( nh, nh, MAX_HEIGHT )
	mtMAX( nh, nh, MIN_HEIGHT )
	mtMAX( nc, nc, 2 )

	mem_cols = nc;
	res = mem_new( nw, nh, bpp );
	if ( res!= 0 )			// Not enough memory!
	{
		memory_errors(1);
	}
	do_new_chores();

	return res;
}

gint create_new( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int nw, nh, nc, nt = 2, i, j=4, bpp = 1, err=0;

	if ( new_window_type == 1 ) j=3;

	for ( i=0; i<j; i++ )
		if ( gtk_toggle_button_get_active(
			&(GTK_RADIO_BUTTON( new_radio[i] )->check_button.toggle_button)
			) ) nt = i;

	gtk_spin_button_update( GTK_SPIN_BUTTON(spinbutton_width) );
	gtk_spin_button_update( GTK_SPIN_BUTTON(spinbutton_height) );	// All needed in GTK+2 for late changes
	gtk_spin_button_update( GTK_SPIN_BUTTON(spinbutton_cols) );

	nw = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(spinbutton_width) );
	nh = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(spinbutton_height) );
	nc = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(spinbutton_cols) );

	if ( nt == 0 ) bpp = 3;

	if ( nt == 3 )		// Grab Screenshot
	{
#if GTK_MAJOR_VERSION == 1
		gdk_window_lower( main_window->window );
		gdk_window_lower( new_window->window );

		gdk_flush();
		while (gtk_events_pending()) gtk_main_iteration();	// Wait for minimize

		sleep(1);			// Wait a second for screen to redraw

		grab_screen();
		do_new_chores();
		notify_changed();

		gdk_window_raise( main_window->window );
#endif
#if GTK_MAJOR_VERSION == 2
		gtk_window_set_transient_for( GTK_WINDOW(new_window), NULL );
		gdk_window_iconify( new_window->window );
		gdk_window_iconify( main_window->window );

		gdk_flush();
		while (gtk_events_pending()) gtk_main_iteration();	// Wait for minimize

		g_usleep(400000);		// Wait 0.4 of a second for screen to redraw

		grab_screen();
		do_new_chores();
		notify_changed();

		gdk_window_deiconify( main_window->window );
		gdk_window_raise( main_window->window );
#endif
	}

	if ( nt < 3 && new_window_type == 0 )		// New image
	{
		err = do_new_one( nw, nh, nc, nt, bpp );

		if ( err>0 )		// System was unable to allocate memory for image, using 8x8 instead
		{
			nw = mem_width;
			nh = mem_height;  
		}

		if ( layers_total>0 ) layers_notify_changed();

		inifile_set_gint32("lastnewWidth", nw );
		inifile_set_gint32("lastnewHeight", nh );
		inifile_set_gint32("lastnewCols", nc );
		inifile_set_gint32("lastnewType", nt );

		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[PAINT_TOOL_ICON]), TRUE );
			// Set tool to square for new image - easy way to lose a selection marquee
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
	}

	if ( new_window_type == 1 ) layer_new( nw, nh, 3-nt, nc  );
	else
	{
		gtk_adjustment_value_changed( gtk_scrolled_window_get_hadjustment(
			GTK_SCROLLED_WINDOW(scrolledwindow_canvas) ) );
		gtk_adjustment_value_changed( gtk_scrolled_window_get_vadjustment(
			GTK_SCROLLED_WINDOW(scrolledwindow_canvas) ) );
		// These 2 are needed to synchronize the scrollbars & image view
	}

	gtk_widget_destroy(new_window);
	
	return FALSE;
}

void generic_new_window(int type)	// 0=New image, 1=New layer
{
	char *rad_txt[] = {_("24 bit RGB"), _("Greyscale"), _("Indexed Palette"), _("Grab Screenshot")},
		*title_txt[] = {_("New Image"), _("New Layer")};
	int w = mem_width, h = mem_height, c = mem_cols, im_type = 3 - mem_img_bpp;
	GSList *group;

	GtkWidget *vbox1, *hbox3;
	GtkWidget *table1;
	GtkWidget *button_create, *button_cancel;

	GtkAccelGroup* ag = gtk_accel_group_new();

	new_window_type = type;

	if ( type == 0 && check_for_changes() == 1 ) return;

	new_window = add_a_window( GTK_WINDOW_TOPLEVEL, title_txt[type], GTK_WIN_POS_CENTER, TRUE );

	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox1);
	gtk_container_add (GTK_CONTAINER (new_window), vbox1);

	add_hseparator( vbox1, 200, 10 );

	table1 = add_a_table( 3, 2, 5, vbox1 );

	if ( type == 0 )
	{
		w = inifile_get_gint32("lastnewWidth", DEFAULT_WIDTH);
		h = inifile_get_gint32("lastnewHeight", DEFAULT_HEIGHT);
		c = inifile_get_gint32("lastnewCols", 256);
		im_type = inifile_get_gint32("lastnewType", 2);
		if ( im_type<0 || im_type>2 ) im_type = 0;
	}

	spin_to_table( table1, &spinbutton_width, 0, 1, 5, w, MIN_WIDTH, MAX_WIDTH );
	spin_to_table( table1, &spinbutton_height, 1, 1, 5, h, MIN_WIDTH, MAX_HEIGHT );
	spin_to_table( table1, &spinbutton_cols, 2, 1, 5, c, 2, 256 );

	add_to_table( _("Width"), table1, 0, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5 );
	add_to_table( _("Height"), table1, 1, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5 );
	add_to_table( _("Colours"), table1, 2, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5 );

	new_radio[0] = add_radio_button( rad_txt[0], NULL,  NULL, vbox1, 0 );
	group = gtk_radio_button_group( GTK_RADIO_BUTTON(new_radio[0]) );
	new_radio[1] = add_radio_button( rad_txt[1], group, NULL, vbox1, 1 );
	new_radio[2] = add_radio_button( rad_txt[2], NULL,  new_radio[1], vbox1, 2 );
	if ( type == 0 )
		new_radio[3] = add_radio_button( rad_txt[3], NULL,  new_radio[1], vbox1, 3 );

	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON( new_radio[im_type]), TRUE );

	add_hseparator( vbox1, 200, 10 );

	hbox3 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox3);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox3, FALSE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (hbox3), 5);

	button_cancel = add_a_button(_("Cancel"), 5, hbox3, TRUE);
	gtk_signal_connect_object( GTK_OBJECT(button_cancel), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(new_window));
	gtk_widget_add_accelerator (button_cancel, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button_create = add_a_button(_("Create"), 5, hbox3, TRUE);
	gtk_signal_connect_object( GTK_OBJECT(button_create), "clicked",
			GTK_SIGNAL_FUNC(create_new), GTK_OBJECT(new_window));
	gtk_widget_add_accelerator (button_create, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button_create, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);

	gtk_signal_connect_object (GTK_OBJECT (new_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_new), NULL);

	gtk_window_set_transient_for( GTK_WINDOW(new_window), GTK_WINDOW(main_window) );
	gtk_widget_show (new_window);
	gtk_window_add_accel_group(GTK_WINDOW (new_window), ag);
}

void pressed_new( GtkMenuItem *menu_item, gpointer user_data )
{
	generic_new_window(0);
}


///	COLOUR A/B EDITOR WINDOW

#define RGB_preview_width 64
#define RGB_preview_height 32

png_color a_old, b_old, a_new, b_new;

GtkWidget *col_window;
GtkWidget *label_A_RGB_2, *label_B_RGB_2;
GtkWidget *drawingarea_A_1, *drawingarea_B_1;
GtkWidget *drawingarea_A_2, *drawingarea_B_2;

GtkWidget *hscale_A_R, *hscale_A_G, *hscale_A_B;
GtkWidget *hscale_B_R, *hscale_B_G, *hscale_B_B;

GtkWidget *checkbutton_posterize, *spinbutton_posterize;

void paint_colour( char *mem, GtkWidget *drawing_area, int r, int g, int b, int x, int y, int w, int h)
{
	int i;
	
	for ( i=0; i<(RGB_preview_width*RGB_preview_height); i++ )
	{
		mem[ 0 + 3*i ] = r;
		mem[ 1 + 3*i ] = g;
		mem[ 2 + 3*i ] = b;
	}
	
	gdk_draw_rgb_image (drawing_area->window,
			drawing_area->style->black_gc,
			x, y, w, h,
			GDK_RGB_DITHER_NONE, mem, w*3
			);
}

static gint expose_colours( GtkWidget *widget, GdkEventExpose *event )
{
	char *rgb;
	int x = event->area.x, y = event->area.y;
	int w = event->area.width, h = event->area.height;

	mtMIN( w, w, RGB_preview_width )
	mtMIN( h, h, RGB_preview_width )

	rgb = grab_memory( RGB_preview_width*RGB_preview_height*3, 0 );

//	This expose event could be from either of the 4 areas, so update all
//	This is very lazy, but it seems OK

	paint_colour( rgb, drawingarea_A_1, a_old.red, a_old.green, a_old.blue, x, y, w, h );
	paint_colour( rgb, drawingarea_B_1, b_old.red, b_old.green, b_old.blue, x, y, w, h );
	paint_colour( rgb, drawingarea_A_2, a_new.red, a_new.green, a_new.blue, x, y, w, h );
	paint_colour( rgb, drawingarea_B_2, b_new.red, b_new.green, b_new.blue, x, y, w, h );

	free( rgb );

	return FALSE;
}

void update_RGB_labels()	// Update labels with values in a/b_new & impose posterizing
{
	int hue[6] = { a_new.red, a_new.green, a_new.blue, b_new.red, b_new.green, b_new.blue };
	int posty, i;
	char txt[64];

	if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_posterize)) )
	{
		posty = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(spinbutton_posterize) );

		for ( i=0; i<6; i++) hue[i] = do_posterize( hue[i], posty );

		a_new.red = hue[0]; a_new.green = hue[1]; a_new.blue = hue[2];
		b_new.red = hue[3]; b_new.green = hue[4]; b_new.blue = hue[5];
	}

	snprintf(txt, 60, "%i,%i,%i", hue[0], hue[1], hue[2]);
	gtk_label_set_text( GTK_LABEL(label_A_RGB_2), txt );

	snprintf(txt, 60, "%i,%i,%i", hue[3], hue[4], hue[5]);
	gtk_label_set_text( GTK_LABEL(label_B_RGB_2), txt );

	gtk_widget_queue_draw(drawingarea_A_1);
}

gint slider_moved( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int	ar = GTK_HSCALE(hscale_A_R)->scale.range.adjustment->value,
		ag = GTK_HSCALE(hscale_A_G)->scale.range.adjustment->value,
		ab = GTK_HSCALE(hscale_A_B)->scale.range.adjustment->value;
	int	br = GTK_HSCALE(hscale_B_R)->scale.range.adjustment->value,
		bg = GTK_HSCALE(hscale_B_G)->scale.range.adjustment->value,
		bb = GTK_HSCALE(hscale_B_B)->scale.range.adjustment->value;

	a_new.red = ar;
	a_new.green = ag;
	a_new.blue = ab;
	b_new.red = br;
	b_new.green = bg;
	b_new.blue = bb;
	
	update_RGB_labels();

	return FALSE;
}

void update_RGB_sliders()	// Update sliders with values in a/b new
{
	int ar = a_new.red, ag = a_new.green, ab = a_new.blue;
	int br = b_new.red, bg = b_new.green, bb = b_new.blue;

	gtk_adjustment_set_value( GTK_HSCALE(hscale_A_R)->scale.range.adjustment, ar );
	gtk_adjustment_set_value( GTK_HSCALE(hscale_A_G)->scale.range.adjustment, ag );
	gtk_adjustment_set_value( GTK_HSCALE(hscale_A_B)->scale.range.adjustment, ab );

	gtk_adjustment_set_value( GTK_HSCALE(hscale_B_R)->scale.range.adjustment, br );
	gtk_adjustment_set_value( GTK_HSCALE(hscale_B_G)->scale.range.adjustment, bg );
	gtk_adjustment_set_value( GTK_HSCALE(hscale_B_B)->scale.range.adjustment, bb );

	update_RGB_labels();
}

void pal_refresher()
{
	update_all_views();
	init_pal();
	gtk_widget_queue_draw(drawing_col_prev);
}

void implement_cols(png_color A, png_color B)
{
	if ( mem_img_bpp == 1 )
	{
		mem_pal[mem_col_A] = A;
		mem_pal[mem_col_B] = B;
	}
	mem_col_A24 = A;
	mem_col_B24 = B;
	pal_refresher();
}

gint delete_col( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	inifile_set_gboolean( "posterizeToggle",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_posterize)) );
	inifile_set_gint32("posterizeInt",
		gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(spinbutton_posterize) ) );

	gtk_widget_destroy(col_window);

	return FALSE;
}

gint click_col_cancel( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	implement_cols( a_old, b_old );
	delete_col( NULL, NULL, NULL );

	return FALSE;
}

gint click_col_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	implement_cols( a_old, b_old );

	spot_undo(UNDO_PAL);

	implement_cols( a_new, b_new );
	delete_col( NULL, NULL, NULL );

	return FALSE;
}

gint click_col_preview( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	implement_cols( a_new, b_new );

	return FALSE;
}

gint click_col_reset( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	a_new = a_old;
	b_new = b_old;

	update_RGB_sliders();

	implement_cols( a_old, b_old );

	return FALSE;
}

gint posterize_click( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_posterize)) )
	{
		update_RGB_labels();
		update_RGB_sliders();
	}

	return FALSE;
}

void choose_colours()
{
	GtkWidget *vbox2, *table2;
	GtkWidget *label_A, *label_B, *label_A_RGB_1, *label_B_RGB_1;
	GtkWidget *drawingarea5, *drawingarea6;
	GtkWidget *hbox_middle, *hbox_bottom;
	GtkWidget *button_ok, *button_apply, *button_reset, *button_cancel;
	char txt[64];

	GtkAccelGroup* ag = gtk_accel_group_new();


	if ( mem_img_bpp == 1 )
	{
		a_old = mem_pal[mem_col_A];
		b_old = mem_pal[mem_col_B];
	}
	else
	{
		a_old = mem_col_A24;
		b_old = mem_col_B24;
	}
	a_new = a_old;
	b_new = b_old;

	col_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Colour Editor"), GTK_WIN_POS_MOUSE, TRUE );

	vbox2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox2);
	gtk_container_add (GTK_CONTAINER (col_window), vbox2);

	table2 = add_a_table( 6, 4, 5, vbox2 );

	if ( mem_img_bpp == 1 )
		snprintf(txt, 60, "A [%i]", mem_col_A);
	else
		sprintf(txt, "A");

	label_A = add_to_table( txt, table2, 0, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	snprintf(txt, 60, "%i,%i,%i", a_old.red, a_old.green, a_old.blue);
	label_A_RGB_1 = add_to_table( txt, table2, 1, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	gtk_widget_set_usize (label_A_RGB_1, 80, -2);
	label_A_RGB_2 = add_to_table( txt, table2, 2, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	gtk_widget_set_usize (label_A_RGB_2, 80, -2);

	if ( mem_img_bpp == 1 )
		snprintf(txt, 60, "B [%i]", mem_col_B);
	else
		sprintf(txt, "B");

	label_B = add_to_table( txt, table2, 3, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	snprintf(txt, 60, "%i,%i,%i", b_old.red, b_old.green, b_old.blue);
	label_B_RGB_1 = add_to_table( txt, table2, 4, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	label_B_RGB_2 = add_to_table( txt, table2, 5, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5);

	add_to_table( _("Red"), table2, 0, 3, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	add_to_table( _("Green"), table2, 1, 3, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	add_to_table( _("Blue"), table2, 2, 3, 0, GTK_JUSTIFY_LEFT, 0, 0.5);

	add_to_table( _("Red"), table2, 3, 3, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	add_to_table( _("Green"), table2, 4, 3, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	add_to_table( _("Blue"), table2, 5, 3, 0, GTK_JUSTIFY_LEFT, 0, 0.5);

	drawingarea_A_1 = gtk_drawing_area_new ();
	gtk_widget_show (drawingarea_A_1);
	gtk_table_attach (GTK_TABLE (table2), drawingarea_A_1, 1, 2, 1, 2,
		(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		(GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_widget_set_usize (drawingarea_A_1, RGB_preview_width, RGB_preview_height);
	gtk_signal_connect_object( GTK_OBJECT(drawingarea_A_1), "expose_event",
		GTK_SIGNAL_FUNC (expose_colours), NULL );

	drawingarea_A_2 = gtk_drawing_area_new ();
	gtk_widget_show (drawingarea_A_2);
	gtk_table_attach (GTK_TABLE (table2), drawingarea_A_2, 1, 2, 2, 3,
		(GtkAttachOptions) (GTK_FILL),
		(GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_widget_set_usize (drawingarea_A_2, RGB_preview_width, RGB_preview_height);
	gtk_signal_connect_object( GTK_OBJECT(drawingarea_A_2), "expose_event",
		GTK_SIGNAL_FUNC (expose_colours), NULL );

	drawingarea_B_1 = gtk_drawing_area_new ();
	gtk_widget_show (drawingarea_B_1);
	gtk_table_attach (GTK_TABLE (table2), drawingarea_B_1, 1, 2, 4, 5,
		(GtkAttachOptions) (GTK_FILL),
		(GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_widget_set_usize (drawingarea_B_1, RGB_preview_width, RGB_preview_height);
	gtk_signal_connect_object( GTK_OBJECT(drawingarea_B_1), "expose_event",
		GTK_SIGNAL_FUNC (expose_colours), NULL );

	drawingarea_B_2 = gtk_drawing_area_new ();
	gtk_widget_show (drawingarea_B_2);
	gtk_table_attach (GTK_TABLE (table2), drawingarea_B_2, 1, 2, 5, 6,
		(GtkAttachOptions) (GTK_FILL),
		(GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_widget_set_usize (drawingarea_B_2, RGB_preview_width, RGB_preview_height);
	gtk_signal_connect_object( GTK_OBJECT(drawingarea_B_2), "expose_event",
		GTK_SIGNAL_FUNC (expose_colours), NULL );

	hscale_A_R = add_slider2table( a_new.red, 0, 255, table2, 0, 2, 255, 20 );
	hscale_A_G = add_slider2table( a_new.green, 0, 255, table2, 1, 2, 255, 20 );
	hscale_A_B = add_slider2table( a_new.blue, 0, 255, table2, 2, 2, 255, 20 );

	hscale_B_R = add_slider2table( b_new.red, 0, 255, table2, 3, 2, 255, 20 );
	hscale_B_G = add_slider2table( b_new.green, 0, 255, table2, 4, 2, 255, 20 );
	hscale_B_B = add_slider2table( b_new.blue, 0, 255, table2, 5, 2, 255, 20 );

	gtk_signal_connect( GTK_OBJECT(GTK_HSCALE(hscale_A_R)->scale.range.adjustment), "value_changed",
		GTK_SIGNAL_FUNC(slider_moved), NULL);
	gtk_signal_connect( GTK_OBJECT(GTK_HSCALE(hscale_A_G)->scale.range.adjustment), "value_changed",
		GTK_SIGNAL_FUNC(slider_moved), NULL);
	gtk_signal_connect( GTK_OBJECT(GTK_HSCALE(hscale_A_B)->scale.range.adjustment), "value_changed",
		GTK_SIGNAL_FUNC(slider_moved), NULL);

	gtk_signal_connect( GTK_OBJECT(GTK_HSCALE(hscale_B_R)->scale.range.adjustment), "value_changed",
		GTK_SIGNAL_FUNC(slider_moved), NULL);
	gtk_signal_connect( GTK_OBJECT(GTK_HSCALE(hscale_B_G)->scale.range.adjustment), "value_changed",
		GTK_SIGNAL_FUNC(slider_moved), NULL);
	gtk_signal_connect( GTK_OBJECT(GTK_HSCALE(hscale_B_B)->scale.range.adjustment), "value_changed",
		GTK_SIGNAL_FUNC(slider_moved), NULL);

	drawingarea5 = gtk_drawing_area_new ();
	gtk_widget_show (drawingarea5);
	gtk_table_attach (GTK_TABLE (table2), drawingarea5, 1, 2, 0, 1,
		(GtkAttachOptions) (GTK_FILL),
		(GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_widget_set_usize (drawingarea5, RGB_preview_width, RGB_preview_height);

	drawingarea6 = gtk_drawing_area_new ();
	gtk_widget_show (drawingarea6);
	gtk_table_attach (GTK_TABLE (table2), drawingarea6, 1, 2, 3, 4,
		(GtkAttachOptions) (GTK_FILL),
		(GtkAttachOptions) (GTK_FILL), 0, 0);
	gtk_widget_set_usize (drawingarea6, RGB_preview_width, RGB_preview_height);

	add_hseparator( vbox2, -2, 10 );

	hbox_middle = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox_middle);
	gtk_box_pack_start (GTK_BOX (vbox2), hbox_middle, FALSE, FALSE, 0);

	checkbutton_posterize = add_a_toggle( _("Posterize"),
		hbox_middle, inifile_get_gboolean("posterizeToggle", FALSE) );

	gtk_signal_connect(GTK_OBJECT(checkbutton_posterize), "clicked",
		GTK_SIGNAL_FUNC(posterize_click), NULL);

	spinbutton_posterize = add_a_spin( inifile_get_gint32("posterizeInt", 1), 1, 8 );
	gtk_box_pack_start (GTK_BOX (hbox_middle), spinbutton_posterize, FALSE, FALSE, 0);

	gtk_signal_connect(GTK_OBJECT( &GTK_SPIN_BUTTON(spinbutton_posterize)->entry ), "changed",
		GTK_SIGNAL_FUNC(posterize_click), NULL);

	add_hseparator( vbox2, -2, 10 );

	hbox_bottom = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox_bottom);
	gtk_box_pack_start (GTK_BOX (vbox2), hbox_bottom, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (hbox_bottom), 5);

	button_cancel = add_a_button(_("Cancel"), 4, hbox_bottom, TRUE);
	gtk_signal_connect(GTK_OBJECT(button_cancel), "clicked", GTK_SIGNAL_FUNC(click_col_cancel), NULL);
	gtk_signal_connect_object (GTK_OBJECT (col_window), "delete_event",
		GTK_SIGNAL_FUNC (click_col_cancel), NULL);
	gtk_widget_add_accelerator (button_cancel, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button_apply = add_a_button(_("Preview"), 4, hbox_bottom, TRUE);
	gtk_signal_connect(GTK_OBJECT(button_apply), "clicked",
		GTK_SIGNAL_FUNC(click_col_preview), NULL);

	button_reset = add_a_button(_("Reset"), 4, hbox_bottom, TRUE);
	gtk_signal_connect(GTK_OBJECT(button_reset), "clicked",
		GTK_SIGNAL_FUNC(click_col_reset), NULL);

	button_ok = add_a_button(_("OK"), 4, hbox_bottom, TRUE );
	gtk_signal_connect(GTK_OBJECT(button_ok), "clicked",
		GTK_SIGNAL_FUNC(click_col_ok), NULL);

	update_RGB_sliders();				// Adjust for posterize & set sliders properly

	gtk_window_set_transient_for( GTK_WINDOW(col_window), GTK_WINDOW(main_window) );
	gtk_widget_show (col_window);
	gtk_window_add_accel_group(GTK_WINDOW (col_window), ag);
}



///	PATTERN & BRUSH CHOOSER WINDOW

static GtkWidget *pat_window, *draw_pat;
static int pat_brush;
static unsigned char *mem_patch = NULL;


static gint delete_pat( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( pat_brush == 0 )
	{
		if ( mem_patch != NULL ) free(mem_patch);
		mem_patch = NULL;
	}
	gtk_widget_destroy(pat_window);

	return FALSE;
}

static gint key_pat( GtkWidget *widget, GdkEventKey *event )
{
	if ( event->keyval!=65505 && event->keyval!=65507 ) delete_pat( widget, NULL, NULL );
		// Xine sends 6550x key values so don't delete on this

	return FALSE;
}

static gint click_pat( GtkWidget *widget, GdkEventButton *event )
{
	int pat_no = 0, mx, my;

	mx = event->x;
	my = event->y;

	pat_no = mx / (PATCH_WIDTH/9) + 9*( my / (PATCH_HEIGHT/9) );
	mtMAX(pat_no, pat_no, 0)
	mtMIN(pat_no, pat_no, 80)

	if ( event->button == 1 )
	{
		if ( pat_brush == 0 )
		{
			tool_pat = pat_no;
			mem_pat_update();				// Update memory
			gtk_widget_queue_draw( drawing_col_prev );	// Update widget
			if ( marq_status >= MARQUEE_PASTE && text_paste )
			{
				render_text( drawing_col_prev );
				check_marquee();
				gtk_widget_queue_draw( drawing_canvas );
			}
		}
		else
		{
			mem_set_brush(pat_no);
			brush_tool_type = tool_type;
			toolbar_update_settings();	// Update spin buttons
			gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[PAINT_TOOL_ICON]),
					TRUE );			// Update toolbar
			set_cursor();
		}
	}

	return FALSE;
}

static gint expose_pat( GtkWidget *widget, GdkEventExpose *event )
{
	gdk_draw_rgb_image( draw_pat->window, draw_pat->style->black_gc,
				event->area.x, event->area.y, event->area.width, event->area.height,
				GDK_RGB_DITHER_NONE,
				mem_patch + 3*( event->area.x + PATCH_WIDTH*event->area.y ),
				PATCH_WIDTH*3
				);
	return FALSE;
}

void choose_pattern(int typ)			// Bring up pattern chooser (0) or brush (1)
{
	int pattern, pixel, r, g, b, row, column, sx, sy, ex, ey;

	pat_brush = typ;

	if ( typ == 0 )
	{
		mem_patch = grab_memory( 3*PATCH_WIDTH*PATCH_HEIGHT, 0 );

		for ( pattern = 0; pattern < 81; pattern++ )
		{
			sy = 2 + (pattern / 9) * 36;		// Start y pixel on main image
			sx = 2 + (pattern % 9) * 36;		// Start x pixel on main image
			for ( column = 0; column < 8; column++ )
			{
				for ( row = 0; row < 8; row++ )
				{
					pixel = mem_patterns[pattern][column][row];
					if ( pixel == 1 )
					{
						r = mem_col_A24.red;
						g = mem_col_A24.green;
						b = mem_col_A24.blue;
					}
					else
					{
						r = mem_col_B24.red;
						g = mem_col_B24.green;
						b = mem_col_B24.blue;
					}
					for ( ey=0; ey<4; ey++ )
					{
					 for ( ex=0; ex<4; ex++ )
					 {
					  mem_patch[ 0+3*( sx+row+8*ex + PATCH_WIDTH*(sy+column+8*ey) ) ] = r;
					  mem_patch[ 1+3*( sx+row+8*ex + PATCH_WIDTH*(sy+column+8*ey) ) ] = g;
					  mem_patch[ 2+3*( sx+row+8*ex + PATCH_WIDTH*(sy+column+8*ey) ) ] = b;
					 }
					}
				}
			}
		}
	}
	else
	{
		mem_patch = mem_brushes;
	}

	pat_window = add_a_window( GTK_WINDOW_POPUP, _("Pattern Chooser"), GTK_WIN_POS_MOUSE, TRUE );
	gtk_container_set_border_width (GTK_CONTAINER (pat_window), 4);

	draw_pat = gtk_drawing_area_new ();
	gtk_widget_set_usize( draw_pat, PATCH_WIDTH, PATCH_HEIGHT );
	gtk_container_add (GTK_CONTAINER (pat_window), draw_pat);
	gtk_widget_show( draw_pat );
	gtk_signal_connect_object( GTK_OBJECT(draw_pat), "expose_event",
		GTK_SIGNAL_FUNC (expose_pat), GTK_OBJECT(draw_pat) );
	gtk_signal_connect_object( GTK_OBJECT(draw_pat), "button_press_event",
		GTK_SIGNAL_FUNC (click_pat), GTK_OBJECT(draw_pat) );
	gtk_signal_connect_object( GTK_OBJECT(draw_pat), "button_release_event",
		GTK_SIGNAL_FUNC (delete_pat), NULL );
	gtk_signal_connect_object (GTK_OBJECT (pat_window), "key_press_event",
		GTK_SIGNAL_FUNC (key_pat), NULL);
	gtk_widget_set_events (draw_pat, GDK_ALL_EVENTS_MASK);

	gtk_widget_show (pat_window);
}



///	ADD COLOURS TO PALETTE WINDOW


GtkWidget *add_col_window;
GtkWidget *spinbutton_col_add;


gint delete_col_add( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gtk_widget_destroy(add_col_window);

	return FALSE;
}

gint click_col_add_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i, to_add;

	gtk_spin_button_update( GTK_SPIN_BUTTON(spinbutton_col_add) );
	to_add = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(spinbutton_col_add) );

	if ( to_add != mem_cols )
	{
		spot_undo(UNDO_PAL);

		if ( to_add>mem_cols )
			for ( i=mem_cols; i<to_add; i++ )
			{
				mem_pal[i].red = 0;
				mem_pal[i].green = 0;
				mem_pal[i].blue = 0;
			}

		mem_cols = to_add;
		if ( mem_img_bpp == 1 )
		{
			if ( mem_col_A >= mem_cols ) mem_col_A = 0;
			if ( mem_col_B >= mem_cols ) mem_col_B = 0;
		}
		init_pal();
	}

	gtk_widget_destroy(add_col_window);

	return FALSE;
}


void pressed_add_cols( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *vbox5, *hbox6;
	GtkWidget *button_cancel, *button_ok;

	GtkAccelGroup* ag = gtk_accel_group_new();

	add_col_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Set Palette Size"),
		GTK_WIN_POS_CENTER, TRUE );

	gtk_widget_set_usize (add_col_window, 320, -2);

	vbox5 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox5);
	gtk_container_add (GTK_CONTAINER (add_col_window), vbox5);

	add_hseparator( vbox5, -2, 10 );

	spinbutton_col_add = add_a_spin( 256, 2, 256 );
	gtk_box_pack_start (GTK_BOX (vbox5), spinbutton_col_add, FALSE, FALSE, 5);

	add_hseparator( vbox5, -2, 10 );

	hbox6 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox6);
	gtk_box_pack_start (GTK_BOX (vbox5), hbox6, FALSE, FALSE, 0);

	button_cancel = add_a_button(_("Cancel"), 5, hbox6, TRUE);
	gtk_signal_connect(GTK_OBJECT(button_cancel), "clicked", GTK_SIGNAL_FUNC(delete_col_add), NULL);
	gtk_signal_connect_object (GTK_OBJECT (add_col_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_col_add), NULL);
	gtk_widget_add_accelerator (button_cancel, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button_ok = add_a_button(_("OK"), 5, hbox6, TRUE);
	gtk_signal_connect(GTK_OBJECT(button_ok), "clicked", GTK_SIGNAL_FUNC(click_col_add_ok), NULL);
	gtk_widget_add_accelerator (button_ok, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button_ok, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);

	gtk_widget_show (add_col_window);
	gtk_window_add_accel_group(GTK_WINDOW (add_col_window), ag);
}


///	CREATE PALETTE SCALE

void pressed_create_pscale( GtkMenuItem *menu_item, gpointer user_data )
{
	int i = mem_col_A, j = mem_col_B;

	mtMAX( i, i, 0 )
	mtMAX( j, j, 0 )
	mtMIN( i, i, mem_cols-1 )
	mtMIN( j, j, mem_cols-1 )

	if ( abs(i-j)>1 )		// Only do this if we have something to do
	{
		spot_undo(UNDO_PAL);

		mem_scale_pal( i, mem_col_A24.red, mem_col_A24.green, mem_col_A24.blue,
			j, mem_col_B24.red, mem_col_B24.green, mem_col_B24.blue );

		init_pal();
		update_all_views();
		gtk_widget_queue_draw( drawing_col_prev );
	}
}



///	BACTERIA EFFECT

int bac_type;
	// Type of form needed 0-5 = bacteria, blur, sharpen, soften, rotate, set key frame
GtkWidget *bacteria_window;
GtkWidget *spin_bacteria, *bac_toggles[1];


gint delete_bacteria( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gtk_widget_destroy(bacteria_window);

	return FALSE;
}

gint bacteria_apply( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i, j, smooth = 0;
	float angle;

	gtk_spin_button_update( GTK_SPIN_BUTTON(spin_bacteria) );
	i = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(spin_bacteria) );
	angle = gtk_spin_button_get_value_as_float( GTK_SPIN_BUTTON(spin_bacteria) );

	if (bac_type >= 4)
	{
		if ( bac_type == 5 )		// Set key frame
		{
			ani_set_key_frame(i);
			layers_notify_changed();
		}
		if ( bac_type == 4 )
		{
			if ( mem_img_bpp == 3 )
			{
				if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(bac_toggles[0])) )
					smooth = 1;
				else	smooth = 0;
			}
			j = mem_rotate_free( angle, smooth );
			if ( j == 0 ) canvas_undo_chores();
			else
			{
				if ( j == -5 ) alert_box(_("Error"),
					_("The image is too large for this rotation."),
					_("OK"), NULL, NULL);
				else memory_errors(j);
			}
		}
	}
	else
	{
		spot_undo(UNDO_FILT);
		if (bac_type == 0) mem_bacteria( i );
		if (bac_type == 1)
		{
			progress_init(_("Image Blur Effect"),1);
			for ( j=0; j<i; j++ )
			{
				if (progress_update( ((float) j)/i )) break;
				do_effect(1, 25 + i/2);
			}
			progress_end();
		}
		if (bac_type == 2) do_effect(3, i);
		if (bac_type == 3) do_effect(4, i);
	}
	if (bac_type != 0) delete_bacteria( NULL, NULL, NULL );
	update_all_views();

	return FALSE;
}

void bac_form( int type )
{
	char *title = NULL;
	int startv = 10, min = 1, max = 100;
	GtkWidget *hbox7, *button_cancel, *button_apply, *vbox6;
	GtkAccelGroup* ag = gtk_accel_group_new();

	bac_type = type;
	if (type == 0) title = _("Bacteria Effect");
	if (type == 1) title = _("Blur Effect");
	if (type == 2)
	{
		title = _("Edge Sharpen");
		startv = 50;
	}
	if (type == 3)
	{
		title = _("Edge Soften");
		startv = 50;
	}
	if (type == 4)
	{
		title = _("Free Rotate");
		startv = 45;
		min = -360;
		max = 360;
	}
	if (type == 5)
	{
		title = _("Set Key Frame");
		startv = ani_frame1;
		min = ani_frame1;
		max = ani_frame2;
	}

	bacteria_window = add_a_window(GTK_WINDOW_TOPLEVEL, title, GTK_WIN_POS_CENTER, TRUE);
	gtk_widget_set_usize (bacteria_window, 300, -2);

	vbox6 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox6);
	gtk_container_add (GTK_CONTAINER (bacteria_window), vbox6);

	add_hseparator( vbox6, -2, 10 );

	spin_bacteria = add_a_spin( startv, min, max );
	gtk_box_pack_start (GTK_BOX (vbox6), spin_bacteria, FALSE, FALSE, 5);

	if ( mem_img_bpp == 3 && type == 4 )
		bac_toggles[0] = add_a_toggle( _("Smooth"), vbox6, TRUE );

	if (type == 4) gtk_spin_button_set_digits(GTK_SPIN_BUTTON(spin_bacteria), 2);

	add_hseparator( vbox6, -2, 10 );

	hbox7 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox7);
	gtk_box_pack_start (GTK_BOX (vbox6), hbox7, FALSE, FALSE, 0);

	button_cancel = add_a_button(_("Cancel"), 5, hbox7, TRUE);
	gtk_signal_connect(GTK_OBJECT(button_cancel), "clicked", GTK_SIGNAL_FUNC(delete_bacteria), NULL);
	gtk_signal_connect_object (GTK_OBJECT (bacteria_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_bacteria), NULL);
	gtk_widget_add_accelerator (button_cancel, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button_apply = add_a_button(_("Apply"), 5, hbox7, TRUE);
	gtk_signal_connect(GTK_OBJECT(button_apply), "clicked", GTK_SIGNAL_FUNC(bacteria_apply), NULL);
	gtk_widget_add_accelerator (button_apply, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button_apply, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);

	gtk_widget_show (bacteria_window);
	gtk_window_add_accel_group(GTK_WINDOW (bacteria_window), ag);
}

void pressed_bacteria( GtkMenuItem *menu_item, gpointer user_data )
{	bac_form(0); }


///	SORT PALETTE COLOURS

GtkWidget *spal_window, *spal_spins[2], *spal_radio[8];


gint click_spal_apply( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i, type = 2, index1 = 0, index2 = 1;
	gboolean reverse;

	for ( i=0; i<8; i++ )
	{
		if ( i!=3 )
			if ( gtk_toggle_button_get_active(
				&(GTK_RADIO_BUTTON( spal_radio[i] )->check_button.toggle_button)
					) ) type = i;
	}
	inifile_set_gint32("lastspalType", type );
	if ( type >=3 ) type--;

	reverse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(spal_radio[3]));
	inifile_set_gboolean( "palrevSort", reverse );

	gtk_spin_button_update( GTK_SPIN_BUTTON(spal_spins[0]) );
	gtk_spin_button_update( GTK_SPIN_BUTTON(spal_spins[1]) );

	index1 = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(spal_spins[0]) );
	index2 = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(spal_spins[1]) );

	if ( index1 == index2 ) return FALSE;

	spot_undo(UNDO_XPAL);
	mem_pal_sort(type, index1, index2, reverse);
	init_pal();
	update_all_views();
	gtk_widget_queue_draw( drawing_col_prev );

	return FALSE;
}

gint click_spal_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	click_spal_apply( NULL, NULL, NULL );
	gtk_widget_destroy(spal_window);

	return FALSE;
}

void pressed_sort_pal( GtkMenuItem *menu_item, gpointer user_data )
{
	char *rad_txt[] = {_("Hue"), _("Saturation"), _("Luminance"), "",
				_("Red"), _("Green"), _("Blue"), _("Frequency") };
	int i;

	GSList *group;
	GtkWidget *vbox1, *hbox3, *hbox, *vbox[2];
	GtkWidget *table1;
	GtkWidget *button;
	GtkAccelGroup* ag = gtk_accel_group_new();

	spal_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Sort Palette Colours"),
		GTK_WIN_POS_CENTER, TRUE );
	gtk_widget_set_usize (GTK_WIDGET (spal_window), 300, -2);

	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox1);
	gtk_container_add (GTK_CONTAINER (spal_window), vbox1);

	add_hseparator( vbox1, 200, 10 );

	table1 = add_a_table( 2, 2, 5, vbox1 );

	spin_to_table( table1, &spal_spins[0], 0, 1, 5, 0, 0, mem_cols-1 );
	spin_to_table( table1, &spal_spins[1], 1, 1, 5, mem_cols-1, 0, mem_cols-1 );

	add_to_table( _("Start Index"), table1, 0, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5 );
	add_to_table( _("End Index"), table1, 1, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5 );

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox, FALSE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);

	for ( i=0; i<2; i++ )
	{
		vbox[i] = gtk_vbox_new (FALSE, 0);
		gtk_widget_show (vbox[i]);
		gtk_container_add (GTK_CONTAINER (hbox), vbox[i]);
	}

	spal_radio[0] = add_radio_button( rad_txt[0], NULL,  NULL, vbox[0], 0 );
	group = gtk_radio_button_group( GTK_RADIO_BUTTON(spal_radio[0]) );
	spal_radio[1] = add_radio_button( rad_txt[1], group, NULL, vbox[0], 1 );
	spal_radio[2] = add_radio_button( rad_txt[2], NULL,  spal_radio[1], vbox[0], 2 );

	spal_radio[3] = add_a_toggle( _("Reverse Order"), vbox[0],
		inifile_get_gboolean("palrevSort", FALSE) );

	for ( i=4; i<8; i++ )
		spal_radio[i] = add_radio_button( rad_txt[i], NULL,  spal_radio[1], vbox[1], i );

	i = inifile_get_gint32("lastspalType", 2);
	if ( mem_img_bpp == 3 )
	{
		if ( i == 7 ) i--;
		gtk_widget_set_sensitive( spal_radio[7], FALSE );
	}
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(spal_radio[i]), TRUE );


	add_hseparator( vbox1, 200, 10 );

	hbox3 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox3);
	gtk_box_pack_start (GTK_BOX (vbox1), hbox3, FALSE, TRUE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (hbox3), 5);

	button = add_a_button(_("Cancel"), 5, hbox3, TRUE);
	gtk_signal_connect_object( GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(spal_window));
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button = add_a_button(_("Apply"), 5, hbox3, TRUE);
	gtk_signal_connect_object( GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(click_spal_apply), GTK_OBJECT(spal_window));

	button = add_a_button(_("OK"), 5, hbox3, TRUE);
	gtk_signal_connect_object( GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(click_spal_ok), GTK_OBJECT(spal_window));
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);

	gtk_widget_show (spal_window);
	gtk_window_add_accel_group(GTK_WINDOW (spal_window), ag);
}


///	BRIGHTNESS-CONTRAST-SATURATION WINDOW

#define BRCOSA_ITEMS 5

GtkWidget *brcosa_window;
GtkWidget *brcosa_scales[BRCOSA_ITEMS], *brcosa_toggles[6], *brcosa_spins[BRCOSA_ITEMS];
GtkWidget *brcosa_buttons[5];

int brcosa_values[BRCOSA_ITEMS];
png_color brcosa_pal[256];

void brcosa_buttons_sensitive() // Set 4 brcosa button as sensitive if the user has assigned changes
{
	int i, vals[] = {0, 0, 0, 8, 100};
	gboolean state = FALSE;

	if ( brcosa_buttons[0] == NULL ) return;

	for ( i=0; i<BRCOSA_ITEMS; i++ ) if ( brcosa_values[i] != vals[i] ) state = TRUE;
	for ( i=2; i<5; i++ ) gtk_widget_set_sensitive( brcosa_buttons[i], state );
}

void brcosa_update_sliders()
{
	int i;

	for ( i=0; i<BRCOSA_ITEMS; i++ )
		gtk_adjustment_set_value( GTK_HSCALE(brcosa_scales[i])->scale.range.adjustment,
			brcosa_values[i] );
}

void brcosa_update_spins()
{
	int i;

	for ( i=0; i<BRCOSA_ITEMS; i++ )
	{
		gtk_spin_button_update( GTK_SPIN_BUTTON(brcosa_spins[i]) );
		gtk_spin_button_set_value( GTK_SPIN_BUTTON(brcosa_spins[i]), brcosa_values[i] );
	}
}

gint brcosa_spin_moved( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i;

	for ( i=0; i<BRCOSA_ITEMS; i++ )
		brcosa_values[i] = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(brcosa_spins[i]) );

	brcosa_buttons_sensitive();

	brcosa_update_sliders();

	return FALSE;
}

gint click_brcosa_preview( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i;
	gboolean do_pal = FALSE;	// RGB palette processing

	if (mem_img_bpp == 3)
	{
		do_pal = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(brcosa_toggles[4]) );
		if ( !do_pal && widget == brcosa_toggles[4] )
		{
			mem_pal_copy( mem_pal, brcosa_pal );	// Get back normal palette as ...
			pal_refresher();			// ... user has just cleared toggle
		}
	}

	for ( i=0; i<BRCOSA_ITEMS; i++ )
	{
		mem_prev_bcsp[i] = brcosa_values[i];
		if ( i<3 ) mem_brcosa_allow[i] =
			gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(brcosa_toggles[i+1]) );
	}

	if ( mem_img_bpp == 1 || do_pal )
	{
		mem_brcosa_pal( mem_pal, brcosa_pal );
		for ( i=0; i<mem_cols; i++ )
		{
			if ( mem_brcosa_allow[0] )
				mem_pal[i].red = do_posterize( mem_pal[i].red, mem_prev_bcsp[3] );
			if ( mem_brcosa_allow[1] )
				mem_pal[i].green = do_posterize( mem_pal[i].green, mem_prev_bcsp[3] );
			if ( mem_brcosa_allow[2] )
				mem_pal[i].blue = do_posterize( mem_pal[i].blue, mem_prev_bcsp[3] );
		}
		pal_refresher();
	}
	if ( mem_img_bpp == 3 )
	{
		gtk_widget_queue_draw_area( drawing_canvas, margin_main_x, margin_main_y,
			mem_width*can_zoom + 1, mem_height*can_zoom + 1);
	}

	return FALSE;
}


gint click_brcosa_preview_toggle( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( brcosa_buttons[1] == NULL ) return FALSE;		// Traps call during initialisation

	if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(brcosa_toggles[0]) ) )
	{
		click_brcosa_preview( widget, NULL, NULL );
		gtk_widget_hide(brcosa_buttons[1]);
	}
	else 	gtk_widget_show(brcosa_buttons[1]);

	return FALSE;
}

gint click_brcosa_RGB_toggle( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(brcosa_toggles[5]) ) )
		mem_preview = 1;
	else	mem_preview = 0;

	click_brcosa_preview( widget, NULL, NULL );

	return FALSE;
}

gint brcosa_slider_moved( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i;

	for ( i=0; i<BRCOSA_ITEMS; i++ )
		brcosa_values[i] = GTK_HSCALE(brcosa_scales[i])->scale.range.adjustment->value;

	brcosa_buttons_sensitive();

	brcosa_update_spins();

	if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(brcosa_toggles[0]) ) )
		click_brcosa_preview( NULL, NULL, NULL );

	return FALSE;
}

gint delete_brcosa( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	inifile_set_gboolean( "autopreviewToggle",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(brcosa_toggles[0]) ) );
	gtk_widget_destroy(brcosa_window);

	mem_preview = 0;		// If in RGB mode this is required to disable live preview

	return FALSE;
}

gint click_brcosa_cancel( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	mem_pal_copy( mem_pal, brcosa_pal );
	pal_refresher();
	delete_brcosa( NULL, NULL, NULL );

	return FALSE;
}

gint click_brcosa_apply( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	mem_pal_copy( mem_pal, brcosa_pal );

	if ( brcosa_values[0] != 0 || brcosa_values[1] != 0 ||
		brcosa_values[2] != 0 || brcosa_values[3] != 8 || brcosa_values[4] != 100 )
	{
		spot_undo(UNDO_COL);

		click_brcosa_preview( NULL, NULL, NULL );
		update_all_views();
		if ( mem_img_bpp == 3 && mem_preview == 1 )	// Only do if toggle set
		{
			if ( brcosa_values[4] != 100 )
				mem_gamma_chunk(mem_img[CHN_IMAGE], mem_width * mem_height);
			if ( brcosa_values[0] != 0 || brcosa_values[1] != 0 || brcosa_values[2] != 0 )
				mem_brcosa_chunk(mem_img[CHN_IMAGE], mem_width * mem_height);
			if ( brcosa_values[3] != 8 )
				mem_posterize_chunk(mem_img[CHN_IMAGE], mem_width * mem_height);
		}

		if ( mem_img_bpp == 1 ) mem_pal_copy( brcosa_pal, mem_pal );
		else
		{
			if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(brcosa_toggles[4]))
				&& (widget != NULL)	// Don't do this when clicking OK
				)
			{
				mem_pal_copy( brcosa_pal, mem_pal );
				click_brcosa_preview(NULL, NULL, NULL);
			}
		}	// Update palette values in RGB/indexed mode as required
	}

	return FALSE;
}

gint click_brcosa_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	click_brcosa_apply( NULL, NULL, NULL );
	delete_brcosa( NULL, NULL, NULL );

	return FALSE;
}

gint click_brcosa_reset( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i;

	mem_pal_copy( mem_pal, brcosa_pal );

	for ( i=0; i<BRCOSA_ITEMS; i++ )
	{
		if (i<3) brcosa_values[i] = 0;
		if (i==3) brcosa_values[i] = 8;
		if (i==4) brcosa_values[i] = 100;
		gtk_adjustment_set_value( GTK_HSCALE(brcosa_scales[i])->scale.range.adjustment,
			brcosa_values[i] );
		gtk_spin_button_set_value( GTK_SPIN_BUTTON(brcosa_spins[i]), brcosa_values[i] );
		gtk_spin_button_update( GTK_SPIN_BUTTON(brcosa_spins[i]) );
	}
	pal_refresher();

	return FALSE;
}

void pressed_brcosa( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *vbox2, *table2;
	GtkWidget *hbox;
	GtkWidget *button;

	int	mins[] = {-255, -100, -100, 1, 20},
		maxs[] = {255, 100, 100, 8, 500},
		vals[] = {0, 0, 0, 8, 100}, i, j;
	char	*tog_txt[] = {	_("Auto-Preview"), _("Red"), _("Green"), _("Blue"), _("Palette"),
				_("Image") },
		*tab_txt[] = {	_("Brightness"), _("Contrast"), _("Saturation"), _("Posterize"),
				_("Gamma") };

	GtkAccelGroup* ag = gtk_accel_group_new();

	mem_pal_copy( brcosa_pal, mem_pal );		// Remember original palette

	for ( i=0; i<BRCOSA_ITEMS; i++ ) mem_prev_bcsp[i] = vals[i];

	for ( i=0; i<4; i++ ) brcosa_buttons[i] = NULL;
			// Enables preview_toggle code to detect an initialisation call

	mem_preview = 1;		// If in RGB mode this is required to enable live preview

	brcosa_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Transform Colour"),
		GTK_WIN_POS_MOUSE, TRUE );

	vbox2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox2);
	gtk_container_add (GTK_CONTAINER (brcosa_window), vbox2);

	table2 = add_a_table( 4, 3, 10, vbox2 );

	for ( i=0; i<BRCOSA_ITEMS; i++ )
		add_to_table( tab_txt[i], table2, (i+1)%5, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5);

	for ( i=0; i<BRCOSA_ITEMS; i++ )
	{
		brcosa_values[i] = vals[i];
		brcosa_scales[i] = add_slider2table( vals[i], mins[i], maxs[i], table2, (i+1)%5, 1, 255, 20 );
		gtk_signal_connect( GTK_OBJECT(GTK_HSCALE(brcosa_scales[i])->scale.range.adjustment),
			"value_changed", GTK_SIGNAL_FUNC(brcosa_slider_moved), NULL);
		spin_to_table( table2, &brcosa_spins[i], (i+1)%5, 2, 1, vals[i], mins[i], maxs[i] );
//		GTK_WIDGET_UNSET_FLAGS (brcosa_spins[i], GTK_CAN_FOCUS);

#if GTK_MAJOR_VERSION == 2
		gtk_signal_connect( GTK_OBJECT( &GTK_SPIN_BUTTON(brcosa_spins[i])->entry ),
			"value_changed", GTK_SIGNAL_FUNC(brcosa_spin_moved), NULL);
#else
		gtk_signal_connect( GTK_OBJECT( &GTK_SPIN_BUTTON(brcosa_spins[i])->entry ),
			"changed", GTK_SIGNAL_FUNC(brcosa_spin_moved), NULL);
#endif
	}

	add_hseparator( vbox2, -2, 10 );

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (vbox2), hbox, FALSE, FALSE, 0);

	if ( mem_img_bpp == 1 ) j=4;
	else j=6;

	for ( i=0; i<j; i++ )
	{
		brcosa_toggles[i] = add_a_toggle( tog_txt[i], hbox, TRUE );
		if ( i == 0 ) gtk_signal_connect(GTK_OBJECT(brcosa_toggles[i]), "clicked",
			GTK_SIGNAL_FUNC(click_brcosa_preview_toggle), NULL);
		if ( i>0 && i<4) gtk_signal_connect(GTK_OBJECT(brcosa_toggles[i]), "clicked",
			GTK_SIGNAL_FUNC(click_brcosa_preview), NULL);
		if ( i>=4 ) gtk_signal_connect(GTK_OBJECT(brcosa_toggles[i]), "clicked",
			GTK_SIGNAL_FUNC(click_brcosa_RGB_toggle), NULL);
	}

	if ( j==6 ) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( brcosa_toggles[4] ), FALSE);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( brcosa_toggles[0] ),
		inifile_get_gboolean("autopreviewToggle", TRUE));

	add_hseparator( vbox2, -2, 10 );

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox);
	gtk_box_pack_start (GTK_BOX (vbox2), hbox, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);

	button = add_a_button(_("Cancel"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_cancel), NULL);
	gtk_signal_connect_object (GTK_OBJECT (brcosa_window), "delete_event",
		GTK_SIGNAL_FUNC (click_brcosa_cancel), NULL);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);
	brcosa_buttons[0] = button;

	button = add_a_button(_("Preview"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_preview), NULL);
	brcosa_buttons[1] = button;

	button = add_a_button(_("Reset"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_reset), NULL);
	brcosa_buttons[2] = button;

	button = add_a_button(_("Apply"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_apply), NULL);
	brcosa_buttons[3] = button;

	button = add_a_button(_("OK"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_ok), NULL);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	brcosa_buttons[4] = button;

	gtk_widget_show (brcosa_window);
	gtk_window_add_accel_group(GTK_WINDOW (brcosa_window), ag);

	click_brcosa_preview_toggle( NULL, NULL, NULL );		// Show/hide preview button
	brcosa_buttons_sensitive();					// Disable buttons
	gtk_window_set_transient_for( GTK_WINDOW(brcosa_window), GTK_WINDOW(main_window) );
}


///	RESIZE/RESCALE WINDOWS

GtkWidget *sisca_window, *sisca_table;
GtkWidget *sisca_spins[6], *sisca_toggles[2];
gboolean sisca_scale;


void sisca_off_lim( int spin, int dim )
{
	int nw = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[spin-2]) );
	int min, max, val;
	gboolean state = TRUE;
	GtkAdjustment *adj;

	if ( sisca_scale ) return;			// Only do this if we are resizing

	adj = gtk_spin_button_get_adjustment( GTK_SPIN_BUTTON(sisca_spins[spin]) );
	val = adj -> value;
	if ( nw == dim )
	{
		state = FALSE;
		min = 0;
		max = 0;
		val = 0;
	}
	else
	{
		if ( nw<dim )			// Size is shrinking
		{
			max = 0;
			min = nw - dim;
		}
		else					// Size is expanding
		{
			max = nw - dim;
			min = 0;
		}
	}
	mtMIN( val, val, max )
	mtMAX( val, val, min )

	adj -> lower = min;
	adj -> upper = max;
	adj -> value = val;

	gtk_adjustment_value_changed( adj );
	gtk_adjustment_changed( adj );
	gtk_widget_set_sensitive( sisca_spins[spin], state );
	gtk_spin_button_update( GTK_SPIN_BUTTON(sisca_spins[spin]) );
}

void sisca_reset_offset_y()
{	sisca_off_lim( 3, mem_height ); }

void sisca_reset_offset_x()
{	sisca_off_lim( 2, mem_width ); }

gint sisca_width_moved( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int nw, nh, oh;

	sisca_reset_offset_x();
	if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sisca_toggles[0])) )
	{
		nw = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[0]) );
		oh = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[1]) );
		nh = mt_round( nw * ((float) mem_height) / ((float) mem_width) );
		mtMIN( nh, nh, MAX_HEIGHT )
		mtMAX( nh, nh, 1 )
		if ( nh != oh )
		{
			gtk_spin_button_update( GTK_SPIN_BUTTON(sisca_spins[1]) );
			gtk_spin_button_set_value( GTK_SPIN_BUTTON(sisca_spins[1]), nh );
			sisca_reset_offset_y();
		}
	}

	return FALSE;
}

gint sisca_height_moved( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int nw, nh, ow;

	sisca_reset_offset_y();
	if ( gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sisca_toggles[0])) )
	{
		ow = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[0]) );
		nh = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[1]) );
		nw = mt_round( nh * ((float) mem_width) / ((float) mem_height) );
		mtMIN( nw, nw, MAX_WIDTH )
		mtMAX( nw, nw, 1 )
		if ( nw != ow )
		{
			gtk_spin_button_update( GTK_SPIN_BUTTON(sisca_spins[0]) );
			gtk_spin_button_set_value( GTK_SPIN_BUTTON(sisca_spins[0]), nw );
			sisca_reset_offset_x();
		}
	}

	return FALSE;
}

static int scale_mode = 7;

static void scale_mode_changed(GtkWidget *widget, gpointer name)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;
	scale_mode = (int) name;
}

gint click_sisca_cancel( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gtk_widget_destroy(sisca_window);
	return FALSE;
}

gint click_sisca_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int nw, nh, ox, oy, res = 1, scale_type = 0;

	gtk_spin_button_update( GTK_SPIN_BUTTON(sisca_spins[0]) );
	gtk_spin_button_update( GTK_SPIN_BUTTON(sisca_spins[1]) );

	nw = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[0]) );
	nh = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[1]) );
	if ( nw != mem_width || nh != mem_height )
	{
		// Needed in Windows to stop GTK+ lowering the main window below window underneath
		gtk_window_set_transient_for( GTK_WINDOW(sisca_window), NULL );

		if ( sisca_scale )
		{
			if ( mem_img_bpp == 3 ) scale_type = scale_mode;
			res = mem_image_scale( nw, nh, scale_type );
		}
		else 
		{
			ox = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[2]) );
			oy = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[3]) );
			res = mem_image_resize( nw, nh, ox, oy );
		}
		if ( res == 0 )
		{
			canvas_undo_chores();
			click_sisca_cancel( NULL, NULL, NULL );
		}
		else	memory_errors(res);
	}
	else alert_box(_("Error"), _("New geometry is the same as now - nothing to do."),
			_("OK"), NULL, NULL);

	return FALSE;
}

void memory_errors(int type)
{
	if ( type == 1 )
		alert_box(_("Error"), _("The operating system cannot allocate the memory for this operation."), _("OK"), NULL, NULL);
	if ( type == 2 )
		alert_box(_("Error"), _("You have not allocated enough memory in the Preferences window for this operation."), _("OK"), NULL, NULL);
}

gint click_sisca_centre( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int nw = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[0]) );
	int nh = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[1]) );

	nw = (nw - mem_width) / 2;
	nh = (nh - mem_height) / 2;

	gtk_spin_button_set_value( GTK_SPIN_BUTTON(sisca_spins[2]), nw );
	gtk_spin_button_set_value( GTK_SPIN_BUTTON(sisca_spins[3]), nh );

	return FALSE;
}

void sisca_init( char *title )
{
	gchar* scale_fnames[] = {
		_("Nearest Neighbour"),
		_("Bilinear / Area Mapping"),
		_("Bicubic"),
		_("Bicubic edged"),
		_("Bicubic better"),
		_("Bicubic sharper"),
		_("Lanczos3"),
		_("Blackman-Harris"),
		NULL
	};

	GtkWidget *button_ok, *button_cancel, *button_centre, *sisca_vbox, *sisca_hbox;
	GtkAccelGroup* ag = gtk_accel_group_new();

	sisca_window = add_a_window( GTK_WINDOW_TOPLEVEL, title, GTK_WIN_POS_CENTER, TRUE );

	sisca_vbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(sisca_vbox);
	gtk_container_add(GTK_CONTAINER (sisca_window), sisca_vbox);

	sisca_table = add_a_table(3, 3, 5, sisca_vbox);

	add_to_table( _("Width     "), sisca_table, 0, 1, 0, GTK_JUSTIFY_LEFT, 0, 0 );
	add_to_table( _("Height    "), sisca_table, 0, 2, 0, GTK_JUSTIFY_LEFT, 0, 0 );

	add_to_table( _("Original      "), sisca_table, 1, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	spin_to_table( sisca_table, &sisca_spins[0], 1, 1, 5, mem_width, mem_width, mem_width );
	spin_to_table( sisca_table, &sisca_spins[1], 1, 2, 5, mem_height, mem_height, mem_height );
	GTK_WIDGET_UNSET_FLAGS (sisca_spins[0], GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS (sisca_spins[1], GTK_CAN_FOCUS);

	add_to_table( _("New"), sisca_table, 2, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
	spin_to_table( sisca_table, &sisca_spins[0], 2, 1, 5, mem_width, 1, MAX_WIDTH );
	spin_to_table( sisca_table, &sisca_spins[1], 2, 2, 5, mem_height, 1, MAX_HEIGHT );

#if GTK_MAJOR_VERSION == 2
	gtk_signal_connect( GTK_OBJECT( &GTK_SPIN_BUTTON(sisca_spins[0])->entry ),
		"value_changed", GTK_SIGNAL_FUNC(sisca_width_moved), NULL);
	gtk_signal_connect( GTK_OBJECT( &GTK_SPIN_BUTTON(sisca_spins[1])->entry ),
		"value_changed", GTK_SIGNAL_FUNC(sisca_height_moved), NULL);
#else
	gtk_signal_connect( GTK_OBJECT( &GTK_SPIN_BUTTON(sisca_spins[0])->entry ),
		"changed", GTK_SIGNAL_FUNC(sisca_width_moved), NULL);
	gtk_signal_connect( GTK_OBJECT( &GTK_SPIN_BUTTON(sisca_spins[1])->entry ),
		"changed", GTK_SIGNAL_FUNC(sisca_height_moved), NULL);
#endif
	// Interesting variation between GTK+1/2 here.  I want to update each of the spinbuttons
	// when either:
	// i)  Up/down button clicked
	// ii) Manual changed followed by a tab keypress
	// MT 19-10-2004

	if ( !sisca_scale )
	{
		add_to_table( _("Offset"), sisca_table, 3, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5);
		spin_to_table( sisca_table, &sisca_spins[2], 3, 1, 5, 0, 0, 0 );
		spin_to_table( sisca_table, &sisca_spins[3], 3, 2, 5, 0, 0, 0 );

		button_centre = gtk_button_new_with_label(_("Centre"));

		gtk_widget_show(button_centre);
		gtk_table_attach (GTK_TABLE (sisca_table), button_centre, 0, 1, 4, 5,
			(GtkAttachOptions) (GTK_FILL),
			(GtkAttachOptions) (0), 5, 5);
		gtk_signal_connect(GTK_OBJECT(button_centre), "clicked",
			GTK_SIGNAL_FUNC(click_sisca_centre), NULL);
	}
	add_hseparator( sisca_vbox, -2, 10 );

	sisca_toggles[0] = add_a_toggle( _("Fix Aspect Ratio"), sisca_vbox, TRUE );
	gtk_signal_connect(GTK_OBJECT(sisca_toggles[0]), "clicked",
		GTK_SIGNAL_FUNC(sisca_width_moved), NULL);
	if ( mem_img_bpp == 3 && sisca_scale )
	{
		GtkWidget *btn = NULL;
		int i;

		add_hseparator( sisca_vbox, -2, 10 );
		for (i = 0; scale_fnames[i]; i++)
		{
			btn = add_radio_button(scale_fnames[i], NULL, btn,
				sisca_vbox, i + 1);
			if (scale_mode == i)
				gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(btn), TRUE);
			gtk_signal_connect(GTK_OBJECT(btn), "toggled",
				GTK_SIGNAL_FUNC(scale_mode_changed),
				(gpointer)(i));
		}
		if (scale_mode >= i) scale_mode = 0;
	}

	add_hseparator( sisca_vbox, -2, 10 );

	sisca_hbox = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (sisca_hbox);
	gtk_box_pack_start (GTK_BOX (sisca_vbox), sisca_hbox, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (sisca_hbox), 5);

	button_cancel = add_a_button( _("Cancel"), 4, sisca_hbox, TRUE );
	gtk_signal_connect(GTK_OBJECT(button_cancel), "clicked",
		GTK_SIGNAL_FUNC(click_sisca_cancel), NULL);
	gtk_signal_connect_object (GTK_OBJECT (sisca_window), "delete_event",
		GTK_SIGNAL_FUNC (click_sisca_cancel), NULL);
	gtk_widget_add_accelerator (button_cancel, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button_ok = add_a_button( _("OK"), 4, sisca_hbox, TRUE );
	gtk_signal_connect(GTK_OBJECT(button_ok), "clicked",
		GTK_SIGNAL_FUNC(click_sisca_ok), NULL);
	gtk_widget_add_accelerator (button_ok, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button_ok, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);

	gtk_window_set_transient_for( GTK_WINDOW(sisca_window), GTK_WINDOW(main_window) );
	gtk_widget_show (sisca_window);
	gtk_window_add_accel_group(GTK_WINDOW (sisca_window), ag);

	sisca_reset_offset_x();
	sisca_reset_offset_y();
}

void pressed_scale( GtkMenuItem *menu_item, gpointer user_data )
{
	sisca_scale = TRUE;
	sisca_init(_("Scale Canvas"));
}

void pressed_size( GtkMenuItem *menu_item, gpointer user_data )
{
	sisca_scale = FALSE;
	sisca_init(_("Resize Canvas"));
}


///	EDIT ALL COLOURS WINDOW

static GtkWidget *allcol_window;
static GdkColor *ctable;
static int col_sel_type, col_sel_opac[3];


static gint delete_allcol( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gtk_widget_destroy(allcol_window);
	free( ctable );

	return FALSE;
}

static void do_allcol()
{
	int i;

	for ( i=0; i<mem_cols; i++ )
	{
		mem_pal[i].red = mt_round( ctable[i].red / 257.0 );
		mem_pal[i].green = mt_round( ctable[i].green / 257.0 );
		mem_pal[i].blue = mt_round( ctable[i].blue / 257.0 );
	}

	init_pal();
	update_all_views();
	gtk_widget_queue_draw( drawing_col_prev );
}

static gint allcol_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( col_sel_type == COLSEL_EDIT_ALL )
	{
		mem_pal_copy( mem_pal, brcosa_pal );
		pal_refresher();
		spot_undo(UNDO_PAL);
		do_allcol();
	}

	delete_allcol( NULL, NULL, NULL );

	return FALSE;
}

static gint allcol_preview( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( col_sel_type == COLSEL_EDIT_ALL ) do_allcol();

	return FALSE;
}

static gint allcol_cancel( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( col_sel_type == COLSEL_EDIT_ALL )
	{
		mem_pal_copy( mem_pal, brcosa_pal );
		pal_refresher();
	}

	delete_allcol( NULL, NULL, NULL );

	return FALSE;
}

static gboolean color_expose( GtkWidget *widget, GdkEventExpose *event, gpointer user_data )
{
	GdkColor *c = (GdkColor *)user_data;
	unsigned char r = c->red/257, g = c->green/257, b = c->blue/257, *rgb = NULL;
	int x = event->area.x, y = event->area.y, w = event->area.width, h = event->area.height;
	int i, j=w*h;

	rgb = malloc( 3*j );
	if ( rgb != NULL )
	{
		for ( i=0; i<j; i++ )
		{
			rgb[3*i] = r;
			rgb[3*i+1] = g;
			rgb[3*i+2] = b;
		}
		gdk_draw_rgb_image( widget->window, widget->style->black_gc, x, y, w, h, GDK_RGB_DITHER_NONE, rgb, w*3 );
		free(rgb);
	}

	return FALSE;
}


static void color_set( GtkColorSelection *selection, gpointer user_data )
{
	gdouble color[4];
	GtkWidget *widget;
	GdkColor *c;
	int i, cur_opac, sel_col = -1;

	gtk_color_selection_get_color( selection, color );
	widget = GTK_WIDGET(gtk_object_get_user_data(GTK_OBJECT(selection)));
	c = gtk_object_get_user_data(GTK_OBJECT(widget));
	c->red = mt_round( color[0]*65535.0 );
	c->green = mt_round( color[1]*65535.0 );
	c->blue = mt_round( color[2]*65535.0 );
	gdk_colormap_alloc_color( gdk_colormap_get_system(), c, FALSE, TRUE );
	gtk_widget_queue_draw(widget);

	if ( col_sel_type == COLSEL_OVERLAYS )
	{
		for ( i=0; i<3; i++ ) if ( c == &ctable[i] ) sel_col = i;
			// Get index of overlay selected

#if GTK_MAJOR_VERSION == 1
		cur_opac = mt_round(color[3]*65535);
#endif
#if GTK_MAJOR_VERSION == 2
		cur_opac = gtk_color_selection_get_current_alpha( selection );
#endif

		col_sel_opac[ sel_col ] = cur_opac;		// Save it for later use

	}
}

static void color_select( GtkList *list, GtkWidget *widget, gpointer user_data )
{
	GtkColorSelection *cs = GTK_COLOR_SELECTION(user_data);
	GdkColor *c = (GdkColor *)gtk_object_get_user_data(GTK_OBJECT(widget));
	gdouble color[4];
	int i, sel_col=-1;

	gtk_object_set_user_data( GTK_OBJECT(cs), widget );
	color[0] = ((gdouble)(c->red))/65535.0;
	color[1] = ((gdouble)(c->green))/65535.0;
	color[2] = ((gdouble)(c->blue))/65535.0;
	color[3] = 1.0;

	gtk_signal_disconnect_by_func(GTK_OBJECT(cs), GTK_SIGNAL_FUNC(color_set), NULL);

	if ( col_sel_type == COLSEL_OVERLAYS )
	{
		for ( i=0; i<3; i++ ) if ( c == &ctable[i] ) sel_col = i;
			// Get index of overlay selected
		color[3] = ((gdouble) col_sel_opac[sel_col]) / 65535;
	}

	gtk_color_selection_set_color( cs, color );
#if GTK_MAJOR_VERSION == 1
	gtk_color_selection_set_color( cs, color);
	if ( col_sel_type == COLSEL_OVERLAYS )
	{
		gtk_color_selection_set_opacity( cs, col_sel_opac[sel_col] );
	}
#endif

#if GTK_MAJOR_VERSION == 2
	gtk_color_selection_set_previous_color( cs, c );
	if ( col_sel_type == COLSEL_OVERLAYS )
	{
		gtk_color_selection_set_current_alpha( cs, col_sel_opac[sel_col] );
		gtk_color_selection_set_previous_alpha( cs, col_sel_opac[sel_col] );
	}
#endif

	gtk_signal_connect( GTK_OBJECT(cs), "color_changed", GTK_SIGNAL_FUNC(color_set), NULL );
}

void pressed_allcol( GtkMenuItem *menu_item, gpointer user_data )
{
	colour_selector( COLSEL_EDIT_ALL );
}


void colour_selector( int cs_type )			// Bring up GTK+ colour wheel
{
	GtkWidget *vbox, *hbox, *hbut, *button_ok, *button_preview, *button_cancel;
	GtkWidget *col_list, *l_item, *hbox2, *label, *drw, *swindow, *viewport;
	GtkWidget *cs;
	png_color ovl[3];	// Overlay colours
	char txt[64], *ovl_txt[] = { _("Alpha"), _("Selection"), _("Mask") };
	int i, j=0;

	GtkAccelGroup* ag = gtk_accel_group_new();


	ovl[0].red = 0;		// Just for testing - real values should come from back end
	ovl[0].green = 0;
	ovl[0].blue = 255;
	ovl[1].red = 255;
	ovl[1].green = 255;
	ovl[1].blue = 0;
	ovl[2].red = 255;
	ovl[2].green = 0;
	ovl[2].blue = 0;
	col_sel_opac[0] = 128 * 257;
	col_sel_opac[1] = 128 * 257;
	col_sel_opac[2] = 128 * 257;

	col_sel_type = cs_type;

	mem_pal_copy( brcosa_pal, mem_pal );			// Remember old settings

	if ( col_sel_type == COLSEL_EDIT_ALL )
	{
		ctable = malloc( mem_cols*sizeof(GdkColor) );
		for ( i=0; i<mem_cols; i++ )
		{
			ctable[i].red   = mem_pal[i].red*257;
			ctable[i].green = mem_pal[i].green*257;
			ctable[i].blue  = mem_pal[i].blue*257;
			ctable[i].pixel =	mem_pal[i].blue +
						(mem_pal[i].green << 8) +
						(mem_pal[i].red << 16);
		}
	}

	if ( col_sel_type == COLSEL_OVERLAYS )
	{
		ctable = malloc( 3*sizeof(GdkColor) );
		for ( i=0; i<3; i++ )
		{
			ctable[i].red   = ovl[i].red*257;
			ctable[i].green = ovl[i].green*257;
			ctable[i].blue  = ovl[i].blue*257;
			ctable[i].pixel = ovl[i].blue +
						(ovl[i].green << 8) +
						(ovl[i].red << 16);
		}
	}

	cs = gtk_color_selection_new();

#if GTK_MAJOR_VERSION == 2
	if ( col_sel_type == COLSEL_EDIT_ALL )
		gtk_color_selection_set_has_opacity_control (GTK_COLOR_SELECTION(cs), FALSE);
	else
		gtk_color_selection_set_has_opacity_control (GTK_COLOR_SELECTION(cs), TRUE);

	gtk_color_selection_set_has_palette (GTK_COLOR_SELECTION(cs), TRUE);
#endif
	gtk_signal_connect( GTK_OBJECT(cs), "color_changed", GTK_SIGNAL_FUNC(color_set), NULL );

	if ( col_sel_type == COLSEL_EDIT_ALL )
	{
		allcol_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Edit All Colours"),
			GTK_WIN_POS_MOUSE, TRUE );
	}

	if ( col_sel_type == COLSEL_OVERLAYS )
	{
		allcol_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Configure Overlays"),
			GTK_WIN_POS_CENTER, TRUE );
	}


	gtk_signal_connect_object( GTK_OBJECT(allcol_window),"delete_event",
		GTK_SIGNAL_FUNC(allcol_cancel), NULL );

	vbox = gtk_vbox_new( FALSE, 5 );
	gtk_widget_show( vbox );
	gtk_container_set_border_width( GTK_CONTAINER(vbox), 5 );
	gtk_container_add( GTK_CONTAINER(allcol_window), vbox );

	hbox = gtk_hbox_new( FALSE, 10 );
	gtk_widget_show( hbox );
	gtk_box_pack_start( GTK_BOX(vbox), hbox, TRUE, TRUE, 0);

	swindow = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_show (swindow);
	gtk_box_pack_start (GTK_BOX (hbox), swindow, FALSE, TRUE, 0);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swindow),
		GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

	viewport = gtk_viewport_new (NULL, NULL);
	gtk_widget_show(viewport);
	gtk_container_add (GTK_CONTAINER (swindow), viewport);

	col_list = gtk_list_new();
	gtk_signal_connect( GTK_OBJECT(col_list), "select_child", GTK_SIGNAL_FUNC(color_select), cs );
	gtk_list_set_selection_mode( GTK_LIST(col_list), GTK_SELECTION_BROWSE );
	gtk_container_add ( GTK_CONTAINER(viewport), col_list );
	gtk_widget_show( col_list );

	if ( col_sel_type == COLSEL_EDIT_ALL ) j = mem_cols;
	if ( col_sel_type == COLSEL_OVERLAYS ) j = 3;

	for ( i=0; i<j; i++ )
	{
		l_item = gtk_list_item_new();
		gtk_object_set_user_data( GTK_OBJECT(l_item), (gpointer)(&ctable[i]));
		gtk_container_add( GTK_CONTAINER(col_list), l_item );
		gtk_widget_show( l_item );

		hbox2 = gtk_hbox_new( FALSE, 3 );
		gtk_widget_show( hbox2 );
		gtk_container_set_border_width( GTK_CONTAINER(hbox2), 3 );
		gtk_container_add( GTK_CONTAINER(l_item), hbox2 );

		drw = gtk_drawing_area_new();
		gtk_drawing_area_size( GTK_DRAWING_AREA(drw), 20, 20 );
		gtk_signal_connect(GTK_OBJECT(drw),"expose_event", GTK_SIGNAL_FUNC(color_expose),
			(gpointer)(&ctable[i]));
		gtk_box_pack_start( GTK_BOX(hbox2), drw, FALSE, FALSE, 0 );
		gtk_widget_show( drw );

		if ( col_sel_type == COLSEL_EDIT_ALL ) sprintf( txt, "%i", i );
		if ( col_sel_type == COLSEL_OVERLAYS ) sprintf( txt, "%s", ovl_txt[i] );
		label = gtk_label_new( txt );
		gtk_widget_show( label );
		gtk_misc_set_alignment( GTK_MISC(label), 0.0, 1.0 );
		gtk_box_pack_start( GTK_BOX(hbox2), label, TRUE, TRUE, 0 );
	}

	gtk_box_pack_start( GTK_BOX(hbox), cs, TRUE, TRUE, 0 );

	hbut = gtk_hbox_new(FALSE, 3);
	gtk_widget_show( hbut );
	gtk_box_pack_start( GTK_BOX(vbox), hbut, FALSE, FALSE, 0 );

	button_ok = gtk_button_new_with_label(_("OK"));
	gtk_widget_show( button_ok );
	gtk_widget_add_accelerator (button_ok, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button_ok, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_signal_connect_object( GTK_OBJECT(button_ok), "clicked",
		GTK_SIGNAL_FUNC(allcol_ok), NULL );
	gtk_box_pack_end( GTK_BOX(hbut), button_ok, FALSE, FALSE, 5 );
	gtk_widget_set_usize(button_ok, 80, -2);

	button_preview = gtk_button_new_with_label(_("Preview"));
	gtk_widget_show( button_preview );
	gtk_signal_connect( GTK_OBJECT(button_preview), "clicked",
		GTK_SIGNAL_FUNC(allcol_preview), NULL );
	gtk_box_pack_end( GTK_BOX(hbut), button_preview, FALSE, FALSE, 5 );
	gtk_widget_set_usize(button_preview, 80, -2);

	button_cancel = gtk_button_new_with_label(_("Cancel"));
	gtk_widget_show( button_cancel );
	gtk_widget_add_accelerator (button_cancel, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);
	gtk_signal_connect_object(GTK_OBJECT(button_cancel),"clicked",
		GTK_SIGNAL_FUNC(allcol_cancel), NULL);
	gtk_box_pack_end( GTK_BOX(hbut), button_cancel, FALSE, FALSE, 5 );
	gtk_widget_set_usize(button_cancel, 80, -2);

	gtk_widget_show( cs );
	gtk_window_set_transient_for( GTK_WINDOW(allcol_window), GTK_WINDOW(main_window) );
	gtk_widget_show( allcol_window );
	gtk_window_add_accel_group( GTK_WINDOW(allcol_window), ag );

#if GTK_MAJOR_VERSION == 1
	while (gtk_events_pending()) gtk_main_iteration();
	gtk_list_select_item( GTK_LIST(col_list), 0 );
		// grubby hack needed to start with proper opacity in GTK+1
#endif
}



///	QUANTIZE WINDOW

GtkWidget *quantize_window, *quantize_spin, *quantize_radio[5], *quantize_radio2[4];
int quantize_cols;


gint delete_quantize( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gtk_widget_destroy(quantize_window);

	return FALSE;
}

gint click_quantize_radio( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i;
	gboolean value = FALSE;

	if ( gtk_toggle_button_get_active(
		&(GTK_RADIO_BUTTON( quantize_radio[0] )->check_button.toggle_button) ) )
		value = FALSE;
	else
		value = TRUE;

	if ( !value )
		// If the user wants an exact transfer, don't allow user to choose dither/floyd etc
	{
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(quantize_radio2[0]), TRUE );
	}

	for ( i=1; i<4; i++ ) gtk_widget_set_sensitive(quantize_radio2[i], value);

	return FALSE;
}

gint click_quantize_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	unsigned char *old_image = mem_img[CHN_IMAGE], newpal[3][256];
	int rad1=0, rad2=0, i, k, new_cols, dither;

	for ( i=0; i<5; i++ )
		if ( gtk_toggle_button_get_active(
			&(GTK_RADIO_BUTTON( quantize_radio[i] )->check_button.toggle_button)
				) ) rad1 = i;

	for ( i=0; i<4; i++ )
		if ( gtk_toggle_button_get_active(
			&(GTK_RADIO_BUTTON( quantize_radio2[i] )->check_button.toggle_button)
				) ) rad2 = i;

	gtk_spin_button_update( GTK_SPIN_BUTTON(quantize_spin) );
	new_cols = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(quantize_spin) );
	mtMAX( new_cols, new_cols, 2 )
	mtMIN( new_cols, new_cols, 256 )

	delete_quantize( NULL, NULL, NULL );

	if ( rad1 == 0 )			// Reduce to indexed using exact canvas pixels
	{
		new_cols = quantize_cols;
		i = mem_convert_indexed();
	}
	else
	{
		pen_down = 0;
		i = undo_next_core(2, mem_width, mem_height, 0, 0, 1, CMASK_IMAGE);
		pen_down = 0;
		if ( i == 1 ) i=2;

		if ( i == 0 )
		{
			if ( rad1 == 1 )
				for ( k=0; k<new_cols; k++ )
				{
					newpal[0][k] = mem_pal[k].red;
					newpal[1][k] = mem_pal[k].green;
					newpal[2][k] = mem_pal[k].blue;
				}

			if ( rad1 == 2 ) i = dl1quant(old_image, mem_width, mem_height,
						new_cols, newpal);
			if ( rad1 == 3 ) i = dl3quant(old_image, mem_width, mem_height,
						new_cols, newpal);
			if ( rad1 == 4 ) i = wu_quant(old_image, mem_width, mem_height,
						new_cols, newpal);
			for ( k=0; k<new_cols; k++ )
			{
				mem_pal[k].red = newpal[0][k];
				mem_pal[k].green = newpal[1][k];
				mem_pal[k].blue = newpal[2][k];
			}
			if ( i == 0 )
			{
				dither = rad2 % 2;
				if ( rad2 < 2 ) i = dl3floste(old_image, mem_img[CHN_IMAGE],
					mem_width, mem_height, new_cols, dither, newpal);
						// Floyd-Steinberg
				if ( rad2 > 1 && rad2 < 4 )
					i = mem_quantize( old_image, new_cols, rad2 );
						// Dither/scatter
			}
		}
	}

	if ( i!=0 ) memory_errors(i);
	else
	{
		if ( tool_type == TOOL_SELECT && marq_status >= MARQUEE_PASTE )
			pressed_select_none( NULL, NULL );
		if ( tool_type == TOOL_SMUDGE )
			gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
					// If the user is pasting or smudging, lose it!

		mem_cols = new_cols;
		update_menus();
		init_pal();
		update_all_views();
		gtk_widget_queue_draw( drawing_col_prev );
	}

	return FALSE;
}

void pressed_quantize( GtkMenuItem *menu_item, gpointer user_data )
{
	int i = mem_cols_used(257), j = i;
	char *rad_txt[] = {_("Exact Conversion"), _("Use Current Palette"),
		_("DL1 Quantize (fastest)"), _("DL3 Quantize (very slow, better quality)"),
		_("Wu Quantize (best method for small palettes)")
		};

	char *rad_txt2[] = {_("Flat Colour"), _("Floyd-Steinberg"),
		_("Dithered"), _("Scattered"),
		};

	GSList *group;
	GtkWidget *vbox4, *vbox5, *hbox6, *table3, *frame;
	GtkWidget *button_cancel, *button_ok;
	GtkAccelGroup* ag = gtk_accel_group_new();

	if ( i<2 )
	{
		alert_box( _("Error"), _("You don't have enough unique RGB pixels to reduce to indexed - you must have at least 2."), _("OK"), NULL, NULL );
		return;
	}

	quantize_cols = j;
	if ( j>256 ) j = mem_cols;

	quantize_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Convert To Indexed"),
		GTK_WIN_POS_CENTER, TRUE );

	vbox5 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox5);
	gtk_container_add (GTK_CONTAINER (quantize_window), vbox5);

	table3 = add_a_table( 2, 2, 10, vbox5 );
	add_to_table( _("Indexed Colours To Use"), table3, 1, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5 );
	spin_to_table( table3, &quantize_spin, 1, 1, 5, j, 2, 256 );

///	Palette FRAME

	frame = gtk_frame_new (_("Palette"));
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (vbox5), frame, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	vbox4 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox4);
	gtk_container_add (GTK_CONTAINER (frame), vbox4);

	quantize_radio[0] = add_radio_button( rad_txt[0], NULL,  NULL, vbox4, 0 );
	gtk_signal_connect(GTK_OBJECT(quantize_radio[0]), "clicked",
			GTK_SIGNAL_FUNC(click_quantize_radio), NULL);
	group = gtk_radio_button_group( GTK_RADIO_BUTTON(quantize_radio[0]) );
	quantize_radio[1] = add_radio_button( rad_txt[1], group, NULL, vbox4, 1 );
	gtk_signal_connect(GTK_OBJECT(quantize_radio[1]), "clicked",
			GTK_SIGNAL_FUNC(click_quantize_radio), NULL);

	for ( i=2; i<5; i++ )
	{
		quantize_radio[i] = add_radio_button( rad_txt[i], NULL,  quantize_radio[1], vbox4, i );
		gtk_signal_connect(GTK_OBJECT(quantize_radio[i]), "clicked",
			GTK_SIGNAL_FUNC(click_quantize_radio), NULL);
	}

///	Image FRAME

	frame = gtk_frame_new (_("Image"));
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (vbox5), frame, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	vbox4 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox4);
	gtk_container_add (GTK_CONTAINER (frame), vbox4);

	quantize_radio2[0] = add_radio_button( rad_txt2[0], NULL,  NULL, vbox4, 0 );
	group = gtk_radio_button_group( GTK_RADIO_BUTTON(quantize_radio2[0]) );
	quantize_radio2[1] = add_radio_button( rad_txt2[1], group, NULL, vbox4, 1 );

	for ( i=2; i<4; i++ )
	{
		quantize_radio2[i] = add_radio_button( rad_txt2[i], NULL,  quantize_radio2[1],
						vbox4, i );
	}


	if ( quantize_cols > 256 )
	{
		gtk_widget_hide (quantize_radio[0]);
		gtk_widget_set_sensitive( quantize_radio[0], FALSE );
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(quantize_radio[4]), TRUE );
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(quantize_radio2[1]), TRUE );
	}

	hbox6 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox6);
	gtk_box_pack_start (GTK_BOX (vbox5), hbox6, FALSE, FALSE, 0);

	button_cancel = add_a_button(_("Cancel"), 5, hbox6, TRUE);
	gtk_signal_connect(GTK_OBJECT(button_cancel), "clicked", GTK_SIGNAL_FUNC(delete_quantize), NULL);
	gtk_widget_add_accelerator (button_cancel, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button_ok = add_a_button(_("OK"), 5, hbox6, TRUE);
	gtk_signal_connect(GTK_OBJECT(button_ok), "clicked", GTK_SIGNAL_FUNC(click_quantize_ok), NULL);
	gtk_widget_add_accelerator (button_ok, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button_ok, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);

	gtk_window_set_transient_for( GTK_WINDOW(quantize_window), GTK_WINDOW(main_window) );
	gtk_widget_show (quantize_window);
	gtk_window_add_accel_group(GTK_WINDOW (quantize_window), ag);

	click_quantize_radio( NULL, NULL, NULL );	// Grey out radio buttons if needed
}

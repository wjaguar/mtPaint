/*	viewer.h
	Copyright (C) 2004, 2005 Mark Tyler

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

float vw_zoom;

gboolean view_showing, allow_cline, vw_focus_on;

GtkWidget *cline_window, *vw_drawing;

gint delete_cline( GtkWidget *widget, GdkEvent *event, gpointer data );
//gint delete_view( GtkWidget *widget, GdkEvent *event, gpointer data );

void pressed_cline( GtkMenuItem *menu_item, gpointer user_data );
void pressed_pan( GtkMenuItem *menu_item, gpointer user_data );

void pressed_setzoom( GtkMenuItem *menu_item, gpointer user_data );	// Menu clicked

void pressed_view( GtkMenuItem *menu_item, gpointer user_data );	// Menu clicked
void init_view( GtkWidget *canvas, GtkWidget *scroll );			// Initial setup
void view_show();
void view_hide();

void pressed_help( GtkMenuItem *menu_item, gpointer user_data );
void pressed_text( GtkMenuItem *menu_item, gpointer user_data );
gint render_text( GtkWidget *widget );

void vw_align_size( float new_zoom );				// Set new zoom
void vw_repaint( int px, int py, int pw, int ph );
void vw_update_area( int x, int y, int w, int h );		// Update x,y,w,h area of current image
void vw_focus_view();						// Focus view window to main window
void view_render_rgb( unsigned char *rgb, int px, int py, int pw, int ph, float zoom );

gboolean vw_configure( GtkWidget *widget, GdkEventConfigure *event );
gboolean grab_screen();

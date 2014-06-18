/*	viewer.h
	Copyright (C) 2004-2014 Mark Tyler and Dmitry Groshev

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

int font_aa, font_bk, font_r;
int font_bkg, font_angle;

int view_showing;	// 0: hidden, 1: horizontal split, 2: vertical split
int vw_focus_on;
float vw_zoom;
int opaque_view;
int max_pan;

GtkWidget *vw_drawing;

void create_cline_area( GtkWidget *vbox1 );

void pressed_pan();

void pressed_centralize(int state);
void pressed_view_focus(int state);
void init_view();			// Initial setup
void view_show();
void view_hide();

int make_text_clipboard(unsigned char *img, int w, int h, int src_bpp);
void pressed_help();
void pressed_text();
void render_text();

void vw_align_size( float new_zoom );				// Set new zoom
void vw_realign();						// Reapply old zoom
void vw_update_area( int x, int y, int w, int h );		// Update x,y,w,h area of current image
void vw_focus_view();						// Focus view window to main window
void vw_focus_idle();						// Same but done in idle cycles
void view_render_rgb( unsigned char *rgb, int px, int py, int pw, int ph, double czoom );
void render_layers(unsigned char *rgb, int px, int py, int pw, int ph,
	double czoom, int lr0, int lr1, int align);

gboolean vw_configure( GtkWidget *widget, GdkEventConfigure *event );

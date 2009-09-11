/*	api.c
	Copyright (C) 2007 Mark Tyler

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

#ifdef WIN32
#ifndef WEXITSTATUS
#define WEXITSTATUS(A) ((A) & 0xFF)
#endif
#else
#include <sys/wait.h>
#endif

#include <stdlib.h>

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "inifile.h"
#include "canvas.h"
#include "quantizer.h"
#include "viewer.h"
#include "layer.h"
#include "polygon.h"
#include "wu.h"
#include "prefs.h"
#include "ani.h"
#include "csel.h"
#include "channels.h"
#include "toolbar.h"



void mtpaint_mem_init()			// Initialise memory & inifile
{
	putenv( "G_BROKEN_FILENAMES=1" );	// Needed to read non ASCII filenames in GTK+2
	inifile_init("/.mtpaint");

	string_init();				// Translate static strings
	var_init();				// Load INI variables
	mem_init();				// Set up memory & back end
	layers_init();
	init_cols();
}

void mtpaint_window_init()		// Create main window and setup default image
{
	main_init();
	create_default_image();
	update_menus();
}

void mtpaint_mem_end()			// Finish memory & inifile
{
	inifile_quit();
}

int mtpaint_new_image(int w, int h, int pal_cols, int pal_type, int bpp)
{
	return do_new_one(w, h, pal_cols, pal_type, bpp);
}

void mtpaint_refresh()			// Update canvases, menus, palette, etc.
{
	update_all_views();
	update_menus();
	repaint_top_swatch();
	init_pal();
	gtk_widget_queue_draw(drawing_col_prev);	// Update widget
}


void mtpaint_rgb_a(int r, int g, int b)		// Set paint colour A
{
	mem_col_A24.red = r;
	mem_col_A24.green = g;
	mem_col_A24.blue = b;

	mem_pat_update();
}

void mtpaint_rgb_b(int r, int g, int b)		// Set paint colour B
{
	mem_col_B24.red = r;
	mem_col_B24.green = g;
	mem_col_B24.blue = b;

	mem_pat_update();
}

void mtpaint_col_a(int c)			// Set paint colour A from palette
{
	c = c % 256;

	mem_col_A = c;
	mem_col_A24 = mem_pal[c];

	mem_pat_update();
}

void mtpaint_col_b(int c)			// Set paint colour B from palette
{
	c = c % 256;

	mem_col_B = c;
	mem_col_B24 = mem_pal[c];

	mem_pat_update();
}


void mtpaint_pixel(int x, int y)		// Paint pixel
{
	put_pixel(x, y);
}

void mtpaint_ellipse(int x1, int y1, int x2, int y2, int thick)
					// Paint ellipse: thick=line thickness, 0=filled
{
	mem_ellipse( x1, y1, x2, y2, thick );
}

void mtpaint_rectangle(int x1, int y1, int x2, int y2)		// Paint filled rectangle
{
	int w = x2-x1, h = y2-y1, x = x1, y = y1;

	if ( h<0 )
	{
		y+=h;
		h=-h;
	}
	if ( w<0 )
	{
		x+=w;
		w=-w;
	}

	f_rectangle( x, y, w, h );
}

void mtpaint_line(int x1, int y1, int x2, int y2, int thick)	// Paint line
{
	tline( x1, y1, x2, y2, thick );
}


void mtpaint_paste(int x, int y)			// Paste the clipboard onto the canvas
{
	int ox = marq_x1, oy = marq_y1, ox2 = marq_x2, oy2 = marq_y2;

	marq_x1 = x;
	marq_y1 = y;
	marq_x2 = x + mem_clip_w - 1;
	marq_y2 = y + mem_clip_h - 1;

	commit_paste(FALSE);

	marq_x1 = ox;
	marq_y1 = oy;
	marq_x2 = ox2;
	marq_y2 = oy2;
}

void mtpaint_polygon_new()			// Clear all polygon points
{
	poly_points = 0;
}

void mtpaint_polygon_point(int x, int y)	// Add a new polygon point
{
	if ( x<0 ) x=0;
	if ( y<0 ) y=0;
	if ( x>=mem_width ) x=mem_width-1;
	if ( y>=mem_height ) y=mem_height-1;

	poly_add(x, y);
}

void mtpaint_polygon_fill()			// Fill the polygon
{
	poly_init();
	poly_paint();
}

void mtpaint_polygon_draw()			// Draw the polygon lines
{
	poly_init();
	poly_outline();
}

int mtpaint_polygon_copy()			// Copy polygon to clipboard
{
	return api_copy_polygon();
}

int mtpaint_file_load(char *filename)				// Load a file.  return<0=fail
{
	int type;

	type = detect_image_format(filename);

	if ( type==FT_NONE ) return -1;

	return load_image(filename, FS_PNG_LOAD, type);
}

int mtpaint_file_save(char *filename, int type, int arg)	// Save a file.  return<0=fail
{
	ls_settings settings;

	init_ls_settings(&settings, NULL);
	settings.ftype = type;
	settings.mode = FS_PNG_SAVE;
	settings.jpeg_quality = arg;
	settings.png_compression = arg;
	settings.jp2_rate = arg;
	settings.silent = TRUE;

	memcpy(settings.img, mem_img, sizeof(chanlist));
	settings.pal = mem_pal;
	settings.width = mem_width;
	settings.height = mem_height;
	settings.bpp = mem_img_bpp;
	settings.colors = mem_cols;

	return save_image(filename, &settings);
}

int mtpaint_clipboard_load(char *filename)	// Load clipboard from an image file, return<0=fail
{
	int type;

	type = detect_image_format(filename);

	if ( type==FT_NONE ) return -1;

	return load_image(filename, FS_CLIP_FILE, type);
}

int mtpaint_clipboard_save(char *filename, int type, int arg)	// Save clipboard to an image file, return<0=fail
{
	ls_settings settings;

	if ( mem_clipboard == NULL ) return -1;		// Sanity

	init_ls_settings(&settings, NULL);
	settings.ftype = type;
	settings.mode = FS_CLIP_FILE;
	settings.jpeg_quality = arg;
	settings.png_compression = arg;
	settings.jp2_rate = arg;
	settings.silent = TRUE;

	settings.img[CHN_IMAGE] = mem_clipboard;
	settings.img[CHN_ALPHA] = mem_clip_alpha;
	settings.img[CHN_SEL] = mem_clip_mask;
	settings.pal = mem_pal;
	settings.width = mem_clip_w;
	settings.height = mem_clip_h;
	settings.bpp = mem_clip_bpp;
	settings.colors = mem_cols;

	return save_image(filename, &settings);
}


void mtpaint_text(char *text, char *font, float angle, int antialias)
							// Render text to the clipboard
{
	gboolean rot = FALSE;
	int rang = angle*100.0;

	inifile_set( "lastTextFont", font );
	inifile_set( "textString", text );
	inifile_set_gint32("fontAngle", rang);
	if ( rang!=0 ) rot=TRUE;
	inifile_set_gboolean( "fontAntialias2", rang );
	inifile_set_gboolean( "fontAntialias", antialias );
	render_text( drawing_canvas );
#if GTK_MAJOR_VERSION == 1
	if (angle!=0) mem_rotate_free(angle, antialias, TRUE, TRUE);
#endif
}


void mtpaint_selection(int x1, int y1, int x2, int y2)	// Set the rectangle selection
{
	marq_x1 = x1;
	marq_y1 = y1;
	marq_x2 = x2;
	marq_y2 = y2;
}

int mtpaint_selection_copy()		// Copy the rectangle selection area to the clipboard
{
	return api_copy_rectangle();
}

int mtpaint_clipboard_rotate(float angle, int smooth, int gamma_correction)
{
	return mem_rotate_free(angle, smooth, gamma_correction, TRUE);
}

void mtpaint_clipboard_alpha2mask()		// Move alpha to clipboard mask
{
	api_clip_alphamask();
}

int mtpaint_animated_gif(char *input, char *output, int delay)		// Create animated GIF
{
	int res;
	char txt[500];

	snprintf(txt, 500, "%s -d %i %s -o \"%s\"", GIFSICLE_CREATE, delay, input, output);

	res = system(txt);

	if ( res != 0 )		// Error occured
	{
		if ( res>0 ) res = WEXITSTATUS(res);
	}

	return res;
}

int mtpaint_screenshot()			// Grab a screenshot
{
	return grab_screen();
}

void mtpaint_opacity(int c)			// Set paint opacity 0-255
{
	tool_opacity = c;
}

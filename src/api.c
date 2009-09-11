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

#define API_SOURCES

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
#include "viewer.h"
#include "layer.h"
#include "polygon.h"
#include "wu.h"
#include "prefs.h"
#include "ani.h"
#include "csel.h"
#include "channels.h"
#include "toolbar.h"
#include "api.h"
#include "font.h"


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

void mtpaint_init()			// Initialise memory & inifile
{
	mtpaint_mem_init();
}

void mtpaint_quit()			// Finish memory & inifile
{
	spawn_quit();
	inifile_quit();
}

int mtpaint_new_image(int w, int h, int pal_cols, int pal_type, int bpp)
{
	return do_new_one(w, h, pal_cols, pal_type == 1 ? NULL : mem_pal_def,
		bpp, FALSE);
}

void mtpaint_refresh()			// Update canvases, menus, palette, etc.
{
	update_menus();
	init_pal();
	update_all_views();
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

	memcpy(settings.img, mem_clip.img, sizeof(chanlist));
	settings.pal = mem_pal;
	settings.width = mem_clip_w;
	settings.height = mem_clip_h;
	settings.bpp = mem_clip_bpp;
	settings.colors = mem_cols;

	return save_image(filename, &settings);
}


// FIXME - this function could be put into memory.c to be used by GTK+ text renderer
#ifdef U_FREETYPE
static int mem_text_clip_prep(int w, int h)		// Prepare new clipboard for text rendering
{
	unsigned char *dest, *pat_off;
	int i, j, clip_bpp;

	if ( w<1 || h<1 ) return 1;

	if (mem_channel == CHN_IMAGE) clip_bpp = mem_img_bpp;
	else clip_bpp = 1;
	mem_clip_new(w, h, clip_bpp, CMASK_IMAGE, FALSE);

	if (!mem_clipboard) return 1;

	if (mem_channel == CHN_IMAGE)		// Pasting to image so use the pattern
	{
		for ( j=0; j<h; j++ )
		{
			dest = mem_clipboard + mem_clip_w*j*mem_clip_bpp;
			if ( mem_clip_bpp == 1 )
			{
				pat_off = mem_col_pat + (j%8)*8;
				for ( i=0; i<w; i++ )
				{
					dest[i] = pat_off[i%8];
				}
			}
			if ( mem_clip_bpp == 3 )
			{
				pat_off = mem_col_pat24 + (j%8)*8*3;
				for ( i=0; i<w; i++ )
				{
					dest[3*i]   = pat_off[3*(i%8)];
					dest[3*i+1] = pat_off[3*(i%8)+1];
					dest[3*i+2] = pat_off[3*(i%8)+2];
				}
			}
		}
	}

	return 0;
}
#endif

int mtpaint_text(				// Render text to the clipboard
		char	*text,		// Text to render
		int	characters,	// Characters to render (may be less if 0 is encountered first)
		char	*filename,	// Font file
		char	*encoding,	// Encoding of text, e.g. ASCII, UTF-8, UNICODE
		double	size,		// Scalable font size
		int	face_index,	// Usually 0, but maybe higher for bitmap fonts like *.FON
		double	angle,		// Rotation, anticlockwise
		int	flags		// MT_TEXT_* flags
		)
{
	unsigned char *new;
	int w=0, h=0;

	new = mt_text_render( text, characters, filename, encoding, size,
			face_index, angle, flags, &w, &h );

	if (new)
	{
		if ( !mem_text_clip_prep(w, h) )
		{
			mem_clip_mask = new;
		}
		else
		{
			free(new);	// Couldn't set up clipboard so free memory and bail out
			return -1;
		}
	}
	else return -1;

	return 0;
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

int mtpaint_clipboard_rotate(float angle, int smooth, int gamma_correction, int destructive)
{
	return mem_rotate_free(angle, smooth, gamma_correction,
		destructive ? 2 : 1);
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
	return (load_image(NULL, FS_PNG_LOAD, FT_PIXMAP) == 1);
}

void mtpaint_opacity(int c)			// Set paint opacity 0-255
{
	tool_opacity = c;
}

static int api_coltrans(unsigned char *rgb, int bpp, int w, int h, int brightness, int contrast, int saturation, int posterize, int gamma, int hue, int red, int green, int blue, int destructive)
{
	int i;
	unsigned char *mask, *rgb_dest = rgb;

	mem_prev_bcsp[0] = brightness;
	mem_prev_bcsp[1] = contrast;
	mem_prev_bcsp[2] = saturation;
	mem_prev_bcsp[3] = posterize;
	mem_prev_bcsp[4] = gamma;
	mem_prev_bcsp[5] = hue;

	mem_brcosa_allow[0] = red;
	mem_brcosa_allow[1] = green;
	mem_brcosa_allow[2] = blue;

	if ( !destructive && rgb == mem_clipboard )	// Do we want to keep the clipboard?
	{
// !!! FIXME: this is broken in 3.14.50+ - WJ
		if ( mem_clip_real_w )		// Image already in reserve, so use it
		{
			free(mem_clipboard);	// Dispose of current clipboard if it exists
			free(mem_clip_mask);
			free(mem_clip_alpha);
			mem_clipboard = NULL;
			mem_clip_mask = NULL;
			mem_clip_alpha = NULL;
			w = mem_clip_real_w;
			h = mem_clip_real_h;
		}
		else	// No clipboard in reserve, so sweep current clipboard into reserve
		{
			mem_clip_real_img[CHN_IMAGE] = mem_clipboard;
			mem_clip_real_img[CHN_ALPHA] = mem_clip_alpha;
			mem_clip_real_img[CHN_SEL] = mem_clip_mask;
			mem_clip_real_w = mem_clip_w;
			mem_clip_real_h = mem_clip_h;
			mem_clipboard = NULL;
			mem_clip_mask = NULL;
			mem_clip_alpha = NULL;
		}

		mem_clipboard = malloc(w*h*bpp);		// Create new clipboard for transform
		if (mem_clip_real_img[CHN_ALPHA])
		{
			mem_clip_alpha = malloc(w*h);
			if (mem_clip_alpha) memcpy(mem_clip_alpha, mem_clip_real_img[CHN_ALPHA], w*h);
		}
		if (mem_clip_real_img[CHN_SEL])
		{
			mem_clip_mask = malloc(w*h);
			if (mem_clip_mask) memcpy(mem_clip_mask, mem_clip_real_img[CHN_SEL], w*h);
		}
		if (!mem_clipboard || (mem_clip_real_img[CHN_ALPHA] && !mem_clip_alpha)
			|| (mem_clip_real_img[CHN_SEL] && !mem_clip_mask))
		{		// No memory so put reserve clipboard back into main clipboard
			free(mem_clipboard);
			free(mem_clip_alpha);
			free(mem_clip_mask);
			mem_clipboard = mem_clip_real_img[CHN_IMAGE];
			mem_clip_alpha = mem_clip_real_img[CHN_ALPHA];
			mem_clip_mask = mem_clip_real_img[CHN_SEL];
			mem_clip_w = mem_clip_real_w;
			mem_clip_h = mem_clip_real_h;
			mem_clip_real_w = 0;
			return -5;
		}
		rgb = mem_clip_real_img[CHN_IMAGE];
		rgb_dest = mem_clipboard;
		memcpy(rgb_dest, rgb, w*h*bpp);	// Keeps good clipboard in case of bail out later
	}

	mask = calloc(1, w);			// Allocate mask
	if (!mask) return -1;			// No memory for mask so bail out

	for ( i=0; i<h; i++ ) do_transform( 0, 1, w, mask, rgb_dest + i*w*bpp, rgb + i*w*bpp);

	free(mask);				// Free the mask

	return 0;
}

int mtpaint_image_rotate(float angle, int smooth, int gamma_correction)
{
	return mem_rotate_free(angle, smooth, gamma_correction, 0);
}

int mtpaint_image_coltrans(int brightness, int contrast, int saturation, int posterize, int gamma, int hue, int red, int green, int blue)		// Transform image colour
{
	if ( mem_img_bpp < 3 ) return -1;		// Must be RGB image

	return api_coltrans(mem_img[CHN_IMAGE], mem_img_bpp, mem_width, mem_height, brightness,
		contrast, saturation, posterize, gamma, hue, red, green, blue, TRUE);
}

int mtpaint_clipboard_coltrans(int brightness, int contrast, int saturation, int posterize, int gamma, int hue, int red, int green, int blue, int destructive)		// Transform clipboard colour
{
	if ( mem_clip_bpp < 3 ) return -1;		// Must be RGB image

	return api_coltrans(mem_clipboard, mem_clip_bpp, mem_clip_w, mem_clip_h, brightness,
		contrast, saturation, posterize, gamma, hue, red, green, blue, destructive);
}

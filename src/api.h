/*	api.h / mtpaint.h
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

#include <png.h>


// --- FUNCTIONS ---

void mtpaint_mem_init();		// Initialise memory & inifile
void mtpaint_window_init();		// Create main window and setup default image
void mtpaint_mem_end();			// Finish memory & inifile

int mtpaint_new_image(int w, int h, int pal_cols, int pal_type, int bpp);
					// Create new image (width, height), flushes undo history
					// return!=0=fail. pal_type=0=colour;1=greyscale

void mtpaint_refresh();			// Update canvases, menus, palette, etc.

void mtpaint_rgb_a(int r, int g, int b);	// Set paint colour A
void mtpaint_rgb_b(int r, int g, int b);	// Set paint colour B
void mtpaint_col_a(int c);			// Set paint colour A from palette
void mtpaint_col_b(int c);			// Set paint colour B from palette
void mtpaint_opacity(int c);			// Set paint opacity 0-255

void mtpaint_pixel(int x, int y);		// Paint pixel
void mtpaint_ellipse(int x1, int y1, int x2, int y2, int thick);
					// Paint ellipse: thick=line thickness, 0=filled
void mtpaint_rectangle(int x1, int y1, int x2, int y2);		// Paint filled rectangle
void mtpaint_line(int x1, int y1, int x2, int y2, int thick);	// Paint line

void mtpaint_paste(int x, int y);		// Paste the clipboard onto the canvas

void mtpaint_polygon_new();			// Clear all polygon points
void mtpaint_polygon_point(int x, int y);	// Add a new polygon point
void mtpaint_polygon_fill();			// Fill the polygon
void mtpaint_polygon_draw();			// Draw the polygon lines
int mtpaint_polygon_copy();			// Copy polygon to clipboard

void mtpaint_text(char *text, char *font, float angle, int antialias);
							// Render text to the clipboard

int mtpaint_file_load(char *filename);				// Load a file.  return<0=fail
int mtpaint_file_save(char *filename, int type, int arg);	// Save a file.  return<0=fail

int mtpaint_clipboard_load(char *filename);	// Load clipboard from an image file, return<0=fail
int mtpaint_clipboard_save(char *filename, int type, int arg);	// Save clipboard to an image file, return<0=fail
void mtpaint_clipboard_alpha2mask();				// Move alpha to clipboard mask
int mtpaint_clipboard_rotate(float angle, int smooth, int gamma_correction, int destructive);
int mtpaint_clipboard_coltrans(int brightness, int contrast, int saturation, int posterize, int gamma, int hue, int red, int green, int blue, int destructive);	// Transform clipboard colour

void mtpaint_selection(int x1, int y1, int x2, int y2);	// Set the rectangle selection
int mtpaint_selection_copy();		// Copy the rectangle selection area to the clipboard


int mtpaint_image_rotate(float angle, int smooth, int gamma_correction);
int mtpaint_image_coltrans(int brightness, int contrast, int saturation, int posterize, int gamma, int hue, int red, int green, int blue);			// Transform image colour

int mtpaint_animated_gif(char *input, char *output, int delay);	// Create animated GIF, 0=success

int mtpaint_screenshot();		// Grab a screenshot



#ifndef API_SOURCES


// --- DEFINITIONS ---

#define CHN_IMAGE 0
#define CHN_ALPHA 1
#define CHN_SEL   2
#define CHN_MASK  3
#define NUM_CHANNELS 4

/* File types */
#define FT_NONE     0
#define FT_PNG      1
#define FT_JPEG     2
#define FT_JP2      3
#define FT_J2K      4
#define FT_TIFF     5
#define FT_GIF      6
#define FT_BMP      7
#define FT_XPM      8
#define FT_XBM      9
#define FT_LSS      10
#define FT_TGA      11



// --- VALUES --- READ ONLY!!!!!!!!!!!!

GtkWidget *drawing_canvas,		// Drawing area
	*scrolledwindow_canvas,		// Scrolled window
	*main_window			// Main window
	;

int mem_undo_done, mem_undo_redo;	// Undo images before and after current image

typedef unsigned char *chanlist[NUM_CHANNELS];

chanlist mem_img;			// Array of pointers to image channels
int mem_width, mem_height;		// Current image geometry
int mem_img_bpp;			// Bytes per pixel = 1 or 3
png_color mem_pal[256];			// RGB entries for all 256 palette colours

unsigned char *mem_clipboard;		// Pointer to clipboard data
unsigned char *mem_clip_mask;		// Pointer to clipboard mask
unsigned char *mem_clip_alpha;		// Pointer to clipboard alpha
int mem_clip_bpp;			// Bytes per pixel
int mem_clip_w, mem_clip_h;		// Clipboard geometry

#endif

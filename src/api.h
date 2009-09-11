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
void mtpaint_init();			// Initialise memory & inifile
void mtpaint_quit();			// Finish memory & inifile

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

int mtpaint_text(				// Render text to the clipboard
		char	*text,		// Text to render
		int	characters,	// Characters to render (may be less if 0 is encountered first)
		char	*filename,	// Font file
		char	*encoding,	// Encoding of text, e.g. ASCII, UTF-8, UNICODE
		double	size,		// Scalable font size
		int	face_index,	// Usually 0, but maybe higher for bitmap fonts like *.FON
		double	angle,		// Rotation, anticlockwise
		int	flags		// MT_TEXT_* flags
		);

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

#define MT_TEXT_SHRINK 1		// Shrink size to minimum area possible around text
#define MT_TEXT_MONO 2			// Force mono rendering
#define MT_TEXT_ROTATE_NN 4		// Use nearest neighbour rotation on bitmap fonts
#define MT_TEXT_OBLIQUE 8		// Apply Oblique matrix transformation to scalable fonts



// --- VALUES --- READ ONLY!!!!!!!!!!!!

#ifdef GTK_MAJOR_VERSION
GtkWidget *drawing_canvas,		// Drawing area
	*scrolledwindow_canvas,		// Scrolled window
	*main_window			// Main window
	;
#endif

// !!! WARNING - the rest of this is broken: out of synch with memory.h - WJ

#define UNDO_TILEMAP_SIZE 32

typedef unsigned char *chanlist[NUM_CHANNELS];

typedef struct {
	chanlist img;
	png_color pal[256];
	unsigned char tilemap[UNDO_TILEMAP_SIZE], *tileptr;
	int cols, width, height, bpp, flags, size;
} undo_item;

typedef struct {
	undo_item *items;	// Pointer to undo images + current image being edited
	int pointer;		// Index of currently used image on canvas/screen
	int done;		// Undo images that we have behind current image (i.e. possible UNDO)
	int redo;		// Undo images that we have ahead of current image (i.e. possible REDO)
	int max;		// Total number of undo slots
} undo_stack;

typedef struct {
	chanlist img;		// Array of pointers to image channels
	png_color pal[256];	// RGB entries for all 256 palette colours
	int cols;	// Number of colours in the palette: 1..256 or 0 for no image
	int bpp;		// Bytes per pixel = 1 or 3
	int width, height;	// Image geometry
	undo_stack undo_;	// Image's undo stack
} image_info;

image_info mem_clip;			// Current clipboard

#define mem_clipboard		mem_clip.img[CHN_IMAGE]
#define mem_clip_alpha		mem_clip.img[CHN_ALPHA]
#define mem_clip_mask		mem_clip.img[CHN_SEL]
#define mem_clip_bpp		mem_clip.bpp
#define mem_clip_w		mem_clip.width
#define mem_clip_h		mem_clip.height


int mem_undo_done, mem_undo_redo;	// Undo images before and after current image

chanlist mem_img;			// Array of pointers to image channels
int mem_width, mem_height;		// Current image geometry
int mem_img_bpp;			// Bytes per pixel = 1 or 3
png_color mem_pal[256];			// RGB entries for all 256 palette colours

#endif		// API_SOURCES

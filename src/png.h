/*	png.h
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

#define SILENCE_LIMIT (512 * 512)

#define FILE_PROGRESS 1048576

//	Loading/Saving errors

#define NOT_INDEXED -10
#define TOO_BIG -11
#define NOT_XBM -12
#define NOT_JPEG -13
#define NOT_XPM -14
#define NOT_TIFF -15
#define NOT_GIF -16

#define FILE_LIB_ERROR 123456
#define FILE_MEM_ERROR 123457
#define FILE_GIF_ANIM 123458


#ifdef WIN32
	#define DIR_SEP '\\'
#else
	#define DIR_SEP '/'
#endif

/* File types */
#define FT_NONE     0
#define FT_PNG      1
#define FT_JPEG     2
#define FT_TIFF     3
#define FT_GIF      4
#define FT_BMP      5
#define FT_XPM      6
#define FT_XBM      7
#define FT_TGA      8
#define FT_PCX      9
#define FT_GPL      10
#define FT_TXT      11
#define FT_PAL      12
#define FT_LAYERS1  13
#define FT_LAYERS2  14
#define NUM_FTYPES  15

/* Features supported by file formats */
#define FF_BW      0x0001 /* Black and white */
#define FF_IDX     0x0002 /* Indexed color */
#define FF_RGB     0x0004 /* Truecolor */
#define FF_IMAGE   0x0007 /* Image of any kind */
#define FF_ANIM    0x0008 /* Animation */
#define FF_ALPHAI  0x0010 /* Alpha channel for indexed images */
#define FF_ALPHAR  0x0020 /* Alpha channel for RGB images */
#define FF_ALPHA   0x0030 /* Alpha channel for all images */
#define FF_MULTI   0x0040 /* Multiple channels */
#define FF_TRANS   0x0080 /* Indexed transparency */
#define FF_COMPR   0x0100 /* Compression levels */
#define FF_SPOT    0x0200 /* "Hot spot" */
#define FF_LAYER   0x0400 /* Layered images */
#define FF_PALETTE 0x0800 /* Palette file (not image) */

#define LONGEST_EXT 5

typedef struct {
	char *name, *ext, *ext2;
	guint32 flags;
} fformat;

fformat file_formats[NUM_FTYPES];

/* All-in-one transport container for save/load */
typedef struct {
	/* Configuration data */
	int mode, ftype;
	int xpm_trans, jpeg_quality, hot_x, hot_y;
	int gif_delay;
	int rgb_trans;
	int silent;
	/* Image data */
	chanlist img;
	png_color *pal;
	int width, height, bpp, colors;
} ls_settings;

char preserved_gif_filename[256];
int preserved_gif_delay;


int file_type_by_ext(char *name, guint32 mask);

int save_image(char *file_name, ls_settings *settings);

int load_image(char *file_name, int mode, int ftype);

int export_undo(char *file_name, ls_settings *settings);
int export_ascii ( char *file_name );

int detect_image_format(char *name);

int show_html(char *browser, char *docs);

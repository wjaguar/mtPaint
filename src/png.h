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

#define PNG_BYTES_TO_CHECK 8

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


//	File extension codes

#define EXT_NONE 0
#define EXT_PNG 1
#define EXT_JPEG 2
#define EXT_TIFF 3
#define EXT_BMP 4
#define EXT_GIF 5
#define EXT_XPM 6
#define EXT_XBM 7
#define EXT_GPL 8
#define EXT_TXT 9

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

typedef struct {
	char *name, *ext;
	guint32 flags;
} fformat;

fformat file_formats[NUM_FTYPES];

/* All-in-one transport container for save/load */
typedef struct {
	/* Configuration data */
	int mode, ftype;
	int xpm_trans, jpeg_quality, hot_x, hot_y;
	int gif_delay;
	int silent;
	/* Image data */
	chanlist img;
	png_color *pal;
	int width, height, bpp, colors;
} ls_settings;

char preserved_gif_filename[256];
int preserved_gif_delay;


int file_extension_get( char *file_name );	// Get the file type from the extension

int save_image( char *file_name );	// Save current canvas to file - sense extension to set type

int load_png( char *file_name, int stype );
int save_png( char *file_name, int stype );

int load_gif( char *file_name, int *delay );
int save_gif( char *file_name );
int save_gif_real( char *file_name,
	unsigned char *im, png_color *pal, int w, int h, int trans, int skip );

int load_xpm( char *file_name );
int save_xpm( char *file_name );
int load_xbm( char *file_name );
int save_xbm( char *file_name );
int load_jpeg( char *file_name );
int save_jpeg( char *file_name );
int load_tiff( char *file_name );
int save_tiff( char *file_name );
int load_bmp( char *file_name );
int save_bmp( char *file_name );

int save_channel( char *filename, unsigned char *image, int w, int h );
int load_channel( char *filename, unsigned char *image, int w, int h );

int export_undo ( char *file_name, int type );
int export_ascii ( char *file_name );

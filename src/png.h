/*	png.h
	Copyright (C) 2004-2007 Mark Tyler and Dmitry Groshev

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

//	Loading/Saving errors

#define NOT_INDEXED -10
#define TOO_BIG -11
#define NOT_XBM -12
#define NOT_JPEG -13
#define NOT_XPM -14
#define NOT_LSS -15
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
#define FT_LSS      8
#define FT_TGA      9
#define FT_PCX      10
#define FT_GPL      11
#define FT_TXT      12
#define FT_PAL      13
#define FT_LAYERS1  14
#define FT_LAYERS2  15
#define NUM_FTYPES  16

/* Features supported by file formats */
#define FF_BW      0x0001 /* Black and white */
#define FF_16      0x0002 /* 16 colors */
#define FF_256     0x0004 /* 256 colors */
#define FF_IDX     0x0007 /* Indexed image */
#define FF_RGB     0x0008 /* Truecolor */
#define FF_IMAGE   0x000F /* Image of any kind */
#define FF_ANIM    0x0010 /* Animation */
#define FF_ALPHAI  0x0020 /* Alpha channel for indexed images */
#define FF_ALPHAR  0x0040 /* Alpha channel for RGB images */
#define FF_ALPHA   0x0060 /* Alpha channel for all images */
#define FF_MULTI   0x0080 /* Multiple channels */
#define FF_TRANS   0x0100 /* Indexed transparency */
#define FF_COMP    0x0600 /* Configurable compression */
#define FF_COMPJ   0x0200 /* JPEG compression */
#define FF_COMPZ   0x0400 /* zlib compression */
#define FF_COMPR   0x0600 /* RLE compression */
#define FF_SPOT    0x0800 /* "Hot spot" */
#define FF_LAYER   0x1000 /* Layered images */
#define FF_PALETTE 0x2000 /* Palette file (not image) */

#define FF_SAVE_MASK (mem_img_bpp == 3 ? FF_RGB : mem_cols > 16 ? FF_256 : \
	mem_cols > 2 ? FF_16 | FF_256 : FF_IDX)

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
	int xpm_trans;
	int hot_x, hot_y;
	int jpeg_quality;
	int png_compression;
	int tga_RLE;
	int gif_delay;
	int rgb_trans;
	int silent;
	/* Image data */
	chanlist img;
	png_color *pal;
	int width, height, bpp, colors;
} ls_settings;

char preserved_gif_filename[256];
int preserved_gif_delay, silence_limit, jpeg_quality, png_compression;
int tga_RLE, tga_565, tga_defdir;

int file_type_by_ext(char *name, guint32 mask);

int save_image(char *file_name, ls_settings *settings);

int load_image(char *file_name, int mode, int ftype);

int export_undo(char *file_name, ls_settings *settings);
int export_ascii ( char *file_name );

int detect_image_format(char *name);

int valid_file(char *filename);		// Can this file be opened for reading?

int show_html(char *browser, char *docs);

/*	png.h
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

#define PNG_BYTES_TO_CHECK 8

#define MAX_WIDTH 16384
#define MAX_HEIGHT 16384
#define MIN_WIDTH 1
#define MIN_HEIGHT 1

#ifdef U_GUADALINEX
	#define DEFAULT_WIDTH 800
	#define DEFAULT_HEIGHT 600
#else
	#define DEFAULT_WIDTH 640
	#define DEFAULT_HEIGHT 480
#endif

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

int file_extension_get( char *file_name );	// Get the file type from the extension

int save_image( char *file_name );	// Save current canvas to file - sense extension to set type

int load_png( char *file_name, int stype );
int save_png( char *file_name, int stype );

int load_gif( char *file_name );
int save_gif( char *file_name );
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

int export_undo ( char *file_name, int type );
int export_ascii ( char *file_name );

/*	memory.c
	Copyright (C) 2004-2006 Mark Tyler

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

#include <math.h>
#include <stdlib.h>
#include <png.h>

#include "global.h"

#include "memory.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "png.h"
#include "patterns.c"
#include "mygtk.h"
#include "layer.h"
#include "inifile.h"
#include "canvas.h"


/// Tint tool - contributed by Dmitry Groshev, January 2006

int tint_mode[3] = {0,0,0};		// [0] = off/on, [1] = add/subtract, [2] = button (none, left, middle, right : 0-3)

/// IMAGE

char mem_filename[256];			// File name of file loaded/saved
unsigned char *mem_image = NULL;	// Pointer to malloc'd memory with byte by byte image data
int mem_image_bpp = 0;			// Bytes per pixel = 1 or 3
int mem_changed = 0;			// Changed since last load/save flag 0=no, 1=changed
int mem_width = 0, mem_height = 0;
float mem_icx = 0.5, mem_icy = 0.5;	// Current centre x,y
int mem_ics = 0;			// Has the centre been set by the user? 0=no 1=yes
int mem_background = 180;		// Non paintable area

unsigned char *mem_clipboard = NULL;	// Pointer to clipboard data
unsigned char *mem_clip_mask = NULL;	// Pointer to clipboard mask
unsigned char *mem_brushes = NULL;	// Preset brushes screen memory
int brush_tool_type = TOOL_SQUARE;	// Last brush tool type
char mem_clip_file[2][256];		// 0=Current filename, 1=temp filename
int mem_clip_bpp = 0;			// Bytes per pixel
int mem_clip_w = -1, mem_clip_h = -1;	// Clipboard geometry
int mem_clip_x = -1, mem_clip_y = -1;	// Clipboard location on canvas
int mem_nudge = -1;			// Nudge pixels per SHIFT+Arrow key during selection/paste

int mem_preview = 0;			// Preview an RGB change
int mem_prev_bcsp[5];			// BR, CO, SA, POSTERIZE

undo_item mem_undo_im[MAX_UNDO];	// Pointers to undo images + current image being edited

int mem_undo_pointer = 0;		// Pointer to currently used image on canas/screen
int mem_undo_done = 0;		// Undo images that we have behind current image (i.e. possible UNDO)
int mem_undo_redo = 0;		// Undo images that we have ahead of current image (i.e. possible REDO)
int mem_undo_limit = 32;	// Max MB memory allocation limit
int mem_undo_opacity = 0;	// Use previous image for opacity calculations?

/// GRID

int mem_show_grid, mem_grid_min;	// Boolean show toggle & minimum zoom to show it at
unsigned char mem_grid_rgb[3];		// RGB colour of grid

/// PATTERNS

unsigned char *mem_pats = NULL;		// RGB screen memory holding current patterns
unsigned char *mem_patch = NULL;	// RGB screen memory holding all patterns for choosing
unsigned char *mem_col_pat;		// Indexed 8x8 colourised pattern using colours A & B
unsigned char *mem_col_pat24;		// RGB 8x8 colourised pattern using colours A & B

/// PREVIEW/TOOLS

char *mem_prev = NULL;			// RGB colours preview
int tool_type = TOOL_SQUARE;		// Currently selected tool
int tool_size = 1, tool_flow = 1;
int tool_opacity = 100;			// Transparency - 100=solid
int tool_pat = 0;			// Tool pattern number
int tool_fixx = -1, tool_fixy = -1;	// Fixate on axis
int pen_down = 0;			// Are we drawing? - Used to see if we need to do an UNDO
int tool_ox = 0, tool_oy = 0;		// Previous tool coords - used by continuous mode
int mem_continuous = 0;			// Area we painting the static shapes continuously?

int mem_brcosa_allow[3];		// BRCOSA RGB



/// FILE

int mem_xpm_trans = -1;			// Current XPM file transparency colour index
int mem_xbm_hot_x=-1, mem_xbm_hot_y=-1;	// Current XBM hot spot
int mem_jpeg_quality = 85;		// JPEG quality setting

/// PALETTE

png_color mem_pal[256];			// RGB entries for all 256 palette colours
int mem_cols = 0;			// Number of colours in the palette: 2..256 or 0 for no image
int mem_col_A = 1, mem_col_B = 0;	// Index for colour A & B
png_color mem_col_A24, mem_col_B24;	// RGB for colour A & B
char *mem_pals = NULL;			// RGB screen memory holding current palette
int found[1024][3];			// Used by mem_cols_used() & mem_convert_indexed
char mem_prot_mask[256];		// 256 bytes used for indexed images
int mem_prot_RGB[256];			// Up to 256 RGB colours protected
int mem_prot;				// 0..256 : Number of protected colours in mem_prot_RGB

int mem_brush_list[81][3] = {		// Preset brushes parameters
{ TOOL_SPRAY, 5, 1 }, { TOOL_SPRAY, 7, 1 }, { TOOL_SPRAY, 9, 2 },
{ TOOL_SPRAY, 13, 2 }, { TOOL_SPRAY, 15, 3 }, { TOOL_SPRAY, 19, 3 },
{ TOOL_SPRAY, 23, 4 }, { TOOL_SPRAY, 27, 5 }, { TOOL_SPRAY, 31, 6 },

{ TOOL_SPRAY, 5, 5 }, { TOOL_SPRAY, 7, 7 }, { TOOL_SPRAY, 9, 9 },
{ TOOL_SPRAY, 13, 13 }, { TOOL_SPRAY, 15, 15 }, { TOOL_SPRAY, 19, 19 },
{ TOOL_SPRAY, 23, 23 }, { TOOL_SPRAY, 27, 27 }, { TOOL_SPRAY, 31, 31 },

{ TOOL_SPRAY, 5, 15 }, { TOOL_SPRAY, 7, 21 }, { TOOL_SPRAY, 9, 27 },
{ TOOL_SPRAY, 13, 39 }, { TOOL_SPRAY, 15, 45 }, { TOOL_SPRAY, 19, 57 },
{ TOOL_SPRAY, 23, 69 }, { TOOL_SPRAY, 27, 81 }, { TOOL_SPRAY, 31, 93 },

{ TOOL_CIRCLE, 3, -1 }, { TOOL_CIRCLE, 5, -1 }, { TOOL_CIRCLE, 7, -1 },
{ TOOL_CIRCLE, 9, -1 }, { TOOL_CIRCLE, 13, -1 }, { TOOL_CIRCLE, 17, -1 },
{ TOOL_CIRCLE, 21, -1 }, { TOOL_CIRCLE, 25, -1 }, { TOOL_CIRCLE, 31, -1 },

{ TOOL_SQUARE, 1, -1 }, { TOOL_SQUARE, 2, -1 }, { TOOL_SQUARE, 3, -1 },
{ TOOL_SQUARE, 4, -1 }, { TOOL_SQUARE, 8, -1 }, { TOOL_SQUARE, 12, -1 },
{ TOOL_SQUARE, 16, -1 }, { TOOL_SQUARE, 24, -1 }, { TOOL_SQUARE, 32, -1 },

{ TOOL_SLASH, 3, -1 }, { TOOL_SLASH, 5, -1 }, { TOOL_SLASH, 7, -1 },
{ TOOL_SLASH, 9, -1 }, { TOOL_SLASH, 13, -1 }, { TOOL_SLASH, 17, -1 },
{ TOOL_SLASH, 21, -1 }, { TOOL_SLASH, 25, -1 }, { TOOL_SLASH, 31, -1 },

{ TOOL_BACKSLASH, 3, -1 }, { TOOL_BACKSLASH, 5, -1 }, { TOOL_BACKSLASH, 7, -1 },
{ TOOL_BACKSLASH, 9, -1 }, { TOOL_BACKSLASH, 13, -1 }, { TOOL_BACKSLASH, 17, -1 },
{ TOOL_BACKSLASH, 21, -1 }, { TOOL_BACKSLASH, 25, -1 }, { TOOL_BACKSLASH, 31, -1 },

{ TOOL_VERTICAL, 3, -1 }, { TOOL_VERTICAL, 5, -1 }, { TOOL_VERTICAL, 7, -1 },
{ TOOL_VERTICAL, 9, -1 }, { TOOL_VERTICAL, 13, -1 }, { TOOL_VERTICAL, 17, -1 },
{ TOOL_VERTICAL, 21, -1 }, { TOOL_VERTICAL, 25, -1 }, { TOOL_VERTICAL, 31, -1 },

{ TOOL_HORIZONTAL, 3, -1 }, { TOOL_HORIZONTAL, 5, -1 }, { TOOL_HORIZONTAL, 7, -1 },
{ TOOL_HORIZONTAL, 9, -1 }, { TOOL_HORIZONTAL, 13, -1 }, { TOOL_HORIZONTAL, 17, -1 },
{ TOOL_HORIZONTAL, 21, -1 }, { TOOL_HORIZONTAL, 25, -1 }, { TOOL_HORIZONTAL, 31, -1 },

};

int mem_pal_def_i = 256;		// Items in default palette

png_color mem_pal_def[256]={		// Default palette entries for new image
/// All RGB in 3 bits per channel. i.e. 0..7 - multiply by 255/7 for full RGB ..
/// .. or: int lookup[8] = {0, 36, 73, 109, 146, 182, 219, 255};

/// Primary colours = 8

#ifdef U_GUADALINEX
{7,7,7}, {0,0,0}, {7,0,0}, {0,7,0}, {7,7,0}, {0,0,7}, {7,0,7}, {0,7,7},
#else
{0,0,0}, {7,0,0}, {0,7,0}, {7,7,0}, {0,0,7}, {7,0,7}, {0,7,7}, {7,7,7},
#endif

/// Primary fades to black: 7 x 6 = 42

{6,6,6}, {5,5,5}, {4,4,4}, {3,3,3}, {2,2,2}, {1,1,1},
{6,0,0}, {5,0,0}, {4,0,0}, {3,0,0}, {2,0,0}, {1,0,0},
{0,6,0}, {0,5,0}, {0,4,0}, {0,3,0}, {0,2,0}, {0,1,0},
{6,6,0}, {5,5,0}, {4,4,0}, {3,3,0}, {2,2,0}, {1,1,0},
{0,0,6}, {0,0,5}, {0,0,4}, {0,0,3}, {0,0,2}, {0,0,1},
{6,0,6}, {5,0,5}, {4,0,4}, {3,0,3}, {2,0,2}, {1,0,1},
{0,6,6}, {0,5,5}, {0,4,4}, {0,3,3}, {0,2,2}, {0,1,1},

/// Shading triangles: 6 x 21 = 126
/// RED
{7,6,6}, {6,5,5}, {5,4,4}, {4,3,3}, {3,2,2}, {2,1,1},
{7,5,5}, {6,4,4}, {5,3,3}, {4,2,2}, {3,1,1},
{7,4,4}, {6,3,3}, {5,2,2}, {4,1,1},
{7,3,3}, {6,2,2}, {5,1,1},
{7,2,2}, {6,1,1},
{7,1,1},

/// GREEN
{6,7,6}, {5,6,5}, {4,5,4}, {3,4,3}, {2,3,2}, {1,2,1},
{5,7,5}, {4,6,4}, {3,5,3}, {2,4,2}, {1,3,1},
{4,7,4}, {3,6,3}, {2,5,2}, {1,4,1},
{3,7,3}, {2,6,2}, {1,5,1},
{2,7,2}, {1,6,1},
{1,7,1},

/// BLUE
{6,6,7}, {5,5,6}, {4,4,5}, {3,3,4}, {2,2,3}, {1,1,2},
{5,5,7}, {4,4,6}, {3,3,5}, {2,2,4}, {1,1,3},
{4,4,7}, {3,3,6}, {2,2,5}, {1,1,4},
{3,3,7}, {2,2,6}, {1,1,5},
{2,2,7}, {1,1,6},
{1,1,7},

/// YELLOW (red + green)
{7,7,6}, {6,6,5}, {5,5,4}, {4,4,3}, {3,3,2}, {2,2,1},
{7,7,5}, {6,6,4}, {5,5,3}, {4,4,2}, {3,3,1},
{7,7,4}, {6,6,3}, {5,5,2}, {4,4,1},
{7,7,3}, {6,6,2}, {5,5,1},
{7,7,2}, {6,6,1},
{7,7,1},

/// MAGENTA (red + blue)
{7,6,7}, {6,5,6}, {5,4,5}, {4,3,4}, {3,2,3}, {2,1,2},
{7,5,7}, {6,4,6}, {5,3,5}, {4,2,4}, {3,1,3},
{7,4,7}, {6,3,6}, {5,2,5}, {4,1,4},
{7,3,7}, {6,2,6}, {5,1,5},
{7,2,7}, {6,1,6},
{7,1,7},

/// CYAN (blue + green)
{6,7,7}, {5,6,6}, {4,5,5}, {3,4,4}, {2,3,3}, {1,2,2},
{5,7,7}, {4,6,6}, {3,5,5}, {2,4,4}, {1,3,3},
{4,7,7}, {3,6,6}, {2,5,5}, {1,4,4},
{3,7,7}, {2,6,6}, {1,5,5},
{2,7,7}, {1,6,6},
{1,7,7},


/// Scales: 11 x 6 = 66

/// RGB
{7,6,5}, {6,5,4}, {5,4,3}, {4,3,2}, {3,2,1}, {2,1,0},
{7,5,4}, {6,4,3}, {5,3,2}, {4,2,1}, {3,1,0},

/// RBG
{7,5,6}, {6,4,5}, {5,3,4}, {4,2,3}, {3,1,2}, {2,0,1},
{7,4,5}, {6,3,4}, {5,2,3}, {4,1,2}, {3,0,1},

/// BRG
{6,5,7}, {5,4,6}, {4,3,5}, {3,2,4}, {2,1,3}, {1,0,2},
{5,4,7}, {4,3,6}, {3,2,5}, {2,1,4}, {1,0,3},

/// BGR
{5,6,7}, {4,5,6}, {3,4,5}, {2,3,4}, {1,2,3}, {0,1,2},
{4,5,7}, {3,4,6}, {2,3,5}, {1,2,4}, {0,1,3},

/// GBR
{5,7,6}, {4,6,5}, {3,5,4}, {2,4,3}, {1,3,2}, {0,2,1},
{4,7,5}, {3,6,4}, {2,5,3}, {1,4,2}, {0,3,1},

/// GRB
{6,7,5}, {5,6,4}, {4,5,3}, {3,4,2}, {2,3,1}, {1,2,0},
{5,7,4}, {4,6,3}, {3,5,2}, {2,4,1}, {1,3,0},

/// Misc
{7,5,0}, {6,4,0}, {5,3,0}, {4,2,0},		// Oranges
{7,0,5}, {6,0,4}, {5,0,3}, {4,0,2},		// Red Pink
{0,5,7}, {0,4,6}, {0,3,5}, {0,2,4},		// Blues
{0,0,0}, {0,0,0}

/// End: Primary (8) + Fades (42) + Shades (126) + Scales (66) + Misc (14) = 256
};

char mem_cross[9][9] = {
	{1,1,0,0,0,0,1,1},
	{1,1,1,0,0,1,1,1},
	{0,1,1,1,1,1,1,0},
	{0,0,1,1,1,1,0,0},
	{0,0,1,1,1,1,0,0},
	{0,1,1,1,1,1,1,0},
	{1,1,1,0,0,1,1,1},
	{1,1,0,0,0,0,1,1}
};
char mem_numbers[10][7][7] = { {
	{0,0,1,1,1,1,0},
	{0,1,1,0,0,1,1},
	{0,1,1,0,0,1,1},
	{0,1,1,0,0,1,1},
	{0,1,1,0,0,1,1},
	{0,1,1,0,0,1,1},
	{0,0,1,1,1,1,0}
},{
	{0,0,0,1,1,0,0},
	{0,0,1,1,1,0,0},
	{0,0,0,1,1,0,0},
	{0,0,0,1,1,0,0},
	{0,0,0,1,1,0,0},
	{0,0,0,1,1,0,0},
	{0,0,0,1,1,0,0}
},{
	{0,0,1,1,1,1,0},
	{0,1,1,0,0,1,1},
	{0,0,0,0,0,1,1},
	{0,0,0,0,1,1,0},
	{0,0,0,1,1,0,0},
	{0,0,1,1,0,0,0},
	{0,1,1,1,1,1,1}
},{
	{0,0,1,1,1,1,0},
	{0,1,1,0,0,1,1},
	{0,0,0,0,0,1,1},
	{0,0,0,1,1,1,0},
	{0,0,0,0,0,1,1},
	{0,1,1,0,0,1,1},
	{0,0,1,1,1,1,0}
},{
	{0,0,0,0,1,1,0},
	{0,0,0,1,1,1,0},
	{0,0,1,1,1,1,0},
	{0,1,1,0,1,1,0},
	{0,1,1,1,1,1,1},
	{0,0,0,0,1,1,0},
	{0,0,0,0,1,1,0}
},{
	{0,1,1,1,1,1,1},
	{0,1,1,0,0,0,0},
	{0,1,1,1,1,1,0},
	{0,0,0,0,0,1,1},
	{0,0,0,0,0,1,1},
	{0,1,1,0,0,1,1},
	{0,0,1,1,1,1,0}
},{
	{0,0,0,1,1,1,0},
	{0,0,1,1,0,0,0},
	{0,1,1,0,0,0,0},
	{0,1,1,1,1,1,0},
	{0,1,1,0,0,1,1},
	{0,1,1,0,0,1,1},
	{0,0,1,1,1,1,0}
},{
	{0,1,1,1,1,1,1},
	{0,0,0,0,0,1,1},
	{0,0,0,0,1,1,0},
	{0,0,0,0,1,1,0},
	{0,0,0,1,1,0,0},
	{0,0,0,1,1,0,0},
	{0,0,0,1,1,0,0}
},{
	{0,0,1,1,1,1,0},
	{0,1,1,0,0,1,1},
	{0,1,1,0,0,1,1},
	{0,0,1,1,1,1,0},
	{0,1,1,0,0,1,1},
	{0,1,1,0,0,1,1},
	{0,0,1,1,1,1,0}
},{
	{0,0,1,1,1,1,0},
	{0,1,1,0,0,1,1},
	{0,1,1,0,0,1,1},
	{0,0,1,1,1,1,1},
	{0,0,0,0,0,1,1},
	{0,0,0,0,1,1,0},
	{0,0,1,1,1,0,0}
} };


int mem_new( int width, int height, int bpp )	// Allocate space for new image, removing old if needed
{
	int i, j, k, res=0;

	for (i=0; i<MAX_UNDO; i++)		// Release old UNDO images
		if ( mem_undo_im[i].image!=NULL )
		{
			free( mem_undo_im[i].image );
			mem_undo_im[i].image = NULL;
		}

	mem_image = malloc( width*height*bpp );
	if ( mem_image == NULL )		// System was unable to allocate as requested
	{
		width = 8;
		height = 8;			// 8x8 is bound to work!
		res = 1;
		mem_image = malloc( width*height*bpp );
	}

	j = width * height * bpp;
	i = 0;
#ifdef U_GUADALINEX
	if ( bpp == 3 ) i = 255;
#endif
	for ( k=0; k<j; k++ ) mem_image[k] = i;

	mem_width = width;
	mem_height = height;
	mem_image_bpp = bpp;

	mem_undo_pointer = 0;
	mem_undo_done = 0;
	mem_undo_redo = 0;

	mem_undo_im[0].image = mem_image;
	mem_undo_im[0].cols = mem_cols;
	mem_undo_im[0].bpp = mem_image_bpp;
	mem_undo_im[0].width = width;
	mem_undo_im[0].height = height;
	mem_pal_copy( mem_undo_im[0].pal, mem_pal );

	mem_col_A = 1, mem_col_B = 0;
	mem_col_A24 = mem_pal[mem_col_A];
	mem_col_B24 = mem_pal[mem_col_B];

	clear_file_flags();

	return res;
}

unsigned char *mem_undo_previous()	// Get address of previous image (or current if none)
{
	int i = mem_undo_pointer - 1;

	if ( mem_undo_done < 1 ) return mem_image;	// No undo so use current
	if ( i<0 ) i=i + MAX_UNDO;

	return mem_undo_im[i].image;
}

void lose_oldest()				// Lose the oldest undo image
{						// Pre-requisite: mem_undo_done > 0
	int t_po = (mem_undo_pointer - mem_undo_done + MAX_UNDO) % MAX_UNDO;
		// Lose oldest image in an attempt to get under the limit
	if ( mem_undo_im[t_po].image != NULL )
	{
		free(mem_undo_im[t_po].image);
		mem_undo_im[t_po].image = NULL;
	}

	mem_undo_done--;
}

int undo_next_core( int handle, int new_width, int new_height, int x_start, int y_start, int new_bpp )
{
	char *old_image, *new_image;
	int i, j, t_po;

	notify_changed();
	if ( pen_down == 0 )
	{
		pen_down = 1;
//printf("Old undo # = %i\n", mem_undo_pointer);

		if ( mem_undo_redo > 0 )		// Release any redundant redo images
		{
			t_po = (mem_undo_pointer + 1) % MAX_UNDO;
			for ( i=1; i<=mem_undo_redo; i++ )
			{
				if ( mem_undo_im[t_po].image != NULL )
				{
					free(mem_undo_im[t_po].image);
					mem_undo_im[t_po].image = NULL;
				}
				t_po = (t_po + 1) % MAX_UNDO;
			}
			mem_undo_redo = 0;
		}

		while ( (mem_used() + new_width*new_height*new_bpp ) >
			(mem_undo_limit*1024*1024/(layers_total+1)) )
		{		// We have exceeded MB limit, release any excess images
			if ( mem_undo_done == 0 ) return 1;
				// Return with no undo done as this image is just too big
			lose_oldest();
		}

		old_image = mem_undo_im[mem_undo_pointer].image;

		mem_undo_im[mem_undo_pointer].cols = mem_cols;
		mem_pal_copy( mem_undo_im[mem_undo_pointer].pal, mem_pal );	// Copy palette to undo

		if (mem_undo_done >= (MAX_UNDO-1))
		{				// Maximum undo reached, so lose the MAX_UNDO'th image
			t_po = (mem_undo_pointer + 1) % MAX_UNDO;
			if ( mem_undo_im[t_po].image != NULL )
			{
				free(mem_undo_im[t_po].image);
				mem_undo_im[t_po].image = NULL;
			}
		}
		else	mtMIN(mem_undo_done, mem_undo_done+1, MAX_UNDO-1);

		new_image = malloc( new_bpp*new_width*new_height );			// Grab memory
		while ( new_image == NULL )
		{
			if ( mem_undo_done == 0 )
			{
//				if ( handle == 0 )
//				{
//					printf("The system has no spare memory to give the mem_undo_next procedure for a canvas - bailing out\n");
//					exit(1);
//				}
//				else
					return 2;
			}
			lose_oldest();
			new_image = malloc( new_bpp*new_width*new_height );	// Grab memory
		}

		mem_undo_pointer = (mem_undo_pointer + 1) % MAX_UNDO;		// New pointer

		mem_image = new_image;
		mem_undo_im[mem_undo_pointer].image = mem_image;
		mem_undo_im[mem_undo_pointer].width = new_width;
		mem_undo_im[mem_undo_pointer].height = new_height;
		mem_undo_im[mem_undo_pointer].cols = mem_cols;
		mem_undo_im[mem_undo_pointer].bpp = new_bpp;
		mem_pal_copy( mem_undo_im[mem_undo_pointer].pal, mem_pal );

		if ( handle == 0 )
			for ( i=0; i<(new_width*new_height*new_bpp); i++ ) new_image[i] = old_image[i];
				// Copy image

		if ( handle == 1 )		// Cropping
		{
			if ( mem_image_bpp == 1 )
				for ( j=0; j<new_height; j++ )
					for ( i=0; i<new_width; i++ )
						new_image[i + j*new_width] =
							old_image[ x_start + i
							+ mem_width*(j + y_start) ];
			if ( mem_image_bpp == 3 )
				for ( j=0; j<new_height; j++ )
					for ( i=0; i<new_width; i++ )
					{
						new_image[ 3*(i + j*new_width) ] =
							old_image[ 3*(x_start + i
							+ mem_width*(j + y_start)) ];
						new_image[ 1 + 3*(i + j*new_width) ] =
							old_image[ 1 + 3*(x_start + i
							+ mem_width*(j + y_start)) ];
						new_image[ 2 + 3*(i + j*new_width) ] =
							old_image[ 2 + 3*(x_start + i
							+ mem_width*(j + y_start)) ];
					}
		}

		if ( handle == 2 )		// Dummy Handle used when not cropping
		{
		}

		mem_width = new_width;
		mem_height = new_height;
		mem_image_bpp = new_bpp;
//printf("New undo # = %i\n\n", mem_undo_pointer);
	}

	return 0;
}

void mem_undo_next()		// Call this after a draw event but before any changes to image
{
	undo_next_core( 0, mem_width, mem_height, 0, 0, mem_image_bpp );
}

int mem_undo_next2( int new_w, int new_h, int from_x, int from_y )
					// Call this after crop/resize to record undo + new geometry
{
//printf("Cropping to w/h: %i,%i data from x/y : %i,%i\n", new_w, new_h, from_x, from_y);
	pen_down = 0;
	return undo_next_core( 1, new_w, new_h, from_x, from_y, mem_image_bpp );
}

void mem_undo_backward()		// UNDO requested by user
{
	if ( mem_undo_done > 0 )
	{
//printf("UNDO!!! Old undo # = %i\n", mem_undo_pointer);
		mem_undo_im[mem_undo_pointer].cols = mem_cols;
		mem_pal_copy( mem_undo_im[mem_undo_pointer].pal, mem_pal );

		mem_undo_pointer = (mem_undo_pointer - 1 + MAX_UNDO) % MAX_UNDO;	// New pointer

		mem_image = mem_undo_im[mem_undo_pointer].image;
		mem_width = mem_undo_im[mem_undo_pointer].width;
		mem_height = mem_undo_im[mem_undo_pointer].height;
		mem_cols = mem_undo_im[mem_undo_pointer].cols;
		mem_image_bpp = mem_undo_im[mem_undo_pointer].bpp;
		mem_pal_copy( mem_pal, mem_undo_im[mem_undo_pointer].pal );

		mem_undo_done--;
		mem_undo_redo++;

		if ( mem_col_A >= mem_cols ) mem_col_A = 0;
		if ( mem_col_B >= mem_cols ) mem_col_B = 0;
//printf("New undo # = %i\n\n", mem_undo_pointer);
	}
	pen_down = 0;
}

void mem_undo_forward()			// REDO requested by user
{
	if ( mem_undo_redo > 0 )
	{
//printf("REDO!!! Old undo # = %i\n", mem_undo_pointer);
		mem_undo_im[mem_undo_pointer].cols = mem_cols;
		mem_pal_copy( mem_undo_im[mem_undo_pointer].pal, mem_pal );

		mem_undo_pointer = (mem_undo_pointer + 1) % MAX_UNDO;		// New pointer

		mem_image = mem_undo_im[mem_undo_pointer].image;
		mem_width = mem_undo_im[mem_undo_pointer].width;
		mem_height = mem_undo_im[mem_undo_pointer].height;
		mem_cols = mem_undo_im[mem_undo_pointer].cols;
		mem_image_bpp = mem_undo_im[mem_undo_pointer].bpp;
		mem_pal_copy( mem_pal, mem_undo_im[mem_undo_pointer].pal );

		mem_undo_done++;
		mem_undo_redo--;

		if ( mem_col_A >= mem_cols ) mem_col_A = 0;
		if ( mem_col_B >= mem_cols ) mem_col_B = 0;
//printf("New undo # = %i\n\n", mem_undo_pointer);
	}
	pen_down = 0;
}

int valid_file( char *filename )		// Can this file be opened for reading?
{
	FILE *fp;

	fp = fopen(filename, "r");
	if ( fp == NULL ) return -1;
	else
	{
		fclose( fp );
		return 0;
	}
}


char *grab_memory( int size, char byte )	// Malloc memory, reset all bytes
{
	char *chunk;
	int i;

	chunk = malloc( size );
	
	if (chunk != NULL) for ( i=0; i<size; i++ ) chunk[i] = byte;

	return chunk;
}

void mem_set_brush(int val)			// Set brush, update size/flow/preview
{
	int offset, i, j, k, o, o2;

	tool_type = mem_brush_list[val][0];
	tool_size = mem_brush_list[val][1];
	if ( mem_brush_list[val][2]>0 ) tool_flow = mem_brush_list[val][2];

	offset = 3*( 2 + 36*(val % 9) + 36*PATCH_WIDTH*(val / 9) + 2*PATCH_WIDTH );
			// Offset in brush RGB
	for ( j=0; j<32; j++ )
	{
		o = 3*PATTERN_WIDTH*PATTERN_WIDTH + 3*PATTERN_WIDTH*j;	// Preview offset
		o2 = offset + 3*PATCH_WIDTH*j;				// Offset in brush RGB
		for ( i=0; i<32; i++ )
		{
			for ( k=0; k<3; k++ )
				mem_pats[o + 3*i + k] = mem_brushes[o2 + 3*i + k];
		}
	}
}

void mem_init()					// Initialise memory
{
	char txt[300];
	int i, j, lookup[8] = {0, 36, 73, 109, 146, 182, 219, 255}, ix, iy, bs, bf, bt;
	png_color temp_pal[256];

	mem_pats = grab_memory( 3*PATTERN_WIDTH*PATTERN_HEIGHT, 0 );
	mem_col_pat = grab_memory( 8*8, 0 );
	mem_col_pat24 = grab_memory( 3*8*8, 0 );
	mem_pals = grab_memory( 3*PALETTE_WIDTH*PALETTE_HEIGHT, 0 );
	mem_prev = grab_memory( 3*PREVIEW_WIDTH*PREVIEW_HEIGHT, 0 );
	mem_brushes = grab_memory( 3*PATCH_WIDTH*PATCH_HEIGHT, 0 );

	for ( i=0; i<256; i++ )		// Load up normal palette defaults
	{
		mem_pal_def[i].red = lookup[mem_pal_def[i].red];
		mem_pal_def[i].green = lookup[mem_pal_def[i].green];
		mem_pal_def[i].blue = lookup[mem_pal_def[i].blue];
	}
	mem_pal_copy( temp_pal, mem_pal_def );

	snprintf( txt, 290, "%s/mtpaint.gpl", get_home_directory() );
	i = valid_file(txt);
	if ( i == 0 )
	{
		i = mem_load_pal( txt, temp_pal );
		if ( i>1 )
		{
			mem_pal_copy( mem_pal_def, temp_pal );
			mem_cols = i;
			mem_pal_def_i = i;
		}
	}

		// Create brush presets

	if ( mem_new( PATCH_WIDTH, PATCH_HEIGHT, 3 ) != 0 )	// Not enough memory!
	{
		memory_errors(1);
		exit(0);
	}
	mem_mask_setall(0);
	mem_pal_copy( mem_pal, mem_pal_def );
	mem_cols = mem_pal_def_i;

	mem_col_A24.red = 255;
	mem_col_A24.green = 255;
	mem_col_A24.blue = 255;
	mem_col_B24.red = 0;
	mem_col_B24.green = 0;
	mem_col_B24.blue = 0;

/*
	mem_col_B24 = mem_pal[0];
	mem_col_A24.red = ( (255 - mem_col_B24.red) >> 7 ) * 255;
	mem_col_A24.green = ( (255 - mem_col_B24.green) >> 7 ) * 255;
	mem_col_A24.blue = ( (255 - mem_col_B24.blue) >> 7 ) * 255;
*/

	j = mem_width*mem_height*3;
	for ( i=0; i<j; i=i+3 )
	{
		mem_image[i] = mem_col_B24.red;
		mem_image[i+1] = mem_col_B24.green;
		mem_image[i+2] = mem_col_B24.blue;
	}

	mem_pat_update();

	for ( i=0; i<81; i++ )					// Draw each brush
	{
		ix = 18 + 36 * (i % 9);
		iy = 18 + 36 * (i / 9);
		bt = mem_brush_list[i][0];
		bs = mem_brush_list[i][1];
		bf = mem_brush_list[i][2];

		if ( bt == TOOL_SQUARE ) f_rectangle( ix - bs/2, iy - bs/2, bs, bs );
		if ( bt == TOOL_CIRCLE ) f_circle( ix, iy, bs );
		if ( bt == TOOL_VERTICAL ) f_rectangle( ix, iy - bs/2, 1, bs );
		if ( bt == TOOL_HORIZONTAL ) f_rectangle( ix - bs/2, iy, bs, 1 );
		if ( bt == TOOL_SLASH ) for ( j=0; j<bs; j++ ) PUT_PIXEL24( ix-bs/2+j, iy+bs/2-j )
		if ( bt == TOOL_BACKSLASH ) for ( j=0; j<bs; j++ ) PUT_PIXEL24( ix+bs/2-j, iy+bs/2-j )
		if ( bt == TOOL_SPRAY )
			for ( j=0; j<bf*3; j++ )
				PUT_PIXEL24( ix-bs/2 + rand() % bs, iy-bs/2 + rand() % bs )
	}

	j = 3*PATCH_WIDTH*PATCH_HEIGHT;
	for ( i=0; i<j; i++ )
	{
		mem_brushes[i] = mem_image[i];		// Store image for later use
		mem_image[i] = 0;			// Clear so user doesn't see it upon load fail
	}

	mem_set_brush(36);		// Initial brush
}

void copy_dig( int index, int tx, int ty )
{
	int i, j, r;

	index = index % 10;

	for ( j=0; j<7; j++ )
	{
		for ( i=0; i<7; i++ )
		{
			r = 200*mem_numbers[index][j][i];
			mem_pals[ 0+3*( tx+i + PALETTE_WIDTH*(ty+j) ) ] = r;
			mem_pals[ 1+3*( tx+i + PALETTE_WIDTH*(ty+j) ) ] = r;
			mem_pals[ 2+3*( tx+i + PALETTE_WIDTH*(ty+j) ) ] = r;
		}
	}
	
}

void copy_num( int index, int tx, int ty )
{
	index = index % 1000;

	if ( index >= 100 ) copy_dig( index/100, tx, ty);
	if ( index >= 10 ) copy_dig( (index/10) % 10, tx+8, ty);
	copy_dig( index % 10, tx+16, ty);
}

void mem_swap_cols()
{
	int oc;
	png_color o24;

	oc = mem_col_A;
	mem_col_A = mem_col_B;
	mem_col_B = oc;

	o24 = mem_col_A24;
	mem_col_A24 = mem_col_B24;
	mem_col_B24 = o24;

	mem_pat_update();
}

void repaint_top_swatch()			// Update selected colours A & B
{
	int i, j, r[2], g[2], b[2], nx, ny;

	if ( mem_image_bpp == 1 )
	{
		mem_col_A24 = mem_pal[mem_col_A];
		mem_col_B24 = mem_pal[mem_col_B];
	}
	r[0] = mem_col_A24.red;
	g[0] = mem_col_A24.green;
	b[0] = mem_col_A24.blue;
	r[1] = mem_col_B24.red;
	g[1] = mem_col_B24.green;
	b[1] = mem_col_B24.blue;

	for ( j=0; j<30; j++ )
	{
		for ( i=0; i<30; i++ )
		{
			nx = 1+i; ny = 1+j;
			mem_prev[ 0 + 3*( nx + ny*PREVIEW_WIDTH) ] = r[0];
			mem_prev[ 1 + 3*( nx + ny*PREVIEW_WIDTH) ] = g[0];
			mem_prev[ 2 + 3*( nx + ny*PREVIEW_WIDTH) ] = b[0];

			nx = 1+i; ny = 33+j;
			mem_prev[ 0 + 3*( nx + ny*PREVIEW_WIDTH) ] = r[1];
			mem_prev[ 1 + 3*( nx + ny*PREVIEW_WIDTH) ] = g[1];
			mem_prev[ 2 + 3*( nx + ny*PREVIEW_WIDTH) ] = b[1];
		}
	}
}

void repaint_swatch( int index )		// Update a palette colour swatch
{
	int tx=25, ty=35+index*16-34, i, j,
		r=mem_pal[index].red, g=mem_pal[index].green, b=mem_pal[index].blue;

	for ( j=0; j<16; j++ )
	{
		for ( i=0; i<26; i++ )
		{
			mem_pals[ 0+3*( tx+i + PALETTE_WIDTH*(ty+j) ) ] = r;
			mem_pals[ 1+3*( tx+i + PALETTE_WIDTH*(ty+j) ) ] = g;
			mem_pals[ 2+3*( tx+i + PALETTE_WIDTH*(ty+j) ) ] = b;
		}
	}

	if ( mem_prot_mask[index] == 0 ) g = 0;		// Protection mask cross
	else g = 1;
	tx = 53+4; ty = 39 + index*16-34;
	for ( j=0; j<8; j++ )
	{
		for ( i=0; i<8; i++ )
		{
			r = 200*g*mem_cross[j][i];
			mem_pals[ 0+3*( tx+i + PALETTE_WIDTH*(ty+j) ) ] = r;
			mem_pals[ 1+3*( tx+i + PALETTE_WIDTH*(ty+j) ) ] = r;
			mem_pals[ 2+3*( tx+i + PALETTE_WIDTH*(ty+j) ) ] = r;
		}
	}

	copy_num( index, 0, 40 + index*16-34 );		// Index number
}

static void validate_pal( int i, int rgb[3], png_color *pal )
{
	int j;

	for ( j=0; j<3; j++ )
	{
		mtMAX( rgb[j], rgb[j], 0 )
		mtMIN( rgb[j], rgb[j], 255 )
	}
	pal[i].red = rgb[0];
	pal[i].green = rgb[1];
	pal[i].blue = rgb[2];
}

void mem_pal_load_def()					// Load default palette
{
	mem_pal_copy( mem_pal, mem_pal_def );
	mem_cols = mem_pal_def_i;
}

int mem_load_pal( char *file_name, png_color *pal )	// Load file into palette array >1 => cols read
{
	int rgb[3], new_mem_cols=0, i;
	FILE *fp;
	char input[128];


	if ((fp = fopen(file_name, "r")) == NULL) return -1;

	if ( get_next_line(input, 30, fp) != 0 )
	{
		fclose( fp );
		return -1;
	}

	if ( strncmp( input, "GIMP Palette", 12 ) == 0 )
	{
//printf("Gimp palette file\n");
		while ( get_next_line(input, 120, fp) == 0 && new_mem_cols<256 )
		{	// Continue to read until EOF or new_mem_cols>255
			// If line starts with a number or space assume its a palette entry
			if ( input[0] == ' ' || (input[0]>='0' && input[0]<='9') )
			{
//printf("Line %3i = %s", new_mem_cols, input);
				sscanf(input, "%i %i %i", &rgb[0], &rgb[1], &rgb[2] );
				validate_pal( new_mem_cols, rgb, pal );
				new_mem_cols++;
			}
//			else printf("NonLine = %s\n", input);
		}
	}
	else
	{
		sscanf(input, "%i", &new_mem_cols);
		mtMAX( new_mem_cols, new_mem_cols, 2 )
		mtMIN( new_mem_cols, new_mem_cols, 256 )

		for ( i=0; i<new_mem_cols; i++ )
		{
			get_next_line(input, 30, fp);
/*			if ( get_next_line(input, 30, fp) != 0 )
			{
printf("Failed - line %i is > 30 chars\n", i);
				fclose( fp );
				return -1;
			}*/
			sscanf(input, "%i,%i,%i\n", &rgb[0], &rgb[1], &rgb[2] );
			validate_pal( i, rgb, pal );
/*			for ( j=0; j<3; j++ )
			{
				mtMAX( rgb[j], rgb[j], 0 )
				mtMIN( rgb[j], rgb[j], 255 )
			}
			pal[i].red = rgb[0];
			pal[i].green = rgb[1];
			pal[i].blue = rgb[2];*/
		}
	}
	fclose( fp );

	return new_mem_cols;
}

void mem_pal_init()					// Initialise whole of palette RGB
{
	int i;

	for ( i=0; i<3*PALETTE_WIDTH*PALETTE_HEIGHT; i++ ) mem_pals[i] = 0;

	repaint_top_swatch();
	mem_mask_init();		// Prepare RGB masks

	for ( i=0; i<mem_cols; i++ ) repaint_swatch( i );
}

void mem_mask_init()			// Initialise RGB protection mask array
{
	int i;

	mem_prot = 0;
	for (i=0; i<mem_cols; i++)
	{
		if ( mem_prot_mask[i] == 1 )
		{
			mem_prot_RGB[mem_prot] = PNG_2_INT( mem_pal[i] );
			mem_prot++;
		}
	}
}

void mem_mask_setall(char val)
{
	int i;

	for (i=0; i<256; i++) mem_prot_mask[i] = val;
}

void mem_pat_update()			// Update indexed and then RGB pattern preview
{
	int i, j, i2, j2, c, offset;
	png_color c24;

	if ( mem_image_bpp == 1 )
	{
		mem_col_A24 = mem_pal[mem_col_A];
		mem_col_B24 = mem_pal[mem_col_B];
	}

	for ( j=0; j<8; j++ )
	{
		for ( i=0; i<8; i++ )
		{
			if ( mem_patterns[tool_pat][j][i] == 1 )
			{
				c = mem_col_A;
				c24 = mem_col_A24;
			}
			else
			{
				c = mem_col_B;
				c24 = mem_col_B24;
			}

			mem_col_pat[i + j*8] = c;

			mem_col_pat24[ 3*(i + j*8) ] = c24.red;
			mem_col_pat24[ 1 + 3*(i + j*8) ] = c24.green;
			mem_col_pat24[ 2 + 3*(i + j*8) ] = c24.blue;

			for ( j2=0; j2<4; j2++ )
			{
				for ( i2=0; i2<4; i2++ )
				{
					offset = 3*(i+i2*8 + (j+j2*8)*PATTERN_WIDTH);
					mem_pats[ 0 + offset ] = c24.red;
					mem_pats[ 1 + offset ] = c24.green;
					mem_pats[ 2 + offset ] = c24.blue;
				}
			}
		}
	}
}

void mem_get_histogram()		// Calculate how many of each colour index is on the canvas
{
	int i, j = mem_width*mem_height;

	for ( i=0; i<256; i++ ) mem_histogram[i] = 0;

	for ( i=0; i<j; i++ ) mem_histogram[ mem_image[i] ]++;
}

void mem_gamma_chunk( unsigned char *rgb, int len )		// Apply gamma to RGB memory
{
	double trans = 100/((double) mem_prev_bcsp[4]);
	int i, j;
	unsigned char table[256];

	for ( i=0; i<256; i++ ) table[i] = mt_round( 255*pow( ((double) i)/255, trans ) );

	for ( i=0; i<len; i++ )
		for ( j=0; j<3; j++ )
			if ( mem_brcosa_allow[j] )
				rgb[3*i + j] = table[ rgb[3*i + j] ];
}

void mem_posterize_chunk( unsigned char *rgb, int len )		// Apply posterize to RGB memory
{
	int i, j, res, posty = mem_prev_bcsp[3];

	for ( i=0; i<len; i++ )
	{
		for ( j=0; j<3; j++ )
		{
			if ( mem_brcosa_allow[j] )
			{
				res = rgb[3*i + j];
				POSTERIZE_MACRO
				rgb[3*i + j] = res;
			}
		}
	}
}

int do_posterize(int val, int posty)	// Posterize a number
{
	int res = val;
	POSTERIZE_MACRO
	return res;
}

int pal_dupes[256];

int scan_duplicates()			// Find duplicate palette colours, return number found
{
	int i, j, found = 0, ended;

	if ( mem_cols < 3 ) return 0;

	for ( i = mem_cols - 1; i > 0; i-- )
	{
		pal_dupes[i] = -1;			// Start with a clean sheet
		j = 0;
		ended = 0;
		while ( ended == 0 )
		{
			if (	mem_pal[i].red == mem_pal[j].red &&
				mem_pal[i].green == mem_pal[j].green &&
				mem_pal[i].blue == mem_pal[j].blue )
			{
				ended = 1;
				found++;
				pal_dupes[i] = j;	// Point to first duplicate in the palette
			}
			j++;
			if ( j == i ) ended = 1;
		}
	}

	return found;
}

void remove_duplicates()		// Remove duplicate palette colours - call AFTER scan_duplicates
{
	int i;
	unsigned char pix;

	for ( i = 0; i < mem_width*mem_height; i++ )		// Scan canvas for duplicates
	{
		pix = mem_image[i];
		if ( pal_dupes[pix] >= 0 )			// Duplicate found
			mem_image[i] = pal_dupes[pix];
	}
}

int mem_remove_unused_check()
{
	int i, found = 0;

	mem_get_histogram();
	for ( i=0; i<mem_cols; i++ ) if ( mem_histogram[i] == 0 ) found++;

	if ( found == 0 ) return 0;			// All palette colours are used on the canvas
	if ( (mem_cols - found) < 2 ) return -1;	// Canvas is all one colour

	return found;
}

int mem_remove_unused()
{
	unsigned char conv[256];
	int i, j, found = mem_remove_unused_check();

	if ( found <= 0 ) return found;

	j = 0;
	for ( i=0; i<256; i++ )				// Create conversion table
	{
		if ( mem_histogram[i] > 0 )
		{
			conv[i] = j;
			mem_pal[j] = mem_pal[i];
			j++;
		}
	}

	for ( i=0; i<(mem_width*mem_height); i++ ) mem_image[i] = conv[mem_image[i]];
							// Convert canvas pixels as required

	mem_cols = mem_cols - found;

	return found;
}

void mem_scale_pal( int i1, int r1, int g1, int b1, int i2, int r2, int g2, int b2 )
{
	int i, ls[8], tot;

	if ( i1 < i2 )		// Switch if not in correct order
	{
		ls[0] = i1; ls[1] = r1; ls[2] = g1; ls[3] = b1;
		ls[4] = i2; ls[5] = r2; ls[6] = g2; ls[7] = b2;
	}
	else
	{
		ls[4] = i1; ls[5] = r1; ls[6] = g1; ls[7] = b1;
		ls[0] = i2; ls[1] = r2; ls[2] = g2; ls[3] = b2;
	}

	tot = ls[4] - ls[0];
	if ( ls[0] != ls[4] )		// Only do something if index values are different
	{
		// Change palette as requested
		for ( i=ls[0]; i<=ls[4]; i++ )
		{
			mem_pal[i].red = mt_round(	((float) i - ls[0])/tot * ls[5] +
							((float) ls[4] - i)/tot * ls[1] );
			mem_pal[i].green = mt_round(	((float) i - ls[0])/tot * ls[6] +
							((float) ls[4] - i)/tot * ls[2] );
			mem_pal[i].blue = mt_round(	((float) i - ls[0])/tot * ls[7] +
							((float) ls[4] - i)/tot * ls[3] );
		}
	}
}


///	BRIGHTNESS CONTRAST SATURATION

void mem_brcosa_chunk( unsigned char *rgb, int len )
// Apply BRCOSA to RGB memory
{	// brightness = -255..+255, contrast = 0..+4, saturation = -1..+1
	float ch[3], grey;
	int i, j;

	int	br = mem_prev_bcsp[0];
	float	co = ((float) mem_prev_bcsp[1]) / 100,
		sa = ((float) mem_prev_bcsp[2]) / 100;

	mtMIN( br, br, 255 )
	mtMAX( br, br, -255 )
	mtMIN( co, co, 1 )
	mtMAX( co, co, -1 )
	mtMIN( sa, sa, 1 )
	mtMAX( sa, sa, -1 )

	if ( co > 0 ) co = co*3 + 1;
	else co = co + 1;

	for ( i=0; i<len; i++ )
	{
		ch[0] = rgb[ 3*i ];
		ch[1] = rgb[ 1 + 3*i ];
		ch[2] = rgb[ 2 + 3*i ];

		for ( j=0; j<3; j++ )		// Calculate brightness/contrast
		{
			if ( mem_brcosa_allow[j] )
			{
				ch[j] = (ch[j] - 127.5)*co + 127.5 + br;	// Brightness & Contrast
				mtMIN( ch[j], ch[j], 255 )
				mtMAX( ch[j], ch[j], 0 )
			}
		}
		grey = 0.3 * ch[0] + 0.58 * ch[1] + 0.12 * ch[2];
		for ( j=0; j<3; j++ )						// Calculate saturation
		{
			if ( mem_brcosa_allow[j] )
			{
				ch[j] = -grey * sa + ch[j] * (1 + sa);
				mtMIN( ch[j], ch[j], 255 )
				mtMAX( ch[j], ch[j], 0 )
			}
		}

		rgb[ 3*i ] = mt_round( ch[0] );
		rgb[ 1 + 3*i ] = mt_round( ch[1] );
		rgb[ 2 + 3*i ] = mt_round( ch[2] );
	}
}

void mem_brcosa_pal( png_color *pal1, png_color *pal2 )
{		// Palette 1 = Palette 2 adjusting brightness/contrast/saturation
	int i;
	unsigned char tpal[256*3];

	for ( i=0; i<256; i++ )
	{
		tpal[ 3*i ] = pal2[ i ].red;
		tpal[ 1 + 3*i ] = pal2[ i ].green;
		tpal[ 2 + 3*i ] = pal2[ i ].blue;
	}

	if ( mem_prev_bcsp[4] != 100 ) mem_gamma_chunk( tpal, 256 );
	mem_brcosa_chunk( tpal, 256 );

	for ( i=0; i<256; i++ )
	{
		pal1[ i ].red = tpal[ 3*i ];
		pal1[ i ].green = tpal[ 1 + 3*i ];
		pal1[ i ].blue = tpal[ 2 + 3*i ];
	}
}

void set_zoom_centre( int x, int y )
{
	IF_IN_RANGE( x, y )
	{
		mem_icx = ((float) x ) / mem_width;
		mem_icy = ((float) y ) / mem_height;
		mem_ics = 1;
	}
}

void mem_pal_copy( png_color *pal1, png_color *pal2 )	// Palette 1 = Palette 2
{
	int i;

	for ( i=0; i<256; i++) pal1[i] = pal2[i];
}

int mem_pal_cmp( png_color *pal1, png_color *pal2 )	// Count itentical palette entries
{
	int i, j = 0;

	for ( i=0; i<256; i++ ) if ( pal1[i].red != pal2[i].red ||
				pal1[i].green != pal2[i].green ||
				pal1[i].blue != pal2[i].blue ) j++;

	return j;
}

int mem_used()				// Return the number of bytes used in image + undo
{
	int i, total = 0;

	for ( i=0; i<MAX_UNDO; i++ )
		if ( mem_undo_im[i].image != NULL )
			total = total + mem_undo_im[i].width * mem_undo_im[i].height
				* mem_undo_im[i].bpp;

	return total;
}

int mem_used_layers()		// Return the number of bytes used in image + undo in all layers
{
	int i, total = 0, l=0;
	undo_item *mundo;

	for ( l=0; l<=layers_total; l++ )
	{
		if ( l==layer_selected ) mundo = mem_undo_im;
		else mundo = layer_table[l].image->mem_undo_im;

		for ( i=0; i<MAX_UNDO; i++ )
		{
			if ( mundo[i].image != NULL )
				total = total + mundo[i].width * mundo[i].height * mundo[i].bpp;
		}
	}

	return total;
}

int mem_convert_rgb()			// Convert image to RGB
{
	char *new_image = NULL;
	unsigned char pix;
	int i, j, res;

	j = mem_width * mem_height;
	new_image = malloc( 3*j );		// Grab new memory chunk
	if ( new_image == NULL ) return 1;	// Not enough memory

	for ( i=0; i<j; i++ )
	{
		pix = mem_image[i];
		new_image[ 3*i ] = mem_pal[pix].red;
		new_image[ 1 + 3*i ] = mem_pal[pix].green;
		new_image[ 2 + 3*i ] = mem_pal[pix].blue;
	}

	pen_down = 0;				// Ensure next tool action is treated separately
	res = undo_next_core( 2, mem_width, mem_height, 0, 0, 3 );
	pen_down = 0;
	if ( res == 1 )
	{
		free( new_image );		// Free memory
		return 2;
	}

	j = mem_height * mem_width * 3;
	for ( i=0; i<j; i++)			// Copy new data to new mem_image
		mem_image[i] = new_image[i];

	free( new_image );			// Free memory

	return 0;
}

int mem_convert_indexed()	// Convert RGB image to Indexed Palette - call after mem_cols_used
{
	unsigned char *old_image = mem_image;
	int i, j, k, res, f;

	pen_down = 0;				// Ensure next tool action is treated separately
	res = undo_next_core( 2, mem_width, mem_height, 0, 0, 1 );
	pen_down = 0;
	if ( res == 1 )	return 2;

	j = mem_width * mem_height;
	for ( i=0; i<j; i++ )
	{
		k = 0;
		f = 0;
		while ( k<256 && f==0 )		// Find index of this RGB
		{
			if (	found[k][0] == old_image[ 3*i ] &&
				found[k][1] == old_image[ 1 + 3*i ] &&
				found[k][2] == old_image[ 2 + 3*i ] ) f = 1;
			else k++;
		}
		if ( k>255 ) return 1;		// No index found - BAD ERROR!!
		mem_image[i] = k;
	}

	for ( i=0; i<256; i++ )
	{
		mem_pal[i].red = found[i][0];
		mem_pal[i].green = found[i][1];
		mem_pal[i].blue = found[i][2];
	}
	mem_col_A = 1;
	mem_col_B = 0;

	return 0;
}

int mem_quantize( unsigned char *old_mem_image, int target_cols, int type )
	// type = 1:flat, 2:dither, 3:scatter
{
	int i, j, k;//, res=0;
	int closest[3][2];
	png_color pcol;

	j = mem_width * mem_height;

//	pen_down = 0;				// Ensure next tool action is treated separately
//	res = undo_next_core( 2, mem_width, mem_height, 0, 0, 1 );
//	pen_down = 0;
//	if ( res == 1 ) return 2;

	progress_init(_("Converting to Indexed Palette"),1);

	for ( j=0; j<mem_height; j++ )		// Convert RGB to indexed
	{
		if ( j%16 == 0)
			if (progress_update( ((float) j)/(mem_height) )) goto stop;
		for ( i=0; i<mem_width; i++ )
		{
			pcol.red = old_mem_image[ 3*(i + mem_width*j) ];
			pcol.green = old_mem_image[ 1 + 3*(i + mem_width*j) ];
			pcol.blue = old_mem_image[ 2 + 3*(i + mem_width*j) ];

			closest[0][0] = 0;		// 1st Closest palette item to pixel
			closest[1][0] = 100000000;
			closest[0][1] = 0;		// 2nd Closest palette item to pixel
			closest[1][1] = 100000000;
			for ( k=0; k<target_cols; k++ )
			{
				closest[2][0] = abs( pcol.red - mem_pal[k].red ) +
					abs( pcol.green - mem_pal[k].green ) +
					abs( pcol.blue - mem_pal[k].blue );
				if ( closest[2][0] < closest[1][0] )
				{
					closest[0][1] = closest[0][0];
					closest[1][1] = closest[1][0];
					closest[0][0] = k;
					closest[1][0] = closest[2][0];
				}
				else
				{
					if ( closest[2][0] < closest[1][1] )
					{
						closest[0][1] = k;
						closest[1][1] = closest[2][0];
					}
				}
			}
			if ( type == 1 ) k = closest[0][0];		// Flat conversion
			else
			{
				if ( closest[1][1] == 100000000 ) closest[1][0] = 0;
				if ( closest[1][0] == 0 ) k = closest[0][0];
				else
				{
				  if ( type == 2 )			// Dithered
				  {
//				  	if ( closest[1][1]/2 >= closest[1][0] )
				  	if ( closest[1][1]*.67 < (closest[1][1] - closest[1][0]) )
						k = closest[0][0];
					else
					{
					  	if ( closest[0][0] > closest[0][1] )
							k = closest[0][ (i+j) % 2 ];
						else
							k = closest[0][ (i+j+1) % 2 ];
					}
				  }
				  if ( type == 3 )			// Scattered
				  {
				    if ( (rand() % (closest[1][1] + closest[1][0])) <= closest[1][1] )
						k = closest[0][0];
				    else	k = closest[0][1];
				  }
				}
			}
			mem_image[ i + mem_width*j ] = k;
		}
	}
stop:
	mem_col_A = 1;
	mem_col_B = 0;
	progress_end();

	return 0;
}

void mem_greyscale()			// Convert image to greyscale
{
	int i, j, k, l, v;
	float value;

	if ( mem_image_bpp == 1)
		for ( i=0; i<256; i++ )
		{
			value = 0.49 + 0.3 * mem_pal[i].red +
				0.58 * mem_pal[i].green + 0.12 * mem_pal[i].blue;
			v = mt_round( value );
			mem_pal[i].red = value;
			mem_pal[i].green = value;
			mem_pal[i].blue = value;
		}
	if ( mem_image_bpp == 3)
	{
		j = mem_width * mem_height * 3;
		progress_init(_("Converting to Greyscale"),1);
		i = 0; l = 0;
		while ( i<j )
		{
			if ( l%16 == 0) if (progress_update( ((float) l)/(mem_height) )) goto stop;
			for ( k=0; k<mem_width; k++ )
			{
				value = 0.49 + 0.3 * ( mem_image[i]) +
					0.58 * ( mem_image[i+1]) +
					0.12 * ( mem_image[i+2]);
				v = mt_round( value );
				mem_image[i] = value;
				mem_image[i+1] = value;
				mem_image[i+2] = value;
				i=i+3;
			}
			l++;
		}
stop:
		progress_end();
	}
}

void pal_hsl( png_color col, float *hh, float *ss, float *ll )
{
	float	h = 0.0, s = 0.0, v = 0.0,
		r = col.red, g = col.green, b = col.blue,
		mini, maxi, delta;
	int order = 0;

	r = r / 255;
	g = g / 255;
	b = b / 255;

	mini = r;
	maxi = r;

	if (g > maxi) { maxi = g; order = 1; }
	if (b > maxi) { maxi = b; order = 2; }
	if (g < mini) mini = g;
	if (b < mini) mini = b;

	delta = maxi - mini;
	v = maxi;
	if ( maxi != 0 )
	{
		s = delta / maxi;

		switch (order)
		{
			case 0: { h = ( g - b ) / delta; break; }		// yel < h < mag
			case 1: { h = 2 + ( b - r ) / delta; break; }		// cyan < h < yel
			case 2: { h = 4 + ( r - g ) / delta; break; }		// mag < h < cyan
		}
		h = h*60;
		if( h < 0 ) h = h + 360;
	}
	else
	{
		s = 0;
		h = 0;
	}

	mtMAX( h, h, 0 )
	mtMIN( h, h, 360 )

	*hh = h;
	*ss = s;
	*ll = 255*( 0.30*r + 0.58*g + 0.12*b );
}

float rgb_hsl( int t, png_color col )
{
	float h, s, l;

	pal_hsl( col, &h, &s, &l );

	if ( t == 0 ) return h;
	if ( t == 1 ) return s;
	if ( t == 2 ) return l;

	return -1;
}

void mem_pal_index_move( int c1, int c2 )	// Move index c1 to c2 and shuffle in between up/down
{
	int direct, i;
	png_color temp;

	if ( c1==c2 ) return;

	direct = 1;
	if ( c1 > c2 ) direct = -1;

	i = c1;
	do
	{
		i = i + direct;
		temp = mem_pal[i];		// do swap
		mem_pal[i] = mem_pal[i - direct];
		mem_pal[i - direct] = temp;
	} while (i != c2);
}

void mem_canvas_index_move( int c1, int c2 )	// Similar to palette item move but reworks canvas pixels
{
	unsigned char table[256], pix;
	int direct, i, j = mem_width*mem_height;

	if ( c1==c2 ) return;

	direct = 1;
	i = 0;
	if ( c1 > c2 )
	{
		direct = -1;
		i = 255;
	}

	while ( i>=0 && i<=255 )
	{
		if ( (i<c1 && i<c2) || (i>c1 && i>c2) )		// Not in range so unchanged
		{
			table[i] = i;
		}
		else
		{
			if ( i == c1 ) table[i] = c2;
			else table[i] = i - direct;
		}
		i = i + direct;
	}

	for ( i=0; i<j; i++ )		// Change pixel index to new palette
	{
		pix = mem_image[i];
		mem_image[i] = table[pix];
	}
}

void mem_pal_sort( int a, int i1, int i2, int rev )		// Sort colours in palette
{
	int tab[257][3], i, j;
	png_color old_pal[256];
	unsigned char pix;

	if ( i2 == i1 || i1>mem_cols || i2>mem_cols ) return;
	if ( i2 < i1 )
	{
		i = i1;
		i1 = i2;
		i2 = i;
	}

	if ( a == 6 ) mem_get_histogram();
	
	for ( i=i1; i<=i2; i++ )
	{
		tab[i][0] = i;
		tab[i][2] = i;
		if ( a==0 ) tab[i][1] = mt_round( 1000*rgb_hsl( 0, mem_pal[i] ) );
		if ( a==1 ) tab[i][1] = mt_round( 1000*rgb_hsl( 1, mem_pal[i] ) );
		if ( a==2 ) tab[i][1] = mt_round( rgb_hsl( 2, mem_pal[i] ) );

		if ( a==3 ) tab[i][1] = mem_pal[i].red;
		if ( a==4 ) tab[i][1] = mem_pal[i].green;
		if ( a==5 ) tab[i][1] = mem_pal[i].blue;

		if ( a==6 ) tab[i][1] = mem_histogram[i];
	}

	for ( j=i2; j>i1; j-- )			// The venerable bubble sort
		for ( i=i1; i<j; i++ )
		{
			if ( (!rev && tab[i][1] > tab[i+1][1]) || (rev && tab[i][1] < tab[i+1][1]) )
			{
				tab[256][0] = tab[i][0];
				tab[256][1] = tab[i][1];

				tab[i][0] = tab[i+1][0];
				tab[i][1] = tab[i+1][1];
				tab[ tab[i][0] ][2] = i;

				tab[i+1][0] = tab[256][0];
				tab[i+1][1] = tab[256][1];
				tab[ tab[i+1][0] ][2] = i+1;
			}
		}

	mem_pal_copy( old_pal, mem_pal );
	for ( i=i1; i<=i2; i++ )
	{
		mem_pal[i] = old_pal[ tab[i][0] ];
	}

	if ( mem_image_bpp == 1 )		// Adjust canvas pixels if in indexed palette mode
		for ( i=0; i<mem_width*mem_height; i++ )
		{
			pix = mem_image[i];
			if ( pix >= i1 && pix <= i2 )		// Only change as needed
				mem_image[i] = tab[pix][2];
		}
}

void mem_invert()			// Invert the palette
{
	int i, j, k, l, v;
	png_color temp;

	if ( mem_image_bpp == 1 )
		for ( i=0; i<256; i++ )
		{
			temp = mem_pal[i];
			mem_pal[i].red = 255 - temp.red;
			mem_pal[i].green = 255 - temp.green;
			mem_pal[i].blue = 255 - temp.blue;
		}
	if ( mem_image_bpp == 3)
	{
		j = mem_width * mem_height * 3;
		progress_init(_("Inverting Image"),1);
		i = 0; l = 0;
		while ( i<j )
		{
			if ( l%16 == 0) if (progress_update( ((float) l)/(mem_height) )) goto stop;
			for ( k=0; k<mem_width; k++ )
			{
				v = mem_image[i];
				mem_image[i] = 255 - v;
				v = mem_image[i+1];
				mem_image[i+1] = 255 - v;
				v = mem_image[i+2];
				mem_image[i+2] = 255 - v;
				i=i+3;
			}
			l++;
		}
stop:
		progress_end();
	}
}

void mem_boundary( int *x, int *y, int *w, int *h )		// Check/amend boundaries
{
	if ( *x < 0 )
	{
		*w = *w + *x;
		*x = 0;
	}
	if ( *y < 0 )
	{
		*h = *h + *y;
		*y = 0;
	}
	if ( (*x + *w) > mem_width )
	{
		*w = mem_width - *x;
	}
	if ( (*y + *h) > mem_height )
	{
		*h = mem_height - *y;
	}
}

void sline( int x1, int y1, int x2, int y2 )		// Draw single thickness straight line
{
	int i, xdo, ydo, px, py, todo;
	float rat;

	xdo = x2 - x1;
	ydo = y2 - y1;
	mtMAX( todo, abs(xdo), abs(ydo) )
	if (todo==0) todo=1;

	for ( i=0; i<=todo; i++ )
	{
		rat = ((float) i ) / todo;
		px = mt_round(x1 + (x2 - x1) * rat);
		py = mt_round(y1 + (y2 - y1) * rat);
		if ( mem_image_bpp == 1 ) IF_IN_RANGE( px, py ) PUT_PIXEL( px, py )
		if ( mem_image_bpp == 3 ) IF_IN_RANGE( px, py ) PUT_PIXEL24( px, py )
	}
}


void tline( int x1, int y1, int x2, int y2, int size )		// Draw size thickness straight line
{
	int xv, yv;			// x/y vectors
	int xv2, yv2;
	int i, xdo, ydo, px, py, todo;
	float rat;
	float xuv, yuv, llen;		// x/y unit vectors, line length
	float xv1, yv1;			// vector for shadow x/y

	xdo = x2 - x1;
	ydo = y2 - y1;
	mtMAX( todo, abs(xdo), abs(ydo) )
	if (todo<2) return;		// The 1st and last points are done by calling procedure

	if ( size < 2 || ( x1 == x2 && y1 == y2) ) sline( x1, y1, x2, y2 );
	else
	{
		if ( size>20 && todo>20 )	// Thick long line so use less accurate g_para
		{
			xv = x2 - x1;
			yv = y2 - y1;
			llen = sqrt( xv * xv + yv * yv );
			xuv = ((float) xv) / llen;
			yuv = ((float) yv) / llen;

			xv1 = -yuv * ((float) size - 0.5);
			yv1 = xuv * ((float) size - 0.5);

			xv2 = mt_round(xv1 / 2 + 0.5*((size+1) %2) );
			yv2 = mt_round(yv1 / 2 + 0.5*((size+1) %2) );

			xv1 = -yuv * ((float) size - 0.5);
			yv1 = xuv * ((float) size - 0.5);

			g_para( x1 - xv2, y1 - yv2, x2 - xv2, y2 - yv2,
				mt_round(xv1), mt_round(yv1) );
		}
		else	// Short or thin line so use more accurate but slower iterative method
		{
			for ( i=1; i<todo; i++ )
			{
				rat = ((float) i ) / todo;
				px = mt_round(x1 + (x2 - x1) * rat);
				py = mt_round(y1 + (y2 - y1) * rat);
				f_circle( px, py, size );
			}
		}
	}
}

void g_para( int x1, int y1, int x2, int y2, int xv, int yv )		// Draw general parallelogram
{							// warning!  y1 != y2
	int nx1, ny1, nx2, ny2, i, j;
	int co[4][2];			// Four points of parallelogram in correct order
	float rat;
	int mx, mx1, mx2, mxlen;

//printf("xv,yv %i,%i\n", xv, yv);

	if ( xv == 0 )		// X vector is zero so its just a v_para
	{
		if ( yv < 0 )
		{
			ny1 = y1 + yv;
			ny2 = y2 + yv;
		}
		else
		{
			ny1 = y1;
			ny2 = y2;
		}
		v_para( x1, ny1, x2, ny2, abs(yv) + 1 );
		return;
	}
	if ( yv == 0 )		// Y vector is zero so its just a h_para
	{
		if ( xv < 0 )
		{
			nx1 = x1 + xv;
			nx2 = x2 + xv;
		}
		else
		{
			nx1 = x1;
			nx2 = x2;
		}
		h_para( nx1, y1, nx2, y2, abs(xv) + 1 );
		return;
	}

	if ( yv < 0 )			// yv must be positive
	{
		yv = -yv;
		xv = -xv;
		x1 = x1 - xv;
		y1 = y1 - yv;
		x2 = x2 - xv;
		y2 = y2 - yv;
	}

	if ( y1 < y2 )			// co[0] must contain the lowest 'y' coord
	{
		co[0][0] = x1;
		co[0][1] = y1;
		co[1][0] = x2;
		co[1][1] = y2;
	}
	else
	{
		co[0][0] = x2;
		co[0][1] = y2;
		co[1][0] = x1;
		co[1][1] = y1;
	}

	co[2][0] = co[0][0] + xv;
	co[2][1] = co[0][1] + yv;
	co[3][0] = co[1][0] + xv;
	co[3][1] = co[1][1] + yv;

	for ( j=co[0][1]; j<=co[3][1]; j++ )		// All y coords of parallelogram
	{
		if ( j>=0 && j<mem_height )		// Only paint on canvas
		{
			if ( j<co[1][1] )		// First X is between point 0 and 1
			{
				rat = ((float) j - co[0][1]) / ( co[1][1] - co[0][1] );
				mx1 = mt_round( co[0][0] + rat * ( co[1][0] - co[0][0] ) );
			}
			else				// First X is between point 1 and 3
			{
				rat = ((float) j - co[1][1]) / ( co[3][1] - co[1][1] );
				mx1 = mt_round( co[1][0] + rat * ( co[3][0] - co[1][0] ) );
			}
			if ( j<co[2][1] )		// Second X is between point 1 and 2
			{
				rat = ((float) j - co[0][1]) / ( co[2][1] - co[0][1] );
				mx2 = mt_round( co[0][0] + rat * ( co[2][0] - co[0][0] ) );
			}
			else				// First X is between point 2 and 4
			{
				rat = ((float) j - co[2][1]) / ( co[3][1] - co[2][1] );
				mx2 = mt_round( co[2][0] + rat * ( co[3][0] - co[2][0] ) );
			}

			mtMIN( mx, mx1, mx2 )
			mxlen = abs( mx2 - mx1 ) + 1;
			if ( mx < 0 )
			{
				mxlen = mxlen + mx;
				mx = 0;
			}
			if ( (mx + mxlen) > mem_width ) mxlen = mem_width - mx;

			if ( mem_image_bpp == 1 ) for ( i=0; i<mxlen; i++ ) PUT_PIXEL( mx + i, j )
			if ( mem_image_bpp == 3 ) for ( i=0; i<mxlen; i++ ) PUT_PIXEL24( mx + i, j )
		}
	}
}

void v_para( int x1, int y1, int x2, int y2, int vlen )		// Draw vertical sided parallelogram
{
	int i, j, xdo, ydo, px, py, todo, flen;
	float rat;

	xdo = x2 - x1;
	ydo = y2 - y1;
	mtMAX( todo, abs(xdo), abs(ydo) )

	for ( i=0; i<=todo; i++ )
	{
		rat = ((float) i ) / todo;
		px = mt_round( x1 + (x2 - x1) * rat );
		py = mt_round( y1 + (y2 - y1) * rat );
		flen = vlen;
		if ( py < 0 )
		{
			flen = flen + py;
			py = 0;
		}
		if ( (py + flen) > mem_height )
		{
			flen = mem_height - py;
		}
		if ( px<mem_width && px>=0 )
		{
			if ( mem_image_bpp == 1 ) for ( j=0; j<flen; j++ ) PUT_PIXEL( px, py+j )
			if ( mem_image_bpp == 3 ) for ( j=0; j<flen; j++ ) PUT_PIXEL24( px, py+j )
		}
	}
}

void h_para( int x1, int y1, int x2, int y2, int hlen )		// Draw horizontal top/bot parallelogram
{
	int i, j, xdo, ydo, px, py, todo, flen;
	float rat;

	xdo = x2 - x1;
	ydo = y2 - y1;
	mtMAX( todo, abs(xdo), abs(ydo) )

	for ( i=0; i<=todo; i++ )
	{
		rat = ((float) i ) / todo;
		px = mt_round( x1 + (x2 - x1) * rat );
		py = mt_round( y1 + (y2 - y1) * rat );
		flen = hlen;
		if ( px < 0 )
		{
			flen = flen + px;
			px = 0;
		}
		if ( (px + flen) > mem_width )
		{
			flen = mem_width - px;
		}
		if ( py<mem_height && py>=0 )
		{
			if ( mem_image_bpp == 1 ) for ( j=0; j<flen; j++ ) PUT_PIXEL( px+j, py )
			if ( mem_image_bpp == 3 ) for ( j=0; j<flen; j++ ) PUT_PIXEL24( px+j, py )
		}
	}
}

void flood_fill( int x, int y, unsigned int target )	// Recursively flood fill an area
{
	int minx = x, maxx = x, ended = 0, newx = 0;

	PUT_PIXEL( x, y )
	while ( ended == 0 )				// Search left for target pixels
	{
		minx--;
		if ( minx < 0 ) ended = 1;
		else
		{
			if ( GET_PIXEL( minx, y ) == target ) { PUT_PIXEL( minx, y ) }
			else ended = 1;
		}
	}
	minx++;

	ended = 0;
	while ( ended == 0 )				// Search right for target pixels
	{
		maxx++;
		if ( maxx >= mem_width ) ended = 1;
		else
		{
			if ( GET_PIXEL( maxx, y ) == target ) { PUT_PIXEL( maxx, y ) }
			else ended = 1;
		}
	}
	maxx--;

	if ( (y-1) >= 0 )				// Recurse upwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( GET_PIXEL(newx, y-1) == target ) flood_fill( newx, y-1, target );

	if ( (y+1) < mem_height )			// Recurse downwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( GET_PIXEL(newx, y+1) == target ) flood_fill( newx, y+1, target );
}

void flood_fill24( int x, int y, png_color target24 )	// Recursively flood fill an area
{
	int minx = x, maxx = x, ended = 0, newx = 0;

	PUT_PIXEL24( x, y )
	while ( ended == 0 )				// Search left for target pixels
	{
		minx--;
		if ( minx < 0 ) ended = 1;
		else
		{
			if ( !png_cmp( get_pixel24( minx, y ), target24) )
				{ PUT_PIXEL24( minx, y ) }
			else ended = 1;
		}
	}
	minx++;

	ended = 0;
	while ( ended == 0 )				// Search right for target pixels
	{
		maxx++;
		if ( maxx >= mem_width ) ended = 1;
		else
		{
			if ( !png_cmp( get_pixel24( maxx, y ), target24) )
				{ PUT_PIXEL24( maxx, y ) }
			else ended = 1;
		}
	}
	maxx--;

	if ( (y-1) >= 0 )				// Recurse upwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( png_cmp( get_pixel24(newx, y-1), target24 ) == 0 )
				flood_fill24( newx, y-1, target24 );

	if ( (y+1) < mem_height )			// Recurse downwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( png_cmp( get_pixel24(newx, y+1), target24 ) == 0 )
				flood_fill24( newx, y+1, target24 );
}

/*
By using the next 2 procedures you can flood fill using patterns without problems.
The only snag is that a chunk of memory must be allocated as a mask - the same size as the image.
This is very wasteful which is why I still use the old procedures above for flat filling.
M.Tyler 26-3-2005
*/

void flood_fill_pat( int x, int y, unsigned int target, unsigned char *pat_mem )
{	// Recursively flood fill an area with a pattern
	int minx = x, maxx = x, ended = 0, newx = 0;

	pat_mem[ x + y*mem_width ] = 1;
	while ( ended == 0 )				// Search left for target pixels
	{
		minx--;
		if ( minx < 0 ) ended = 1;
		else
		{
			if ( GET_PIXEL( minx, y ) == target ) pat_mem[ minx + y*mem_width ] = 1;
			else ended = 1;
		}
	}
	minx++;

	ended = 0;
	while ( ended == 0 )				// Search right for target pixels
	{
		maxx++;
		if ( maxx >= mem_width ) ended = 1;
		else
		{
			if ( GET_PIXEL( maxx, y ) == target ) pat_mem[ maxx + y*mem_width ] = 1;
			else ended = 1;
		}
	}
	maxx--;

	if ( (y-1) >= 0 )				// Recurse upwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( GET_PIXEL(newx, y-1) == target && pat_mem[newx + mem_width*(y-1)] == 0 )
				flood_fill_pat( newx, y-1, target, pat_mem );

	if ( (y+1) < mem_height )			// Recurse downwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( GET_PIXEL(newx, y+1) == target && pat_mem[newx + mem_width*(y+1)] == 0 )
				flood_fill_pat( newx, y+1, target, pat_mem );
}

void flood_fill24_pat( int x, int y, png_color target24, unsigned char *pat_mem )
{	// Recursively flood fill an area with a pattern
	int minx = x, maxx = x, ended = 0, newx = 0;

	PUT_PIXEL24( x, y )
	pat_mem[ x + y*mem_width ] = 1;
	while ( ended == 0 )				// Search left for target pixels
	{
		minx--;
		if ( minx < 0 ) ended = 1;
		else
		{
			if ( !png_cmp( get_pixel24( minx, y ), target24) )
				pat_mem[ minx + y*mem_width ] = 1;
			else ended = 1;
		}
	}
	minx++;

	ended = 0;
	while ( ended == 0 )				// Search right for target pixels
	{
		maxx++;
		if ( maxx >= mem_width ) ended = 1;
		else
		{
			if ( !png_cmp( get_pixel24( maxx, y ), target24) )
				pat_mem[ maxx + y*mem_width ] = 1;
			else ended = 1;
		}
	}
	maxx--;

	if ( (y-1) >= 0 )				// Recurse upwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( png_cmp( get_pixel24(newx, y-1), target24 ) == 0
				&& pat_mem[newx + mem_width*(y-1)] == 0 )
					flood_fill24_pat( newx, y-1, target24, pat_mem );

	if ( (y+1) < mem_height )			// Recurse downwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( png_cmp( get_pixel24(newx, y+1), target24 ) == 0
				&& pat_mem[newx + mem_width*(y+1)] == 0 )
					flood_fill24_pat( newx, y+1, target24, pat_mem );
}

void mem_paint_mask( unsigned char *pat_mem )		// Paint over image using mask
{
	int i, j = mem_width * mem_height;

	if ( mem_image_bpp == 1 )
		for ( i=0; i<j; i++ )
			if ( pat_mem[i] == 1 ) PUT_PIXEL( i % mem_width, i / mem_width );

	if ( mem_image_bpp == 3 )
		for ( i=0; i<j; i++ )
			if ( pat_mem[i] == 1 ) PUT_PIXEL24( i % mem_width, i / mem_width );
}

void f_rectangle( int x, int y, int w, int h )		// Draw a filled rectangle
{
	int i, j;

	if ( x<0 )
	{
		w = w + x;
		x = 0;
	}
	if ( y<0 )
	{
		h = h + y;
		y = 0;
	}
	if ( (x+w) > mem_width ) w = mem_width - x;
	if ( (y+h) > mem_height ) h = mem_height - y;

	for ( j=0; j<h; j++ )
	{
		if ( mem_image_bpp == 1 )
			for ( i=0; i<w; i++ ) PUT_PIXEL( x + i, y + j )
		if ( mem_image_bpp == 3 )
			for ( i=0; i<w; i++ ) PUT_PIXEL24( x + i, y + j )
	}
}

void f_circle( int x, int y, int r )				// Draw a filled circle
{
	float r2, r3, k;
	int i, j, rx, ry, r4;
	int ox = x - r/2, oy = y - r/2;

	for ( j=0; j<r; j++ )
	{
		if ( r < 3 ) r4 = r-1;
		else
		{
			if ( r>10 ) k = 0.3 + 0.4*((float) j)/(r-1);	// Better for larger
			else k = 0.1 + 0.8*((float) j)/(r-1);		// Better for smaller

			r2 = 2*(k+(float) j) / r - 1;

			r3 = sqrt( 1 - r2*r2);
			if ( r%2 == 1 ) r4 = 2*mt_round( (r-1) * r3 / 2 );
			else
			{
				r4 = mt_round( (r-1) * r3 );
				if ( r4 % 2 == 0 ) r4--;
			}
		}
		ry = oy + j;
		rx = ox + (r-r4)/2;
		if ( mem_image_bpp == 1 )
		{
			for ( i=0; i<=r4; i++)
			{
				IF_IN_RANGE( rx+i, ry ) PUT_PIXEL( rx+i, ry )
			}
		}
		if ( mem_image_bpp == 3 )
		{
			for ( i=0; i<=r4; i++)
			{
				IF_IN_RANGE( rx+i, ry ) PUT_PIXEL24( rx+i, ry )
			}
		}
	}
}

/*
 * This code uses midpoint ellipse algorithm modified for uncentered ellipses,
 * with floating-point arithmetics to prevent overflows. (C) Dmitry Groshev
 */
static int xc2, yc2;
static void put4pix(int dx, int dy)
{
	int x0, x1, y0, y1;

	x0 = (xc2 - dx) >> 1;
	x1 = (xc2 + dx) >> 1;
	y0 = (yc2 - dy) >> 1;
	y1 = (yc2 + dy) >> 1;
	if (mem_image_bpp == 1)
	{
		PUT_PIXEL(x0, y0);
		if (x0 != x1) PUT_PIXEL(x1, y0);
		if (y0 == y1) return;
		PUT_PIXEL(x0, y1);
		if (x0 != x1) PUT_PIXEL(x1, y1);
	}
	else
	{
		PUT_PIXEL24(x0, y0);
		if (x0 != x1) PUT_PIXEL24(x1, y0);
		if (y0 == y1) return;
		PUT_PIXEL24(x0, y1);
		if (x0 != x1) PUT_PIXEL24(x1, y1);
	}
}

void trace_ellipse(int w, int h, int *left, int *right)
{
	int dx, dy;
	double err, stx, sty, w2, h2;

	h2 = h * h;
	w2 = w * w;
	dx = w & 1;
	dy = h;
	stx = h2 * dx;
	sty = w2 * dy;
	err = h2 * (dx * 5 + 4) + w2 * (1 - h - h);

	while (1) /* Have to force first step */
	{
		if (left[dy >> 1] > dx) left[dy >> 1] = dx;
		if (right[dy >> 1] < dx) right[dy >> 1] = dx;
		if (err >= 0.0)
		{
			dy -= 2;
			sty -= w2 + w2;
			err -= 4.0 * sty;
		}
		dx += 2;
		stx += h2 + h2;
		err += 4.0 * (h2 + stx);
		if ((dy < 2) || (stx >= sty)) break;
	}

	err += 3.0 * (w2 - h2) - 2.0 * (stx + sty);

	while (dy > 1)
	{
		if (left[dy >> 1] > dx) left[dy >> 1] = dx;
		if (right[dy >> 1] < dx) right[dy >> 1] = dx;
		if (err < 0.0)
		{
			dx += 2;
			stx += h2 + h2;
			err += 4.0 * stx;
		}
		dy -= 2;
		sty -= w2 + w2;
		err += 4.0 * (w2 - sty);
	}

	if (left[dy >> 1] > w) left[dy >> 1] = w;
	if (right[dy >> 1] < w) right[dy >> 1] = w;

	/* For too-flat ellipses */
	if (left[(dy >> 1) + 1] > dx) left[(dy >> 1) + 1] = dx;
	if (right[(dy >> 1) + 1] < w - 2) right[(dy >> 1) + 1] = w - 2;
}

void wjellipse(int xs, int ys, int w, int h, int type, int thick)
{
	int i, j, k, *left, *right;

	/* Prepare */
	yc2 = --h + ys + ys;
	xc2 = --w + xs + xs;
	k = type ? w + 1 : w & 1;
	j = h / 2 + 1;
	left = malloc(2 * j * sizeof(int));
	if (!left) return;
	right = left + j;
	for (i = 0; i < j; i++)
	{
		left[i] = k;
		right[i] = 0;
	}

	/* Plot outer */
	trace_ellipse(w, h, left, right);

	/* Plot inner */
	if (type && (thick > 1))
	{
		/* Determine possible height */
		thick += thick - 2;
		for (i = h; i >= 0; i -= 2)
		{
			if (left[i >> 1] > thick + 1) break;
		}
		i = i >= h - thick ? h - thick : i + 2;

		/* Determine possible width */
		j = left[thick >> 1];
		if (j > w - thick) j = w - thick;
		if (j < 2) i = h & 1;

		/* Do the plotting */
		for (k = i >> 1; k <= h >> 1; k++) left[k] = w & 1;
		if (i > 1) trace_ellipse(j, i, left, right);
	}

	/* Draw result */
	for (i = h & 1; i <= h; i += 2)
	{
		for (j = left[i >> 1]; j <= right[i >> 1]; j += 2)
		{
			put4pix(j, i);
		}
	}

	free(left);
}

void mem_ellipse( int x1, int y1, int x2, int y2, int thick, int type )		// 0=filled, 1=outline
{
	int xs, ys, xl, yl;

	mtMIN( xs, x1, x2 )
	mtMIN( ys, y1, y2 )
	xl = abs( x2 - x1 ) + 1;
	yl = abs( y2 - y1 ) + 1;

	if ( xl < 2 || yl < 2 )
	{
		f_rectangle( xs, ys, xl, yl );		// Too small so draw rectangle instead
		return;
	}

	wjellipse(xs, ys, xl, yl, type, thick);
}

void o_ellipse( int x1, int y1, int x2, int y2, int thick )	// Draw an ellipse outline
{
	if ( thick*2 > abs(x1 - x2) || thick*2 > abs(y1 - y2) )
		mem_ellipse( x1, y1, x2, y2, thick, 0 );	// Too thick so draw filled ellipse
	else
		mem_ellipse( x1, y1, x2, y2, thick, 1 );
}

void f_ellipse( int x1, int y1, int x2, int y2 )		// Draw a filled ellipse
{
	mem_ellipse( x1, y1, x2, y2, 0, 0 );
}

int mt_round( float n )			// Round a float to nearest whole number
{
	if ( n < 0 ) return ( (int) (n - 0.49999) );
	else return ( (int) (n + 0.49999) );
}

int get_next_line(char *input, int length, FILE *fp)
{
	char *st;

	st = fgets(input, length, fp);
	if ( st==NULL ) return -1;

	return 0;
}


int lo_case( int c )				// Convert character to lower case
{
	int r = c;

	if ( c>= 'A' && c<='Z' ) r = c - 'A' + 'a';

	return r;
}

int check_str( int max, char *a, char *b )	// Compare up to max characters of 2 strings
						// Case insensitive
{
	int ca, cb, i;

	i=0;
	while ( i<max )
	{
		ca = lo_case( a[i] );
		cb = lo_case( b[i] );
		if ( ca != cb ) return 0;	// Different char found
		if ( a[i] == 0 ) return 1;	// End of both strings - identical
		i++;
	}

	return 1;			// Same
}

char get_hex( int in )				// Turn 0..15 into hex
{
	char tab[] = "0123456789ABCDEF";

	if ( in<0 || in>15 ) return 'X';

	return tab[in];
}

int read_hex( char in )			// Convert character to hex value 0..15.  -1=error
{
	int res = -1;

	if ( in >= '0' && in <='9' ) res = in - '0';
	if ( in >= 'a' && in <='f' ) res = in - 'a' + 10;
	if ( in >= 'A' && in <='F' ) res = in - 'A' + 10;

	return res;
}

int read_hex_dub( char *in )		// Read hex double
{
	int hi, lo;

	hi = read_hex( in[0] );
	if ( hi < 0 ) return -1;
	lo = read_hex( in[1] );
	if ( lo < 0 ) return -1;

	return 16*hi + lo;
}

void clear_file_flags()			// Reset various file flags, e.g. XPM/XBM after new/load gif etc
{
	mem_xpm_trans = -1;
	mem_xbm_hot_x = -1;
	mem_xbm_hot_y = -1;
}

void mem_flip_v( char *mem, int w, int h, int bpp )
{
	unsigned char pix;
	int i, j, k, lim=4*PROGRESS_LIM;

	if ( w*h > lim ) progress_init(_("Flipping vertically"),0);
	for ( i=0; i<h/2; i++ )
	{
		if ( w*h > lim && i%16 == 0) progress_update( ((float) i)/(h/2) );
		if ( bpp == 1 )
			for ( j=0; j<w; j++ )
			{
				pix = mem[ j + w*i ];
				mem[ j + w*i ] = mem[ j + w*(h - i - 1) ];
				mem[ j + w*(h - i - 1) ] = pix;
			}
		if ( bpp == 3 )
			for ( j=0; j<w; j++ )
			{
				for ( k=0; k<3; k++ )
				{
					pix = mem[ k + 3*(j + w*i) ];
					mem[ k + 3*(j + w*i) ] = mem[ k + 3*(j + w*(h - i - 1)) ];
					mem[ k + 3*(j + w*(h - i - 1)) ] = pix;
				}
			}
	}
	if ( w*h > lim ) progress_end();
}

void mem_flip_h( char *mem, int w, int h, int bpp )
{
	unsigned char pix;
	int i, j, k, lim=4*PROGRESS_LIM;

	if ( w*h > lim ) progress_init(_("Flipping horizontally"),0);
	for ( i=0; i<w/2; i++ )
	{
		if ( w*h > lim && i%16 == 0) progress_update( ((float) i)/(w/2) );
		if ( bpp == 1 )
			for ( j=0; j<h; j++ )
			{
				pix = mem[ i + w*j ];
				mem[ i + w*j ] = mem[ w - 1 - i + w*j ];
				mem[ w - 1 - i + w*j ] = pix;
			}
		if ( bpp == 3 )
			for ( j=0; j<h; j++ )
			{
				for ( k=0; k<3; k++ )
				{
					pix = mem[ k + 3*(i + w*j) ];
					mem[ k + 3*(i + w*j) ] = mem[ k + 3*(w - 1 - i + w*j) ];
					mem[ k + 3*(w - 1 - i + w*j) ] = pix;
				}
			}
	}
	if ( w*h > lim ) progress_end();
}

void mem_bacteria( int val )			// Apply bacteria effect val times the canvas area
{						// Ode to 1994 and my Acorn A3000
	int i, j, k, x, y, w = mem_width-2, h = mem_height-2, tot = w*h, np, cancel = 0;
	unsigned int pixy;

	while ( tot > PROGRESS_LIM )	// Ensure the user gets a regular opportunity to cancel
	{
		tot = tot / 2;
		val = val * 2;
	}

	if ( (w*h*val) > PROGRESS_LIM )
		progress_init(_("Bacteria Effect"),1);
	for ( i=0; i<val; i++ )
	{
		if ( (w*h*val) > PROGRESS_LIM )
			cancel = progress_update( ((float) i)/val );
		if ( cancel == 1 ) goto stop;

		for ( j=0; j<tot; j++ )
		{
			x = 1 + rand() % w;
			y = 1 + rand() % h;
			pixy = 0;
			for ( k=0; k<9; k++ )
				pixy = pixy + mem_image[ x + k%3 - 1 +
					mem_width*(y + k/3 - 1) ];
			np = (mt_round( ((float) pixy) / 9 ) + 1) % mem_cols;
			mem_image[ x + mem_width*y ] = (unsigned char) np;
		}
	}
stop:
	if ( (w*h*val) > PROGRESS_LIM )
		progress_end();
}

void mem_rotate( char *new, char *old, int old_w, int old_h, int dir, int bpp )
{
	unsigned char pix;
	int i, j, k, lim=PROGRESS_LIM*4;

	if ( old_w*old_h > lim ) progress_init(_("Rotating"),1);
	if ( dir == 0 )				// Clockwise
	{
		for ( j=0; j<old_h; j++ )
		{
			if ( old_w*old_h > lim && j%16 == 0 )
				progress_update( ((float) j)/old_h );
			if ( bpp == 1 )
				for ( i=0; i<old_w; i++ )
				{
					pix = old[ i + j * old_w ];
					new[ (old_h - 1 - j) + old_h*i ] = pix;
				}
			if ( bpp == 3 )
				for ( i=0; i<old_w; i++ )
				{
					for ( k = 0; k<3; k++ )
					{
						pix = old[ k + 3*(i + j * old_w) ];
						new[ k + 3*((old_h - 1 - j) + old_h*i) ] = pix;
					}
				}
		}
	}
	else					// Anti-Clockwise
	{
		for ( j=0; j<old_h; j++ )
		{
			if ( old_w*old_h > lim && j%16 == 0 )
				progress_update( ((float) j)/old_h );
			if ( bpp == 1 )
				for ( i=0; i<old_w; i++ )
				{
					pix = old[ i + j * old_w ];
					new[ j + old_h*(old_w - 1 - i) ] = pix;
				}
			if ( bpp == 3 )
				for ( i=0; i<old_w; i++ )
				{
					for ( k = 0; k<3; k++ )
					{
						pix = old[ k + 3*(i + j * old_w) ];
						new[ k + 3*(j + old_h*(old_w - 1 - i)) ] = pix;
					}
				}
		}
	}
	if ( old_w*old_h > lim ) progress_end();
}

int mem_sel_rot( int dir )					// Rotate clipboard 90 degrees
{
	char *new_clipboard = NULL, *new_mask;
	int i;

	new_clipboard = malloc( mem_clip_w * mem_clip_h * mem_clip_bpp );
				// Grab new memory chunk

	if ( new_clipboard == NULL ) return 1;			// Not enough memory

	mem_rotate( new_clipboard, mem_clipboard, mem_clip_w, mem_clip_h, dir, mem_clip_bpp );
	if ( mem_clip_mask != NULL )
	{
		new_mask = malloc( mem_clip_w * mem_clip_h );
		if ( new_mask == NULL )
		{
			free( new_clipboard );
			return 1;
		}
		mem_rotate( new_mask, mem_clip_mask, mem_clip_w, mem_clip_h, dir, 1 );
		free( mem_clip_mask );
		mem_clip_mask = new_mask;
	}

	i = mem_clip_w;
	mem_clip_w = mem_clip_h;		// Flip geometry
	mem_clip_h = i;

	free( mem_clipboard );			// Free old clipboard
	mem_clipboard = new_clipboard;		// Put in new address

	return 0;
}

void spinco( float angle, float x, float y, float cx, float cy, float *res_x, float *res_y )
{		// Spin x/y around centre by angle (radians) and put results into res_x/y
	*res_x = cx + (x - cx) * sin(angle + M_PI/2) + (y - cy) * sin(angle);
	*res_y = cy + (x - cx) * cos(angle + M_PI/2) + (y - cy) * cos(angle);
}

float get_smla( float a, float b, float c, float d, int type )		// Get smallest/largest
{
	float res;

	if ( type == 0 )
	{
		mtMIN( res, a, b )
		mtMIN( res, res, c )
		mtMIN( res, res, d )
	}
	else
	{
		mtMAX( res, a, b )
		mtMAX( res, res, c )
		mtMAX( res, res, d )
	}

	return res;
}

int mem_rotate_free( float angle, int type )	// Rotate canvas by any angle (degrees)
{
	unsigned char *old_image = mem_image, pix[4][3];
	int ow = mem_width, oh = mem_height, nw, nh, res;
	int nx, ny, ox, oy, ki1, ki2, ki3, i, xa, ya;
	float centre[2][2];			// Centre - 0=old, 1=new
	float corner[4][2];			// 4 corners of new image
	float rangle = M_PI*angle/180;		// Radians
	float maxx, minx, maxy, miny;
	float s1, s2, c1, c2;			// Trig values
	float k1, k2, k3, k4;			// Quick look up values
	float sfact[2], fox, foy, pfact[4];	// Smoothing factors for X/Y

	s1 = sin(rangle + M_PI/2);
	s2 = sin(rangle);
	c1 = cos(rangle + M_PI/2);
	c2 = cos(rangle);

	centre[0][0] = ow;
	centre[0][1] = oh;
	centre[0][0] /= 2;
	centre[0][1] /= 2;

	spinco( rangle, 0, 0, centre[0][0], centre[0][1], &corner[0][0], &corner[0][1] );
	spinco( rangle, ow, 0, centre[0][0], centre[0][1], &corner[1][0], &corner[1][1] );
	spinco( rangle, ow, oh, centre[0][0], centre[0][1], &corner[2][0], &corner[2][1] );
	spinco( rangle, 0, oh, centre[0][0], centre[0][1], &corner[3][0], &corner[3][1] );

	minx = get_smla( corner[0][0], corner[1][0], corner[2][0], corner[3][0], 0 );
	maxx = get_smla( corner[0][0], corner[1][0], corner[2][0], corner[3][0], 1 );
	miny = get_smla( corner[0][1], corner[1][1], corner[2][1], corner[3][1], 0 );
	maxy = get_smla( corner[0][1], corner[1][1], corner[2][1], corner[3][1], 1 );

	nw = mt_round(maxx - minx + 0.99);
	nh = mt_round(maxy - miny + 0.99);

	if ( nw>MAX_WIDTH || nh>MAX_HEIGHT ) return -5;		// If new image is too big return -5

	centre[1][0] = nw;
	centre[1][1] = nh;
	centre[1][0] /= 2;
	centre[1][1] /= 2;

	pen_down = 0;				// Ensure next tool action is treated separately
	res = undo_next_core( 2, nw, nh, 0, 0, mem_image_bpp );
	pen_down = 0;
	if ( res == 1 ) return 2;		// No undo space

	k3 = (-centre[1][0])*s1 + centre[0][0] + (-centre[1][1])*s2;
	k4 = (-centre[1][0])*c1 + centre[0][1] + (-centre[1][1])*c2;

	progress_init(_("Free Rotation"),0);
	for ( ny=0; ny<nh; ny++ )
	{
		k1 = ny*s2 + k3;
		k2 = ny*c2 + k4;
		ki1 = ny*nw;
		if ( ny%16 == 0 ) progress_update( ((float) ny)/nh );
		if ( type == 0 )	// Non-Smooth Indexed/RGB
		{
			if ( mem_image_bpp == 1 )
				for ( nx=0; nx<nw; nx++ )
				{
					ox = nx*s1 + k1;
					oy = nx*c1 + k2;

					if ( ox<0 || ox>=ow || oy<0 || oy>=oh )
						mem_image[nx + ki1] = mem_col_A;
					else
						mem_image[nx + ki1] = old_image[ox + oy*ow];
				}
			if ( mem_image_bpp == 3 )
				for ( nx=0; nx<nw; nx++ )
				{
					ox = nx*s1 + k1;
					oy = nx*c1 + k2;
					ki3 = 3*(nx + ki1);
					if ( ox<0 || ox>=ow || oy<0 || oy>=oh )
					{
						mem_image[ki3] = mem_col_A24.red;
						mem_image[1 + ki3] = mem_col_A24.green;
						mem_image[2 + ki3] = mem_col_A24.blue;
					}
					else
					{
						ki2 = 3*(ox + oy*ow);
						mem_image[ki3] = old_image[ki2];
						mem_image[1 + ki3] = old_image[1 + ki2];
						mem_image[2 + ki3] = old_image[2 + ki2];
					}
				}
		}
		if ( type == 1 )	// Smooth RGB
		{
			for ( nx=0; nx<nw; nx++ )
			{
				ki3 = 3*(nx + ki1);
				fox = nx*s1 + k1;
				foy = nx*c1 + k2;

				if (fox>=0) ox = fox;
				else ox = fox-1;
				if (foy>=0) oy = foy;
				else oy = foy-1;

				sfact[0] = fox - ox;
				sfact[1] = foy - oy;

				pfact[0] = (1-sfact[0]) * (1-sfact[1]);
				pfact[1] = sfact[0] * (1-sfact[1]);
				pfact[2] = (1-sfact[0]) * sfact[1];
				pfact[3] = sfact[0] * sfact[1];

				for ( i=0; i<4; i++ )	// Get main pixels
				{
					xa = ox + i%2;
					ya = oy + i/2;
					if ( xa<0 || xa>=ow || ya<0 || ya>=oh )
					{
						pix[i][0] = mem_col_A24.red;
						pix[i][1] = mem_col_A24.green;
						pix[i][2] = mem_col_A24.blue;
					}
					else
					{
						ki2 = 3*(xa + ya*ow);
						pix[i][0] = old_image[ki2];
						pix[i][1] = old_image[1 + ki2];
						pix[i][2] = old_image[2 + ki2];
					}
				}

				for ( i=0; i<3; i++ )
					mem_image[i + ki3] = mt_round
						(
						pfact[0] * pix[0][i] +
						pfact[1] * pix[1][i] +
						pfact[2] * pix[2][i] +
						pfact[3] * pix[3][i]
						);
			}
		}
	}
	progress_end();

	return 0;
}

int mem_image_rot( int dir )					// Rotate image 90 degrees
{
	char *new_image = NULL;
	int i, j, ow = mem_width, oh = mem_height;

	new_image = malloc( mem_width * mem_height * mem_image_bpp );
						// Grab new memory chunk
	if ( new_image == NULL ) return 1;			// Not enough memory

	mem_rotate( new_image, mem_image, mem_width, mem_height, dir, mem_image_bpp );

	pen_down = 0;				// Ensure next tool action is treated separately
	undo_next_core( 2, mem_height, mem_width, 0, 0, mem_image_bpp );
	pen_down = 0;				// Ensure next tool action is treated separately

	mem_width = oh;
	mem_height = ow;
	j = mem_height * mem_width * mem_image_bpp;
	for ( i=0; i<j; i++)			// Copy rotated data to new mem_image
		mem_image[i] = new_image[i];

	free( new_image );			// Free memory

	return 0;
}



///	Code for scaling contributed by Dmitry Groshev, January 2006

typedef struct {
	int idx;
	float k;
} fstep;


fstep *make_filter(int l0, int l1, int type)
{
	fstep *res, *buf;
	double Aarray[4] = {-0.5, -2.0 / 3.0, -0.75, -1.0};
	double x, y, basept, fwidth, delta, scale = (double)l1 / (double)l0;
	double A = 0.0, kk = 1.0, sum;
	int pic_tile = FALSE; /* Allow to enable tiling mode later */
	int i, j, k;


	/* To correct scale-shift */
	delta = 0.5 / scale - 0.5;

	if (type == 0) type = -1;
	if (scale < 1.0)
	{
		kk = scale;
		if (type == 1) type = 0;
	}

	switch (type)
	{
	case 0: fwidth = 1.0 + scale; /* Area-mapping */
		break;
	case 1: fwidth = 2.0; /* Bilinear */
		break;
	case 2:	case 3: case 4: case 5:	/* Bicubic, all flavors */
		fwidth = 4.0;
		A = Aarray[type - 2];
		break;
	case 6: case 7:		/* Windowed sinc, all flavors */
		fwidth = 6.0;
		break;
	default:	 /* Bug */
		fwidth = 0.0;
		break;
	}
	fwidth /= kk;

	i = (int)floor(fwidth) + 2;
	res = buf = calloc(l1 * (i + 1), sizeof(fstep));

	for (i = 0; i < l1; i++)
	{
		basept = (double)i / scale + delta;
		k = (int)floor(basept + fwidth / 2.0);
		for (j = (int)ceil(basept - fwidth / 2.0); j <= k; j++)
		{
			if (j < 0) buf->idx = pic_tile ? l0 + j : -j;
			else if (j < l0) buf->idx = j;
			else buf->idx = pic_tile ? j - l0 : 2 * (l0 - 1) - j;
			x = fabs(((double)j - basept) * kk);
			y = 0;
			switch (type)
			{
			case 0: /* Area mapping */
				if (x <= 0.5 - scale / 2.0) y = 1.0;
				else y = 0.5 - (x - 0.5) / scale;
				break;
			case 1: /* Bilinear */
				y = 1.0 - x;
				break;
			case 2: case 3: case 4: case 5: /* Bicubic */
				if (x < 1.0) y = ((A + 2.0) * x - (A + 3)) * x * x + 1.0;
				else y = A * (((x - 5.0) * x + 8.0) * x - 4.0);
				break;
			case 6: /* Lanczos3 */
				if (x < 1e-7) y = 1.0;
				else y = sin(M_PI * x) * sin((M_PI / 3.0) * x) /
					((M_PI * M_PI / 3.0) * x * x);
				break;
			case 7: /* Blackman-Harris */
				if (x < 1e-7) y = 1.0;
				else y = (sin(M_PI * x) / (M_PI * x)) * (0.42323 +
					0.49755 * cos(x * (M_PI * 2.0 / 6.0)) +
					0.07922 * cos(x * (M_PI * 4.0 / 6.0)));
				break;
			default: /* Bug */
				break;
			}
			buf->k = y * kk;
			if (buf->k != 0.0) buf++;
		}
		buf->idx = -1;
		buf++;
	}
	(buf - 1)->idx = -2;

	/* Normalization pass. Damn the filters that require it. */
	sum = 0.0;
	for (buf = res, i = 0; ; i++)
	{
		if (buf[i].idx >= 0) sum += buf[i].k;
		else
		{
			if ((sum != 0.0) && (sum != 1.0))
			{
				sum = 1.0 / sum;
				for (j = 0; j < i; j++)
					buf[j].k *= sum;
			}
			if (buf[i].idx < -1) break;
			sum = 0.0; buf += i + 1; i = -1;
		}
	}

	return (res);
}

float *work_area;
fstep *hfilter, *vfilter;

#define N_CHANNELS 3
int prepare_scale(int nw, int nh, int type)
{
	work_area = NULL;
	hfilter = vfilter = NULL;
	work_area = malloc(mem_width * N_CHANNELS * sizeof(float));
	hfilter = make_filter(mem_width, nw, type);
	vfilter = make_filter(mem_height, nh, type);
	if (!work_area || !hfilter || !vfilter)
	{
		free(work_area);
		free(hfilter);
		free(vfilter);
		return FALSE;
	}
	else return TRUE;
}

void do_scale(char *new_image, int nw, int nh)
{
	unsigned char *img;
	fstep *tmp, *tmpx;
	float *wrk;
	double sum[N_CHANNELS], kk;
	int i, j, n;

	/* For each destination line */
	tmp = vfilter;
	for (i = 0; i < nh; i++, tmp++)
	{
		memset(work_area, 0, mem_width * N_CHANNELS * sizeof(float));

		/* Build one vertically-scaled row */
		for (; tmp->idx >= 0; tmp++)
		{
			img = mem_image + tmp->idx * mem_width * N_CHANNELS;
			wrk = work_area;
			kk = tmp->k;
			for (j = 0; j < mem_width; j++)
			{
		/* WARNING: this for N_CHANNELS == 3 !!! */
				wrk[0] += kk * img[0];
				wrk[1] += kk * img[1];
				wrk[2] += kk * img[2];
				wrk += N_CHANNELS;
				img += N_CHANNELS;
			}
		}

		/* Scale it horizontally */
		img = new_image + i * nw * N_CHANNELS;
		/* WARNING: this for N_CHANNELS == 3 !!! */
		sum[0] = sum[1] = sum[2] = 0.0;
		for (tmpx = hfilter; ; tmpx++)
		{
			if (tmpx->idx >= 0)
			{
				kk = tmpx->k;
				wrk = work_area + tmpx->idx * N_CHANNELS;
		/* WARNING: this for N_CHANNELS == 3 !!! */
				sum[0] += kk * wrk[0];
				sum[1] += kk * wrk[1];
				sum[2] += kk * wrk[2];
			}
			else
			{
				for (n = 0; n < N_CHANNELS; n++)
				{
					j = (int)rint(sum[n]);
					*img++ = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
					sum[n] = 0.0;
				}
				if (tmpx->idx < -1) break;
			}
		}

		if ((i * 10) % nh >= nh - 10) progress_update((float)(i + 1) / nh);
		if (tmp->idx < -1) break;
	}

	free(hfilter);
	free(vfilter);
	free(work_area);
}

int mem_image_scale( int nw, int nh, int type )				// Scale image
{
	char *new_image = NULL;
	int i, j, oi, oj, res;

	mtMIN( nw, nw, MAX_WIDTH )
	mtMAX( nw, nw, 1 )
	mtMIN( nh, nh, MAX_HEIGHT )
	mtMAX( nh, nh, 1 )

	new_image = malloc( nw * nh * mem_image_bpp );		// Grab new memory chunk
	if ( new_image == NULL ) return 1;			// Not enough memory
	if (type && (mem_image_bpp == 3))
	{
		if (!prepare_scale(nw, nh, type)) return 1;	// Not enough memory
	}

	progress_init(_("Scaling Image"),0);
	if ( mem_image_bpp == 1 )
		for ( j=0; j<nh; j++ )
		{
			progress_update( ((float) j)/nh );
			oj = mem_height * ((float) j)/nh;
			for ( i=0; i<nw; i++ )
			{
				oi = mem_width * ((float) i)/nw;
				new_image[ i + nw*j ] = mem_image[ oi + mem_width*oj ];
			}
		}
	else if ( mem_image_bpp == 3 )
	{
		if (type == 0)
		{
		    for ( j=0; j<nh; j++ )
		    {
			progress_update( ((float) j)/nh );
			oj = mem_height * ((float) j)/nh;
			for ( i=0; i<nw; i++ )
			{
				oi = mem_width * ((float) i)/nw;
				new_image[ 3*(i + nw*j) ] = mem_image[ 3*(oi + mem_width*oj) ];
				new_image[ 1 + 3*(i + nw*j) ] = mem_image[ 1 + 3*(oi + mem_width*oj) ];
				new_image[ 2 + 3*(i + nw*j) ] = mem_image[ 2 + 3*(oi + mem_width*oj) ];
			}
		    }
		}
		else do_scale(new_image, nw, nh);
	}
	progress_end();

	pen_down = 0;				// Ensure next tool action is treated separately
	res = undo_next_core( 2, nw, nh, 0, 0, mem_image_bpp );
	pen_down = 0;
	if ( res == 1 )
	{
		free( new_image );		// Free memory
		return 2;
	}

	j = mem_height * mem_width * mem_image_bpp;
	for ( i=0; i<j; i++)			// Copy scaled data to new mem_image
		mem_image[i] = new_image[i];

	free( new_image );			// Free memory

	return 0;
}

int mem_isometrics(int type)
{
	int i, j, ow = mem_width, oh = mem_height, offset;

	if ( type<2 )
	{
		if ( (oh + (ow-1)/2) > MAX_HEIGHT ) return -666;
		i = mem_image_resize( ow, oh + (ow-1)/2, 0, 0 );
	}
	if ( type>1 )
	{
		if ( (ow+oh-1) > MAX_WIDTH ) return -666;
		i = mem_image_resize( ow + oh - 1, oh, 0, 0 );
	}

	if ( i<0 ) return i;

	if ( type < 2 )			// Left/Right side down
	{
		for ( i=2*type; i<(ow-2*(1-type)); i++ )
		{
			if ( type == 1 ) offset = i/2;
			else offset = (ow-1-i)/2;
			if ( mem_image_bpp == 1 )
			{
			 for ( j=(oh-1+offset); j>=offset; j-- )
			  mem_image[ i + j*mem_width ] = mem_image[ i + (j-offset)*mem_width ];
			 for ( j=0; j<offset; j++ )
			  mem_image[ i + j*mem_width ] = mem_col_A;
			}
			else
			{
			 for ( j=(oh-1+offset); j>=offset; j-- )
			 {
			  mem_image[ 3*(i+j*mem_width) ] = mem_image[ 3*(i+(j-offset)*mem_width) ];
			  mem_image[ 1+3*(i+j*mem_width) ] = mem_image[ 1+3*(i+(j-offset)*mem_width) ];
			  mem_image[ 2+3*(i+j*mem_width) ] = mem_image[ 2+3*(i+(j-offset)*mem_width) ];
			 }
			 for ( j=0; j<offset; j++ )
			 {
			  mem_image[ 3*(i+j*mem_width) ] = mem_col_A24.red;
			  mem_image[ 1+3*(i+j*mem_width) ] = mem_col_A24.green;
			  mem_image[ 2+3*(i+j*mem_width) ] = mem_col_A24.blue;
			 }
			}
		}
	}
	else				// Top/Bottom side right
	{
		for ( j=(type-2); j<(oh-(3-type)); j++ )
		{
			if ( type == 2 ) offset = oh-1-j;
			else offset = j;
			if ( mem_image_bpp == 1 )
			{
			 for ( i=(ow-1+offset); i>=offset; i-- )
			  mem_image[i + mem_width*j] = mem_image[i - offset + mem_width*j];
			 for ( i=0; i<offset; i++ )
			  mem_image[i + mem_width*j] = mem_col_A;
			}
			else
			{
			 for ( i=(ow-1+offset); i>=offset; i-- )
			 {
			  mem_image[3*(i+mem_width*j)] = mem_image[3*(i-offset+mem_width*j)];
			  mem_image[1+3*(i+mem_width*j)] = mem_image[1+3*(i-offset+mem_width*j)];
			  mem_image[2+3*(i+mem_width*j)] = mem_image[2+3*(i-offset+mem_width*j)];
			 }
			 for ( i=0; i<offset; i++ )
			 {
			  mem_image[3*(i + mem_width*j)] = mem_col_A24.red;
			  mem_image[1 + 3*(i + mem_width*j)] = mem_col_A24.green;
			  mem_image[2 + 3*(i + mem_width*j)] = mem_col_A24.blue;
			 }
			}
		}
	}

	return 0;
}

int mem_image_resize( int nw, int nh, int ox, int oy )		// Scale image
{
	char *new_image = NULL;
	int i, j, oxo = 0, oyo = 0, nxo = 0, nyo = 0, ow, oh, res;

	mtMIN( nw, nw, MAX_WIDTH )
	mtMAX( nw, nw, 1 )
	mtMIN( nh, nh, MAX_HEIGHT )
	mtMAX( nh, nh, 1 )

	j = nw * nh * mem_image_bpp;
	new_image = malloc( j );		// Grab new memory chunk
	if ( new_image == NULL ) return 1;			// Not enough memory

	if ( mem_image_bpp == 1 )
		for ( i=0; i<j; i++ ) new_image[i] = mem_col_A;
	if ( mem_image_bpp == 3 )
		for ( i=0; i<j; i=i+3 )				// Background is current colour A
		{
			new_image[ i ] = mem_col_A24.red;
			new_image[ 1 + i ] = mem_col_A24.green;
			new_image[ 2 + i ] = mem_col_A24.blue;
		}

	if ( ox < 0 ) oxo = -ox;
	else nxo = ox;
	if ( oy < 0 ) oyo = -oy;
	else nyo = oy;

	mtMIN( ow, mem_width, nw )
	mtMIN( oh, mem_height, nh )

	if ( mem_image_bpp == 1 )
		for ( j=0; j<oh; j++ )
			for ( i=0; i<ow; i++ )
				new_image[ i + nxo + nw*(j + nyo) ] =
					mem_image[ i + oxo + mem_width*(j + oyo) ];
	if ( mem_image_bpp == 3 )
		for ( j=0; j<oh; j++ )
			for ( i=0; i<ow; i++ )
			{
				new_image[ 3*(i + nxo + nw*(j + nyo)) ] =
					mem_image[ 3*(i + oxo + mem_width*(j + oyo)) ];
				new_image[ 1 + 3*(i + nxo + nw*(j + nyo)) ] =
					mem_image[ 1 + 3*(i + oxo + mem_width*(j + oyo)) ];
				new_image[ 2 + 3*(i + nxo + nw*(j + nyo)) ] =
					mem_image[ 2 + 3*(i + oxo + mem_width*(j + oyo)) ];
			}

	pen_down = 0;				// Ensure next tool action is treated separately
	res = undo_next_core( 2, nw, nh, 0, 0, mem_image_bpp );
	pen_down = 0;
	if ( res == 1 )
	{
		free( new_image );		// Free memory
		return 2;
	}

	j = mem_height * mem_width * mem_image_bpp;
	for ( i=0; i<j; i++)			// Copy rotated data to new mem_image
		mem_image[i] = new_image[i];

	free( new_image );			// Free memory

	return 0;
}

png_color get_pixel24( int x, int y )				// RGB version
{
	png_color pix = {
			mem_image[ 3*(x + mem_width*y) ],
			mem_image[ 1 + 3*(x + mem_width*y) ],
			mem_image[ 2 + 3*(x + mem_width*y) ]
			};

	return pix;
}

int mem_protected_RGB(int intcol)		// Is this intcol in list?
{
	int i;

	if ( mem_prot==0 ) return 0;
	for ( i=0; i<mem_prot; i++ ) if ( intcol == mem_prot_RGB[i] ) return 1;

	return 0;
}

void put_pixel( int x, int y )				// paletted version
{
	unsigned char *old_image, newc;
	int offset = x + mem_width*y;

	if (mem_prot_mask[mem_image[x + (y)*mem_width]] == 0)
	{
		newc = mem_col_pat[((x) % 8) + 8*((y) % 8)];
		if (tint_mode[0])
		{
			if ( mem_undo_opacity ) old_image = mem_undo_previous();
			else old_image = mem_image;

			if ( tint_mode[2] == 1 || (tint_mode[2] == 0 && tint_mode[1] == 0) )
				newc = old_image[offset] > mem_cols - 1 - newc ? mem_cols-1 : old_image[offset] + newc;
			else
				newc = old_image[offset] > newc ? old_image[offset] - newc : 0;

		}
		mem_image[offset] = newc;
	}
}
void put_pixel24( int x, int y )				// RGB version
{
	unsigned char *old_image = NULL, r, g, b, nr, ng, nb;
	int offset = 3*(x + mem_width*y), curpix;

	if ( mem_prot>0 )		// Have any pixel colours been protected?
	{
		curpix = MEM_2_INT(mem_image, offset);
		if ( mem_protected_RGB(curpix) ) return; // Bailout if we are on a protected pixel
	}

	nr = mem_col_pat24[ 3*(((x) % 8) + 8*((y) % 8)) ];
	ng = mem_col_pat24[ 1 + 3*(((x) % 8) + 8*((y) % 8)) ];
	nb = mem_col_pat24[ 2 + 3*(((x) % 8) + 8*((y) % 8)) ];

	if ( mem_undo_opacity ) old_image = mem_undo_previous();
	else old_image = mem_image;

	if (tint_mode[0])
	{
		if ( tint_mode[2] == 1 || (tint_mode[2] == 0 && tint_mode[1] == 0) )
		{
			nr = old_image[offset] > 255 - nr ? 255 : old_image[offset] + nr;
			ng = old_image[1 + offset] > 255 - ng ? 255 : old_image[1 + offset] + ng;
			nb = old_image[2 + offset] > 255 - nb ? 255 : old_image[2 + offset] + nb;
		}
		else
		{
			nr = old_image[offset] > nr ? old_image[offset] - nr : 0;
			ng = old_image[1 + offset] > ng ? old_image[1 + offset] - ng : 0;
			nb = old_image[2 + offset] > nb ? old_image[2 + offset] - nb : 0;
		}
	}

	if ( tool_opacity == 100 )
	{
		mem_image[ offset ] = nr;
		mem_image[ 1 + offset ] = ng;
		mem_image[ 2 + offset ] = nb;
	}
	else
	{
		r = old_image[ offset ];
		g = old_image[ 1 + offset ];
		b = old_image[ 2 + offset ];

		mem_image[offset] = ( nr*tool_opacity +	r*(100-tool_opacity) ) / 100;
		mem_image[1 + offset] = ( ng*tool_opacity + g*(100-tool_opacity) ) / 100;
		mem_image[2 + offset] = ( nb*tool_opacity + b*(100-tool_opacity) ) / 100;
	}
}

int png_cmp( png_color a, png_color b )			// Compare 2 colours
{
	if ( a.red == b.red && a.green == b.green && a.blue == b.blue ) return 0;
	else return -1;
			// Return TRUE if different
}

int mem_count_all_cols()				// Count all colours - Using main image
{
	return mem_count_all_cols_real(mem_image, mem_width, mem_height);
}

int mem_count_all_cols_real(unsigned char *im, int w, int h)	// Count all colours - very memory greedy
{
	unsigned char *tab, c;
	int i, j, k, o;

	tab = malloc( 256*256*32 );			// HUGE colour cube
	if ( tab == NULL ) return -1;			// Not enough memory Mr Greedy ;-)

	j = 256*256*32;
	for ( i=0; i<j; i++ ) tab[i] = 0;		// Flush table

	j = w*h;
	for ( i=0; i<j; i++ )				// Scan each pixel
	{
		o = im[0] + 256*im[1] + 256*256*( im[2] >> 3 );
		c = tab[o];
		c = c | ( 1 << (im[2] % 8) );
		tab[o] = c;

		im += 3;
	}

	j = 256*256*32;
	k = 0;
	for ( i=0; i<j; i++ )		// Count each colour
	{
		k = k + ((tab[i]>>0) % 2) + ((tab[i]>>1) % 2) + ((tab[i]>>2) % 2) + ((tab[i]>>3) % 2) +
		  ((tab[i]>>4) % 2) + ((tab[i]>>5) % 2) + ((tab[i]>>6) % 2) + ((tab[i]>>7) % 2);
	}

	free(tab);

	return k;
}

int mem_cols_used(int max_count)			// Count colours used in main RGB image
{
	if ( mem_image_bpp == 1 ) return -1;			// RGB only

	return mem_cols_used_real(mem_image, mem_width, mem_height, max_count, 1);
}

void mem_cols_found_dl(unsigned char userpal[3][256])		// Convert results ready for DL code
{
	int i, j;

	for ( i=0; i<256; i++ )
		for ( j=0; j<3; j++ )
			userpal[j][i] = found[i][j];
}

int mem_cols_used_real(unsigned char *im, int w, int h, int max_count, int prog)
			// Count colours used in RGB chunk
{
	int i = 3, j = w*h*3, res = 1, k, f;


	found[0][0] = im[0];
	found[0][1] = im[1];
	found[0][2] = im[2];
	if ( prog == 1 ) progress_init(_("Counting Unique RGB Pixels"),0);
	while ( i<j && res<max_count )				// Skim all pixels
	{
		k = 0;
		f = 0;
		while ( k<res && f==0 )
		{
			if (	im[i]   == found[k][0] &&
				im[i+1] == found[k][1] &&
				im[i+2] == found[k][2]
				) f = 1;
			k++;
		}
		if ( f == 0 )					// New colour so add to list
		{
			found[res][0] = im[i];
			found[res][1] = im[i+1];
			found[res][2] = im[i+2];
			res++;
			if ( res % 16 == 0 && prog == 1 )
				if ( progress_update( ((float) res)/1024 ) ) goto stop;
		}
		i = i + 3;
	}
stop:
	if ( prog == 1 ) progress_end();

	return res;
}


////	EFFECTS

void do_effect( int type, int param )		// 0=edge detect 1=blur 2=emboss
{
	unsigned char *rgb, pix[3];
	int offset, pixels = mem_width*mem_height*3, i, j, k, l=0, diffs[4][3];
	float blur = ((float) param) / 200, b2, b3;

	rgb = grab_memory( pixels, 0 );
	if (rgb == NULL) return;

	if ( type != 1 ) progress_init(_("Applying Effect"),1);
	for ( j=1; j<(mem_height-1); j++ )
	{
		if ( j%16 == 0 && type != 1 ) if (progress_update( ((float) j)/(mem_height) )) goto stop;
		for ( i=1; i<(mem_width-1); i++ )
		{
			offset = 3*(i + j*mem_width);
			for ( k=0; k<3; k++ )
			{
				pix[k] = mem_image[k + offset];
				diffs[0][k] = mem_image[k + offset - 3*mem_width];
				diffs[1][k] = mem_image[k + offset - 3];
				diffs[2][k] = mem_image[k + offset + 3];
				diffs[3][k] = mem_image[k + offset + 3*mem_width];
				if ( type==0 )	// Edge detect
				{
					diffs[0][k] = abs( pix[k] - diffs[0][k] );
					diffs[1][k] = abs( pix[k] - diffs[1][k] );
					diffs[2][k] = abs( pix[k] - diffs[2][k] );
					diffs[3][k] = abs( pix[k] - diffs[3][k] );
					l = diffs[0][k] + diffs[1][k] + diffs[2][k] + diffs[3][k];
				}
				if ( type==1 )	// Blur
				{
					b2 = diffs[0][k] + diffs[1][k] + diffs[2][k] + diffs[3][k];
					b2 = b2 / 4;
					b3 = pix[k];
					l = 0.4999 + b2*blur + b3*(1-blur);
				}
				if ( type==2 )	// Emboss
				{
					diffs[2][k] = mem_image[k + offset - 3 - 3*mem_width];
					diffs[3][k] = mem_image[k + offset + 3 - 3*mem_width];
					l = (diffs[0][k] + diffs[1][k] + diffs[2][k] + diffs[3][k])/4;
					l = l - pix[k] + 127;
				}
				if ( type==3 )	// Edge sharpen
				{
					l = diffs[0][k] + diffs[1][k] + diffs[2][k] + diffs[3][k];
					l = l - 4*pix[k];
					l = pix[k] - blur*l;
				}
				if ( type==4 )	// Edge soften
				{
					l = diffs[0][k] + diffs[1][k] + diffs[2][k] + diffs[3][k];
					l = l - 4*pix[k];
					l = pix[k] + 5*((float) l)/(125 - param);
				}
				mtMIN( l, l, 255 )
				mtMAX( l, l, 0 )
				rgb[k + offset] = l;
			}
		}
	}
stop:
	if ( type != 1 ) progress_end();
	if ( type == 1 || type == 3 || type == 4 )		// Reinstate border pixels
	{
		offset = 3*mem_width*(mem_height-1);
		for ( i=0; i<mem_width; i++ )
		{
			for ( k=0; k<3; k++ )
			{
				rgb[k + 3*i] = mem_image[k + 3*i];			// Top
				rgb[k + 3*i + offset ] = mem_image[k + 3*i + offset];	// Bottom
			}
		}
		offset = 3*mem_width;
		for ( j=0; j<mem_height; j++ )
		{
			for ( k=0; k<3; k++ )
			{
				rgb[k + j*offset] = mem_image[k + j*offset];			// Left
				rgb[k + (j+1)*offset - 3 ] = mem_image[k + (j+1)*offset - 3];	// Right
			}
		}
	}

	for ( i=0; i<pixels; i++ ) mem_image[i] = rgb[i];

	free(rgb);
}


///	CLIPBOARD MASK

int mem_clip_mask_init(unsigned char val)		// Initialise the clipboard mask
{
	int i, j = mem_clip_w*mem_clip_h;

	if ( mem_clipboard != NULL ) mem_clip_mask_clear();	// Remove old mask

	mem_clip_mask = malloc(j);
	if ( mem_clip_mask == NULL ) return 1;			// Not alble to allocate memory

	for ( i=0; i<j; i++ ) mem_clip_mask[i] = val;		// Start with fully opaque/clear mask

	return 0;
}

void mem_clip_mask_set(unsigned char val)		// (un)Mask colours A and B on the clipboard
{
	int i, j = mem_clip_w*mem_clip_h;

	if ( mem_clip_bpp == 1 )
	{
		for ( i=0; i<j; i++ )
		{
			if ( mem_clipboard[i] == mem_col_A || mem_clipboard[i] == mem_col_B )
				mem_clip_mask[i] = val;
		}
	}
	if ( mem_clip_bpp == 3 )
	{
		for ( i=0; i<j; i++ )
		{
			if (	mem_clipboard[3*i] == mem_col_A24.red &&
				mem_clipboard[1+3*i] == mem_col_A24.green &&
				mem_clipboard[2+3*i] == mem_col_A24.blue )
					mem_clip_mask[i] = val;
			if (	mem_clipboard[3*i] == mem_col_B24.red &&
				mem_clipboard[1+3*i] == mem_col_B24.green &&
				mem_clipboard[2+3*i] == mem_col_B24.blue )
					mem_clip_mask[i] = val;
		}
	}
}

void mem_clip_mask_clear()		// Clear/remove the clipboard mask
{
	if ( mem_clip_mask != NULL )
	{
		free(mem_clip_mask);
		mem_clip_mask = NULL;
	}
}

int mem_clip_scale_alpha()	// Extract alpha information from RGB clipboard - alpha if pixel is in scale of A->B. Result 0=ok 1=problem
{
	int i, ii, j, k, AA[3], BB[3], CC[3], chan, ok;
	float p;

	AA[0] = mem_col_A24.red;
	AA[1] = mem_col_A24.green;
	AA[2] = mem_col_A24.blue;
	BB[0] = mem_col_B24.red;
	BB[1] = mem_col_B24.green;
	BB[2] = mem_col_B24.blue;

	chan = 0;	// Find the channel with the widest range - gives most accurate result later
	if ( abs(AA[1] - BB[1]) > abs(AA[0] - BB[0]) ) chan = 1;
	if ( abs(AA[2] - BB[2]) > abs(AA[chan] - BB[chan]) ) chan = 2;
	if ( (AA[chan] - BB[chan]) == 0 ) return 1;	// A == B so bail out - nothing to do

	if ( mem_clipboard == NULL || mem_clip_bpp != 3 ) return 1;

	if ( mem_clip_mask == NULL ) mem_clip_mask_init(0);	// Create new alpha memory if needed
	if ( mem_clip_mask == NULL ) return 1;			// Bail out if not available

	ii = 0;
	j = mem_clip_w * mem_clip_h * 3;
	for ( i=0; i<j; i+=3 )
	{
		ok = TRUE;		// Ensure pixel lies between A and B for each channel
		for ( k=0; k<3; k++ )
		{
			if ( mem_clipboard[i+k] < AA[k] && mem_clipboard[i+k] < BB[k] ) ok = FALSE;
			if ( mem_clipboard[i+k] > AA[k] && mem_clipboard[i+k] > BB[k] ) ok = FALSE;
		}
		if ( mem_clip_mask[ii] > 0 ) ok = FALSE;	// Already semi-opaque so don't touch

		if ( ok )
		{
			p = ((float) (mem_clipboard[i+chan] - AA[chan])) / (BB[chan] - AA[chan]);

				// Check delta for all channels is roughly the same ...
				// ... if it isn't, ignore this pixel as its not in A->B scale
			for ( k=0; k<3; k++ )
			{
				CC[k] = 0.5 + (1-p)*AA[k] + p*BB[k];
				if ( abs(CC[k] - mem_clipboard[i+k]) > 2 ) ok = FALSE;
			}

			if ( ok )	// Pixel is a shade of A/B so set alpha & clipboard values
			{
				mem_clipboard[i]   = AA[0];
				mem_clipboard[i+1] = AA[1];
				mem_clipboard[i+2] = AA[2];
				mem_clip_mask[ii] = 0.5 + p*255;
			}
		}
		ii++;
	}

	return 0;
}


void mem_smudge(int ox, int oy, int nx, int ny)		// Smudge from old to new @ tool_size, RGB only
{
	unsigned char *rgb;
	int ax = ox - tool_size/2, ay = oy - tool_size/2, w = tool_size, h = tool_size;
	int xv = nx - ox, yv = ny - oy;		// Vector
	int i, j, k, rx, ry, pixy, offs;

	if ( ax<0 )		// Ensure original area is within image
	{
		w = w + ax;
		ax = 0;
	}
	if ( ay<0 )
	{
		h = h + ay;
		ay = 0;
	}
	if ( (ax+w)>mem_width )
		w = mem_width - ax;
	if ( (ay+h)>mem_height )
		h = mem_height - ay;

	rgb = malloc( w*h*3 );
	for ( j=0; j<h; j++ )		// Grab old area of canvas
	{
		ry = ay + j;
		for ( i=0; i<w; i++ )
		{
			rx = ax + i;
			for ( k=0; k<3; k++ )
			{
				rgb[ k + 3*(i + w*j) ] = mem_image[ k + 3*(rx + mem_width*ry) ];
			}
		}
	}

	for ( j=0; j<h; j++ )		// Blend old area with new area
	{
		ry = ay + yv + j;
		if (ry>=0 && ry<mem_height) for ( i=0; i<w; i++ )
		{
			rx = ax + xv + i;
			if (rx>=0 && rx<mem_width)
			{
				offs = 3*(rx + mem_width*ry);
				pixy = MEM_2_INT(mem_image, offs);

				if ( !mem_protected_RGB(pixy) )
				{
					for ( k=0; k<3; k++ )
					{
						mem_image[ k + offs ] = (rgb[ k + 3*(i + w*j) ] +
							mem_image[ k + offs ]) / 2;
					}
				}
			}
		}
	}
	free(rgb);
}

void mem_clone(int ox, int oy, int nx, int ny)		// Clone from old to new @ tool_size
{
	unsigned char *rgb,
			*orgb = mem_image;		// Used for <100% opacity
	int ax = ox - tool_size/2, ay = oy - tool_size/2, w = tool_size, h = tool_size;
	int xv = nx - ox, yv = ny - oy;		// Vector
	int i, j, k, rx, ry, pixy, offs;
	int opac = mt_round( ((float) tool_opacity) / 100 * 255), opac2 = 255 - opac;

	if ( mem_image_bpp == 3 && mem_undo_opacity )
		orgb = mem_undo_previous();

	if ( ax<0 )		// Ensure original area is within image
	{
		w = w + ax;
		ax = 0;
	}
	if ( ay<0 )
	{
		h = h + ay;
		ay = 0;
	}
	if ( (ax+w)>mem_width )
		w = mem_width - ax;
	if ( (ay+h)>mem_height )
		h = mem_height - ay;

	if ( w<1 || h<1 )
	{
		return;
	}
//printf("w=%i h=%i x=%i y=%i\n",w,h,ox,oy);

	rgb = malloc( w*h*mem_image_bpp );
	for ( j=0; j<h; j++ )		// Grab old area of canvas
	{
		ry = ay + j;
		if ( mem_image_bpp == 1 )
		{
			for ( i=0; i<w; i++ )
			{
				rx = ax + i;
				rgb[ i + w*j ] = mem_image[ rx + mem_width*ry ];
			}
		}
		if ( mem_image_bpp == 3 )
		{
			for ( i=0; i<w; i++ )
			{
				rx = ax + i;
				for ( k=0; k<3; k++ )
				{
					rgb[ k + 3*(i + w*j) ] =
						orgb[ k + 3*(rx + mem_width*ry) ];
				}
			}
		}
	}

	for ( j=0; j<h; j++ )		// Blend old area with new area
	{
		ry = ay + yv + j;
		if (ry>=0 && ry<mem_height && mem_image_bpp==1) for ( i=0; i<w; i++ )
		{
			rx = ax + xv + i;
			if (rx>=0 && rx<mem_width)
			{
				offs = rx + mem_width*ry;
				if ( mem_prot_mask[mem_image[offs]] == 0 )
					mem_image[offs] = rgb[ i + w*j ];
			}
		}
		if (ry>=0 && ry<mem_height && mem_image_bpp==3) for ( i=0; i<w; i++ )
		{
			rx = ax + xv + i;
			if (rx>=0 && rx<mem_width)
			{
				offs = 3*(rx + mem_width*ry);
				pixy = MEM_2_INT(mem_image, offs);

				if ( !mem_protected_RGB(pixy) )
				{
					for ( k=0; k<3; k++ )
					{
						mem_image[ k + offs ] =(
							opac * rgb[ k + 3*(i + w*j) ] +
							opac2 * orgb[ k + offs ] + 128
							) / 255;
					}
				}
			}
		}
	}
	free(rgb);
}


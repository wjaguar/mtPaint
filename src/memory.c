/*	memory.c
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

#include <math.h>
#include <stdlib.h>
#include <string.h>
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
#include "channels.h"
#include "toolbar.h"
#include "viewer.h"
#include "csel.h"


char *channames[NUM_CHANNELS + 1];


/// Tint tool - contributed by Dmitry Groshev, January 2006

int tint_mode[3] = {0,0,0};		// [0] = off/on, [1] = add/subtract, [2] = button (none, left, middle, right : 0-3)

int mem_cselect;
int mem_unmask;

/// FLOOD FILL SETTINGS

double flood_step;
int flood_cube, flood_img, flood_slide;

int smudge_mode;

/// IMAGE

char mem_filename[256];			// File name of file loaded/saved
chanlist mem_img;			// Array of pointers to image channels
int mem_channel = CHN_IMAGE;		// Current active channel
int mem_img_bpp;			// Bytes per pixel = 1 or 3
int mem_changed;			// Changed since last load/save flag 0=no, 1=changed
int mem_width, mem_height;
float mem_icx = 0.5, mem_icy = 0.5;	// Current centre x,y
int mem_ics;				// Has the centre been set by the user? 0=no 1=yes
int mem_background = 180;		// Non paintable area

unsigned char *mem_clipboard;		// Pointer to clipboard data
unsigned char *mem_clip_mask;		// Pointer to clipboard mask
unsigned char *mem_clip_alpha;		// Pointer to clipboard alpha
unsigned char *mem_brushes;		// Preset brushes screen memory
int brush_tool_type = TOOL_SQUARE;	// Last brush tool type
char mem_clip_file[2][256];		// 0=Current filename, 1=temp filename
int mem_clip_bpp;			// Bytes per pixel
int mem_clip_w = -1, mem_clip_h = -1;	// Clipboard geometry
int mem_clip_x = -1, mem_clip_y = -1;	// Clipboard location on canvas
int mem_nudge = -1;			// Nudge pixels per SHIFT+Arrow key during selection/paste

int mem_preview;			// Preview an RGB change
int mem_prev_bcsp[6];			// BR, CO, SA, POSTERIZE, Hue

undo_item mem_undo_im[MAX_UNDO];	// Pointers to undo images + current image being edited

int mem_undo_pointer;		// Pointer to currently used image on canas/screen
int mem_undo_done;		// Undo images that we have behind current image (i.e. possible UNDO)
int mem_undo_redo;		// Undo images that we have ahead of current image (i.e. possible REDO)
int mem_undo_limit = 32;	// Max MB memory allocation limit
int mem_undo_opacity;		// Use previous image for opacity calculations?

/// GRID

int mem_show_grid, mem_grid_min;	// Boolean show toggle & minimum zoom to show it at
unsigned char mem_grid_rgb[3];		// RGB colour of grid

/// PATTERNS

unsigned char *mem_col_pat;		// Indexed 8x8 colourised pattern using colours A & B
unsigned char *mem_col_pat24;		// RGB 8x8 colourised pattern using colours A & B

/// PREVIEW/TOOLS

int tool_type = TOOL_SQUARE;		// Currently selected tool
int tool_size = 1, tool_flow = 1;
int tool_opacity = 255;			// Opacity - 255 = solid
int tool_pat;				// Tool pattern number
int tool_fixx = -1, tool_fixy = -1;	// Fixate on axis
int pen_down;				// Are we drawing? - Used to see if we need to do an UNDO
int tool_ox, tool_oy;			// Previous tool coords - used by continuous mode
int mem_continuous;			// Area we painting the static shapes continuously?

int mem_brcosa_allow[3];		// BRCOSA RGB



/// FILE

int mem_xpm_trans = -1;			// Current XPM file transparency colour index
int mem_xbm_hot_x=-1, mem_xbm_hot_y=-1;	// Current XBM hot spot
int mem_jpeg_quality = 85;		// JPEG quality setting

/// PALETTE

png_color mem_pal[256];			// RGB entries for all 256 palette colours
int mem_cols;				// Number of colours in the palette: 2..256 or 0 for no image
int mem_col_A = 1, mem_col_B = 0;	// Index for colour A & B
png_color mem_col_A24, mem_col_B24;	// RGB for colour A & B
char *mem_pals = NULL;			// RGB screen memory holding current palette
static unsigned char found[1024 * 3];	// Used by mem_cols_used() & mem_convert_indexed
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

void undo_free_x(undo_item *undo)
{
	int i;

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!undo->img[i]) continue;
		if (undo->img[i] != (void *)(-1)) free(undo->img[i]);
		undo->img[i] = NULL;
	}
}

void undo_free(int idx)
{
	undo_free_x(&mem_undo_im_[idx]);
}

void mem_clear()
{
	int i;

	for (i = 0; i < MAX_UNDO; i++)		// Release old UNDO images
		undo_free(i);
	memset(mem_img, 0, sizeof(chanlist));	// Already freed along with UNDO
}

/* Allocate space for new image, removing old if needed */
int mem_new( int width, int height, int bpp, int cmask )
{
	unsigned char *res;
	undo_item *undo = &mem_undo_im_[0];
	int i, j = width * height;

	mem_clear();

	res = mem_img[CHN_IMAGE] = malloc(j * bpp);
	for (i = CHN_ALPHA; res && (cmask > CMASK_FOR(i)); i++)
	{
		if (!(cmask & CMASK_FOR(i))) continue;
		res = mem_img[i] = malloc(j);
	}
	if (!res)	// Not enough memory
	{
		for (i = 0; i < NUM_CHANNELS; i++)
			free(mem_img[i]);
		memset(mem_img, 0, sizeof(chanlist));
		width = height = 8; // 8x8 is bound to work!
		j = width * height;
		mem_img[CHN_IMAGE] = malloc(j * bpp);
	}

	i = 0;
#ifdef U_GUADALINEX
	if ( bpp == 3 ) i = 255;
#endif
	memset(mem_img[CHN_IMAGE], i, j * bpp);
	for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
		if (mem_img[i]) memset(mem_img[i], channel_fill[i], j);

	mem_width = width;
	mem_height = height;
	mem_img_bpp = bpp;
	mem_channel = CHN_IMAGE;

	mem_undo_pointer = 0;
	mem_undo_done = 0;
	mem_undo_redo = 0;

	memcpy(undo->img, mem_img, sizeof(chanlist));
	undo->cols = mem_cols;
	undo->bpp = mem_img_bpp;
	undo->width = width;
	undo->height = height;
	mem_pal_copy(undo->pal, mem_pal);

	mem_col_A = 1;
	mem_col_B = 0;
	mem_col_A24 = mem_pal[mem_col_A];
	mem_col_B24 = mem_pal[mem_col_B];
	memset(channel_col_A, 255, NUM_CHANNELS);

	clear_file_flags();

	return (!res);
}

/* Get address of previous channel data (or current if none) */
unsigned char *mem_undo_previous(int channel)
{
	unsigned char *res;
	int i;

	i = mem_undo_pointer ? mem_undo_pointer - 1 : MAX_UNDO - 1;
	res = mem_undo_im_[i].img[channel];
	if (!res || (res == (void *)(-1)))
		res = mem_img[channel];	// No undo so use current
	return (res);
}

void lose_oldest()				// Lose the oldest undo image
{						// Pre-requisite: mem_undo_done > 0
	undo_free((mem_undo_pointer - mem_undo_done + MAX_UNDO) % MAX_UNDO);
	mem_undo_done--;
}

/* Mode bits are: |1 - force create, |2 - forbid copy, |4 - force delete,
 * |8 - respect pen_down */
int undo_next_core(int mode, int new_width, int new_height, int new_bpp, int cmask)
{
	undo_item *undo;
	unsigned char *img;
	chanlist holder;
	int i, j, k, mem_req, mem_lim;

	notify_changed();
	if (pen_down && (mode & 8)) return (0);
	pen_down = mode & 8 ? 1 : 0;
//printf("Old undo # = %i\n", mem_undo_pointer);

	/* Release redo data */
	if (mem_undo_redo)
	{
		k = mem_undo_pointer;
		for (i = 0; i < mem_undo_redo; i++)
		{
			k = (k + 1) % MAX_UNDO;
			undo_free(k);
		}
		mem_undo_redo = 0;
	}

	mem_req = 0;
	if (cmask && !(mode & 4))
	{
		for (i = j = 0; i < NUM_CHANNELS; i++)
		{
			if (mem_img[i] && (cmask & (1 << i))) j++;
		}
		if (cmask & CMASK_IMAGE) j += new_bpp - 1;
		mem_req = (new_width * new_height + 32) * j;
	}
	mem_lim = (mem_undo_limit * (1024 * 1024)) / (layers_total + 1);

	/* Mem limit exceeded - drop oldest */
	while (mem_used() + mem_req > mem_lim)
	{
		/* Fail if not enough memory */
		if (!mem_undo_done) return (1);
		lose_oldest();
	}

	/* Fill undo frame */
	undo = &mem_undo_im_[mem_undo_pointer];
	for (i = 0; i < NUM_CHANNELS; i++)
	{
		img = mem_img[i];
		if (img && !(cmask & (1 << i))) img = (void *)(-1);
		undo->img[i] = img;
	}
	undo->cols = mem_cols;
	mem_pal_copy(undo->pal, mem_pal);
	undo->width = mem_width;
	undo->height = mem_height;
	undo->bpp = mem_img_bpp;

	/* Duplicate affected channels */
	mem_req = new_width * new_height;
	for (i = 0; i < NUM_CHANNELS; i++)
	{
		holder[i] = img = mem_img[i];
		if (!(cmask & (1 << i))) continue;
		if (mode & 4)
		{
			holder[i] = NULL;
			continue;
		}
		if (!img && !(mode & 1)) continue;
		mem_lim = mem_req;
		if (i == CHN_IMAGE) mem_lim *= new_bpp;
		while (!((img = malloc(mem_lim))))
		{
			if (!mem_undo_done)
			{
#if 0
				if (handle == 0)
				{
					printf("No memory for undo!\n");
					exit(1);
				}
#endif
				/* Release memory and fail */
				for (j = 0; j < i; j++)
				{
					if (holder[j] != mem_img[j])
						free(holder[j]);
				}
				return (2);
			}
			lose_oldest();
		}
		holder[i] = img;
		/* Copy */
		if (!undo->img[i] || (mode & 2)) continue;
		memcpy(img, undo->img[i], mem_lim);
	}

	/* Commit */
	memcpy(mem_img, holder, sizeof(chanlist));
	mem_width = new_width;
	mem_height = new_height;
	mem_img_bpp = new_bpp;
		
	if (mem_undo_done >= MAX_UNDO - 1)
		undo_free((mem_undo_pointer + 1) % MAX_UNDO);
	else mtMIN(mem_undo_done, mem_undo_done + 1, MAX_UNDO - 1);

	mem_undo_pointer = (mem_undo_pointer + 1) % MAX_UNDO;	// New pointer
	undo = &mem_undo_im_[mem_undo_pointer];
	memcpy(undo->img, holder, sizeof(chanlist));
	undo->cols = mem_cols;
	mem_pal_copy(undo->pal, mem_pal);
	undo->width = mem_width;
	undo->height = mem_height;
	undo->bpp = mem_img_bpp;
//printf("New undo # = %i\n\n", mem_undo_pointer);

	return (0);
}

// Call this after a draw event but before any changes to image
int mem_undo_next(int mode)
{
	int cmask = CMASK_ALL, wmode = 0;

	switch (mode)
	{
	case UNDO_PAL: /* Palette changes */
		cmask = CMASK_NONE;
		break;
	case UNDO_XPAL: /* Palette and indexed image changes */
		cmask = mem_img_bpp == 1 ? CMASK_IMAGE : CMASK_NONE;
		break;
	case UNDO_COL: /* Palette and/or RGB image changes */
		cmask = mem_img_bpp == 3 ? CMASK_IMAGE : CMASK_NONE;
		break;
	case UNDO_TOOL: /* Continuous drawing */
		wmode = 8;
	case UNDO_DRAW: /* Changes to current channel / RGBA */
		cmask = (mem_channel == CHN_IMAGE) && RGBA_mode ?
			CMASK_RGBA : CMASK_CURR;
		break;
	case UNDO_INV: /* "Invert" operation */
		if ((mem_channel == CHN_IMAGE) && (mem_img_bpp == 1))
			cmask = CMASK_NONE;
		else cmask = CMASK_CURR;
		break;
	case UNDO_XFORM: /* Changes to all channels */
		cmask = CMASK_ALL;
		break;
	case UNDO_FILT: /* Changes to current channel */
		cmask = CMASK_CURR;
		break;
	case UNDO_PASTE: /* Paste to current channel / RGBA */
		wmode = 8;	/* !!! Workaround for move-with-RMB-pressed */
		cmask = (mem_channel == CHN_IMAGE) && !channel_dis[CHN_ALPHA] &&
			(mem_clip_alpha || RGBA_mode) ? CMASK_RGBA : CMASK_CURR;
		break;
	}
	return (undo_next_core(wmode, mem_width, mem_height, mem_img_bpp, cmask));
}

void mem_undo_swap(int old, int new)
{
	undo_item *curr, *prev;
	int i;

	curr = &mem_undo_im_[old];
	prev = &mem_undo_im_[new];

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		curr->img[i] = mem_img[i];
		if (prev->img[i] == (void *)(-1)) curr->img[i] = (void *)(-1);
		else mem_img[i] = prev->img[i];
	}

	curr->width = mem_width;
	curr->height = mem_height;
	curr->cols = mem_cols;
	curr->bpp = mem_img_bpp;
	mem_pal_copy(curr->pal, mem_pal);

	mem_width = prev->width;
	mem_height = prev->height;
	mem_cols = prev->cols;
	mem_img_bpp = prev->bpp;
	mem_pal_copy(mem_pal, prev->pal);

	if ( mem_col_A >= mem_cols ) mem_col_A = 0;
	if ( mem_col_B >= mem_cols ) mem_col_B = 0;
	if (!mem_img[mem_channel]) mem_channel = CHN_IMAGE;
}

void mem_undo_backward()		// UNDO requested by user
{
	int i;

	if ( mem_undo_done > 0 )
	{
//printf("UNDO!!! Old undo # = %i\n", mem_undo_pointer);
		i = mem_undo_pointer;
		mem_undo_pointer = (mem_undo_pointer - 1 + MAX_UNDO) % MAX_UNDO;	// New pointer
		mem_undo_swap(i, mem_undo_pointer);

		mem_undo_done--;
		mem_undo_redo++;
//printf("New undo # = %i\n\n", mem_undo_pointer);
	}
	pen_down = 0;
}

void mem_undo_forward()			// REDO requested by user
{
	int i;

	if ( mem_undo_redo > 0 )
	{
//printf("REDO!!! Old undo # = %i\n", mem_undo_pointer);
		i = mem_undo_pointer;
		mem_undo_pointer = (mem_undo_pointer + 1) % MAX_UNDO;		// New pointer
		mem_undo_swap(i, mem_undo_pointer);

		mem_undo_done++;
		mem_undo_redo--;
//printf("New undo # = %i\n\n", mem_undo_pointer);
	}
	pen_down = 0;
}

int mem_undo_size(undo_item *undo)
{
	int i, j, k, total = 0;

	for (i = 0; i < MAX_UNDO; i++)
	{
		k = undo->width * undo->height + 32;
		for (j = 0; j < NUM_CHANNELS; j++)
		{
			if (!undo->img[j] || (undo->img[j] == (void *)(-1)))
				continue;
			total += j == CHN_IMAGE ? k * undo->bpp : k;
		}
		undo++;
	}

	return total;
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

	chunk = malloc( size );
	
	if (chunk) memset(chunk, byte, size);

	return chunk;
}

void mem_init()					// Initialise memory
{
	unsigned char *dest;
	char txt[300], *cnames[NUM_CHANNELS + 1] =
		{ "", _("Alpha"), _("Selection"), _("Mask"), NULL };
	int i, j, lookup[8] = {0, 36, 73, 109, 146, 182, 219, 255}, ix, iy, bs, bf, bt;
	png_color temp_pal[256];


	for (i = 0; i < NUM_CHANNELS + 1; i++) channames[i] = cnames[i];

	toolbar_preview_init();

	mem_col_pat = grab_memory( 8*8, 0 );
	mem_col_pat24 = grab_memory( 3*8*8, 0 );
	mem_pals = grab_memory( 3*PALETTE_WIDTH*PALETTE_HEIGHT, 0 );
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

	if (mem_new(PATCH_WIDTH, PATCH_HEIGHT, 3, CMASK_IMAGE))	// Not enough memory!
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

	j = mem_width * mem_height;
	dest = mem_img[CHN_IMAGE];
	for (i = 0; i < j; i++)
	{
		*dest++ = mem_col_B24.red;
		*dest++ = mem_col_B24.green;
		*dest++ = mem_col_B24.blue;
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
		if ( bt == TOOL_SLASH ) for ( j=0; j<bs; j++ ) put_pixel( ix-bs/2+j, iy+bs/2-j );
		if ( bt == TOOL_BACKSLASH ) for ( j=0; j<bs; j++ ) put_pixel( ix+bs/2-j, iy+bs/2-j );
		if ( bt == TOOL_SPRAY )
			for ( j=0; j<bf*3; j++ )
				put_pixel( ix-bs/2 + rand() % bs, iy-bs/2 + rand() % bs );
	}

	j = 3*PATCH_WIDTH*PATCH_HEIGHT;
	memcpy(mem_brushes, mem_img[CHN_IMAGE], j);	// Store image for later use
	memset(mem_img[CHN_IMAGE], 0, j);	// Clear so user doesn't see it upon load fail

	mem_set_brush(36);		// Initial brush

	for ( i=0; i<NUM_CHANNELS; i++ )
	{
		for ( j=0; j<4; j++ )
		{
			sprintf(txt, "overlay%i%i", i, j);
			if ( j<3 )
			{
				channel_rgb[i][j] = inifile_get_gint32(txt, channel_rgb[i][j] );
			}
			else	channel_opacity[i] = inifile_get_gint32(txt, channel_opacity[i] );
		}
	}

	opaque_view = inifile_get_gboolean( "disableTransparency", FALSE );
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

static void copy_num( int index, int tx, int ty )
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

	if (mem_channel != CHN_IMAGE)
	{
		oc = channel_col_A[mem_channel];
		channel_col_A[mem_channel] = channel_col_B[mem_channel];
		channel_col_B[mem_channel] = oc;
		return;
	}

	oc = mem_col_A;
	mem_col_A = mem_col_B;
	mem_col_B = oc;

	o24 = mem_col_A24;
	mem_col_A24 = mem_col_B24;
	mem_col_B24 = o24;

	if (RGBA_mode)
	{
		oc = channel_col_A[CHN_ALPHA];
		channel_col_A[CHN_ALPHA] = channel_col_B[CHN_ALPHA];
		channel_col_B[CHN_ALPHA] = oc;
	}

	mem_pat_update();
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
		if (mem_prot_mask[i])
		{
			mem_prot_RGB[mem_prot] = PNG_2_INT( mem_pal[i] );
			mem_prot++;
		}
	}
}

void mem_mask_setall(char val)
{
	memset(mem_prot_mask, val, 256);
}

void mem_get_histogram(int channel)	// Calculate how many of each colour index is on the canvas
{
	int i, j = mem_width * mem_height;
	unsigned char *img = mem_img[channel];

	memset(mem_histogram, 0, sizeof(mem_histogram));

	for (i = 0; i < j; i++) mem_histogram[*img++]++;
}

static unsigned char gamma_table[256], bc_table[256];
static int last_gamma, last_br, last_co;

void do_transform(int start, int step, int cnt, unsigned char *mask,
	unsigned char *imgr, unsigned char *img0)
{
	static int ixx[7] = {0, 1, 2, 0, 1, 2, 0};
	static int posm[9] = {0, 0xFF00, 0x5500, 0x2480, 0x1100,
				 0x0840, 0x0410, 0x0204, 0};
	int do_gamma, do_bc, do_sa;
	double w;
	unsigned char rgb[3];
	int br, co, sa, ps, pmul;
	int dH, sH, tH, ix0, ix1, ix2, c0, c1, c2, dc = 0;
	int i, j, r, g, b, ofs3, opacity, op0 = 0, op1 = 0, op2 = 0, ops;

	cnt = start + step * cnt;

	if (!mem_brcosa_allow[0]) op0 = 255;
	if (!mem_brcosa_allow[1]) op1 = 255;
	if (!mem_brcosa_allow[2]) op2 = 255;
	ops = op0 + op1 + op2;

	br = mem_prev_bcsp[0] * 255;
	co = mem_prev_bcsp[1];
	if (co > 0) co *= 3;
	co += 100;
	co = (255 * co) / 100;
	sa = (255 * mem_prev_bcsp[2]) / 100;
	dH = sH = mem_prev_bcsp[5];
	ps = 8 - mem_prev_bcsp[3];
	pmul = posm[mem_prev_bcsp[3]];

	do_gamma = mem_prev_bcsp[4] - 100;
	do_bc = br | (co - 255);
	do_sa = sa - 255;

	/* Prepare gamma table */
	if (do_gamma && (do_gamma != last_gamma))
	{
		last_gamma = do_gamma;
		w = 100.0 / (double)mem_prev_bcsp[4];
		for (i = 0; i < 256; i++)
		{
			gamma_table[i] = rint(255.0 * pow((double)i / 255.0, w));
		}
	}
	/* Prepare brightness-contrast table */
	if (do_bc && ((br != last_br) || (co != last_co)))
	{
		last_br = br; last_co = co;
		for (i = 0; i < 256; i++)
		{
			j = ((i + i - 255) * co + (255 * 255)) / 2 + br;
			j = j < 0 ? 0 : j > (255 * 255) ? (255 * 255) : j;
			bc_table[i] = (j + (j >> 8) + 1) >> 8;
		}
	}
	if (dH)
	{
		if (dH < 0) dH += 1530;
		dc = (dH / 510) * 2; dH -= dc * 255;
		if ((sH = dH > 255))
		{
			dH = 510 - dH;
			dc = dc < 4 ? dc + 2 : 0;
		}
	}
	ix0 = ixx[dc]; ix1 = ixx[dc + 1]; ix2 = ixx[dc + 2];

	for (i = start; i < cnt; i += step)
	{
		ofs3 = i * 3;
		rgb[0] = img0[ofs3 + 0];
		rgb[1] = img0[ofs3 + 1];
		rgb[2] = img0[ofs3 + 2];
		opacity = mask[i];
		if (opacity == 255)
		{
			imgr[ofs3 + 0] = rgb[0];
			imgr[ofs3 + 1] = rgb[1];
			imgr[ofs3 + 2] = rgb[2];
			continue;
		}
		/* If we do gamma transform */
		if (do_gamma)
		{
			rgb[0] = gamma_table[rgb[0]];
			rgb[1] = gamma_table[rgb[1]];
			rgb[2] = gamma_table[rgb[2]];
		}
		/* If we do hue transform & colour has a hue */
		if (dH && ((rgb[0] ^ rgb[1]) | (rgb[0] ^ rgb[2])))
		{
			/* Min. component */
			c2 = dc;
			if (rgb[ix2] < rgb[ix0]) c2++;
			if (rgb[ixx[c2]] >= rgb[ixx[c2 + 1]]) c2++;
			/* Actual indices */
			c2 = ixx[c2];
			c0 = ixx[c2 + 1];
			c1 = ixx[c2 + 2];

			/* Max. component & edge dir */
			if ((tH = rgb[c0] <= rgb[c1]))
			{
				c0 = ixx[c2 + 2];
				c1 = ixx[c2 + 1];
			}
			/* Do adjustment */
			j = dH * (rgb[c0] - rgb[c2]) + 127; /* Round up (?) */
			j = (j + (j >> 8) + 1) >> 8;
			r = rgb[c0]; g = rgb[c1]; b = rgb[c2];
			if (tH ^ sH) /* Falling edge */
			{
				rgb[c1] = r = g > j + b ? g - j : b;
				rgb[c2] += j + r - g;
			}
			else /* Rising edge */
			{
				rgb[c1] = b = g < r - j ? g + j : r;
				rgb[c0] -= j + g - b;
			}
		}
		r = rgb[ix0];
		g = rgb[ix1];
		b = rgb[ix2];
		/* If we do brightness/contrast transform */
		if (do_bc)
		{
			r = bc_table[r];
			g = bc_table[g];
			b = bc_table[b];
		}
		/* If we do saturation transform */
		if (sa)
		{
			j = 0.299 * r + 0.587 * g + 0.114 * b;
			r = r * 255 + (r - j) * sa;
			r = r < 0 ? 0 : r > (255 * 255) ? (255 * 255) : r;
			r = (r + (r >> 8) + 1) >> 8;
			g = g * 255 + (g - j) * sa;
			g = g < 0 ? 0 : g > (255 * 255) ? (255 * 255) : g;
			g = (g + (g >> 8) + 1) >> 8;
			b = b * 255 + (b - j) * sa;
			b = b < 0 ? 0 : b > (255 * 255) ? (255 * 255) : b;
			b = (b + (b >> 8) + 1) >> 8;
		}
		/* If we do posterize transform */
		if (ps)
		{
			r = ((r >> ps) * pmul) >> 8;
			g = ((g >> ps) * pmul) >> 8;
			b = ((b >> ps) * pmul) >> 8;
		}
		/* If we do partial masking */
		if (ops || opacity)
		{
			r = r * 255 + (img0[ofs3 + 0] - r) * (opacity | op0);
			r = (r + (r >> 8) + 1) >> 8;
			g = g * 255 + (img0[ofs3 + 1] - g) * (opacity | op1);
			g = (g + (g >> 8) + 1) >> 8;
			b = b * 255 + (img0[ofs3 + 2] - b) * (opacity | op2);
			b = (b + (b >> 8) + 1) >> 8;
		}
		imgr[ofs3 + 0] = r;
		imgr[ofs3 + 1] = g;
		imgr[ofs3 + 2] = b;
	}
}

int do_posterize(int val, int posty)	// Posterize a number
{
	int res = val;
	POSTERIZE_MACRO
	return res;
}

unsigned char pal_dupes[256];

int scan_duplicates()			// Find duplicate palette colours, return number found
{
	int i, j, found = 0;

	if ( mem_cols < 3 ) return 0;

	for (i = mem_cols - 1; i > 0; i--)
	{
		pal_dupes[i] = i;			// Start with a clean sheet
		for (j = 0; j < i; j++)
		{
			if (	mem_pal[i].red == mem_pal[j].red &&
				mem_pal[i].green == mem_pal[j].green &&
				mem_pal[i].blue == mem_pal[j].blue )
			{
				found++;
				pal_dupes[i] = j;	// Point to first duplicate in the palette
				break;
			}
		}
	}

	return found;
}

void remove_duplicates()		// Remove duplicate palette colours - call AFTER scan_duplicates
{
	int i, j = mem_width * mem_height;
	unsigned char *img = mem_img[CHN_IMAGE];

	for (i = 0; i < j; i++)		// Scan canvas for duplicates
	{
		*img = pal_dupes[*img];
		img++;
	}
}

int mem_remove_unused_check()
{
	int i, found = 0;

	mem_get_histogram(CHN_IMAGE);
	for (i = 0; i < mem_cols; i++)
		if (!mem_histogram[i]) found++;

	if (!found) return 0;		// All palette colours are used on the canvas
	if (mem_cols - found < 2) return -1;	// Canvas is all one colour

	return found;
}

int mem_remove_unused()
{
	unsigned char conv[256], *img;
	int i, j, found = mem_remove_unused_check();

	if ( found <= 0 ) return found;

	for (i = j = 0; i < 256; i++)	// Create conversion table
	{
		if (mem_histogram[i])
		{
			mem_pal[j] = mem_pal[i];
			conv[i] = j++;
		}
	}

	j = mem_width * mem_height;
	img = mem_img[CHN_IMAGE];
	for (i = 0; i < j; i++)	// Convert canvas pixels as required
	{
		*img = conv[*img];
		img++;
	}

	mem_cols -= found;

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

void transform_pal(png_color *pal1, png_color *pal2, int p1, int p2)
{
	int i;
	unsigned char tmp[256 * 3], mask[256], *wrk;

	memset(mask, 0, ++p2 - p1);
	for (wrk = tmp , i = p1; i < p2; i++ , wrk += 3)
	{
		wrk[0] = pal2[i].red;
		wrk[1] = pal2[i].green;
		wrk[2] = pal2[i].blue;
	}

	do_transform(0, 1, p2 - p1, mask, tmp, tmp);

	for (wrk = tmp , i = p1; i < p2; i++ , wrk += 3)
	{
		pal1[i].red = wrk[0];
		pal1[i].green = wrk[1];
		pal1[i].blue = wrk[2];
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
	return mem_undo_size(mem_undo_im_);
}

int mem_used_layers()		// Return the number of bytes used in image + undo in all layers
{
	undo_item *undo;
	int l, total = 0;

	for (l = 0; l <= layers_total; l++)
	{
		if (l == layer_selected) undo = mem_undo_im_;
		else undo = layer_table[l].image->mem_undo_im_;
		total += mem_undo_size(undo);
	}

	return total;
}

int mem_convert_rgb()			// Convert image to RGB
{
	char *old_image = mem_img[CHN_IMAGE], *new_image;
	unsigned char pix;
	int i, j, res;

	res = undo_next_core(2, mem_width, mem_height, 3, CMASK_IMAGE);
	if (res) return 1;	// Not enough memory

	new_image = mem_img[CHN_IMAGE];
	j = mem_width * mem_height;
	for (i = 0; i < j; i++)
	{
		pix = *old_image++;
		*new_image++ = mem_pal[pix].red;
		*new_image++ = mem_pal[pix].green;
		*new_image++ = mem_pal[pix].blue;
	}

	return 0;
}

int mem_convert_indexed()	// Convert RGB image to Indexed Palette - call after mem_cols_used
{
	unsigned char *old_image, *new_image;
	int i, j, k, pix;

	old_image = mem_undo_previous(CHN_IMAGE);
	new_image = mem_img[CHN_IMAGE];
	j = mem_width * mem_height;
	for (i = 0; i < j; i++)
	{
		pix = MEM_2_INT(old_image, 0);
		for (k = 0; k < 256; k++)	// Find index of this RGB
		{
			if (MEM_2_INT(found, k * 3) == pix) break;
		}
		if (k > 255) return 1;		// No index found - BAD ERROR!!
		*new_image++ = k;
		old_image += 3;
	}

	for (i = 0; i < 256; i++)
	{
		mem_pal[i].red = found[i * 3];
		mem_pal[i].green = found[i * 3 + 1];
		mem_pal[i].blue = found[i * 3 + 2];
	}

	return 0;
}

/* Dithering works with 6-bit colours, because hardware VGA palette is 6-bit,
 * and any kind of dithering is imprecise by definition anyway - WJ */

typedef struct {
	double xyz256[768];
	int cspace, cdist, ncols;
	unsigned char cmap[64 * 64 * 64], xcmap[64 * 64 * 8];
} ctable;

static ctable *ctp;

static int lookup_srgb(double *srgb)
{
	int i, j, k, col[3];
	double d, td, td2, tmp[3];

	/* Convert to 6-bit RGB coords */
	col[0] = UNGAMMA64(srgb[0]);
	col[1] = UNGAMMA64(srgb[1]);
	col[2] = UNGAMMA64(srgb[2]);

	/* Use colour cache if possible */
	k = (col[0] << 12) + (col[1] << 6) + col[2];
	if (ctp->xcmap[k >> 3] & (1 << (k & 7))) return (ctp->cmap[k]);
	
	/* Prepare colour coords */
	switch (ctp->cspace)
	{
	default:
	case 0: /* RGB */
		tmp[0] = col[0] * (1.0 / 63.0);
		tmp[1] = col[1] * (1.0 / 63.0);
		tmp[2] = col[2] * (1.0 / 63.0);
		break;
	case 1: /* sRGB */
		tmp[0] = gamma64[col[0]];
		tmp[1] = gamma64[col[1]];
		tmp[2] = gamma64[col[2]];
		break;
	case 2: /* L*X*N* */
		rgb2LXN(tmp, gamma64[col[0]], gamma64[col[1]], gamma64[col[2]]);
		break;
	}

	/* Find nearest colour */
	d = 1000000000.0;
	for (i = j = 0; i < ctp->ncols; i++)
	{
		switch (ctp->cdist)
		{
		case 0: /* Largest absolute difference (Linf measure) */
			td = fabs(tmp[0] - ctp->xyz256[i * 3]);
			td2 = fabs(tmp[1] - ctp->xyz256[i * 3 + 1]);
			if (td < td2) td = td2;
			td2 = fabs(tmp[2] - ctp->xyz256[i * 3 + 2]);
			if (td < td2) td = td2;
			break;
		case 1: /* Sum of absolute differences (L1 measure) */
			td = fabs(tmp[0] - ctp->xyz256[i * 3]) +
				fabs(tmp[1] - ctp->xyz256[i * 3 + 1]) +
				fabs(tmp[2] - ctp->xyz256[i * 3 + 2]);
			break;
		default:
		case 2: /* Euclidean distance (L2 measure) */
			td = sqrt((tmp[0] - ctp->xyz256[i * 3]) *
				(tmp[0] - ctp->xyz256[i * 3]) +
				(tmp[1] - ctp->xyz256[i * 3 + 1]) *
				(tmp[1] - ctp->xyz256[i * 3 + 1]) +
				(tmp[2] - ctp->xyz256[i * 3 + 2]) *
				(tmp[2] - ctp->xyz256[i * 3 + 2]));
			break;
		}
		if (td >= d) continue;
		j = i; d = td;
	}

	/* Store & return result */
	ctp->xcmap[k >> 3] |= 1 << (k & 7);
	ctp->cmap[k] = j;
	return (j);
}

// !!! No support for transparency yet !!!
/* Damping functions roughly resemble old GIMP's behaviour, but may need some
 * tuning because linear sRGB is just too different from normal RGB */
int mem_dither(unsigned char *old, int ncols, short *dither, int cspace, int dist,
	int limit, int selc, int serpent, double emult)
{
	int i, j, k, l, kk, j0, j1, dj, rlen, col0, col1;
	unsigned char *ddata1, *ddata2, *src, *dest;
	double *row0, *row1, *row2, *tmp;
	double err, intd, extd, gamma6[256], lin6[256];
	double tc0[3], tc1[3], color0[3], color1[3];
	double fdiv = 0, gamut[6] = {1, 1, 1, 0, 0, 0};

	/* Allocate working space */
	rlen = (mem_width + 4) * 3;
	k = (rlen * 3 + 1) * sizeof(double);
	ddata1 = calloc(1, k);
	ddata2 = calloc(1, sizeof(ctable) + sizeof(double));
	if (!ddata1 || !ddata2)
	{
		free(ddata1);
		free(ddata2);
		return (1);
	}
	row0 = ALIGNTO(ddata1, double);
	row1 = row0 + rlen;
	row2 = row1 + rlen;
	ctp = ALIGNTO(ddata2, double);

	/* Prepare tables */
	for (i = 0; i < 256; i++)
	{
		j = 63 * i;
		j = (j + (j >> 8) + 0x80) >> 8;
		gamma6[i] = gamma64[j];
		lin6[i] = j * (1.0 / 63.0);
	}
	tmp = ctp->xyz256;
	for (i = 0; i < ncols; i++ , tmp += 3)
	{
		/* Update gamut limits */
		tmp[0] = gamma6[mem_pal[i].red];
		tmp[1] = gamma6[mem_pal[i].green];
		tmp[2] = gamma6[mem_pal[i].blue];
		for (j = 0; j < 3; j++)
		{
			if (tmp[j] < gamut[j]) gamut[j] = tmp[j];
			if (tmp[j] > gamut[j + 3]) gamut[j + 3] = tmp[j];
		}
		/* Store colour coords */
		switch (cspace)
		{
		default:
		case 0: /* RGB */
			tmp[0] = lin6[mem_pal[i].red];
			tmp[1] = lin6[mem_pal[i].green];
			tmp[2] = lin6[mem_pal[i].blue];
			break;
		case 1: /* sRGB - done already */
			break;
		case 2: /* L*X*N* */
			rgb2LXN(tmp, tmp[0], tmp[1], tmp[2]);
			break;
		}
	}
	ctp->cspace = cspace; ctp->cdist = dist; ctp->ncols = ncols;
	serpent = serpent ? 0 : 2;
	if (dither) fdiv = 1.0 / *dither++;

	/* Process image */
	for (i = 0; i < mem_height; i++)
	{
		src = old + i * mem_width * 3;
		dest = mem_img[CHN_IMAGE] + i * mem_width;
		memset(row2, 0, rlen * sizeof(double));
		if (serpent ^= 1)
		{
			j0 = 0; j1 = mem_width * 3; dj = 1;
		}
		else
		{
			j0 = (mem_width - 1) * 3; j1 = -3; dj = -1;
			dest += mem_width - 1;
		}
		for (j = j0; j != j1; j += dj * 3)
		{
			for (k = 0; k < 3; k++)
			{
				/* Posterize to 6 bits as natural for palette */
				color0[k] = gamma6[src[j + k]];
				/* Add in error, maybe limiting it */
				err = row0[j + k + 6];
				if (limit == 1) /* To half of SRGB range */
				{
					err = err < -0.5 ? -0.5 :
						err > 0.5 ? 0.5 : err;
				}
				else if (limit == 2) /* To 1/4, with damping */
				{
					err = err < -0.1 ? (err < -0.25 ?
						-0.25 : 0.5 * err - 0.05) :
						err > 0.1 ? (err > 0.25 ?
						0.25 : 0.5 * err + 0.05) : err;
				}
				color1[k] = color0[k] + err;
				/* Limit result to palette gamut */
				if (color1[k] < gamut[k]) color1[k] = gamut[k];
				if (color1[k] > gamut[k + 3]) color1[k] = gamut[k + 3];
			}
			/* Output best colour */
			col1 = lookup_srgb(color1);
			*dest = col1;
			dest += dj;
			if (!dither) continue;
			/* Evaluate new error */
			tc1[0] = gamma6[mem_pal[col1].red];
			tc1[1] = gamma6[mem_pal[col1].green];
			tc1[2] = gamma6[mem_pal[col1].blue];
			if (selc) /* Selective error damping */
			{
				col0 = lookup_srgb(color0);
				tc0[0] = gamma6[mem_pal[col0].red];
				tc0[1] = gamma6[mem_pal[col0].green];
				tc0[2] = gamma6[mem_pal[col0].blue];
				/* Split error the obvious way */
				if (!(selc & 1) && (col0 == col1))
				{
					color1[0] = (color1[0] - color0[0]) * emult +
						color0[0] - tc0[0];
					color1[1] = (color1[1] - color0[1]) * emult +
						color0[1] - tc0[1];
					color1[2] = (color1[2] - color0[2]) * emult +
						color0[2] - tc0[2];
				}
				/* Weigh component errors separately */
				else if (selc < 3)
				{
					for (k = 0; k < 3; k++)
					{
						intd = fabs(color0[k] - tc0[k]);
						extd = fabs(color0[k] - color1[k]);
						if (intd + extd == 0.0) err = 1.0;
						else err = (intd + emult * extd) / (intd + extd);
						color1[k] = err * (color1[k] - tc1[k]);
					}
				}
				/* Weigh errors by vector length */
				else
				{
					intd = sqrt((color0[0] - tc0[0]) * (color0[0] - tc0[0]) +
						(color0[1] - tc0[1]) * (color0[1] - tc0[1]) +
						(color0[2] - tc0[2]) * (color0[2] - tc0[2]));
					extd = sqrt((color0[0] - color1[0]) * (color0[0] - color1[0]) +
						(color0[1] - color1[1]) * (color0[1] - color1[1]) +
						(color0[2] - color1[2]) * (color0[2] - color1[2]));
					if (intd + extd == 0.0) err = 1.0;
					else err = (intd + emult * extd) / (intd + extd);
					color1[0] = err * (color1[0] - tc1[0]);
					color1[1] = err * (color1[1] - tc1[1]);
					color1[2] = err * (color1[2] - tc1[2]);
				}
			}
			else /* Indiscriminate error damping */
			{
				color1[0] = (color1[0] - tc1[0]) * emult;
				color1[1] = (color1[1] - tc1[1]) * emult;
				color1[2] = (color1[2] - tc1[2]) * emult;
			}
			/* Distribute the error */
			color1[0] *= fdiv;
			color1[1] *= fdiv;
			color1[2] *= fdiv;
			for (k = 0; k < 5; k++)
			{
				kk = j + (k - 2) * dj * 3 + 6;
				for (l = 0; l < 3; l++ , kk++)
				{
					row0[kk] += color1[l] * dither[k];
					row1[kk] += color1[l] * dither[k + 5];
					row2[kk] += color1[l] * dither[k + 10];
				}
			}
		}
		tmp = row0; row0 = row1; row1 = row2; row2 = tmp;
	}

	free(ddata1);
	free(ddata2);
	return (0);
}

int mem_quantize( unsigned char *old_mem_image, int target_cols, int type )
	// type = 1:flat, 2:dither, 3:scatter
{
	unsigned char *new_img = mem_img[CHN_IMAGE];
	int i, j, k;//, res=0;
	int closest[3][2];
	png_color pcol;

	j = mem_width * mem_height;

	progress_init(_("Converting to Indexed Palette"),1);

	for ( j=0; j<mem_height; j++ )		// Convert RGB to indexed
	{
		if ( j%16 == 0)
			if (progress_update( ((float) j)/(mem_height) )) break;
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
			*new_img++ = k;
		}
	}
	progress_end();

	return 0;
}

void mem_greyscale()			// Convert image to greyscale
{
	unsigned char *img = mem_img[CHN_IMAGE];
	int i, j, v;
	float value;

	if ( mem_img_bpp == 1)
	{
		for (i = 0; i < 256; i++)
		{
			value = 0.299 * mem_pal[i].red +
				0.587 * mem_pal[i].green + 0.114 * mem_pal[i].blue;
			v = (int)rint(value);
			mem_pal[i].red = v;
			mem_pal[i].green = v;
			mem_pal[i].blue = v;
		}
	}
	else
	{
		j = mem_width * mem_height;
		for (i = 0; i < j; i++)
		{
			value = 0.299 * img[0] + 0.587 * img[1] + 0.114 * img[2];
			v = (int)rint(value);
			*img++ = v;
			*img++ = v;
			*img++ = v;
		}
	}
}

/* Valid for x=0..5, which is enough here */
#define MOD3(x) ((((x) * 5 + 1) >> 2) & 3)

/* Nonclassical HSV: H is 0..6, S is 0..1, V is 0..255 */
void rgb2hsv(int *rgb, double *hsv)
{
	int c0, c1, c2;

	if (!((rgb[0] ^ rgb[1]) | (rgb[0] ^ rgb[2])))
	{
		hsv[0] = hsv[1] = 0.0;
		hsv[2] = rgb[0];
		return;
	}
	c2 = rgb[2] < rgb[0] ? 1 : 0;
	if (rgb[c2] >= rgb[c2 + 1]) c2++;
	c0 = MOD3(c2 + 1);
	c1 = MOD3(c2 + 2);
	hsv[2] = rgb[c0] > rgb[c1] ? rgb[c0] : rgb[c1];
	hsv[1] = hsv[2] - rgb[c2];
	hsv[0] = c0 * 2 + 1 + (rgb[c1] - rgb[c0]) / hsv[1];
	hsv[1] /= hsv[2];
}

static double rgb_hsl(int t, png_color col)
{
	double hsv[3];
	int rgb[3] = {col.red, col.green, col.blue};

	if (t == 2) return (0.299 * rgb[0] + 0.587 * rgb[1] + 0.114 * rgb[2]);
	rgb2hsv(rgb, hsv);
	return (hsv[t]);
}

void mem_pal_index_move( int c1, int c2 )	// Move index c1 to c2 and shuffle in between up/down
{
	png_color temp;
	int i, j;

	if (c1 == c2) return;

	j = c1 < c2 ? 1 : -1;
	temp = mem_pal[c1];
	for (i = c1; i != c2; i += j) mem_pal[i] = mem_pal[i + j];
	mem_pal[c2] = temp;
}

void mem_canvas_index_move( int c1, int c2 )	// Similar to palette item move but reworks canvas pixels
{
	unsigned char table[256], *img = mem_img[CHN_IMAGE];
	int i, j = mem_width * mem_height;

	if (c1 == c2) return;

	for (i = 0; i < 256; i++)
	{
		table[i] = i + (i > c2) - (i > c1);
	}
	table[c1] = c2;
	table[c2] += (c1 > c2);

	for (i = 0; i < j; i++)		// Change pixel index to new palette
	{
		*img = table[*img];
		img++;
	}
}

void mem_pal_sort( int a, int i1, int i2, int rev )		// Sort colours in palette
{
	int tab0[256], tab1[256], tmp, i, j;
	png_color old_pal[256];
	unsigned char *img;
	double lxnA[3], lxnB[3], lxn[3];

	if ( i2 == i1 || i1>mem_cols || i2>mem_cols ) return;
	if ( i2 < i1 )
	{
		i = i1;
		i1 = i2;
		i2 = i;
	}

	switch (a)
	{
	case 3: case 4:
		get_lxn(lxnA, PNG_2_INT(mem_col_A24));
		get_lxn(lxnB, PNG_2_INT(mem_col_B24));
		break;
	case 9:	mem_get_histogram(CHN_IMAGE);
		break;
	}
	
	for (i = 0; i < 256; i++)
		tab0[i] = i;
	for (i = i1; i <= i2; i++)
	{
		switch (a)
		{
		/* Hue */
		case 0: tab1[i] = rint(1000 * rgb_hsl(0, mem_pal[i]));
			break;
		/* Saturation */
		case 1: tab1[i] = rint(1000 * rgb_hsl(1, mem_pal[i]));
			break;
		/* Value */
		case 2: tab1[i] = rint(1000 * rgb_hsl(2, mem_pal[i]));
			break;
		/* Distance to A */
		case 3: get_lxn(lxn, PNG_2_INT(mem_pal[i]));
			tab1[i] = rint(1000 * ((lxn[0] - lxnA[0]) *
				(lxn[0] - lxnA[0]) + (lxn[1] - lxnA[1]) *
				(lxn[1] - lxnA[1]) + (lxn[2] - lxnA[2]) *
				(lxn[2] - lxnA[2])));
			break;
		/* Distance to A+B */
		case 4: get_lxn(lxn, PNG_2_INT(mem_pal[i]));
			tab1[i] = rint(1000 *
				(sqrt((lxn[0] - lxnA[0]) * (lxn[0] - lxnA[0]) +
				(lxn[1] - lxnA[1]) * (lxn[1] - lxnA[1]) +
				(lxn[2] - lxnA[2]) * (lxn[2] - lxnA[2])) +
				sqrt((lxn[0] - lxnB[0]) * (lxn[0] - lxnB[0]) +
				(lxn[1] - lxnB[1]) * (lxn[1] - lxnB[1]) +
				(lxn[2] - lxnB[2]) * (lxn[2] - lxnB[2]))));
			break;
		/* Red */
		case 5: tab1[i] = mem_pal[i].red;
			break;
		/* Green */
		case 6: tab1[i] = mem_pal[i].green;
			break;
		/* Blue */
		case 7: tab1[i] = mem_pal[i].blue;
			break;
		/* Projection on A->B */
		case 8: tab1[i] = mem_pal[i].red * (mem_col_B24.red - mem_col_A24.red) +
				mem_pal[i].green * (mem_col_B24.green - mem_col_A24.green) +
				mem_pal[i].blue * (mem_col_B24.blue - mem_col_A24.blue);
			break;
		/* Frequency */
		case 9: tab1[i] = mem_histogram[i];
			break;
		}
	}

	rev = rev ? 1 : 0;
	for ( j=i2; j>i1; j-- )			// The venerable bubble sort
		for ( i=i1; i<j; i++ )
		{
			if (tab1[i + 1 - rev] < tab1[i + rev])
			{
				tmp = tab0[i];
				tab0[i] = tab0[i + 1];
				tab0[i + 1] = tmp;

				tmp = tab1[i];
				tab1[i] = tab1[i + 1];
				tab1[i + 1] = tmp;
			}
		}

	mem_pal_copy( old_pal, mem_pal );
	for ( i=i1; i<=i2; i++ )
	{
		mem_pal[i] = old_pal[tab0[i]];
	}

	if (mem_img_bpp != 1) return;

	// Adjust canvas pixels if in indexed palette mode
	for (i = 0; i < 256; i++)
		tab1[tab0[i]] = i;
	img = mem_img[CHN_IMAGE];
	j = mem_width * mem_height;
	for (i = 0; i < j; i++)
	{
		*img = tab1[*img];
		img++;
	}
	/* Modify A & B */
	mem_col_A = tab1[mem_col_A];
	mem_col_B = tab1[mem_col_B];
}

void mem_invert()			// Invert the palette
{
	int i, j;
	png_color *col = mem_pal;
	unsigned char *img;

	if ((mem_channel == CHN_IMAGE) && (mem_img_bpp == 1))
	{
		for ( i=0; i<256; i++ )
		{
			col->red = 255 - col->red;
			col->green = 255 - col->green;
			col->blue = 255 - col->blue;
			col++;
		}
	}
	else
	{
		j = mem_width * mem_height;
		if (mem_channel == CHN_IMAGE) j *= 3;
		img = mem_img[mem_channel];
		for (i = 0; i < j; i++)
		{
			*img++ ^= 255;
		}
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
		IF_IN_RANGE( px, py ) put_pixel( px, py );
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

			for ( i=0; i<mxlen; i++ ) put_pixel( mx + i, j );
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
			for ( j=0; j<flen; j++ ) put_pixel( px, py+j );
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
			for ( j=0; j<flen; j++ ) put_pixel( px+j, py );
		}
	}
}

/*
 * This flood fill algorithm processes image in quadtree order, and thus has
 * guaranteed upper bound on memory consumption, of order O(width + height).
 * (C) Dmitry Groshev
 */
#define QLEVELS 11
#define QMINSIZE 32
#define QMINLEVEL 5
void wjfloodfill(int x, int y, int col, unsigned char *bmap, int lw)
{
	short *nearq, *farq;
	int qtail[QLEVELS + 1], ntail = 0;
	int borders[4] = {0, mem_width, 0, mem_height};
	int corners[4], levels[4], coords[4];
	int i, j, k, kk, lvl, tx, ty, fmode = 0, lastr[3], thisr[3];
	int bidx = 0, bbit = 0;
	double lastc[3], thisc[3], dist2, mdist2 = flood_step * flood_step;
	csel_info *flood_data = NULL;
	char *tmp = NULL;

	/* Init */
	if ((x < 0) || (x >= mem_width) || (y < 0) || (y >= mem_height) ||
		(get_pixel(x, y) != col) || (pixel_protected(x, y) == 255))
		return;
	i = ((mem_width + mem_height) * 3 + QMINSIZE * QMINSIZE) * 2 * sizeof(short);
	nearq = malloc(i); // Exact limit is less, but it's too complicated 
	if (!nearq) return;
	farq = nearq + QMINSIZE * QMINSIZE;
	memset(qtail, 0, sizeof(qtail));

	/* Start drawing */
	if (bmap) bmap[y * lw + (x >> 3)] |= 1 << (x & 7);
	else
	{
		put_pixel(x, y);
		if (get_pixel(x, y) == col)
		{
			/* Can't draw */
			free(nearq);
			return;
		}
	}

	/* Configure fuzzy flood fill */
	if (flood_step && ((mem_channel == CHN_IMAGE) || flood_img))
	{
		if (flood_slide) fmode = flood_cube ? 2 : 3;
		else flood_data = ALIGNTO(tmp = calloc(1, sizeof(csel_info)
			+ sizeof(double)), double);
		if (flood_data)
		{
			flood_data->center = get_pixel_RGB(x, y);
			flood_data->range = flood_step;
			flood_data->mode = flood_cube ? 2 : 0;
/* !!! Alpha isn't tested yet !!! */
			csel_reset(flood_data);
			fmode = 1;
		}
	}

	while (1)
	{
		/* Determine area */
		corners[0] = x & ~(QMINSIZE - 1);
		corners[1] = corners[0] + QMINSIZE;
		corners[2] = y & ~(QMINSIZE - 1);
		corners[3] = corners[2] + QMINSIZE;
		/* Determine queue levels */
		for (i = 0; i < 4; i++)
		{
			j = (corners[i] & ~(corners[i] - 1)) - 1;
			j = (j & 0x5555) + ((j & 0xAAAA) >> 1);
			j = (j & 0x3333) + ((j & 0xCCCC) >> 2);
			j = (j & 0x0F0F) + ((j & 0xF0F0) >> 4);
			levels[i] = (j & 0xFF) + (j >> 8) - QMINLEVEL;
		}
		/* Process near points */
		while (1)
		{
			coords[0] = x;
			coords[2] = y;
			if (fmode > 1)
			{
				k = get_pixel_RGB(x, y);
				if (fmode == 3) get_lxn(lastc, k);
				else
				{
					lastr[0] = INT_2_R(k);
					lastr[1] = INT_2_G(k);
					lastr[2] = INT_2_B(k);
				}
			}
			for (i = 0; i < 4; i++)
			{
				coords[1] = x;
				coords[3] = y;
				coords[(i & 2) + 1] += ((i + i) & 2) - 1;
				/* Is pixel valid? */
				if (coords[i] == borders[i]) continue;
				tx = coords[1];
				ty = coords[3];
				if (bmap)
				{
					bidx = ty * lw + (tx >> 3);
					bbit = 1 << (tx & 7);
					if (bmap[bidx] & bbit) continue;
				}
				/* Sliding mode */
				if (fmode == 3)
				{
					get_lxn(thisc, get_pixel_RGB(tx, ty));
					dist2 = (thisc[0] - lastc[0]) * (thisc[0] - lastc[0]) +
						(thisc[1] - lastc[1]) * (thisc[1] - lastc[1]) +
						(thisc[2] - lastc[2]) * (thisc[2] - lastc[2]);
					if (dist2 > mdist2) continue;
				}
				else if (fmode == 2)
				{
					k = get_pixel_RGB(tx, ty);
					thisr[0] = INT_2_R(k);
					thisr[1] = INT_2_G(k);
					thisr[2] = INT_2_B(k);
					if ((abs(thisr[0] - lastr[0]) > flood_step) ||
						(abs(thisr[1] - lastr[1]) > flood_step) ||
						(abs(thisr[2] - lastr[2]) > flood_step))
						continue;
				}
				/* Centered mode */
				else if (fmode)
				{
					if (!csel_scan(ty * mem_width + tx, 1, 1,
						NULL, mem_img[CHN_IMAGE], flood_data))
						continue;
				}
				/* Normal mode */
				else if (get_pixel(tx, ty) != col) continue;
				/* Is pixel writable? */
				if (bmap)
				{
					if (pixel_protected(tx, ty) == 255)
						continue;
					bmap[bidx] |= bbit;
				}
				else
				{
					put_pixel(tx, ty);
					if (get_pixel(tx, ty) == col) continue;
				}
				/* Near queue */
				if (coords[i] != corners[i])
				{
					nearq[ntail++] = tx;
					nearq[ntail++] = ty;
					continue;
				}
				/* Far queue */
				lvl = levels[i];
				for (j = 0; j < lvl; j++) // Slide lower levels
				{
					k = qtail[j];
					qtail[j] = k + 2;
					if (k > qtail[j + 1])
					{
						kk = qtail[j + 1];
						farq[k] = farq[kk];
						farq[k + 1] = farq[kk + 1];
					}
				}
				k = qtail[lvl];
				farq[k] = tx;
				farq[k + 1] = ty;
				qtail[lvl] = k + 2;
			}
			if (!ntail) break;
			y = nearq[--ntail];
			x = nearq[--ntail];
		}
		/* All done? */
		if (!qtail[0]) break;
		i = qtail[0] - 2;
		x = farq[i];
		y = farq[i + 1];
		qtail[0] = i;
		for (j = 1; qtail[j] > i; j++)
			qtail[j] = i;
	}
	free(nearq);
	free(tmp);
}

/* Regular flood fill */
void flood_fill(int x, int y, unsigned int target)
{
	wjfloodfill(x, y, target, NULL, 0);
}

/* Pattern flood fill - uses temporary area (1 bit per pixel) */
void flood_fill_pat(int x, int y, unsigned int target)
{
	unsigned char *pat, *temp;
	int i, j, k, lw = (mem_width + 7) >> 3;

	j = lw * mem_height;
	pat = temp = malloc(j);
	if (!pat)
	{
		memory_errors(1);
		return;
	}
	memset(pat, 0, j);
	wjfloodfill(x, y, target, pat, lw);
	for (i = 0; i < mem_height; i++)
	{
		for (j = 0; j < mem_width; )
		{
			k = *temp++;
			if (!k)
			{
				j += 8;
				continue;
			}
			for (; k; k >>= 1)
			{
				if (k & 1) put_pixel(j, i);
				j++;
			}
			j = (j + 7) & ~(7);
		}
	}
	free(pat);
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
		for ( i=0; i<w; i++ ) put_pixel( x + i, y + j );
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
		for ( i=0; i<=r4; i++)
		{
			IF_IN_RANGE( rx+i, ry ) put_pixel( rx+i, ry );
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

	put_pixel(x0, y0);
	if (x0 != x1) put_pixel(x1, y0);
	if (y0 == y1) return;
	put_pixel(x0, y1);
	if (x0 != x1) put_pixel(x1, y1);
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


int check_str( int max, char *a, char *b )	// Compare up to max characters of 2 strings
						// Case insensitive
{
	return (strncasecmp(a, b, max) == 0);
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

void mem_flip_v(char *mem, char *tmp, int w, int h, int bpp)
{
	unsigned char *src, *dest;
	int i, k;

	k = w * bpp;
	src = mem;
	dest = mem + (h - 1) * k;
	h /= 2;

	for (i = 0; i < h; i++)
	{
		memcpy(tmp, src, k);
		memcpy(src, dest, k);
		memcpy(dest, tmp, k);
		src += k;
		dest -= k;
	}
}

void mem_flip_h( char *mem, int w, int h, int bpp )
{
	unsigned char tmp, *src, *dest;
	int i, j, k;

	k = w * bpp;
	w /= 2;
	for (i = 0; i < h; i++)
	{
		src = mem + i * k;
		dest = src + k - bpp;
		if (bpp == 1)
		{
			for (j = 0; j < w; j++)
			{
				tmp = *src;
				*src++ = *dest;
				*dest-- = tmp;
			}
		}
		else
		{
			for (j = 0; j < w; j++)
			{
				tmp = src[0];
				src[0] = dest[0];
				dest[0] = tmp;
				tmp = src[1];
				src[1] = dest[1];
				dest[1] = tmp;
				tmp = src[2];
				src[2] = dest[2];
				dest[2] = tmp;
				src += 3;
				dest -= 3;
			}
		}
	}
}

void mem_bacteria( int val )			// Apply bacteria effect val times the canvas area
{						// Ode to 1994 and my Acorn A3000
	int i, j, x, y, w = mem_width-2, h = mem_height-2, tot = w*h, np, cancel;
	unsigned int pixy;
	unsigned char *img;

	while ( tot > PROGRESS_LIM )	// Ensure the user gets a regular opportunity to cancel
	{
		tot /= 2;
		val *= 2;
	}

	cancel = (w * h * val > PROGRESS_LIM);
	if (cancel)
	{
		progress_init(_("Bacteria Effect"), 1);
		progress_update(0.0);
	}
	for ( i=0; i<val; i++ )
	{
		if (cancel && ((i * 20) % val >= val - 20))
			if (progress_update((float)i / val)) break;

		for ( j=0; j<tot; j++ )
		{
			x = rand() % w;
			y = rand() % h;
			img = mem_img[CHN_IMAGE] + x + mem_width * y;
			pixy = img[0] + img[1] + img[2];
			img += mem_width;
			pixy += img[0] + img[1] + img[2];
			img += mem_width;
			pixy += img[0] + img[1] + img[2];
			np = ((pixy + pixy + 9) / 18 + 1) % mem_cols;
			*(img - mem_width + 1) = (unsigned char)np;
		}
	}
	if (cancel) progress_end();
}

void mem_rotate( char *new, char *old, int old_w, int old_h, int dir, int bpp )
{
	unsigned char *src;
	int i, j, k, l, flag;

	flag = (old_w * old_h > PROGRESS_LIM * 4);
	j = old_w * bpp;
	l = dir ? -bpp : bpp;
	k = -old_w * l;
	old += dir ? j - bpp: (old_h - 1) * j;

	if (flag) progress_init(_("Rotating"), 1);
	for (i = 0; i < old_w; i++)
	{
		if (flag && ((i * 5) % old_w >= old_w - 5))
				progress_update((float)i / old_w);
		src = old;
		if (bpp == 1)
		{
			for (j = 0; j < old_h; j++)
			{
				*new++ = *src;
				src += k;
			}
		}
		else
		{
			for (j = 0; j < old_h; j++)
			{
				*new++ = src[0];
				*new++ = src[1];
				*new++ = src[2];
				src += k;
			}
		}
		old += l;
	}
	if (flag) progress_end();
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

// Rotate canvas by any angle (degrees)
int mem_rotate_free(double angle, int type, int gcor)
{
	chanlist old_img;
	unsigned char *src, *dest, *alpha, A_rgb[3], fillv;
	unsigned char *pix1, *pix2, *pix3, *pix4;
	int ow = mem_width, oh = mem_height, nw, nh, res;
	int nx, ny, ox, oy, cc;
	double rangle = (M_PI / 180.0) * angle;	// Radians
	double s1, s2, c1, c2;			// Trig values
	double cx0, cy0, cx1, cy1;
	double x00, y00, x0y, y0y;		// Quick look up values
	double fox, foy, k1, k2, k3, k4;	// Pixel weights
	double aa1, aa2, aa3, aa4, aa;
	double rr, gg, bb;

	c2 = cos(rangle);
	s2 = sin(rangle);
	c1 = -s2;
	s1 = c2;

	nw = ceil(fabs(ow * c2) + fabs(oh * s2));
	nh = ceil(fabs(oh * c2) + fabs(ow * s2));

	if ( nw>MAX_WIDTH || nh>MAX_HEIGHT ) return -5;		// If new image is too big return -5

	memcpy(old_img, mem_img, sizeof(chanlist));
	res = undo_next_core(2, nw, nh, mem_img_bpp, CMASK_ALL);
	if ( res == 1 ) return 2;		// No undo space

	/* Centerpoints, including half-pixel offsets */
	cx0 = (ow - 1) / 2.0;
	cy0 = (oh - 1) / 2.0;
	cx1 = (nw - 1) / 2.0;
	cy1 = (nh - 1) / 2.0;

	x00 = cx0 - cx1 * s1 - cy1 * s2;
	y00 = cy0 - cx1 * c1 - cy1 * c2;
	A_rgb[0] = mem_col_A24.red;
	A_rgb[1] = mem_col_A24.green;
	A_rgb[2] = mem_col_A24.blue;

	progress_init(_("Free Rotation"),0);
	for (ny = 0; ny < nh; ny++)
	{
		if ((ny * 10) % nh >= nh - 10)
			progress_update((float)ny / nh);
		x0y = ny * s2 + x00;
		y0y = ny * c2 + y00;

		for (cc = 0; cc < NUM_CHANNELS; cc++)
		{
			if (!mem_img[cc]) continue;
			/* RGB nearest neighbour */
			if (!type && (cc == CHN_IMAGE) && (mem_img_bpp == 3))
			{
				dest = mem_img[CHN_IMAGE] + ny * nw * 3;
				for (nx = 0; nx < nw; nx++)
				{
					ox = rint(nx * s1 + x0y);
					oy = rint(nx * c1 + y0y);
					src = A_rgb;
					if ((ox >= 0) && (ox < ow) &&
						(oy >= 0) && (oy < oh))
						src = old_img[CHN_IMAGE] +
							(oy * ow + ox) * 3;
					*dest++ = src[0];
					*dest++ = src[1];
					*dest++ = src[2];
				}
				continue;
			}
			/* One-bpp nearest neighbour */
			if (!type)
			{
				fillv = cc == CHN_IMAGE ? mem_col_A : 0;
				dest = mem_img[cc] + ny * nw;
				for (nx = 0; nx < nw; nx++)
				{
					ox = rint(nx * s1 + x0y);
					oy = rint(nx * c1 + y0y);
					if ((ox >= 0) && (ox < ow) &&
						(oy >= 0) && (oy < oh))
						*dest++ = old_img[cc][oy * ow + ox];
					else *dest++ = fillv;
				}
				continue;
			}
			/* RGB/RGBA bilinear */
			if (cc == CHN_IMAGE)
			{
				alpha = NULL;
				if (mem_img[CHN_ALPHA] && !channel_dis[CHN_ALPHA])
					alpha = mem_img[CHN_ALPHA] + ny * nw;
				dest = mem_img[CHN_IMAGE] + ny * nw * 3;
				for (nx = 0; nx < nw; nx++)
				{
					fox = nx * s1 + x0y;
					foy = nx * c1 + y0y;
					ox = floor(fox);
					oy = floor(foy);
					if ((ox < -1) || (ox >= ow) ||
						(oy < -1) || (oy >= oh))
					{
						*dest++ = A_rgb[0];
						*dest++ = A_rgb[1];
						*dest++ = A_rgb[2];
						if (!alpha) continue;
						*alpha++ = 0;
						continue;
					}
					fox -= ox;
					foy -= oy;
					k4 = fox * foy;
					k3 = foy - k4;
					k2 = fox - k4;
					k1 = 1.0 - fox - foy + k4;
					pix1 = old_img[CHN_IMAGE] + (oy * ow + ox) * 3;
					pix2 = pix1 + 3;
					pix3 = pix1 + ow * 3;
					pix4 = pix3 + 3;
					if (ox > ow - 2) pix2 = pix4 = A_rgb;
					else if (ox < 0) pix1 = pix3 = A_rgb;
					if (oy > oh - 2) pix3 = pix4 = A_rgb;
					else if (oy < 0) pix1 = pix2 = A_rgb;
					if (alpha)
					{
						aa1 = aa2 = aa3 = aa4 = 0.0;
						src = old_img[CHN_ALPHA] + oy * ow + ox;
						if (pix1 != A_rgb) aa1 = src[0] * k1;
						if (pix2 != A_rgb) aa2 = src[1] * k2;
						if (pix3 != A_rgb) aa3 = src[ow] * k3;
						if (pix4 != A_rgb) aa4 = src[ow + 1] * k4;
						aa = aa1 + aa2 + aa3 + aa4;
						if ((*alpha++ = rint(aa)))
						{
							aa = 1.0 / aa;
							k1 = aa1 * aa;
							k2 = aa2 * aa;
							k3 = aa3 * aa;
							k4 = aa4 * aa;
						}
					}
					if (gcor) /* Gamma-correct */
					{
						rr = gamma256[pix1[0]] * k1 +
							gamma256[pix2[0]] * k2 +
							gamma256[pix3[0]] * k3 +
							gamma256[pix4[0]] * k4;
						gg = gamma256[pix1[1]] * k1 +
							gamma256[pix2[1]] * k2 +
							gamma256[pix3[1]] * k3 +
							gamma256[pix4[1]] * k4;
						bb = gamma256[pix1[2]] * k1 +
							gamma256[pix2[2]] * k2 +
							gamma256[pix3[2]] * k3 +
							gamma256[pix4[2]] * k4;
						*dest++ = UNGAMMA256(rr);
						*dest++ = UNGAMMA256(gg);
						*dest++ = UNGAMMA256(bb);
					}
					else /* Leave as is */
					{
						rr = pix1[0] * k1 + pix2[0] * k2 +
							pix3[0] * k3 + pix4[0] * k4;
						gg = pix1[1] * k1 + pix2[1] * k2 +
							pix3[1] * k3 + pix4[1] * k4;
						bb = pix1[2] * k1 + pix2[2] * k2 +
							pix3[2] * k3 + pix4[2] * k4;
						*dest++ = rint(rr);
						*dest++ = rint(gg);
						*dest++ = rint(bb);
					}
				}
				continue;
			}
			/* Alpha channel already done... maybe */
			if ((cc == CHN_ALPHA) && !channel_dis[CHN_ALPHA])
				continue;
			/* Utility channel bilinear */
			dest = mem_img[cc] + ny * nw;
			for (nx = 0; nx < nw; nx++)
			{
				fox = nx * s1 + x0y;
				foy = nx * c1 + y0y;
				ox = floor(fox);
				oy = floor(foy);
				if ((ox < -1) || (ox >= ow) ||
					(oy < -1) || (oy >= oh))
				{
					*dest++ = 0;
					continue;
				}
				fox -= ox;
				foy -= oy;
				k4 = fox * foy;
				k3 = foy - k4;
				k2 = fox - k4;
				k1 = 1.0 - fox - foy + k4;
				src = old_img[cc] + oy * ow + ox;
				aa1 = aa2 = aa3 = aa4 = 0.0;
				if (ox < ow - 1)
				{
					if (oy < oh - 1) aa4 = src[ow + 1] * k4;
					if (oy >= 0) aa2 = src[1] * k2;
				}
				if (ox >= 0)
				{
					if (oy < oh - 1) aa3 = src[ow] * k3;
					if (oy >= 0) aa1 = src[0] * k1;
				}
				*dest++ = rint(aa1 + aa2 + aa3 + aa4);
			}
		}
	}
	progress_end();

	return 0;
}

int mem_image_rot( int dir )					// Rotate image 90 degrees
{
	chanlist old_img;
	int i, ow = mem_width, oh = mem_height;

	memcpy(old_img, mem_img, sizeof(chanlist));
	i = undo_next_core(2, oh, ow, mem_img_bpp, CMASK_ALL);
	if (i) return 1;			// Not enough memory

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!mem_img[i]) continue;
		mem_rotate(mem_img[i], old_img[i], ow, oh, dir, BPP(i));
	}
	return 0;
}



///	Code for scaling contributed by Dmitry Groshev, January 2006

typedef struct {
	int idx;
	float k;
} fstep;


static fstep *make_filter(int l0, int l1, int type)
{
	fstep *res, *buf;
	double Aarray[4] = {-0.5, -2.0 / 3.0, -0.75, -1.0};
	double x, y, basept, fwidth, delta, scale = (double)l1 / (double)l0;
	double A = 0.0, kk = 1.0, sum;
	int pic_tile = FALSE; /* Allow to enable tiling mode later */
	int pic_skip = FALSE; /* Allow to enable skip mode later */
	int i, j, k, ix;


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
			ix = j;
			if ((j < 0) || (j >= l0))
			{
				if (pic_skip) continue;
				if (pic_tile)
				{
					if (ix < 0) ix = l0 - (-ix % l0);
					ix %= l0;
				}
				else if (l0 == 1) ix = 0;
				else 
				{
					ix = abs(ix) % (l0 + l0 - 2);
					if (ix >= l0) ix = l0 + l0 - 2 - ix;
				}
			}
			buf->idx = ix;
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

	/* Normalization pass */
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

static char *workarea;
static fstep *hfilter, *vfilter;

static void clear_scale()
{
	free(workarea);
	free(hfilter);
	free(vfilter);
}

static int prepare_scale(int ow, int oh, int nw, int nh, int type)
{
	workarea = NULL;
	hfilter = vfilter = NULL;
	if (!type || (mem_img_bpp == 1)) return TRUE;
	workarea = malloc((7 * ow + 1) * sizeof(double));
	hfilter = make_filter(ow, nw, type);
	vfilter = make_filter(oh, nh, type);
	if (!workarea || !hfilter || !vfilter)
	{
		clear_scale();
		return FALSE;
	}
	return TRUE;
}

static void do_scale(chanlist old_img, chanlist new_img, int ow, int oh,
	int nw, int nh, int gcor)
{
	unsigned char *src, *img, *imga;
	fstep *tmp = NULL, *tmpx, *tmpy, *tmpp;
	double *wrk, *wrk2, *wrka, *work_area;
	double sum, sum1, sum2, kk, mult;
	int i, j, cc, bpp, gc, tmask;

	work_area = ALIGNTO(workarea, double);
	tmask = new_img[CHN_ALPHA] && !channel_dis[CHN_ALPHA] ? CMASK_RGBA : CMASK_NONE;

	/* For each destination line */
	tmpy = vfilter;
	for (i = 0; i < nh; i++, tmpy++)
	{
		/* Process regular channels */
		for (cc = 0; cc < NUM_CHANNELS; cc++)
		{
			if (!new_img[cc]) continue;
			/* Do RGBA separately */
			if (tmask & CMASK_FOR(cc)) continue;
			bpp = cc == CHN_IMAGE ? 3 : 1;
			gc = cc == CHN_IMAGE ? gcor : FALSE;
			memset(work_area, 0, ow * bpp * sizeof(double));
			src = old_img[cc];
			/* Build one vertically-scaled row */
			for (tmp = tmpy; tmp->idx >= 0; tmp++)
			{
				img = src + tmp->idx * ow * bpp;
				wrk = work_area;
				kk = tmp->k;
				if (gc) /* Gamma-correct */
				{
					for (j = 0; j < ow * bpp; j++)
						*wrk++ += gamma256[*img++] * kk;
				}
				else /* Leave as is */
				{
					for (j = 0; j < ow * bpp; j++)
						*wrk++ += *img++ * kk;
				}
			}
			/* Scale it horizontally */
			img = new_img[cc] + i * nw * bpp;
			sum = sum1 = sum2 = 0.0;
			for (tmpx = hfilter; ; tmpx++)
			{
				if (tmpx->idx >= 0)
				{
					wrk = work_area + tmpx->idx * bpp;
					kk = tmpx->k;
					sum += wrk[0] * kk;
					if (bpp == 1) continue;
					sum1 += wrk[1] * kk;
					sum2 += wrk[2] * kk;
					continue;
				}
				if (gc) /* Reverse gamma correction */
				{
					*img++ = sum < 0.0 ? 0 : sum > 1.0 ?
						0xFF : UNGAMMA256(sum);
					*img++ = sum1 < 0.0 ? 0 : sum1 > 1.0 ?
						0xFF : UNGAMMA256(sum1);
					*img++ = sum2 < 0.0 ? 0 : sum2 > 1.0 ?
						0xFF : UNGAMMA256(sum2);
					sum = sum1 = sum2 = 0.0;
					if (tmpx->idx < -1) break;
					continue;
				}
				if (bpp > 1)
				{
					j = (int)rint(sum1);
					img[1] = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
					j = (int)rint(sum2);
					img[2] = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
					sum1 = sum2 = 0.0;
				}
				j = (int)rint(sum);
				img[0] = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
				sum = 0.0; img += bpp;
				if (tmpx->idx < -1) break;
			}
		}
		/* Process RGBA */
		if (tmask != CMASK_NONE)
		{
			memset(work_area, 0, ow * 7 * sizeof(double));
			/* Build one vertically-scaled row - with & w/o alpha */
			for (tmp = tmpy; tmp->idx >= 0; tmp++)
			{
				img = old_img[CHN_IMAGE] + tmp->idx * ow * 3;
				imga = old_img[CHN_ALPHA] + tmp->idx * ow;
				wrk = work_area + ow;
				wrk2 = work_area + 4 * ow;
				if (gcor) /* Gamma-correct */
				{
					for (j = 0; j < ow; j++)
					{
						kk = imga[j] * tmp->k;
						work_area[j] += kk;
						wrk[0] += gamma256[img[0]] * tmp->k;
						wrk2[0] += gamma256[img[0]] * kk;
						wrk[1] += gamma256[img[1]] * tmp->k;
						wrk2[1] += gamma256[img[1]] * kk;
						wrk[2] += gamma256[img[2]] * tmp->k;
						wrk2[2] += gamma256[img[2]] * kk;
						wrk += 3; wrk2 += 3; img += 3;
					}
				}
				else /* Leave as is */
				{
					for (j = 0; j < ow; j++)
					{
						kk = imga[j] * tmp->k;
						work_area[j] += kk;
						wrk[0] += img[0] * tmp->k;
						wrk2[0] += img[0] * kk;
						wrk[1] += img[1] * tmp->k;
						wrk2[1] += img[1] * kk;
						wrk[2] += img[2] * tmp->k;
						wrk2[2] += img[2] * kk;
						wrk += 3; wrk2 += 3; img += 3;
					}
				}
			}
			/* Scale it horizontally */
			img = new_img[CHN_IMAGE] + i * nw * 3;
			imga = new_img[CHN_ALPHA] + i * nw;
			for (tmpp = tmpx = hfilter; tmpp->idx >= -1; tmpx = tmpp + 1)
			{
				sum = 0.0;
				for (tmpp = tmpx; tmpp->idx >= 0; tmpp++)
					sum += work_area[tmpp->idx] * tmpp->k;
				j = (int)rint(sum);
				*imga = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
				wrk = work_area + ow;
				mult = 1.0;
				if (*imga++)
				{
					wrk = work_area + 4 * ow;
					mult /= sum;
				}
				sum = sum1 = sum2 = 0.0;
				for (tmpp = tmpx; tmpp->idx >= 0; tmpp++)
				{
					wrka = wrk + tmpp->idx * 3;
					kk = tmpp->k;
					sum += wrka[0] * kk;
					sum1 += wrka[1] * kk;
					sum2 += wrka[2] * kk;
				}
				sum *= mult; sum1 *= mult; sum2 *= mult;
				if (gcor) /* Reverse gamma correction */
				{
					*img++ = sum < 0.0 ? 0 : sum > 1.0 ?
						0xFF : UNGAMMA256(sum);
					*img++ = sum1 < 0.0 ? 0 : sum1 > 1.0 ?
						0xFF : UNGAMMA256(sum1);
					*img++ = sum2 < 0.0 ? 0 : sum2 > 1.0 ?
						0xFF : UNGAMMA256(sum2);
				}
				else /* Simply round to nearest */
				{
					j = (int)rint(sum);
					*img++ = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
					j = (int)rint(sum1);
					*img++ = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
					j = (int)rint(sum2);
					*img++ = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
				}
			}
		}
		if ((i * 10) % nh >= nh - 10) progress_update((float)(i + 1) / nh);
		if (tmp->idx < -1) break;
		tmpy = tmp;
	}

	clear_scale();
}

int mem_image_scale(int nw, int nh, int type, int gcor)		// Scale image
{
	chanlist old_img;
	char *src, *dest;
	int i, j, oi, oj, cc, bpp, res, ow = mem_width, oh = mem_height;
	double scalex, scaley, deltax, deltay;

	mtMIN( nw, nw, MAX_WIDTH )
	mtMAX( nw, nw, 1 )
	mtMIN( nh, nh, MAX_HEIGHT )
	mtMAX( nh, nh, 1 )

	if (!prepare_scale(ow, oh, nw, nh, type)) return 1;	// Not enough memory

	memcpy(old_img, mem_img, sizeof(chanlist));
	res = undo_next_core(2, nw, nh, mem_img_bpp, CMASK_ALL);
	if (res)
	{
		clear_scale();
		return 1;			// Not enough memory
	}

	progress_init(_("Scaling Image"),0);
	if (type && (mem_img_bpp == 3))
		do_scale(old_img, mem_img, ow, oh, nw, nh, gcor);
	else
	{
		scalex = (double)ow / (double)nw;
		scaley = (double)oh / (double)nh;
		deltax = 0.5 * scalex - 0.5;
		deltay = 0.5 * scaley - 0.5;

		for (j = 0; j < nh; j++)
		{
			for (cc = 0; cc < NUM_CHANNELS; cc++)
			{
				if (!mem_img[cc]) continue;
				bpp = BPP(cc);
				dest = mem_img[cc] + nw * j * bpp;
				oj = rint(scaley * j + deltay);
				src = old_img[cc] + ow * oj * bpp;
				for (i = 0; i < nw; i++)
				{
					oi = (int)rint(scalex * i + deltax) * bpp;
					*dest++ = src[oi];
					if (bpp == 1) continue;
					*dest++ = src[oi + 1];
					*dest++ = src[oi + 2];
				}
			}
			if ((j * 10) % nh >= nh - 10)
				progress_update((float)(j + 1) / nh);
		}
	}
	progress_end();

	return 0;
}

int mem_isometrics(int type)
{
	unsigned char *wrk, *src, *dest, *fill;
	int i, j, k, l, cc, step, bpp, ow = mem_width, oh = mem_height;

	if ( type<2 )
	{
		if ( (oh + (ow-1)/2) > MAX_HEIGHT ) return -666;
		i = mem_image_resize(ow, oh + (ow-1)/2, 0, 0, 0);
	}
	if ( type>1 )
	{
		if ( (ow+oh-1) > MAX_WIDTH ) return -666;
		i = mem_image_resize(ow + oh - 1, oh, 0, 0, 0);
	}

	if ( i<0 ) return i;

	for (cc = 0; cc < NUM_CHANNELS; cc++)
	{
		if (!mem_img[cc]) continue;
		bpp = BPP(cc);
		if ( type < 2 )		// Left/Right side down
		{
			fill = mem_img[cc] + (mem_height - 1) * ow * bpp;
			step = ow * bpp;
			if (type) step = -step;
			else fill += (2 - (ow & 1)) * bpp;
			for (i = mem_height - 1; i >= 0; i--)
			{
				k = i + i + 2;
				if (k > ow) k = ow;
				l = k;
				j = 0;
				dest = mem_img[cc] + i * ow * bpp;
				src = dest - step;
				if (!type)
				{
					j = ow - k;
					dest += j * bpp;
					src += (j - ow * ((ow - j - 1) >> 1)) * bpp;
					j = j ? 0 : ow & 1;
					k += j;
					if (j) src += step;
				}
				for (; j < k; j++)
				{
					if (!(j & 1)) src += step;
					*dest++ = *src++;
					if (bpp == 1) continue;
					*dest++ = *src++;
					*dest++ = *src++;
				}
				if (l < ow)
				{
					if (!type) dest = mem_img[cc] + i * ow * bpp;
					memcpy(dest, fill, (ow - l) * bpp);
				}
			}
		}
		else			// Top/Bottom side right
		{
			step = mem_width * bpp;
			fill = mem_img[cc] + ow * bpp;
			k = (oh - 1) * mem_width * bpp;
			if (type == 2)
			{
				fill += k;
				step = -step;
			}
			wrk = fill + step - 1;
			k = ow * bpp;
			for (i = 1; i < oh; i++)
			{
				src = wrk;
				dest = wrk + i * bpp;
				for (j = 0; j < k; j++)
					*dest-- = *src--;
				memcpy(src + 1, fill, i * bpp);
				wrk += step;
			}
		}
	}

	return 0;
}

/* This code assumes that source image is in bounds when enlarging */
/* Modes: 0 - clear, 1 - tile, 2 - mirror tile */
int mem_image_resize(int nw, int nh, int ox, int oy, int mode)
{
	chanlist old_img;
	char *src, *dest;
	int i, j, k, cc, bpp, oxo = 0, oyo = 0, nxo = 0, nyo = 0, ow, oh, res;
	int oww = mem_width, ohh = mem_height, mirr = 0, tw = mem_width;

	nw = nw < 1 ? 1 : nw > MAX_WIDTH ? MAX_WIDTH : nw;
	nh = nh < 1 ? 1 : nh > MAX_HEIGHT ? MAX_HEIGHT : nh;
	if ((nw <= oww) && (nh <= ohh)) mode = 0;

	memcpy(old_img, mem_img, sizeof(chanlist));
	res = undo_next_core(2, nw, nh, mem_img_bpp, CMASK_ALL);
	if (res) return 1;			// Not enough memory

	if ( ox < 0 ) oxo = -ox;
	else nxo = ox;
	if ( oy < 0 ) oyo = -oy;
	else nyo = oy;

	if (!mode) /* Clear */
	{
		j = nw * nh;
		for (cc = 0; cc < NUM_CHANNELS; cc++)
		{
			if (!mem_img[cc]) continue;
			dest = mem_img[cc];
			if ((cc != CHN_IMAGE) || (mem_img_bpp == 1))
			{
				memset(dest, cc == CHN_IMAGE ? mem_col_A : 0, j);
				continue;
			}
			for (i = 0; i < j; i++)	// Background is current colour A
			{
				*dest++ = mem_col_A24.red;
				*dest++ = mem_col_A24.green;
				*dest++ = mem_col_A24.blue;
			}
		}
		ow = oww < nw ? oww : nw;
	}
	else /* Tile - prepare for horizontal pass */
	{
		mirr = (mode == 2) && (oww > 2) ? 1 : 0;
		ow = nw;
		tw -= mirr;
		i = (nxo + tw - 1) / tw;
		if (i & 1) mirr = -mirr;
		oxo = i * tw - nxo;
		nxo = 0;
	}
	oh = ohh < nh ? ohh : nh;

	/* Do horizontal tiling */
	for (; ow; ow -= res)
	{	
		res = tw - oxo < ow ? tw - oxo : ow;
		for (cc = 0; cc < NUM_CHANNELS; cc++)
		{
			if (!mem_img[cc]) continue;
			bpp = BPP(cc);
			j = res * bpp;
			for (i = 0; i < oh; i++)
			{
				src = old_img[cc] + (oxo + oww * (i + oyo)) * bpp;
				dest = mem_img[cc] + (nxo + nw * (i + nyo)) * bpp;
				/* Normal copy */
				if (mirr >= 0)
				{
					memcpy(dest, src, j);
					continue;
				}
				/* Reverse copy */
				src += (oww - oxo - oxo - 1) * bpp;
				for (k = 0; k < j; k += bpp , src -= bpp)
				{
					dest[k] = src[0];
					if (bpp == 1) continue;
					dest[k + 1] = src[1];
					dest[k + 2] = src[2];
				}
			}
		}
		nxo += res;
		oxo = 0;
		mirr = -mirr;
	}

	/* Only one stripe? */
	if (!mode || (nh <= ohh)) return (0);

	/* Tile up & down */
	if (ohh < 3) mirr = 0;
	for (cc = 0; cc < NUM_CHANNELS; cc++)
	{
		if (!mem_img[cc]) continue;
		bpp = nw * BPP(cc);
		i = nyo - 1;
		dest = mem_img[cc] + i * bpp;
		if (mirr) /* Reverse copy up */
		{
			j = i - (ohh - 2);
			if (j < -1) j = -1;
			src = dest + 2 * bpp;
			for (; i > j; i--)
			{
				memcpy(dest, src, bpp);
				dest -= bpp;
				src += bpp;
			}
		}
		/* Forward copy up */
		src = mem_img[cc] + (nyo + ohh - 1) * bpp;
		for (; i >= 0; i--)
		{
			memcpy(dest, src, bpp);
			dest -= bpp;
			src -= bpp;
		}
		i = nyo + ohh;
		dest = mem_img[cc] + i * bpp;
		if (mirr) /* Reverse copy down */
		{
			j = i + ohh - 2;
			if (j > nh) j = nh;
			src = dest - 2 * bpp;
			for (; i < j; i++)
			{
				memcpy(dest, src, bpp);
				dest += bpp;
				src -= bpp;
			}
		}
		/* Forward copy down */
		src = mem_img[cc] + nyo * bpp;
		for (; i < nh; i++)
		{
			memcpy(dest, src, bpp);
			dest += bpp;
			src += bpp;
		}
	}

	return 0;
}

void mem_threshold(int channel, int level)		// Threshold channel values
{
	unsigned char *wrk = mem_img[channel];
	int i, j = mem_width * mem_height * MEM_BPP;

	if (!wrk) return; /* Paranoia */
	for (i = 0; i < j; i++)
	{
		wrk[i] = wrk[i] < level ? 0 : 255;
	}
}

png_color get_pixel24( int x, int y )	/* RGB */
{
	unsigned char *img = mem_img[CHN_IMAGE];
	png_color pix;

	x = (mem_width * y + x) * 3;
	pix.red = img[x];
	pix.green = img[x + 1];
	pix.blue = img[x + 2];

	return pix;
}

int get_pixel( int x, int y )	/* Mixed */
{
	x = mem_width * y + x;
	if ((mem_channel != CHN_IMAGE) || (mem_img_bpp == 1))
		return (mem_img[mem_channel][x]);
	x *= 3;
	return (MEM_2_INT(mem_img[CHN_IMAGE], x));
}

int get_pixel_RGB( int x, int y )	/* RGB */
{
	x = mem_width * y + x;
	if (mem_img_bpp == 1)
		return (PNG_2_INT(mem_pal[mem_img[CHN_IMAGE][x]]));
	x *= 3;
	return (MEM_2_INT(mem_img[CHN_IMAGE], x));
}

int mem_protected_RGB(int intcol)		// Is this intcol in list?
{
	int i;

	if (!mem_prot) return (0);
	for (i = 0; i < mem_prot; i++)
		if (intcol == mem_prot_RGB[i]) return (255);

	return (0);
}

int pixel_protected(int x, int y)
{
	int offset = y * mem_width + x;

	if (mem_unmask) return (0);

	/* Colour protection */
	if (mem_img_bpp == 1)
	{
		if (mem_prot_mask[mem_img[CHN_IMAGE][offset]]) return (255);
	}
	else
	{
		if (mem_prot && mem_protected_RGB(MEM_2_INT(mem_img[CHN_IMAGE],
			offset * 3))) return (255);
	}

	/* Colour selectivity */
	if (mem_cselect && csel_scan(offset, 1, 1, NULL, mem_img[CHN_IMAGE], csel_data))
		return (255);

	/* Mask channel */
	if ((mem_channel <= CHN_ALPHA) && mem_img[CHN_MASK] && !channel_dis[CHN_MASK])
		return (mem_img[CHN_MASK][offset]);

	return (0);
}

void prep_mask(int start, int step, int cnt, unsigned char *mask,
	unsigned char *mask0, unsigned char *img0)
{
	int i;

	cnt = start + step * (cnt - 1) + 1;

	if (mem_unmask)
	{
		memset(mask, 0, cnt);
		return;
	}

	/* Clear mask or copy mask channel into it */
	if (mask0) memcpy(mask, mask0, cnt);
	else memset(mask, 0, cnt);

	/* Add colour protection to it */
	if (mem_img_bpp == 1)
	{
		for (i = start; i < cnt; i += step)
		{
			mask[i] |= mem_prot_mask[img0[i]];
		}
	}
	else if (mem_prot)
	{
		for (i = start; i < cnt; i += step)
		{
			mask[i] |= mem_protected_RGB(MEM_2_INT(img0, i * 3));
		}
	}

	/* Add colour selectivity to it */
	if (mem_cselect) csel_scan(start, step, cnt, mask, img0, csel_data);
}

/* Prepare mask array - for each pixel >0 if masked, 0 if not */
void row_protected(int x, int y, int len, unsigned char *mask)
{
	unsigned char *mask0 = NULL;
	int ofs = y * mem_width + x;

	/* Clear mask or copy mask channel into it */
	if ((mem_channel <= CHN_ALPHA) && mem_img[CHN_MASK] && !channel_dis[CHN_MASK])
		mask0 = mem_img[CHN_MASK] + ofs;

	prep_mask(0, 1, len, mask, mask0, mem_img[CHN_IMAGE] + ofs * mem_img_bpp);
}

void put_pixel( int x, int y )	/* Combined */
{
	unsigned char *old_image, *new_image, *old_alpha = NULL, newc, oldc;
	unsigned char r, g, b, nr, ng, nb;
	int i, j, offset, ofs3, opacity = 0, tint;

	j = pixel_protected(x, y);
	if (mem_img_bpp == 1 ? j : j == 255) return;

	tint = tint_mode[0] ? 1 : 0;
	if ((tint_mode[2] == 1) || !(tint_mode[2] || tint_mode[1])) tint = -tint;

	if (mem_undo_opacity) old_image = mem_undo_previous(mem_channel);
	else old_image = mem_img[mem_channel];
	if (mem_channel <= CHN_ALPHA)
	{
		if (RGBA_mode || (mem_channel == CHN_ALPHA))
		{
			if (mem_undo_opacity)
				old_alpha = mem_undo_previous(CHN_ALPHA);
			else old_alpha = mem_img[CHN_ALPHA];
		}
		if (mem_img_bpp == 3)
		{
			j = (255 - j) * tool_opacity;
			opacity = (j + (j >> 8) + 1) >> 8;
		}
	}
	offset = x + mem_width * y;
	i = ((x & 7) + 8 * (y & 7));

	/* Alpha channel */
	if (old_alpha && mem_img[CHN_ALPHA])
	{
		newc = mem_col_pat[i] == mem_col_A ? channel_col_A[CHN_ALPHA] :
			channel_col_B[CHN_ALPHA];
		oldc = old_alpha[offset];
		if (tint)
		{
			if (tint < 0) newc = oldc > 255 - newc ?
				255 : oldc + newc;
			else newc = oldc > newc ? oldc - newc : 0;
		}
		if (opacity)
		{
			j = oldc * 255 + (newc - oldc) * opacity;
			mem_img[CHN_ALPHA][offset] = (j + (j >> 8) + 1) >> 8;
			if (j && !channel_dis[CHN_ALPHA])
				opacity = (255 * opacity * newc) / j;
		}
		else mem_img[CHN_ALPHA][offset] = newc;
		if (mem_channel == CHN_ALPHA) return;
	}

	/* Indexed image or utility channel */
	if ((mem_channel != CHN_IMAGE) || (mem_img_bpp == 1))
	{
		newc = mem_col_pat[i];
		if (mem_channel != CHN_IMAGE)
			newc = newc == mem_col_A ? channel_col_A[mem_channel] :
				channel_col_B[mem_channel];
		if (tint)
		{
			if (tint < 0)
			{
				j = mem_channel == CHN_IMAGE ? mem_cols - 1 : 255;
				newc = old_image[offset] > j - newc ? j : old_image[offset] + newc;
			}
			else
				newc = old_image[offset] > newc ? old_image[offset] - newc : 0;

		}
		mem_img[mem_channel][offset] = newc;
	}
	/* RGB image channel */
	else
	{
		ofs3 = offset * 3;
		new_image = mem_img[CHN_IMAGE];

		i *= 3;
		nr = mem_col_pat24[i + 0];
		ng = mem_col_pat24[i + 1];
		nb = mem_col_pat24[i + 2];

		if (tint)
		{
			if (tint < 0)
			{
				nr = old_image[ofs3] > 255 - nr ? 255 : old_image[ofs3] + nr;
				ng = old_image[ofs3 + 1] > 255 - ng ? 255 : old_image[ofs3 + 1] + ng;
				nb = old_image[ofs3 + 2] > 255 - nb ? 255 : old_image[ofs3 + 2] + nb;
			}
			else
			{
				nr = old_image[ofs3] > nr ? old_image[ofs3] - nr : 0;
				ng = old_image[ofs3 + 1] > ng ? old_image[ofs3 + 1] - ng : 0;
				nb = old_image[ofs3 + 2] > nb ? old_image[ofs3 + 2] - nb : 0;
			}
		}

		if (opacity == 255)
		{
			new_image[ofs3] = nr;
			new_image[ofs3 + 1] = ng;
			new_image[ofs3 + 2] = nb;
		}
		else
		{
			r = old_image[ofs3];
			g = old_image[ofs3 + 1];
			b = old_image[ofs3 + 2];

			i = r * 255 + (nr - r) * opacity;
			new_image[ofs3] = (i + (i >> 8) + 1) >> 8;
			i = g * 255 + (ng - g) * opacity;
			new_image[ofs3 + 1] = (i + (i >> 8) + 1) >> 8;
			i = b * 255 + (nb - b) * opacity;
			new_image[ofs3 + 2] = (i + (i >> 8) + 1) >> 8;
		}
	}
}

void process_mask(int start, int step, int cnt, unsigned char *mask,
	unsigned char *alphar, unsigned char *alpha0, unsigned char *alpha,
	unsigned char *trans, int opacity)
{
	unsigned char newc, oldc;
	int i, j, k, tint;

	cnt = start + step * cnt;

	tint = tint_mode[0] ? 1 : 0;
	if ((tint_mode[2] == 1) || !(tint_mode[2] || tint_mode[1])) tint = -tint;

	/* Opacity mode */
	if (opacity)
	{
		for (i = start; i < cnt; i += step)
		{
			k = (255 - mask[i]) * opacity;
			if (!k)
			{
				mask[i] = 0;
				continue;
			}
			k = (k + (k >> 8) + 1) >> 8;

			if (trans)
			{
				/* Have transparency mask */
				k *= trans[i];
				k = (k + (k >> 8) + 1) >> 8;
			}
			mask[i] = k;

			if (!alpha || !k) continue;
			/* Have alpha channel - process it */
			newc = alpha[i];
			oldc = alpha0[i];
			if (tint)
			{
				if (tint < 0) newc = oldc > 255 - newc ?
					255 : oldc + newc;
				else newc = oldc > newc ? oldc - newc : 0;
			}
			j = oldc * 255 + (newc - oldc) * k;
			alphar[i] = (j + (j >> 8) + 1) >> 8;
			if (j) mask[i] = (255 * k * newc) / j;
		}
	}

	/* Indexed mode with transparency mask and/or alpha */
	else if (trans || alpha)
	{
		for (i = start; i < cnt; i += step)
		{
			if (trans) mask[i] |= trans[i] ^ 255;
			if (!alpha || mask[i]) continue;
			/* Have alpha channel - process it */
			newc = alpha[i];
			if (tint)
			{
				oldc = alpha0[i];
				if (tint < 0) newc = oldc > 255 - newc ?
					255 : oldc + newc;
				else newc = oldc > newc ? oldc - newc : 0;
			}
			alphar[i] = newc;
		}
	}
}

void process_img(int start, int step, int cnt, unsigned char *mask,
	unsigned char *imgr, unsigned char *img0, unsigned char *img,
	int opacity)
{
	unsigned char newc, oldc;
	unsigned char r, g, b, nr, ng, nb;
	int i, j, ofs3, tint;

	cnt = start + step * cnt;

	tint = tint_mode[0] ? 1 : 0;
	if ((tint_mode[2] == 1) || !(tint_mode[2] || tint_mode[1])) tint = -tint;

	/* Indexed image or utility channel */
	if (!opacity)
	{
		for (i = start; i < cnt; i += step)
		{
			if (mask[i]) continue;
			newc = img[i];
			if (tint)
			{
				oldc = img0[i];
				if (tint < 0) newc = oldc >= mem_cols - newc ?
					mem_cols - 1 : oldc + newc;
				else newc = oldc > newc ? oldc - newc : 0;
			}
			imgr[i] = newc;
		}
	}

	/* RGB image */
	else
	{
		for (i = start; i < cnt; i += step)
		{
			opacity = mask[i];
			if (!opacity) continue;
			ofs3 = i * 3;
			nr = img[ofs3 + 0];
			ng = img[ofs3 + 1];
			nb = img[ofs3 + 2];
			if (tint)
			{
				r = img0[ofs3 + 0];
				g = img0[ofs3 + 1];
				b = img0[ofs3 + 2];
				if (tint < 0)
				{
					nr = r > 255 - nr ? 255 : r + nr;
					ng = g > 255 - ng ? 255 : g + ng;
					nb = b > 255 - nb ? 255 : b + nb;
				}
				else
				{
					nr = r > nr ? r - nr : 0;
					ng = g > ng ? g - ng : 0;
					nb = b > nb ? b - nb : 0;
				}
			}
			if (opacity == 255)
			{
				imgr[ofs3 + 0] = nr;
				imgr[ofs3 + 1] = ng;
				imgr[ofs3 + 2] = nb;
				continue;
			}
			r = img0[ofs3 + 0];
			g = img0[ofs3 + 1];
			b = img0[ofs3 + 2];
			j = r * 255 + (nr - r) * opacity;
			imgr[ofs3 + 0] = (j + (j >> 8) + 1) >> 8;
			j = g * 255 + (ng - g) * opacity;
			imgr[ofs3 + 1] = (j + (j >> 8) + 1) >> 8;
			j = b * 255 + (nb - b) * opacity;
			imgr[ofs3 + 2] = (j + (j >> 8) + 1) >> 8;
		}
	}	
}

/* Separate function for faster paste */
void paste_pixels(int x, int y, int len, unsigned char *mask, unsigned char *img,
	unsigned char *alpha, unsigned char *trans, int opacity)
{
	unsigned char *old_image, *old_alpha = NULL, *dest = NULL;
	int bpp, ofs = x + mem_width * y;

	bpp = MEM_BPP;

	/* Setup opacity mode */
	if ((mem_channel > CHN_ALPHA) || (mem_img_bpp == 1)) opacity = 0;

	/* Alpha channel is special */
	if (mem_channel == CHN_ALPHA)
	{
		alpha = img;
		img = NULL;
	}

	/* Prepare alpha */
	if (!mem_img[CHN_ALPHA]) alpha = NULL;
	if (alpha)
	{
		if (mem_undo_opacity) old_alpha = mem_undo_previous(CHN_ALPHA);
		else old_alpha = mem_img[CHN_ALPHA];
		old_alpha += ofs;
		dest = mem_img[CHN_ALPHA] + ofs;
	}

	process_mask(0, 1, len, mask, dest, old_alpha, alpha, trans, opacity);

	/* Stop if we have alpha without image */
	if (!img) return;

	if (mem_undo_opacity) old_image = mem_undo_previous(mem_channel);
	else old_image = mem_img[mem_channel];
	old_image += ofs * bpp;
	dest = mem_img[mem_channel] + ofs * bpp;

	process_img(0, 1, len, mask, dest, old_image, img, opacity);
}

int png_cmp( png_color a, png_color b )			// Compare 2 colours
{
	if ( a.red == b.red && a.green == b.green && a.blue == b.blue ) return 0;
	else return -1;
			// Return TRUE if different
}

int mem_count_all_cols()				// Count all colours - Using main image
{
	return mem_count_all_cols_real(mem_img[CHN_IMAGE], mem_width, mem_height);
}

int mem_count_all_cols_real(unsigned char *im, int w, int h)	// Count all colours - very memory greedy
{
	unsigned char *tab;
	int i, j, k, ix;

	j = 0x200000;
	tab = malloc(j);			// HUGE colour cube
	if (!tab) return -1;			// Not enough memory Mr Greedy ;-)

	memset(tab, 0, j);			// Flush table

	k = w * h;
	for (i = 0; i < k; i++)			// Scan each pixel
	{
		ix = (im[0] >> 3) + (im[1] << 5) + (im[2] << 13);
		tab[ix] |= 1 << (im[0] & 7);
		im += 3;
	}

	k = 0;
	for (i = 0; i < j; i++)			// Count each colour
	{
		ix = tab[i];
		ix = (ix & 0x55) + ((ix & 0xAA) >> 1);
		ix = (ix & 0x33) + ((ix & 0xCC) >> 2);
		k += (ix & 0xF) + (ix >> 4);
	}

	free(tab);

	return k;
}

int mem_cols_used(int max_count)			// Count colours used in main RGB image
{
	if ( mem_img_bpp == 1 ) return -1;			// RGB only

	return (mem_cols_used_real(mem_img[CHN_IMAGE], mem_width, mem_height,
		max_count, 1));
}

void mem_cols_found_dl(unsigned char userpal[3][256])		// Convert results ready for DL code
{
	int i;

	for (i = 0; i < 256; i++)
	{
		userpal[0][i] = found[i * 3];
		userpal[1][i] = found[i * 3 + 1];
		userpal[2][i] = found[i * 3 + 2];
	}
}

int mem_cols_used_real(unsigned char *im, int w, int h, int max_count, int prog)
			// Count colours used in RGB chunk
{
	int i, j = w * h * 3, k, res, pix;

	max_count *= 3;
	found[0] = im[0];
	found[1] = im[1];
	found[2] = im[2];
	if (prog) progress_init(_("Counting Unique RGB Pixels"), 0);
	for (i = res = 3; (i < j) && (res < max_count); i += 3)	// Skim all pixels
	{
		pix = MEM_2_INT(im, i);
		for (k = 0; k < res; k += 3)
		{
			if (MEM_2_INT(found, k) == pix) break;
		}
		if (k >= res)	// New colour so add to list
		{
			found[res] = im[i];
			found[res + 1] = im[i + 1];
			found[res + 2] = im[i + 2];
			res += 3;
			if (!prog || (res & 15)) continue;
			if (progress_update((float)res / max_count)) break;
		}
	}
	if (prog) progress_end();

	return (res / 3);
}


////	EFFECTS


void do_effect( int type, int param )		// 0=edge detect 1=UNUSED 2=emboss
{
	unsigned char *src, *dest, *tmp = "\0", *mask = NULL;
	int i, j, k = 0, ix, bpp, ll, dxp1, dxm1, dyp1, dym1;
	int op, md, ms;
	double blur = (double)param / 200.0;

	bpp = MEM_BPP;
	ll = mem_width * bpp;
	ms = bpp == 3 ? 1 : 4;

	src = mem_undo_previous(mem_channel);
	dest = mem_img[mem_channel];
	mask = malloc(mem_width);
	if (!mask)
	{
		memory_errors(1);
		return;
	}
	progress_init(_("Applying Effect"), 1);

	for (ix = i = 0; i < mem_height; i++)
	{
		if (mask) row_protected(0, i, mem_width, tmp = mask);
		dyp1 = i < mem_height - 1 ? ll : -ll;
		dym1 = i ? -ll : ll;
		for (md = j = 0; j < ll; j++ , ix++)
		{
			op = *tmp;
			/* One step for 1 or 3 bytes */
			md += ms + (md >> 1);
			tmp += md >> 2;
			md &= 3;
			if (op == 255) continue;
			dxp1 = j < ll - bpp ? bpp : -bpp;
			dxm1 = j >= bpp ? -bpp : bpp;
			switch (type)
			{
			case 0:	/* Edge detect */
				k = src[ix];
				k = abs(k - src[ix + dym1]) + abs(k - src[ix + dyp1]) +
					abs(k - src[ix + dxm1]) + abs(k - src[ix + dxp1]);
				break;
			case 2:	/* Emboss */
				k = src[ix + dym1] + src[ix + dxm1] +
					src[ix + dxm1 + dym1] + src[ix + dxp1 + dym1];
				k = k / 4 - src[ix] + 127;
				break;
			case 3:	/* Edge sharpen */
				k = src[ix + dym1] + src[ix + dyp1] +
					src[ix + dxm1] + src[ix + dxp1] - 4 * src[ix];
				k = src[ix] - blur * k;
				break;
			case 4:	/* Edge soften */
				k = src[ix + dym1] + src[ix + dyp1] +
					src[ix + dxm1] + src[ix + dxp1] - 4 * src[ix];
				k = src[ix] + (5 * k) / (125 - param);
				break;
			}
			k = k < 0 ? 0 : k > 0xFF ? 0xFF : k;
			k = 255 * k + (src[ix] - k) * op;
			dest[ix] = (k + (k >> 8) + 1) >> 8;
		}
		if ((type != 1) && ((i * 10) % mem_height >= mem_height - 10))
			if (progress_update((float)(i + 1) / mem_height)) break;
	}
	free(mask);
	progress_end();
}

/* Most-used variables are local to inner blocks to shorten their live ranges -
 * otherwise stupid compilers might allocate them to memory */
static void gauss_filter(double *gaussX, int lenX, int lenY, int *idx,
	unsigned char *mask, int channel, int gcor)
{
	int i, wid, mh2, bpp;
	double sum, sum1, sum2, *temp, *gaussY;
	unsigned char *src0, *src1, *chan, *dest;

	bpp = BPP(channel);
	wid = mem_width * bpp;
	gaussY = gaussX + lenX;
	temp = gaussY + lenY;
	chan = mem_undo_previous(channel);
	mh2 = mem_height > 1 ? 2 * mem_height - 2 : 1;
	for (i = 0; i < mem_height; i++)
	{
		/* Apply vertical filter */
		{
			int j, k;

			src0 = chan + i * wid;
			if (gcor) /* Gamma-correct RGB values */
			{
				for (j = 0; j < wid; j++)
				{
					temp[j] = gamma256[src0[j]] * gaussY[0];
				}
			}
			else /* Leave RGB values as they were */
			{
				for (j = 0; j < wid; j++)
				{
					temp[j] = src0[j] * gaussY[0];
				}
			}
			for (j = 1; j < lenY; j++)
			{
				k = (i + j) % mh2;
				if (k >= mem_height) k = mh2 - k;
				src0 = chan + k * wid;
				k = abs(i - j) % mh2;
				if (k >= mem_height) k = mh2 - k;
				src1 = chan + k * wid;
				if (gcor) /* Gamma-correct */
				{
					for (k = 0; k < wid; k++)
					{
						temp[k] += (gamma256[src0[k]] +
							gamma256[src1[k]]) * gaussY[j];
					}
				}
				else /* Leave alone */
				{
					for (k = 0; k < wid; k++)
					{
						temp[k] += (src0[k] + src1[k]) * gaussY[j];
					}
				}
			}
		}
		row_protected(0, i, mem_width, mask);
		dest = mem_img[channel] + i * wid;
		if (bpp == 3) /* Run 3-bpp horizontal filter */
		{
			int j, jj, k, k1, k2, x1, x2;

			for (j = jj = 0; jj < mem_width; jj++ , j += 3)
			{
				if (mask[jj] == 255) continue;
				sum = temp[j] * gaussX[0];
				sum1 = temp[j + 1] * gaussX[0];
				sum2 = temp[j + 2] * gaussX[0];
				for (k = 1; k < lenX; k++)
				{
					x1 = idx[jj - k] * 3;
					x2 = idx[jj + k] * 3;
					sum += (temp[x1] + temp[x2]) * gaussX[k];
					sum1 += (temp[x1 + 1] + temp[x2 + 1]) * gaussX[k];
					sum2 += (temp[x1 + 2] + temp[x2 + 2]) * gaussX[k];
				}
				if (gcor) /* Reverse gamma correction */
				{
					k = UNGAMMA256(sum);
					k1 = UNGAMMA256(sum1);
					k2 = UNGAMMA256(sum2);
				}
				else /* Simply round to nearest */
				{
					k = rint(sum);
					k1 = rint(sum1);
					k2 = rint(sum2);
				}
				k = k * 255 + (dest[j] - k) * mask[jj];
				dest[j] = (k + (k >> 8) + 1) >> 8;
				k1 = k1 * 255 + (dest[j + 1] - k1) * mask[jj];
				dest[j + 1] = (k1 + (k1 >> 8) + 1) >> 8;
				k2 = k2 * 255 + (dest[j + 2] - k2) * mask[jj];
				dest[j + 2] = (k2 + (k2 >> 8) + 1) >> 8;
			}
		}
		else /* Run 1-bpp horizontal filter - no gamma here */
		{
			int j, k;

			for (j = 0; j < mem_width; j++)
			{
				if (mask[j] == 255) continue;
				sum = temp[j] * gaussX[0];
				for (k = 1; k < lenX; k++)
				{
					sum += (temp[idx[j - k]] +
						temp[idx[j + k]]) * gaussX[k];
				}
				k = rint(sum);
				k = k * 255 + (dest[j] - k) * mask[j];
				dest[j] = (k + (k >> 8) + 1) >> 8;
			}
		}
		if ((i * 10) % mem_height >= mem_height - 10)
			if (progress_update((float)(i + 1) / mem_height)) break;
	}
}

/* While slower, and rather complex and memory hungry, this is the only way
 * to do *PRECISE* RGBA-coupled Gaussian blur */
static void gauss_filter_rgba(double *gaussX, int lenX, int lenY, int *idx,
	unsigned char *mask, unsigned char **buf, int gcor)
{
	int i, j, k, mh2, len, slide;
	double sum, sum1, sum2, mult, *temp, *tmpa, *atmp, *src, *gaussY;
	unsigned char *src0, *src1, *chan, *dest;
	unsigned char *alf0, *alf1, *alpha, *dsta;
	unsigned char *tm0, *tm1;
	double *td0, *td1;
	unsigned short *ts0, *ts1;

	chan = mem_undo_previous(CHN_IMAGE);
	alpha = mem_undo_previous(CHN_ALPHA);
	mh2 = mem_height > 1 ? 2 * mem_height - 2 : 1;

	/* Set up the premultiplied row buffer */
	tm0 = (void *)(buf + (mem_height + 2 * lenY - 2));
	if (gcor) tm0 = ALIGNTO(tm0, double);
	len = mem_width * 3 * (gcor ? sizeof(double) : sizeof(short));
	slide = mem_height >= 2 * lenY;

	if (slide) /* Buffer slides over image */
	{
		j = 2 * lenY - 1;
		for (i = 0; i < mem_height + j - 1; i++)
		{
			buf[i] = tm0 + (i % j) * len;
		}
		buf += lenY - 1;
		k = mem_width * lenY;
	}
	else /* Image fits into buffer */
	{
		buf += lenY - 1;
		for (i = -lenY + 1; i < mem_height + lenY - 1; i++)
		{
			j = abs(i) % mh2;
			if (j >= mem_height) j = mh2 - j;
			buf[i] = tm0 + j * len;
		}
		k = mem_width * mem_height;
	}
	if (gcor) /* Gamma correct */
	{
		td0 = (void *)buf[0];
		for (i = j = 0; i < k; i++ , j += 3)
		{
			td0[j] = gamma256[chan[j]] * alpha[i];
			td0[j + 1] = gamma256[chan[j + 1]] * alpha[i];
			td0[j + 2] = gamma256[chan[j + 2]] * alpha[i];
		}
	}
	else /* Use as is */
	{
		ts0 = (void *)buf[0];
		for (i = j = 0; i < k; i++ , j += 3)
		{
			ts0[j] = chan[j] * alpha[i];
			ts0[j + 1] = chan[j + 1] * alpha[i];
			ts0[j + 2] = chan[j + 2] * alpha[i];
		}
	}
	if (slide) /* Mirror image rows */
	{
		for (i = 1; i < lenY - 1; i++)
		{
			memcpy(buf[-i], buf[i], len);
		}
	}

	/* Set up the main row buffer and process the image */
	gaussY = gaussX + lenX;
	temp = gaussY + lenY;
	tmpa = temp + mem_width * 3;
	atmp = tmpa + mem_width * 3;
	for (i = 0; i < mem_height; i++)
	{
		/* Premultiply a new row */
		if (slide)
		{
			int j, k;

			k = i + lenY - 1;
			if ((k %= mh2) >= mem_height) k = mh2 - k;
			alf0 = alpha + k * mem_width;
			src0 = chan + k * mem_width * 3;
			if (gcor) /* Gamma correct */
			{
				td0 = (void *)buf[k];
				for (j = k = 0; j < mem_width; j++ , k += 3)
				{
					td0[k] = gamma256[src0[k]] * alf0[j];
					td0[k + 1] = gamma256[src0[k + 1]] * alf0[j];
					td0[k + 2] = gamma256[src0[k + 2]] * alf0[j];
				}
			}
			else /* Use as is */
			{
				ts0 = (void *)buf[k];
				for (j = k = 0; j < mem_width; j++ , k += 3)
				{
					ts0[k] = src0[k] * alf0[j];
					ts0[k + 1] = src0[k + 1] * alf0[j];
					ts0[k + 2] = src0[k + 2] * alf0[j];
				}
			}
		}
		/* Apply vertical filter */
		{
			int j, jj, k, kk;

			alf0 = alpha + i * mem_width;
			src0 = chan + i * mem_width * 3;
			if (gcor) /* Gamma correct */
			{
				td0 = (void *)buf[i];
				for (j = jj = 0; j < mem_width; j++ , jj += 3)
				{
					atmp[j] = alf0[j] * gaussY[0];
					temp[jj] = gamma256[src0[jj]] * gaussY[0];
					temp[jj + 1] = gamma256[src0[jj + 1]] * gaussY[0];
					temp[jj + 2] = gamma256[src0[jj + 2]] * gaussY[0];
					tmpa[jj] = td0[jj] * gaussY[0];
					tmpa[jj + 1] = td0[jj + 1] * gaussY[0];
					tmpa[jj + 2] = td0[jj + 2] * gaussY[0];
				}
			}
			else /* Use as is */
			{
				ts0 = (void *)buf[i];
				for (j = jj = 0; j < mem_width; j++ , jj += 3)
				{
					atmp[j] = alf0[j] * gaussY[0];
					temp[jj] = src0[jj] * gaussY[0];
					temp[jj + 1] = src0[jj + 1] * gaussY[0];
					temp[jj + 2] = src0[jj + 2] * gaussY[0];
					tmpa[jj] = ts0[jj] * gaussY[0];
					tmpa[jj + 1] = ts0[jj + 1] * gaussY[0];
					tmpa[jj + 2] = ts0[jj + 2] * gaussY[0];
				}
			}
			for (j = 1; j < lenY; j++)
			{
				tm0 = buf[i + j];
				k = (i + j) % mh2;
				if (k >= mem_height) k = mh2 - k;
				alf0 = alpha + k * mem_width;
				src0 = chan + k * mem_width * 3;
				tm1 = buf[i - j];
				k = abs(i - j) % mh2;
				if (k >= mem_height) k = mh2 - k;
				alf1 = alpha + k * mem_width;
				src1 = chan + k * mem_width * 3;
				if (gcor) /* Gamma correct */
				{
					td0 = (void *)tm0;
					td1 = (void *)tm1;
					for (k = kk = 0; k < mem_width; k++ , kk += 3)
					{
						atmp[k] += (alf0[k] + alf1[k]) * gaussY[j];
						temp[kk] += (gamma256[src0[kk]] + gamma256[src1[kk]]) * gaussY[j];
						temp[kk + 1] += (gamma256[src0[kk + 1]] + gamma256[src1[kk + 1]]) * gaussY[j];
						temp[kk + 2] += (gamma256[src0[kk + 2]] + gamma256[src1[kk + 2]]) * gaussY[j];
						tmpa[kk] += (td0[kk] + td1[kk]) * gaussY[j];
						tmpa[kk + 1] += (td0[kk + 1] + td1[kk + 1]) * gaussY[j];
						tmpa[kk + 2] += (td0[kk + 2] + td1[kk + 2]) * gaussY[j];
					}
				}
				else /* Use as is */
				{
					ts0 = (void *)tm0;
					ts1 = (void *)tm1;
					for (k = kk = 0; k < mem_width; k++ , kk += 3)
					{
						atmp[k] += (alf0[k] + alf1[k]) * gaussY[j];
						temp[kk] += (src0[kk] + src1[kk]) * gaussY[j];
						temp[kk + 1] += (src0[kk + 1] + src1[kk + 1]) * gaussY[j];
						temp[kk + 2] += (src0[kk + 2] + src1[kk + 2]) * gaussY[j];
						tmpa[kk] += (ts0[kk] + ts1[kk]) * gaussY[j];
						tmpa[kk + 1] += (ts0[kk + 1] + ts1[kk + 1]) * gaussY[j];
						tmpa[kk + 2] += (ts0[kk + 2] + ts1[kk + 2]) * gaussY[j];
					}
				}
			}
		}
		row_protected(0, i, mem_width, mask);
		dest = mem_img[CHN_IMAGE] + i * mem_width * 3;
		dsta = mem_img[CHN_ALPHA] + i * mem_width;
		/* Horizontal RGBA filter */
		{
			int j, jj, k, k1, k2, kk, x1, x2;

			for (j = jj = 0; j < mem_width; j++ , jj += 3)
			{
				if (mask[j] == 255) continue;
				sum = atmp[j] * gaussX[0];
				for (k = 1; k < lenX; k++)
				{
					sum += (atmp[idx[j - k]] + atmp[idx[j + k]]) * gaussX[k];
				}
				kk = mask[j];
				k = rint(sum);
				src = temp;
				mult = 1.0;
				if (k)
				{
					src = tmpa;
					mult /= sum;
				}
				k = k * 255 + (dsta[j] - k) * kk;
				if (k) kk = (255 * kk * dsta[j]) / k;
				dsta[j] = (k + (k >> 8) + 1) >> 8;
				sum = src[jj] * gaussX[0];
				sum1 = src[jj + 1] * gaussX[0];
				sum2 = src[jj + 2] * gaussX[0];
				for (k = 1; k < lenX; k++)
				{
					x1 = idx[j - k] * 3;
					x2 = idx[j + k] * 3;
					sum += (src[x1] + src[x2]) * gaussX[k];
					sum1 += (src[x1 + 1] + src[x2 + 1]) * gaussX[k];
					sum2 += (src[x1 + 2] + src[x2 + 2]) * gaussX[k];
				}
				if (gcor) /* Reverse gamma correction */
				{
					k = UNGAMMA256(sum * mult);
					k1 = UNGAMMA256(sum1 * mult);
					k2 = UNGAMMA256(sum2 * mult);
				}
				else /* Simply round to nearest */
				{
					k = rint(sum * mult);
					k1 = rint(sum1 * mult);
					k2 = rint(sum2 * mult);
				}
				k = k * 255 + (dest[jj] - k) * kk;
				dest[jj] = (k + (k >> 8) + 1) >> 8;
				k1 = k1 * 255 + (dest[jj + 1] - k1) * kk;
				dest[jj + 1] = (k1 + (k1 >> 8) + 1) >> 8;
				k2 = k2 * 255 + (dest[jj + 2] - k2) * kk;
				dest[jj + 2] = (k2 + (k2 >> 8) + 1) >> 8;
			}
		}
		if ((i * 10) % mem_height >= mem_height - 10)
			if (progress_update((float)(i + 1) / mem_height)) break;
	}
}

/* Gaussian blur */
void mem_gauss(double radiusX, double radiusY, int gcor)
{
	int i, j, k, l, lenX, lenY, rgba, rgbb, *idxx, *idx;
	double exkX, exkY, sum, *tmp, *gaussX, *gaussY;
	unsigned char *mask, **abuf = NULL;

	/* Cutoff point is where gaussian becomes < 1/255 */
	lenX = ceil(radiusX) + 2;
	lenY = ceil(radiusY) + 2;
	exkX = -log(255.0) / ((radiusX + 1.0) * (radiusX + 1.0));
	exkY = -log(255.0) / ((radiusY + 1.0) * (radiusY + 1.0));

	/* RGBA or not? */
	rgba = (mem_channel == CHN_IMAGE) && mem_img[CHN_ALPHA] && RGBA_mode;
	rgbb = rgba && !channel_dis[CHN_ALPHA];

	/* Allocate memory */
	if (rgbb) /* Cyclic buffer for premultiplied RGB + extra linebuffer */
	{
		i = mem_height + 2 * (lenY - 1); // row pointers
		j = (mem_height < i ? mem_height : i) * mem_width * 3; // data
		/* Gamma corrected - allocate doubles for buffer */
		if (gcor) k = i * sizeof(double *) + (j + 1) * sizeof(double);
		/* No gamma - shorts are enough */
		else k = i * sizeof(short *) + j * sizeof(short);
		abuf = malloc(k);
		if (!abuf) return;
		i = mem_width * 7 + lenX + lenY + 1;
	}
	else i = mem_width * MEM_BPP + lenX + lenY + 1;
	tmp = malloc(i * sizeof(double));
	i = mem_width + 2 * (lenX - 1);
	idxx = calloc(i, sizeof(int));
	mask = malloc(mem_width);
	if (!tmp || !idxx || !mask)
	{
		free(abuf);
		free(tmp);
		free(idxx);
		free(mask);
		return;
	}
	gaussX = ALIGNTO(tmp, double);

	/* Prepare filters */
	j = lenX; gaussY = gaussX;
	while (1)
	{
		sum = gaussY[0] = 1.0;
		for (i = 1; i < j; i++)
		{
			sum += 2.0 * (gaussY[i] = exp((double)(i * i) * exkX));
		}
		sum = 1.0 / sum;
		for (i = 0; i < j; i++)
		{
			gaussY[i] *= sum;
		}
		if (gaussY != gaussX) break;
		exkX = exkY; j = lenY; gaussY = gaussX + lenX;
	}

	/* Prepare horizontal indices, assuming mirror boundary */
	l = lenX - 1;
	idx = idxx + l; // To simplify addressing
	if (mem_width > 1) // Else don't run horizontal pass
	{
		k = 2 * mem_width - 2;
		for (i = -l; i < mem_width + l; i++)
		{
			j = abs(i) % k;
			idx[i] = j < mem_width ? j : k - j;
		}
	}

	/* Run filter */
	progress_init(_("Gaussian Blur"), 1);
	progress_update(0.0);
	if (!rgba) /* One channel */
		gauss_filter(gaussX, lenX, lenY, idx, mask, mem_channel, gcor);
	else if (rgbb) /* Coupled RGBA */
		gauss_filter_rgba(gaussX, lenX, lenY, idx, mask, abuf, gcor);
	else /* RGB and alpha */
	{
		gauss_filter(gaussX, lenX, lenY, idx, mask, CHN_IMAGE, gcor);
		gauss_filter(gaussX, lenX, lenY, idx, mask, CHN_ALPHA, FALSE);
	}
	progress_end();
	free(abuf);
	free(tmp);
	free(idxx);
	free(mask);
}	

///	CLIPBOARD MASK

int mem_clip_mask_init(unsigned char val)		// Initialise the clipboard mask
{
	int j = mem_clip_w*mem_clip_h;

	if ( mem_clipboard != NULL ) mem_clip_mask_clear();	// Remove old mask

	mem_clip_mask = malloc(j);
	if (!mem_clip_mask) return 1;			// Not able to allocate memory

	memset(mem_clip_mask, val, j);		// Start with fully opaque/clear mask

	return 0;
}

void mem_clip_mask_set(unsigned char val)		// (un)Mask colours A and B on the clipboard
{
	int i, j = mem_clip_w * mem_clip_h, k, aa, bb;

	if ( mem_clip_bpp == 1 )
	{
		if (mem_channel == CHN_IMAGE)
		{
			aa = mem_col_A;
			bb = mem_col_B;
		}
		else
		{
			aa = channel_col_A[mem_channel];
			bb = channel_col_B[mem_channel];
		}
		for ( i=0; i<j; i++ )
		{
			if ( mem_clipboard[i] == aa || mem_clipboard[i] == bb )
				mem_clip_mask[i] = val;
		}
	}
	if ( mem_clip_bpp == 3 )
	{
		aa = PNG_2_INT(mem_col_A24);
		bb = PNG_2_INT(mem_col_B24);
		for ( i=0; i<j; i++ )
		{
			k = MEM_2_INT(mem_clipboard, i * 3);
			if ((k == aa) || (k == bb))
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

/*
 * Extract alpha information from RGB image - alpha if pixel is in colour
 * scale of A->B. Return 0 if OK, 1 otherwise
 */
int mem_scale_alpha(unsigned char *img, unsigned char *alpha,
	int width, int height, int mode)
{
	int i, j = width * height, AA[3], BB[3], DD[6], chan, c1, c2, dc1, dc2;
	double p0, p1, p2, dchan, KK[6];

	if (!img || !alpha) return (1);

	AA[0] = mem_col_A24.red;
	AA[1] = mem_col_A24.green;
	AA[2] = mem_col_A24.blue;
	BB[0] = mem_col_B24.red;
	BB[1] = mem_col_B24.green;
	BB[2] = mem_col_B24.blue;
	for (i = 0; i < 3; i++)
	{
		if (AA[i] < BB[i])
		{
			DD[i] = AA[i];
			DD[i + 3] = BB[i];
		}
		else
		{
			DD[i] = BB[i];
			DD[i + 3] = AA[i];
		}
	}

	chan = 0;	// Find the channel with the widest range - gives most accurate result later
	if (DD[4] - DD[1] > DD[3] - DD[0]) chan = 1;
	if (DD[5] - DD[2] > DD[chan + 3] - DD[chan]) chan = 2;

	if (AA[chan] == BB[chan])	/* if A == B then work GIMP-like way */
	{
		for (i = 0; i < 3; i++)
		{
			KK[i] = AA[i] ? 255.0 / AA[i] : 1.0;
			KK[i + 3] = AA[i] < 255 ? -255.0 / (255 - AA[i]) : 0.0;
		}

		for (i = 0; i < j; i++ , alpha++ , img += 3)
		{
			/* Already semi-opaque so don't touch */
			if (*alpha != 255) continue;

			/* Evaluate the three possible alphas */
			p0 = (AA[0] - img[0]) * (img[0] <= AA[0] ? KK[0] : KK[3]);
			p1 = (AA[1] - img[1]) * (img[1] <= AA[1] ? KK[1] : KK[4]);
			p2 = (AA[2] - img[2]) * (img[2] <= AA[2] ? KK[2] : KK[5]);
			if (p0 < p1) p0 = p1;
			if (p0 < p2) p0 = p2;

			/* Set alpha */
			*alpha = rint(p0);

			/* Demultiply image if this is alpha and nonzero */
			if (!mode) continue;
			dchan = p0 ? 255.0 / p0 : 0.0;
			img[0] = rint((img[0] - AA[0]) * dchan) + AA[0];
			img[1] = rint((img[1] - AA[1]) * dchan) + AA[1];
			img[2] = rint((img[2] - AA[2]) * dchan) + AA[2];
		}
	}
	else	/* Limit processing to A->B scale */
	{
		dchan = 1.0 / (BB[chan] - AA[chan]);
		c1 = 1 ^ (chan & 1);
		c2 = 2 ^ (chan & 2);
		dc1 = BB[c1] - AA[c1];
		dc2 = BB[c2] - AA[c2];

		for (i = 0; i < j; i++ , alpha++ , img += 3)
		{
			/* Already semi-opaque so don't touch */
			if (*alpha != 255) continue;
			/* Ensure pixel lies between A and B for each channel */
			if ((img[0] < DD[0]) || (img[0] > DD[3])) continue;
			if ((img[1] < DD[1]) || (img[1] > DD[4])) continue;
			if ((img[2] < DD[2]) || (img[2] > DD[5])) continue;

			p0 = (img[chan] - AA[chan]) * dchan;

			/* Check delta for all channels is roughly the same ...
			 * ... if it isn't, ignore this pixel as its not in A->B scale
			 */
			if (abs(AA[c1] + (int)rint(p0 * dc1) - img[c1]) > 2) continue;
			if (abs(AA[c2] + (int)rint(p0 * dc2) - img[c2]) > 2) continue;

			/* Pixel is a shade of A/B so set alpha */
			*alpha = (int)rint(p0 * 255) ^ 255;

			/* Demultiply image if this is alpha */
			if (!mode) continue;
			img[0] = AA[0];
			img[1] = AA[1];
			img[2] = AA[2];
		}
	}

	return 0;
}

void do_clone(int ox, int oy, int nx, int ny, int opacity, int mode)
{
	unsigned char *src, *dest, *srca = NULL, *dsta = NULL;
	int ax = ox - tool_size/2, ay = oy - tool_size/2, w = tool_size, h = tool_size;
	int xv = nx - ox, yv = ny - oy;		// Vector
	int i, j, k, rx, ry, offs, delta, delta1, bpp;
	int x0, x1, dx, y0, y1, dy, opw, op2;

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

	if ((w < 1) || (h < 1)) return;

/* !!! I modified this tool action somewhat - White Jaguar */
	if (mode) src = mem_undo_previous(mem_channel);
	else src = mem_img[mem_channel];
	dest = mem_img[mem_channel];
	if ((mem_channel == CHN_IMAGE) && RGBA_mode && mem_img[CHN_ALPHA])
	{
		if (mode) srca = mem_undo_previous(CHN_ALPHA);
		else srca = mem_img[CHN_ALPHA];
		dsta = mem_img[CHN_ALPHA];
	}
	bpp = MEM_BPP;
	delta1 = yv * mem_width + xv;
	delta = delta1 * bpp;

	if (xv > 0)
	{
		x0 = w - 1; x1 = -1; dx = -1;
	}
	else
	{
		x0 = 0; x1 = w; dx = 1;
	}
	if (yv > 0)
	{
		y0 = h - 1; y1 = -1; dy = -1;
	}
	else
	{
		y0 = 0; y1 = h; dy = 1;
	}

	for (j = y0; j != y1; j += dy)	// Blend old area with new area
	{
		ry = ay + yv + j;
		if ((ry < 0) || (ry >= mem_height)) continue;
		for (i = x0; i != x1; i += dx)
		{
			rx = ax + xv + i;
			if ((rx < 0) || (rx >= mem_width)) continue;
			k = pixel_protected(rx, ry);
			offs = mem_width * ry + rx;
			if (!opacity)
			{
				if (k) continue;
				dest[offs] = src[offs - delta];
				if (!dsta) continue;
				dsta[offs] = srca[offs - delta];
				continue;
			}
			opw = (255 - k) * opacity;
			if (opw < 255) continue;
			opw = (opw + (opw >> 8) + 1) >> 8;
			if (dsta)
			{
				k = srca[offs];
				k = k * 255 + (srca[offs - delta1] - k) * opw;
				dsta[offs] = (k + (k >> 8) + 1) >> 8;
				if (k && !channel_dis[CHN_ALPHA])
					opw = (255 * opw * srca[offs - delta1]) / k;
			}
			op2 = 255 - opw;
			offs *= bpp;
			k = src[offs - delta] * opw + src[offs] * op2;
			dest[offs] = (k + (k >> 8) + 1) >> 8;
			if (bpp == 1) continue;
			offs++;
			k = src[offs - delta] * opw + src[offs] * op2;
			dest[offs] = (k + (k >> 8) + 1) >> 8;
			offs++;
			k = src[offs - delta] * opw + src[offs] * op2;
			dest[offs] = (k + (k >> 8) + 1) >> 8;
		}
	}
}

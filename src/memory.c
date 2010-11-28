/*	memory.c
	Copyright (C) 2004-2010 Mark Tyler and Dmitry Groshev

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

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "layer.h"
#include "inifile.h"
#include "canvas.h"
#include "prefs.h"
#include "channels.h"
#include "toolbar.h"
#include "viewer.h"
#include "csel.h"
#include "thread.h"


grad_info gradient[NUM_CHANNELS];	// Per-channel gradients
double grad_path, grad_x0, grad_y0;	// Stroke gradient temporaries
grad_map graddata[NUM_CHANNELS + 1];	// RGB + per-channel gradient data
grad_store gradbytes;			// Storage space for custom gradients
int grad_opacity;			// Preview opacity

/// Vectorized low-level drawing functions

void (*put_pixel)(int x, int y) = put_pixel_def;
void (*put_pixel_row)(int x, int y, int len, unsigned char *xsel) = put_pixel_row_def;

#define ROW_BUFLEN 2048 /* Preferred length of internal row buffer */

/// Bayer ordered dithering

const unsigned char bayer[16] = {
	0x00, 0x40, 0x10, 0x50, 0x04, 0x44, 0x14, 0x54,
	0x01, 0x41, 0x11, 0x51, 0x05, 0x45, 0x15, 0x55 };

/// Tint tool - contributed by Dmitry Groshev, January 2006

int tint_mode[3];		// [0] = off/on, [1] = add/subtract, [2] = button (none, left, middle, right : 0-3)

int mem_cselect;
int mem_blend;
int mem_unmask;
int mem_gradient;

/// BLEND MODE SETTINGS

int blend_mode = BLEND_HUE;

/// FLOOD FILL SETTINGS

double flood_step;
int flood_cube, flood_img, flood_slide;

int smudge_mode;
int posterize_mode;	// bitwise/truncated/rounded

/// QUANTIZATION SETTINGS

int quan_sqrt;	// "Diameter based weighting" - use sqrt of pixel count

/// IMAGE

int mem_undo_depth = DEF_UNDO;		// Current undo depth
image_info mem_image;			// Current image
image_info mem_clip;			// Current clipboard
image_state mem_state;			// Current edit settings

int mem_background = 180;		// Non paintable area

unsigned char mem_brushes[PATCH_WIDTH * PATCH_HEIGHT * 3];
					// Preset brushes screen memory
int mem_clip_x = -1, mem_clip_y = -1;	// Clipboard location on canvas
int mem_nudge = -1;			// Nudge pixels per SHIFT+Arrow key during selection/paste

int mem_prev_bcsp[6];			// BR, CO, SA, POSTERIZE, Hue

/// UNDO ENGINE

#define TILE_SIZE 64
#define TILE_SHIFT 6
#define MAX_TILEMAP ((((MAX_WIDTH + TILE_SIZE - 1) / TILE_SIZE + 7) / 8) * \
	((MAX_HEIGHT + TILE_SIZE - 1) / TILE_SIZE))
#define UF_TILED 1
#define UF_FLAT  2
#define UF_SIZED 4
#define UF_ORIG  8 /* Unmodified state */

int mem_undo_limit;		// Max MB memory allocation limit
int mem_undo_common;		// Percent of undo space in common arena
int mem_undo_opacity;		// Use previous image for opacity calculations?

#define UNDO_STORESIZE 1023 /* Leave space for memory block header */

static undo_data *undo_datastore, *undo_freelist;
static int undo_freecnt;

/// PATTERNS

unsigned char mem_pattern[8 * 8];		// Original 0-1 pattern
unsigned char mem_col_pat[8 * 8];	// Indexed 8x8 colourised pattern using colours A & B
unsigned char mem_col_pat24[8 * 8 * 3];	// RGB 8x8 colourised pattern using colours A & B

/// TOOLS

tool_info tool_state = { TOOL_SQUARE, TOOL_SQUARE, { 1, 1, 255 } };
				// Type, brush, size/flow/opacity
int pen_down;			// Are we drawing? - Used to see if we need to do an UNDO
int tool_ox, tool_oy;		// Previous tool coords - used by continuous mode
int mem_continuous;		// Area we painting the static shapes continuously?

/// PREVIEW

int mem_brcosa_allow[3];	// BRCOSA RGB



/// PALETTE

unsigned char mem_pals[PALETTE_WIDTH * PALETTE_HEIGHT * 3];
				// RGB screen memory holding current palette
static int found[1024];		// Used by mem_cols_used() & mem_convert_indexed

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

{0,0,0}, {7,0,0}, {0,7,0}, {7,7,0}, {0,0,7}, {7,0,7}, {0,7,7}, {7,7,7},

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

/// FONT FOR PALETTE WINDOW

#define B8(A,B,C,D,E,F,G,H) (A|B<<1|C<<2|D<<3|E<<4|F<<5|G<<6|H<<7)

static unsigned char mem_cross[PALETTE_CROSS_H] = {
	B8( 1,1,0,0,0,0,1,1 ),
	B8( 1,1,1,0,0,1,1,1 ),
	B8( 0,1,1,1,1,1,1,0 ),
	B8( 0,0,1,1,1,1,0,0 ),
	B8( 0,0,1,1,1,1,0,0 ),
	B8( 0,1,1,1,1,1,1,0 ),
	B8( 1,1,1,0,0,1,1,1 ),
	B8( 1,1,0,0,0,0,1,1 )
};

#include "graphics/xbm_n7x7.xbm"
#if (PALETTE_DIGIT_W != xbm_n7x7_width) || (PALETTE_DIGIT_H * 10 != xbm_n7x7_height)
#error "Mismatched palette-window font"
#endif

/* While a number of unwieldy allocation APIs is provided by GLib, it's better
 * to do it the right way once, than constantly invent workarounds - WJ */

#define WJMEM_DEFINCR 16384
#define WJMEM_RESERVED 64
#define WJMEM_DEFSIZE (WJMEM_DEFINCR - WJMEM_RESERVED)

wjmem *wjmemnew(int minsize, int incr)
{
	wjmem *mem;

	if (incr <= 0) incr = WJMEM_DEFINCR;
	if (minsize <= sizeof(wjmem)) minsize = WJMEM_DEFSIZE;
	mem = calloc(1, minsize);
	if (mem)
	{
		mem->block = (char *)mem;
		mem->here = sizeof(wjmem);
		mem->size = mem->minsize = minsize;
		mem->incr = incr;
	}
	return (mem);
}

void wjmemfree(wjmem *mem)
{
	char *this, *next;

	for (this = mem->block; this != (char *)mem; this = next)
	{
		next = *(char **)this;
		free(this);
	}
	free(mem);
}

void *wjmalloc(wjmem *mem, int size, int align)
{
	char *dest;
	unsigned int sz, ds;

	align += !align; // 0 -> 1
	dest = mem->block + mem->here;
	dest = ALIGNED(dest, align);
	ds = dest - mem->block + size;
	if (ds > mem->size)
	{
		sz = mem->minsize;
		ds = sizeof(char *) + align + size;
		if (sz < ds)
		{
			ds += WJMEM_RESERVED + mem->incr - 1;
			sz = ds - ds % mem->incr - WJMEM_RESERVED;
		}
		dest = calloc(1, sz);
		if (!dest) return (NULL);
		*(char **)dest = mem->block;
		mem->block = dest;
		mem->size = sz;
		dest += sizeof(char *);
		dest = ALIGNED(dest, align);
		ds = dest - mem->block + size;
	}
	mem->here = ds;
	return ((void *)dest);
}

/* This allocates several memory chunks in one block - making it one single
 * point of allocation failure, and needing just a single free() later on.
 * On Windows, allocations aren't guaranteed to be double-aligned, so
 * MA_ALIGN_DOUBLE flag is necessary there unless no chunks contain doubles. */
void *multialloc(int flags, void *ptr, int size, ...)
{
	va_list args;
	void *res;
	char *tmp;
	size_t tsz, sz = size, align = 0;


	if ((flags & MA_ALIGN_MASK) == MA_ALIGN_DOUBLE)
		align = sizeof(double) - 1;

	va_start(args, size);
	while (va_arg(args, void *))
	{
		sz = (sz + align) & ~align;
		sz += va_arg(args, int);
	}
	va_end(args);
	if (align) sz += align + 1;
	tmp = res = calloc(1, sz);
	if (res)
	{
		tmp = ALIGNED(tmp, align + 1);
		sz = 0; tsz = size;
		va_start(args, size);
		while (TRUE)
		{
			if (tsz || !(flags & MA_SKIP_ZEROSIZE))
				*(void **)ptr = (void *)(tmp + sz);

			if (!(ptr = va_arg(args, void *))) break;

			sz = (sz + tsz + align) & ~align;
			tsz = va_arg(args, int);
		}
		va_end(args);
	}
	return (res);
}

static int frameset_realloc(frameset *fset)
{
	image_frame *tmp;
	int n;

	/* Next power of 2 */
	n = nextpow2(fset->cnt);
	if (n < FRAMES_MIN) n = FRAMES_MIN;
	if (n == fset->max) return (TRUE);
	tmp = realloc(fset->frames, n * sizeof(image_frame));
	if (!tmp) return (FALSE);
	fset->frames = tmp;
	fset->max = n;
	fset->size = 0; // Recalculate it later
	return (TRUE);
}

/* Add one more frame to a frameset */
int mem_add_frame(frameset *fset, int w, int h, int bpp, int cmask, png_color *pal)
{
	image_frame *frm;
	unsigned char *res;
	size_t l, sz = (size_t)w * h;
	int i;

	/* Extend frames array if necessary */
	if (fset->cnt >= fset->max)
	{
		if ((fset->cnt >= FRAMES_MAX) || !frameset_realloc(fset))
			return (FALSE);
	}

	/* Initialize the frame */
	frm = fset->frames + fset->cnt;
	memset(frm, 0, sizeof(image_frame));
	frm->width = w;
	frm->height = h;
	frm->bpp = bpp;

	/* Allocate channels */
	l = sz * bpp;
	res = (void *)(-1);
	for (i = CHN_IMAGE; res && (i < NUM_CHANNELS); i++)
	{
		if (cmask & CMASK_FOR(i))
			res = frm->img[i] = malloc(l);
		l = sz;
	}

	/* Allocate palette if it differs from default */
	if (res && pal && (!fset->pal || memcmp(fset->pal, pal, SIZEOF_PALETTE)))
	{
		res = malloc(SIZEOF_PALETTE);
		if (res)
		{
			/* Set as default if first frame and no default yet */
			if (!fset->cnt && !fset->pal) fset->pal = (void *)res;
			else frm->pal = (void *)res;
			mem_pal_copy(res, pal);
		}
	}

	if (!res) /* Not enough memory */
	{
		while (--i >= 0) free(frm->img[i]);
		return (FALSE);
	}

	fset->cnt++;
	fset->size = 0; // Recalculate it later
	return (TRUE);
}

/* Remove specified frame from a frameset */
void mem_remove_frame(frameset *fset, int frame)
{
	image_frame *tmp;
	int l = fset->cnt;

	if (frame >= l) return;
	tmp = fset->frames + frame;
	mem_free_chanlist(tmp->img);
	free(tmp->pal);
	memmove(tmp, tmp + 1, (--l - frame) * sizeof(image_frame));
	fset->cnt = l;
// !!! Like with layers, you switch to another frame before deleting current one
	if (fset->cur > frame) fset->cur--;
	/* Reduce array size if 2/3+ empty */
	if ((l * 3 <= fset->max) && (fset->max > FRAMES_MIN))
		frameset_realloc(fset);
	fset->size = 0; // Recalculate it later
}

/* Empty a frameset */
void mem_free_frames(frameset *fset)
{
	image_frame *frm;
	int i;

	if (fset->frames)
	{
		for (i = 0 , frm = fset->frames; i < fset->cnt; i++ , frm++)
		{
			mem_free_chanlist(frm->img);
			free(frm->pal);
		}
		free(fset->frames);
	}
	free(fset->pal);
	memset(fset, 0, sizeof(frameset));
}

/* Set initial state of image variables */
void init_istate(image_state *state, image_info *image)
{
	memset(state->prot_mask, 0, 256);	/* Clear all mask info */
	state->prot = 0;
	state->col_[0] = 1;
	state->col_[1] = 0;
	state->col_24[0] = image->pal[1];
	state->col_24[1] = image->pal[0];
	state->tool_pat = 0;
}

/* Add a new undo data node */
int undo_add_data(undo_item *undo, int type, void *ptr)
{
	undo_data *node;
	unsigned int tmap = 1 << type;


	/* Reuse existing node */
	if ((node = undo->dataptr))
	{
		if (node->map & tmap) goto fail; // Prevent duplication
		tmap |= node->map;
	}
	/* Allocate a new node */
	else if ((node = undo_freelist)) undo_freelist = node->store[0];
	else
	{
		if (!undo_freecnt)
		{
			node = calloc(UNDO_STORESIZE, sizeof(undo_data));
			if (!node) goto fail;
			/* Datastores are never freed, so lose the previous ptr */
			undo_datastore = node;
			undo_freecnt = UNDO_STORESIZE - 1;
		}
		else node = undo_datastore + UNDO_STORESIZE - undo_freecnt--;
	}
	node->map = tmap;
	node->store[type] = ptr;
	undo->dataptr = node;
	return (TRUE);

fail:	/* Cannot store - delete the data right now */
	if (tmap & UD_FREE_MASK) free(ptr);
	return (FALSE);
}

/* Free an undo data block, and delete its data */
void undo_free_data(undo_item *undo)
{
	undo_data *tmp;
	unsigned int tmap;
	int i;

	if (!(tmp = undo->dataptr)) return;
	for (tmap = tmp->map & UD_FREE_MASK, i = 0; tmap; tmap >>= 1 , i++)
		if (tmap & 1) free(tmp->store[i]);
	tmp->store[0] = undo_freelist;
	undo_freelist = tmp;
	undo->dataptr = NULL;
}

/* Swap undo data - move current set out, and replace by incoming set */
void undo_swap_data(undo_item *outp, undo_item *inp)
{
	undo_data *tmp;
	unsigned int tmap;


	if (mem_tempname) undo_add_data(outp, UD_TEMPNAME, mem_tempname);
	mem_tempname = NULL;
// !!! Other unconditionally outgoing stuff goes here

	if (!(tmp = inp->dataptr)) return;
	tmap = tmp->map;
	if (tmap & (1 << UD_FILENAME))
	{
		undo_add_data(outp, UD_FILENAME, mem_filename);
		mem_filename = tmp->store[UD_FILENAME];
	}
	if (tmap & (1 << UD_TEMPNAME)) mem_tempname = tmp->store[UD_TEMPNAME];
// !!! All stuff (swappable or incoming) goes here

	/* Release the incoming node */
	tmp->store[0] = undo_freelist;
	undo_freelist = tmp;
	inp->dataptr = NULL;
}

/* Change layer's filename
 * Note: calling this with non-empty redo, for example when saving to a new
 * name, will "reparent" its frames from the old filename to the new one - WJ */
void mem_replace_filename(int layer, char *fname)
{
	image_info *image = &mem_image;
	undo_stack *undo;
	char *name;

	if (layer != layer_selected) image = &layer_table[layer].image->image_;
	name = image->filename;
	if (fname && !fname[0]) fname = NULL; // Empty name is no name

	/* Do nothing if "replacing" name by itself */
	if (fname && name ? !strcmp(fname, name) : fname == name) return;

	/* !!! Make a copy of new filename while the old filename still exists,
	 * because the new pointer must differ from the old one - WJ */
	if (fname) fname = strdup(fname);

	/* Store the old filename in _previous_ undo frame if possible */
	undo = &image->undo_;
	if (undo->done) undo_add_data(undo->items + (undo->pointer ?
		undo->pointer : undo->max) - 1, UD_FILENAME, name);
	else free(name);

	/* Replace filename, and clear tempname too while we're at it */
	image->filename = fname;
	image->tempname = NULL;
}

/* Label file's frames in current layer as changed */
void mem_file_modified(char *fname)
{
	char *name;
	undo_item *undo;
	int i, j, k, l, changed;

	l = mem_undo_done;
	for (k = -1; k <= 1; k += 2)
	{
		name = mem_filename;
		changed = name && !strcmp(name, fname) ? ~UF_ORIG : ~0;
		for (i = 1; i <= l; i++)
		{
			j = (mem_undo_max + mem_undo_pointer + i * k) % mem_undo_max;
			undo = mem_undo_im_ + j;
			if (undo->dataptr && (undo->dataptr->map & (1 << UD_FILENAME)))
			{
				name = undo->dataptr->store[UD_FILENAME];
				changed = name && !strcmp(name, fname) ? ~UF_ORIG : ~0;
			}
			undo->flags &= changed;
		}
		l = mem_undo_redo;
	}
}

/* Create new undo stack of a given depth */
int init_undo(undo_stack *ustack, int depth)
{
	if (!(ustack->items = calloc(depth, sizeof(undo_item)))) return (FALSE);
	ustack->max = depth;
	ustack->pointer = ustack->done = ustack->redo = 0;
	ustack->size = 0;
	return (TRUE);
}

/* Copy image state into current undo frame */
void update_undo(image_info *image)
{
	undo_item *undo = image->undo_.items + image->undo_.pointer;

/* !!! If system is unable to allocate 768 bytes, may as well die by SIGSEGV
 * !!! right here, and not hobble along till GUI does the dying - WJ */
	if (!undo->pal_) undo->pal_ = malloc(SIZEOF_PALETTE);
	mem_pal_copy(undo->pal_, image->pal);

	memcpy(undo->img, image->img, sizeof(chanlist));
	undo->dataptr = NULL;
	undo->cols = image->cols;
	undo->width = image->width;
	undo->height = image->height;
	undo->bpp = image->bpp;
	undo->flags = image->changed ? 0 : UF_ORIG;
}

void mem_free_chanlist(chanlist img)
{
	int i;

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!img[i]) continue;
		if (img[i] != (void *)(-1)) free(img[i]);
	}
}

static size_t undo_free_x(undo_item *undo)
{
	int j = undo->size;

	undo_free_data(undo);
	free(undo->pal_);
	mem_free_chanlist(undo->img);
	memset(undo, 0, sizeof(undo_item));
	return (j);
}

/* This resizes an in-use undo stack
 * !!! Both old and new depths must be nonzero */
static int resize_undo(undo_stack *ustack, int depth)
{
	undo_stack nstack;
	int i, j, k, undo, redo, ptr, uptr, udepth, trim;

	if (!init_undo(&nstack, depth)) return (FALSE);
	undo = ustack->done;
	redo = ustack->redo;
	if ((trim = undo + redo + 1 > depth))
	{
		i = (depth - 1) / 2;
		if (undo < i) redo = depth - undo - 1;
		else
		{
			if (redo > i) redo = i;
			undo = depth - redo - 1;
		}
	}
	uptr = ptr = ustack->pointer;
	if (ptr >= depth) ptr = 0;
	udepth = ustack->max;
	for (i = -undo; i <= redo; i++)
	{
		j = (uptr + i + udepth) % udepth;
		k = (ptr + i + depth) % depth;
		nstack.items[k] = ustack->items[j];
		memset(ustack->items + j, 0, sizeof(undo_item));
	}
	nstack.pointer = ptr;
	nstack.done = undo;
	nstack.redo = redo;
	if (trim)
	{
		for (i = 0; i < udepth; i++)
			undo_free_x(ustack->items + i);
	}
	free(ustack->items);
	*ustack = nstack;
	return (TRUE);
}

/* Resize all undo stacks */
void update_undo_depth()
{
	image_info *image;
	int l;

	mem_undo_depth = mem_undo_depth <= MIN_UNDO ? MIN_UNDO :
		mem_undo_depth >= MAX_UNDO ? MAX_UNDO : mem_undo_depth | 1;
	for (l = 0; l <= layers_total; l++)
	{
		image = l == layer_selected ? &mem_image :
			&layer_table[l].image->image_;
		if (image->undo_.max == mem_undo_depth) continue;
		resize_undo(&image->undo_, mem_undo_depth);
	}
}

/* Clear/remove image data */
void mem_free_image(image_info *image, int mode)
{
	int i, j = image->undo_.max, p = image->undo_.pointer;

	/* Delete current image (don't rely on undo frame being up to date) */
	if (mode & FREE_IMAGE)
	{
		mem_free_chanlist(image->img);
		memset(image->img, 0, sizeof(chanlist));
		image->width = image->height = 0;

		free(image->filename);
		image->filename = image->tempname = NULL;
	}

	/* Delete undo frames if any */
	image->undo_.pointer = image->undo_.done = image->undo_.redo = 0;
	if (!image->undo_.items) return;
	memset(image->undo_.items[p].img, 0, sizeof(chanlist)); // Already freed
	for (i = 0; i < j; i++) undo_free_x(image->undo_.items + i);

	/* Delete undo stack if finalizing */
	if (mode & FREE_UNDO)
	{
		free(image->undo_.items);
		image->undo_.items = NULL;
		image->undo_.max = 0;
	}
}

/* Allocate new image data */
// !!! Does NOT copy palette in copy mode, as it may be invalid
int mem_alloc_image(int mode, image_info *image, int w, int h, int bpp,
	int cmask, image_info *src)
{
	unsigned char *res;
	size_t l, sz;
	int i;

	if (mode & AI_CLEAR) memset(image, 0, sizeof(image_info));
	else
	{
		memset(image->img, 0, sizeof(chanlist));
		image->filename = image->tempname = NULL; /* Paranoia */
		image->changed = 0;
	}

	if (mode & AI_COPY)
	{
		if (src->filename && !(image->filename = strdup(src->filename)))
			return (FALSE);
		image->tempname = src->tempname;
		image->changed = src->changed;

		w = src->width;
		h = src->height;
		bpp = src->bpp;
		cmask = cmask_from(src->img);
	}

	image->width = w;
	image->height = h;
	image->bpp = bpp;

	if (!cmask) return (TRUE); /* Empty block requested */

	sz = (size_t)w * h;
	l = sz * bpp;
	res = (void *)(-1);
	for (i = CHN_IMAGE; res && (i < NUM_CHANNELS); i++)
	{
		if (cmask & CMASK_FOR(i)) res = image->img[i] = malloc(l);
		l = sz;
	}
	if (res && image->undo_.items)
	{
		int k = image->undo_.pointer;
		if (!image->undo_.items[k].pal_)
			res = (void *)(image->undo_.items[k].pal_ =
				malloc(SIZEOF_PALETTE));
	}
	if (!res) /* Not enough memory */
	{
		free(image->filename);
		image->filename = NULL;
		while (--i >= 0) free(image->img[i]);
		memset(image->img, 0, sizeof(chanlist));
		return (FALSE);
	}

	l = sz * bpp;
	if (mode & AI_COPY) /* Clone */
	{
		for (i = CHN_IMAGE; i < NUM_CHANNELS; i++)
		{
			if (image->img[i]) memcpy(image->img[i], src->img[i], l);
			l = sz;
		}
	}
	else if (!(mode & AI_NOINIT)) /* Init */
	{
		for (i = CHN_IMAGE; i < NUM_CHANNELS; i++)
		{
			if (image->img[i]) memset(image->img[i], channel_fill[i], l);
			l = sz;
		}
	}

	return (TRUE);
}

/* Allocate space for new image, removing old if needed */
int mem_new( int width, int height, int bpp, int cmask )
{
	int res;

	mem_free_image(&mem_image, FREE_IMAGE);
	res = mem_alloc_image(0, &mem_image, width, height, bpp, cmask, NULL);
	if (!res) /* Not enough memory */
	{
		// 8x8 is bound to work!
		mem_alloc_image(0, &mem_image, 8, 8, bpp, CMASK_IMAGE, NULL);
	}

// !!! If palette isn't set up before mem_new(), undo frame will get wrong one
// !!! (not that it affects anything at this time)
	update_undo(&mem_image);
	mem_channel = CHN_IMAGE;
	mem_xpm_trans = mem_xbm_hot_x = mem_xbm_hot_y = -1;

	return (!res);
}

int cmask_from(chanlist img)
{
	int i, j, k = 1;

	for (i = j = 0; i < NUM_CHANNELS; i++ , k += k)
		if (img[i]) j |= k;
	return (j);
}

/* Allocate new clipboard, removing or preserving old as needed */
int mem_clip_new(int width, int height, int bpp, int cmask, int backup)
{
	int res;


	/* Text flag defaults to cleared */
	text_paste = 0;

	/* Clear everything if no backup needed */
	if (!backup) mem_free_image(&mem_clip, FREE_ALL);

	/* Backup current contents if no backup yet */
	else if (!HAVE_OLD_CLIP)
	{
		/* Ensure a minimal undo stack */
		if (!mem_clip.undo_.items) init_undo(&mem_clip.undo_, 2);

		/* No point in firing up undo engine for this */
		mem_clip.undo_.pointer = OLD_CLIP;
		update_undo(&mem_clip);
		mem_clip.undo_.done = 1;
		mem_clip.undo_.pointer = 0;
	}

	/* Clear current contents if backup exists */
	else mem_free_chanlist(mem_clip.img);

	/* Add old clipboard's channels to cmask */
	if (backup) cmask |= cmask_from(mem_clip_real_img);

	/* Allocate new frame */
	res = mem_alloc_image(AI_NOINIT, &mem_clip, width, height, bpp, cmask, NULL);

	/* Remove backup if allocation failed */
	if (!res && HAVE_OLD_CLIP) mem_free_image(&mem_clip, FREE_ALL);

	/* Fill current undo frame if any */
	else if (mem_clip.undo_.items) update_undo(&mem_clip);

	return (!res);
}

/* Get address of previous channel data (or current if none) */
unsigned char *mem_undo_previous(int channel)
{
	unsigned char *res;
	int i;

	i = (mem_undo_pointer ? mem_undo_pointer : mem_undo_max) - 1;
	res = mem_undo_im_[i].img[channel];
	if (!res || (res == (void *)(-1)) || (mem_undo_im_[i].flags & UF_TILED))
		res = mem_img[channel];	// No usable undo so use current
	return (res);
}

static size_t lose_oldest(undo_stack *ustack)	// Lose the oldest undo image
{
	int idx;

	if (ustack->redo > ustack->done) idx = ustack->redo--;
	else if (ustack->done) idx = ustack->max - ustack->done--;
	else return (0);
/* !!! mem_try_malloc() may call this on an unsized undo stack - but it
 * !!! doesn't need valid sizes anyway - WJ */
	return (undo_free_x(ustack->items + (ustack->pointer + idx) % ustack->max));
}

/* Convert tile bitmap row into a set of spans (skip/copy), terminated by
 * a zero-length copy span; return copied length */
static int mem_undo_spans(int *spans, unsigned char *tmap, int width, int bpp)
{
	int bt = 0, bw = 0, tl = 0, l = 0, ll = bpp * TILE_SIZE;

	while (width > 0)
	{
		if ((bw >>= 1) < 2) bw = 0x100 + *tmap++;
		if (bt ^ (bw & 1))
		{
			*spans++ = tl * ll;
			tl = 0;
		}
		tl++;
		l += (bt = bw & 1) * ll;
		width -= TILE_SIZE;
	}
	width *= bpp;
	*spans++ = tl * ll + width;
	l += bt * width;
	spans[0] = spans[bt] = 0;
	return (l);
}

/* Endianness-aware byte shifts */
#if G_BYTE_ORDER == G_LITTLE_ENDIAN
#define SHIFTUP(X,N) (X) <<= ((N) << 3)
#define SHIFTDN(X,N) (X) >>= ((N) << 3)
#else /* G_BYTE_ORDER == G_BIG_ENDIAN */
#define SHIFTUP(X,N) (X) >>= ((N) << 3)
#define SHIFTDN(X,N) (X) <<= ((N) << 3)
#endif

/* Register-sized unsigned integer - redefine if this isn't it */
#include <stdint.h>
#define R_INT uintptr_t

/* Integer-at-a-time byte array comparison function; its efficiency depends on
 * both arrays aligned, or misaligned, the same - which is natural for channel
 * strips when geometry and position match - WJ */
static int tile_row_compare(unsigned char *src, unsigned char *dest,
	int w, int h, unsigned char *buf)
{
	const int amask = sizeof(R_INT) - 1;
	int l = w * h, mc = (w + TILE_SIZE - 1) >> TILE_SHIFT, nc = 0;

	/* Long enough & identically aligned - use fast path */
	if ((w > sizeof(R_INT)) && (l > sizeof(R_INT) * 4) &&
		(((int)src & amask) == ((int)dest & amask)))
	{
		R_INT v, vm, vt1, vt2, *isrc, *idest;
		int i, k, t, x, d0, d1, il;

		/* Given that loose ends, if any, belong to tiles too, we
		 * simply leave them for later - maybe there won't be need */
		d0 = (((int)src ^ amask) + 1) & amask;
		d1 = (int)(src + l) & amask;
		isrc = (R_INT *)(src + d0);
		idest = (R_INT *)(dest + d0);
		il = (l - d0 - d1) / sizeof(v);
		i = 0;
		while (TRUE)
		{
			/* Fast comparison loop - damn GCC's guts for not
			 * allocating it on registers without heavy wizardry */
			{
				int wi = il - i;
				R_INT *wsrc = isrc + i, *wdest = idest + i;
				while (TRUE)
				{
					if (wi-- <= 0) goto done;
					if (*wsrc != *wdest) break;
					++wsrc; ++wdest;
				}
				t = (unsigned char *)wsrc - src;
			}
			k = (unsigned int)t % w;
			x = TILE_SIZE - (k & (TILE_SIZE - 1));
			if (k + x > w) x = w - k;
			k >>= TILE_SHIFT;

			/* Value overlaps two or three tiles */
			while (x < sizeof(v))
			{
				v = isrc[i] ^ idest[i];
tile2:				vm = ~0UL;
				SHIFTUP(vm, x);
				x += sizeof(v);
				if (!(vm &= v)) break;
				x -= sizeof(v);

				/* Farther tile(s) differ */
				if ((v != vm) && !buf[k]) /* First one differs too */
				{
					buf[k] = 1;
					if (++nc >= mc) x = l; /* Done is done */
				}
				if (++k + 1 == mc) /* May be 3 tiles */
				{
					x += w & (TILE_SIZE - 1);
					if (x >= sizeof(v)) break;
					v = vm;
					goto tile2;
				}
				x += TILE_SIZE;
				if (k == mc) k = 0; /* Row wrap */
				break;
			}
			i = (t + x - d0) / sizeof(v);
			if (buf[k]) continue;
			buf[k] = 1;
			if (++nc >= mc) break;
		}
done:
		/* Compare the ends - using the fact that memory blocks
		 * *must* be aligned at least that much */
		if (d1 && !buf[mc - 1])
		{
			vt2 = isrc[il] ^ idest[il];
			SHIFTUP(vt2, sizeof(vt2) - d1);
			if (vt2) ++nc , buf[mc - 1] = 1;
		}
		if (d0 && !buf[0])
		{
			vt1 = *(isrc - 1) ^ *(idest - 1);
			SHIFTDN(vt1, d0);
			if (vt1) ++nc , buf[0] = 1;
		}
	}
	/* Misaligned - use slow path */
	else
	{
		int i, k, x;

		for (i = 0; i < l; i++)
		{
			if (src[i] != dest[i])
			{
				k = (unsigned int)i % w;
				x = TILE_SIZE - (k & (TILE_SIZE - 1));
				if (k + x > w) x = w - k;
				i += x;
				k >>= TILE_SHIFT;
				if (buf[k]) continue;
				buf[k] = 1;
				if (++nc >= mc) break;
			}
		}
	}
	return (nc);
}

/* Convert undo frame to tiled representation */
static void mem_undo_tile(undo_item *undo)
{
	unsigned char buf[((MAX_WIDTH + TILE_SIZE - 1) / TILE_SIZE) * 3];
	unsigned char *tstrip, tmap[MAX_TILEMAP], *tmp = NULL;
	int spans[(MAX_WIDTH + TILE_SIZE - 1) / TILE_SIZE + 3];
	size_t sz, area = 0, msize = 0;
	int i, j, k, nt, dw, cc, bpp;
	int h, nc, bw, tw, tsz, nstrips, ntiles = 0;


	undo->flags |= UF_FLAT; /* Not tiled by default */

	/* Not tileable if too small */
	if (mem_width + mem_height < TILE_SIZE * 3) return;

	/* Not tileable if different geometry */
	if ((undo->width != mem_width) || (undo->height != mem_height) ||
		(undo->bpp != mem_img_bpp)) return;

	for (i = nc = 0; i < NUM_CHANNELS; i++)
	{
		/* Not tileable if different set of channels */
		if (!!undo->img[i] ^ !!mem_img[i]) return;
		if (undo->img[i] && mem_img[i] &&
			(undo->img[i] != (void *)(-1))) nc |= 1 << i;
	}
	/* Not tileable if no matching channels */
	if (!nc) return;

	/* Build tilemap */
	nstrips = (mem_height + TILE_SIZE - 1) / TILE_SIZE;
	dw = (TILE_SIZE - 1) & ~(mem_width - 1);
	bw = (mem_width + TILE_SIZE - 1) / TILE_SIZE;
	tw = (bw + 7) >> 3; tsz = tw * nstrips;
	memset(tmap, 0, tsz);
	for (i = 0 , tstrip = tmap; i < mem_height; i += TILE_SIZE , tstrip += tw)
	{
		h = mem_height - i;
		if (h > TILE_SIZE) h = TILE_SIZE;

		/* Compare strip of image */
		memset(buf, 0, bw * 3);
		for (cc = 0; nc >= 1 << cc; cc++)
		{
			unsigned char *src, *dest;
			int j, k, j2, w;

			if (!(nc & 1 << cc)) continue;
			bpp = BPP(cc);
			w = mem_width * bpp;
			k = i * w;
			src = undo->img[cc] + k;
			dest = mem_img[cc] + k;
			if (!tile_row_compare(src, dest, w, h, buf)) continue;
			if (bpp == 1) continue;
			/* 3 bpp happen only in image channel, which goes first;
			 * so we can postprocess the results to match 1 bpp */
			for (j = j2 = 0; j < bw; j++ , j2 += 3)
				buf[j] = buf[j2] | buf[j2 + 1] | buf[j2 + 2];
		}
		/* Fill tilemap row */
		for (j = nt = 0; j < bw; j++)
		{
			nt += (k = buf[j]);
			tstrip[j >> 3] |= k << (j & 7);
		}
		ntiles += nt;
		area += (nt * TILE_SIZE - buf[bw - 1] * dw) * h;
	}

	/* Not tileable if tilemap cannot fit in space gained */
	sz = (size_t)mem_width * mem_height;
	bpp = (nc & CMASK_IMAGE ? mem_img_bpp : 1);
	if ((sz - area) * bpp <= tsz) return;

	/* Implement tiling */
	sz = (size_t)mem_width * mem_height;
	for (cc = 0; nc >= 1 << cc; cc++)
	{
		unsigned char *src, *dest, *blk;
		size_t l;
		int i;

		if (!(nc & 1 << cc)) continue;
		if (!ntiles) /* Channels unchanged - free the memory */
		{
			free(undo->img[cc]);
			undo->img[cc] = (void *)(-1);
			continue;
		}

		/* Try to reduce memory fragmentation - allocate small blocks
		 * anew when possible, instead of carving up huge ones */
		src = blk = undo->img[cc];
		bpp = BPP(cc);
		l = area * bpp + (tmp ? 0 : tsz);
		if (l * 3 <= sz * bpp) /* Small enough */
		{
			blk = malloc(l);
			/* Use original chunk if cannot get new one */
			if (!blk) blk = src;
		}
		dest = blk;

		/* Compress channel */
		for (i = 0; i < nstrips; i++)
		{
			int j, k, *span;

			mem_undo_spans(spans, tmap + tw * i, mem_width, bpp);
			k = mem_height - i * TILE_SIZE;
			if (k > TILE_SIZE) k = TILE_SIZE;
			for (j = 0; j < k; j++)
			{
				span = spans;
				while (TRUE)
				{
					src += *span++;
					if (!*span) break;
					if (dest != src) memmove(dest, src, *span);
					src += *span; dest += *span++;
				}
			}
		}

		/* Resize or free memory block */
		if (blk == undo->img[cc]) /* Resize old */
		{
			dest = realloc(undo->img[cc], l);
			/* Leave chunk alone if resizing failed */
			if (!dest) l = sz * bpp;
			else undo->img[cc] = dest;
		}
		else /* Replace with new */
		{
			free(undo->img[cc]);
			undo->img[cc] = blk;
		}
		msize += l + 32;

		/* Place tilemap in first chunk */
		if (!tmp) tmp = undo->img[cc] + area * bpp;
	}

	/* Re-label as tiled and store tilemap, if there *are* tiles */
	if (msize)
	{
		undo->flags ^= UF_FLAT | UF_TILED;
		undo->tileptr = tmp;
		memcpy(tmp, tmap, tsz);
	}

	if (undo->pal_) msize += SIZEOF_PALETTE + 32;
	undo->size = msize;
	undo->flags |= UF_SIZED;
}

/* Compress last undo frame */
void mem_undo_prepare()
{
	undo_item *undo;
	int k;

	if (!mem_undo_done) return;
	k = (mem_undo_pointer ? mem_undo_pointer : mem_undo_max) - 1;
	undo = mem_undo_im_ + k;

	/* Already processed? */
	if (undo->flags & (UF_TILED | UF_FLAT)) return;

	/* Cull palette if unchanged */
	if (undo->pal_ && !memcmp(undo->pal_, mem_pal, SIZEOF_PALETTE))
	{
		/* Free new block, reuse old */
		free(mem_undo_im_[mem_undo_pointer].pal_);
		mem_undo_im_[mem_undo_pointer].pal_ = undo->pal_;
		undo->pal_ = NULL;
	}
	/* Tile image */
	mem_undo_tile(undo);
}

static size_t mem_undo_size(undo_stack *ustack)
{
	undo_item *undo = ustack->items;
	size_t k, l, total = 0;
	int i, j, umax = ustack->max;

	for (i = 0; i < umax; i++ , total += (undo++)->size)
	{
		/* Empty or already scanned? */
		if (!undo->width || (undo->flags & UF_SIZED)) continue;
		k = (size_t)undo->width * undo->height;
		for (j = l = 0; j < NUM_CHANNELS; j++)
		{
			if (!undo->img[j] || (undo->img[j] == (void *)(-1)))
				continue;
			l += (j == CHN_IMAGE ? k * undo->bpp : k) + 32;
		}
		if (undo->pal_) l += SIZEOF_PALETTE + 32;
		undo->size = l;
		undo->flags |= UF_SIZED;
	}

	return (total);
}

/* Add up sizes of all layers other than current */
static size_t mem_undo_lsize()
{
	undo_stack *ustack;
	size_t total = 0;
	int l;

	for (l = 0; l <= layers_total; l++)
	{
		if (l == layer_selected) continue;
		ustack = &layer_table[l].image->image_.undo_;
// !!! This relies on layer_table items already processed by update_undo()
		if (!ustack->size) ustack->size = mem_undo_size(ustack);
		total += ustack->size;
	}

	return (total);
}

/* Free requested amount of undo space */
static int mem_undo_space(size_t mem_req)
{
	undo_stack *heap[MAX_LAYERS + 2], *wp, *hp;
	size_t mem_r, mem_lim, mem_max = (size_t)mem_undo_limit * (1024 * 1024);
	int i, l, l2, h, csz = mem_undo_common * layers_total;
	
	/* Layer mem limit including common area */
	mem_lim = mem_max * (csz * 0.01 + 1) / (layers_total + 1);

	/* Fail if hopeless */
	if (mem_req > mem_lim) return (2);

	/* Layer mem limit exceeded - drop oldest */
	mem_r = mem_req + mem_undo_size(&mem_image.undo_);
	while (mem_r > mem_lim)
	{
		if (!mem_undo_done) return (1);
		mem_r -= lose_oldest(&mem_image.undo_);
	}
	/* All done if no common area */
	if (!csz) return (0);

	mem_r += mem_undo_lsize();
	if (mem_r <= mem_max) return (0); // No need to trim other layers yet
	mem_lim -= mem_max * (mem_undo_common * 0.01); // Reserved space per layer

	/* Build heap of undo stacks */
	for (i = h = 0; i <= layers_total; i++)
	{
		// Skip current layer
		if (i == layer_selected) continue;
		wp = &layer_table[i].image->image_.undo_;
		// Skip layers without extra frames
		if (!(wp->done + wp->redo)) continue;
		// Skip layers under the memory limit
		if (wp->size <= mem_lim) continue;
		// Put undo stack onto heap
		for (l = ++h; l > 1; l = l2)
		{
			l2 = l >> 1;
			if ((hp = heap[l2])->size >= wp->size) break;
			heap[l] = hp;
		}
		heap[l] = wp;
	}

	/* Drop frames of greediest layers */
	while (h > 0)
	{
		size_t mem_nx = h > 1 ? heap[2]->size : 0;
		if ((h > 2) && (heap[3]->size > mem_nx)) mem_nx = heap[3]->size;
		/* Drop frames */
		wp = heap[1];
		while (TRUE)
		{
			size_t res = lose_oldest(wp);
			wp->size -= res; // Maintain undo stack size
			mem_r -= res;
			if (mem_r <= mem_max) return (0);
			if (!(wp->done + wp->redo) || (wp->size <= mem_lim))
				wp = heap[h--];
			else if (wp->size >= mem_nx) continue;
			break;
		}
		/* Reheap layer */
		mem_nx = wp->size;
		for (l = 1; (l2 = l + l) <= h; l = l2)
		{
			if ((l2 < h) && (heap[l2]->size < heap[l2 + 1]->size)) l2++;
			if (mem_nx >= (hp = heap[l2])->size) break;
			heap[l] = hp;
		}
		heap[l] = wp;
	}

	return (0);
}

/* Try to allocate a memory block, releasing undo frames if needed */
void *mem_try_malloc(size_t size)
{
	void *ptr;

	while (!((ptr = malloc(size))))
	{
// !!! Hardcoded to work with mem_image for now
		if (!mem_undo_done) return (NULL);
		lose_oldest(&mem_image.undo_);
	}
	return (ptr);
}

int undo_next_core(int mode, int new_width, int new_height, int new_bpp, int cmask)
{
	png_color *newpal;
	undo_item *undo;
	unsigned char *img;
	char *tempname = mem_tempname;
	chanlist holder, frame;
	size_t mem_req, mem_lim, wh;
	int i, j, k, need_frame;


	if (pen_down && (mode & UC_PENDOWN)) return (0);
	pen_down = mode & UC_PENDOWN ? 1 : 0;

	/* Fill undo frame */
	update_undo(&mem_image);

	/* Postpone change notify if nothing will be done without new frame */
	need_frame = mode & (UC_CREATE | UC_NOCOPY | UC_GETMEM);
	if (!need_frame) notify_changed();

	/* Release redo data */
	if (mem_undo_redo)
	{
		k = mem_undo_pointer;
		for (i = 0; i < mem_undo_redo; i++)
		{
			k = (k + 1) % mem_undo_max;
			undo_free_x(mem_undo_im_ + k);
		}
		mem_undo_redo = 0;
	}

	/* Compress last undo frame */
	mem_undo_prepare();

	/* Calculate memory requirements */
	mem_req = SIZEOF_PALETTE + 32;
	wh = (size_t)new_width * new_height;
	if (!(mode & UC_DELETE))
	{
		for (i = j = 0; i < NUM_CHANNELS; i++)
		{
			if ((cmask & (1 << i)) &&
				(mem_img[i] || (mode & UC_CREATE))) j++;
		}
		if (cmask & CMASK_IMAGE) j += new_bpp - 1;
		mem_req += (wh + 32) * j;
// !!! Must be after update_undo() to get used memory right
		if (mem_undo_space(mem_req)) return (2);
	}
	if (mode & UC_GETMEM) return (0); // Enough memory was freed

	/* Prepare outgoing frame */
	undo = mem_undo_im_ + mem_undo_pointer;
	for (i = 0; i < NUM_CHANNELS; i++)
	{
		img = undo->img[i];
		frame[i] = img && !(cmask & (1 << i)) ? (void *)(-1) : img;
	}

	/* Allocate new palette */
	newpal = mem_try_malloc(SIZEOF_PALETTE);
	if (!newpal) return (1);

	/* Duplicate affected channels */
	for (i = 0; i < NUM_CHANNELS; i++)
	{
		holder[i] = img = mem_img[i];
		if (!(cmask & (1 << i))) continue;
		if (mode & UC_DELETE)
		{
			holder[i] = NULL;
			continue;
		}
		if (!img && !(mode & UC_CREATE)) continue;
		mem_lim = i == CHN_IMAGE ? wh * new_bpp : wh;
		img = mem_try_malloc(mem_lim);
		if (!img) /* Release memory and fail */
		{
			free(newpal);
			for (j = 0; j < i; j++)
				if (holder[j] != mem_img[j]) free(holder[j]);
			return (1);
		}
		holder[i] = img;
		/* Copy */
		if (!frame[i] || (mode & UC_NOCOPY)) continue;
		memcpy(img, frame[i], mem_lim);
	}

	/* Next undo step */
	mem_undo_pointer = (mem_undo_pointer + 1) % mem_undo_max;
	if (mem_undo_done >= mem_undo_max - 1)
		undo_free_x(mem_undo_im_ + mem_undo_pointer);
	else mem_undo_done++;

	/* Commit */
	if (tempname) undo_add_data(undo, UD_TEMPNAME, tempname);
	memcpy(undo->img, frame, sizeof(chanlist));
	mem_undo_im_[mem_undo_pointer].pal_ = newpal;
	memcpy(mem_img, holder, sizeof(chanlist));
	mem_width = new_width;
	mem_height = new_height;
	mem_img_bpp = new_bpp;
 
	/* Do postponed change notify, now that new frame is created */
	if (need_frame) notify_changed();

	update_undo(&mem_image);
	return (0);
}

// Call this after a draw event but before any changes to image
void mem_undo_next(int mode)
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
		wmode = UC_PENDOWN;
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
		wmode = UC_PENDOWN;	/* !!! Workaround for move-with-RMB-pressed */
		cmask = (mem_channel == CHN_IMAGE) && !channel_dis[CHN_ALPHA] &&
			(mem_clip_alpha || RGBA_mode) ? CMASK_RGBA : CMASK_CURR;
		break;
	}
	undo_next_core(wmode, mem_width, mem_height, mem_img_bpp, cmask);
}

/* Swap image & undo tiles; in process, normal order translates to reverse and
 * vice versa - in order to do it in same memory with minimum extra copies */
static void mem_undo_tile_swap(undo_item *undo, int redo)
{
	unsigned char buf[MAX_WIDTH * 3], *tmap, *src, *dest;
	int spans[(MAX_WIDTH + TILE_SIZE - 1) / TILE_SIZE + 3];
	int i, l, h, cc, nw, bpp, w;

	nw = ((mem_width + TILE_SIZE - 1) / TILE_SIZE + 7) >> 3;
	for (cc = 0; cc < NUM_CHANNELS; cc++)
	{
		if (!undo->img[cc] || (undo->img[cc] == (void *)(-1)))
			continue;
		tmap = undo->tileptr;
		bpp = BPP(cc);
		w = mem_width * bpp;
		src = undo->img[cc];
		for (i = 0; i < mem_height; i += TILE_SIZE , tmap += nw)
		{
			int j, j1, dj;

			if (!(l = mem_undo_spans(spans, tmap, mem_width, bpp)))
				continue;
			dest = mem_img[cc] + w * i;
			h = mem_height - i;
			if (h > TILE_SIZE) h = TILE_SIZE;

			/* First row stored after last in redo frames */
			if (!redo) j = 0 , j1 = h , dj = 1;
			else
			{
				j = h - 1; j1 = dj = -1;
				memcpy(buf, src + j * l, l);
			}
			/* Process undo normally, and redo backwards */
			for (; j != j1; j += dj)
			{
				unsigned char *ts, *td, *tm;
				int *span = spans;

				td = dest + j * w;
				tm = ts = src + j * l;
				*(redo ? &ts : &tm) = j ? tm - l : buf;
				while (TRUE)
				{
					td += *span++;
					if (!*span) break;
					memcpy(tm, td, *span);
					memcpy(td, ts, *span);
					tm += *span;
					ts += *span; td += *span++;
				}
			}
			src += h * l;
			if (!redo) memcpy(src - l, buf, l);
		}
	}
}

static void mem_undo_swap(int old, int new, int redo)
{
	undo_item *curr, *prev;
	int i;

	curr = &mem_undo_im_[old];
	prev = &mem_undo_im_[new];

	if (prev->flags & UF_TILED)
	{
		mem_undo_tile_swap(prev, redo);
		for (i = 0; i < NUM_CHANNELS; i++)
		{
			curr->img[i] = prev->img[i];
			prev->img[i] = mem_img[i];
		}
		curr->tileptr = prev->tileptr;
		prev->tileptr = NULL;
		curr->size = prev->size;
		curr->flags = prev->flags & ~UF_ORIG;
	}
	else
	{
		for (i = 0; i < NUM_CHANNELS; i++)
		{
			if (prev->img[i] == (void *)(-1))
			{
				curr->img[i] = (void *)(-1);
				prev->img[i] = mem_img[i];
			}
			else
			{
				curr->img[i] = mem_img[i];
				mem_img[i] = prev->img[i];
			}
		}
		curr->flags = UF_FLAT;
	}
	prev->flags &= UF_ORIG;

	mem_pal_copy(curr->pal_, mem_pal);
	if (!prev->pal_)
	{
		prev->pal_ = curr->pal_;
		curr->pal_ = NULL;
	}
	else mem_pal_copy(mem_pal, prev->pal_);

	undo_swap_data(curr, prev);

	curr->width = mem_width;
	curr->height = mem_height;
	curr->cols = mem_cols;
	curr->bpp = mem_img_bpp;
	if (!mem_changed) curr->flags |= UF_ORIG;

	mem_width = prev->width;
	mem_height = prev->height;
	mem_cols = prev->cols;
	mem_img_bpp = prev->bpp;
	mem_changed = !(prev->flags & UF_ORIG);
}

void mem_undo_backward()		// UNDO requested by user
{
	int i;

	/* Compress last undo frame */
	mem_undo_prepare();

	if ( mem_undo_done > 0 )
	{
		i = (mem_undo_pointer - 1 + mem_undo_max) % mem_undo_max;
		mem_undo_swap(mem_undo_pointer, i, 0);

		mem_undo_pointer = i;
		mem_undo_done--;
		mem_undo_redo++;
	}
	pen_down = 0;
}

void mem_undo_forward()			// REDO requested by user
{
	int i;

	/* Compress last undo frame */
	mem_undo_prepare();

	if ( mem_undo_redo > 0 )
	{
		i = (mem_undo_pointer + 1) % mem_undo_max;	// New pointer
		mem_undo_swap(mem_undo_pointer, i, 1);

		mem_undo_pointer = i;
		mem_undo_done++;
		mem_undo_redo--;
	}
	pen_down = 0;
}

/* Return the number of bytes used in image + undo */
size_t mem_used()
{
	update_undo(&mem_image);
	return mem_undo_size(&mem_image.undo_);
}

/* Return the number of bytes used in image + undo in all layers */
size_t mem_used_layers()
{
	return (mem_used() + mem_undo_lsize());
}

/* Fast approximate atan2() function, returning result in degrees. This code is
 * approximately 2x faster than using libm on P4/Linux, and 6x on Windows.
 * Absolute error is below 0.0003 degrees, which means 1/10 of a pixel in worst
 * possible case. - WJ */

#define ATANNUM 128
static float ATAN[2 * ATANNUM + 2];

double atan360(int x, int y)
{
	double d;
	int xa, ya, n;

	if (!(x | y)) return (0.0);
	xa = abs(x); ya = abs(y);
	d = ya < xa ? (double)ya / (double)xa : 2.0 - (double)xa / (double)ya;
	d *= ATANNUM;
	n = d;
	d = ATAN[n] + (ATAN[n + 1] - ATAN[n]) * (d - n);

	if (x < 0) d = 180.0 - d;
	return (y >= 0 ? d : 360.0 - d);
}

static void make_ATAN()
{
	int i;

	for (i = 0; i <= ATANNUM; i++)
		ATAN[2 * ATANNUM - i] = 90.0 -
			(ATAN[i] = atan(i * (1.0 / ATANNUM)) * (180.0 / M_PI));
	ATAN[2 * ATANNUM + 1] = 90.0;
}

int load_def_palette(char *name)
{
	int i;

	if (!name[0]) return (FALSE); // Useless
	i = detect_palette_format(name);
	if (i > FT_NONE) return (load_image(name, FS_PALETTE_DEF, i) == 1);
	return (FALSE);
}

int load_def_patterns(char *name)
{
	int i;

	if (!name[0]) return (FALSE); // Useless
	i = detect_image_format(name);
	if ((i > FT_NONE) && (file_formats[i].flags & FF_IDX))
		return (load_image(name, FS_PATTERN_LOAD, i) == 1);
	return (FALSE);
}

void mem_init()					// Initialise memory
{
	static const unsigned char lookup[8] =
		{ 0, 36, 73, 109, 146, 182, 219, 255 };
	unsigned char *dest;
	char txt[64];
	int i, j, ix, iy, bs, bf, bt;


	make_ATAN();

	for (i = 0; i < 256; i++)	// Load up normal palette defaults
	{
		mem_pal_def[i].red = lookup[mem_pal_def[i].red];
		mem_pal_def[i].green = lookup[mem_pal_def[i].green];
		mem_pal_def[i].blue = lookup[mem_pal_def[i].blue];
	}

	load_def_palette(inifile_get(DEFAULT_PAL_INI, ""));
	load_def_patterns(inifile_get(DEFAULT_PAT_INI, ""));

	/* Init editor settings */
	mem_channel = CHN_IMAGE;
	mem_icx = mem_icy = 0.5;
	mem_xpm_trans = mem_xbm_hot_x = mem_xbm_hot_y = -1;
	mem_col_A = 1;
	mem_col_B = 0;

	/* Set up default undo stack */
	mem_undo_depth = mem_undo_depth <= MIN_UNDO ? MIN_UNDO :
		mem_undo_depth >= MAX_UNDO ? MAX_UNDO : mem_undo_depth | 1;
	if (!init_undo(&mem_image.undo_, mem_undo_depth))
	{
		memory_errors(1);
		exit(0);
	}

	// Create brush presets

	mem_cols = mem_pal_def_i;
	mem_pal_copy( mem_pal, mem_pal_def );
	if (mem_new(PATCH_WIDTH, PATCH_HEIGHT, 3, CMASK_IMAGE))	// Not enough memory!
	{
		memory_errors(1);
		exit(0);
	}
	mem_mask_setall(0);

	mem_col_A24.red = 255;
	mem_col_A24.green = 255;
	mem_col_A24.blue = 255;
	mem_col_B24.red = 0;
	mem_col_B24.green = 0;
	mem_col_B24.blue = 0;

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

	j = PATCH_WIDTH * PATCH_HEIGHT * 3;
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

	/* Preset gradients */
	for (i = 0; i < NUM_CHANNELS; i++)
	{
		grad_info *grad = gradient + i;

		grad->gmode = GRAD_MODE_LINEAR;
		grad->rmode = GRAD_BOUND_STOP;
		grad_update(grad);
	}
	for (i = 0; i <= NUM_CHANNELS; i++)
	{
		graddata[i].gtype = GRAD_TYPE_RGB;
		graddata[i].otype = GRAD_TYPE_CONST;
	}
	grad_def_update(-1);
	for (i = 0; i <= NUM_CHANNELS; i++)
		gmap_setup(graddata + i, gradbytes, i);
}

void mem_swap_cols(int redraw)
{
	int oc, flags;
	png_color o24;

	if (mem_channel != CHN_IMAGE)
	{
		oc = channel_col_A[mem_channel];
		channel_col_A[mem_channel] = channel_col_B[mem_channel];
		channel_col_B[mem_channel] = oc;
		flags = redraw ? UPD_GRAD : CF_GRAD;
	}
	else
	{
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
		flags = redraw ? UPD_AB : CF_AB | CF_GRAD;
	}
	update_stuff(flags);
}

#define PALETTE_TEXT_GREY 200

static void repaint_swatch(int index)		// Update a palette colour swatch
{
	unsigned char *tmp, pcol[2] = { 0, 0 };
	int i, j;

	tmp = mem_pals + index * PALETTE_SWATCH_H * PALETTE_W3 +
		PALETTE_SWATCH_Y * PALETTE_W3 + PALETTE_SWATCH_X * 3;
	tmp[0] = mem_pal[index].red;
	tmp[1] = mem_pal[index].green;
	tmp[2] = mem_pal[index].blue;
	for (i = 3; i < PALETTE_SWATCH_W * 3; i++) tmp[i] = tmp[i - 3];
	for (i = 1; i < PALETTE_SWATCH_H; i++)
		memcpy(tmp + i * PALETTE_W3, tmp, PALETTE_SWATCH_W * 3);

	if (mem_prot_mask[index]) pcol[1] = PALETTE_TEXT_GREY;	// Protection mask cross
	tmp += PALETTE_CROSS_DY * PALETTE_W3 +
		(PALETTE_CROSS_X + PALETTE_CROSS_DX - PALETTE_SWATCH_X) * 3;
	for (i = 0; i < PALETTE_CROSS_H; i++)
	{
		for (j = 0; j < PALETTE_CROSS_W; j++)
		{
			tmp[0] = tmp[1] = tmp[2] = pcol[(mem_cross[i] >> j) & 1];
			tmp += 3;
		}
		tmp += PALETTE_W3 - PALETTE_CROSS_W * 3;
	}
}

static void copy_num(int index, int tx, int ty)
{
	static const unsigned char pcol[2] = { 0, PALETTE_TEXT_GREY };
	unsigned char *tmp = mem_pals + ty * PALETTE_W3 + tx * 3;
	int i, j, n, d, v = index;

	for (d = 100; d; d /= 10 , tmp += (PALETTE_DIGIT_W + 1) * 3)
	{
		if ((index < d) && (d > 1)) continue;
		v -= (n = v / d) * d;
		n *= PALETTE_DIGIT_H;
		for (i = 0; i < PALETTE_DIGIT_H; i++)
		{
			for (j = 0; j < PALETTE_DIGIT_W; j++)
			{
				tmp[0] = tmp[1] = tmp[2] =
					pcol[(xbm_n7x7_bits[n + i] >> j) & 1];
				tmp += 3;
			}
			tmp += PALETTE_W3 - PALETTE_DIGIT_W * 3;
		}
		tmp -= PALETTE_DIGIT_H * PALETTE_W3;
	}
}

void mem_pal_init()			// Redraw whole palette
{
	int i;

	memset(mem_pals, 0, PALETTE_WIDTH * PALETTE_HEIGHT * 3);
	for (i = 0; i < mem_cols; i++)
	{
		repaint_swatch(i);
		copy_num(i, PALETTE_INDEX_X, i * PALETTE_SWATCH_H +
			PALETTE_SWATCH_Y + PALETTE_INDEX_DY);	// Index number
	}
}

void mem_pal_load_def()					// Load default palette
{
	mem_pal_copy( mem_pal, mem_pal_def );
	mem_cols = mem_pal_def_i;
}

void mem_mask_init()		// Initialise RGB protection mask array
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
	mem_mask_init();
}

void mem_get_histogram(int channel)	// Calculate how many of each colour index is on the canvas
{
	int i, j = mem_width * mem_height;
	unsigned char *img = mem_img[channel];

	memset(mem_histogram, 0, sizeof(mem_histogram));

	for (i = 0; i < j; i++) mem_histogram[*img++]++;
}

void do_transform(int start, int step, int cnt, unsigned char *mask,
	unsigned char *imgr, unsigned char *img0)
{
	static const int ixx[7] = {0, 1, 2, 0, 1, 2, 0};
	static unsigned char gamma_table[256], bc_table[256], ps_table[256];
	static int last_gamma, last_br, last_co, last_ps;
	int do_gamma, do_bc, do_sa, do_ps;
	unsigned char rgb[3], fmask;
	int br, co, sa;
	int dH, sH, tH, ix0, ix1, ix2, c0, c1, c2, dc = 0;
	int opacity, op0 = 0, op1 = 0, op2 = 0, ops;
	int j, mstep, r, g, b, ofs3;

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

	// Map bitwise to truncated
	do_ps = posterize_mode ? mem_prev_bcsp[3] : 1 << mem_prev_bcsp[3];
	// Disable if 1:1, else separate truncated from rounded
	if (do_ps &= 255) do_ps += (posterize_mode > 1) << 8;

	do_gamma = mem_prev_bcsp[4] - 100;
	do_bc = br | (co - 255);
	do_sa = sa - 255;

	/* Prepare posterize table */
	if (do_ps && (do_ps != last_ps))
	{
		int mul = do_ps & 255, div = 256, add = 0, div2 = mul - 1;
		int i, j;

		last_ps = do_ps;
		if (do_ps > 255) // Rounded
		{
			mul += mul - 2;
			div = 255 * 2;
			add = 255;
		}
		for (i = 0; i < 256; i++)
		{
			j = (i * mul + add) / div;
			ps_table[i] = (j * 255 * 2 + div2) / (div2 + div2);
		}
	}
	/* Prepare gamma table */
	if (do_gamma && (do_gamma != last_gamma))
	{
		double w;
		int i;

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
		int i, j;

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

	/* Use fake mask if no real one provided */
	if (!mask) mask = &fmask , mstep = 0 , fmask = 0;
	else mask += start , mstep = step;

	start *= 3; step *= 3; cnt *= 3; // Step by triples
	for (ofs3 = start; ofs3 < cnt; ofs3 += step , mask += mstep)
	{
		rgb[0] = img0[ofs3 + 0];
		rgb[1] = img0[ofs3 + 1];
		rgb[2] = img0[ofs3 + 2];
		opacity = *mask;
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
		if (do_ps)
		{
			r = ps_table[r];
			g = ps_table[g];
			b = ps_table[b];
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

static unsigned char pal_dupes[256];

int scan_duplicates()			// Find duplicate palette colours, return number found
{
	int i, j, c, found = 0;

	for (i = mem_cols - 1; i > 0; i--)
	{
		pal_dupes[i] = i;		// Start with a clean sheet
		c = PNG_2_INT(mem_pal[i]);
		for (j = 0; j < i; j++)
		{
			if (c != PNG_2_INT(mem_pal[j])) continue;
			found++;
			pal_dupes[i] = j;	// Point to first duplicate in the palette
			break;
		}
	}

	return found;
}

void remove_duplicates()	// Remove duplicate palette colours - call AFTER scan_duplicates
{
	do_xlate(pal_dupes, mem_img[CHN_IMAGE], mem_width * mem_height);
}

int mem_remove_unused_check()
{
	int i, found = 0;

	mem_get_histogram(CHN_IMAGE);
	for (i = 0; i < mem_cols; i++)
		if (!mem_histogram[i]) found++;

	if (!found) return 0;	// All palette colours are used on the canvas
	// Leave at least one colour even if canvas is 0x0
	return (mem_cols > found ? found : mem_cols - 1);
}

int mem_remove_unused()
{
	unsigned char conv[256], *img;
	int i, j, found = mem_remove_unused_check();

	if (found <= 0) return (0);

	conv[0] = 0; // Ensure this works even with empty histogram
	for (i = j = 0; i < 256; i++)	// Create conversion table
	{
		if (mem_histogram[i])
		{
			mem_pal[j] = mem_pal[i];
			conv[i] = j++;
		}
	}

	if ( mem_xpm_trans >= 0 )		// Re-adjust transparent colour index if it exists
	{
		if ( mem_histogram[mem_xpm_trans] == 0 )
			mem_xpm_trans = -1;	// No transparent pixels exist so remove reference
		else
			mem_xpm_trans = conv[mem_xpm_trans];	// New transparency colour position
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

// Generate black-to-white palette
void mem_bw_pal(png_color *pal, int i1, int i2)
{
	int i, j, step = i2 > i1 ? 1 : -1, d = abs(i2 - i1);

	if (!d) return;
	for (i = i1 , j = d , d += d; i != i2 + step; i += step , j += 255 * 2)
	{
		pal[i].red = pal[i].green = pal[i].blue = j / d;
	}
}

void transform_pal(png_color *pal1, png_color *pal2, int p1, int p2)
{
	int i, l = p2 - p1 + 1;
	unsigned char tmp[256 * 3], *wrk;


	wrk = tmp; pal2 += p1;
	for (i = 0; i < l; i++ , wrk += 3 , pal2++)
	{
		wrk[0] = pal2->red;
		wrk[1] = pal2->green;
		wrk[2] = pal2->blue;
	}

	do_transform(0, 1, l, NULL, tmp, tmp);

	wrk = tmp; pal1 += p1;
	for (i = 0; i < l; i++ , wrk += 3 , pal1++)
	{
		pal1->red = wrk[0];
		pal1->green = wrk[1];
		pal1->blue = wrk[2];
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

void do_convert_rgb(int start, int step, int cnt, unsigned char *dest,
	unsigned char *src)
{
	int i, s3 = step * 3;

	dest += start * 3;
	cnt = start + step * cnt;
	for (i = start; i < cnt; i += step)
	{
		png_color *col = mem_pal + src[i];
		dest[0] = col->red;
		dest[1] = col->green;
		dest[2] = col->blue;
		dest += s3;
	}
}

int mem_convert_rgb()			// Convert image to RGB
{
	char *old_image = mem_img[CHN_IMAGE];
	int res;

	res = undo_next_core(UC_NOCOPY, mem_width, mem_height, 3, CMASK_IMAGE);
	if (res) return (res);	// Not enough memory
	do_convert_rgb(0, 1, mem_width * mem_height, mem_img[CHN_IMAGE], old_image);
	return (0);
}

// Convert colours list into palette
void mem_cols_found(png_color *userpal)
{
	int i, j;

	for (i = 0; i < 256; i++)
	{
		j = found[i];
		userpal[i].red = INT_2_R(j);
		userpal[i].green = INT_2_G(j);
		userpal[i].blue = INT_2_B(j);
	}
}

// Convert RGB image to Indexed Palette - call after mem_cols_used
int mem_convert_indexed()
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
			if (found[k] == pix) break;
		}
		if (k > 255) return (1);	// No index found - BAD ERROR!!
		*new_image++ = k;
		old_image += 3;
	}

	return (0);
}

/* Max-Min quantization algorithm - good for preserving saturated colors,
 * and because of that, bad when used without dithering - WJ */

#define HISTSIZE (64 * 64 * 64)
int maxminquan(unsigned char *inbuf, int width, int height, int quant_to,
	png_color *userpal)
{
	int i, j, k, ii, r, g, b, dr, dg, db, l = width * height, *hist;

	/* Allocate histogram */
	hist = calloc(1, HISTSIZE * sizeof(int));
	if (!hist) return (-1);

	/* Fill histogram */
	for (i = 0; i < l; i++)
	{
		++hist[((inbuf[0] & 0xFC) << 10) + ((inbuf[1] & 0xFC) << 4) +
			(inbuf[2] >> 2)];
		inbuf += 3;
	}

	/* Find the most frequent color */
	j = k = -1;
	for (i = 0; i < HISTSIZE; i++)
	{
		if (hist[i] <= k) continue;
		j = i; k = hist[i];
	}

	/* Make it first */
	userpal[0].red = r = j >> 12;
	userpal[0].green = g = (j >> 6) & 0x3F;
	userpal[0].blue = b = j & 0x3F;

	/* Find distances from all others to it */
	if (quant_to > 1)
	{
		for (i = 0; i < HISTSIZE; i++)
		{
			if (!hist[i]) continue;
			dr = (i >> 12) - r;
			dg = ((i >> 6) & 0x3F) - g;
			db = (i & 0x3F) - b;
			hist[i] = dr * dr + dg * dg + db * db;
		}
	}

	/* Add more colors */
	for (ii = 1; ii < quant_to; ii++)
	{
		/* Find farthest color */
		j = -1;
		for (i = k = 0; i < HISTSIZE; i++)
		{
			if (hist[i] <= k) continue;
			j = i; k = hist[i];
		}
		/* No more colors? */
		if (j < 0) break;

		/* Store into palette */
		userpal[ii].red = r = j >> 12;
		userpal[ii].green = g = (j >> 6) & 0x3F;
		userpal[ii].blue = b = j & 0x3F;

		/* Update distances */
		for (i = 0; i < HISTSIZE; i++)
		{
			if (!hist[i]) continue;
			dr = (i >> 12) - r;
			dg = ((i >> 6) & 0x3F) - g;
			db = (i & 0x3F) - b;
			k = dr * dr + dg * dg + db * db;
			if (k < hist[i]) hist[i] = k;
		}
	}

	/* Upconvert colors */
	for (i = 0; i < ii; i++)
	{
		userpal[i].red = (userpal[i].red << 2) + (userpal[i].red >> 4);
		userpal[i].green = (userpal[i].green << 2) + (userpal[i].green >> 4);
		userpal[i].blue = (userpal[i].blue << 2) + (userpal[i].blue >> 4);
	}

	/* Clear empty slots */
	for (i = ii; i < quant_to; i++)
		userpal[i].red = userpal[i].green = userpal[i].blue = 0;

	free(hist);
	return (0);
}

/* Pairwise Nearest Neighbor quantization algorithm - minimizes mean square
 * error measure; time used is proportional to number of bins squared - WJ */

typedef struct {
	double rc, gc, bc, err;
	int cnt;
	unsigned short nn, fw, bk, tm, mtm;
} pnnbin;

static void find_nn(pnnbin *bins, int idx)
{
	pnnbin *bin1, *bin2;
	int i, nn = 0;
	double n1, wr, wg, wb, err = 1e100;

	bin1 = bins + idx;
	n1 = bin1->cnt;
	wr = bin1->rc;
	wg = bin1->gc;
	wb = bin1->bc;
	for (i = bin1->fw; i; i = bin2->fw)
	{
		double nerr, n2;

		bin2 = bins + i;
		nerr = (bin2->rc - wr) * (bin2->rc - wr) +
			(bin2->gc - wg) * (bin2->gc - wg) +
			(bin2->bc - wb) * (bin2->bc - wb);
		n2 = bin2->cnt;
		nerr *= (n1 * n2) / (n1 + n2);
		if (nerr >= err) continue;
		err = nerr;
		nn = i;
	}
	bin1->err = err;
	bin1->nn = nn;
}

int pnnquan(unsigned char *inbuf, int width, int height, int quant_to,
	png_color *userpal)
{
	unsigned short heap[32769];
	pnnbin *bins, *tb, *nb;
	double d, err, n1, n2;
	int i, j, k, l, l2, h, b1, maxbins, extbins, res = 1;


	heap[0] = 0; // Empty
	bins = calloc(32768, sizeof(pnnbin));
	if (!bins) return (-1);

	progress_init(_("Quantize Pass 1"), 1);

	/* Build histogram */
	k = width * height;
	for (i = 0; i < k; i++ , inbuf += 3)
	{
// !!! Can throw gamma correction in here, but what to do about perceptual
// !!! nonuniformity then?
		j = ((inbuf[0] & 0xF8) << 7) + ((inbuf[1] & 0xF8) << 2) +
			(inbuf[2] >> 3);
		tb = bins + j;
		tb->rc += inbuf[0]; tb->gc += inbuf[1]; tb->bc += inbuf[2];
		tb->cnt++;
	}

	/* Cluster nonempty bins at one end of array */
	tb = bins;
	for (i = 0; i < 32768; i++)
	{
		if (!bins[i].cnt) continue;
		*tb = bins[i];
		d = 1.0 / (double)tb->cnt;
		tb->rc *= d; tb->gc *= d; tb->bc *= d;
		if (quan_sqrt) tb->cnt = sqrt(tb->cnt);
		tb++;
	}
	maxbins = tb - bins;
	for (i = 0; i < maxbins - 1; i++)
	{
		bins[i].fw = i + 1;
		bins[i + 1].bk = i;
	}
// !!! Already zeroed out by calloc()
//	bins[0].bk = bins[i].fw = 0;

	/* Initialize nearest neighbors and build heap of them */
	for (i = 0; i < maxbins; i++)
	{
		if (((i * 50) % maxbins >= maxbins - 50))
			if (progress_update((float)i / maxbins)) goto quit;

		find_nn(bins, i);
		/* Push slot on heap */
		err = bins[i].err;
		for (l = ++heap[0]; l > 1; l = l2)
		{
			l2 = l >> 1;
			if (bins[h = heap[l2]].err <= err) break;
			heap[l] = h;
		}
		heap[l] = i;
	}

	progress_end();
	progress_init(_("Quantize Pass 2"), 1);

	/* Merge bins which increase error the least */
	extbins = maxbins - quant_to;
	for (i = 0; i < extbins; )
	{
		if (((i * 50) % extbins >= extbins - 50))
			if (progress_update((float)i / extbins)) goto quit;

		/* Use heap to find which bins to merge */
		while (TRUE)
		{
			tb = bins + (b1 = heap[1]); /* One with least error */
			/* Is stored error up to date? */
			if ((tb->tm >= tb->mtm) &&
				(bins[tb->nn].mtm <= tb->tm)) break;
			if (tb->mtm == 0xFFFF) /* Deleted node */
				b1 = heap[1] = heap[heap[0]--];
			else /* Too old error value */
			{
				find_nn(bins, b1);
				tb->tm = i;
			}
			/* Push slot down */
			err = bins[b1].err;
			for (l = 1; (l2 = l + l) <= heap[0]; l = l2)
			{
				if ((l2 < heap[0]) && (bins[heap[l2]].err >
					bins[heap[l2 + 1]].err)) l2++;
				if (err <= bins[h = heap[l2]].err) break;
				heap[l] = h;
			}
			heap[l] = b1;
		}

		/* Do a merge */
		nb = bins + tb->nn;
		n1 = tb->cnt;
		n2 = nb->cnt;
		d = 1.0 / (n1 + n2);
		tb->rc = d * rint(n1 * tb->rc + n2 * nb->rc);
		tb->gc = d * rint(n1 * tb->gc + n2 * nb->gc);
		tb->bc = d * rint(n1 * tb->bc + n2 * nb->bc);
		tb->cnt += nb->cnt;
		tb->mtm = ++i;

		/* Unchain deleted bin */
		bins[nb->bk].fw = nb->fw;
		bins[nb->fw].bk = nb->bk;
		nb->mtm = 0xFFFF;
	}

	/* Fill palette */
	i = j = 0;
	while (TRUE)
	{
		userpal[j].red = rint(bins[i].rc);
		userpal[j].green = rint(bins[i].gc);
		userpal[j++].blue = rint(bins[i].bc);
		if (!(i = bins[i].fw)) break;
	}

	/* Clear empty slots */
	for (; j < quant_to; j++)
		userpal[j].red = userpal[j].green = userpal[j].blue = 0;
	res = 0;

quit:	progress_end();
	free(bins);
	return (res);
}

/* Distance functions for 3 distance measures */

#if defined(__GNUC__) && defined(__i386__)
#define REGPARM2 __attribute__ ((regparm (2)))
#else
#define REGPARM2
#endif

typedef double REGPARM2 (*distance_func)(const double *v0, const double *v1);

static double REGPARM2 distance_linf(const double *v0, const double *v1)
{
	double td, td2;

	td = fabs(v0[0] - v1[0]);
	td2 = fabs(v0[1] - v1[1]);
	if (td < td2) td = td2;
	td2 = fabs(v0[2] - v1[2]);
	if (td < td2) td = td2;
	return (td);
}

static double REGPARM2 distance_l1(const double *v0, const double *v1)
{
	return (fabs(v0[0] - v1[0]) + fabs(v0[1] - v1[1]) + fabs(v0[2] - v1[2]));
}

static double REGPARM2 distance_l2(const double *v0, const double *v1)
{
	return (sqrt((v0[0] - v1[0]) * (v0[0] - v1[0]) +
		(v0[1] - v1[1]) * (v0[1] - v1[1]) +
		(v0[2] - v1[2]) * (v0[2] - v1[2])));
}

static const distance_func distance_3d[NUM_DISTANCES] = {
	distance_linf, distance_l1, distance_l2
};

/* Dithering works with 6-bit colours, because hardware VGA palette is 6-bit,
 * and any kind of dithering is imprecise by definition anyway - WJ */

typedef struct {
	double xyz256[768], gamma[256 * 2], lin[256 * 2];
	int cspace, cdist, ncols;
	guint32 xcmap[64 * 64 * 2 + 128 * 2]; /* Cache bitmap */
	guint32 lcmap[64 * 64 * 2]; /* Extension bitmap */
	unsigned char cmap[64 * 64 * 64 + 128 * 64]; /* Index cache */
} ctable;

static ctable *ctp;

/* !!! Beware of GCC misoptimizing this! The two functions below is the result
 * of much trial and error, and hopefully not VERY brittle; but still, after any
 * modification to them, compare the performance to what it was before - WJ */

static int find_nearest(int col[3], int n)
{
	/* !!! Stack misalignment is a very real issue here */
	unsigned char tmp_[4 * sizeof(double)];
	double *tmp = ALIGNED(tmp_, sizeof(double));

	
	/* Prepare colour coords */
	switch (ctp->cspace)
	{
	default:
	case CSPACE_RGB:
		tmp[0] = ctp->lin[n + col[0]];
		tmp[1] = ctp->lin[n + col[1]];
		tmp[2] = ctp->lin[n + col[2]];
		break;
	case CSPACE_SRGB:
		tmp[0] = ctp->gamma[n + col[0]];
		tmp[1] = ctp->gamma[n + col[1]];
		tmp[2] = ctp->gamma[n + col[2]];
		break;
	case CSPACE_LXN:
		rgb2LXN(tmp, ctp->gamma[n + col[0]], ctp->gamma[n + col[1]],
			ctp->gamma[n + col[2]]);
		break;
	}

	/* Find nearest colour */
	{
		const distance_func dist = distance_3d[ctp->cdist];
		double d = 1000000000.0, td, *xyz = ctp->xyz256;
		int i, j, l = ctp->ncols;

		for (i = j = 0; i < l; i++)
		{
			td = dist(tmp, xyz + i * 3);
			if (td < d) j = i , d = td;
		}
		return (j);
	}
}

static int lookup_srgb(double *srgb)
{
	int k, n = 0, col[3];

	/* Convert to 8-bit RGB coords */
	col[0] = UNGAMMA256(srgb[0]);
	col[1] = UNGAMMA256(srgb[1]);
	col[2] = UNGAMMA256(srgb[2]);

	/* Check if there is extended precision */
	k = ((col[0] & 0xFC) << 10) + ((col[1] & 0xFC) << 4) + (col[2] >> 2);
	if (ctp->lcmap[k >> 5] & (1 << (k & 31))) k = 64 * 64 * 64 +
		ctp->cmap[k] * 64 + ((col[0] & 3) << 4) +
		((col[1] & 3) << 2) + (col[2] & 3);
	else n = 256; /* Use posterized values for 6-bit part */

	/* Use colour cache if possible */
	if (!(ctp->xcmap[k >> 5] & (1 << (k & 31))))
	{
		ctp->xcmap[k >> 5] |= 1 << (k & 31);
		ctp->cmap[k] = find_nearest(col, n);
	}

	return (ctp->cmap[k]);
}

// !!! No support for transparency yet !!!
/* Damping functions roughly resemble old GIMP's behaviour, but may need some
 * tuning because linear sRGB is just too different from normal RGB */
int mem_dither(unsigned char *old, int ncols, short *dither, int cspace,
	int dist, int limit, int selc, int serpent, int rgb8b, double emult)
{
	int i, j, k, l, kk, j0, j1, dj, rlen, col0, col1, progress;
	unsigned char *ddata, *src, *dest;
	double *row0, *row1, *row2, *tmp;
	double err, intd, extd, *gamma6, *lin6;
	double tc0[3], tc1[3], color0[3], color1[3];
	double fdiv = 0, gamut[6] = {1, 1, 1, 0, 0, 0};

	/* Allocate working space */
	rlen = (mem_width + 4) * 3 * sizeof(double);
	ddata = multialloc(MA_ALIGN_DOUBLE,
		&row0, rlen,
		&row1, rlen,
		&row2, rlen,
		&ctp, sizeof(ctable),
		NULL);
	if (!ddata) return (1);

	if ((progress = mem_width * mem_height > 1000000))
		progress_init(_("Converting to Indexed Palette"), 0);

	/* Preprocess palette to find whether to extend precision and where */
	for (i = 0; i < ncols; i++)
	{
		j = ((mem_pal[i].red & 0xFC) << 10) +
			((mem_pal[i].green & 0xFC) << 4) +
			(mem_pal[i].blue >> 2);
		if (!(l = ctp->cmap[j]))
		{
			ctp->cmap[j] = l = i + 1;
			ctp->xcmap[l * 4 + 2] = j;
		}
		k = ((mem_pal[i].red & 3) << 4) +
			((mem_pal[i].green & 3) << 2) +
			(mem_pal[i].blue & 3);
		ctp->xcmap[l * 4 + (k & 1)] |= 1 << (k >> 1);
	}
	memset(ctp->cmap, 0, 64 * 64 * 64);
	for (k = 0 , i = 4; i < 256 * 4; i += 4)
	{
		guint32 v = ctp->xcmap[i] | ctp->xcmap[i + 1];
		/* Are 2+ colors there somewhere? */
		if (!((v & (v - 1)) | (ctp->xcmap[i] & ctp->xcmap[i + 1])))
			continue;
		rgb8b = TRUE; /* Force 8-bit precision */
		j = ctp->xcmap[i + 2];
		ctp->lcmap[j >> 5] |= 1 << (j & 31);
		ctp->cmap[j] = k++;
	}
	memset(ctp->xcmap, 0, 257 * 4 * sizeof(guint32));

	/* Prepare tables */
	for (i = 0; i < 256; i++)
	{
		j = (i & 0xFC) + (i >> 6);
		ctp->gamma[i] = gamma256[i];
		ctp->gamma[i + 256] = gamma256[j];
		ctp->lin[i] = i * (1.0 / 255.0);
		ctp->lin[i + 256] = j * (1.0 / 255.0);
	}
	/* Keep all 8 bits of input or posterize to 6 bits? */
	i = rgb8b ? 0 : 256;
	gamma6 = ctp->gamma + i; lin6 = ctp->lin + i;
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
		case CSPACE_RGB:
			tmp[0] = lin6[mem_pal[i].red];
			tmp[1] = lin6[mem_pal[i].green];
			tmp[2] = lin6[mem_pal[i].blue];
			break;
		case CSPACE_SRGB:
			break; /* Done already */
		case CSPACE_LXN:
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
		memset(row2, 0, rlen);
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
					err = err < -0.1 ? (err < -0.4 ?
						-0.25 : 0.5 * err - 0.05) :
						err > 0.1 ? (err > 0.4 ?
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
		if (progress && (i * 10) % mem_height >= mem_height - 10)
			progress_update((float)(i + 1) / mem_height);
	}

	if (progress) progress_end();
	free(ddata);
	return (0);
}

/* Dumb (but fast) Floyd-Steinberg dithering in RGB space, loosely based on
 * Dennis Lee's dithering implementation from dl3quant.c, in turn based on
 * dithering code from the IJG's jpeg library - WJ */
int mem_dumb_dither(unsigned char *old, unsigned char *new, png_color *pal,
	int width, int height, int ncols, int dither)
{
	unsigned short cols[32768], sqrtb[512], *sqr;
	short limtb[512], *lim, fr[3] = {0, 0, 0};
	short *rows = NULL, *row0 = fr, *row1 = fr;
	unsigned char clamp[768], *src, *dest;
	int i, j, k, j0, dj, dj3, r, g, b, rlen, serpent = 2;

	/* Allocate working space */
	rlen = (width + 2) * 3;
	if (dither)
	{
		rows = calloc(rlen * 2, sizeof(short));
		if (!rows) return (1);
		serpent = 0;
	}

	/* Color cache, squares table, clamp table */
	memset(cols, 0, sizeof(cols));
	sqr = sqrtb + 256;
	for (i = -255; i < 256; i++) sqr[i] = i * i;
	memset(clamp, 0, 256);
	memset(clamp + 512, 255, 256);
	for (i = 0; i < 256; i++) clamp[i + 256] = i;

	/* Error limiter table, Dennis Lee's way */
#define ERR_LIM 20
	lim = limtb + 256;
	for (i = 0; i < ERR_LIM; i++)
		lim[i] = i , lim[-i] = -i;
	for (; i < 256; i++)
		lim[i] = ERR_LIM , lim[-i] = -ERR_LIM;
#undef ERR_LIM

	/* Process image */
	for (i = 0; i < height; i++)
	{
		src = old + i * width * 3;
		dest = new + i * width;
		if (serpent ^= 1)
		{
			j0 = 0; dj = 1;
		}
		else
		{
			j0 = (width - 1) * 3; dj = -1;
			dest += width - 1;
		}
		if (dither)
		{
			row0 = row1 = rows + 3;
			*(serpent ? &row1 : &row0) += rlen;
			memset(row1 - 3, 0, rlen * sizeof(short));
			src += j0; row0 += j0; row1 += j0;
		}
		dj3 = dj * 3;
		for (j = 0; j < width; j++ , src += dj3 , dest += dj)
		{
			r = clamp[src[0] + ((row0[0] + 0x1008) >> 4)];
			g = clamp[src[1] + ((row0[1] + 0x1008) >> 4)];
			b = clamp[src[2] + ((row0[2] + 0x1008) >> 4)];
			k = ((r & 0xF8) << 7) + ((g & 0xF8) << 2) + (b >> 3);
			if (!cols[k]) /* Find nearest color in RGB */
			{
				int i, j, n = 0, l = 1000000;

/* Searching for color nearest to first color in cell, instead of to cell
 * itself, looks like a bug, but works like a feature - makes FS dither less
 * prone to patterning. This trick I learned from Dennis Lee's code - WJ */
				for (i = 0; i < ncols; i++)
				{
					j = sqr[r - pal[i].red] +
						sqr[g - pal[i].green] +
						sqr[b - pal[i].blue];
					if (j >= l) continue;
					l = j; n = i;
				}
				cols[k] = n + 1;
			}
			*dest = k = cols[k] - 1;
			if (!dither) continue;

			r = lim[r - pal[k].red];
			g = lim[g - pal[k].green];
			b = lim[b - pal[k].blue];
			k = r + r;
			row1[0 + dj3] += r;
			row1[0 - dj3] += (r += k);
			row1[0 + 0  ] += (r += k);
			row0[0 + dj3] += r + k;
			k = g + g;
			row1[1 + dj3] += g;
			row1[1 - dj3] += (g += k);
			row1[1 + 0  ] += (g += k);
			row0[1 + dj3] += g + k;
			k = b + b;
			row1[2 + dj3] += b;
			row1[2 - dj3] += (b += k);
			row1[2 + 0  ] += (b += k);
			row0[2 + dj3] += b + k;
			row0 += dj3; row1 += dj3;
		}
	}

	free(rows);
	return (0);
}

void mem_find_dither(int red, int green, int blue)
{
	int pat = 4, dp = 3;
	int i, ix1, ix2, pn, tpn, pp2 = pat * pat * 2;
	double r, g, b, r1, g1, b1, dr0, dg0, db0, dr, dg, db;
	double l, l2, tl, t;

	r = gamma256[red];
	g = gamma256[green];
	b = gamma256[blue];
	l = 16.0; ix1 = -1;

	for (i = 0; i < mem_cols; i++)
	{
		dr = r - gamma256[mem_pal[i].red];
		dg = g - gamma256[mem_pal[i].green];
		db = b - gamma256[mem_pal[i].blue];
		tl = dr * dr + dg * dg + db * db;
		if (tl >= l) continue;
		l = tl;
		ix1 = i;
	}

	r1 = gamma256[mem_pal[ix1].red];
	g1 = gamma256[mem_pal[ix1].green];
	b1 = gamma256[mem_pal[ix1].blue];
	dr0 = r - r1;
	dg0 = g - g1;
	db0 = b - b1;

	l2 = l; ix2 = ix1; pn = 0;

	for (i = 0; i < mem_cols; i++)
	{
		if (i == ix1) continue;
		dr = gamma256[mem_pal[i].red] - r1;
		dg = gamma256[mem_pal[i].green] - g1;
		db = gamma256[mem_pal[i].blue] - b1;
		t = pp2 * (dr0 * dr + dg0 * dg + db0 * db) /
			(dr * dr + dg * dg + db * db);
		if ((t <= dp) || (t >= pp2 - dp)) continue;
		t = (tpn = rint(0.5 * t)) / (double)(pp2 >> 1);
		dr = dr * t - dr0;
		dg = dg * t - dg0;
		db = db * t - db0;
		tl = dr * dr + dg * dg + db * db;
		if (tl >= l2) continue;
		l2 = tl;
		ix2 = i;
		pn = tpn;
	}

	mem_col_A = ix1;
	mem_col_B = ix2;
/* !!! A mix with less than half of nearest color cannot be better than it, so
 * !!! patterns less dense than 50:50 won't be needed */
	mem_tool_pat = pat == 4 ? pn : pn * 4;

	mem_col_A24 = mem_pal[mem_col_A];
	mem_col_B24 = mem_pal[mem_col_B];
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

/* Convert image to greyscale */
void mem_greyscale(int gcor)
{
	unsigned char *mask, *img = mem_img[CHN_IMAGE];
	int i, j, k, v, ch;
	double value;

	if ( mem_img_bpp == 1)
	{
		for (i = 0; i < 256; i++)
		{
			if (gcor) /* Gamma correction + Helmholtz-Kohlrausch effect */
			{
				value = rgb2B(gamma256[mem_pal[i].red],
					gamma256[mem_pal[i].green],
					gamma256[mem_pal[i].blue]);
				v = UNGAMMA256(value);
			}
			else /* Usual braindead formula */
			{
				value = 0.299 * mem_pal[i].red +
					0.587 * mem_pal[i].green +
					0.114 * mem_pal[i].blue;
				v = (int)rint(value);
			}
			mem_pal[i].red = v;
			mem_pal[i].green = v;
			mem_pal[i].blue = v;
		}
	}
	else
	{
		mask = malloc(mem_width);
		if (!mask) return;
		ch = mem_channel;
		mem_channel = CHN_IMAGE;
		for (i = 0; i < mem_height; i++)
		{
			row_protected(0, i, mem_width, mask);
			for (j = 0; j < mem_width; j++)
			{
				if (gcor) /* Gamma + H-K effect */
				{
					value = rgb2B(gamma256[img[0]],
						gamma256[img[1]],
						gamma256[img[2]]);
					v = UNGAMMA256(value);
				}
				else /* Usual */
				{
					value = 0.299 * img[0] + 0.587 * img[1] +
						0.114 * img[2];
					v = (int)rint(value);
				}
				v *= 255 - mask[j];
				k = *img * mask[j] + v;
				*img++ = (k + (k >> 8) + 1) >> 8;
				k = *img * mask[j] + v;
				*img++ = (k + (k >> 8) + 1) >> 8;
				k = *img * mask[j] + v;
				*img++ = (k + (k >> 8) + 1) >> 8;
			}
		}
		mem_channel = ch;
		free(mask);
	}
}

/* Valid for x=0..5, which is enough here */
#define MOD3(x) ((((x) * 5 + 1) >> 2) & 3)

/* Nonclassical HSV: H is 0..6, S is 0..1, V is 0..255 */
void rgb2hsv(unsigned char *rgb, double *hsv)
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
	c1 = (c2 + c0) ^ 3;
	hsv[2] = rgb[c0] > rgb[c1] ? rgb[c0] : rgb[c1];
	hsv[1] = hsv[2] - rgb[c2];
	hsv[0] = c0 * 2 + 1 + (rgb[c1] - rgb[c0]) / hsv[1];
	hsv[1] /= hsv[2];
}

void hsv2rgb(unsigned char *rgb, double *hsv)
{
	double h0, h1, h2;
	int i;

	h2 = hsv[2] * 2;
	h1 = h2 * (1.0 - hsv[1]);
	i = hsv[0];
	h0 = (hsv[0] - i) * (h2 - h1);
	if (i & 1) h2 -= h0 , h0 += h2;
	else h0 += h1;
	i >>= 1;
	rgb[i] = ((int)h2 + 1) >> 1;
	rgb[MOD3(i + 1)] = ((int)h0 + 1) >> 1;
	rgb[MOD3(i + 2)] = ((int)h1 + 1) >> 1;
}

static double rgb_hsl(int t, png_color col)
{
	double hsv[3];
	unsigned char rgb[3] = {col.red, col.green, col.blue};

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
	double lxnA[3], lxn[3];

	if ( i2 == i1 || i1>mem_cols || i2>mem_cols ) return;
	if ( i2 < i1 )
	{
		i = i1;
		i1 = i2;
		i2 = i;
	}

	if (a == 4) get_lxn(lxnA, PNG_2_INT(mem_col_A24));
	if (a == 9) mem_get_histogram(CHN_IMAGE);
	
	for (i = 0; i < 256; i++)
		tab0[i] = i;
	for (i = i1; i <= i2; i++)
	{
		switch (a)
		{
		/* Hue */
		case 0: tab1[i] = rint(1024 * rgb_hsl(0, mem_pal[i]));
			break;
		/* Saturation */
		case 1: tab1[i] = rint(1024 * rgb_hsl(1, mem_pal[i]));
			break;
		/* Luminance */
		case 2: tab1[i] = rint(1024 * rgb_hsl(2, mem_pal[i]));
			break;
		/* Brightness */
		case 3: tab1[i] = rint(1024 * rgb2B(gamma256[mem_pal[i].red],
			gamma256[mem_pal[i].green], gamma256[mem_pal[i].blue]));
			break;
		/* Distance to A */
		case 4: get_lxn(lxn, PNG_2_INT(mem_pal[i]));
			tab1[i] = rint(1024 * ((lxn[0] - lxnA[0]) *
				(lxn[0] - lxnA[0]) + (lxn[1] - lxnA[1]) *
				(lxn[1] - lxnA[1]) + (lxn[2] - lxnA[2]) *
				(lxn[2] - lxnA[2])));
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
		unsigned char *mask = calloc(1, mem_width);

		j = mem_width * mem_height;
		if (mem_channel == CHN_IMAGE) j *= 3;
		img = mem_img[mem_channel];
		for (i = 0; i < j; i++)
		{
			*img++ ^= 255;
		}
		if (mask)
		{
			mask_merge(mem_undo_previous(mem_channel), mem_channel, mask);
			free(mask);
		}
	}
}

/* !!! The rectangles here exclude bottom & right border */
int clip(int *rxy, int x0, int y0, int x1, int y1, const int *vxy)
{
	rxy[0] = x0 < vxy[0] ? vxy[0] : x0;
	rxy[1] = y0 < vxy[1] ? vxy[1] : y0;
	rxy[2] = x1 > vxy[2] ? vxy[2] : x1;
	rxy[3] = y1 > vxy[3] ? vxy[3] : y1;
	return ((rxy[2] > rxy[0]) && (rxy[3] > rxy[1]));
}

/* Intersect outer & inner rectangle, write out 1 to 5 rectangles the outer one
 * separates into, return number of outer rectangles */
int clip4(int *rect04, int xo, int yo, int wo, int ho, int xi, int yi, int wi, int hi)
{
	int *p = rect04 + 4;
	int xo1 = xo + wo, yo1 = yo + ho, xi1 = xi + wi, yi1 = yi + hi;

	// Whole outer rectangle
	p[0] = xo; p[1] = yo; p[2] = wo; p[3] = ho;
	// No intersection
	if ((xi >= xo1) || (yi >= yo1) || (xo >= xi1) || (yo >= yi1))
	{
		rect04[0] = xi; rect04[1] = yi; rect04[2] = rect04[3] = 0;
		return (1);
	}
	if (yi > yo) // Top rectangle
		p[3] = yi - yo , p += 4;
	else yi = yo;
	if (yi1 < yo1) // Bottom rectangle
		*p++ = xo , *p++ = yi1 , *p++ = wo , *p++ = yo1 - yi1;
	else yi1 = yo1;
	hi = yi1 - yi;
	if (xi > xo) // Left rectangle
		*p++ = xo , *p++ = yi , *p++ = xi - xo , *p++ = hi;
	else xi = xo;
	if (xi1 < xo1) // Right rectangle
		*p++ = xi1 , *p++ = yi , *p++ = xo1 - xi1 , *p++ = hi;
	else xi1 = xo1;
	wi = xi1 - xi;
	// Clipped inner rectangle
	rect04[0] = xi; rect04[1] = yi; rect04[2] = wi; rect04[3] = hi;

	// Number of outer rectangles
	return ((p - rect04 - 4) >> 2);
}

void line_init(linedata line, int x0, int y0, int x1, int y1)
{
	int i;
	line[0] = x0;
	line[1] = y0;
	line[4] = (x1 - x0) * (line[6] = line[8] = x1 - x0 < 0 ? -1 : 1);
	line[5] = (y1 - y0) * (line[7] = line[9] = y1 - y0 < 0 ? -1 : 1);
	i = line[4] >= line[5]; /* More horizontal? */
	line[2] = line[3] = line[5 - i];
	line[4] = 2 * line[4 + i];
	line[5] = 2 * line[2];
	line[6 + i] = 0;
}

int line_step(linedata line)
{
	line[3] -= line[4];
	if (line[3] <= 0)
	{
		line[3] += line[5];
		line[0] += line[8];
		line[1] += line[9];
	}
	else
	{
		line[0] += line[6];
		line[1] += line[7];
	}
	return (--line[2]);
}

void line_nudge(linedata line, int x, int y)
{
	while ((line[0] != x) && (line[1] != y) && (line[2] >= 0))
		line_step(line);
}

/* !!! The clipping rectangle here includes both borders */
int line_clip(linedata line, const int *vxy, int *step)
{
	int vh = !line[6], hv = vh ^ 1, hs = line[8 + vh], vs = line[8 + hv];
	int dh = hs < 0, dv = vs < 0, l4 = line[4], steps[2];
	int i, j, dx, dy;

	for (i = 1; 1;)
	{
		dx = (vxy[(dh ^ i) * 2 + vh] - line[vh]) * hs + i;
		if (dx < 0) dx = 0;
		if (l4)
		{
			dy = (vxy[(dv ^ i) * 2 + hv] - line[hv]) * vs + i;
			if (dy < 0) dy = 0;
			dy = (line[5] * (dy - 1) + line[3] + l4 - 1) / l4;
			if ((dy > dx) ^ i) dx = dy;
		}
		steps[i] = dx;
		if (!i--) break;
	}

	if (line[5]) // Makes no sense for a single point
	{
		/* Too short? */
		if ((line[2] -= dx) < 0) return (line[2]);
		dy = (j = l4 * dx + line[5] - line[3]) / line[5];
		line[vh] += hs * dx; line[hv] += vs * dy;
		line[3] = line[5] * (dy + 1) - j;
	}
	/* Misses the rectangle? */
	if ((line[0] < vxy[0]) || (line[0] > vxy[2]) ||
		(line[1] < vxy[1]) || (line[1] > vxy[3])) return (line[2] = -1);
	*step = dx;
	j = steps[1] - dx - 1;
	if (j < line[2]) line[2] = j;
	return (line[2]);
}

void line_flip(linedata line)
{
	int l, d2;

	if (!line[2]) return; // Single point
	l = line[4] * line[2] + line[5] - line[3];
	d2 = l / line[5];
	line[3] = l - d2 * line[5] + 1;
	line[0] += line[2] * line[6] + d2 * (line[8] - line[6]);
	line[1] += line[2] * line[7] + d2 * (line[9] - line[7]);
	line[6] *= -1; line[7] *= -1;
	line[8] *= -1; line[9] *= -1;
}

/* Produce a horizontal segment from two connected lines */
static void twoline_segment(int *xx, linedata line1, linedata line2)
{
	xx[0] = xx[1] = line1[0];
	while (TRUE)
	{
		if (!line1[7]) /* Segments longer than 1 pixel */
		{
			while ((line1[2] > 0) && (line1[3] > line1[4]))
				line_step(line1);
		}
		if (xx[0] > line1[0]) xx[0] = line1[0];
		if (xx[1] < line1[0]) xx[1] = line1[0];
		if ((line1[2] > 0) || (line2[2] < 0)) break;
		memcpy(line1, line2, sizeof(linedata));
		line2[2] = -1;
		if (xx[0] > line1[0]) xx[0] = line1[0];
		if (xx[1] < line1[0]) xx[1] = line1[0];
	}
}

void sline( int x1, int y1, int x2, int y2 )		// Draw single thickness straight line
{
	linedata line;

	line_init(line, x1, y1, x2, y2);
	for (; line[2] >= 0; line_step(line))
	{
		IF_IN_RANGE(line[0], line[1]) put_pixel(line[0], line[1]);
	}
}

void circle_line(int x0, int y0, int dx, int dy, int thick);

void tline( int x1, int y1, int x2, int y2, int size )		// Draw size thickness straight line
{
	linedata line;
	int xdo, ydo, todo;

	xdo = abs(x2 - x1);
	ydo = abs(y2 - y1);
	todo = xdo > ydo ? xdo : ydo;
	if (todo < 2) return;	// The 1st and last points are done by calling procedure

	if (size < 2) /* One pixel wide */
	{
		sline(x1, y1, x2, y2);
		return;
	}

	/* Draw middle segment */
	circle_line(x1, y1, x2 - x1, y2 - y1, size);

	/* Add four more circles to cover all odd points */
	if (!xdo || !ydo || (xdo == ydo)) return; /* Not needed */
	line_init(line, x1, y1, x2, y2);
	line_nudge(line, x1 + line[8] - 2 * line[6],
		y1 + line[9] - 2 * line[7]); /* Jump to first diagonal step */
	f_circle(line[0], line[1], size);
	f_circle(line[0] - line[8], line[1] - line[9], size);
	line_nudge(line, x2, y2); /* Jump to last diagonal step */
	f_circle(line[0], line[1], size);
	f_circle(line[0] - line[8], line[1] - line[9], size);
}

/* Draw whatever is bounded by two pairs of lines */
void draw_quad(linedata line1, linedata line2, linedata line3, linedata line4)
{
	int x1, x2, y1, xx[4];
	for (; line1[2] >= 0; line_step(line1) , line_step(line3))
	{
		y1 = line1[1];
		twoline_segment(xx + 0, line1, line2);
		twoline_segment(xx + 2, line3, line4);
		if ((y1 < 0) || (y1 >= mem_height)) continue;
		if (xx[0] > xx[2]) xx[0] = xx[2];
		if (xx[1] < xx[3]) xx[1] = xx[3];
		x1 = xx[0] < 0 ? 0 : xx[0];
		x2 = xx[1] >= mem_width ? mem_width - 1 : xx[1];
		put_pixel_row(x1, y1, x2 - x1 + 1, NULL);
	}
}

/* Draw general parallelogram */
void g_para( int x1, int y1, int x2, int y2, int xv, int yv )
{
	linedata line1, line2, line3, line4;
	int i, j, x[2] = {x1, x2}, y[2] = {y1, y2};

	j = (y1 < y2) ^ (yv < 0); i = j ^ 1;
	line_init(line1, x[i], y[i], x[i] + xv, y[i] + yv);
	line_init(line2, x[i] + xv, y[i] + yv, x[j] + xv, y[j] + yv);
	line_init(line3, x[i], y[i], x[j], y[j]);
	line_init(line4, x[j], y[j], x[j] + xv, y[j] + yv);
	draw_quad(line1, line2, line3, line4);
}

/* Shapeburst engine */

int sb_rect[4];
static unsigned short *sb_buf;

static void put_pixel_sb(int x, int y)
{
	int j, x1, y1;

	x1 = x - sb_rect[0];
	y1 = y - sb_rect[1];
	if ((x1 < 0) || (x1 >= sb_rect[2]) || (y1 < 0) || (y1 >= sb_rect[3]))
		return;

	j = pixel_protected(x, y);
	if (IS_INDEXED ? j : j == 255) return;

	sb_buf[y1 * sb_rect[2] + x1] = 0xFFFF;
}

static void mask_select(unsigned char *mask, unsigned char *xsel, int l);

static void put_pixel_row_sb(int x, int y, int len, unsigned char *xsel)
{
	unsigned char mask[ROW_BUFLEN];
	int x1, y1, sb_ofs, offset, use_mask, masked;


	if (len <= 0) return;
	x1 = x - sb_rect[0];
	y1 = y - sb_rect[1];
	if ((x1 < 0) || (x1 >= sb_rect[2]) || (y1 < 0) || (y1 >= sb_rect[3]))
		return;
	if (x1 + len > sb_rect[2]) len = sb_rect[2] - x1;

	sb_ofs = y1 * sb_rect[2] + x1;
	offset = x + mem_width * y;
	masked = IS_INDEXED ? 1 : 255;
	use_mask = (mem_channel <= CHN_ALPHA) && mem_img[CHN_MASK] && !channel_dis[CHN_MASK];

	while (TRUE)
	{
		int i, l = len <= ROW_BUFLEN ? len : ROW_BUFLEN;

		prep_mask(0, 1, l, mask,
			use_mask ? mem_img[CHN_MASK] + offset : NULL,
			mem_img[CHN_IMAGE] + offset * mem_img_bpp);

		if (xsel)
		{
			mask_select(mask, xsel, l);
			xsel += l;
		}

		for (i = 0; i < l; i++)
			if (mask[i] < masked) sb_buf[sb_ofs + i] = 0xFFFF;

		if (!(len -= l)) return;
		sb_ofs += l;
		offset += l;
	}
}

/* Distance transform of binary image map;
 * for now, uses hardcoded L1 distance metric */
static int shapeburst(int w, int h, unsigned short *dmap)
{
	unsigned short *r0;
	int i, j, k, l, dx, dy, maxd;


	/* Calculate distance */
	r0 = dmap; dx = 1; dy = w; /* Forward pass */
	while (TRUE)
	{
		/* First row */
		for (i = 0; i < w; i++ , r0 += dx) if (*r0) *r0 = 1;
		/* Other rows */
		for (j = 1; j < h; j++)
		{
			/* 1st pixel */
			if (*r0) *r0 = 1; r0 += dx;
			/* Other pixels */
			for (i = w - 1; i > 0; i-- , r0 += dx)
			{
				k = *(r0 - dx); l = *(r0 - dy);
				if (k > l) k = l;
				if (*r0 > k) *r0 = k + 1;
			}
		}
		if (dx < 0) break; /* Both passes done */
		r0 = dmap + w * h - 1; dx = -1; dy = -w; /* Backward pass */
	}

	/* Find largest */
	maxd = 0; r0 = dmap;
	for (k = w * h; k; k-- , r0++) if (maxd < *r0) maxd = *r0;
	return (maxd);
}

int init_sb()
{
	sb_buf = calloc(sb_rect[2] * sb_rect[3], sizeof(unsigned short));
	if (!sb_buf)
	{
		memory_errors(1);
		return (FALSE);
	}
	put_pixel = put_pixel_sb;
	put_pixel_row = put_pixel_row_sb;
	return (TRUE);
}

void render_sb()
{
	grad_info svgrad, *grad = gradient + mem_channel;
	int i, y1, maxd;

	if (!sb_buf) return; /* Uninitialized */
	put_pixel = put_pixel_def;
	put_pixel_row = put_pixel_row_def;
	maxd = shapeburst(sb_rect[2], sb_rect[3], sb_buf);
	if (maxd) /* Have something to draw */
	{
		svgrad = *grad;
		grad->gmode = GRAD_MODE_BURST;
		if (!grad->len) grad->len = maxd - (maxd > 1);
		grad_update(grad);

		y1 = sb_rect[1] + sb_rect[3];
		for (i = sb_rect[1]; i < y1; i++)
			put_pixel_row(sb_rect[0], i, sb_rect[2], NULL);

		*grad = svgrad;
	}
	free(sb_buf);
	sb_buf = NULL;
}

/*
 * This flood fill algorithm processes image in quadtree order, and thus has
 * guaranteed upper bound on memory consumption, of order O(width + height).
 * This implementation takes O(max(width, height)) for simplicity - X and Y
 * bitmaps are interleaved.
 * (C) Dmitry Groshev
 */

#define QLEVELS 11
#define QMINSIZE 32
#define QMINLEVEL 5

/* 
 * Level bitmaps are ordered from nearest to farthest, with cells interleaved
 * in the following order: Left-Y Right-Y Top-X Bottom-X.
 */

static int wjfloodfill(int x, int y, int col, unsigned char *bmap, int lw)
{
	short nearq[QMINSIZE * QMINSIZE * 2];
	/* QMINSIZE bits per cell */
	guint32 tmap, lmap[(MAX_DIM >> QMINLEVEL) * 12 + QLEVELS * 4], maps[4];
	int borders[4] = {0, mem_width, 0, mem_height};
	int corners[4], coords[4], slots[4];
	int i, j, k, tx, ty, fmode = 0, imgc = 0, lastr[3], thisr[3];
	int lmax, ntail, bidx = 0, bbit = 0;
	double lastc[3], thisc[3], dist2, mdist2 = flood_step * flood_step;
	csel_info *flood_data = NULL;
	char *tmp = NULL;

	/* Init */
	if ((x < 0) || (x >= mem_width) || (y < 0) || (y >= mem_height) ||
		(get_pixel(x, y) != col) || (pixel_protected(x, y) == 255))
		return (FALSE);
	/* Exact limits are less, but it's too complicated */
	lmax = mem_width > mem_height ? mem_width : mem_height;
	lmax = (lmax >> QMINLEVEL) * 12 + QLEVELS * 4;
	memset(lmap, 0, lmax * sizeof(*lmap));

	/* Start drawing */
	if (bmap) bmap[y * lw + (x >> 3)] |= 1 << (x & 7);
	else
	{
		put_pixel(x, y);
		if (get_pixel(x, y) == col) return (FALSE); /* Can't draw */
	}

	/* Configure fuzzy flood fill */
	if (flood_step && ((mem_channel == CHN_IMAGE) || flood_img))
	{
		if (flood_slide) fmode = flood_cube ? 2 : 3;
		else flood_data = ALIGN(tmp = calloc(1, sizeof(csel_info) + sizeof(double)));
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
	/* Configure by-image flood fill */
	else if (!flood_step && flood_img && (mem_channel != CHN_IMAGE))
	{
		imgc = get_pixel_img(x, y);
		fmode = -1;
	}

	/* Set up initial area */
	corners[0] = x & ~(QMINSIZE - 1);
	corners[2] = y & ~(QMINSIZE - 1);
	nearq[0] = x; nearq[1] = y; ntail = 2; 

	while (1)
	{
		corners[1] = corners[0] + QMINSIZE;
		corners[3] = corners[2] + QMINSIZE;

		for (i = 0; i < 4; i++)
		{
			int j, k, i2 = (i >> 1) ^ 1;
			int wx = corners[i2 + i2], wy = corners[i];

			/* Locate map slots */
			j = ((unsigned)(wy & ~(wy - 1)) - 1) >> QMINLEVEL; // Level mask
			j += j; k = (wx >> QMINLEVEL) & (j + 1);
			slots[i] = k = (j + k) * 4 + i;

			/* Prefill near queue */
			if (k >= lmax) continue; // Outside image
			k ^= 1; tmap = lmap[k]; lmap[k] = 0;
			for (wy -= i & 1; tmap; wx++ , tmap >>= 1)
			{
				if (!(tmap & 1)) continue;
				nearq[ntail++ + i2] = wx;
				nearq[ntail++ - i2] = wy;
			}
		}

		/* Clear the side bitmaps */
		maps[0] = maps[1] = maps[2] = maps[3] = 0;
//		memset(maps, 0, sizeof(maps));

		/* Process near points */
		while (ntail)
		{
			/* Unqueue last x & y */
			coords[2] = y = nearq[--ntail];
			coords[0] = x = nearq[--ntail];
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
				switch (fmode)
				{
				case 3: /* Sliding L*X*N* */
					get_lxn(thisc, get_pixel_RGB(tx, ty));
					dist2 = (thisc[0] - lastc[0]) * (thisc[0] - lastc[0]) +
						(thisc[1] - lastc[1]) * (thisc[1] - lastc[1]) +
						(thisc[2] - lastc[2]) * (thisc[2] - lastc[2]);
					if (dist2 > mdist2) continue;
					break;
				case 2: /* Sliding RGB */
					k = get_pixel_RGB(tx, ty);
					thisr[0] = INT_2_R(k);
					thisr[1] = INT_2_G(k);
					thisr[2] = INT_2_B(k);
					if ((abs(thisr[0] - lastr[0]) > flood_step) ||
						(abs(thisr[1] - lastr[1]) > flood_step) ||
						(abs(thisr[2] - lastr[2]) > flood_step))
						continue;
					break;
				case 1: /* Centered mode */
					if (!csel_scan(ty * mem_width + tx, 1, 1,
						NULL, mem_img[CHN_IMAGE], flood_data))
						continue;
					break;
				case 0: /* Normal mode */
					if (get_pixel(tx, ty) != col) continue;
					break;
				default: /* (-1) - By-image mode */
					if (get_pixel_img(tx, ty) != imgc) continue;
					break;
				}
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
//				if (coords[i] != corners[i])
				if (coords[i] & (QMINSIZE - 1))
				{
					nearq[ntail++] = tx;
					nearq[ntail++] = ty;
					continue;
				}
				/* Far map */
				j = coords[(i & 2) ^ 3] & (QMINSIZE - 1);
				maps[i] |= 1 << j;
			}
		}

		/* Store maps */
		for (i = 0; i < 4; i++)
		{
			/* !!! Condition prevents out-of-bounds access */
			if (maps[i]) lmap[slots[i]] |= maps[i];
		}

		/* Find what else remains */
		for (i = 0; (i < lmax) && !lmap[i]; i++);
		if (i >= lmax) break; // All done

		/* Determine where that happens to be */
		j = ((i >> 2) + 2) << QMINLEVEL;
		k = nextpow2(j) >> 1; // MSB
		x = (k >> 1) + ((i & 1) << QMINLEVEL) - QMINSIZE;
		y = j - k;
		i &= 2;
		corners[i] = (corners[i] & ~(k - 1)) + x;
		i ^= 2;
		corners[i] = (corners[i] & ~(k - 1)) + y;
	}
	free(tmp);
	return (TRUE);
}

/* Determine bitmap boundaries */
static int bitmap_bounds(int *rect, unsigned char *pat, int lw)
{
	unsigned char buf[MAX_WIDTH / 8], *tmp;
	int i, j, k, x0, x1, y0, y1, w, h;

	/* Find top row */
	k = lw * rect[3];
	tmp = pat; i = k;
	while (!*tmp++ && i--);
	if (i <= 0) return (0); /* Nothing there */
	y0 = y1 = (tmp - pat - 1) / lw;

	/* Find bottom row */
	for (j = y0 + 1; j < rect[3]; j++)
	{
		tmp = pat + j * lw; i = lw;
		while (!*tmp++ && i--);
		if (i > 0) y1 = j;
	}
	y1++;

	/* Find left & right extents (8 pixels granular) */
	memcpy(buf, tmp = pat + y0 * lw, lw); tmp += lw;
	for (j = y0 + 1; j < y1; j++)
	{
		for (i = 0; i < lw; i++) buf[i] |= *tmp++;
	}
	for (x0 = 0; !buf[x0] && (x0 < lw); x0++);
	for (x1 = lw - 1; !buf[x1] && (x1 > x0); x1--);
	x0 *= 8; x1 = x1 * 8 + 8;
	if (x1 > rect[2]) x1 = rect[2];

	/* Set up boundaries */
	rect[0] += x0; rect[1] += y0;
	rect[2] = w = x1 - x0; rect[3] = h = y1 - y0;
	return (w * h);
}

/* Flood fill - may use temporary area (1 bit per pixel) */
void flood_fill(int x, int y, unsigned int target)
{
	unsigned char *pat, *buf, *temp;
	int i, j, k, l, sb, lw = (mem_width + 7) >> 3;

	/* Shapeburst mode */
	sb = STROKE_GRADIENT;

	/* Regular fill? */
	if (!sb && !mem_tool_pat && (tool_opacity == 255) && !flood_step &&
		(!flood_img || (mem_channel == CHN_IMAGE)))
	{
		wjfloodfill(x, y, target, NULL, 0);
		return;
	}

	buf = calloc(1, mem_width + lw * mem_height);
	if (!buf)
	{
		memory_errors(1);
		return;
	}
	pat = temp = buf + mem_width;
	while (wjfloodfill(x, y, target, pat, lw))
	{
		if (sb) /* Shapeburst - setup rendering backbuffer */
		{
			sb_rect[0] = sb_rect[1] = 0;
			sb_rect[2] = mem_width;
			sb_rect[3] = mem_height;
			l = bitmap_bounds(sb_rect, pat, lw);
			if (!l) break; /* Nothing to draw */
			if (!init_sb()) break; /* Not enough memory */
		}

		for (i = 0; i < mem_height; i++)
		{
			for (j = l = 0; j < mem_width; )
			{
				k = *temp++;
				if (!k)
				{
					j += 8;
					continue;
				}
				for (; k; k >>= 1)
				{
					if (k & 1) l = buf[j] = 255;
					j++;
				}
				j = (j + 7) & ~(7);
			}
			if (!l) continue; // Avoid wasting time on empty rows
			put_pixel_row(0, i, mem_width, buf);
			memset(buf, 0, mem_width);
		}

		if (sb) render_sb(); /* Finalize */

		break;
	}
	free(buf);
}


void f_rectangle(int x, int y, int w, int h)	// Draw a filled rectangle
{
	w += x; h += y;
	if (x < 0) x = 0;
	if (y < 0) y = 0;
	if (w > mem_width) w = mem_width;
	if (h > mem_height) h = mem_height;
	w -= x;

	for (; y < h; y++) put_pixel_row(x, y, w, NULL);
}

/*
 * This code uses midpoint ellipse algorithm modified for uncentered ellipses,
 * with floating-point arithmetics to prevent overflows. (C) Dmitry Groshev
 */
static void trace_ellipse(int w, int h, int *left, int *right)
{
	int dx, dy;
	double err, stx, sty, w2, h2;

	if (left[0] > w) left[0] = w;
	if (right[0] < w) right[0] = w;

	if (h <= 1) return; /* Too small */

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

	/* For too-flat ellipses */
	if (left[1] > dx) left[1] = dx;
	if (right[1] < w - 2) right[1] = w - 2;
}

static void wjellipse(int xs, int ys, int w, int h, int type, int thick)
{
	int i, j, k, dx0, dx1, dy, *left, *right;

	/* Prepare */
	ys += ys + --h;
	xs += xs + --w;
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
		int i, j, k;

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
	for (dy = h & 1; dy <= h; dy += 2)
	{
		int y0 = ys - dy, y1 = ys + dy;

		if (y1 < 0) continue;
		if (y0 < 0) y0 = y1;
		y0 >>= 1; y1 >>= 1;
		if (y0 >= mem_height) continue;

		dx0 = right[dy >> 1];
		dx1 = left[dy >> 1];
		if (dx1 <= 1) dx1 = -dx0; // Merge two spans
		while (TRUE)
		{
			int x0 = xs - dx0, x1 = xs - dx1;

			if ((x1 >= 0) && (x0 < mem_width * 2))
			{
				x0 >>= 1; x1 >>= 1;
				if (x0 < 0) x0 = 0;
				if (++x1 > mem_width) x1 = mem_width;
				x1 -= x0;
				put_pixel_row(x0, y0, x1, NULL);
				if (y1 != y0) put_pixel_row(x0, y1, x1, NULL);
			}
			if (dx1 <= 0) break;
			x1 = -dx0; dx0 = -dx1; dx1 = x1;
		}
	}

	free(left);
}

/* Thickness 0 means filled */
void mem_ellipse(int x1, int y1, int x2, int y2, int thick)
{
	int xs, ys, xl, yl, sb = FALSE;

	xs = x1 < x2 ? x1 : x2;
	ys = y1 < y2 ? y1 : y2;
	xl = abs(x2 - x1) + 1;
	yl = abs(y2 - y1) + 1;

	/* Shapeburst mode */
	if (STROKE_GRADIENT)
	{
		sb_rect[0] = xs; sb_rect[1] = ys;
		sb_rect[2] = xl; sb_rect[3] = yl;
		sb = init_sb();
	}

	/* Draw rectangle instead if too small */
	if ((xl <= 2) || (yl <= 2)) f_rectangle(xs, ys, xl, yl);
	else wjellipse(xs, ys, xl, yl, thick && (thick * 2 < xl) &&
		(thick * 2 < yl), thick);

	if (sb) render_sb();
}

static int circ_r, circ_trace[128];

static void retrace_circle(int r)
{
	int sz, left[128];

	circ_r = r--;
	sz = ((r >> 1) + 1) * sizeof(int);
	memset(left, 0, sz);
	memset(circ_trace, 0, sz);
	trace_ellipse(r, r, left, circ_trace);
}

void f_circle( int x, int y, int r )				// Draw a filled circle
{
	int i, x0, x1, y0, y1, r1 = r - 1, half = r1 & 1;

	/* Prepare & cache circle contour */
	if (circ_r != r) retrace_circle(r);

	/* Draw result */
	for (i = half; i <= r1; i += 2)
	{
		y0 = y - ((i + half) >> 1);
		y1 = y + ((i - half) >> 1);
		if ((y0 >= mem_height) || (y1 < 0)) continue;

		x0 = x - ((circ_trace[i >> 1] + half) >> 1);
		x1 = x + ((circ_trace[i >> 1] - half) >> 1) + 1;
		if (x0 < 0) x0 = 0;
		if (x1 > mem_width) x1 = mem_width;
		x1 -= x0;
		if (y0 >= 0) put_pixel_row(x0, y0, x1, NULL);
		if ((y1 != y0) && (y1 < mem_height)) put_pixel_row(x0, y1, x1, NULL);
	}
}

static int find_tangent(int dx, int dy)
{
	int i, j = 0, yy = (circ_r + 1) & 1, d, dist = 0;

	dx = abs(dx); dy = abs(dy);
	for (i = 0; i < (circ_r + 1) >> 1; i++)
	{
		d = (i + i + yy) * dy + circ_trace[i] * dx;
		if (d < dist) continue;
		dist = d;
		j = i;
	}
	return (j);
}

/* Draw line as if traced by circle brush */
void circle_line(int x0, int y0, int dx, int dy, int thick)
{
	int n, ix, iy, xx[2], yy[2], dt = (thick + 1) & 1;

	if (circ_r != thick) retrace_circle(thick);
	n = find_tangent(dx, dy);
	ix = dx >= 0 ? 0 : 1;
	iy = dy >= 0 ? 0 : 1;
	xx[ix] = x0 - n - dt;
	xx[ix ^ 1] = x0 + n;
	yy[iy] = y0 + ((circ_trace[n] - dt) >> 1);
	yy[iy ^ 1] = y0 - ((circ_trace[n] + dt) >> 1);

	g_para(xx[0], yy[0], xx[1], yy[1], dx, dy);
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
	if (cancel) progress_init(_("Bacteria Effect"), 1);

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

int mem_sel_rot( int dir )			// Rotate clipboard 90 degrees
{
	unsigned char *buf = NULL;
	int i, j = mem_clip_w * mem_clip_h, bpp = mem_clip_bpp;

	for (i = 0; i < NUM_CHANNELS; i++ , bpp = 1)
	{
		if (!mem_clip.img[i]) continue;
		buf = malloc(j * bpp);
		if (!buf) break;	// Not enough memory
		mem_rotate(buf, mem_clip.img[i], mem_clip_w, mem_clip_h, dir, bpp);
		free(mem_clip.img[i]);
		mem_clip.img[i] = buf;
	}

	/* Don't leave a mix of rotated and unrotated channels */
	if (!buf && i) mem_free_image(&mem_clip, FREE_ALL);
	if (!buf) return (1);

	i = mem_clip_w;
	mem_clip_w = mem_clip_h;		// Flip geometry
	mem_clip_h = i;

	return (0);
}

/* Clear the channels */
static void mem_clear_img(chanlist img, int w, int h, int bpp)
{
	int i, j, k, l = w * h;

	if (!img[CHN_IMAGE]); // !!! Here, image channel CAN be absent
	else if (bpp == 3)
	{
		unsigned char *tmp = img[CHN_IMAGE];
		tmp[0] = mem_col_A24.red;
		tmp[1] = mem_col_A24.green;
		tmp[2] = mem_col_A24.blue;
		j = l * 3;
		for (i = 3; i < j; i++) tmp[i] = tmp[i - 3];
	}
	else memset(img[CHN_IMAGE], mem_col_A, l);

	for (k = CHN_IMAGE + 1; k < NUM_CHANNELS; k++)
		if (img[k]) memset(img[k], 0, l);
}

void mem_rotate_free_real(chanlist old_img, chanlist new_img, int ow, int oh,
	int nw, int nh, int bpp, double angle, int mode, int gcor, int dis_a,
	int silent)
{
	unsigned char *src, *dest, *alpha, A_rgb[3];
	unsigned char *pix1, *pix2, *pix3, *pix4;
	int nx, ny, ox, oy, cc;
	double rangle = (M_PI / 180.0) * angle;	// Radians
	double s1, s2, c1, c2;			// Trig values
	double cx0, cy0, cx1, cy1;
	double x00, y00, x0y, y0y;		// Quick look up values
	double fox, foy, k1, k2, k3, k4;	// Pixel weights
	double aa1, aa2, aa3, aa4, aa;
	double rr, gg, bb;
	double tw, th, ta, ca, sa, sca, csa, Y00, Y0h, Yw0, Ywh, X00, Xwh;

	c2 = cos(rangle);
	s2 = sin(rangle);
	c1 = -s2;
	s1 = c2;

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

	/* Prepare clipping rectangle */
	tw = 0.5 * (ow + (mode ? 1 : 0));
	th = 0.5 * (oh + (mode ? 1 : 0));
	ta = M_PI * (angle / 180.0 - floor(angle / 180.0));
	ca = cos(ta); sa = sin(ta);
	sca = ca ? sa / ca : 0.0;
	csa = sa ? ca / sa : 0.0;
	Y00 = cy1 - th * ca - tw * sa;
	Y0h = cy1 + th * ca - tw * sa;
	Yw0 = cy1 - th * ca + tw * sa;
	Ywh = cy1 + th * ca + tw * sa;
	X00 = cx1 - tw * ca + th * sa;
	Xwh = cx1 + tw * ca - th * sa;

	mem_clear_img(new_img, nw, nh, bpp); /* Clear the channels */

	for (ny = 0; ny < nh; ny++)
	{
		int xl, xm;
		if (!silent && ((ny * 10) % nh >= nh - 10))
			progress_update((float)ny / nh);

		/* Clip this row */
		if (ny < Y0h) xl = ceil(X00 + (Y00 - ny) * sca);
		else if (ny < Ywh) xl = ceil(Xwh + (ny - Ywh) * csa);
		else /* if (ny < Yw0) */ xl = ceil(Xwh + (Ywh - ny) * sca);
		if (ny < Y00) xm = ceil(X00 + (Y00 - ny) * sca);
		else if (ny < Yw0) xm = ceil(X00 + (ny - Y00) * csa);
		else /* if (ny < Ywh) */ xm = ceil(Xwh + (Ywh - ny) * sca);
		if (xl < 0) xl = 0;
		if (--xm >= nw) xm = nw - 1;

		x0y = ny * s2 + x00;
		y0y = ny * c2 + y00;
		for (cc = 0; cc < NUM_CHANNELS; cc++)
		{
			if (!new_img[cc]) continue;
			/* RGB nearest neighbour */
			if (!mode && (cc == CHN_IMAGE) && (bpp == 3))
			{
				dest = new_img[CHN_IMAGE] + (ny * nw + xl) * 3;
				for (nx = xl; nx <= xm; nx++ , dest += 3)
				{
					WJ_ROUND(ox, nx * s1 + x0y);
					WJ_ROUND(oy, nx * c1 + y0y);
					src = old_img[CHN_IMAGE] +
						(oy * ow + ox) * 3;
					dest[0] = src[0];
					dest[1] = src[1];
					dest[2] = src[2];
				}
				continue;
			}
			/* One-bpp nearest neighbour */
			if (!mode)
			{
				dest = new_img[cc] + ny * nw + xl;
				for (nx = xl; nx <= xm; nx++)
				{
					WJ_ROUND(ox, nx * s1 + x0y);
					WJ_ROUND(oy, nx * c1 + y0y);
					*dest++ = old_img[cc][oy * ow + ox];
				}
				continue;
			}
			/* RGB/RGBA bilinear */
			if (cc == CHN_IMAGE)
			{
				alpha = NULL;
				if (new_img[CHN_ALPHA] && !dis_a)
					alpha = new_img[CHN_ALPHA] + ny * nw + xl;
				dest = new_img[CHN_IMAGE] + (ny * nw + xl) * 3;
				for (nx = xl; nx <= xm; nx++ , dest += 3)
				{
					fox = nx * s1 + x0y;
					foy = nx * c1 + y0y;
					/* floor() is *SLOW* on Win32 - avoiding... */
					ox = (int)(fox + 2.0) - 2;
					oy = (int)(foy + 2.0) - 2;
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
						dest[0] = UNGAMMA256(rr);
						dest[1] = UNGAMMA256(gg);
						dest[2] = UNGAMMA256(bb);
					}
					else /* Leave as is */
					{
						rr = pix1[0] * k1 + pix2[0] * k2 +
							pix3[0] * k3 + pix4[0] * k4;
						gg = pix1[1] * k1 + pix2[1] * k2 +
							pix3[1] * k3 + pix4[1] * k4;
						bb = pix1[2] * k1 + pix2[2] * k2 +
							pix3[2] * k3 + pix4[2] * k4;
						dest[0] = rint(rr);
						dest[1] = rint(gg);
						dest[2] = rint(bb);
					}
				}
				continue;
			}
			/* Alpha channel already done... maybe */
			if ((cc == CHN_ALPHA) && !dis_a)
				continue;
			/* Utility channel bilinear */
			dest = new_img[cc] + ny * nw + xl;
			for (nx = xl; nx <= xm; nx++)
			{
				fox = nx * s1 + x0y;
				foy = nx * c1 + y0y;
				/* floor() is *SLOW* on Win32 - avoiding... */
				ox = (int)(fox + 2.0) - 2;
				oy = (int)(foy + 2.0) - 2;
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
}

#define PIX_ADD (127.0 / 128.0) /* Include all _visibly_ altered pixels */

void mem_rotate_geometry(int ow, int oh, double angle, int *nw, int *nh)
				// Get new image geometry of rotation. angle = degrees
{
	int dx, dy;
	double rangle = (M_PI / 180.0) * angle,	// Radians
		s2, c2;				// Trig values


	c2 = fabs(cos(rangle));
	s2 = fabs(sin(rangle));

	/* Preserve original centering */
	dx = ow & 1; dy = oh & 1;
	/* Exchange Y with X when rotated Y is nearer to old X */
	if ((dx ^ dy) && (c2 < s2)) dx ^= 1 , dy ^= 1;
	*nw = 2 * (int)(0.5 * (ow * c2 + oh * s2 - dx) + PIX_ADD) + dx;
	*nh = 2 * (int)(0.5 * (oh * c2 + ow * s2 - dy) + PIX_ADD) + dy;
}

// Rotate canvas or clipboard by any angle (degrees)
int mem_rotate_free(double angle, int type, int gcor, int clipboard)
{
	chanlist old_img, new_img;
	unsigned char **oldmask;
	int ow, oh, nw, nh, res, rot_bpp;


	if (clipboard)
	{
		if (!mem_clipboard) return (-1);	// Nothing to rotate
		if (!HAVE_OLD_CLIP) clipboard = 2;
		if (clipboard == 1)
		{
			ow = mem_clip_real_w;
			oh = mem_clip_real_h;
		}
		else
		{
			mem_clip_real_clear();
			ow = mem_clip_w;
			oh = mem_clip_h;
		}
		rot_bpp = mem_clip_bpp;
	}
	else
	{
		ow = mem_width;
		oh = mem_height;
		rot_bpp = mem_img_bpp;
	}

	mem_rotate_geometry(ow, oh, angle, &nw, &nh);

	if ( nw>MAX_WIDTH || nh>MAX_HEIGHT ) return -5;		// If new image is too big return -5

	if (!clipboard)
	{
		memcpy(old_img, mem_img, sizeof(chanlist));
		res = undo_next_core(UC_NOCOPY, nw, nh, mem_img_bpp, CMASK_ALL);
		if (res) return (res);		// No undo space
		memcpy(new_img, mem_img, sizeof(chanlist));
		progress_init(_("Free Rotation"), 0);
	}
	else
	{
		/* Note:  even if the original clipboard doesn't have a mask,
		 * the rotation will need one to chop off the corners of
		 * a rotated rectangle. */
		oldmask = (HAVE_OLD_CLIP ? mem_clip_real_img : mem_clip.img) + CHN_SEL;
		if (!*oldmask)
		{
			if (!(*oldmask = malloc(ow * oh)))
				return (1);	// Not enough memory
			memset(*oldmask, 255, ow * oh);
		}

		res = mem_clip_new(nw, nh, mem_clip_bpp, 0, TRUE);
		if (res) return (1);	// Not enough memory
		memcpy(old_img, mem_clip_real_img, sizeof(chanlist));
		memcpy(new_img, mem_clip.img, sizeof(chanlist));
	}

	if ( rot_bpp == 1 ) type = FALSE;
	mem_rotate_free_real(old_img, new_img, ow, oh, nw, nh, rot_bpp, angle, type,
		gcor, channel_dis[CHN_ALPHA] && !clipboard, clipboard);
	if (!clipboard) progress_end();

	/* Destructive rotation - lose old unwanted clipboard */
	if (clipboard > 1) mem_clip_real_clear();

	return 0;
}

int mem_image_rot( int dir )					// Rotate image 90 degrees
{
	chanlist old_img;
	int i, ow = mem_width, oh = mem_height;

	memcpy(old_img, mem_img, sizeof(chanlist));
	i = undo_next_core(UC_NOCOPY, oh, ow, mem_img_bpp, CMASK_ALL);
	if (i) return (i);			// Not enough memory

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!mem_img[i]) continue;
		mem_rotate(mem_img[i], old_img[i], ow, oh, dir, BPP(i));
	}
	mem_undo_prepare();
	return 0;
}



///	Code for scaling contributed by Dmitry Groshev, January 2006
///	Multicore support added by Dmitry Groshev, November 2010

typedef struct {
	float *k;
	int idx;
} fstep;

static double Cubic(double x, double A)
{
	if (x < -1.5) return (0.0);
	else if (x < -0.5) return (A * (-1.0 / 8.0) * (((x * 8.0 + 28.0) * x +
		30.0) * x + 9.0));
	else if (x < 0.5) return (0.5 * (((-4.0 * A - 4.0) * x * x + A + 3.0) *
		x + 1.0));
	else if (x < 1.5) return (A * (-1.0 / 8.0) * (((x * 8.0 - 28.0) * x +
		30.0) * x - 9.0) + 1.0);
	else return (1.0);
}

static double BH1(double x)
{
	if (x < 1e-7) return (1.0);
	return ((sin(M_PI * x) / (M_PI * x)) * (0.42323 +
		0.49755 * cos(x * (M_PI * 2.0 / 6.0)) +
		0.07922 * cos(x * (M_PI * 4.0 / 6.0))));
}

static double BH(double x)
{
	double y = 0.0, xx = fabs(x);

	if (xx < 2.5)
	{
		y = BH1(xx + 0.5);
		if (xx < 1.5) y += BH1(xx + 1.5);
		if (xx < 0.5) y += BH1(xx + 2.5);
	}
	return (x > 0.0 ? 1.0 - y : y);
}

static const double Aarray[4] = {-0.5, -2.0 / 3.0, -0.75, -1.0};

/* !!! Filter as a whole must be perfectly symmetric, and "idx" of step 0
 * must be <= 0; these natural properties are relied on when allocating and
 * extending horizontal temp arrays for BOUND_TILE mode.
 * 2 extra steps at end hold end pointer & index, and terminating NULL. */
static fstep *make_filter(int l0, int l1, int type, int sharp, int bound)
{
	fstep *res, *buf;
	__typeof__(*res->k) *kp;
	double x, y, basept, fwidth, delta, scale = (double)l1 / (double)l0;
	double A = 0.0, kk = 1.0, sum;
	int i, j, k, ix, j0, k0 = 0;


	/* To correct scale-shift */
	delta = 0.5 / scale - 0.5;

	/* Untransformed bilinear is useless for reduction */
	if (type == 1) sharp = TRUE;

	/* 1:1 transform is special */
	if (scale == 1.0) type = 0;

	if (scale < 1.0) kk = scale;
	else sharp = FALSE;

	switch (type)
	{
	case 1:	fwidth = 2.0; /* Bilinear / Area-mapping */
		break;
	case 2:	case 3: case 4: case 5:	/* Bicubic, all flavors */
		fwidth = 4.0;
		A = Aarray[type - 2];
		break;
	case 6:	fwidth = 6.0; /* Blackman-Harris windowed sinc */
		break;
	default: /* 1:1 */
		fwidth = 0.0;
		break;
	}
	if (sharp) fwidth += scale - 1.0;
	fwidth /= kk;

	buf = multialloc(MA_ALIGN_DOUBLE, &res, (l1 + 2) * sizeof(*res),
		&kp, l1 * ((int)floor(fwidth) + 1) * sizeof(*res->k), NULL);
	if (!buf) return (NULL);
	res = buf; /* No need to double-align the index array */

	fwidth *= 0.5;
	type = type * 2 + !!sharp;
	for (i = 0; i < l1; i++ , buf++)
	{
		basept = (double)i / scale + delta;
		j = j0 = (int)ceil(basept - fwidth);
		k = k0 = (int)floor(basept + fwidth) + 1;
		if (j0 < 0) j0 = 0;
		if (k0 > l0) k0 = l0;
		/* If filter doesn't cover source from end to end, tiling will
		 * require physical copying */
		if ((bound == BOUND_TILE) && (k0 - j0 < l0))
			k0 = k , j0 = j;
		buf->idx = j0;
		buf->k = kp;
		kp += k0 - j0;
		sum = 0.0; 
		for (; j < k; j++)
		{
			ix = j;
			if ((j < j0) || (j >= k0))
			{
				if (bound == BOUND_VOID) continue;
				if (bound == BOUND_TILE)
				{
					if (ix < 0) ix = k0 - (-ix % k0);
					ix %= k0;
				}
				else if (k0 == 1) ix = 0;
				else
				{
					ix = abs(ix) % (k0 + k0 - 2);
					if (ix >= k0) ix = k0 + k0 - 2 - ix;
				}
			}
			ix -= j0;
			x = fabs(((double)j - basept) * kk);
			switch (type)
			{
			case 0: /* 1:1 */
			case 2: /* Bilinear */
				y = 1.0 - x;
				break;
			case 3: /* Area mapping */
				if (x <= 0.5 - scale / 2.0) y = 1.0;
				else y = 0.5 - (x - 0.5) / scale;
				break;
			case 4: case 6: case 8: case 10: /* Bicubic */
				if (x < 1.0) y = ((A + 2.0) * x - (A + 3)) * x * x + 1.0;
				else y = A * (((x - 5.0) * x + 8.0) * x - 4.0);
				break;
			case 5: case 7: case 9: case 11: /* Sharpened bicubic */
				y = Cubic(x + scale * 0.5, A) - Cubic(x - scale * 0.5, A);
				break;
			case 12: /* Blackman-Harris */
				y = BH1(x);
				break;
			case 13: /* Sharpened Blackman-Harris */
				y = BH(x + scale * 0.5) - BH(x - scale * 0.5);
				break;
			default: /* Bug */
				y = 0;
				break;
			}
			buf->k[ix] += y;
			sum += y;
		}
		/* Normalize */
		if ((sum != 0.0) && (sum != 1.0))
		{
			__typeof__(*kp) *tp = buf->k;
			sum = 1.0 / sum;
			while (tp != kp) *tp++ *= sum;
		}
	}
	/* Finalize */
	buf->idx = k0; // The rightmost extent
	buf->k = kp;

	return (res);
}

typedef struct {
	int tmask, gcor, progress;
	int ow, oh, nw, nh, bpp;
	unsigned char **src, **dest;
	double *rgb;
	fstep *hfilter, *vfilter;
	threaddata *tdata; // For simplicity
} scale_context;

static void clear_scale(scale_context *ctx)
{
	free(ctx->hfilter);
	free(ctx->vfilter);
	free(ctx->tdata);
}

static int prepare_scale(scale_context *ctx, int type, int sharp, int bound)
{
	ctx->hfilter = ctx->vfilter = NULL;
	ctx->tdata = NULL;

	/* We don't use threading for NN */
	if (!type || (ctx->bpp == 1)) return (TRUE);

	if ((ctx->hfilter = make_filter(ctx->ow, ctx->nw, type, sharp, bound)) &&
		(ctx->vfilter = make_filter(ctx->oh, ctx->nh, type, sharp, bound)))
	{
		int l = (ctx->ow - ctx->hfilter[0].idx * 2) * sizeof(double);
		if ((ctx->tdata = talloc(MA_ALIGN_DOUBLE,
			image_threads(ctx->nw, ctx->nh), ctx, sizeof(*ctx),
			NULL,
			// !!! No space for RGBAS for now
			&ctx->rgb, l * (ctx->tmask ? 7 : 3),
			NULL))) return (TRUE);
	}

	clear_scale(ctx);
	return (FALSE);
}

static void tile_extend(double *temp, int w, int l)
{
	memcpy(temp - l, temp + w - l, l * sizeof(*temp));
	memcpy(temp + w, temp, l * sizeof(*temp));
}

typedef void REGPARM2 (*istore_func)(unsigned char *img, const double *sum);

static void REGPARM2 istore_gc(unsigned char *img, const double *sum)
{
	/* Reverse gamma correction */
	img[0] = UNGAMMA256X(sum[0]);
	img[1] = UNGAMMA256X(sum[1]);
	img[2] = UNGAMMA256X(sum[2]);
}

static void REGPARM2 istore_3(unsigned char *img, const double *sum)
{
	int j = (int)rint(sum[0]);
	img[0] = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
	j = (int)rint(sum[1]);
	img[1] = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
	j = (int)rint(sum[2]);
	img[2] = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
}

static void REGPARM2 istore_1(unsigned char *img, const double *sum)
{
	int j = (int)rint(sum[0]);
	img[0] = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
}

/* !!! Once again, beware of GCC misoptimization! The two functions below
 * should not be both inlineable at once, otherwise poor code wasting both
 * time and space will be produced - WJ */

static void scale_row(fstep *tmpy, fstep *hfilter, double *work_area,
	int bpp, int gc, int ow, int oh, int nw, int i,
	unsigned char *src, unsigned char *dest)
{
	/* !!! Protect from possible stack misalignment */
	unsigned char sum_[4 * sizeof(double)];
	double *sum = ALIGNED(sum_, sizeof(double));
	istore_func istore;
	unsigned char *img;
	fstep *tmpx;
	__typeof__(*tmpy->k) *kp = tmpy->k - tmpy->idx;
	int j, y, h = tmpy[1].k - kp, ll = hfilter[0].idx;


	work_area -= ll * bpp;
	ow *= bpp;
	memset(work_area, 0, ow * sizeof(double));
	/* Build one vertically-scaled row */
	for (y = tmpy->idx; y < h; y++)
	{
		const double tk = kp[y];
		double *wrk = work_area;
		/* Only simple tiling isn't built into filter */
		img = src + ((y + oh) % oh) * ow;
		if (gc) /* Gamma-correct */
		{
			for (j = 0; j < ow; j++)
				*wrk++ += gamma256[*img++] * tk;
		}
		else /* Leave as is */
		{
			for (j = 0; j < ow; j++)
				*wrk++ += *img++ * tk;
		}
	}
	tile_extend(work_area, ow, -ll * bpp);
	/* Scale it horizontally */
	istore = gc ? istore_gc : bpp == 1 ? istore_1 : istore_3;
	img = dest + i * nw * bpp;
	for (tmpx = hfilter; tmpx[1].k; tmpx++ , img += bpp)
	{
		__typeof__(*tmpx->k) *tp, *kp = tmpx[1].k;
		double *wrk = work_area + tmpx->idx * bpp;
		double sum0, sum1, sum2;

		sum0 = sum1 = sum2 = 0.0;
		tp = tmpx->k;
		while (tp != kp)
		{
			const double kk = *tp++;
			sum0 += *wrk++ * kk;
			if (bpp == 1) continue;
			sum1 += *wrk++ * kk;
			sum2 += *wrk++ * kk;
		}
		sum[0] = sum0; sum[1] = sum1; sum[2] = sum2;
		istore(img, sum);
	}
}

static void scale_rgba(fstep *tmpy, fstep *hfilter, double *work_area,
	int bpp, int gc, int ow, int oh, int nw, int i,
	unsigned char *src, unsigned char *dest,
	unsigned char *srca, unsigned char *dsta)
{
	/* !!! Protect from possible stack misalignment */
	unsigned char sum_[4 * sizeof(double)];
	double *sum = ALIGNED(sum_, sizeof(double));
	istore_func istore;
	unsigned char *img, *imga;
	fstep *tmpx;
	__typeof__(*tmpy->k) *kp = tmpy->k - tmpy->idx;
	int j, y, h = tmpy[1].k - kp, ll = hfilter[0].idx;
	double *wrka = work_area + ow * 6 - ll * 13;


	work_area -= ll * 6;
	memset(work_area, 0, (ow - ll) * 7 * sizeof(double));
	for (y = tmpy->idx; y < h; y++)
	{
		double *wrk = work_area;
		unsigned char *img, *imga;
		int ix = (y + oh) % oh;

		img = src + ix * ow * 3;
		imga = srca + ix * ow;
		if (gc) /* Gamma-correct */
		{
			const double tk = kp[y];
			for (j = 0; j < ow; j++)
			{
				const double kk = imga[j] * tk;
				double tv;

				wrka[j] += kk;
				wrk[0] += (tv = gamma256[img[0]]) * tk;
				wrk[3] += tv * kk;
				wrk[1] += (tv = gamma256[img[1]]) * tk;
				wrk[4] += tv * kk;
				wrk[2] += (tv = gamma256[img[2]]) * tk;
				wrk[5] += tv * kk;
				wrk += 6; img += 3;
			}
		}
		else /* Leave as is */
		{
			const double tk = kp[y];
			for (j = 0; j < ow; j++)
			{
				const double kk = imga[j] * tk;
				double tv;

				wrka[j] += kk;
				wrk[0] += (tv = img[0]) * tk;
				wrk[3] += tv * kk;
				wrk[1] += (tv = img[1]) * tk;
				wrk[4] += tv * kk;
				wrk[2] += (tv = img[2]) * tk;
				wrk[5] += tv * kk;
				wrk += 6; img += 3;
			}
		}
	}
	tile_extend(work_area, ow * 6, -ll * 6);
	tile_extend(wrka, ow, -ll);
	/* Scale it horizontally */
	istore = gc ? istore_gc : bpp == 1 ? istore_1 : istore_3;
	img = dest + i * nw * 3;
	imga = dsta + i * nw;
	for (tmpx = hfilter; tmpx[1].k; tmpx++)
	{
		__typeof__(*tmpx->k) *tp, *kp = tmpx[1].k;
		double *wrk;
		double sum0, sum1, sum2, mult;

		sum0 = 0.0;
		wrk = wrka + tmpx->idx;
		tp = tmpx->k;
		while (tp != kp) sum0 += *wrk++ * *tp++;
		j = (int)rint(sum0);
		*imga = j < 0 ? 0 : j > 0xFF ? 0xFF : j;
		wrk = work_area + tmpx->idx * 6;
		mult = 1.0;
		if (*imga++)
		{
			wrk += 3;
			mult /= sum0;
		}
		sum0 = sum1 = sum2 = 0.0;
		tp = tmpx->k;
		while (tp != kp)
		{
			const double kk = *tp++;
			sum0 += wrk[0] * kk;
			sum1 += wrk[1] * kk;
			sum2 += wrk[2] * kk;
			wrk += 6;
		}
		sum[0] = sum0 * mult; sum[1] = sum1 * mult; sum[2] = sum2 * mult;
		istore(img, sum);
		img += 3;
	}
}

static void do_scale(tcb *thread)
{
	scale_context ctx = *(scale_context *)thread->data;
	fstep *tmpy;
	int i, ii, cc, cnt = thread->nsteps;


	/* For each destination line */
	for (i = thread->step0 , ii = 0; ii < cnt; i++ , ii++)
	{
		tmpy = ctx.vfilter + i;
		if (ctx.dest[CHN_IMAGE]) // Chanlist may contain, e.g., only mask
		{
			(ctx.tmask == CMASK_NONE ? (__typeof__(&scale_rgba))scale_row :
				scale_rgba)(tmpy, ctx.hfilter, ctx.rgb,
				3, ctx.gcor, ctx.ow, ctx.oh, ctx.nw, i,
				ctx.src[CHN_IMAGE], ctx.dest[CHN_IMAGE],
				ctx.src[CHN_ALPHA], ctx.dest[CHN_ALPHA]);
		}

		for (cc = CHN_IMAGE + 1; cc < NUM_CHANNELS; cc++)
		{
			if (ctx.dest[cc] && !(ctx.tmask & CMASK_FOR(cc)))
				scale_row(tmpy, ctx.hfilter, ctx.rgb,
					1, FALSE, ctx.ow, ctx.oh, ctx.nw, i,
					ctx.src[cc], ctx.dest[cc]);
		}

		if (ctx.progress && thread_step(thread, ii + 1, cnt, 10)) break;
	}
	thread_done(thread);
}

static void do_scale_nn(chanlist old_img, chanlist neo_img, int img_bpp,
	int type, int ow, int oh, int nw, int nh, int gcor, int progress)
{
	char *src, *dest;
	int i, j, oi, oj, cc, bpp;
	double scalex, scaley, deltax, deltay;


	scalex = (double)ow / (double)nw;
	scaley = (double)oh / (double)nh;
	deltax = 0.5 * scalex - 0.5;
	deltay = 0.5 * scaley - 0.5;

	for (j = 0; j < nh; j++)
	{
		for (cc = 0 , bpp = img_bpp; cc < NUM_CHANNELS; cc++ , bpp = 1)
		{
			if (!neo_img[cc]) continue;
			dest = neo_img[cc] + nw * j * bpp;
			WJ_ROUND(oj, scaley * j + deltay);
			src = old_img[cc] + ow * oj * bpp;
			for (i = 0; i < nw; i++)
			{
				WJ_ROUND(oi, scalex * i + deltax);
				oi *= bpp;
				*dest++ = src[oi];
				if (bpp == 1) continue;
				*dest++ = src[oi + 1];
				*dest++ = src[oi + 2];
			}
		}
		if (progress && ((j * 10) % nh >= nh - 10))
			progress_update((float)(j + 1) / nh);
	}
}


int mem_image_scale_real(chanlist old_img, int ow, int oh, int bpp,
	chanlist new_img, int nw, int nh, int type, int gcor, int sharp)
{
	scale_context ctx;

	ctx.tmask = CMASK_NONE;
	ctx.gcor = gcor;
	ctx.progress = FALSE;
	ctx.ow = ow;
	ctx.oh = oh;
	ctx.nw = nw;
	ctx.nh = nh;
	ctx.bpp = bpp;
	ctx.src = old_img;
	ctx.dest = new_img;

	if (!prepare_scale(&ctx, type, sharp, BOUND_MIRROR))
		return (1);	// Not enough memory

	if (type && (bpp == 3))
		launch_threads(do_scale, ctx.tdata, NULL, nh);
	else do_scale_nn(old_img, new_img, bpp, type, ow, oh, nw, nh, gcor, FALSE);

	return (0);
}

int mem_image_scale(int nw, int nh, int type, int gcor, int sharp, int bound)	// Scale image
{
	scale_context ctx;
	chanlist old_img;
	int res;

	memcpy(old_img, mem_img, sizeof(chanlist));
	nw = nw < 1 ? 1 : nw > MAX_WIDTH ? MAX_WIDTH : nw;
	nh = nh < 1 ? 1 : nh > MAX_HEIGHT ? MAX_HEIGHT : nh;

	ctx.tmask = mem_img[CHN_ALPHA] && !channel_dis[CHN_ALPHA] ? CMASK_RGBA : CMASK_NONE;
	ctx.gcor = gcor;
	ctx.progress = TRUE;
	ctx.ow = mem_width;
	ctx.oh = mem_height;
	ctx.nw = nw;
	ctx.nh = nh;
	ctx.bpp = mem_img_bpp;
	ctx.src = old_img;
	ctx.dest = mem_img;

	if (!prepare_scale(&ctx, type, sharp, bound))
		return (1);	// Not enough memory

	if (!(res = undo_next_core(UC_NOCOPY, nw, nh, mem_img_bpp, CMASK_ALL)))
	{
		progress_init(_("Scaling Image"), 0);
		if (type && (mem_img_bpp == 3))
			launch_threads(do_scale, ctx.tdata, NULL, mem_height);
		else do_scale_nn(old_img, mem_img, mem_img_bpp, type,
			ctx.ow, ctx.oh, nw, nh, gcor, TRUE);
		progress_end();
	}

	clear_scale(&ctx);
	return (res);
}



int mem_isometrics(int type)
{
	unsigned char *wrk, *src, *dest, *fill;
	int i, j, k, l, cc, step, bpp, ow = mem_width, oh = mem_height;

	if ( type<2 )
	{
		if ( (oh + (ow-1)/2) > MAX_HEIGHT ) return -5;
		i = mem_image_resize(ow, oh + (ow-1)/2, 0, 0, 0);
	}
	if ( type>1 )
	{
		if ( (ow+oh-1) > MAX_WIDTH ) return -5;
		i = mem_image_resize(ow + oh - 1, oh, 0, 0, 0);
	}

	if (i) return (i);

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

/* Modes: 0 - clear, 1 - tile, 2 - mirror tile */
int mem_image_resize(int nw, int nh, int ox, int oy, int mode)
{
	chanlist old_img;
	char *src, *dest;
	int i, h, ow = mem_width, oh = mem_height, hmode = mode;
	int res, hstep, vstep, vstep2 = 0, oxo = 0, oyo = 0, nxo = 0, nyo = 0;
	int rspan1 = 0, span1 = 0, rspan2 = 0, span2 = 0, rep = 0, tail = 0;

	nw = nw < 1 ? 1 : nw > MAX_WIDTH ? MAX_WIDTH : nw;
	nh = nh < 1 ? 1 : nh > MAX_HEIGHT ? MAX_HEIGHT : nh;

	memcpy(old_img, mem_img, sizeof(chanlist));
	res = undo_next_core(UC_NOCOPY, nw, nh, mem_img_bpp, CMASK_ALL);
	if (res) return (res);			// Not enough memory

	/* Special mode for simplest, one-piece-covering case */
	if ((ox <= 0) && (nw - ox <= ow)) hmode = -1;
	if ((oy <= 0) && (nh - oy <= oh)) mode = -1;

	/* Clear */
	if (!mode || !hmode)
	{			
		int i, l, cc;

		l = nw * nh;
		for (cc = 0; cc < NUM_CHANNELS; cc++)
		{
			if (!mem_img[cc]) continue;
			dest = mem_img[cc];
			if ((cc != CHN_IMAGE) || (mem_img_bpp == 1))
			{
				memset(dest, cc == CHN_IMAGE ? mem_col_A : 0, l);
				continue;
			}
			for (i = 0; i < l; i++)	// Background is current colour A
			{
				*dest++ = mem_col_A24.red;
				*dest++ = mem_col_A24.green;
				*dest++ = mem_col_A24.blue;
			}
		}
		/* All done if source out of bounds */
		if ((ox >= nw) || (ox + ow <= 0) || (oy >= nh) ||
			(oy + oh <= 0)) return (0);
	}

	/* Tiled vertically */
	if (mode > 0)
	{
		/* No mirror when height < 3 */
		if (oh < 3) mode = 1;
		/* Period length */
		if (mode == 2) vstep = 2 * (vstep2 = oh - 1);
		else vstep = oh;
		/* Normalize offset */
		oyo = oy <= 0 ? -oy % vstep : vstep - 1 - (oy - 1) % vstep;
		h = nh;
	}
	/* Single vertical span */
	else
	{
		/* No periodicity */
		vstep = nh + oh;
		/* Normalize offset */
		if (oy < 0) oyo = -oy;
		else nyo = oy;
		h = oh + oy;
		if (h > nh) h = nh;
	}

	/* Tiled horizontally */
	if (hmode > 0)
	{
		/* No mirror when width < 3 */
		if (ow < 3) hmode = 1;
		/* Period length */
		if (hmode == 2) hstep = ow + ow - 2;
		else hstep = ow;
		/* Normalize offset */
		oxo = ox <= 0 ? -ox % hstep : hstep - 1 - (ox - 1) % hstep;
		/* Single direct span? */
		if ((oxo <= 0) && (oxo + ow >= nw)) hmode = -1;
		if (hmode == 2) /* Mirror tiling */
		{
			if (oxo < ow - 1) span1 = ow - 1 - oxo;
			res = nw - span1;
			rspan1 = hstep - oxo - span1;
			if (rspan1 > res) rspan1 = res;
			span2 = (res = res - rspan1);
			if (span2 > ow - 1 - span1) span2 = ow - 1 - span1;
			rspan2 = res - span2;
			if (rspan2 > ow - 1 - rspan1) rspan2 = ow - 1 - rspan1;
		}
		else /* Normal tiling */
		{
			span1 = ow - oxo;
			span2 = nw - span1;
			if (span2 > oxo) span2 = oxo;
		}
		rep = nw / hstep;
		if (rep) tail = nw % hstep;
	}
	/* Single horizontal span */
	else
	{
		/* No periodicity */
		hstep = nw;
		/* Normalize offset */
		if (ox < 0) oxo = -ox;
		else nxo = ox;
		/* First direct span */
		span1 = nw - nxo;
		if (span1 > ow - oxo) span1 = ow - oxo;
	}

	/* Row loop */
	for (i = nyo; i < h; i++)
	{
		int j, k, l, bpp, cc;

		/* Main period */
		k = i - vstep;
		/* Mirror period */
		if ((k < 0) && (vstep2 > 1)) k = i - ((i + oyo) % vstep2) * 2;
		/* The row is there - copy it */
		if ((k >= 0) && (k < i))
		{
			for (cc = 0; cc < NUM_CHANNELS; cc++)
			{
				if (!mem_img[cc]) continue;
				l = nw * BPP(cc);
				src = mem_img[cc] + k * l;
				dest = mem_img[cc] + i * l;
				memcpy(dest, src, l);
			}
			continue;
		}
		/* First encounter - have to build the row anew */
		k = (i - nyo + oyo) % vstep;
		if (k >= oh) k = vstep - k;
		for (cc = 0; cc < NUM_CHANNELS; cc++)
		{
			if (!mem_img[cc]) continue;
			bpp = BPP(cc);
			dest = mem_img[cc] + (i * nw + nxo) * bpp;
			/* First direct span */
			if (span1)
			{
				src = old_img[cc] + (k * ow + oxo) * bpp;
				memcpy(dest, src, span1 * bpp);
				if (hmode < 1) continue; /* Single-span mode */
				dest += span1 * bpp;
			}
			/* First reverse span */
			if (rspan1)
			{
				src = old_img[cc] + (k * ow + hstep - oxo -
					span1) * bpp;
				for (j = 0; j < rspan1; j++ , src -= bpp)
				{
					*dest++ = src[0];
					if (bpp == 1) continue;
					*dest++ = src[1];
					*dest++ = src[2];
				}
			}
			/* Second direct span */
			if (span2)
			{
				src = old_img[cc] + k * ow * bpp;
				memcpy(dest, src, span2 * bpp);
				dest += span2 * bpp;
			}
			/* Second reverse span */
			if (rspan2)
			{
				src = old_img[cc] + (k * ow + ow - 1) * bpp;
				for (j = 0; j < rspan2; j++ , src -= bpp)
				{
					*dest++ = src[0];
					if (bpp == 1) continue;
					*dest++ = src[1];
					*dest++ = src[2];
				}
			}
			/* Repeats */
			if (rep)
			{
				src = mem_img[cc] + i * nw * bpp;
				l = hstep * bpp;
				for (j = 1; j < rep; j++)
				{
					memcpy(dest, src, l);
					dest += l;
				}
				memcpy(dest, src, tail * bpp);
			}
		}
	}
	mem_undo_prepare();

	return (0);
}

/* Threshold channel values */
void mem_threshold(unsigned char *img, int len, int level)
{
	if (!img) return; /* Paranoia */
	for (; len; len-- , img++)
		*img = *img < level ? 0 : 255;
}

/* Only supports BPP = 1 and 3 */
void mem_demultiply(unsigned char *img, unsigned char *alpha, int len, int bpp)
{
	int i, k;
	double d;

	for (i = 0; i < len; i++ , img += bpp)
	{
		if (!alpha[i]) continue;
		d = 255.0 / (double)alpha[i];
		k = rint(d * img[0]);
		img[0] = k > 255 ? 255 : k;
		if (bpp == 1) continue;
		k = rint(d * img[1]);
		img[1] = k > 255 ? 255 : k;
		k = rint(d * img[2]);
		img[2] = k > 255 ? 255 : k;
	}
}

/* Build bitdepth translation table */
void set_xlate(unsigned char *xlat, int bpp)
{
	int i, j, m, n = (1 << bpp) - 1;

	for (i = 0 , j = n , m = n + n; i <= n; i++ , j += 255 * 2)
		xlat[i] = j / m;
}

/* Check if byte array is all one value */
int is_filled(unsigned char *data, unsigned char val, int len)
{
	len++;
	while (--len && (*data++ != val));
	return (!len);
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

int get_pixel_img( int x, int y )	/* RGB or indexed */
{
	x = mem_width * y + x;
	if (mem_img_bpp == 1) return (mem_img[CHN_IMAGE][x]);
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
	int i, j;

	j = start + step * (cnt - 1) + 1;

	if (mem_unmask)
	{
		memset(mask, 0, j);
		return;
	}

	/* Clear mask or copy mask channel into it */
	if (mask0) memcpy(mask, mask0, j);
	else memset(mask, 0, j);

	/* Add colour protection to it */
	if (mem_img_bpp == 1)
	{
		for (i = start; i < j; i += step)
		{
			mask[i] |= mem_prot_mask[img0[i]];
		}
	}
	else if (mem_prot)
	{
		for (i = start; i < j; i += step)
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

static int blend_pixels(int start, int step, int cnt, const unsigned char *mask,
	unsigned char *imgr, unsigned char *img0, unsigned char *img, int bpp)
{
	static const unsigned char hhsv[8 * 3] = {
		0, 1, 2, /* #0: B..M */
		2, 1, 0, /* #1: M+..R- */
		0, 1, 2, /* #2: B..M alt */
		1, 0, 2, /* #3: C+..B- */
		2, 0, 1, /* #4: G..C */
		0, 2, 1, /* #5: Y+..G- */
		1, 2, 0, /* #6: R..Y */
		1, 2, 0  /* #7: W */ };
	const unsigned char *new, *old;
	int j, step3, uninit_(nhex), uninit_(ohex), mode = blend_mode;


	/* Backward transfer? */
	if (mode & BLEND_REVERSE) new = img0 , old = img;
	else new = img , old = img0;
	mode &= BLEND_MMASK;
	if (bpp == 1) mode += BLEND_NMODES;

	cnt = start + step * (cnt - 1);
	step3 = step * bpp;
	new += (start - step) * bpp; old += (start - step) * bpp;
	j = start - step;
	while (j < cnt)
	{
		unsigned char *dest;
		int j0, j1, j2;

		j += step; old += step3; new += step3;
		if (!mask[j]) continue;

		dest = imgr + j * bpp;

		if (mode < BLEND_1BPP)
		{
			nhex = ((((0x200 + new[0]) - new[1]) ^ ((0x400 + new[1]) - new[2]) ^
				 ((0x100 + new[2]) - new[0])) >> 8) * 3;
			ohex = ((((0x200 + old[0]) - old[1]) ^ ((0x400 + old[1]) - old[2]) ^
				 ((0x100 + old[2]) - old[0])) >> 8) * 3;
		}

		switch (mode)
		{
		case BLEND_HUE: /* HS* Hue */
		{
			int i, nsi, nvi;
			unsigned char os, ov;

			ov = old[hhsv[ohex + 2]];

			if (nhex == 7 * 3) /* New is white */
			{
				dest[0] = dest[1] = dest[2] = ov;
				break;
			}

			os = old[hhsv[ohex + 1]];
			nsi = hhsv[nhex + 1];
			nvi = hhsv[nhex + 2];

			i = new[nvi] - new[nsi];
			dest[hhsv[nhex]] = (i + (ov - os) * 2 *
				(new[hhsv[nhex]] - new[nsi])) / (i + i) + os;
			dest[nsi] = os;
			dest[nvi] = ov;
			break;
		}
		case BLEND_SAT: /* HSV Saturation */
		{
			int i, osi, ovi;
			unsigned char ov, os, ns, nv;

			if (ohex == 7 * 3) /* Old is white - leave it so */
			{
				dest[0] = old[0]; dest[1] = old[1]; dest[2] = old[2];
				break;
			}

			ovi = hhsv[ohex + 2];
			ov = old[ovi];

			if (nhex == 7 * 3) /* New is white */
			{
				dest[0] = dest[1] = dest[2] = ov;
				break;
			}

			osi = hhsv[ohex + 1];
			os = old[osi];

			nv = new[hhsv[nhex + 2]];
			ns = (new[hhsv[nhex + 1]] * ov * 2 + nv) / (nv + nv);

			i = ov - os;
			dest[hhsv[ohex]] = (i + (ov - ns) * 2 *
				(old[hhsv[ohex]] - os)) / (i + i) + ns;
			dest[osi] = ns;
			dest[ovi] = ov;
			break;
		}
		case BLEND_VALUE: /* HSV Value */
		{
			int osi, ovi;
			unsigned char ov, nv;

			nv = new[hhsv[nhex + 2]];

			if (ohex == 7 * 3) /* Old is white */
			{
				dest[0] = dest[1] = dest[2] = nv;
				break;
			}

			ov = old[hhsv[ohex + 2]];
			osi = hhsv[ohex + 1];
			ovi = hhsv[ohex + 2];

			dest[hhsv[ohex]] = (old[hhsv[ohex]] * nv * 2 + ov) / (ov + ov);
			dest[osi] = (old[osi] * nv * 2 + ov) / (ov + ov);
			dest[ovi] = nv;
			break;
		}
		case BLEND_COLOR: /* HSL Hue + Saturation */
		{
			int nsi, nvi, x0, x1, y0, y1, vsy1, vs1y;
			unsigned char os, ov;

			os = old[hhsv[ohex + 1]];
			ov = old[hhsv[ohex + 2]];
			x0 = os + ov;

			/* New is white */
			if (nhex == 7 * 3)
			{
				dest[0] = dest[1] = dest[2] = (x0 + 1) >> 1;
				break;
			}

			nsi = hhsv[nhex + 1];
			nvi = hhsv[nhex + 2];
			x1 = new[nvi] + new[nsi];

			y1 = x1 > 255 ? 510 - x1 : x1;
			vs1y = (x0 + 1) * y1;
			y0 = x0 > 255 ? 510 - x0 : x0;
			vsy1 = (new[nvi] - new[nsi]) * y0;
			y1 += y1;

			dest[hhsv[nhex]] = (vs1y + (new[hhsv[nhex]] * 2 - x1) * y0) / y1;
			dest[nsi] = (vs1y - vsy1) / y1;
			dest[nvi] = (vs1y + vsy1) / y1;
			break;
		}
		case BLEND_SATPP: /* Perceived saturation (a hack, but useful one) */
		{
			int i, xyz = old[0] + old[1] + old[2];

			/* This makes difference between MIN and MAX twice larger -
			 * somewhat like doubling HSL saturation, but without strictly
			 * preserving L */
			i = (old[0] * 6 - xyz + 2) / 3;
			dest[0] = i < 0 ? 0 : i > 255 ? 255 : i;
			i = (old[1] * 6 - xyz + 2) / 3;
			dest[1] = i < 0 ? 0 : i > 255 ? 255 : i;
			i = (old[2] * 6 - xyz + 2) / 3;
			dest[2] = i < 0 ? 0 : i > 255 ? 255 : i;
			break;
		}
		case BLEND_SCREEN: // ~mult(~old, ~new)
			j1 = (old[1] + new[1]) * 255 - old[1] * new[1];
			dest[1] = (j1 + (j1 >> 8) + 1) >> 8;
			j2 = (old[2] + new[2]) * 255 - old[2] * new[2];
			dest[2] = (j2 + (j2 >> 8) + 1) >> 8;
		case BLEND_SCREEN + BLEND_NMODES:
			j0 = (old[0] + new[0]) * 255 - old[0] * new[0];
			dest[0] = (j0 + (j0 >> 8) + 1) >> 8;
			break;
		case BLEND_MULT:
			j1 = old[1] * new[1];
			dest[1] = (j1 + (j1 >> 8) + 1) >> 8;
			j2 = old[2] * new[2];
			dest[2] = (j2 + (j2 >> 8) + 1) >> 8;
		case BLEND_MULT + BLEND_NMODES:
			j0 = old[0] * new[0];
			dest[0] = (j0 + (j0 >> 8) + 1) >> 8;
			break;
		case BLEND_BURN: // ~div(~old, new)
			j1 = ((unsigned char)~old[1] << 8) / (new[1] + 1);
			dest[1] = 255 - j1 >= 0 ? 255 - j1 : 0;
			j2 = ((unsigned char)~old[2] << 8) / (new[2] + 1);
			dest[2] = 255 - j2 >= 0 ? 255 - j2 : 0;
		case BLEND_BURN + BLEND_NMODES:
			j0 = ((unsigned char)~old[0] << 8) / (new[0] + 1);
			dest[0] = 255 - j0 >= 0 ? 255 - j0 : 0;
			break;
		case BLEND_DODGE: // div(old, ~new)
			j1 = (old[1] << 8) / (~new[1] + 1);
			dest[1] = j1 < 255 ? j1 : 255;
			j2 = (old[2] << 8) / (~new[2] + 1);
			dest[2] = j2 < 255 ? j2 : 255;
		case BLEND_DODGE + BLEND_NMODES:
			j0 = (old[0] << 8) / (~new[0] + 1);
			dest[0] = j0 < 255 ? j0 : 255;
			break;
		case BLEND_DIV:
			j1 = (old[1] << 8) / (new[1] + 1);
			dest[1] = j1 < 255 ? j1 : 255;
			j2 = (old[2] << 8) / (new[2] + 1);
			dest[2] = j2 < 255 ? j2 : 255;
		case BLEND_DIV + BLEND_NMODES:
			j0 = (old[0] << 8) / (new[0] + 1);
			dest[0] = j0 < 255 ? j0 : 255;
			break;
		case BLEND_HLIGHT:
			j1 = old[1] * new[1] * 2;
			if (new[1] >= 128)
				j1 = (old[1] + new[1]) * (255 * 2) - (255 * 255) - j1;
			dest[1] = (j1 + (j1 >> 8) + 1) >> 8;
			j2 = old[2] * new[2] * 2;
			if (new[2] >= 128)
				j2 = (old[2] + new[2]) * (255 * 2) - (255 * 255) - j2;
			dest[2] = (j2 + (j2 >> 8) + 1) >> 8;
		case BLEND_HLIGHT + BLEND_NMODES:
			j0 = old[0] * new[0] * 2;
			if (new[0] >= 128)
				j0 = (old[0] + new[0]) * (255 * 2) - (255 * 255) - j0;
			dest[0] = (j0 + (j0 >> 8) + 1) >> 8;
			break;
		case BLEND_SLIGHT:
// !!! This formula is equivalent to one used in Pegtop XFader and GIMP,
// !!! and differs from one used by Photoshop and PhotoPaint
			j1 = old[1] * ((255 * 255) - (unsigned char)~old[1] *
				(255 - (new[1] << 1)));
			// Precise division by 255^2
			j1 += j1 >> 7;
			dest[1] = (j1 + ((j1 * 3 + 0x480) >> 16)) >> 16;
			j2 = old[2] * ((255 * 255) - (unsigned char)~old[2] *
				(255 - (new[2] << 1)));
			j2 += j2 >> 7;
			dest[2] = (j2 + ((j2 * 3 + 0x480) >> 16)) >> 16;
		case BLEND_SLIGHT + BLEND_NMODES:
			j0 = old[0] * ((255 * 255) - (unsigned char)~old[0] *
				(255 - (new[0] << 1)));
			j0 += j0 >> 7;
			dest[0] = (j0 + ((j0 * 3 + 0x480) >> 16)) >> 16;
			break;
// "Negation" : ~BLEND_DIFF(~old, new)
		case BLEND_DIFF:
			dest[1] = abs(old[1] - new[1]);
			dest[2] = abs(old[2] - new[2]);
		case BLEND_DIFF + BLEND_NMODES:
			dest[0] = abs(old[0] - new[0]);
			break;
		case BLEND_DARK:
			dest[1] = old[1] < new[1] ? old[1] : new[1];
			dest[2] = old[2] < new[2] ? old[2] : new[2];
		case BLEND_DARK + BLEND_NMODES:
			dest[0] = old[0] < new[0] ? old[0] : new[0];
			break;
		case BLEND_LIGHT:
			dest[1] = old[1] > new[1] ? old[1] : new[1];
			dest[2] = old[2] > new[2] ? old[2] : new[2];
		case BLEND_LIGHT + BLEND_NMODES:
			dest[0] = old[0] > new[0] ? old[0] : new[0];
			break;
		case BLEND_GRAINX:
			j1 = old[1] - new[1] + 128;
			dest[1] = j1 < 0 ? 0 : j1 > 255 ? 255 : j1;
			j2 = old[2] - new[2] + 128;
			dest[2] = j2 < 0 ? 0 : j2 > 255 ? 255 : j2;
		case BLEND_GRAINX + BLEND_NMODES:
			j0 = old[0] - new[0] + 128;
			dest[0] = j0 < 0 ? 0 : j0 > 255 ? 255 : j0;
			break;
		case BLEND_GRAINM:
			j1 = old[1] + new[1] - 128;
			dest[1] = j1 < 0 ? 0 : j1 > 255 ? 255 : j1;
			j2 = old[2] + new[2] - 128;
			dest[2] = j2 < 0 ? 0 : j2 > 255 ? 255 : j2;
		case BLEND_GRAINM + BLEND_NMODES:
			j0 = old[0] + new[0] - 128;
			dest[0] = j0 < 0 ? 0 : j0 > 255 ? 255 : j0;
			break;
// Photoshop's "Linear light" is equivalent to XFader's "Stamp" with swapped A&B
		default: /* RGB mode applied to 1bpp */
			dest[0] = img0[j];
 			break;
		}
	}
	return (TRUE);
}

void put_pixel_def(int x, int y)	/* Combined */
{
	unsigned char *ti, *old_image, *old_alpha = NULL;
	unsigned char fmask, opacity = 255, cset[NUM_CHANNELS + 3];
	int i, j, offset, idx, bpp, tint, op = tool_opacity;


	idx = IS_INDEXED;
	j = pixel_protected(x, y);
	if (idx ? j : j == 255) return;
	bpp = MEM_BPP;
	ti = cset + (bpp == 3 ? 0 : mem_channel + 3);

	tint = tint_mode[0];
	if (tint_mode[1] ^ (tint_mode[2] < 2)) tint = -tint;

	if (mem_gradient) /* Gradient mode - ask for one pixel */
	{
		fmask = 0; // Fake mask on input
		grad_pixels(0, 1, 1, x, y, &fmask, &fmask, ti,
			cset + CHN_ALPHA + 3);
		if (!(op = fmask)) return;
	}
	else /* Default mode - init "colorset" */
	{
		i = ((x & 7) + 8 * (y & 7));
		cset[mem_channel + 3] = channel_col_[mem_pattern[i]][mem_channel];
		cset[CHN_ALPHA + 3] = channel_col_[mem_pattern[i]][CHN_ALPHA];
		cset[CHN_IMAGE + 3] = mem_col_pat[i]; /* !!! This must be last! */
		i *= 3;
		cset[0] = mem_col_pat24[i + 0];
		cset[1] = mem_col_pat24[i + 1];
		cset[2] = mem_col_pat24[i + 2];
	}

	old_image = mem_undo_opacity ? mem_undo_previous(mem_channel) :
		mem_img[mem_channel];
	if ((mem_channel == CHN_IMAGE) && RGBA_mode)
		old_alpha = mem_undo_opacity ? mem_undo_previous(CHN_ALPHA) :
			mem_img[CHN_ALPHA];

	if (!idx) // No use for opacity with indexed images
	{
		j = (255 - j) * op;
		opacity = (j + (j >> 8) + 1) >> 8;
	}

	offset = x + mem_width * y;

	/* Coupled alpha channel */
	if (old_alpha && mem_img[CHN_ALPHA])
	{
		unsigned char newc = cset[CHN_ALPHA + 3];

		if (opacity < 255)
		{
			unsigned char oldc = old_alpha[offset];
			int j = oldc * 255 + (newc - oldc) * opacity;

			if (j && !channel_dis[CHN_ALPHA])
				opacity = (255 * opacity * newc) / j;
			newc = (j + (j >> 8) + 1) >> 8;
		}
		mem_img[CHN_ALPHA][offset] = newc;
	}

	offset *= bpp;
	process_img(0, 1, 1, &opacity,
		mem_img[mem_channel] + offset, old_image + offset, ti,
		ti, bpp, !idx * bpp);
}

/* Repeat pattern in buffer */
static void pattern_rep(unsigned char *dest, unsigned char *src,
	int ofs, int rep, int len, int bpp)
{
	int l1;

	ofs *= bpp; rep *= bpp; len *= bpp;
	l1 = rep - ofs;
	if (l1 > len) l1 = len;
	memcpy(dest, src + ofs, l1);
	if (!(len -= l1)) return;
	dest += l1;
	if ((len -= rep) > 0)
	{
		memcpy(dest, src, rep);
		src = dest;
		dest += rep;
		while ((len -= rep) > 0)
		{
			memcpy(dest, src, rep);
			dest += rep;
			rep += rep;
		}
	}
	memcpy(dest, src, len + rep);
}

/* Merge mask with selection */
static void mask_select(unsigned char *mask, unsigned char *xsel, int l)
{
	int i, j, k;

	for (i = 0; i < l; i++)
	{
		k = mask[i] * (j = xsel[i]);
		mask[i] = ((k + (k >> 8) + 1) >> 8) + 255 - j;
	}
}

/* Faster function for large brushes and fills */
void put_pixel_row_def(int x, int y, int len, unsigned char *xsel)
{
	unsigned char tmp_image[ROW_BUFLEN * 3], mask[ROW_BUFLEN],
		tmp_alpha[ROW_BUFLEN], tmp_opacity[ROW_BUFLEN],
		*source_alpha = NULL, *source_opacity = NULL;
	unsigned char *old_image, *old_alpha, *srcp, src1[8];
	int offset, use_mask, bpp, idx;


	if (len <= 0) return;

	old_image = mem_undo_opacity ? mem_undo_previous(mem_channel) :
		mem_img[mem_channel];
	old_alpha = mem_undo_opacity ? mem_undo_previous(CHN_ALPHA) :
		mem_img[CHN_ALPHA];

	offset = x + mem_width * y;

	bpp = MEM_BPP; idx = IS_INDEXED;
	use_mask = (mem_channel <= CHN_ALPHA) && mem_img[CHN_MASK] && !channel_dis[CHN_MASK];
	if ((mem_channel == CHN_IMAGE) && RGBA_mode && mem_img[CHN_ALPHA])
		source_alpha = tmp_alpha;

// !!! This depends on buffer length being a multiple of pattern length
	if (!mem_gradient) /* Default mode - init buffer(s) from pattern */
	{
		int i, dy = 8 * (y & 7), l = len <= ROW_BUFLEN ? len : ROW_BUFLEN;

		srcp = mem_pattern + dy;
		if (source_alpha)
		{
			for (i = 0; i < 8; i++)
				src1[i] = channel_col_[srcp[i]][CHN_ALPHA];
			pattern_rep(tmp_alpha, src1, x & 7, 8, l, 1);
		}
		if (mem_channel != CHN_IMAGE)
		{
			for (i = 0; i < 8; i++)
				src1[i] = channel_col_[srcp[i]][mem_channel];
			srcp = src1;
		}
		else srcp = idx ? mem_col_pat + dy : mem_col_pat24 + dy * 3;
		pattern_rep(tmp_image, srcp, x & 7, 8, l, bpp);
	}

	idx ^= 1; // 0 if indexed, now
	while (TRUE)
	{
		int l = len <= ROW_BUFLEN ? len : ROW_BUFLEN;

		prep_mask(0, 1, l, mask,
			use_mask ? mem_img[CHN_MASK] + offset : NULL,
			mem_img[CHN_IMAGE] + offset * mem_img_bpp);

		if (xsel)
		{
			mask_select(mask, xsel, l);
			xsel += l;
		}

		/* Gradient mode */
		if (mem_gradient) grad_pixels(0, 1, l, x, y, mask,
			source_opacity = tmp_opacity, tmp_image, source_alpha);
		/* Buffers stay unchanged for default mode */

		process_mask(0, 1, l, mask,
			mem_img[CHN_ALPHA] + offset,
			old_alpha + offset, source_alpha, source_opacity,
			idx * tool_opacity, channel_dis[CHN_ALPHA]);
		process_img(0, 1, l, mask,
			mem_img[mem_channel] + offset * bpp,
			old_image + offset * bpp, tmp_image,
			tmp_image, bpp, idx * bpp);

		if (!(len -= l)) return;
		x += l;
		offset += l;
	}
}

void process_mask(int start, int step, int cnt, unsigned char *mask,
	unsigned char *alphar, unsigned char *alpha0, unsigned char *alpha,
	unsigned char *trans, int opacity, int noalpha)
{
	unsigned char *xalpha = NULL;


	cnt = start + step * cnt;

	/* Use alpha as selection when pasting RGBA to RGB */
	if (alpha && !alphar)
	{
		*(trans ? &xalpha : &trans) = alpha;
		alpha = NULL;
	}

	/* Opacity mode */
	if (opacity)
	{
		int i, j, k;

		for (i = start; i < cnt; i += step)
		{
			unsigned char newc, oldc;

			k = (255 - mask[i]) * opacity;
			if (!k)
			{
				mask[i] = 0;
				continue;
			}
			k = (k + (k >> 8) + 1) >> 8;

			if (trans) /* Have transparency mask */
			{
				if (xalpha) /* Have two :-) */
				{
					k *= xalpha[i];
					k = (k + (k >> 8) + 1) >> 8;
				}
				k *= trans[i];
				k = (k + (k >> 8) + 1) >> 8;
			}
			mask[i] = k;

			if (!alpha || !k) continue;
			/* Have coupled alpha channel - process it */
			newc = alpha[i];
			oldc = alpha0[i];
			j = oldc * 255 + (newc - oldc) * k;
			alphar[i] = (j + (j >> 8) + 1) >> 8;
			if (noalpha) continue;
			if (j) mask[i] = (255 * k * newc) / j;
		}
	}

	/* Indexed mode - set mask to on-off */
	else
	{
		int i, k;

		for (i = start; i < cnt; i += step)
		{
			k = mask[i];
			if (trans)
			{
				unsigned char oldc = trans[i];
				if (xalpha) oldc &= xalpha[i];
				k |= oldc ^ 255;
			}
			mask[i] = k = k ? 0 : 255;
			if (!alpha || !k) continue;
			/* Have coupled alpha channel - process it */
			alphar[i] = alpha[i];
		}
	}
}

void process_img(int start, int step, int cnt, unsigned char *mask,
	unsigned char *imgr, unsigned char *img0, unsigned char *img,
	unsigned char *xbuf, int sourcebpp, int destbpp)
{
	int tint;


	if (sourcebpp < destbpp)
	{
		/* Convert paletted source to RGB */
		do_convert_rgb(start, step, cnt, xbuf, img);
		img = xbuf;
	}
	if (mem_blend && (blend_mode & BLEND_MMASK))
	{
		/* Apply blend mode's transform part */
		if (blend_pixels(start, step, cnt, mask,
			xbuf, img0, img, destbpp < 3 ? 1 : 3)) img = xbuf;
	}

	cnt = start + step * cnt;

	tint = tint_mode[0];
	if (tint_mode[1] ^ (tint_mode[2] < 2)) tint = -tint;

	/* Indexed image or utility channel */
	if (destbpp < 3)
	{
		unsigned char newc, oldc;
		int i, j, mx = destbpp ? 255 : mem_cols - 1;

		for (i = start; i < cnt; i += step)
		{
			j = mask[i];
			if (!j) continue;
			newc = img[i];
			oldc = img0[i];

			if (!tint); // Do nothing
			else if (tint < 0) newc = oldc + newc < mx ?
				oldc + newc : mx;
			else newc = oldc > newc ? oldc - newc : 0;

			if (j < 255)
			{
				j = oldc * 255 + (newc - oldc) * j;
				newc = (j + (j >> 8) + 1) >> 8;
			}
			imgr[i] = newc;
		}
	}

	/* RGB image */
	else
	{
		int rgbmask = mem_blend ? (blend_mode >> BLEND_RGBSHIFT) & 7 : 0;
		unsigned char r, g, b, nrgb[3];
		int i, j, ofs3, opacity;

		for (i = start; i < cnt; i += step)
		{
			opacity = mask[i];
			if (!opacity) continue;
			ofs3 = i * 3;
			nrgb[0] = img[ofs3 + 0];
			nrgb[1] = img[ofs3 + 1];
			nrgb[2] = img[ofs3 + 2];

/* !!! On x86, register pressure here is too large already, so rereading img0
 * is preferable to lengthening r/g/b's living ranges - WJ */
			if (tint)
			{
				r = img0[ofs3 + 0];
				g = img0[ofs3 + 1];
				b = img0[ofs3 + 2];
				if (tint < 0)
				{
					nrgb[0] = r > 255 - nrgb[0] ? 255 : r + nrgb[0];
					nrgb[1] = g > 255 - nrgb[1] ? 255 : g + nrgb[1];
					nrgb[2] = b > 255 - nrgb[2] ? 255 : b + nrgb[2];
				}
				else
				{
					nrgb[0] = r > nrgb[0] ? r - nrgb[0] : 0;
					nrgb[1] = g > nrgb[1] ? g - nrgb[1] : 0;
					nrgb[2] = b > nrgb[2] ? b - nrgb[2] : 0;
				}
			}

			if (opacity < 255)
			{
				r = img0[ofs3 + 0];
				g = img0[ofs3 + 1];
				b = img0[ofs3 + 2];
				j = r * 255 + (nrgb[0] - r) * opacity;
				nrgb[0] = (j + (j >> 8) + 1) >> 8;
				j = g * 255 + (nrgb[1] - g) * opacity;
				nrgb[1] = (j + (j >> 8) + 1) >> 8;
				j = b * 255 + (nrgb[2] - b) * opacity;
				nrgb[2] = (j + (j >> 8) + 1) >> 8;
			}

			if (rgbmask)
			switch (rgbmask)
			{
			case 6: nrgb[1] = img0[ofs3 + 1]; /* Red */
			case 4: nrgb[2] = img0[ofs3 + 2]; /* Red + Green */
				break;
			case 5: nrgb[2] = img0[ofs3 + 2]; /* Green */
			case 1: nrgb[0] = img0[ofs3 + 0]; /* Green + Blue */
				break;
			case 3: nrgb[0] = img0[ofs3 + 0]; /* Blue */
			case 2: nrgb[1] = img0[ofs3 + 1]; /* Blue + Red */
				break;
			}

			imgr[ofs3 + 0] = nrgb[0];
			imgr[ofs3 + 1] = nrgb[1];
			imgr[ofs3 + 2] = nrgb[2];
		}
	}	
}

/* !!! This assumes dest area lies entirely within src, its bpp matches src's
 * current channel bpp, and it has alpha channel only if src has it too */
void copy_area(image_info *dest, image_info *src, int x, int y)
{
	int w = dest->width, h = dest->height, bpp = dest->bpp, ww = src->width;
	int i, ofs, delta, len;

	/* Current channel */
	ofs = (y * ww + x) * bpp;
	delta = 0;
	len = w * bpp;
	for (i = 0; i < h; i++)
	{
		memcpy(dest->img[CHN_IMAGE] + delta, src->img[mem_channel] + ofs, len);
		ofs += ww * bpp;
		delta += len;
	}

	/* Alpha channel */
	if (!dest->img[CHN_ALPHA]) return;
	ofs = y * ww + x;
	delta = 0;
	for (i = 0; i < h; i++)
	{
		memcpy(dest->img[CHN_ALPHA] + delta, src->img[CHN_ALPHA] + ofs, w);
		ofs += ww;
		delta += w;
	}
}

int mem_count_all_cols()				// Count all colours - Using main image
{
	return mem_count_all_cols_real(mem_img[CHN_IMAGE], mem_width, mem_height);
}

int mem_count_all_cols_real(unsigned char *im, int w, int h)	// Count all colours - very memory greedy
{
	guint32 *tab;
	int i, j, k, ix;

	j = 0x80000;
	tab = calloc(j, sizeof(guint32));	// HUGE colour cube
	if (!tab) return -1;			// Not enough memory Mr Greedy ;-)

	k = w * h;
	for (i = 0; i < k; i++)			// Scan each pixel
	{
		ix = (im[0] >> 5) + (im[1] << 3) + (im[2] << 11);
		tab[ix] |= 1 << (im[0] & 31);
		im += 3;
	}

	// Count each colour
	for (i = k = 0; i < j; i++) k += bitcount(tab[i]);

	free(tab);

	return k;
}

int mem_cols_used(int max_count)			// Count colours used in main RGB image
{
	if ( mem_img_bpp == 1 ) return -1;			// RGB only

	return (mem_cols_used_real(mem_img[CHN_IMAGE], mem_width, mem_height,
		max_count, 1));
}

int mem_cols_used_real(unsigned char *im, int w, int h, int max_count, int prog)
			// Count colours used in RGB chunk
{
	int i, j = w * h * 3, k, res = 1, pix;

	found[0] = MEM_2_INT(im, 0);
	if (prog) progress_init(_("Counting Unique RGB Pixels"), 0);
	for (i = 3; (i < j) && (res < max_count); i += 3) // Skim all pixels
	{
		pix = MEM_2_INT(im, i);
		for (k = 0; k < res; k++)
		{
			if (found[k] == pix) break;
		}
		if (k >= res)	// New colour so add to list
		{
			found[res++] = pix;
			if (!prog || (res & 7)) continue;
			if (progress_update((float)res / max_count)) break;
		}
	}
	if (prog) progress_end();

	return (res);
}


////	EFFECTS

static inline double dist(int n1, int n2)
{
	return (sqrt(n1 * n1 + n2 * n2));
}

void do_effect(int type, int param)
{
	unsigned char *src, *dest, *tmp = "\0", *mask = NULL;
	int i, j, k = 0, k1, k2, bpp, ll, dxp1, dxm1, dyp1, dym1;
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

	for (i = 0; i < mem_height; i++)
	{
		if (mask) row_protected(0, i, mem_width, tmp = mask);
		dyp1 = i < mem_height - 1 ? ll : -ll;
		dym1 = i ? -ll : ll;
		for (md = j = 0; j < ll; j++ , src++ , dest++)
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
			case FX_EDGE: /* Edge detect */
				k = *src;
				k = abs(k - src[dym1]) + abs(k - src[dyp1]) +
					abs(k - src[dxm1]) + abs(k - src[dxp1]);
				k += k >> 1;
				break;
			case FX_EMBOSS: /* Emboss */
				k = src[dym1] + src[dxm1] +
					src[dxm1 + dym1] + src[dxp1 + dym1];
				k = k / 4 - *src + 127;
				break;
			case FX_SHARPEN: /* Edge sharpen */
				k = src[dym1] + src[dyp1] +
					src[dxm1] + src[dxp1] - 4 * src[0];
				k = *src - blur * k;
				break;
			case FX_SOFTEN: /* Edge soften */
				k = src[dym1] + src[dyp1] +
					src[dxm1] + src[dxp1] - 4 * src[0];
				k = *src + (5 * k) / (125 - param);
				break;
			case FX_SOBEL: /* Another edge detector */
				k = dist((src[dxp1] - src[dxm1]) * 2 +
					src[dym1 + dxp1] - src[dym1 + dxm1] +
					src[dyp1 + dxp1] - src[dyp1 + dxm1],
					(src[dyp1] - src[dym1]) * 2 +
					src[dyp1 + dxm1] + src[dyp1 + dxp1] -
					src[dym1 + dxm1] - src[dym1 + dxp1]);
				break;
			case FX_PREWITT: /* Yet another edge detector */
/* Actually, the filter kernel used is "Robinson"; what is attributable to
 * Prewitt is "compass filtering", which can be done with other filter
 * kernels too - WJ */
			case FX_KIRSCH: /* Compass detector with another kernel */
/* Optimized compass detection algorithm: I calculate three values (compass,
 * plus and minus) and then mix them according to filter type - WJ */
				k = 0;
				k1 = src[dyp1 + dxm1] - src[dxp1];
				if (k < k1) k = k1;
				k1 += src[dyp1] - src[dym1 + dxp1];
				if (k < k1) k = k1;
				k1 += src[dyp1 + dxp1] - src[dym1];
				if (k < k1) k = k1;
				k1 += src[dxp1] - src[dym1 + dxm1];
				if (k < k1) k = k1;
				k1 += src[dym1 + dxp1] - src[dxm1];
				if (k < k1) k = k1;
				k1 += src[dym1] - src[dyp1 + dxm1];
				if (k < k1) k = k1;
				k1 += src[dym1 + dxm1] - src[dyp1];
				if (k < k1) k = k1;
				k1 = src[dym1 + dxm1] + src[dym1] + src[dym1 + dxp1] +
					src[dxm1] + src[dxp1];
				k2 = src[dyp1 + dxm1] + src[dyp1] + src[dyp1 + dxp1];
				if (type == FX_PREWITT)
					k = k * 2 + k1 - k2 - src[0] * 2;
				else /* if (type == FX_KIRSCH) */
					k = (k * 8 + k1 * 3 - k2 * 5) / 4;
					// Division is for equalizing weight of edge
				break;
			case FX_GRADIENT: /* Still another edge detector */
				k = 4.0 * dist(src[dxp1] - src[0],
					src[dyp1] - src[0]);
				break;
			case FX_ROBERTS: /* One more edge detector */
				k = 4.0 * dist(src[dyp1 + dxp1] - src[0],
					src[dxp1] - src[dyp1]);
				break;
			case FX_LAPLACE: /* The last edge detector... I hope */
				k = src[dym1 + dxm1] + src[dym1] + src[dym1 + dxp1] +
					src[dxm1] - 8 * src[0] + src[dxp1] +
					src[dyp1 + dxm1] + src[dyp1] + src[dyp1 + dxp1];
				break;
			case FX_MORPHEDGE: /* Morphological edge detection */
			case FX_ERODE: /* Greyscale erosion */
				k = src[0];
				if (k > src[dym1 + dxm1]) k = src[dym1 + dxm1];
				if (k > src[dym1]) k = src[dym1];
				if (k > src[dym1 + dxp1]) k = src[dym1 + dxp1];
				if (k > src[dxm1]) k = src[dxm1];
				if (k > src[dxp1]) k = src[dxp1];
				if (k > src[dyp1 + dxm1]) k = src[dyp1 + dxm1];
				if (k > src[dyp1]) k = src[dyp1];
				if (k > src[dyp1 + dxp1]) k = src[dyp1 + dxp1];
				if (type == FX_MORPHEDGE)
					k = (src[0] - k) * 2;
				break;
			case FX_DILATE: /* Greyscale dilation */
				k = src[0];
				if (k < src[dym1 + dxm1]) k = src[dym1 + dxm1];
				if (k < src[dym1]) k = src[dym1];
				if (k < src[dym1 + dxp1]) k = src[dym1 + dxp1];
				if (k < src[dxm1]) k = src[dxm1];
				if (k < src[dxp1]) k = src[dxp1];
				if (k < src[dyp1 + dxm1]) k = src[dyp1 + dxm1];
				if (k < src[dyp1]) k = src[dyp1];
				if (k < src[dyp1 + dxp1]) k = src[dyp1 + dxp1];
				break;
			}
			k = k < 0 ? 0 : k > 0xFF ? 0xFF : k;
			k = 255 * k + (*src - k) * op;
			*dest = (k + (k >> 8) + 1) >> 8;
		}
		if ((i * 10) % mem_height >= mem_height - 10)
			if (progress_update((float)(i + 1) / mem_height)) break;
	}
	free(mask);
	progress_end();
}

/* Apply vertical filter */
static void vert_gauss(unsigned char *chan, int w, int h, int y, double *temp,
	double *gaussY, int lenY, int gcor)
{
	unsigned char *src0, *src1;
	double gv = gaussY[0];
	int j, k, mh2 = h > 1 ? h + h - 2 : 1;

	src0 = chan + y * w;
	if (gcor) /* Gamma-correct RGB values */
	{
		for (j = 0; j < w; j++) temp[j] = gamma256[src0[j]] * gv;
	}
	else /* Leave RGB values as they were */
	{
		for (j = 0; j < w; j++) temp[j] = src0[j] * gv;
	}
	for (j = 1; j < lenY; j++)
	{
		double gv = gaussY[j];

		k = (y + j) % mh2;
		if (k >= h) k = mh2 - k;
		src0 = chan + k * w;
		k = abs(y - j) % mh2;
		if (k >= h) k = mh2 - k;
		src1 = chan + k * w;
		if (gcor) /* Gamma-correct */
		{
			for (k = 0; k < w; k++)
			{
				temp[k] += (gamma256[src0[k]] +
					gamma256[src1[k]]) * gv;
			}
		}
		else /* Leave alone */
		{
			for (k = 0; k < w; k++)
			{
				temp[k] += (src0[k] + src1[k]) * gv;
			}
		}
	}
}

typedef struct {
	double *gaussX, *gaussY, *temp;
	unsigned char *mask;
	int *idx;
	int lenX, lenY;
	int channel, gcor;
	// For unsharp mask
	int threshold;
	double amount;
} gaussd;

/* Extend horizontal array, using precomputed indices */
static void gauss_extend(gaussd *gd, double *temp, int w, int bpp)
{
	double *dest, *src;
	int i, l = gd->lenX - 1, *tp = gd->idx;

	dest = temp - l * bpp;
	while (TRUE)
	{
		for (i = 0; i < l; i++ , dest += bpp)
		{
			src = temp + *tp++ * bpp;
			dest[0] = src[0];
			if (bpp == 1) continue;
			dest[1] = src[1];
			dest[2] = src[2];
		}
		if (dest != temp) break; // "w * bpp" is definitely nonzero
		dest += w * bpp;
	}
}

/* Apply RGB horizontal filter */
static void hor_gauss3(double *temp, int w, double *gaussX, int lenX,
	unsigned char *mask)
{
	int j, jj, k, x1, x2;
	double sum0, sum1, sum2;

	for (j = jj = 0; jj < w; jj++ , j += 3)
	{
		if (mask[jj] == 255) continue;
		sum0 = temp[j] * gaussX[0];
		sum1 = temp[j + 1] * gaussX[0];
		sum2 = temp[j + 2] * gaussX[0];
		x1 = x2 = j;
		for (k = 1; k < lenX; k++)
		{
			double gv = gaussX[k];
			x1 -= 3; x2 += 3;
			sum0 += (temp[x1] + temp[x2]) * gv;
			sum1 += (temp[x1 + 1] + temp[x2 + 1]) * gv;
			sum2 += (temp[x1 + 2] + temp[x2 + 2]) * gv;
		}
		temp[x1] = sum0;
		temp[x1 + 1] = sum1;
		temp[x1 + 2] = sum2;
	}
}

// !!! Will need extra checks if used for out-of-range values
static void pack_row3(unsigned char *dest, const double *src, int w, int gcor,
	unsigned char *mask)
{
	int j, jj, k0, k1, k2, m;
	double sum0, sum1, sum2;

	for (j = jj = 0; jj < w; jj++ , j += 3)
	{
		sum0 = src[j];
		sum1 = src[j + 1];
		sum2 = src[j + 2];
		if (gcor) /* Reverse gamma correction */
		{
			k0 = UNGAMMA256(sum0);
			k1 = UNGAMMA256(sum1);
			k2 = UNGAMMA256(sum2);
		}
		else /* Simply round to nearest */
		{
			k0 = rint(sum0);
			k1 = rint(sum1);
			k2 = rint(sum2);
		}
		m = mask[jj];
		k0 = k0 * 255 + (dest[j] - k0) * m;
		dest[j] = (k0 + (k0 >> 8) + 1) >> 8;
		k1 = k1 * 255 + (dest[j + 1] - k1) * m;
		dest[j + 1] = (k1 + (k1 >> 8) + 1) >> 8;
		k2 = k2 * 255 + (dest[j + 2] - k2) * m;
		dest[j + 2] = (k2 + (k2 >> 8) + 1) >> 8;
	}
}

/* Most-used variables are local to inner blocks to shorten their live ranges -
 * otherwise stupid compilers might allocate them to memory */
static void gauss_filter(tcb *thread)
{
	gaussd *gd = thread->data;
	int lenX = gd->lenX, gcor = gd->gcor, channel = gd->channel;
	int i, ii, cnt, wid, bpp;
	double sum, *temp, *gaussX = gd->gaussX;
	unsigned char *chan, *dest, *mask = gd->mask;

	cnt = thread->nsteps;
	bpp = BPP(channel);
	wid = mem_width * bpp;
	chan = mem_undo_previous(channel);
	temp = gd->temp + (lenX - 1) * bpp;
	for (i = thread->step0 , ii = 0; ii < cnt; i++ , ii++)
	{
		vert_gauss(chan, wid, mem_height, i, temp, gd->gaussY, gd->lenY, gcor);
		gauss_extend(gd, temp, mem_width, bpp);
		row_protected(0, i, mem_width, mask);
		dest = mem_img[channel] + i * wid;
		if (bpp == 3) /* Run 3-bpp horizontal filter */
		{
			hor_gauss3(temp, mem_width, gaussX, lenX, mask);
			pack_row3(dest, gd->temp, mem_width, gcor, mask);
		}
		else /* Run 1-bpp horizontal filter - no gamma here */
		{
			int j, k, k0;

			for (j = 0; j < mem_width; j++)
			{
				if (mask[j] == 255) continue;
				sum = temp[j] * gaussX[0];
				for (k = 1; k < lenX; k++)
				{
					sum += (temp[j - k] + temp[j + k]) * gaussX[k];
				}
				k0 = rint(sum);
				k0 = k0 * 255 + (dest[j] - k0) * mask[j];
				dest[j] = (k0 + (k0 >> 8) + 1) >> 8;
			}
		}
		if (thread_step(thread, ii + 1, cnt, 10)) break;
	}
	thread_done(thread);
}

/* The inner loops are coded the way they are because one must spell everything
 * out for dumb compiler to get acceptable code out of it */
static void gauss_filter_rgba(tcb *thread)
{
	gaussd *gd = thread->data;
	int i, ii, cnt, mh2, lenX = gd->lenX, lenY = gd->lenY, gcor = gd->gcor;
	double sum, sum1, sum2, mult, *temp, *tmpa, *atmp, *src, *gaussX, *gaussY;
	unsigned char *chan, *dest, *alpha, *dsta, *mask = gd->mask;

	cnt = thread->nsteps;
	chan = mem_undo_previous(CHN_IMAGE);
	alpha = mem_undo_previous(CHN_ALPHA);
	gaussX = gd->gaussX;
	gaussY = gd->gaussY;
	temp = gd->temp + (lenX - 1) * 3;
	mh2 = mem_height > 1 ? 2 * mem_height - 2 : 1;

	/* Set up the main row buffer and process the image */
	tmpa = temp + mem_width * 3 + (lenX - 1) * (3 + 3);
	atmp = tmpa + mem_width * 3 + (lenX - 1) * (3 + 1);
	for (i = thread->step0 , ii = 0; ii < cnt; i++ , ii++)
	{
		/* Apply vertical filter */
		{
			unsigned char *srcc, *src0, *src1;
			unsigned char *alff, *alf0, *alf1;
			int j, k;

			alff = alpha + i * mem_width;
			srcc = chan + i * mem_width * 3;
			if (gcor) /* Gamma correct */
			{
				double gk = gaussY[0];
				int j, jj;

				for (j = jj = 0; j < mem_width; j++)
				{
					double ak, tv;

					atmp[j] = ak = alff[j] * gk;
					temp[jj] = (tv = gamma256[srcc[jj]]) * gk;
					tmpa[jj++] = tv * ak;
					temp[jj] = (tv = gamma256[srcc[jj]]) * gk;
					tmpa[jj++] = tv * ak;
					temp[jj] = (tv = gamma256[srcc[jj]]) * gk;
					tmpa[jj++] = tv * ak;
				}
			}
			else /* Use as is */
			{
				double gk = gaussY[0];
				int j, jj;

				for (j = jj = 0; j < mem_width; j++)
				{
					double ak, tv;

					atmp[j] = ak = alff[j] * gk;
					temp[jj] = (tv = srcc[jj]) * gk;
					tmpa[jj++] = tv * ak;
					temp[jj] = (tv = srcc[jj]) * gk;
					tmpa[jj++] = tv * ak;
					temp[jj] = (tv = srcc[jj]) * gk;
					tmpa[jj++] = tv * ak;
				}
			}
			for (j = 1; j < lenY; j++)
			{
				double gk = gaussY[j];

				k = (i + j) % mh2;
				if (k >= mem_height) k = mh2 - k;
				alf0 = alpha + k * mem_width;
				src0 = chan + k * mem_width * 3;
				k = abs(i - j) % mh2;
				if (k >= mem_height) k = mh2 - k;
				alf1 = alpha + k * mem_width;
				src1 = chan + k * mem_width * 3;
				if (gcor) /* Gamma correct */
				{
					int k, kk;

					for (k = kk = 0; k < mem_width; k++)
					{
						double ak0, ak1, tv0, tv1;

						ak0 = alf0[k] * gk;
						ak1 = alf1[k] * gk;
						atmp[k] += ak0 + ak1;
						tv0 = gamma256[src0[kk]];
						tv1 = gamma256[src1[kk]];
						temp[kk] += (tv0 + tv1) * gk;
						tmpa[kk++] += tv0 * ak0 + tv1 * ak1;
						tv0 = gamma256[src0[kk]];
						tv1 = gamma256[src1[kk]];
						temp[kk] += (tv0 + tv1) * gk;
						tmpa[kk++] += tv0 * ak0 + tv1 * ak1;
						tv0 = gamma256[src0[kk]];
						tv1 = gamma256[src1[kk]];
						temp[kk] += (tv0 + tv1) * gk;
						tmpa[kk++] += tv0 * ak0 + tv1 * ak1;
					}
				}
				else /* Use as is */
				{
					int k, kk;

					for (k = kk = 0; k < mem_width; k++)
					{
						double ak0, ak1, tv0, tv1;

						ak0 = alf0[k] * gk;
						ak1 = alf1[k] * gk;
						atmp[k] += ak0 + ak1;
						tv0 = src0[kk];
						tv1 = src1[kk];
						temp[kk] += (tv0 + tv1) * gk;
						tmpa[kk++] += tv0 * ak0 + tv1 * ak1;
						tv0 = src0[kk];
						tv1 = src1[kk];
						temp[kk] += (tv0 + tv1) * gk;
						tmpa[kk++] += tv0 * ak0 + tv1 * ak1;
						tv0 = src0[kk];
						tv1 = src1[kk];
						temp[kk] += (tv0 + tv1) * gk;
						tmpa[kk++] += tv0 * ak0 + tv1 * ak1;
					}
				}
			}
		}
		gauss_extend(gd, temp, mem_width, 3);
		gauss_extend(gd, tmpa, mem_width, 3);
		gauss_extend(gd, atmp, mem_width, 1);
		row_protected(0, i, mem_width, mask);
		dest = mem_img[CHN_IMAGE] + i * mem_width * 3;
		dsta = mem_img[CHN_ALPHA] + i * mem_width;
		/* Horizontal RGBA filter */
		{
			int j, jj, k, kk, x1, x2;

			for (j = jj = 0; j < mem_width; j++ , jj += 3)
			{
				if (mask[j] == 255) continue;

				sum = atmp[j] * gaussX[0];
				for (k = 1; k < lenX; k++)
				{
					sum += (atmp[j - k] + atmp[j + k]) * gaussX[k];
				}
				k = rint(sum);
				src = temp;
				mult = 1.0;
				if (k)
				{
					src = tmpa;
					mult /= sum;
				}
				kk = mask[j];
				k = k * 255 + (dsta[j] - k) * kk;
				if (k) mask[j] = (255 * kk * dsta[j]) / k;
				dsta[j] = (k + (k >> 8) + 1) >> 8;

				sum = src[jj] * gaussX[0];
				sum1 = src[jj + 1] * gaussX[0];
				sum2 = src[jj + 2] * gaussX[0];
				x1 = x2 = jj;
				for (k = 1; k < lenX; k++)
				{
					double gv = gaussX[k];
					x1 -= 3; x2 += 3;
					sum += (src[x1] + src[x2]) * gv;
					sum1 += (src[x1 + 1] + src[x2 + 1]) * gv;
					sum2 += (src[x1 + 2] + src[x2 + 2]) * gv;
				}
				temp[x1] = sum * mult;
				temp[x1 + 1] = sum1 * mult;
				temp[x1 + 2] = sum2 * mult;
			}
			pack_row3(dest, gd->temp, mem_width, gcor, mask);
		}
		if (thread_step(thread, ii + 1, cnt, 10)) break;
	}
	thread_done(thread);
}

/* Modes: 0 - normal, 1 - RGBA, 2 - DoG */
static threaddata *init_gauss(gaussd *gd, double radiusX, double radiusY, int mode)
{
	threaddata *tdata;
	int i, j, k, l, lenX, lenY, w, bpp = MEM_BPP;
	double sum, exkX, exkY, *gauss;


	/* Cutoff point is where gaussian becomes < 1/255 */
	gd->lenX = lenX = ceil(radiusX) + 2;
	gd->lenY = lenY = ceil(radiusY) + 2;
	exkX = -log(255.0) / ((radiusX + 1.0) * (radiusX + 1.0));
	exkY = -log(255.0) / ((radiusY + 1.0) * (radiusY + 1.0));

	/* Allocate memory */
	if (mode == 1) i = 7;			/* Extra linebuffer for RGBA */
	else if (mode == 2) i = bpp + bpp;	/* Extra buffer in DoG mode */
	else i = bpp;
	l = 2 * (lenX - 1);
	w = mem_width + l;

	tdata = talloc(MA_ALIGN_DOUBLE, 0, gd, sizeof(gaussd),
		&gd->gaussX, lenX * sizeof(double),
		&gd->gaussY, lenY * sizeof(double),
		&gd->idx, l * sizeof(int),
		NULL, 
		&gd->temp, i * w * sizeof(double),
		&gd->mask, mem_width,
		NULL);
	if (!tdata) return (NULL);

	/* Prepare filters */
	j = lenX; gauss = gd->gaussX;
	while (1)
	{
		sum = gauss[0] = 1.0;
		for (i = 1; i < j; i++)
		{
			sum += 2.0 * (gauss[i] = exp((double)(i * i) * exkX));
		}
		sum = 1.0 / sum;
		for (i = 0; i < j; i++)
		{
			gauss[i] *= sum;
		}
		if (gauss != gd->gaussX) break;
		exkX = exkY; j = lenY; gauss = gd->gaussY;
	}

	/* Prepare horizontal indices, assuming mirror boundary */
	if (mem_width > 1) // Else, good already (zeroed out)
	{
		int *idx = gd->idx + lenX - 1; // To simplify indexing

		k = 2 * mem_width - 2;
		for (i = 1; i < lenX; i++)
		{
			j = i % k;
			idx[-i] = j < mem_width ? j : k - j;
			j = (mem_width + i - 1) % k;
			idx[i - 1] = j < mem_width ? j : k - j;

		}
	}
	return (tdata);
}

/* Gaussian blur */
void mem_gauss(double radiusX, double radiusY, int gcor)
{
	gaussd gd;
	threaddata *tdata;
	int rgba, rgbb;

	/* RGBA or not? */
	rgba = (mem_channel == CHN_IMAGE) && mem_img[CHN_ALPHA] && RGBA_mode;
	rgbb = rgba && !channel_dis[CHN_ALPHA];

	/* Create arrays */
	if (mem_channel != CHN_IMAGE) gcor = FALSE;
	gd.gcor = gcor;
	gd.channel = mem_channel;
	tdata = init_gauss(&gd, radiusX, radiusY, rgbb);
	if (!tdata)
	{
		memory_errors(1);
		return;
	}

	progress_init(_("Gaussian Blur"), 1);
	if (rgbb) /* Coupled RGBA */
		launch_threads(gauss_filter_rgba, tdata, NULL, mem_height);
	else /* One channel, or maybe two */
	{
		launch_threads(gauss_filter, tdata, NULL, mem_height);
		if (rgba) /* Need to process alpha too */
		{
#ifdef U_THREADS
			int i, j = tdata->count;

			for (i = 0; i < j; i++)
			{
				gaussd *gp = tdata->threads[i]->data;
				gp->channel = CHN_ALPHA;
				gp->gcor = FALSE;
			}
#else
			gaussd *gp = tdata->threads[0]->data;
			gp->channel = CHN_ALPHA;
			gp->gcor = FALSE;
#endif
			launch_threads(gauss_filter, tdata, NULL, mem_height);
		}
	}
	progress_end();
	free(tdata);
}

static void unsharp_filter(tcb *thread)
{
	gaussd *gd = thread->data;
	int lenX = gd->lenX, threshold = gd->threshold, channel = gd->channel;
	int i, ii, cnt, wid, bpp, gcor = gd->gcor;
	double *temp, *gaussX = gd->gaussX;
	double sum, sum1, sum2, amount = gd->amount;
	unsigned char *chan, *dest, *mask = gd->mask;

	cnt = thread->nsteps;
	bpp = BPP(channel);
	wid = mem_width * bpp;
	chan = mem_undo_previous(channel);
	temp = gd->temp + (lenX - 1) * bpp;
	for (i = thread->step0 , ii = 0; ii < cnt; i++ , ii++)
	{
		vert_gauss(chan, wid, mem_height, i, temp, gd->gaussY, gd->lenY, gcor);
		gauss_extend(gd, temp, mem_width, bpp);
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
				x1 = x2 = j;
				for (k = 1; k < lenX; k++)
				{
					double gv = gaussX[k];
					x1 -= 3; x2 += 3;
					sum += (temp[x1] + temp[x2]) * gv;
					sum1 += (temp[x1 + 1] + temp[x2 + 1]) * gv;
					sum2 += (temp[x1 + 2] + temp[x2 + 2]) * gv;
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
				/* Threshold */
	/* !!! GIMP has an apparent bug which I won't reproduce - so mtPaint's
	 * threshold value means _actual_ difference, not half of it - WJ */
				if ((abs(k - dest[j]) < threshold) &&
					(abs(k1 - dest[j + 1]) < threshold) &&
					(abs(k2 - dest[j + 2]) < threshold))
					continue;
				if (gcor) /* Involve gamma *AGAIN* */
				{
					sum = gamma256[dest[j]] + amount *
						(gamma256[dest[j]] - sum);
					sum1 = gamma256[dest[j + 1]] + amount *
						(gamma256[dest[j + 1]] - sum1);
					sum2 = gamma256[dest[j + 2]] + amount *
						(gamma256[dest[j + 2]] - sum2);
					k = UNGAMMA256X(sum);
					k1 = UNGAMMA256X(sum1);
					k2 = UNGAMMA256X(sum2);
				}
				else /* Combine values as linear */
				{
					k = rint(dest[j] + amount *
						(dest[j] - sum));
					k = k < 0 ? 0 : k > 255 ? 255 : k;
					k1 = rint(dest[j + 1] + amount *
						(dest[j + 1] - sum1));
					k1 = k1 < 0 ? 0 : k1 > 255 ? 255 : k1;
					k2 = rint(dest[j + 2] + amount *
						(dest[j + 2] - sum2));
					k2 = k2 < 0 ? 0 : k2 > 255 ? 255 : k2;
				}
				/* Store the result */
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
					sum += (temp[j - k] + temp[j + k]) * gaussX[k];
				}
				k = rint(sum);
				/* Threshold */
				/* !!! Same non-bug as above */
				if (abs(k - dest[j]) < threshold) continue;
				/* Combine values */
				k = rint(dest[j] + amount * (dest[j] - sum));
				k = k < 0 ? 0 : k > 255 ? 255 : k;
				/* Store the result */
				k = k * 255 + (dest[j] - k) * mask[j];
				dest[j] = (k + (k >> 8) + 1) >> 8;
			}
		}
		if (thread_step(thread, ii + 1, cnt, 10)) break;
	}
	thread_done(thread);
}

/* Unsharp mask */
void mem_unsharp(double radius, double amount, int threshold, int gcor)
{
	gaussd gd;
	threaddata *tdata;

	/* Create arrays */
	if (mem_channel != CHN_IMAGE) gcor = 0;
	gd.gcor = gcor;
	gd.channel = mem_channel;
	gd.amount = amount;
	gd.threshold = threshold;
// !!! No RGBA mode for now
	tdata = init_gauss(&gd, radius, radius, 0);
	if (!tdata)
	{
		memory_errors(1);
		return;
	}
	/* Run filter */
	launch_threads(unsharp_filter, tdata, _("Unsharp Mask"), mem_height);
	free(tdata);
}	

static void do_alpha_blend(unsigned char *dest, unsigned char *lower,
	unsigned char *upper, unsigned char *alpha, int len, int bpp)
{
	int j, k, mv;

	for (j = 0; j < len;)
	{
		mv = *alpha++;
		k = lower[j];
		k = k * 255 + (upper[j] - k) * mv;
		dest[j++] = (k + (k >> 8) + 1) >> 8;
		if (bpp == 1) continue;
		k = lower[j];
		k = k * 255 + (upper[j] - k) * mv;
		dest[j++] = (k + (k >> 8) + 1) >> 8;
		k = lower[j];
		k = k * 255 + (upper[j] - k) * mv;
		dest[j++] = (k + (k >> 8) + 1) >> 8;
	}
}

/* Retroactive masking - by blending with undo frame */
void mask_merge(unsigned char *old, int channel, unsigned char *mask)
{
	chanlist tlist;
	unsigned char *dest, *mask0 = NULL;
	int i, ofs, bpp = BPP(channel), w = mem_width * bpp;

	memcpy(tlist, mem_img, sizeof(chanlist));
	tlist[channel] = old;

	/* Clear mask or copy mask channel into it */
	if ((channel <= CHN_ALPHA) && mem_img[CHN_MASK] && !channel_dis[CHN_MASK])
		mask0 = tlist[CHN_MASK];

	for (i = 0; i < mem_height; i++)
	{
		ofs = i * mem_width;
		prep_mask(0, 1, mem_width, mask, mask0 ? mask0 + ofs : NULL,
			tlist[CHN_IMAGE] + ofs * mem_img_bpp);
		dest = mem_img[channel] + ofs * bpp;
		do_alpha_blend(dest, dest, old + ofs * bpp, mask, w, bpp);
	}
}

static void dog_filter(tcb *thread)
{
	gaussd *gd = thread->data;
	int channel = gd->channel, bpp = BPP(channel), wid = mem_width * bpp;
	int i, ii, cnt, gcor = gd->gcor;
	int lenW = gd->lenX, lenN = gd->lenY;
	double sum, sum1, sum2, *tmp1, *tmp2;
	double *gaussW = gd->gaussX, *gaussN = gd->gaussY;
	unsigned char *chan, *dest;

	cnt = thread->nsteps;
	chan = mem_undo_previous(channel);
	tmp1 = gd->temp + (lenW - 1) * bpp;
	tmp2 = tmp1 + wid + (lenW - 1) * bpp * 2;
	for (i = thread->step0 , ii = 0; ii < cnt; i++ , ii++)
	{
		vert_gauss(chan, wid, mem_height, i, tmp1, gaussW, lenW, gcor);
		vert_gauss(chan, wid, mem_height, i, tmp2, gaussN, lenN, gcor);
		gauss_extend(gd, tmp1, mem_width, bpp);
		gauss_extend(gd, tmp2, mem_width, bpp);
		dest = mem_img[channel] + i * wid;
		if (bpp == 3) /* Run 3-bpp horizontal filter */
		{
			int j, jj, k, k1, k2;

			for (j = jj = 0; jj < mem_width; jj++ , j += 3)
			{
				int x1, x2, x3, x4;

				sum = tmp1[j] * gaussW[0] - tmp2[j] * gaussN[0];
				sum1 = tmp1[j + 1] * gaussW[0] - tmp2[j + 1] * gaussN[0];
				sum2 = tmp1[j + 2] * gaussW[0] - tmp2[j + 2] * gaussN[0];
				x1 = x2 = j;
				for (k = 1; k < lenW; k++)
				{
					double gv = gaussW[k];
					x1 -= 3; x2 += 3;
					sum += (tmp1[x1] + tmp1[x2]) * gv;
					sum1 += (tmp1[x1 + 1] + tmp1[x2 + 1]) * gv;
					sum2 += (tmp1[x1 + 2] + tmp1[x2 + 2]) * gv;
				}
				x3 = x4 = j;
				for (k = 1; k < lenN; k++)
				{
					double gv = gaussN[k];
					x3 -= 3; x4 += 3;
					sum -= (tmp2[x3] + tmp2[x4]) * gv;
					sum1 -= (tmp2[x3 + 1] + tmp2[x4 + 1]) * gv;
					sum2 -= (tmp2[x3 + 2] + tmp2[x4 + 2]) * gv;
				}
				if (gcor)
				{
#if 1 /* Reverse gamma - but does it make sense? */
					k = UNGAMMA256X(sum);
					k1 = UNGAMMA256X(sum1);
					k2 = UNGAMMA256X(sum2);
#else /* Let values remain linear */
					k = rint(sum * 255.0);
					k = k < 0 ? 0 : k;
					k1 = rint(sum1 * 255.0);
					k1 = k1 < 0 ? 0 : k1;
					k2 = rint(sum2 * 255.0);
					k2 = k2 < 0 ? 0 : k2;
#endif
				}
				else
				{
					k = rint(sum);
					k = k < 0 ? 0 : k;
					k1 = rint(sum1);
					k1 = k1 < 0 ? 0 : k1;
					k2 = rint(sum2);
					k2 = k2 < 0 ? 0 : k2;
				}
				/* Store the result */
				dest[j] = k;
				dest[j + 1] = k1;
				dest[j + 2] = k2;
			}
		}
		else /* Run 1-bpp horizontal filter - no gamma here */
		{
			int j, k;

			for (j = 0; j < mem_width; j++)
			{
				sum = tmp1[j] * gaussW[0] - tmp2[j] * gaussN[0];
				for (k = 1; k < lenW; k++)
				{
					sum += (tmp1[j - k] + tmp1[j + k]) * gaussW[k];
				}
				for (k = 1; k < lenN; k++)
				{
					sum -= (tmp2[j - k] + tmp2[j + k]) * gaussN[k];
				}
				k = rint(sum);
				dest[j] = k < 0 ? 0 : k;
			}
		}
		if (thread_step(thread, ii + 1, cnt, 10)) break;
	}
	thread_done(thread);
}

/* Difference of Gaussians */
void mem_dog(double radiusW, double radiusN, int norm, int gcor)
{
	gaussd gd;
	threaddata *tdata;

	/* Create arrays */
	if (mem_channel != CHN_IMAGE) gcor = 0;
	gd.gcor = gcor;
	gd.channel = mem_channel;
// !!! No RGBA mode for ever - DoG mode instead
	tdata = init_gauss(&gd, radiusW, radiusN, 2);
	if (!tdata)
	{
		memory_errors(1);
		return;
	}

	/* Run filter */
	progress_init(_("Difference of Gaussians"), 1);
	launch_threads(dog_filter, tdata, NULL, mem_height);

	/* Normalize values (expand to full 0..255) */
	while (norm)
	{
		unsigned char *tmp, xtb[256];
		double d;
		int i, l, mx = 0;

		l = mem_height * mem_width * BPP(mem_channel);
		tmp = mem_img[mem_channel];
		for (i = l; i; i-- , tmp++)
			if (*tmp > mx) mx = *tmp;

		if (!mx) break;
		d = 255.0 / (double)mx;
		for (i = 0; i <= mx; i++) xtb[i] = rint(i * d);

		tmp = mem_img[mem_channel];
		for (i = l; i; i-- , tmp++) *tmp = xtb[*tmp];

		break;
	}

	/* Mask-merge with prior picture */
	mask_merge(mem_undo_previous(mem_channel), mem_channel, gd.mask);

	progress_end();
	free(tdata);
}


/* !!! Kuwahara-Nagao filter's radius is limited to 255, to use byte offsets */
typedef struct {
	int *idx;	// Index array
	double *rs;	// Sum of gamma-corrected RGB if using gamma
	int *avg;	// Sum of pixel values (for average)
	int *dis;	// Sum of pixel values squared (for variance)
	unsigned char *min;	// Offset to minimum-variance square
	double r2i;	// 1/r^2 to multiply things with
	int w, r;	// Row width & filter radius
	int gcor;	// Gamma correction toggle
	int l, rl;	// Row array lengths
} kuwahara_info;

/* This function uses running sums, which gives x87 FPU's "precision jitter"
 * a chance to accumulate; to avoid, reduced-precision gamma is used - WJ */
static void kuwahara_row(unsigned char *src, int base, int add, kuwahara_info *info)
{
	double rs0 = 0.0, rs1 = 0.0, rs2 = 0.0;
	int avg[3] = { 0, 0, 0 }, dis[3] = { 0, 0, 0 };
	int i, w, r = info->r, gc = info->gcor, *idx = info->idx;

	w = info->w + r++;
	for (i = -r; i < w; i++)
	{
		unsigned char *tvv;
		int tv, i3;

		tvv = src + idx[i];
		avg[0] += (tv = tvv[0]);
		dis[0] += tv * tv;
		avg[1] += (tv = tvv[1]);
		dis[1] += tv * tv;
		avg[2] += (tv = tvv[2]);
		dis[2] += tv * tv;
		if (gc)
		{
			rs0 += Fgamma256[tvv[0]];
			rs1 += Fgamma256[tvv[1]];
			rs2 += Fgamma256[tvv[2]];
		}
		if (i < 0) continue;

		tvv = src + idx[i - r];
		avg[0] -= (tv = tvv[0]);
		dis[0] -= tv * tv;
		avg[1] -= (tv = tvv[1]);
		dis[1] -= tv * tv;
		avg[2] -= (tv = tvv[2]);
		dis[2] -= tv * tv;
		i3 = (base + i) * 3;
		if (add)
		{
			info->avg[i3 + 0] += avg[0];
			info->avg[i3 + 1] += avg[1];
			info->avg[i3 + 2] += avg[2];
			info->dis[i3 + 0] += dis[0];
			info->dis[i3 + 1] += dis[1];
			info->dis[i3 + 2] += dis[2];
		}
		else
		{
			info->avg[i3 + 0] -= avg[0];
			info->avg[i3 + 1] -= avg[1];
			info->avg[i3 + 2] -= avg[2];
			info->dis[i3 + 0] -= dis[0];
			info->dis[i3 + 1] -= dis[1];
			info->dis[i3 + 2] -= dis[2];
		}
		if (!gc) continue;
		rs0 -= Fgamma256[tvv[0]];
		rs1 -= Fgamma256[tvv[1]];
		rs2 -= Fgamma256[tvv[2]];
		if (add)
		{
			info->rs[i3 + 0] += rs0;
			info->rs[i3 + 1] += rs1;
			info->rs[i3 + 2] += rs2;
		}
		else
		{
			info->rs[i3 + 0] -= rs0;
			info->rs[i3 + 1] -= rs1;
			info->rs[i3 + 2] -= rs2;
		}
	}
}

static void kuwahara_copy(int dest, int src, kuwahara_info *info)
{
	src *= 3; dest *= 3;
	memcpy(info->rs + dest, info->rs + src, info->rl * 3 * sizeof(double));
	memcpy(info->avg + dest, info->avg + src, info->l * 3 * sizeof(int));
	memcpy(info->dis + dest, info->dis + src, info->l * 3 * sizeof(int));
}

static double kuwahara_square(int idx, kuwahara_info *info)
{
	double r2i = info->r2i;
	int *dp = info->dis + idx * 3, *ap = info->avg + idx * 3;
	// !!! Multiplication is done this way to avoid integer overflow
	return (dp[0] + dp[1] + dp[2] - ((r2i * ap[0]) * ap[0] +
		(r2i * ap[1]) * ap[1] + (r2i * ap[2]) * ap[2]));
}

/* For each X, locate the square with minimum variance & store its offset */
static void kuwahara_min(int base, kuwahara_info *info)
{
	double da[256];
	int i, j, j1, w = info->w, r = info->r, min = -1;

	for (i = 0; i < r; i++) da[i] = kuwahara_square(base + i, info);
	for (i = 0; i < w; i++)
	{
		j1 = (i + r) & 255;
		da[j1] = kuwahara_square(base + (i + r), info);
		if (min > i) // Old minimum still valid
		{
			if (da[j1] <= da[min & 255]) min = i + r;
		}
		else // Forced to find a new one
		{
			min = i;
			for (j = 1; j <= r; j++)
				if (da[(i + j) & 255] <= da[min & 255])
					min = i + j;
		}
		info->min[base + i] = min - i;
	}
}

/* Replace each pixel in image row by nearest color in 3x3 Kuwahara'ed region */
static void kuwahara_detailed(unsigned char *buf, unsigned char *mask, int y,
	int gcor)
{
	unsigned char *tmp;
	int l, w = mem_width * 3;
#define REGION_SIZE 9
	int steps[REGION_SIZE] = { 3, 3, w, 3, 3, w, 3, 3, w };

#if 0 /* Make scanning order the same as with flat image */
	l = (y + 2) % 3;
	buf += (w + 6) * l;
	steps [8 - 3 * l] -= (w + 6) * 3;
#endif

	row_protected(0, y, mem_width, mask);
	tmp = mem_img[CHN_IMAGE] + y * w;
	for (l = 0; l < mem_width; l++ , tmp += 3)
	{
		unsigned char *tb, *found;
		int rr, gg, bb, op = *mask++;

		if (op == 255) continue;
		/* Find the nearest color pixel */
		rr = tmp[0]; gg = tmp[1]; bb = tmp[2];
		found = tb = buf + l * 3;
		if (gcor) // Gamma corrected
		{
			double d, r2, g2, b2;
			int i;

			d = 100.0; // More than 3*1.0^2
			r2 = gamma256[rr];
			g2 = gamma256[gg];
			b2 = gamma256[bb];
			for (i = 0; i < REGION_SIZE; tb += steps[i++])
			{
				double d2, dr, dg, db;

				dr = gamma256[tb[0]] - r2;
				dg = gamma256[tb[1]] - g2;
				db = gamma256[tb[2]] - b2;
				d2 = dr * dr + dg * dg + db * db;
				if (d2 >= d) continue;
				found = tb; d = d2;
			}
		}
		else // Raw RGB
		{
			int i, d, d2;

			d = 1000000; // More than 3*255^2
			for (i = 0; i < REGION_SIZE; tb += steps[i++])
			{
				d2 = (rr - tb[0]) * (rr - tb[0]) +
					(gg - tb[1]) * (gg - tb[1]) +
					(bb - tb[2]) * (bb - tb[2]);
				if (d2 >= d) continue;
				found = tb; d = d2;
			}
		}
		/* Mask-merge it into image */
		rr = found[0]; gg = found[1]; bb = found[2];
		rr = 255 * rr + (*tmp - rr) * op;
		tmp[0] = (rr + (rr >> 8) + 1) >> 8;
		gg = 255 * gg + (*tmp - gg) * op;
		tmp[1] = (gg + (gg >> 8) + 1) >> 8;
		bb = 255 * bb + (*tmp - bb) * op;
		tmp[2] = (bb + (bb >> 8) + 1) >> 8;
	}
#undef REGION_SIZE
}

/* Convert virtual row to row index (mirror boundary) */
static int idx2row(int idx)
{
	int j, k;

	if (mem_height == 1) return (0);
	k = mem_height + mem_height - 2;
	j = abs(idx) % k;
	if (j >= mem_height) j = k - j;
	return (j);
}

/* RGB only - cannot be generalized without speed loss */
void mem_kuwahara(int r, int gcor, int detail)
{
	kuwahara_info info;
	unsigned char *mem, *src, *buf, *mask, *tmp, *timg;
	int i, j, k, l, ir, len, rl, r1 = r + 1;
	int w = mem_width * 3, wbuf = w + 3 * 2, ch = mem_channel;
	double r2i = 1.0 / (double)(r1 * r1);


	if (mem_img_bpp != 3) return; // Sanity check

	len = mem_width + r + r + 1;
	info.l = l = mem_width + r;
	info.rl = rl = gcor ? l : 0;
	mem = multialloc(MA_ALIGN_DOUBLE,
		&info.rs, rl * r1 * 3 * sizeof(double),
		&info.avg, l * r1 * 3 * sizeof(int),
		&info.dis, l * r1 * 3 * sizeof(int),
		&info.idx, len * sizeof(int),
		&info.min, l * r1,
		&mask, mem_width,
		&timg, wbuf * 3,
		NULL);
	if (!mem)
	{
		memory_errors(1);
		return;
	}
	info.r2i = r2i; info.w = mem_width; info.r = r; info.gcor = gcor;

	progress_init(_("Kuwahara-Nagao Filter"), 1);
	info.idx += r1;
	if (mem_width > 1) // All indices remain zero otherwise
	{
		k = mem_width + mem_width - 2;
		for (i = -(r + 1); i < mem_width + r; i++)
		{
			j = abs(i) % k;
			if (j >= mem_width) j = k - j;
			info.idx[i] = j * 3;
		}
	}

	mem_channel = CHN_IMAGE; // For row_protected()
	src = mem_undo_previous(CHN_IMAGE);
	/* Initialize the bottom sum */
	for (i = -r; i <= 0; i++)
		kuwahara_row(src + idx2row(i) * w, 0, TRUE, &info);
	kuwahara_min(0, &info);
	/* Initialize the rest of sums */
	for (i = 1; i <= r; i++)
	{
		int j = l * i;
		kuwahara_copy(j, j - l, &info);
		kuwahara_row(src + idx2row(i - r1) * w, j, FALSE, &info);
		kuwahara_row(src + idx2row(i) * w, j, TRUE, &info);
		kuwahara_min(j, &info);
	}
	/* Actually process image */
	ir = i = 0;
	while (TRUE)
	{
		int j, k, jp;

		if ((i * 10) % mem_height >= mem_height - 10)
			if (progress_update((float)(i + 1) / mem_height)) break;

		/* Process a pixel row */
		if (!detail) row_protected(0, i, mem_width, mask);
		tmp = buf = timg + wbuf * (i % 3);
		for (j = 0; j < mem_width; j++)
		{
			double dis;
			int jj, jk;

			tmp += 3;
			if (!detail && (mask[j] == 255)) continue;
			/* Select minimum variance square from covered rows */
			jj = j + l;
			jk = j + info.min[j];
			dis = kuwahara_square(jk, &info);
// !!! Only the all-or-nothing mode for now - weighted mode not implemented yet
			for (k = 1; k < r1; k++ , jj += l)
			{
				int jv = jj + info.min[jj];
				double dv = kuwahara_square(jv, &info);
				if (dv < dis) jk = jv , dis = dv;
			}
			/* Calculate & store new RGB */
			jk *= 3;
			if (gcor)
			{
				double *wr = info.rs + jk;
				tmp[0] = UNGAMMA256(wr[0] * r2i);
				tmp[1] = UNGAMMA256(wr[1] * r2i);
				tmp[2] = UNGAMMA256(wr[2] * r2i);
			}
			else
			{
				int *ar = info.avg + jk;
				tmp[0] = rint(*ar++ * r2i);
				tmp[1] = rint(*ar++ * r2i);
				tmp[2] = rint(*ar * r2i);
			}
		}

		if (detail)
		{
			/* Copy-extend the row on both ends */
			memcpy(buf, buf + 3, 3);
			memcpy(tmp + 3, tmp, 3);
			/* Copy-extend the top row */
			if (!i) memcpy(timg + wbuf * 2, buf, wbuf);
			/* Build and mask-merge the previous row */
			else kuwahara_detailed(timg, mask, i - 1, gcor);
		}
		else
		{
			/* Mask-merge current row */
			tmp = mem_img[CHN_IMAGE] + i * w;
			do_alpha_blend(tmp, buf + 3, tmp, mask, w, 3);
		}

		if (++i < mem_height)
		{
			/* Update sums for a new row */
			jp = ir * l;
			kuwahara_copy(jp, ((ir + r) % r1) * l, &info);
			kuwahara_row(src + idx2row(i - 1) * w, jp, FALSE, &info);
			kuwahara_row(src + idx2row(i + r) * w, jp, TRUE, &info);
			kuwahara_min(jp, &info);
			ir = (ir + 1) % r1;
			continue;
		}

		if (detail)
		{
			/* Copy-extend the bottom row */
			memcpy(timg + wbuf * (i % 3), buf, wbuf);
			/* Build and mask-merge it */
			kuwahara_detailed(timg, mask, i - 1, gcor);
		}
		break;
	}

	mem_channel = ch;

	progress_end();
	free(mem);
}

///	CLIPBOARD MASK

int mem_clip_mask_init(unsigned char val)		// Initialise the clipboard mask
{
	int j = mem_clip_w*mem_clip_h;

	if (mem_clipboard) mem_clip_mask_clear();	// Remove old mask

	mem_clip_mask = malloc(j);
	if (!mem_clip_mask) return 1;			// Not able to allocate memory

	memset(mem_clip_mask, val, j);		// Start with fully opaque/clear mask

	return 0;
}

void mem_mask_colors(unsigned char *mask, unsigned char *img, unsigned char v,
	int width, int height, int bpp, int col0, int col1)
{
	int i, j = width * height, k;

	if (bpp == 1)
	{
		for (i = 0; i < j; i++)
		{
			if ((img[i] == col0) || (img[i] == col1)) mask[i] = v;
		}
	}
	else
	{
		for (i = 0; i < j; i++ , img += 3)
		{
			k = MEM_2_INT(img, 0);
			if ((k == col0) || (k == col1)) mask[i] = v;
		}
	}
}

void mem_clip_mask_set(unsigned char val)		// (un)Mask colours A and B on the clipboard
{
	int aa, bb;

	if (mem_clip_bpp == 1) /* Indexed/utility */
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
	}
	else /* RGB */
	{
		aa = PNG_2_INT(mem_col_A24);
		bb = PNG_2_INT(mem_col_B24);
	}
	mem_mask_colors(mem_clip_mask, mem_clipboard, val,
		mem_clip_w, mem_clip_h, mem_clip_bpp, aa, bb);
}

void mem_clip_mask_clear()		// Clear/remove the clipboard mask
{
	free(mem_clip_mask);
	mem_clip_mask = NULL;
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
	unsigned char mask[256], img[256 * 2 * 3], alf[256 * 2];
	unsigned char *src, *dest, *srca = NULL, *dsta = NULL;
	int ax, ay, bx, by, w, h;
	int xv = nx - ox, yv = ny - oy;		// Vector
	int i, j, delta, delta1, bpp;
	int y0, y1, dy, opw, op2, cpf;


	if (!opacity) return;

	/* Clip source and dest areas to image bounds */
	ax = ox - tool_size / 2;
	bx = ax + tool_size;
	if (ax < 0) ax = 0;
	if (ax + xv < 0) ax = -xv;
	if (bx > mem_width) bx = mem_width;
	if (bx + xv > mem_width) bx = mem_width - xv;
	w = bx - ax;

	ay = oy - tool_size / 2;
	by = ay + tool_size;
	if (ay < 0) ay = 0;
	if (ay + yv < 0) ay = -yv;
	if (by > mem_height) by = mem_height;
	if (by + yv > mem_height) by = mem_height - yv;
	h = by - ay;

	if ((w < 1) || (h < 1)) return;

	if (IS_INDEXED) opacity = -1; // No mixing for indexed image

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

	/* Copy source if destination overwrites it */
	cpf = !yv && (xv > 0) && (w > xv); 
	/* Set up Y pass to prevent overwriting source */
	if ((yv > 0) && (h > yv))
		y0 = ay + h - 1 , y1 = ay - 1 , dy = -1; // Bottom to top
	else y0 = ay , y1 = ay + h , dy = 1; // Top to bottom

	for (j = y0; j != y1; j += dy)	// Blend old area with new area
	{
		unsigned char *ts, *td, *tsa = NULL, *tda = NULL;
		int offs = j * mem_width + ax;

		row_protected(ax + xv, j + yv, w, mask);
		ts = src + offs * bpp;
		td = dest + offs * bpp + delta;
		if (cpf)
		{
			memcpy(img, ts, w * bpp + delta);
			ts = img;
		}
		if (dsta)
		{
			tsa = srca + offs;
			tda = dsta + offs + delta1;
			if (cpf)
			{
				memcpy(alf, tsa, w + delta1);
				tsa = alf;
			}
		}

		for (i = 0; i < w; i++ , ts += bpp , td += bpp)
		{
			int k = mask[i], k0, k1, k2;

			if (opacity < 0)
			{
				if (k) continue;
				*td = *ts;
				if (tda) tda[i] = tsa[i];
				continue;
			}
			opw = (255 - k) * opacity;
			opw = (opw + (opw >> 8) + 1) >> 8;
			if (!opw) continue;
			if (tda)
			{
				int k = tsa[i + delta1];
				k = k * 255 + (tsa[i] - k) * opw + 127;
				tda[i] = (k + (k >> 8) + 1) >> 8;
				if (k && !channel_dis[CHN_ALPHA])
					opw = (255 * opw * tsa[i]) / k;
			}
			op2 = 255 - opw;
			k0 = ts[0] * opw + ts[delta] * op2 + 127;
			td[0] = (k0 + (k0 >> 8) + 1) >> 8;
			if (bpp == 1) continue;
			k1 = ts[1] * opw + ts[delta + 1] * op2 + 127;
			td[1] = (k1 + (k1 >> 8) + 1) >> 8;
			k2 = ts[2] * opw + ts[delta + 2] * op2 + 127;
			td[2] = (k2 + (k2 >> 8) + 1) >> 8;
		}
	}
}

///	GRADIENTS

/* Evaluate channel gradient at coordinate, return opacity
 * Coordinate 0 is center of 1st pixel, 1 center of last
 * Scale of return values is 0x0000..0xFF00 (NOT 0xFFFF) */
int grad_value(int *dest, int slot, double x)
{
	int i, k, len, op;
	unsigned char *gdata, *gmap;
	grad_map *gradmap;
	double xx, hsv[6];

	/* Gradient slot (first RGB, then 1-bpp channels) */
	gradmap = graddata + slot;

	/* Get opacity */
	gdata = gradmap->op; gmap = gradmap->opmap; len = gradmap->oplen;
	xx = (gradmap->orev ? 1.0 - x : x) * (len - 1);
	i = xx;
	if (i > len - 2) i = len - 2;
	k = gmap[i] == GRAD_TYPE_CONST ? 0 : (int)((xx - i) * 0x10000 + 0.5);
	op = (gdata[i] << 8) + ((k * (gdata[i + 1] - gdata[i]) + 127) >> 8);
	if (!op) return (0); /* Stop if zero opacity */

	/* Get channel value */
	gdata = gradmap->vs; gmap = gradmap->vsmap; len = gradmap->vslen;
	xx = (gradmap->grev ? 1.0 - x : x) * (len - 1);
	i = xx;
	if (i > len - 2) i = len - 2;
	k = gmap[i] == GRAD_TYPE_CONST ? 0 : (int)((xx - i) * 0x10000 + 0.5);
	if (!slot) /* RGB */
	{
		unsigned char *gslot = gdata + i * 3;
		int j3 = 0;

		switch (gmap[i])
		{
		case GRAD_TYPE_BK_HSV: /* Backward HSV interpolation */
			j3 = 3;
		case GRAD_TYPE_HSV: /* HSV interpolation */
			/* Convert */
			rgb2hsv(gslot + 0, hsv + 0);
			rgb2hsv(gslot + 3, hsv + 3);
			/* Grey has no hue */
			if (hsv[1] == 0.0) hsv[0] = hsv[3];
			if (hsv[4] == 0.0) hsv[3] = hsv[0];
			/* Prevent wraparound */
			if (hsv[j3] > hsv[j3 ^ 3]) hsv[j3] -= 6.0;
			/* Interpolate */
			hsv[0] += (xx - i) * (hsv[3] - hsv[0]);
			hsv[1] += (xx - i) * (hsv[4] - hsv[1]);
			hsv[2] += (xx - i) * (hsv[5] - hsv[2]);
			/* Convert back */
			hsv[2] *= 512;
			hsv[1] = hsv[2] * (1.0 - hsv[1]);
			if (hsv[0] < 0.0) hsv[0] += 6.0;
			j3 = hsv[0];
			hsv[0] = (hsv[0] - j3) * (hsv[2] - hsv[1]);
			if (j3 & 1) { hsv[2] -= hsv[0]; hsv[0] += hsv[2]; }
			else hsv[0] += hsv[1];
			j3 >>= 1;
			dest[j3] = ((int)hsv[2] + 1) >> 1;
			dest[MOD3(j3 + 1)] = ((int)hsv[0] + 1) >> 1;
			dest[MOD3(j3 + 2)] = ((int)hsv[1] + 1) >> 1;
			break;
		case GRAD_TYPE_SRGB: /* sRGB interpolation */
			dest[0] = ungamma65281(gamma256[gslot[0]] + (xx - i) *
				(gamma256[gslot[3]] - gamma256[gslot[0]]));
			dest[1] = ungamma65281(gamma256[gslot[1]] + (xx - i) *
				(gamma256[gslot[4]] - gamma256[gslot[1]]));
			dest[2] = ungamma65281(gamma256[gslot[2]] + (xx - i) *
				(gamma256[gslot[5]] - gamma256[gslot[2]]));
			break;
		default: /* RGB interpolation */
			dest[0] = (gslot[0] << 8) +
				((k * (gslot[3] - gslot[0]) + 127) >> 8);
			dest[1] = (gslot[1] << 8) +
				((k * (gslot[4] - gslot[1]) + 127) >> 8);
			dest[2] = (gslot[2] << 8) +
				((k * (gslot[5] - gslot[2]) + 127) >> 8);
			break;
		}
	}
	else if (slot == CHN_IMAGE + 1) /* Indexed */
	{
		dest[0] = gdata[i];
		dest[1] = gdata[i + ((k + 0xFFFF) >> 16)];
		dest[CHN_IMAGE + 3] = (k + 127) >> 8;
	}
	else /* Utility */
	{
		dest[slot + 2] = (gdata[i] << 8) +
			((k * (gdata[i + 1] - gdata[i]) + 127) >> 8);
	}

	return (op);
}

/* Evaluate (coupled) alpha gradient at coordinate */
static void grad_alpha(int *dest, double x)
{
	int i, k, len;
	unsigned char *gdata, *gmap;
	grad_map *gradmap;
	double xx;

	/* Get coupled alpha */
	gradmap = graddata + CHN_ALPHA + 1;
	gdata = gradmap->vs; gmap = gradmap->vsmap; len = gradmap->vslen;
	xx = (gradmap->grev ? 1.0 - x : x) * (len - 1);
	i = xx;
	if (i > len - 2) i = len - 2;
	k = gmap[i] == GRAD_TYPE_CONST ? 0 : (int)((xx - i) * 0x10000 + 0.5);
	dest[CHN_ALPHA + 3] = (gdata[i] << 8) +
		((k * (gdata[i + 1] - gdata[i]) + 127) >> 8);
}

/* Evaluate gradient at a sequence of points */
/* !!! For now, works only in (slower) exact mode */
void grad_pixels(int start, int step, int cnt, int x, int y, unsigned char *mask,
	unsigned char *op0, unsigned char *img0, unsigned char *alpha0)
{
	grad_info *grad = gradient + mem_channel;
	unsigned char *dest;
	int i, mmask, dither, op, slot, wrk[NUM_CHANNELS + 3];
	double dist, len1, l2;
	

	if (!RGBA_mode) alpha0 = NULL;
	mmask = IS_INDEXED ? 1 : 255; /* On/off opacity */
	slot = mem_channel + ((0x81 + mem_channel + mem_channel - mem_img_bpp) >> 7);

	cnt = start + step * cnt; x += start;
	for (i = start; i < cnt; op0[i] = op , x += step , i += step)
	{
		op = 0;
		if (mask[i] >= mmask) continue;

		/* Disabled because of unusable settings? */
		if (grad->wmode == GRAD_MODE_NONE) continue;

		/* Distance for gradient mode */
		if (grad->status == GRAD_NONE)
		{
			/* Stroke gradient */
			if (grad->wmode != GRAD_MODE_BURST) dist = grad_path +
				(x - grad_x0) * grad->xv + (y - grad_y0) * grad->yv;
			/* Shapeburst gradient */
			else
			{
				int n = sb_buf[(y - sb_rect[1]) * sb_rect[2] +
					(x - sb_rect[0])] - 1;
				if (n < 0) continue;
				dist = n;
			}
		}
		else
		{
			int dx = x - grad->xy[0], dy = y - grad->xy[1];

			switch (grad->wmode)
			{
			default:
			case GRAD_MODE_LINEAR:	/* Linear/bilinear gradient */
			case GRAD_MODE_BILINEAR:
				dist = dx * grad->xv + dy * grad->yv;
				if (grad->wmode == GRAD_MODE_LINEAR) break;
				dist = fabs(dist); /* Bilinear */
				break;
			case GRAD_MODE_RADIAL:	/* Radial gradient */
				dist = sqrt(dx * dx + dy * dy);
				break;
			case GRAD_MODE_SQUARE:	/* Square gradient */
				/* !!! Here is code duplication with linear/
				 * bilinear path - but merged paths actually
				 * LOSE in both time and space, at least
				 * with GCC - WJ */
				dist = fabs(dx * grad->xv + dy * grad->yv) +
					fabs(dx * grad->yv - dy * grad->xv);
				break;
			case GRAD_MODE_ANGULAR:	/* Angular/conical gradient */
			case GRAD_MODE_CONICAL:
				dist = atan360(dx, dy) - grad->wa;
				if (dist < 0.0) dist += 360.0;
				if (grad->wmode == GRAD_MODE_ANGULAR) break;
				if (dist >= 180.0) dist = 360.0 - dist;
				break;
			}
		}
		dist -= grad->ofs;

		/* Apply repeat mode */
		len1 = grad->wrep;
		switch (grad->wrmode)
		{
		case GRAD_BOUND_MIRROR: /* Mirror repeat */
			l2 = len1 + len1;
			dist -= l2 * (int)(dist * grad->wil2);
			if (dist < 0.0) dist += l2;
			if (dist > len1) dist = l2 - dist;
			break;
		case GRAD_BOUND_REPEAT: /* Repeat */
			l2 = len1 + 1.0; /* Repeat period is 1 pixel longer */
			dist -= l2 * (int)((dist + 0.5) * grad->wil2);
			if (dist < -0.5) dist += l2;
			break;
		case GRAD_BOUND_REP_A: /* Angular repeat */
			dist -= len1 * (int)(dist * grad->wil2);
			if (dist < 0.0) dist += len1;
			break;
		case GRAD_BOUND_STOP: /* Nothing is outside bounds */
			if ((dist < -0.5) || (dist >= len1 + 0.5)) continue;
			break;
		case GRAD_BOUND_STOP_A: /* Nothing is outside angle */
			if ((dist < 0.0) || (dist > len1)) continue;
			break;
		case GRAD_BOUND_LEVEL: /* Constant extension */
		default:
			break;
		}

		/* Rescale to 0..1, enforce boundaries */
		dist = dist <= 0.0 ? 0.0 : dist >= len1 ? 1.0 : dist * grad->wil1;

		/* Value from Bayer dither matrix */
		dither = BAYER(x, y);

		/* Get gradient */
		wrk[CHN_IMAGE + 3] = 0;
		op = (grad_value(wrk, slot, dist) + dither) >> 8;
		if (!op) continue;

		if (mem_channel == CHN_IMAGE)
		{
			if (alpha0)
			{
				grad_alpha(wrk, dist);
				alpha0[i] = (wrk[CHN_ALPHA + 3] + dither) >> 8;
			}
			if (mem_img_bpp == 3)
			{
				dest = img0 + i * 3;
				dest[0] = (wrk[0] + dither) >> 8;
				dest[1] = (wrk[1] + dither) >> 8;
				dest[2] = (wrk[2] + dither) >> 8;
			}
			else
			{
				img0[i] = (unsigned char)wrk[(wrk[CHN_IMAGE + 3] +
					dither) >> 8];
				op = 255;
			}
		}
		else img0[i] = (wrk[mem_channel + 3] + dither) >> 8;
	}
}

/* Reevaluate gradient placement functions */
void grad_update(grad_info *grad)
{
	double len, len1, l2;
	int dx = grad->xy[2] - grad->xy[0], dy = grad->xy[3] - grad->xy[1];

	/* Distance for gradient mode */
	grad->wmode = grad->gmode;
	len = grad->len;
	while (1)
	{
		 /* Stroke gradient */
		if (grad->status == GRAD_NONE)
		{
			if (!grad->len) len = grad->rep + grad->ofs;
			if (len <= 0.0) grad->wmode = GRAD_MODE_NONE;
			break;
		}

		/* Placement length */
		l2 = sqrt(dx * dx + dy * dy);
		if (l2 == 0.0)
		{
			grad->wmode = GRAD_MODE_RADIAL;
			break;
		}
		grad->xv = dx / l2;
		grad->yv = dy / l2;
		grad->wa = atan360(dx, dy);
		if (!grad->len) len = grad->wmode == GRAD_MODE_ANGULAR ? 360.0 :
			grad->wmode == GRAD_MODE_CONICAL ? 180.0 : l2;
		break;
	}

	/* Base length (one repeat) */
	len1 = grad->rep > 0 ? grad->rep : len - grad->ofs;
	if (len1 < 1.0) len1 = 1.0;
	grad->wrep = len1;
	grad->wil1 = 1.0 / len1;

	/* Inverse period */
	l2 = 1.0;
	grad->wrmode = grad->rmode;
	if (grad->rmode == GRAD_BOUND_MIRROR) /* Mirror repeat */
		l2 = len1 + len1;
	else if (grad->rmode == GRAD_BOUND_REPEAT) /* Repeat */
		l2 = len1 + 1.0;
	/* Angular distance is in degrees, not pixels */
	if ((grad->wmode == GRAD_MODE_ANGULAR) ||
		(grad->wmode == GRAD_MODE_CONICAL))
	{
		if (grad->rmode == GRAD_BOUND_REPEAT)
			grad->wrmode = GRAD_BOUND_REP_A , l2 = len1;
		else if (grad->rmode == GRAD_BOUND_STOP)
			grad->wrmode = GRAD_BOUND_STOP_A;
	}
	grad->wil2 = 1.0 / l2;
}

static unsigned char grad_def[4 + 8 + NUM_CHANNELS * 4];

/* Setup gradient mapping */
void gmap_setup(grad_map *gmap, grad_store gstore, int slot)
{
	unsigned char *data, *map;

	data = grad_def + (slot ? 8 + slot * 4 : 4);
	map = grad_def + 10 + slot * 4;
	gmap->vslen = 2;
	if (gmap->gtype == GRAD_TYPE_CUSTOM)
	{
		gmap->vs = gstore + GRAD_CUSTOM_DATA(slot);
		gmap->vsmap = gstore + GRAD_CUSTOM_DMAP(slot);
		if (gmap->cvslen > 1) gmap->vslen = gmap->cvslen;
		else
		{
			memcpy(gmap->vs, data, slot ? 2 : 6);
			gmap->vsmap[0] = map[0];
		}
	}
	else
	{
		gmap->vs = data;
		gmap->vsmap = map;
		grad_def[10 + slot * 4] = (unsigned char)gmap->gtype;
	}

	gmap->oplen = 2;
	if (gmap->otype == GRAD_TYPE_CUSTOM)
	{
		gmap->op = gstore + GRAD_CUSTOM_OPAC(slot);
		gmap->opmap = gstore + GRAD_CUSTOM_OMAP(slot);
		if (gmap->coplen > 1) gmap->oplen = gmap->coplen;
		else
		{
			gmap->op[0] = grad_def[0];
			gmap->op[1] = grad_def[1];
			gmap->opmap[0] = grad_def[2];
		}
	}
	else
	{
		gmap->op = grad_def;
		gmap->opmap = grad_def + 2;
		grad_def[2] = gmap->otype;
	}
}

/* Store default gradient */
void grad_def_update(int slot)
{
	grad_map *gradmap;

	/* Gradient slot (first RGB, then 1-bpp channels) */
	if (slot < 0) slot = mem_channel + ((0x81 + mem_channel + mem_channel -
		mem_img_bpp) >> 7);
	gradmap = graddata + slot;

	grad_def[0] = tool_opacity;
	/* !!! As there's only 1 tool_opacity, use 0 for 2nd point */ 
	grad_def[1] = 0;
	grad_def[2] = gradmap->otype;

	grad_def[10 + slot * 4] = gradmap->gtype;
	if (slot)
	{
		grad_def[8 + slot * 4] = channel_col_A[slot - 1];
		grad_def[9 + slot * 4] = channel_col_B[slot - 1];
		grad_def[12] = mem_col_A;
		grad_def[13] = mem_col_B;
	}
	else
	{
		grad_def[4] = mem_col_A24.red;
		grad_def[5] = mem_col_A24.green;
		grad_def[6] = mem_col_A24.blue;
		grad_def[7] = mem_col_B24.red;
		grad_def[8] = mem_col_B24.green;
		grad_def[9] = mem_col_B24.blue;
	}

	gradmap = graddata + CHN_ALPHA + 1;
	grad_def[12 + CHN_ALPHA * 4] = channel_col_A[CHN_ALPHA];
	grad_def[13 + CHN_ALPHA * 4] = channel_col_B[CHN_ALPHA];
	grad_def[14 + CHN_ALPHA * 4] = gradmap->gtype;
}

/* Convert to RGB & blend indexed/indexed+alpha for preview */
void blend_indexed(int start, int step, int cnt, unsigned char *rgb,
	unsigned char *img0, unsigned char *img,
	unsigned char *alpha0, unsigned char *alpha, int opacity)
{
	png_color *col, *col0;
	int i, j, k, i3;

	cnt = start + step * cnt;
	for (i = start; i < cnt; i += step)
	{
		j = opacity;
		if (alpha)
		{
			if (alpha[i])
			{
				if (alpha0[i]) /* Opaque both */
					alpha[i] = 255;
				else /* Opaque new */
				{
					alpha[i] = opacity;
					j = 255;
				}
			}
			else if (alpha0[i]) /* Opaque old */
			{
				alpha[i] = opacity ^ 255;
				j = 0;
			}
			else /* Transparent both */
			{
				alpha[i] = 0;
				continue;
			}
		}
		col = mem_pal + img[i];
		col0 = mem_pal + img0[i];
		i3 = i * 3;
		k = col0->red * 255 + j * (col->red - col0->red);
		rgb[i3 + 0] = (k + (k >> 8) + 1) >> 8;
		k = col0->green * 255 + j * (col->green - col0->green);
		rgb[i3 + 1] = (k + (k >> 8) + 1) >> 8;
		k = col0->blue * 255 + j * (col->blue - col0->blue);
		rgb[i3 + 2] = (k + (k >> 8) + 1) >> 8;
	}
}

static void grad_point(double *xyz, int cspace, int idx)
{
	int wrk[NUM_CHANNELS + 3];

	grad_value(wrk, 0, idx * (1.0 / 4096.0));
	switch (cspace)
	{
	default:
	case CSPACE_RGB:
		xyz[0] = wrk[0] * (1.0 / 256.0);
		xyz[1] = wrk[1] * (1.0 / 256.0);
		xyz[2] = wrk[2] * (1.0 / 256.0);
		break;
	case CSPACE_LXN:
	case CSPACE_SRGB:
		xyz[0] = gamma65281(wrk[0]);
		xyz[1] = gamma65281(wrk[1]);
		xyz[2] = gamma65281(wrk[2]);
		if (cspace == CSPACE_LXN) rgb2LXN(xyz, xyz[0], xyz[1], xyz[2]);
		break;
	}
}

int mem_pick_gradient(unsigned char *buf, int cspace, int mode)
{
	grad_map oldgr;
	double pal[256 * 3], near[256 * 3], dist[256], len[256], lastc[3];
	unsigned char *tb = buf;
	int i, j, k, l;

	/* Set up new RGB gradient */
	oldgr = graddata[0];
	memset(graddata, 0, sizeof(grad_map));
	graddata[0].gtype = mode;
	graddata[0].otype = GRAD_TYPE_CONST;
	graddata[0].grev = graddata[0].orev = FALSE;
	grad_def_update(0);
	gmap_setup(graddata, gradbytes, 0);

	/* Map palette to colorspace, and init point/distance/position */
	grad_point(lastc, cspace, 0);
	for (i = 0; i < mem_cols; i++)
	{
		double *tmp = pal + i * 3;

		switch (cspace)
		{
		default:
		case CSPACE_RGB:
			tmp[0] = mem_pal[i].red;
			tmp[1] = mem_pal[i].green;
			tmp[2] = mem_pal[i].blue;
			break;
		case CSPACE_SRGB:
			tmp[0] = gamma256[mem_pal[i].red];
			tmp[1] = gamma256[mem_pal[i].green];
			tmp[2] = gamma256[mem_pal[i].blue];
			break;
		case CSPACE_LXN:
			get_lxn(tmp, PNG_2_INT(mem_pal[i]));
			break;
		}
		dist[i] = (tmp[0] - lastc[0]) * (tmp[0] - lastc[0]) +
			(tmp[1] - lastc[1]) * (tmp[1] - lastc[1]) +
			(tmp[2] - lastc[2]) * (tmp[2] - lastc[2]);
		memcpy(near + i * 3, lastc, sizeof(lastc));
	}
	memset(len, 0, sizeof(len));

	/* Find nearest point on gradient curve for each palette color */
	for (i = 1; i < 4096; i++)
	{
		double thisc[3], dx, dy, dz, l2;

		grad_point(thisc, cspace, i);
		dx = thisc[0] - lastc[0];
		dy = thisc[1] - lastc[1];
		dz = thisc[2] - lastc[2];
		l2 = dx * dx + dy * dy + dz * dz;
		if (l2 == 0.0) continue;
		for (j = 0; j < mem_cols; j++)
		{
			double a, d, newc[3], *tmp = pal + j * 3;
			a = ((tmp[0] - lastc[0]) * dx + (tmp[1] - lastc[1]) * dy +
				(tmp[2] - lastc[2]) * dz) / l2;
			a = a < 0.0 ? 0.0 : a > 1.0 ? 1.0 : a;
			newc[0] = lastc[0] + a * dx;
			newc[1] = lastc[1] + a * dy;
			newc[2] = lastc[2] + a * dz;
			d = (tmp[0] - newc[0]) * (tmp[0] - newc[0]) +
				(tmp[1] - newc[1]) * (tmp[1] - newc[1]) +
				(tmp[2] - newc[2]) * (tmp[2] - newc[2]);
			if (d >= dist[j]) continue;
			dist[j] = d; // Distance from the curve
			len[j] = a + i; // Position along the curve
			memcpy(near + j * 3, newc, sizeof(newc)); // Point
		}
		memcpy(lastc, thisc, sizeof(thisc));
	}

	/* Include gradient's second end */
	grad_point(lastc, cspace, 4096);
	for (i = 0; i < mem_cols; i++)
	{
		double d, *tmp = pal + i * 3;
		d = (tmp[0] - lastc[0]) * (tmp[0] - lastc[0]) +
			(tmp[1] - lastc[1]) * (tmp[1] - lastc[1]) +
			(tmp[2] - lastc[2]) * (tmp[2] - lastc[2]);
		if (d >= dist[i]) continue;
		dist[i] = d;
		len[i] = 4096.0;
		memcpy(near + i * 3, lastc, sizeof(lastc));
	}

	/* Restore old RGB gradient */
	graddata[0] = oldgr;
	grad_def_update(-1);
	gmap_setup(graddata, gradbytes, 0);

	/* Pick colors with *uncontested* nearest points */
	scan_duplicates(); // Need to avoid duplicated colors
	for (i = 0; i < mem_cols; i++)
	{
		double d, d0, *tmp, *xyz;

		if (pal_dupes[i] != i) continue;
		*tb++ = i; // Add to result set by default
		d0 = dist[i];
		xyz = near + i * 3;
		for (j = 0 , tmp = pal; j < mem_cols; j++ , tmp += 3)
		{
			if (pal_dupes[j] == i) continue;
			tmp = pal + j * 3;
			d = (tmp[0] - xyz[0]) * (tmp[0] - xyz[0]) +
				(tmp[1] - xyz[1]) * (tmp[1] - xyz[1]) +
				(tmp[2] - xyz[2]) * (tmp[2] - xyz[2]);
			if (d <= d0)
			{
				tb--; // Fail - remove this color
				break;
			}
		}
	}

	/* Bubble-sort the result set by position */
	l = tb - buf;
	for (i = l - 1; i > 0; i--)
	for (j = 0; j < i; j++)
	{
		k = buf[j + 1];
		if (len[buf[j]] > len[k])
		{
			buf[j + 1] = buf[j];
			buf[j] = k;
		}
	}

	/* Return number of colors in result set */
	return (l);
}

///	SKEW ENGINE

#define FILT_MAX 6 /* Must be no less than largest filter width */

static void *make_skew_filter(double **filt, int **dcc, int *fw,
	int len, double shift, double skew, int type)
{
	void *tmp;
	double x, y, x0, dy, fw2, sum, A = 0.0, *fdata;
	int i, j, k, y0, fwidth = 0, *ofdata;

	// Use NN "filter" for integer shifts
	if ((fabs(skew - rint(skew)) < 1e-10) &&
		(fabs(shift - rint(shift)) < 1e-10)) type = 0;

	switch (type)
	{
	case 0: fwidth = 1; /* Nearest neighbor */
		break;
	case 1:	fwidth = 2; /* Bilinear */
		break;
	case 2:	case 3: case 4: case 5:	/* Bicubic, all flavors */
		fwidth = 4;
		A = Aarray[type - 2];
		break;
	case 6:	fwidth = 6; /* Blackman-Harris windowed sinc */
		break;
	}

	*filt = NULL; *dcc = NULL; *fw = fwidth;
	tmp = multialloc(MA_ALIGN_DOUBLE, filt, len * fwidth * sizeof(double),
		dcc, len * sizeof(int), NULL);
	if (!tmp) return (NULL);
	fdata = *filt; ofdata = *dcc;

	/* Generate filter */
	fw2 = fwidth >> 1;
	x0 = 0.5 * (len - 1);
	for (i = 0; i < len; i++)
	{
		/* As mapping is dest-to-src, shifts are negative */
		dy = (x0 - i) * skew - shift;
		/* Specialcase NN filter, for simplicity */
		if (!type)
		{
			WJ_FLOOR(*ofdata++, dy + 0.5);
			*fdata++ = 1.0;
			continue;
		}
		/* Build regular filters*/
		dy -= (*ofdata++ = y0 = ceil(dy - fw2)); // Mirrored offset
		sum = 0.0;
		for (j = 0; j < fwidth; j++ , dy -= 1.0)
		{
			x = fabs(dy);
			y = 0;
			switch (type)
			{
			case 1: /* Bilinear */
				y = 1.0 - x;
				break;
			case 2: case 3: case 4: case 5: /* Bicubic */
				if (x < 1.0) y = ((A + 2.0) * x - (A + 3)) * x * x + 1.0;
				else y = A * (((x - 5.0) * x + 8.0) * x - 4.0);
				break;
			case 6: /* Blackman-Harris */
				y = BH1(x);
				break;
			}
			sum += (fdata[j] = y);
		}
		/* Normalization pass */
		sum = 1.0 / sum;
		for (k = 0; k < fwidth; k++) *fdata++ *= sum;
	}
	return (tmp);
}

static void skew_fill_rgba(double *buf, double *filler,
	unsigned char *src, unsigned char *srca,
	int y0, int ow, int xl, int xr, int x0l, int x0r,
	int xfsz, double *xfilt, int *dxx, int *dyy, int gcor)
{
	double *dest, *tmp;
	int j, k, l;

	/* Initialize dest buffer */
	k = x0l < xl ? x0l : xl;
	l = (x0r > xr ? x0r : xr) - k - 1;
	if (l < 0) return; // Nothing to do
	tmp = buf + k * 7;
	memcpy(tmp, filler, sizeof(double) * 7);
	for (tmp += 7 , l *= 7; l > 0; tmp++ , l--) *tmp = *(tmp - 7);

	/* Collect pixels */
	dest = buf + xl * 7;
	for (j = xl; j < xr; j++ , dest += 7)
	{
		unsigned char *img, *alpha;
		double *filt, acc = 0.0;
		int x, y, x1, ofs;

		/* Get location */
		y = y0 + dyy[j];
		x = j + dxx[y];
		x1 = x + xfsz;
		filt = xfilt - x + y * xfsz;
		
		/* Accumulate empty space */
		while (x1 > ow) acc += filt[--x1];
		while (x < 0) acc += filt[x++];

		/* Setup source & dest */
		ofs = y * ow + x;
		img = src + ofs * 3;
		alpha = srca + ofs;
// !!! Maybe use temp vars for accumulators - but will it make a difference?
		dest[0] *= acc;
		dest[1] *= acc;
		dest[2] *= acc;

		/* Accumulate image data */
		filt += x;
		for (; x < x1; x++ , img += 3)
		{
			double rr, gg, bb, aa, fv;

			fv = *filt++;
			if (gcor)
			{
				rr = gamma256[img[0]] * fv;
				gg = gamma256[img[1]] * fv;
				bb = gamma256[img[2]] * fv;
			}
			else
			{
				rr = img[0] * fv;
				gg = img[1] * fv;
				bb = img[2] * fv;
			}
			dest[6] += (aa = *alpha++) * fv;
			dest[0] += rr;
			dest[1] += gg;
			dest[2] += bb;
			dest[3] += rr * aa;
			dest[4] += gg * aa;
			dest[5] += bb * aa;
		}
	}
}

static void skew_fill_rgb(double *buf, double *filler,
	unsigned char *src, unsigned char *srca,
	int y0, int ow, int xl, int xr, int x0l, int x0r,
	int xfsz, double *xfilt, int *dxx, int *dyy, int gcor)
{
	double *dest, *tmp;
	int j, k, l;

	/* Initialize dest buffer */
	k = x0l < xl ? x0l : xl;
	l = (x0r > xr ? x0r : xr) - k - 1;
	if (l < 0) return; // Nothing to do
	tmp = buf + k * 3;
	memcpy(tmp, filler, sizeof(double) * 3);
	for (tmp += 3 , l *= 3; l > 0; tmp++ , l--) *tmp = *(tmp - 3);

	/* Collect pixels */
	dest = buf + xl * 3;
	for (j = xl; j < xr; j++ , dest += 3)
	{
		unsigned char *img;
		double *filt, acc = 0.0;
		double rv, gv, bv;
		int x, y, x1;

		/* Get location */
		y = y0 + dyy[j];
		x = j + dxx[y];
		x1 = x + xfsz;
		filt = xfilt - x + y * xfsz;
		
		/* Accumulate empty space */
		while (x1 > ow) acc += filt[--x1];
		while (x < 0) acc += filt[x++];

		/* Setup source & dest */
		img = src + (y * ow + x) * 3;
		rv = dest[0] * acc;
		gv = dest[1] * acc;
		bv = dest[2] * acc;

		/* Accumulate image data */
		filt += x;
		for (; x < x1; x++ , img += 3)
		{
			double fv = *filt++;
			if (gcor)
			{
				rv += gamma256[img[0]] * fv;
				gv += gamma256[img[1]] * fv;
				bv += gamma256[img[2]] * fv;
			}
			else
			{
				rv += img[0] * fv;
				gv += img[1] * fv;
				bv += img[2] * fv;
			}
		}
		dest[0] = rv;
		dest[1] = gv;
		dest[2] = bv;
	}
}

static void skew_fill_util(double *buf, double *filler,
	unsigned char *src, unsigned char *srca,
	int y0, int ow, int xl, int xr, int x0l, int x0r,
	int xfsz, double *xfilt, int *dxx, int *dyy, int gcor)
{
	double *dest;
	int j, k, l;

	/* Initialize dest buffer */
	k = x0l < xl ? x0l : xl;
	l = (x0r > xr ? x0r : xr) - k;
	if (l <= 0) return; // Nothing to do
	memset(buf + k, 0, l * sizeof(double));

	/* Collect pixels */
	dest = buf + xl * 3;
	for (j = xl; j < xr; j++)
	{
		unsigned char *img;
		double *filt, sum;
		int x, y, x1;

		/* Get location */
		y = y0 + dyy[j];
		x = j + dxx[y];
		x1 = x + xfsz;
		filt = xfilt - x + y * xfsz;
		
		/* Skip empty space */
		while (x1 > ow) x1--;
		while (x < 0) x++;

		/* Setup source */
		img = src + y * ow + x;

		/* Accumulate image data */
		filt += x; sum = 0.0;
		for (; x < x1; x++) sum += *img++ * *filt++;
		*dest++ = sum;
	}
}

/* !!! This works, after a fashion - but remains 2.5 times slower than a smooth
 * free-rotate if using 6-tap filter, or 1.5 times if using 2-tap one. Which,
 * while still being several times faster than anything else, is rather bad
 * for a high-quality tool like mtPaint. Needs improvement. - WJ */
static void mem_skew_filt(chanlist old_img, chanlist new_img, int ow, int oh,
	int nw, int nh, double xskew, double yskew, int mode, int gcor,
	int dis_a, int silent)
{
	void *xmem, *ymem, *tmem;
	double *xfilt, *yfilt, *wbuf, *rbuf;
	int *dxx, *dyy;
	double x0, y0, d, Kh, Kv, XX[4], YY[4], filler[7];
	int i, cc, fw2, fh2, xfsz, yfsz, wbsz, rgba, step, ny, nr;


	/* Create temp data */
	step = (rgba = new_img[CHN_ALPHA] && !dis_a) ? 7 : 3;
	xmem = make_skew_filter(&xfilt, &dxx, &xfsz, oh, (nw - ow) * 0.5, xskew, mode);
	ymem = make_skew_filter(&yfilt, &dyy, &yfsz, nw, (nh - oh) * 0.5, yskew, mode);
	fw2 = xfsz >> 1; fh2 = yfsz >> 1;

	wbsz = nw * step;
	tmem = multialloc(MA_ALIGN_DOUBLE, &wbuf, wbsz * yfsz * sizeof(double),
		&rbuf, wbsz * sizeof(double), NULL);
	if (!xmem || !ymem || !tmem) goto fail;
	x0 = 0.5 * (nw - 1); y0 = 0.5 * (nh - 1);

	/* Calculate clipping parallelogram's corners */
	// To avoid corner cases, we add an extra pixel to original dimensions
	XX[1] = XX[3] = (XX[0] = XX[2] = 0.5 * (nw - ow) - 1) + ow + 1;
	YY[2] = YY[3] = (YY[0] = YY[1] = 0.5 * (nh - oh) - 1) + oh + 1;
	for (i = 0; i < 4; i++)
	{
		XX[i] += (YY[i] - y0) * xskew;
		YY[i] += (XX[i] - x0) * yskew;
	}
	d = 1.0 + xskew * yskew;
	Kv = d ? xskew / d : 0.0; // for left & right
	Kh = yskew ? 1.0 / yskew : 0.0; // for top & bottom

	/* Init filler */
	memset(filler, 0, sizeof(filler));
	if (gcor)
	{
		filler[0] = gamma256[mem_col_A24.red];
		filler[1] = gamma256[mem_col_A24.green];
		filler[2] = gamma256[mem_col_A24.blue];
	}
	else
	{
		filler[0] = mem_col_A24.red;
		filler[1] = mem_col_A24.green;
		filler[2] = mem_col_A24.blue;
	}

	/* Process image channels */
	for (nr = cc = 0; cc < NUM_CHANNELS; cc++) nr += !!new_img[cc];
	nr = (nr - rgba) * (nh + yfsz - 1);
	for (ny = cc = 0; cc < NUM_CHANNELS; cc++)
	{
		int ring_l[FILT_MAX], ring_r[FILT_MAX];
		int i, idx, bpp = cc == CHN_IMAGE ? step : 1;

		if (!new_img[cc]) continue;
		/* Alpha already processed for RGBA */
		if ((cc == CHN_ALPHA) && rgba) continue;

		/* Init border rings to all-filled */
		for (i = 0; i < yfsz; i++) ring_l[i] = 0 , ring_r[i] = nw;

		/* Row loop */
		for (i = 1 - yfsz , idx = 0; i < nh; i++ , ++idx >= yfsz ? idx = 0 : 0)
		{
			double *filt0, *thatbuf, *thisbuf = wbuf + idx * wbsz;
			int j, k, y0, xl, xr, len, ofs, lfx = -xfsz;

			if (!silent && ((++ny * 10) % nr >= nr - 10))
				progress_update((float)ny / nr);

			/* Locate source row */
			y0 = i + yfsz - 1; // Effective Y offset

			/* !!! A reliable equation for pixel-precise clipping
			 * of source rows stubbornly refuses to be found, so
			 * a brute-force approach is used instead - WJ */
			xl = 0; xr = nw;
			for (; xl < xr; xl++) // Skip empty pixels on the left
			{
				int j = y0 + dyy[xl];
				if ((j < 0) || (j >= oh)) continue;
				j = xl + dxx[j];
				if ((j <= lfx) || (j >= ow)) continue;
				break;
			}
			for (; xl < xr; xr--) // Same on the right
			{
				int j = y0 + dyy[xr - 1];
				if ((j < 0) || (j >= oh)) continue;
				j = xr - 1 + dxx[j];
				if ((j <= lfx) || (j >= ow)) continue;
				break;
			}
			if (xl >= xr) xl = xr = ring_r[idx];

			/* Read in a new row */
			(cc != CHN_IMAGE ? skew_fill_util : rgba ?
				skew_fill_rgba : skew_fill_rgb)(thisbuf,
				filler, old_img[cc], old_img[CHN_ALPHA],
				y0, ow, xl, xr, ring_l[idx], ring_r[idx],
				xfsz, xfilt, dxx, dyy, gcor);

			if (xl >= xr) xl = nw , xr = 0;
			ring_l[idx] = xl;
			ring_r[idx] = xr;

			if (i < 0) continue; // Initialization phase

			/* Clip target row */
			if (i <= YY[0]) xl = ceil(XX[0] + (i - YY[0]) * Kh);
			else if (i <= YY[2]) xl = ceil(XX[2] + (i - YY[2]) * Kv);
			else /* if (i <= YY[3]) */ xl = ceil(XX[2] + (i - YY[2]) * Kh);
			if (i <= YY[1]) xr = ceil(XX[1] + (i - YY[1]) * Kh);
			else if (i <= YY[3]) xr = ceil(XX[3] + (i - YY[3]) * Kv);
			else /* if (i <= YY[2]) */ xr = ceil(XX[3] + (i - YY[3]) * Kh);
			if (xl < 0) xl = 0;
			if (xr > nw) xr = nw; // Right boundary is exclusive

			/* Run vertical filter over the row buffers */
			thisbuf = rbuf + xl * bpp;
			thatbuf = wbuf + xl * bpp;
			len = xr - xl;
			if (len <= 0); // Do nothing
			else if (yfsz == 1) // Just copy
				memcpy(thisbuf, thatbuf, len * bpp * sizeof(double));
			else // Apply filter
			{
				memset(thisbuf, 0, len * bpp * sizeof(double));
				filt0 = yfilt + xl * yfsz;
				for (j = 0 , k = idx; j < yfsz; j++)
				{
					double *dsrc, *ddest = thisbuf, *filt = filt0++;
					int l = len;

					if (++k >= yfsz) k = 0;
					dsrc = thatbuf + k * wbsz;
					while (l-- > 0)
					{
						double kk = *filt;
						filt += yfsz;
						*ddest++ += *dsrc++ * kk;
						if (bpp < 3) continue;
						*ddest++ += *dsrc++ * kk;
						*ddest++ += *dsrc++ * kk;
						if (bpp == 3) continue;
						*ddest++ += *dsrc++ * kk;
						*ddest++ += *dsrc++ * kk;
						*ddest++ += *dsrc++ * kk;
						*ddest++ += *dsrc++ * kk;
					}
				}
			}

			/* Write out results */
			ofs = i * nw + xl;
			if (cc == CHN_IMAGE) // RGB and RGBA
			{
				double *dsrc = thisbuf;
				unsigned char *dest, *dsta;
				int l = len, n = step;

				dest = new_img[CHN_IMAGE] + ofs * 3;
				dsta = rgba ? new_img[CHN_ALPHA] + ofs : NULL;
				while (l-- > 0)
				{
					double rr, gg, bb, aa;
					int a;

					if (dsta && (a = rint(aa = dsrc[6]) ,
						*dsta++ = a < 0 ? 0 :
						a > 0xFF ? 0xFF : a))
					{
						aa = 1.0 / aa;
						rr = dsrc[3] * aa;
						gg = dsrc[4] * aa;
						bb = dsrc[5] * aa;
					}
					else
					{
						rr = dsrc[0];
						gg = dsrc[1];
						bb = dsrc[2];
					}
					if (gcor)
					{
						dest[0] = UNGAMMA256X(rr);
						dest[1] = UNGAMMA256X(gg);
						dest[2] = UNGAMMA256X(bb);
					}
					else
					{
						int r, g, b;
						r = rint(rr);
						dest[0] = r < 0 ? 0 : r > 0xFF ? 0xFF : r;
						g = rint(gg);
						dest[1] = g < 0 ? 0 : g > 0xFF ? 0xFF : g;
						b = rint(bb);
						dest[2] = b < 0 ? 0 : b > 0xFF ? 0xFF : b;
					}
					dsrc += n; dest += 3;
				}
			}
			else // Utility channel
			{
				double *dsrc = thisbuf;
				unsigned char *dest = new_img[cc] + ofs;
				int l = len, n;

				while (l-- > 0)
				{
					n = rint(*dsrc++);
					*dest++ = n < 0 ? 0 : n > 0xFF ? 0xFF : n;
				}
			}
		}
	}

fail:	free(xmem);
	free(ymem);
	free(tmem);
}

static void mem_skew_nn(chanlist old_img, chanlist new_img, int ow, int oh,
	int nw, int nh, int bpp, double xskew, double yskew, int silent)
{
	double x0, y0, d, Kh, Kv, XX[4], YY[4];
	int i, ny;

	/* Calculate clipping parallelogram's corners */
	x0 = 0.5 * (nw - 1); y0 = 0.5 * (nh - 1);
	XX[1] = XX[3] = (XX[0] = XX[2] = 0.5 * (nw - ow - 1)) + ow;
	YY[2] = YY[3] = (YY[0] = YY[1] = 0.5 * (nh - oh - 1)) + oh;
	for (i = 0; i < 4; i++)
	{
		XX[i] += (YY[i] - y0) * xskew;
		YY[i] += (XX[i] - x0) * yskew;
	}
	d = 1.0 + xskew * yskew;
	Kv = d ? xskew / d : 0.0; // for left & right
	Kh = yskew ? 1.0 / yskew : 0.0; // for top & bottom

	/* Process image row by row */
	for (ny = 0; ny < nh; ny++)
	{
		int cc, xl, xr;

		if (!silent && ((ny * 10) % nh >= nh - 10))
			progress_update((float)ny / nh);

		/* Clip row */
		if (ny <= YY[0]) xl = ceil(XX[0] + (ny - YY[0]) * Kh);
		else if (ny <= YY[2]) xl = ceil(XX[2] + (ny - YY[2]) * Kv);
		else /* if (ny <= YY[3]) */ xl = ceil(XX[2] + (ny - YY[2]) * Kh);
		if (ny <= YY[1]) xr = ceil(XX[1] + (ny - YY[1]) * Kh);
		else if (ny <= YY[3]) xr = ceil(XX[3] + (ny - YY[3]) * Kv);
		else /* if (ny <= YY[2]) */ xr = ceil(XX[3] + (ny - YY[3]) * Kh);
		if (xl < 0) xl = 0;
		if (xr > nw) xr = nw; // Right boundary is exclusive

		for (cc = 0; cc < NUM_CHANNELS; cc++)
		{
			unsigned char *src, *dest;
			double x0y, y0y;
			int nx, ox, oy;

			if (!new_img[cc]) continue;

			x0y = 0.5 * (ow - 1) - x0 * d + (y0 - ny) * xskew;
			y0y = x0 * yskew - 0.5 * (nh - oh) + ny;

			/* RGB nearest neighbour */
			if ((cc == CHN_IMAGE) && (bpp == 3))
			{
				dest = new_img[CHN_IMAGE] + (ny * nw + xl) * 3;
				for (nx = xl; nx < xr; nx++ , dest += 3)
				{
// !!! Later, try reimplementing these calculations in row-then-column way -
// !!! while less theoretically precise, it might cause less jitter, and better
// !!! match what other skew-transform code does - WJ
					WJ_ROUND(ox, x0y + nx * d);
					WJ_ROUND(oy, y0y - nx * yskew);
					src = old_img[CHN_IMAGE] +
						(oy * ow + ox) * 3;
					dest[0] = src[0];
					dest[1] = src[1];
					dest[2] = src[2];
				}
			}
			/* One-bpp nearest neighbour */
			else
			{
				dest = new_img[cc] + ny * nw + xl;
				for (nx = xl; nx < xr; nx++)
				{
					WJ_ROUND(ox, x0y + nx * d);
					WJ_ROUND(oy, y0y - nx * yskew);
					*dest++ = old_img[cc][oy * ow + ox];
				}
			}
		}
	}
}

/* Skew geometry calculation is far nastier than same for rotation, and worse,
 * the approaches don't quite match in case of 3-skew rotation - WJ */
static void mem_skew_geometry(int ow, int oh, double xskew, double yskew,
	int rotation, int *nw, int *nh)
{
	double nww, nhh, ax, ay;
	int dx, dy, dx0, dy0;


	/* Select new centering */
	dx0 = ow & 1; dy0 = oh & 1;
	/* Pure skew */
	if (!rotation)
	{
		/* For certain skew factors, when the other dimension is even,
		 * rows/columns nearest to axis fit the pixel grid better if
		 * skew dimension is realigned to offset them half a pixel */
		dx = dy0 ? dx0 : dx0 ^ ((int)(fabs(xskew) + 0.5) & 1);
		dy = dx ? dy0 : dy0 ^ ((int)(fabs(yskew) + 0.5) & 1);
	}
	/* Rotation for 45 degrees or less */
	else if (fabs(yskew) <= M_SQRT1_2) dx = dx0 , dy = dy0;
	/* Rotation for more than 45 degrees */
	else
	{
		// Height gets to be odd - do width realign now
		if (dx0) dx = dy0;
		// Leave width realign till 3rd pass
		else if (dy0) dx = 0;
		// Let double realign happen when possible & useful
		else dx = (int)(fabs(xskew) + 0.5) & 1;
		dy = dx0;
	}

	/* Calculate theoretical dimensions */
	ax = fabs(xskew); ay = fabs(yskew);
	nww = ow + oh * ax;
	if (xskew * yskew >= 0) nhh = nww * ay + oh;
	else if (ax * ay > 1) nhh = nww * ay - oh;
	else nhh = (ow - oh * ax) * ay + oh;
	/* Convert to actual pixel dimensions */
	*nw = 2 * (int)(0.5 * (nww - dx) + PIX_ADD) + dx;
	*nh = 2 * (int)(0.5 * (nhh - dy) + PIX_ADD) + dy;
}

// Skew canvas in one or two directions (X then Y)
// !!! Later, extend to handle skew+shift in both directions, too
int mem_skew(double xskew, double yskew, int type, int gcor)
{
	chanlist old_img, new_img;
	int ow, oh, nw, nh, res, bpp;

	ow = mem_width;
	oh = mem_height;
	bpp = mem_img_bpp;

	mem_skew_geometry(ow, oh, xskew, yskew, FALSE, &nw, &nh);

	if ((nw > MAX_WIDTH) || (nh > MAX_HEIGHT)) return (-5);

	memcpy(old_img, mem_img, sizeof(chanlist));
	res = undo_next_core(UC_NOCOPY, nw, nh, bpp, CMASK_ALL);
	if (res) return (res);		// No undo space
	memcpy(new_img, mem_img, sizeof(chanlist));
	progress_init(_("Skew"), 0);

	mem_clear_img(new_img, nw, nh, bpp);
	if (!type || (mem_img_bpp == 1)) mem_skew_nn(old_img, new_img,
		ow, oh, nw, nh, bpp, xskew, yskew, FALSE);
	else mem_skew_filt(old_img, new_img, ow, oh, nw, nh, xskew, yskew,
		type, gcor, channel_dis[CHN_ALPHA], FALSE);

	progress_end();

	return (0);
}

// Get gamma-corrected average of RGB pixels in an area, or -1 if out of bounds
int average_pixels(unsigned char *rgb, int iw, int ih, int x, int y, int w, int h)
{
	unsigned char *tmp;
	double rr, gg, bb, dd;
	int i, j;


	/* Clip to image */
	w += x; h += y;
	if (x < 0) x = 0;
	if (w > iw) w = iw;
	if (y < 0) y = 0;
	if (h > ih) h = ih;
	/* Nothing remained */
	if ((x >= w) || (y >= h)) return (-1);

	/* Average (gamma corrected) area */
	w -= x;
	rr = gg = bb = 0.0;
	for (i = y; i < h; i++)
	{
		tmp = rgb + (i * iw + x) * 3;
		for (j = 0; j < w; j++ , tmp += 3)
		{
			rr += gamma256[tmp[0]];
			gg += gamma256[tmp[1]];
			bb += gamma256[tmp[2]];
		}
	}
	dd = 1.0 / (w * (h - y));
	rr *= dd; gg *= dd; bb *= dd;
	return (RGB_2_INT(UNGAMMA256(rr), UNGAMMA256(gg), UNGAMMA256(bb)));
}

// Convert a row of pixels to any of 3 colorspaces
static void mem_convert_row(double *dest, unsigned char *src, int l, int cspace)
{
	if (cspace == CSPACE_LXN)
	{
		while (l-- > 0)
		{
			get_lxn(dest, MEM_2_INT(src, 0));
			dest += 3; src += 3;
		}
	}
	else if (cspace == CSPACE_SRGB)
	{
		l *= 3;
		while (l-- > 0) *dest++ = gamma256[*src++];
	}
	else /* if (cspace == CSPACE_RGB) */
	{
		l *= 3;
		while (l-- > 0) *dest++ = *src++;
	}
}

///	SEGMENTATION

/*
 * This code implements a 4-way variation of the segmentation algorithm
 * described in:
 * Pedro F. Felzenszwalb, "Efficient Graph-Based Image Segmentation"
 */

static int cmp_edge(const void *v1, const void *v2)
{
	float f1 = ((seg_edge *)v1)->diff, f2 = ((seg_edge *)v2)->diff;
	return (f1 < f2 ? -1 : f1 != f2 ? f1 > f2 :
		((seg_edge *)v1)->which - ((seg_edge *)v2)->which);
}

static inline int seg_find(seg_pixel *pix, int n)
{
	unsigned int i, j;

	for (i = n; i != (j = pix[i].group); i = j);
	return (pix[n].group = i);
}

static inline int seg_join(seg_pixel *pix, int a, int b)
{
	seg_pixel *ca = pix + a, *cb = pix + b;

	if (ca->rank > cb->rank)
	{
		ca->cnt += cb->cnt;
		return (cb->group = a);
	}
	cb->cnt += ca->cnt;
	cb->rank += (ca->rank == cb->rank);
	return (ca->group = b);
}

seg_state *mem_seg_prepare(seg_state *s, unsigned char *img, int w, int h,
	int flags, int cspace, int dist)
{
	static const unsigned char dist_scales[NUM_CSPACES] = { 1, 255, 1 };
	seg_edge *e;
	double mult, *row0, *row1, *rows[2];
	int i, j, k, l, bsz, sz = w * h;


	// !!! Will need a longer int type (and twice the memory) otherwise
	if (sz > (INT_MAX >> 1) + 1) return (NULL);

	/* 3 buffers will be sharing space */
	bsz = w * 3 * 2 * sizeof(double);
	l = sz * sizeof(seg_pixel);
	if (l > bsz) bsz = l;

	if (!s) // Reuse existing allocation if possible
	{ /* Allocation is HUGE, but no way to make do with smaller one - WJ */
		void *v[3];

		s = multialloc(MA_ALIGN_DOUBLE,
			v, sizeof(seg_state), // Dummy pointer (header struct)
			v + 1, bsz, // Row buffers/pixel nodes
			v + 2, sz * 2 * sizeof(seg_edge), // Pixel connections
			NULL);
		if (!s) return (NULL);
		s->pix = v[1];
		s->edges = v[2];
		s->w = w;
		s->h = h;
	}
	rows[0] = (void *)s->pix; // 1st row buffer
	rows[1] = rows[0] + w * 3; // 2nd row buffer
	s->phase = 0; // Struct is to be refilled

	if (flags & SEG_PROGRESS) progress_init(_("Segmentation Pass 1"), 1);

	/* Compute color distances, fill connections buffer */
	l = w * 3;
	mult = dist_scales[cspace]; // Make all colorspaces use similar scale
	for (i = 0 , e = s->edges; i < h; i++)
	{
		k = i * w;
		mem_convert_row(row1 = rows[i & 1], img + k * 3, w, cspace);
		/* Right vertices for this row */
		for (j = 3 , k *= 2; j < l; j += 3 , k += 2 , e++)
		{
			e->which = k;
			e->diff = mult * distance_3d[dist](row1 + j - 3, row1 + j);
		}
		if (!i) continue;
		/* Bottom vertices for previous row */
		k = (i - 1) * w * 2 + 1;
		row0 = rows[~i & 1];
		for (j = 0; j < l; j += 3 , k += 2 , e++)
		{
			e->which = k;
			e->diff = mult * distance_3d[dist](row0 + j, row1 + j);
		}
		if ((flags & SEG_PROGRESS) && ((i * 20) % h >= h - 20))
			if (progress_update((0.9 * i) / h)) goto quit;
	}

	/* Sort connections, smallest distances first */
	s->cnt = e - s->edges;
	qsort(s->edges, s->cnt, sizeof(seg_edge), cmp_edge);

	s->phase = 1;

quit:	if (flags & SEG_PROGRESS) progress_end();

	return (s);
}

int mem_seg_process_chunk(int start, int cnt, seg_state *s)
{
	seg_edge *edge;
	seg_pixel *cp, *pix = s->pix;
	double threshold = s->threshold;
	int minrank = s->minrank, minsize = s->minsize;
	int sz = s->w * s->h, w1[2] = { 1, s->w };
	int i, ix, pass;

	/* Initialize pixel nodes */
	if (!start)
	{
		for (i = 0 , cp = pix; i < sz; i++ , cp++)
		{
			cp->group = i;
			cp->cnt = 1;
			cp->rank = 0;
			cp->threshold = threshold;
		}
	}

	/* Setup loop range */
	pass = start / s->cnt;
	i = start % s->cnt;
	cnt += i;

	for (; pass < 3; pass++)
	{
		ix = cnt < s->cnt ? cnt : s->cnt;
		edge = s->edges + i;
		for (; i < ix; i++ , edge++)
		{
			float dist;
			int j, k, idx;

			/* Get the original pixel */
			dist = edge->diff;
			idx = edge->which;
			j = idx >> 1;
			/* Get the neighboring pixel's index */
			k = j + w1[idx & 1];
			/* Get segment anchors */
			j = seg_find(pix, j);
			k = seg_find(pix, k);
			if (j == k) continue;
			/* Merge segments if difference is small enough in pass 0,
			 * one of segments is too low rank in pass 1, or is too
			 * small in pass 2 */
			if (!pass ? ((dist <= pix[j].threshold) &&
					(dist <= pix[k].threshold)) :
				pass == 1 ? ((pix[j].rank < minrank) ||
					(pix[k].rank < minrank)) :
				((pix[j].cnt < minsize) || (pix[k].cnt < minsize)))
			{
				seg_pixel *cp = pix + seg_join(pix, j, k);
				cp->threshold = dist + threshold / cp->cnt;
			}
		}
		/* Pass not yet completed - return progress */
		if (cnt < s->cnt) return (pass * s->cnt + cnt);
		cnt -= s->cnt;
		i = 0;
		pass += !pass && !minrank; // Maybe skip pass 1
		pass += pass && (minsize <= (1 << minrank)); // Maybe skip pass 2
	}

	/* Normalize groups */
	for (i = 0; i < sz; i++) seg_find(pix, i);

	/* All done */
	s->phase |= 2;
	return (s->cnt * 3);
}

int mem_seg_process(seg_state *s)
{
	int i, n, cnt = s->cnt * 3;


	if (s->w * s->h < 1024 * 1024)
	{
		/* Run silently */
		mem_seg_process_chunk(0, cnt, s);
		return (TRUE);
	}

	/* Show progress */
	n = (cnt + 19) / 20;
	progress_init(_("Segmentation Pass 2"), 1);
	for (i = 0; s->phase < 2; i = mem_seg_process_chunk(i, n, s))
 		if (progress_update((float)i / cnt)) break;
	progress_end();
	return (s->phase >= 2);
}

/* This produces for one row 2 difference bits per pixel: left & up; if called
 * with segmentation still in progress, will show oversegmentation */
void mem_seg_scan(unsigned char *dest, int y, int x, int w, int zoom,
	const seg_state *s)
{
	int i, j, k, l, ofs, dy;
	seg_pixel *pix = s->pix;

	memset(dest, 0, (w + 3) >> 2);
	ofs = (y * s->w + x) * zoom;
	dy = y ? s->w * zoom : 0; // No up neighbors for Y=0
	j = pix[ofs + (!x - 1) * zoom].group; // No left neighbor for X=0
	for (i = 0; i < w; i++ , j = k , ofs += zoom)
	{
		k = pix[ofs].group , l = pix[ofs - dy].group;
		dest[i >> 2] |= ((j != k) * 2 + (k != l)) << ((i + i) & 6);
	}
}

/* Draw segments in unique colors */
void mem_seg_render(unsigned char *img, const seg_state *s)
{
	int i, k, l, sz = s->w * s->h;
	seg_pixel *pix = s->pix;

	for (i = l = 0; i < sz; i++)
	{
		int j, k, r, g, b;

		if (pix[i].group != i) continue; // Only new groups
		k = l++;
		/* Transform index to most distinct RGB color */
		for (j = r = g = b = 0; j < 8; j++)
		{
			r += r + (k & 1); k >>= 1;
			g += g + (k & 1); k >>= 1;
			b += b + (k & 1); k >>= 1;
		}
		pix[i].cnt = RGB_2_INT(r, g, b);
	}

	for (i = 0; i < sz; i++ , img += 3)
	{
		k = pix[pix[i].group].cnt;
		img[0] = INT_2_R(k);
		img[1] = INT_2_G(k);
		img[2] = INT_2_B(k);
	}
}

#define FRACTAL_DIM 1.25
//#define FRACTAL_THR 20 /* dragban2.jpg */
#define FRACTAL_THR 15 /* dragban2.jpg after K-N */
#define THRESHOLD_MULT 20.0

/* Estimate contour length by fractal dimension, use the difference value found
 * this way as threshold's base value */
double mem_seg_threshold(seg_state *s)
{
	int k = s->cnt - FRACTAL_THR * pow(s->cnt, 0.5 * FRACTAL_DIM);
	while (!s->edges[k].diff && (k < s->cnt - 1)) k++;
	return (s->edges[k].diff ? s->edges[k].diff * THRESHOLD_MULT : 1.0);
}

#undef FRACTAL_DIM
#undef FRACTAL_THR
#undef THRESHOLD_MULT

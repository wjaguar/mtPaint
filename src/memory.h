/*	memory.h
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

#include <limits.h>
#include <png.h>

/// Definitions, structures & variables

#define MAX_WIDTH 16384
#define MAX_HEIGHT 16384
#define MIN_WIDTH 1
#define MIN_HEIGHT 1
/* !!! If MAX_WIDTH * MAX_HEIGHT * max bpp won't fit into int, lots of code
 * !!! will have to be modified to use size_t instead */
#define MAX_DIM (MAX_WIDTH > MAX_HEIGHT ? MAX_WIDTH : MAX_HEIGHT)

#define DEFAULT_WIDTH 640
#define DEFAULT_HEIGHT 480

/* Palette area layout */

#define PALETTE_SWATCH_X 25
#define PALETTE_SWATCH_Y 1
#define PALETTE_SWATCH_W 26
#define PALETTE_SWATCH_H 16
#define PALETTE_INDEX_X  0
#define PALETTE_INDEX_DY 5
#define PALETTE_DIGIT_W  7
#define PALETTE_DIGIT_H  7
#define PALETTE_CROSS_X  53
#define PALETTE_CROSS_DX 4
#define PALETTE_CROSS_DY 4
#define PALETTE_CROSS_W  8
#define PALETTE_CROSS_H  8

#define PALETTE_WIDTH 70
#define PALETTE_W3 (PALETTE_WIDTH * 3)
#define PALETTE_HEIGHT (PALETTE_SWATCH_H * 256 + PALETTE_SWATCH_Y * 2)

#define PATCH_WIDTH 324
#define PATCH_HEIGHT 324

#define TOOL_SQUARE 0
#define TOOL_CIRCLE 1
#define TOOL_HORIZONTAL 2
#define TOOL_VERTICAL 3
#define TOOL_SLASH 4
#define TOOL_BACKSLASH 5
#define TOOL_SPRAY 6
#define TOOL_SHUFFLE 7
#define TOOL_FLOOD 8
#define TOOL_SELECT 9
#define TOOL_LINE 10
#define TOOL_SMUDGE 11
#define TOOL_POLYGON 12
#define TOOL_CLONE 13
#define TOOL_GRADIENT 14

#define TOTAL_CURSORS 15

#define NO_PERIM(T) (((T) == TOOL_FLOOD) || ((T) == TOOL_SELECT) || \
	((T) == TOOL_POLYGON) || ((T) == TOOL_GRADIENT))

#define PROGRESS_LIM 262144

#define CHN_IMAGE 0
#define CHN_ALPHA 1
#define CHN_SEL   2
#define CHN_MASK  3
#define NUM_CHANNELS 4

#define CMASK_NONE  0
#define CMASK_IMAGE (1 << CHN_IMAGE)
#define CMASK_RGBA  ((1 << CHN_IMAGE) | (1 << CHN_ALPHA))
#define CMASK_ALL   ((1 << NUM_CHANNELS) - 1)
#define CMASK_CURR  (1 << mem_channel)
#define CMASK_FOR(A) (1 << (A))
#define CMASK_CLIP  ((1 << CHN_IMAGE) | (1 << CHN_ALPHA) | (1 << CHN_SEL))

#define SIZEOF_PALETTE (256 * sizeof(png_color))

// Both limits should be powers of two
#define FRAMES_MIN 16
#define FRAMES_MAX (1024 * 1024) /* A million frames should be QUITE enough */

/* Frame flags */
#define FM_DISPOSAL     3 /* Disposal mask */
#define FM_DISP_REMOVE  0 /* Remove (make transparent) (default) */
#define FM_DISP_LEAVE   1 /* Leave in place */
#define FM_DISP_RESTORE 2 /* Restore to previous state */
#define FM_NUKE         4 /* Delete this frame at earliest opportunity */

/* Undo data types */
#define UD_FILENAME 0 /* Filename */
#define UD_TEMPNAME 1 /* Temp file name */
#define NUM_UTYPES  2 /* Should be no larger than 32 */
//	List in here all types which need freeing
#define UD_FREE_MASK (1 << UD_FILENAME)

typedef unsigned char *chanlist[NUM_CHANNELS];

typedef struct {
	chanlist img;
	png_color *pal;
	int width, height;
	int x, y, delay;
	short cols, bpp, trans;
	unsigned short flags;
} image_frame;

typedef struct {
	image_frame *frames;	// Pointer to frames array
	png_color *pal;		// Default palette
	int cur;		// Index of current frame
	int cnt;		// Number of frames in use
	int max;		// Total number of frame slots
	size_t size;		// Total used memory (0 means count it anew)
} frameset;

typedef struct {
	unsigned int map;
	void *store[NUM_UTYPES];
} undo_data;

typedef struct {
	chanlist img;
	png_color *pal_;
	unsigned char *tileptr;
	undo_data *dataptr;
	int cols, width, height, bpp, flags;
	size_t size;
} undo_item;

typedef struct {
	undo_item *items;	// Pointer to undo images + current image being edited
	int pointer;		// Index of currently used image on canvas/screen
	int done;		// Undo images that we have behind current image (i.e. possible UNDO)
	int redo;		// Undo images that we have ahead of current image (i.e. possible REDO)
	int max;		// Total number of undo slots
	size_t size;		// Total used memory (0 means count it anew)
} undo_stack;

typedef struct {
	chanlist img;		// Array of pointers to image channels
	png_color pal[256];	// RGB entries for all 256 palette colours
	int cols;	// Number of colours in the palette: 1..256 or 0 for no image
	int bpp;		// Bytes per pixel = 1 or 3
	int width, height;	// Image geometry
	undo_stack undo_;	// Image's undo stack
	char *filename;		// File name of file loaded/saved
	char *tempname;		// File name of up-to-date temp file
	int changed;		// Changed since last load/save flag
} image_info;

typedef struct {
	int channel;			// Current active channel
	int ics;			// Has the centre been set by the user?
	float icx, icy;			// Current centre x,y
	int tool_pat;			// Tool pattern number
	int xpm_trans;			// Transparent colour index (-1 if none)
	int xbm_hot_x, xbm_hot_y;	// Current XBM hot spot
	char prot_mask[256];		// 256 bytes used for indexed images
	int prot;			// Number of protected colours in prot_RGB
	int prot_RGB[256];		// Up to 256 RGB colours protected
	int col_[2];			// Index for colour A & B
	png_color col_24[2];		// RGB for colour A & B
} image_state;

/// GRADIENTS

#define MAX_GRAD 65536
#define GRAD_POINTS 256

typedef struct {
	/* Base values */
	int status, xy[4];	// Gradient placement tool
	int len, rep, ofs;	// Gradient length, repeat, and offset
	int gmode, rmode;	// Gradient mode and repeat mode
	/* Derived values */
	double wrep, wil1, wil2, xv, yv, wa;
	int wmode, wrmode;
} grad_info;

typedef struct {
	char gtype, otype;		// Main and opacity gradient types
	char grev, orev;		// Main and opacity reversal flags
	int vslen, oplen;		// Current gradient lengths
	int cvslen, coplen;		// Custom gradient lengths
	unsigned char *vs, *vsmap;	// Current gradient data
	unsigned char *op, *opmap;	// Current gradient opacities
} grad_map;

typedef unsigned char grad_store[(6 + NUM_CHANNELS * 4) * GRAD_POINTS];

grad_info gradient[NUM_CHANNELS];	// Per-channel gradient placement
double grad_path, grad_x0, grad_y0;	// Stroke gradient temporaries
grad_map graddata[NUM_CHANNELS + 1];	// RGB + per-channel gradient data
grad_store gradbytes;			// Storage space for custom gradients
int grad_opacity;			// Preview opacity

/* Gradient modes */
#define GRAD_MODE_NONE     0
#define GRAD_MODE_BURST    1
#define GRAD_MODE_LINEAR   2
#define GRAD_MODE_BILINEAR 3
#define GRAD_MODE_RADIAL   4
#define GRAD_MODE_SQUARE   5
#define GRAD_MODE_ANGULAR  6
#define GRAD_MODE_CONICAL  7

/* Boundary conditions */
#define GRAD_BOUND_STOP    0
#define GRAD_BOUND_LEVEL   1
#define GRAD_BOUND_REPEAT  2
#define GRAD_BOUND_MIRROR  3
#define GRAD_BOUND_STOP_A  4 /* Stop mode for angular gradient */
#define GRAD_BOUND_REP_A   5 /* Repeat mode for same */

/* Gradient types */
#define GRAD_TYPE_RGB      0
#define GRAD_TYPE_SRGB     1
#define GRAD_TYPE_HSV      2
#define GRAD_TYPE_BK_HSV   3
#define GRAD_TYPE_CONST    4
#define GRAD_TYPE_CUSTOM   5

/// Bayer ordered dithering

const unsigned char bayer[16];

#define BAYER_MASK 15 /* Use 16x16 matrix */
#define BAYER(x,y) (bayer[((x) ^ (y)) & BAYER_MASK] * 2 + bayer[(y) & BAYER_MASK])

/// Tint tool - contributed by Dmitry Groshev, January 2006

int tint_mode[3];			// [0] = off/on, [1] = add/subtract, [2] = button (none, left, middle, right : 0-3)

int mem_cselect;
int mem_blend;
int mem_unmask;
int mem_gradient;

/// BLEND MODE

int blend_mode;

/* Blend modes */
enum {
	/* RGB-only modes */
	BLEND_NORMAL = 0,
	BLEND_HUE,
	BLEND_SAT,
	BLEND_VALUE,
	BLEND_COLOR,
	BLEND_SATPP,

	/* Per-channel modes */
	BLEND_MULT,
	BLEND_DIV,
	BLEND_SCREEN,
// !!! No "Overlay" - it is a reverse "Hard Light"
	BLEND_DODGE,
	BLEND_BURN,
	BLEND_HLIGHT,
	BLEND_SLIGHT,
	BLEND_DIFF,
	BLEND_DARK,
	BLEND_LIGHT,
	BLEND_GRAINX,
	BLEND_GRAINM,

	BLEND_NMODES
};
#define BLEND_1BPP BLEND_MULT /* First one-byte mode */

#define BLEND_MMASK    0x7F
#define BLEND_REVERSE  0x80
#define BLEND_RGBSHIFT 8


/// FLOOD FILL SETTINGS

double flood_step;
int flood_cube, flood_img, flood_slide;

int smudge_mode;
int posterize_mode;	// bitwise/truncated/rounded

/// QUANTIZATION SETTINGS

int quan_sqrt;	// "Diameter based weighting" - use sqrt of pixel count

/// IMAGE

#define MIN_UNDO 11	// Number of undo levels + 1
#define DEF_UNDO 101
#define MAX_UNDO 1001

int mem_undo_depth;				// Current undo depth

image_info mem_image;			// Current image

#define mem_img		mem_image.img		
#define mem_pal		mem_image.pal
#define mem_cols	mem_image.cols
#define mem_img_bpp	mem_image.bpp
#define mem_width	mem_image.width
#define mem_height	mem_image.height

#define mem_undo_im_		mem_image.undo_.items
#define mem_undo_pointer	mem_image.undo_.pointer
#define mem_undo_done		mem_image.undo_.done
#define mem_undo_redo		mem_image.undo_.redo
#define mem_undo_max		mem_image.undo_.max

#define mem_filename		mem_image.filename
#define mem_tempname		mem_image.tempname
#define mem_changed		mem_image.changed

image_info mem_clip;			// Current clipboard

#define mem_clipboard		mem_clip.img[CHN_IMAGE]
#define mem_clip_alpha		mem_clip.img[CHN_ALPHA]
#define mem_clip_mask		mem_clip.img[CHN_SEL]
#define mem_clip_bpp		mem_clip.bpp
#define mem_clip_w		mem_clip.width
#define mem_clip_h		mem_clip.height

// Always use undo slot #1 for clipboard backup
#define OLD_CLIP 1
// mem_clip.undo_.done == 0 means no backup clipboard
#define HAVE_OLD_CLIP		(mem_clip.undo_.done)
#define mem_clip_real_img	mem_clip.undo_.items[OLD_CLIP].img
#define mem_clip_real_w		mem_clip.undo_.items[OLD_CLIP].width
#define mem_clip_real_h		mem_clip.undo_.items[OLD_CLIP].height
#define mem_clip_real_clear()	mem_free_image(&mem_clip, FREE_UNDO)

image_state mem_state;			// Current edit settings

#define mem_channel		mem_state.channel
#define mem_icx			mem_state.icx
#define mem_icy			mem_state.icy
#define mem_ics			mem_state.ics
#define mem_tool_pat		mem_state.tool_pat
#define mem_xpm_trans		mem_state.xpm_trans
#define mem_xbm_hot_x		mem_state.xbm_hot_x
#define mem_xbm_hot_y		mem_state.xbm_hot_y
#define mem_prot_mask		mem_state.prot_mask
#define mem_prot_RGB		mem_state.prot_RGB
#define mem_prot		mem_state.prot
#define mem_col_		mem_state.col_
#define mem_col_A		mem_state.col_[0]
#define mem_col_B		mem_state.col_[1]
#define mem_col_24		mem_state.col_24
#define mem_col_A24		mem_state.col_24[0]
#define mem_col_B24		mem_state.col_24[1]

int mem_clip_x, mem_clip_y;		// Clipboard location on canvas

extern unsigned char mem_brushes[];	// Preset brushes image
int mem_brush_list[81][3];		// Preset brushes parameters
int mem_nudge;				// Nudge pixels per SHIFT+Arrow key during selection/paste

int mem_prev_bcsp[6];			// BR, CO, SA, POSTERIZE, GAMMA, Hue

int mem_undo_limit;		// Max MB memory allocation limit
int mem_undo_common;		// Percent of undo space in common arena
int mem_undo_opacity;		// Use previous image for opacity calculations?

/// PATTERNS

unsigned char mem_pattern[8 * 8];	// Current pattern
unsigned char mem_col_pat[8 * 8];	// Indexed 8x8 colourised pattern using colours A & B
unsigned char mem_col_pat24[8 * 8 * 3];	// RGB 8x8 colourised pattern using colours A & B

/// TOOLS

typedef struct {
	int type, brush;
	int var[3];		// Size, flow, opacity
} tool_info;

tool_info tool_state;

#define tool_type	tool_state.type		/* Currently selected tool */
#define brush_type	tool_state.brush	/* Last brush tool type */
#define tool_size	tool_state.var[0]
#define tool_flow	tool_state.var[1]
#define tool_opacity	tool_state.var[2]	/* Opacity - 255 = solid */

int pen_down;				// Are we drawing? - Used to see if we need to do an UNDO
int tool_ox, tool_oy;			// Previous tool coords - used by continuous mode
int mem_continuous;			// Area we painting the static shapes continuously?

/// PREVIEW

int mem_brcosa_allow[3];		// BRCOSA RGB

/// PALETTE

png_color mem_pal_def[256];		// Default palette entries for new image
int mem_pal_def_i;			// Items in default palette
extern unsigned char mem_pals[];	// RGB screen memory holding current palette

int mem_background;			// Non paintable area
int mem_histogram[256];

/// Next power of two

static inline unsigned int nextpow2(unsigned int n)
{
	n |= n >> 1; n |= n >> 2; n |= n >> 4; n |= n >> 8; n |= n >> 16;
#if UINT_MAX > 0xFFFFFFFFUL
	n |= n >> 32;
#endif
	return (n + 1);
}

// Number of set bits

static inline unsigned int bitcount(unsigned int n)
{
	unsigned int m;
#if UINT_MAX > 0xFFFFFFFFUL
	n -= (n >> 1) & 0x5555555555555555ULL;
	m = n & 0xCCCCCCCCCCCCCCCCULL; n = (n ^ m) + (m >> 2);
	n = (n + (n >> 4)) & 0x0F0F0F0F0F0F0F0FULL;
	n = (n + (n >> 8)) & 0x00FF00FF00FF00FFULL;
	n = (n + (n >> 16)) & 0x0000FFFF0000FFFFULL;
	n = (n + (n >> 32)) & 0x00000000FFFFFFFFULL;
#else
	n -= (n >> 1) & 0x55555555;
	m = n & 0x33333333; n = m + ((n ^ m) >> 2);
	n = (n + (n >> 4)) & 0x0F0F0F0F;
	n = (n + (n >> 8)) & 0x00FF00FF;
	n = (n + (n >> 16)) & 0x0000FFFF;
#endif
	return (n);
}

/// Floored integer division

static inline int floor_div(int dd, int dr)
{
	return (dd / dr - (dd % dr < 0)); // optimizes to perfection on x86
}

/// Table-based translation

static inline void do_xlate(unsigned char *xlat, unsigned char *data, int len)
{
	int i;

	for (i = 0; i < len; i++) data[i] = xlat[data[i]];
}

/// Copying "x0y0x1y1" quad

#define copy4(D,S) memcpy(D, S, 4 * sizeof(int))

/// Block allocator

typedef struct {
	char *block;
	unsigned int here, size;
	unsigned int minsize, incr;
} wjmem;

wjmem *wjmemnew(int size, int incr);
void wjmemfree(wjmem *mem);
void *wjmalloc(wjmem *mem, int size, int align);

/// Multiblock allocator

#define MA_ALIGN_MASK    0x03 /* For alignment constraints */
#define MA_ALIGN_DEFAULT 0x00
#define MA_ALIGN_DOUBLE  0x01
#define MA_SKIP_ZEROSIZE 0x04 /* Leave pointers to zero-size areas unchanged */

void *multialloc(int flags, void *ptr, int size, ...);

/// Vectorized low-level drawing functions

void (*put_pixel)(int x, int y);
void (*put_pixel_row)(int x, int y, int len, unsigned char *xsel);

	// Intersect two rectangles
int clip(int *rxy, int x0, int y0, int x1, int y1, const int *vxy);

	// Intersect outer & inner rectangle, write out what it separates into
int clip4(int *rect04, int xo, int yo, int wo, int ho, int xi, int yi, int wi, int hi);

/// Line iterator

/* Indices 0 and 1 are current X and Y, 2 is number of pixels remaining */
typedef int linedata[10];

void line_init(linedata line, int x0, int y0, int x1, int y1);
int line_step(linedata line);
void line_nudge(linedata line, int x, int y);
int line_clip(linedata line, const int *vxy, int *step);
void line_flip(linedata line);

/// Procedures

//	Add one more frame to a frameset
int mem_add_frame(frameset *fset, int w, int h, int bpp, int cmask, png_color *pal);
//	Remove specified frame from a frameset
void mem_remove_frame(frameset *fset, int frame);
//	Empty a frameset
void mem_free_frames(frameset *fset);

void init_istate(image_state *state, image_info *image);	// Set initial state of image variables
void mem_replace_filename(int layer, char *fname);	// Change layer's filename
void mem_file_modified(char *fname);	// Label file's frames in current layer as changed
int init_undo(undo_stack *ustack, int depth);	// Create new undo stack of a given depth
void update_undo_depth();	// Resize all undo stacks

void mem_free_chanlist(chanlist img);
int cmask_from(chanlist img);	// Chanlist to cmask

int mem_count_all_cols();			// Count all colours - Using main image
int mem_count_all_cols_real(unsigned char *im, int w, int h);	// Count all colours - very memory greedy

int mem_cols_used(int max_count);		// Count colours used in main RGB image
int mem_cols_used_real(unsigned char *im, int w, int h, int max_count, int prog);
			// Count colours used in RGB chunk and dump to found table
void mem_cols_found(png_color *userpal);	// Convert colours list into palette


int read_hex( char in );			// Convert character to hex value 0..15.  -1=error
int read_hex_dub( char *in );			// Read hex double

#define FREE_IMAGE 1
#define FREE_UNDO  2
#define FREE_ALL   3

//	Clear/remove image data
void mem_free_image(image_info *image, int mode);

#define AI_COPY   1 /* Duplicate source channels, not insert them */
#define AI_NOINIT 2 /* Do not initialize source-less channels */
#define AI_CLEAR  4 /* Initialize image structure first */

//	Allocate new image data
int mem_alloc_image(int mode, image_info *image, int w, int h, int bpp,
	int cmask, image_info *src);
//	Allocate space for new image, removing old if needed
int mem_new( int width, int height, int bpp, int cmask );
//	Allocate new clipboard, removing or preserving old as needed
int mem_clip_new(int width, int height, int bpp, int cmask, int backup);

int load_def_palette(char *name);
int load_def_patterns(char *name);
void mem_init();				// Initialise memory

//	Return the number of bytes used in image + undo stuff
size_t mem_used();
//	Return the number of bytes used in image + undo in all layers
size_t mem_used_layers();

#define FX_EDGE       0
#define FX_EMBOSS     2
#define FX_SHARPEN    3
#define FX_SOFTEN     4
#define FX_SOBEL      5
#define FX_PREWITT    6
#define FX_GRADIENT   7
#define FX_ROBERTS    8
#define FX_LAPLACE    9
#define FX_KIRSCH    10
#define FX_ERODE     11
#define FX_DILATE    12
#define FX_MORPHEDGE 13

void do_effect( int type, int param );		// 0=edge detect 1=UNUSED 2=emboss
void mem_bacteria( int val );			// Apply bacteria effect val times the canvas area
void mem_gauss(double radiusX, double radiusY, int gcor);
void mem_unsharp(double radius, double amount, int threshold, int gcor);
void mem_dog(double radiusW, double radiusN, int norm, int gcor);
void mem_kuwahara(int r, int gcor, int detail);

/* Colorspaces */
#define CSPACE_RGB  0
#define CSPACE_SRGB 1
#define CSPACE_LXN  2
#define NUM_CSPACES 3

/* Distance measures */
#define DIST_LINF 0 /* Largest absolute difference (Linf measure) */
#define DIST_L1   1 /* Sum of absolute differences (L1 measure) */
#define DIST_L2   2 /* Euclidean distance (L2 measure) */

/// PALETTE PROCS

void mem_pal_load_def();		// Load default palette

#define mem_pal_copy(A, B) memcpy((A), (B), SIZEOF_PALETTE)
void mem_pal_init();			// Initialise whole of palette RGB
void mem_greyscale(int gcor);		// Convert image to greyscale
void do_convert_rgb(int start, int step, int cnt, unsigned char *dest,
	unsigned char *src);
int mem_convert_rgb();			// Convert image to RGB
int mem_convert_indexed();		// Convert image to Indexed Palette
//	Quantize image using Max-Min algorithm
int maxminquan(unsigned char *inbuf, int width, int height, int quant_to,
	png_color *userpal);
//	Quantize image using PNN algorithm
int pnnquan(unsigned char *inbuf, int width, int height, int quant_to,
	png_color *userpal);
//	Convert RGB->indexed using error diffusion with variety of options
int mem_dither(unsigned char *old, int ncols, short *dither, int cspace,
	int dist, int limit, int selc, int serpent, int rgb8b, double emult);
//	Do the same in dumb but fast way
int mem_dumb_dither(unsigned char *old, unsigned char *new, png_color *pal,
	int width, int height, int ncols, int dither);
//	Set up colors A, B, and pattern for dithering a given RGB color
void mem_find_dither(int red, int green, int blue); 
//	Convert image to Indexed Palette using quantize
int mem_quantize( unsigned char *old_mem_image, int target_cols, int type );
void mem_invert();			// Invert the palette

void mem_mask_setall(char val);		// Clear/set all masks
void mem_mask_init();			// Initialise RGB protection mask
int mem_protected_RGB(int intcol);	// Is this intcol in list?

void mem_swap_cols(int redraw);		// Swaps colours and update memory
void mem_get_histogram(int channel);	// Calculate how many of each colour index is on the canvas
int scan_duplicates();			// Find duplicate palette colours
void remove_duplicates();		// Remove duplicate palette colours - call AFTER scan_duplicates
int mem_remove_unused_check();		// Check to see if we can remove unused palette colours
int mem_remove_unused();		// Remove unused palette colours
void mem_bw_pal(png_color *pal, int i1, int i2); // Generate black-to-white palette
//	Create colour-transformed palette
void transform_pal(png_color *pal1, png_color *pal2, int p1, int p2);
void mem_pal_sort( int a, int i1, int i2, int rev );
					// Sort colours in palette 0=luminance, 1=RGB

void mem_pal_index_move( int c1, int c2 );	// Move index c1 to c2 and shuffle in between up/down
void mem_canvas_index_move( int c1, int c2 );	// Similar to palette item move but reworks canvas pixels

void set_zoom_centre( int x, int y );

// Nonclassical HSV: H is 0..6, S is 0..1, V is 0..255
void rgb2hsv(unsigned char *rgb, double *hsv);
void hsv2rgb(unsigned char *rgb, double *hsv);

//// UNDO

#define UNDO_PAL   0	/* Palette changes */
#define UNDO_XPAL  1	/* Palette and indexed image changes */
#define UNDO_COL   2	/* Palette and/or RGB image changes */
#define UNDO_DRAW  3	/* Changes to current channel / RGBA */
#define UNDO_INV   4	/* "Invert" operation */
#define UNDO_XFORM 5	/* Changes to all channels */
#define UNDO_FILT  6	/* Changes to current channel */
#define UNDO_PASTE 7	/* Paste operation (current / RGBA) */
#define UNDO_TOOL  8	/* Same as UNDO_DRAW but respects pen_down */

void mem_undo_next(int mode);	// Call this after a draw event but before any changes to image
//	 Get address of previous channel data (or current if none)
unsigned char *mem_undo_previous(int channel);
void mem_undo_prepare();	// Call this after changes to image, to compress last frame

void mem_undo_backward();		// UNDO requested by user
void mem_undo_forward();		// REDO requested by user

#define UC_CREATE  0x01	/* Force create */
#define UC_NOCOPY  0x02	/* Forbid copy */
#define UC_DELETE  0x04	/* Force delete */
#define UC_PENDOWN 0x08	/* Respect pen_down */
#define UC_GETMEM  0x10 /* Get memory and do nothing */

int undo_next_core(int mode, int new_width, int new_height, int new_bpp, int cmask);
void update_undo(image_info *image);	// Copy image state into current undo frame
//	Try to allocate a memory block, releasing undo frames if needed
void *mem_try_malloc(size_t size);

//// Drawing Primitives

int sb_rect[4];				// Backbuffer placement
int init_sb();				// Create shapeburst backbuffer
void render_sb();			// Render from shapeburst backbuffer

int mem_clip_mask_init(unsigned char val);		// Initialise the clipboard mask
//	Extract alpha info from RGB clipboard
int mem_scale_alpha(unsigned char *img, unsigned char *alpha,
	int width, int height, int mode);
void mem_mask_colors(unsigned char *mask, unsigned char *img, unsigned char v,
	int width, int height, int bpp, int col0, int col1);
void mem_clip_mask_set(unsigned char val);		// Mask colours A and B on the clipboard
void mem_clip_mask_clear();				// Clear the clipboard mask

void do_clone(int ox, int oy, int nx, int ny, int opacity, int mode);
#define mem_smudge(A, B, C, D) do_clone((A), (B), (C), (D), tool_opacity / 2, \
	smudge_mode && mem_undo_opacity)
#define mem_clone(A, B, C, D) do_clone((A), (B), (C), (D), tool_opacity, \
	mem_undo_opacity)

//	Apply colour transform
void do_transform(int start, int step, int cnt, unsigned char *mask,
	unsigned char *imgr, unsigned char *img0);

void mem_flip_v(char *mem, char *tmp, int w, int h, int bpp);	// Flip image vertically
void mem_flip_h( char *mem, int w, int h, int bpp );		// Flip image horizontally
int mem_sel_rot( int dir );					// Rotate clipboard 90 degrees
int mem_image_rot( int dir );					// Rotate canvas 90 degrees

//	Get new image geometry of rotation. angle = degrees
void mem_rotate_geometry(int ow, int oh, double angle, int *nw, int *nh);
//	Rotate canvas or clipboard by any angle (degrees)
int mem_rotate_free(double angle, int type, int gcor, int clipboard);
void mem_rotate_free_real(chanlist old_img, chanlist new_img, int ow, int oh,
	int nw, int nh, int bpp, double angle, int mode, int gcor, int dis_a,
	int silent);

#define BOUND_MIRROR 0 /* Mirror image beyond edges */
#define BOUND_TILE   1 /* Tiled image beyond edges */
#define BOUND_VOID   2 /* Transparency beyond edges */

//	Scale image
int mem_image_scale(int nw, int nh, int type, int gcor, int sharp, int bound);
int mem_image_scale_real(chanlist old_img, int ow, int oh, int bpp,
	chanlist new_img, int nw, int nh, int type, int gcor, int sharp);
int mem_image_resize(int nw, int nh, int ox, int oy, int mode);	// Resize image

int mem_isometrics(int type);

void mem_threshold(unsigned char *img, int len, int level);	// Threshold channel values
void mem_demultiply(unsigned char *img, unsigned char *alpha, int len, int bpp);

void set_xlate(unsigned char *xlat, int bpp);			// Build bitdepth translation table
int is_filled(unsigned char *data, unsigned char val, int len);	// Check if byte array is all one value

void flood_fill( int x, int y, unsigned int target );

void sline( int x1, int y1, int x2, int y2 );			// Draw single thickness straight line
void tline( int x1, int y1, int x2, int y2, int size );		// Draw size thickness straight line
void g_para( int x1, int y1, int x2, int y2, int xv, int yv );	// Draw general parallelogram
void f_rectangle( int x, int y, int w, int h );			// Draw a filled rectangle
void f_circle( int x, int y, int r );				// Draw a filled circle
void mem_ellipse( int x1, int y1, int x2, int y2, int thick );	// Thickness 0 means filled

// Draw whatever is bounded by two pairs of lines
void draw_quad(linedata line1, linedata line2, linedata line3, linedata line4);

//	A couple of shorthands to get an int representation of an RGB colour
#define PNG_2_INT(P) (((P).red << 16) + ((P).green << 8) + ((P).blue))
#define MEM_2_INT(M,I) (((M)[(I)] << 16) + ((M)[(I) + 1] << 8) + (M)[(I) + 2])
#define INT_2_R(A) ((A) >> 16)
#define INT_2_G(A) (((A) >> 8) & 0xFF)
#define INT_2_B(A) ((A) & 0xFF)
#define RGB_2_INT(R,G,B) (((R) << 16) + ((G) << 8) + (B))

#define MEM_BPP (mem_channel == CHN_IMAGE ? mem_img_bpp : 1)
#define BPP(x) ((x) == CHN_IMAGE ? mem_img_bpp : 1)
#define IS_INDEXED ((mem_channel == CHN_IMAGE) && (mem_img_bpp == 1))

/* Whether process_img() needs extra buffer passed in */
#define NEED_XBUF_DRAW (mem_blend && (blend_mode & BLEND_MMASK))
#define NEED_XBUF_PASTE (NEED_XBUF_DRAW || \
	((mem_channel == CHN_IMAGE) && (mem_clip_bpp < mem_img_bpp)))

void prep_mask(int start, int step, int cnt, unsigned char *mask,
	unsigned char *mask0, unsigned char *img0);
void process_mask(int start, int step, int cnt, unsigned char *mask,
	unsigned char *alphar, unsigned char *alpha0, unsigned char *alpha,
	unsigned char *trans, int opacity, int noalpha);
void process_img(int start, int step, int cnt, unsigned char *mask,
	unsigned char *imgr, unsigned char *img0, unsigned char *img,
	unsigned char *xbuf, int sourcebpp, int destbpp);
void copy_area(image_info *dest, image_info *src, int x, int y);

// Retroactive masking - by blending with undo frame
void mask_merge(unsigned char *old, int channel, unsigned char *mask);

int pixel_protected(int x, int y);				// generic
void row_protected(int x, int y, int len, unsigned char *mask);
void put_pixel_def( int x, int y );				// generic
void put_pixel_row_def(int x, int y, int len, unsigned char *xsel); // generic
int get_pixel( int x, int y );					// generic
int get_pixel_RGB( int x, int y );				// converter
int get_pixel_img( int x, int y );				// from image

int grad_value(int *dest, int slot, double x);
void grad_pixels(int start, int step, int cnt, int x, int y, unsigned char *mask,
	unsigned char *op0, unsigned char *img0, unsigned char *alpha0);
void grad_update(grad_info *grad);
void gmap_setup(grad_map *gmap, grad_store gstore, int slot);
void grad_def_update(int slot);

#define GRAD_CUSTOM_DATA(X) ((X) ? GRAD_POINTS * ((X) * 4 + 2) : 0)
#define GRAD_CUSTOM_DMAP(X) (GRAD_POINTS * ((X) * 4 + 3))
#define GRAD_CUSTOM_OPAC(X) (GRAD_POINTS * ((X) * 4 + 4))
#define GRAD_CUSTOM_OMAP(X) (GRAD_POINTS * ((X) * 4 + 5))

void blend_indexed(int start, int step, int cnt, unsigned char *rgb,
	unsigned char *img0, unsigned char *img,
	unsigned char *alpha0, unsigned char *alpha, int opacity);

//	Select colors nearest to A->B gradient
int mem_pick_gradient(unsigned char *buf, int cspace, int mode);

int mem_skew(double xskew, double yskew, int type, int gcor);

int average_pixels(unsigned char *rgb, int iw, int ih, int x, int y, int w, int h);

#define IF_IN_RANGE( x, y ) if ( x>=0 && y>=0 && x<mem_width && y<mem_height )

#define mtMIN(a,b,c) if ( b<c ) a=b; else a=c;
#define mtMAX(a,b,c) if ( b>c ) a=b; else a=c;

/*
 * Win32 libc (MSVCRT.DLL) violates the C standard as to malloc()'ed memory
 * alignment; this macro serves as part of a workaround for that problem
 */
#ifdef WIN32
/* In Win32, pointers fit into ints */
#define ALIGN(p) ((void *)(((int)(p) + sizeof(double) - 1) & (-sizeof(double))))
#else
#define ALIGN(p) ((void *)(p))
#endif

/* Reasonably portable pointer alignment macro */

#define ALIGNED(P, N) ((void *)((char *)(P) + \
	((~(unsigned)((char *)(P) - (char *)0) + 1) & ((N) - 1))))

/* Get preferred alignment of a type */

#ifdef __GNUC__ /* Have native implementation */
#define ALIGNOF(X) __alignof__(X)
#else
#include <stddef.h> /* For offsetof() */
#define ALIGNOF(X) offsetof(struct {char c; X x;}, x)
#endif

/* x87 FPU uses long doubles internally, which may cause calculation results
 * to depend on emitted assembly code, and change in mysterious ways depending
 * on source structure and current optimizations.
 * http://gcc.gnu.org/bugzilla/show_bug.cgi?id=323
 * SSE math works with doubles and floats natively, so is free from this
 * instability, while a bit less precise.
 * We aren't requiring C99 yet, so use GCC's define instead of the C99-standard
 * "FLT_EVAL_METHOD" from float.h for deciding which mode is used.
 */

#undef NATIVE_DOUBLES
#if defined(__FLT_EVAL_METHOD__) && ((__FLT_EVAL_METHOD__ == 0) || (__FLT_EVAL_METHOD__ == 1))
#define NATIVE_DOUBLES
#endif

/*
 * rint() function rounds halfway cases (0.5 etc.) to even, which may cause
 * weird results in geometry calculations. And straightforward (int)(X + 0.5)
 * may be affected by double-rounding issues on x87 FPU - and in case floor()
 * is implemented as compiler intrinsic, the same can happen with it too.
 * These macros are for when neither is acceptable.
 */
#ifndef NATIVE_DOUBLES /* Have extra precision */
#define WJ_ROUND(I,X) \
{						\
	const volatile double RounD___ = (X);	\
	(I) = (int)(RounD___ + 0.5);		\
}
#define WJ_FLOOR(I,X) \
{						\
	const volatile double RounD___ = (X);	\
	(I) = floor(RounD___);			\
}
#else /* Doubles are doubles */
#define WJ_ROUND(I,X) (I) = (int)((X) + 0.5)
#define WJ_FLOOR(I,X) (I) = floor(X)
#endif

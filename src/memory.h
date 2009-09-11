/*	memory.h
	Copyright (C) 2004-2007 Mark Tyler and Dmitry Groshev

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

/// Definitions, structures & variables

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

typedef unsigned char *chanlist[NUM_CHANNELS];

#define UNDO_TILEMAP_SIZE 32

typedef struct
{
	chanlist img;
	png_color pal[256];
	unsigned char tilemap[UNDO_TILEMAP_SIZE], *tileptr;
	int cols, width, height, bpp, flags, size;
} undo_item;

/// GRADIENTS

#define MAX_GRAD 65536
#define GRAD_POINTS 256

typedef struct {
	/* Base values */
	int status, x1, y1, x2, y2;	// Gradient placement tool
	int len, rep, ofs;	// Gradient length, repeat, and offset
	int gmode, rmode;	// Gradient mode and repeat mode
	/* Derived values */
	double wrep, wil1, wil2, xv, yv;
	int wmode;
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
#define GRAD_MODE_LINEAR   1
#define GRAD_MODE_BILINEAR 2
#define GRAD_MODE_RADIAL   3
#define GRAD_MODE_SQUARE   4

/* Boundary conditions */
#define GRAD_BOUND_STOP    0
#define GRAD_BOUND_LEVEL   1
#define GRAD_BOUND_REPEAT  2
#define GRAD_BOUND_MIRROR  3

/* Gradient types */
#define GRAD_TYPE_RGB      0
#define GRAD_TYPE_HSV      1
#define GRAD_TYPE_BK_HSV   2
#define GRAD_TYPE_CONST    3
#define GRAD_TYPE_CUSTOM   4

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
#define BLEND_NORMAL 0
#define BLEND_HUE    1
#define BLEND_SAT    2
#define BLEND_VALUE  3
#define BLEND_COLOR  4
#define BLEND_SATPP  5

#define BLEND_NMODES 6
#define BLEND_MMASK    0x7F
#define BLEND_REVERSE  0x80
#define BLEND_RGBSHIFT 8

/// FLOOD FILL SETTINGS

double flood_step;
int flood_cube, flood_img, flood_slide;

int smudge_mode;

/// IMAGE

char mem_filename[256];			// File name of file loaded/saved
chanlist mem_img;			// Array of pointers to image channels
int mem_channel;			// Current active channel
int mem_img_bpp;			// Bytes per pixel = 1 or 3
int mem_changed;			// Changed since last load/save flag 0=no, 1=changed
int mem_width, mem_height;		// Current image geometry
float mem_icx, mem_icy;			// Current centre x,y
int mem_ics;				// Has the centre been set by the user? 0=no 1=yes

unsigned char *mem_clipboard;		// Pointer to clipboard data
unsigned char *mem_clip_mask;		// Pointer to clipboard mask
unsigned char *mem_clip_alpha;		// Pointer to clipboard alpha
extern unsigned char mem_brushes[];	// Preset brushes image
int brush_tool_type;			// Last brush tool type
int mem_brush_list[81][3];		// Preset brushes parameters
int mem_clip_bpp;			// Bytes per pixel
int mem_clip_w, mem_clip_h;		// Clipboard geometry
int mem_clip_x, mem_clip_y;		// Clipboard location on canvas
int mem_nudge;				// Nudge pixels per SHIFT+Arrow key during selection/paste

int mem_preview;			// Preview an RGB change
int mem_prev_bcsp[6];			// BR, CO, SA, POSTERIZE, GAMMA, Hue

#define MAX_UNDO 101			// Maximum number of undo levels + 1

undo_item mem_undo_im_[MAX_UNDO];	// Pointers to undo images + current image being edited

int mem_undo_pointer;		// Pointer to currently used image on canvas/screen
int mem_undo_done;		// Undo images that we have behind current image (i.e. possible UNDO)
int mem_undo_redo;		// Undo images that we have ahead of current image (i.e. possible REDO)
int mem_undo_limit;		// Max MB memory allocation limit
int mem_undo_opacity;		// Use previous image for opacity calculations?

/// GRID

int mem_show_grid, mem_grid_min;	// Boolean show toggle & minimum zoom to show it at
unsigned char mem_grid_rgb[3];		// RGB colour of grid

/// PATTERNS

char mem_patterns[81][8][8];		// Pattern bitmaps
unsigned char *mem_pattern;		// Original 0-1 pattern
unsigned char mem_col_pat[8 * 8];	// Indexed 8x8 colourised pattern using colours A & B
unsigned char mem_col_pat24[8 * 8 * 3];	// RGB 8x8 colourised pattern using colours A & B

/// PREVIEW/TOOLS

int tool_type, tool_size, tool_flow;	// Currently selected tool
int tool_opacity;			// Opacity - 255 = solid
int tool_pat;				// Tool pattern number
int pen_down;				// Are we drawing? - Used to see if we need to do an UNDO
int tool_ox, tool_oy;			// Previous tool coords - used by continuous mode
int mem_continuous;			// Area we painting the static shapes continuously?

int mem_brcosa_allow[3];		// BRCOSA RGB

/// FILE

int mem_xpm_trans;			// Current XPM file transparency colour index
int mem_xbm_hot_x, mem_xbm_hot_y;	// Current XBM hot spot

/// PALETTE

png_color mem_pal[256];			// RGB entries for all 256 palette colours
png_color mem_pal_def[256];		// Default palette entries for new image
int mem_pal_def_i;			// Items in default palette
int mem_cols;				// Number of colours in the palette: 2..256 or 0 for no image
extern unsigned char mem_pals[];	// RGB screen memory holding current palette
char mem_prot_mask[256];		// 256 bytes used for indexed images
int mem_prot_RGB[256];			// Up to 256 RGB colours protected
int mem_prot;				// 0..256 : Number of protected colours in mem_prot_RGB

int mem_col_[2];			// Index for colour A & B
#define mem_col_A mem_col_[0]
#define mem_col_B mem_col_[1]
png_color mem_col_24[2];		// RGB for colour A & B
#define mem_col_A24 mem_col_24[0]
#define mem_col_B24 mem_col_24[1]
int mem_background;			// Non paintable area
int mem_histogram[256];

/// Line iterator

/* Indices 0 and 1 are current X and Y, 2 is number of pixels remaining */
typedef int linedata[10];

void line_init(linedata line, int x0, int y0, int x1, int y1);
int line_step(linedata line);
void line_nudge(linedata line, int x, int y);

/// Procedures

void init_istate();	// Set initial state of image variables

int mem_count_all_cols();			// Count all colours - Using main image
int mem_count_all_cols_real(unsigned char *im, int w, int h);	// Count all colours - very memory greedy

int mem_cols_used(int max_count);		// Count colours used in main RGB image
int mem_cols_used_real(unsigned char *im, int w, int h, int max_count, int prog);
			// Count colours used in RGB chunk and dump to found table
void mem_cols_found_dl(unsigned char userpal[3][256]);		// Convert results ready for DL code


int read_hex( char in );			// Convert character to hex value 0..15.  -1=error
int read_hex_dub( char *in );			// Read hex double

void mem_clear();				// Remove old image if any
//	Allocate space for new image, removing old if needed
int mem_new( int width, int height, int bpp, int cmask );
void mem_init();				// Initialise memory

int mem_used();				// Return the number of bytes used in image + undo stuff
int mem_used_layers();			// Return the number of bytes used in image + undo in all layers

void mem_bacteria( int val );			// Apply bacteria effect val times the canvas area
void do_effect( int type, int param );		// 0=edge detect 1=UNUSED 2=emboss
void mem_gauss(double radiusX, double radiusY, int gcor);
void mem_unsharp(double radius, double amount, int threshold, int gcor);

/// PALETTE PROCS

int mem_load_pal( char *file_name, png_color *pal );	// Load file into palette array >1 => cols read
void mem_pal_load_def();		// Load default palette

#define mem_pal_copy(A, B) memcpy((A), (B), sizeof(png_color) * 256)
void mem_pal_init();			// Initialise whole of palette RGB
int mem_pal_cmp( png_color *pal1,	// Count itentical palette entries
	png_color *pal2 );
void mem_greyscale(int gcor);		// Convert image to greyscale
int mem_convert_rgb();			// Convert image to RGB
int mem_convert_indexed();		// Convert image to Indexed Palette
//	Quantize image using Max-Min algorithm
int maxminquan(unsigned char *inbuf, int width, int height, int quant_to,
	unsigned char userpal[3][256]);
//	Convert RGB->indexed using error diffusion with variety of options
int mem_dither(unsigned char *old, int ncols, short *dither, int cspace,
	int dist, int limit, int selc, int serpent, int rgb8b, double emult);
int mem_quantize( unsigned char *old_mem_image, int target_cols, int type );
					// Convert image to Indexed Palette using quantize
void mem_invert();			// Invert the palette

void mem_mask_setall(char val);		// Clear/set all masks
void mem_mask_init();			// Initialise RGB protection mask
int mem_protected_RGB(int intcol);	// Is this intcol in list?

void mem_swap_cols();			// Swaps colours and update memory
void repaint_swatch( int index );	// Update a palette swatch
void mem_get_histogram(int channel);	// Calculate how many of each colour index is on the canvas
int do_posterize(int val, int posty);	// Posterize a number
int scan_duplicates();			// Find duplicate palette colours
void remove_duplicates();		// Remove duplicate palette colours - call AFTER scan_duplicates
int mem_remove_unused_check();		// Check to see if we can remove unused palette colours
int mem_remove_unused();		// Remove unused palette colours
void mem_scale_pal(png_color *pal, int i1, int r1, int g1, int b1,
	int i2, int r2, int g2, int b2); // Generate a scaled palette
//	Create colour-transformed palette
void transform_pal(png_color *pal1, png_color *pal2, int p1, int p2);
void mem_pal_sort( int a, int i1, int i2, int rev );
					// Sort colours in palette 0=luminance, 1=RGB

void mem_pal_index_move( int c1, int c2 );	// Move index c1 to c2 and shuffle in between up/down
void mem_canvas_index_move( int c1, int c2 );	// Similar to palette item move but reworks canvas pixels

void set_zoom_centre( int x, int y );

// Nonclassical HSV: H is 0..6, S is 0.. 1, V is 0..255
void rgb2hsv(unsigned char *rgb, double *hsv);

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


int undo_free_x(undo_item *undo);
int mem_undo_next(int mode);		// Call this after a draw event but before any changes to image
//	 Get address of previous channel data (or current if none)
unsigned char *mem_undo_previous(int channel);
void mem_undo_prepare();	// Call this after changes to image, to compress last frame

void mem_undo_backward();		// UNDO requested by user
void mem_undo_forward();		// REDO requested by user

#define UC_CREATE  1	/* Force create */
#define UC_NOCOPY  2	/* Forbid copy */
#define UC_DELETE  4	/* Force delete */
#define UC_PENDOWN 8	/* Respect pen_down */

int undo_next_core(int mode, int new_width, int new_height, int new_bpp, int cmask);


//// Drawing Primitives

int mem_clip_mask_init(unsigned char val);		// Initialise the clipboard mask
//	Extract alpha info from RGB clipboard
int mem_scale_alpha(unsigned char *img, unsigned char *alpha,
	int width, int height, int mode);
void mem_mask_colors(unsigned char *mask, unsigned char *img, unsigned char v,
	int width, int height, int bpp, int col0, int col1);
void mem_clip_mask_set(unsigned char val);		// Mask colours A and B on the clipboard
void mem_clip_mask_clear();				// Clear the clipboard mask

void do_clone(int ox, int oy, int nx, int ny, int opacity, int mode);
#define mem_smudge(A, B, C, D) do_clone((A), (B), (C), (D), MEM_BPP == 3 ? \
	tool_opacity / 2 : 127, smudge_mode && mem_undo_opacity)
#define mem_clone(A, B, C, D) do_clone((A), (B), (C), (D), MEM_BPP == 3 ? \
	tool_opacity : -1, mem_undo_opacity)

//	Apply colour transform
void do_transform(int start, int step, int cnt, unsigned char *mask,
	unsigned char *imgr, unsigned char *img0);

void mem_flip_v(char *mem, char *tmp, int w, int h, int bpp);	// Flip image vertically
void mem_flip_h( char *mem, int w, int h, int bpp );		// Flip image horizontally
int mem_sel_rot( int dir );					// Rotate clipboard 90 degrees
int mem_image_rot( int dir );					// Rotate canvas 90 degrees
int mem_rotate_free(double angle, int type, int gcor);		// Rotate canvas by any angle (degrees)
int mem_image_scale(int nw, int nh, int type, int gcor, int sharp);	// Scale image
int mem_image_resize(int nw, int nh, int ox, int oy, int mode);	// Resize image

int mem_isometrics(int type);

void mem_threshold(int channel, int level);			// Threshold channel values
void mem_demultiply(unsigned char *img, unsigned char *alpha, int len, int bpp);

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
#define PNG_2_INT(var) ((var.red << 16) + (var.green << 8) + (var.blue))
#define MEM_2_INT(M,I) (((M)[(I)] << 16) + ((M)[(I) + 1] << 8) + (M)[(I) + 2])
#define INT_2_R(A) ((A) >> 16)
#define INT_2_G(A) (((A) >> 8) & 0xFF)
#define INT_2_B(A) ((A) & 0xFF)
#define RGB_2_INT(R,G,B) (((R) << 16) + ((G) << 8) + (B))

#define MEM_BPP (mem_channel == CHN_IMAGE ? mem_img_bpp : 1)
#define BPP(x) ((x) == CHN_IMAGE ? mem_img_bpp : 1)

#define GET_PIXEL(x,y) (mem_img[CHN_IMAGE][(x) + (y) * mem_width])

#define POSTERIZE_MACRO res = 0.49 + ( ((1 << posty) - 1) * ((float) res)/255);\
			res = 0.49 + 255 * ( ((float) res) / ((1 << posty) - 1) );

void prep_mask(int start, int step, int cnt, unsigned char *mask,
	unsigned char *mask0, unsigned char *img0);
void process_mask(int start, int step, int cnt, unsigned char *mask,
	unsigned char *alphar, unsigned char *alpha0, unsigned char *alpha,
	unsigned char *trans, int opacity, int noalpha);
void process_img(int start, int step, int cnt, unsigned char *mask,
	unsigned char *imgr, unsigned char *img0, unsigned char *img,
	int opacity, int sourcebpp);
void paste_pixels(int x, int y, int len, unsigned char *mask, unsigned char *img,
	unsigned char *alpha, unsigned char *trans, int opacity);

int pixel_protected(int x, int y);				// generic
void row_protected(int x, int y, int len, unsigned char *mask);
void put_pixel( int x, int y );					// generic
int get_pixel( int x, int y );					// generic
int get_pixel_RGB( int x, int y );				// converter
int get_pixel_img( int x, int y );				// from image

int grad_value(int *dest, int slot, double x);
int grad_pixel(unsigned char *dest, int x, int y);
void grad_update(grad_info *grad);
void gmap_setup(grad_map *gmap, grad_store gstore, int slot);
void grad_def_update();
void prep_grad(int start, int step, int cnt, int x, int y, unsigned char *mask,
	unsigned char *op0, unsigned char *img0, unsigned char *alpha0);

#define GRAD_CUSTOM_DATA(X) ((X) ? GRAD_POINTS * ((X) * 4 + 2) : 0)
#define GRAD_CUSTOM_DMAP(X) (GRAD_POINTS * ((X) * 4 + 3))
#define GRAD_CUSTOM_OPAC(X) (GRAD_POINTS * ((X) * 4 + 4))
#define GRAD_CUSTOM_OMAP(X) (GRAD_POINTS * ((X) * 4 + 5))

void blend_channel(int start, int step, int cnt, unsigned char *mask,
	unsigned char *dest, unsigned char *src, int opacity);
void blend_indexed(int start, int step, int cnt, unsigned char *rgb,
	unsigned char *img0, unsigned char *img,
	unsigned char *alpha0, unsigned char *alpha, int opacity);


#define IF_IN_RANGE( x, y ) if ( x>=0 && y>=0 && x<mem_width && y<mem_height )

#define mtMIN(a,b,c) if ( b<c ) a=b; else a=c;
#define mtMAX(a,b,c) if ( b>c ) a=b; else a=c;

/*
 * Win32 libc (MSVCRT.DLL) violates the C standard as to malloc()'ed memory
 * alignment; this macro serves as part of a workaround for that problem
 */
#ifdef WIN32
#define ALIGNTO(p,s) ((void *)(((int)(p) + sizeof(s) - 1) & (-sizeof(s))))
#else
#define ALIGNTO(p,s) ((void *)(p))
#endif

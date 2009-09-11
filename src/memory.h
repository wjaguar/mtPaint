/*	memory.h
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

#include <png.h>

/// Definitions, structures & variables

#define PALETTE_WIDTH 74
#define PALETTE_HEIGHT 4500
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

typedef struct
{
	chanlist img;
	png_color pal[256];
	int cols, width, height, bpp;
} undo_item;

/// Tint tool - contributed by Dmitry Groshev, January 2006

int tint_mode[3];			// [0] = off/on, [1] = add/subtract, [2] = button (none, left, middle, right : 0-3)

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
unsigned char *mem_brushes;		// Preset brushes image
int brush_tool_type;			// Last brush tool type
int mem_brush_list[81][3];		// Preset brushes parameters
char mem_clip_file[2][256];		// 0=Current filename, 1=temp filename
int mem_clip_bpp;			// Bytes per pixel
int mem_clip_w, mem_clip_h;		// Clipboard geometry
int mem_clip_x, mem_clip_y;		// Clipboard location on canvas
int mem_nudge;				// Nudge pixels per SHIFT+Arrow key during selection/paste

int mem_preview;			// Preview an RGB change
int mem_prev_bcsp[5];			// BR, CO, SA, POSTERIZE, GAMMA

#define MAX_UNDO 101			// Maximum number of undo levels + 1

undo_item mem_undo_im_[MAX_UNDO];	// Pointers to undo images + current image being edited
// undo_item mem_undo_im[MAX_UNDO];	// Pointers to undo images + current image being edited

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
unsigned char *mem_col_pat;		// Indexed 8x8 colourised pattern using colours A & B
unsigned char *mem_col_pat24;		// RGB 8x8 colourised pattern using colours A & B

/// PREVIEW/TOOLS

int tool_type, tool_size, tool_flow;	// Currently selected tool
int tool_opacity;			// Opacity - 255 = solid
int tool_pat;				// Tool pattern number
int tool_fixx, tool_fixy;		// Fixate on axis
int pen_down;				// Are we drawing? - Used to see if we need to do an UNDO
int tool_ox, tool_oy;			// Previous tool coords - used by continuous mode
int mem_continuous;			// Area we painting the static shapes continuously?

int mem_brcosa_allow[3];		// BRCOSA RGB

/// FILE

int mem_xpm_trans;			// Current XPM file transparency colour index
int mem_xbm_hot_x, mem_xbm_hot_y;	// Current XBM hot spot
int mem_jpeg_quality;			// JPEG quality setting

/// PALETTE

png_color mem_pal[256];			// RGB entries for all 256 palette colours
png_color mem_pal_def[256];		// Default palette entries for new image
int mem_pal_def_i;			// Items in default palette
int mem_cols;				// Number of colours in the palette: 2..256 or 0 for no image
char *mem_pals;				// RGB screen memory holding current palette
char mem_prot_mask[256];		// 256 bytes used for indexed images
int mem_prot_RGB[256];			// Up to 256 RGB colours protected
int mem_prot;				// 0..256 : Number of protected colours in mem_prot_RGB

int mem_col_A, mem_col_B;		// Index for colour A & B
png_color mem_col_A24, mem_col_B24;	// RGB for colour A & B
int mem_background;			// Non paintable area
int mem_histogram[256];

/// Procedures

int mem_count_all_cols();			// Count all colours - Using main image
int mem_count_all_cols_real(unsigned char *im, int w, int h);	// Count all colours - very memory greedy

int mem_cols_used(int max_count);		// Count colours used in main RGB image
int mem_cols_used_real(unsigned char *im, int w, int h, int max_count, int prog);
			// Count colours used in RGB chunk and dump to found table
void mem_cols_found_dl(unsigned char userpal[3][256]);		// Convert results ready for DL code


int get_next_line(char *input, int length, FILE *fp);		// Get next length chars of text file
int mt_round( float n );			// Round a float to nearest whole number
int check_str( int max, char *a, char *b );	// Compare up to max characters of 2 strings
						// Case insensitive
int read_hex( char in );			// Convert character to hex value 0..15.  -1=error
char get_hex( int in );				// Turn 0..15 into hex
int read_hex_dub( char *in );			// Read hex double
void clear_file_flags();		// Reset various file flags, e.g. XPM/XBM after new/load gif etc

char *grab_memory( int size, char byte );	// Malloc memory, reset all bytes
void mem_clear();				// Remove old image if any
int mem_new( int width, int height, int bpp );	// Allocate space for new image, removing old if needed
int valid_file( char *filename );		// Can this file be opened for reading?
void mem_init();				// Initialise memory

int mem_used();				// Return the number of bytes used in image + undo stuff
int mem_used_layers();			// Return the number of bytes used in image + undo in all layers

void mem_bacteria( int val );			// Apply bacteria effect val times the canvas area
void do_effect( int type, int param );		// 0=edge detect 1=blur 2=emboss

/// PALETTE PROCS

int mem_load_pal( char *file_name, png_color *pal );	// Load file into palette array >1 => cols read
void mem_pal_load_def();		// Load default palette

void mem_pal_init();			// Initialise whole of palette RGB
void mem_pal_copy( png_color *pal1,	// Palette 1 = Palette 2
	png_color *pal2 );
int mem_pal_cmp( png_color *pal1,	// Count itentical palette entries
	png_color *pal2 );
void mem_greyscale();			// Convert image to greyscale
int mem_convert_rgb();			// Convert image to RGB
int mem_convert_indexed();		// Convert image to Indexed Palette
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
void mem_scale_pal( int i1, int r1, int g1, int b1, int i2, int r2, int g2, int b2 );
					// Generate a scaled palette
void mem_brcosa_pal( png_color *pal1, png_color *pal2 );
					// Palette 1 = Palette 2 adjusting brightness/contrast/saturation
void mem_pal_sort( int a, int i1, int i2, int rev );
					// Sort colours in palette 0=luminance, 1=RGB

void mem_pal_index_move( int c1, int c2 );	// Move index c1 to c2 and shuffle in between up/down
void mem_canvas_index_move( int c1, int c2 );	// Similar to palette item move but reworks canvas pixels

void set_zoom_centre( int x, int y );
void mem_boundary( int *x, int *y, int *w, int *h );		// Check/amend boundaries

int png_cmp( png_color a, png_color b );			// Compare 2 colours

void pal_hsl( png_color col, float *hh, float *ss, float *ll );	// Turn RGB into HSL

//// UNDO

#define UNDO_PAL   0	/* Palette changes */
#define UNDO_XPAL  1	/* Palette and indexed image changes */
#define UNDO_COL   2	/* Palette and/or RGB image changes */
#define UNDO_DRAW  3	/* Changes to current channel / RGBA */
#define UNDO_INV   4	/* "Invert" operation */
#define UNDO_XFORM 5	/* Changes to all channels */
#define UNDO_FILT  6	/* Changes to current channel */


void undo_free_x(undo_item *undo);
int mem_undo_next(int mode);		// Call this after a draw event but before any changes to image
unsigned char *mem_undo_previous();	// Get address of previous image (or current if none)

void mem_undo_backward();		// UNDO requested by user
void mem_undo_forward();		// REDO requested by user

int undo_next_core(int mode, int new_width, int new_height, int new_bpp, int cmask);


//// Drawing Primitives

int mem_clip_mask_init(unsigned char val);		// Initialise the clipboard mask
//	Extract alpha info from RGB clipboard
int mem_scale_alpha(unsigned char *img, unsigned char *alpha,
	int width, int height, int mode, unsigned char xorr);
void mem_clip_mask_set(unsigned char val);		// Mask colours A and B on the clipboard
void mem_clip_mask_clear();				// Clear the clipboard mask

void mem_smudge(int ox, int oy, int nx, int ny);		// Smudge from old to new @ tool_size
void mem_clone(int ox, int oy, int nx, int ny);			// Clone from old to new @ tool_size

void mem_brcosa_chunk( unsigned char *rgb, int len );		// Apply BRCOSA to RGB memory
void mem_posterize_chunk( unsigned char *rgb, int len );	// Apply posterize to RGB memory
void mem_gamma_chunk( unsigned char *rgb, int len );		// Apply gamma to RGB memory

void mem_flip_v(char *mem, char *tmp, int w, int h, int bpp);	// Flip image vertically
void mem_flip_h( char *mem, int w, int h, int bpp );		// Flip image horizontally
int mem_sel_rot( int dir );					// Rotate clipboard 90 degrees
int mem_image_rot( int dir );					// Rotate canvas 90 degrees
int mem_rotate_free( float angle, int type );			// Rotate canvas by any angle (degrees)
int mem_image_scale( int nw, int nh, int type );		// Scale image
int mem_image_resize( int nw, int nh, int ox, int oy );		// Resize image

int mem_isometrics(int type);

void flood_fill( int x, int y, unsigned int target );		// Recursively flood fill an area Indexed

//	Flood fill using patterns - pat_mem points to width*height area of memory blanked to zero
void flood_fill_pat( int x, int y, unsigned int target, unsigned char *pat_mem );

void mem_paint_mask( unsigned char *pat_mem );			// Paint fill on image using mask

void sline( int x1, int y1, int x2, int y2 );			// Draw single thickness straight line
void tline( int x1, int y1, int x2, int y2, int size );		// Draw size thickness straight line
void v_para( int x1, int y1, int x2, int y2, int vlen );	// Draw vertical sided parallelogram
void h_para( int x1, int y1, int x2, int y2, int hlen );	// Draw horizontal parallelogram
void g_para( int x1, int y1, int x2, int y2, int xv, int yv );	// Draw general parallelogram
void f_rectangle( int x, int y, int w, int h );			// Draw a filled rectangle
void f_circle( int x, int y, int r );				// Draw a filled circle
void f_ellipse( int x1, int y1, int x2, int y2 );		// Draw a filled ellipse
void o_ellipse( int x1, int y1, int x2, int y2, int thick );	// Draw an ellipse outline

//	A couple of shorthands to get an int representation of an RGB colour
#define PNG_2_INT(var) ((var.red << 16) + (var.green << 8) + (var.blue))
#define MEM_2_INT(M,I) (((M)[(I)] << 16) + ((M)[(I) + 1] << 8) + (M)[(I) + 2])
#define INT_2_R(A) ((A) >> 16)
#define INT_2_G(A) (((A) >> 8) & 0xFF)
#define INT_2_B(A) ((A) & 0xFF)

#define MEM_BPP (mem_channel == CHN_IMAGE ? mem_img_bpp : 1)
#define BPP(x) ((x) == CHN_IMAGE ? mem_img_bpp : 1)

#define GET_PIXEL(x,y) (mem_img[CHN_IMAGE][(x) + (y) * mem_width])

#define POSTERIZE_MACRO res = 0.49 + ( ((1 << posty) - 1) * ((float) res)/255);\
			res = 0.49 + 255 * ( ((float) res) / ((1 << posty) - 1) );

void prep_mask(int start, int step, int cnt, unsigned char *mask,
	unsigned char *mask0, unsigned char *img0);
void process_mask(int start, int step, int cnt, unsigned char *mask,
	unsigned char *alphar, unsigned char *alpha0, unsigned char *alpha,
	unsigned char *trans, int opacity);
void process_img(int start, int step, int cnt, unsigned char *mask,
	unsigned char *imgr, unsigned char *img0, unsigned char *img,
	int opacity);
void paste_pixels(int x, int y, int len, unsigned char *mask, unsigned char *img,
	unsigned char *alpha, unsigned char *trans, int opacity);

int pixel_protected(int x, int y);				// generic
void row_protected(int x, int y, int len, unsigned char *mask);
void put_pixel( int x, int y );					// generic
png_color get_pixel24( int x, int y );				// RGB version
int get_pixel( int x, int y );					// generic
void put_pixel24( int x, int y );				// RGB version

#define IF_IN_RANGE( x, y ) if ( x>=0 && y>=0 && x<mem_width && y<mem_height )

#define mtMIN(a,b,c) if ( b<c ) a=b; else a=c;
#define mtMAX(a,b,c) if ( b>c ) a=b; else a=c;

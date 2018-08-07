/*	mainwindow.h
	Copyright (C) 2004-2016 Mark Tyler and Dmitry Groshev

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


/* Keyboard action codes */
#define ACTMOD_DUMMY	-1 /* Means action is done already */
enum { // To let constants renumber themselves when adding new ones
	ACT_QUIT = 1,
	ACT_ZOOM,
	ACT_VIEW,
	ACT_PAN,
	ACT_CROP,
	ACT_SWAP_AB,
	ACT_TOOL,
	ACT_SEL_MOVE,
	ACT_OPAC,
	ACT_LR_MOVE,
	ACT_ESC,
	ACT_COMMIT,
	ACT_RCLICK,
	ACT_ARROW,
	ACT_A,
	ACT_B,
	ACT_CHANNEL,
	ACT_VWZOOM,
	ACT_SAVE,
	ACT_FACTION,
	ACT_LOAD_RECENT,
	ACT_DO_UNDO,
	ACT_COPY,
	ACT_PASTE,
	ACT_COPY_PAL,
	ACT_PASTE_PAL,
	ACT_LOAD_CLIP,
	ACT_SAVE_CLIP,
	ACT_TBAR,
	ACT_DOCK,
	ACT_CENTER,
	ACT_GRID,
	ACT_SNAP,
	ACT_VWWIN,
	ACT_VWSPLIT,
	ACT_VWFOCUS,
	ACT_FLIP_V,
	ACT_FLIP_H,
	ACT_ROTATE,
	ACT_SELECT,
	ACT_LASSO,
	ACT_OUTLINE,
	ACT_ELLIPSE,
	ACT_SEL_FLIP_V,
	ACT_SEL_FLIP_H,
	ACT_SEL_ROT,
	ACT_RAMP,
	ACT_SEL_ALPHA_AB,
	ACT_SEL_ALPHAMASK,
	ACT_SEL_MASK_AB,
	ACT_SEL_MASK,
	ACT_PAL_DEF,
	ACT_PAL_MASK,
	ACT_DITHER_A,
	ACT_PAL_MERGE,
	ACT_PAL_CLEAN,
	ACT_ISOMETRY,
	ACT_CHN_DIS,
	ACT_SET_RGBA,
	ACT_SET_OVERLAY,
	ACT_LR_SAVE,
	ACT_LR_ADD,
	ACT_LR_DEL,
	ACT_DOCS,
	ACT_REBIND_KEYS,
	ACT_MODE,
	ACT_LR_SHIFT,
	ACT_LR_CENTER,
	ACT_SCRIPT,
	ACT_RUN_SCRIPT,

	DLG_BRCOSA,
	DLG_CHOOSER,
	DLG_SCALE,
	DLG_SIZE,
	DLG_NEW,
	DLG_FSEL,
	DLG_FACTIONS,
	DLG_TEXT,
	DLG_TEXT_FT,
	DLG_LAYERS,
	DLG_INDEXED,
	DLG_ROTATE,
	DLG_INFO,
	DLG_PREFS,
	DLG_COLORS,
	DLG_PAL_SIZE,
	DLG_PAL_SORT,
	DLG_PAL_SHIFTER,
	DLG_FILTER,
	DLG_CHN_DEL,
	DLG_ANI,
	DLG_ANI_VIEW,
	DLG_ANI_KEY,
	DLG_ANI_KILLKEY,
	DLG_ABOUT,
	DLG_SKEW,
	DLG_FLOOD,
	DLG_SMUDGE,
	DLG_CLONE,
	DLG_GRAD,
	DLG_STEP,
	DLG_FILT,
	DLG_TRACE,
	DLG_PICK_GRAD,
	DLG_SEGMENT,
	DLG_SCRIPT,
	DLG_LASSO,
	DLG_KEYS,

	FILT_2RGB,
	FILT_INVERT,
	FILT_GREY,
	FILT_EDGE,
	FILT_DOG,
	FILT_SHARPEN,
	FILT_UNSHARP,
	FILT_SOFTEN,
	FILT_GAUSS,
	FILT_FX,
	FILT_BACT,
	FILT_THRES,
	FILT_UALPHA,
	FILT_KUWAHARA,
	FILT_NOISE,

	ACT_TEST /* Reserved for testing things */
};

// New layer sources for ACT_LR_ADD
#define LR_NEW   0
#define LR_DUP   1
#define LR_PASTE 2
#define LR_COMP  3

int key_action(key_ext *key, int toggle);
#define wtf_pressed(X) key_action((X), FALSE)
void action_dispatch(int action, int mode, int state, int kbd);

/* Widget dependence flags */
#define NEED_UNDO  0x0001
#define NEED_REDO  0x0002
#define NEED_CROP  0x0004
#define NEED_MARQ  0x0008
#define NEED_SEL   0x0010
#define NEED_CLIP  0x0020
#define NEED_24    0x0040
#define NEED_NOIDX 0x0080
#define NEED_IDX   0x0100
#define NEED_LASSO 0x0200
#define NEED_ACLIP 0x0400
#define NEED_CHAN  0x0800
#define NEED_RGBA  0x1000
#define NEED_PCLIP 0x2000
#define NEED_SKIP  0x4000 /* Never activated at all */
#define NEED_SEL2  (NEED_SEL | NEED_LASSO)
#define NEED_PSEL  (NEED_MARQ | NEED_PCLIP)
#define NEED_LAS2  (NEED_LASSO | NEED_PCLIP)

/* Notable menu items */
enum {
	MENU_FACTION1 = 1,
	MENU_FACTION2,
	MENU_FACTION3,
	MENU_FACTION4,
	MENU_FACTION5,
	MENU_FACTION6,
	MENU_FACTION7,
	MENU_FACTION8,
	MENU_FACTION9,
	MENU_FACTION10,
	MENU_FACTION11,
	MENU_FACTION12,
	MENU_FACTION13,
	MENU_FACTION14,
	MENU_FACTION15,
	MENU_FACTION_S,
	MENU_RECENT_S,
	MENU_RECENT1,
	MENU_RECENT2,
	MENU_RECENT3,
	MENU_RECENT4,
	MENU_RECENT5,
	MENU_RECENT6,
	MENU_RECENT7,
	MENU_RECENT8,
	MENU_RECENT9,
	MENU_RECENT10,
	MENU_RECENT11,
	MENU_RECENT12,
	MENU_RECENT13,
	MENU_RECENT14,
	MENU_RECENT15,
	MENU_RECENT16,
	MENU_RECENT17,
	MENU_RECENT18,
	MENU_RECENT19,
	MENU_RECENT20,
	MENU_SCRIPT,
	MENU_SCRIPT_M,
	MENU_SCRIPT1,
	MENU_SCRIPT2,
	MENU_SCRIPT3,
	MENU_SCRIPT4,
	MENU_SCRIPT5,
	MENU_SCRIPT6,
	MENU_SCRIPT7,
	MENU_SCRIPT8,
	MENU_SCRIPT9,
	MENU_SCRIPT10,
	MENU_TBSET,
	MENU_VIEW,
	MENU_LAYER,
	MENU_PREFS,
	MENU_CHAN0,
	MENU_CHAN1,
	MENU_CHAN2,
	MENU_CHAN3,
	MENU_DCHAN0,
	MENU_DCHAN1,
	MENU_DCHAN2,
	MENU_DCHAN3,
	MENU_OALPHA,
	MENU_HELP,
	MENU_DOCK,

	TOTAL_MENU_IDS
};

#define MAX_RECENT 20

/// TRACING IMAGE

unsigned char *bkg_rgb;
int bkg_x, bkg_y, bkg_w, bkg_h, bkg_scale, bkg_flag;

int config_bkg(int src); // 0 = unchanged, 1 = none, 2 = image, 3 = clipboard

/// GRID

/* !!! Indices 0-2 are hardcoded in draw_grid() */
#define GRID_NORMAL  0 /* Normal/image grid */
#define GRID_BORDER  1
#define GRID_TRANS   2 /* For transparency */
#define GRID_TILE    3
#define GRID_SEGMENT 4

#define GRID_MAX     5

int grid_rgb[GRID_MAX];	// Grid colors to use
int mem_show_grid;	// Boolean show toggle
int mem_grid_min;	// Minimum zoom to show it at
int color_grid;		// If to use grid coloring
int show_tile_grid;	// Tile grid toggle
int tgrid_x0, tgrid_y0;	// Tile grid origin
int tgrid_dx, tgrid_dy;	// Tile grid spacing
int tgrid_snap;		// Coordinates snap toggle

/* Snap coordinate pair to tile grid (floored) */
void snap_xy(int *xy);

const unsigned char greyz[2]; // For opacity squares


char *channames[NUM_CHANNELS + 1], *allchannames[NUM_CHANNELS + 1];
char *cspnames[NUM_CSPACES];

char *channames_[NUM_CHANNELS + 1];
char *cspnames_[NUM_CSPACES];

void **main_window_, **main_keys, **settings_dock, **layers_dock, **main_split,
	**drawing_canvas, **scrolledwindow_canvas,
	**menu_slots[TOTAL_MENU_IDS];

int	view_image_only, viewer_mode, drag_index, q_quit, cursor_tool;
int	show_menu_icons, paste_commit, scroll_zoom;
int	drag_index_vals[2], cursor_corner, use_gamma, view_vsplit;
int	files_passed, cmd_mode, tablet_working;
char **file_args, **script_cmds;

extern char mem_clip_file[];

extern void *scriptbar_code[];		// Set up scriptable items for tools toolbar

int kpix_threads;			// Min kpixels per render thread

/* With 2 cores and uniform layers stack, no noticeable difference between
 * 1, 4 and 8 strips per thread; time jitter of about 10% is common, and spikes
 * of about 40% sometimes happen; with 1 strip, even 50% was observed once.
 * Still, with nonuniform layers, more strips can maybe balance better - WJ */
#define MAX_TH_STRIPS 8

void var_init();			// Load INI variables
void string_init();			// Translate static strings
void main_init();			// Initialise and display the main window

int run_script(char **res);		// Interpret parsed sequence of commands

void draw_dash(int c0, int c1, int ofs, int x, int y, int w, int h, rgbcontext *ctx);
void draw_poly(int *xy, int cnt, int shift, int x00, int y00, rgbcontext *ctx);

void canvas_size(int *w, int *h);	// Get zoomed canvas size
void prepare_line_clip(int *lxy, int *vxy, int scale);	// Map clipping rectangle to line-space
void main_update_area(int x, int y, int w, int h);	// Update x,y,w,h area of current image
void repaint_canvas( int px, int py, int pw, int ph );		// Redraw area of canvas
void grad_stroke(int x, int y);		// Update stroke gradient

int async_bk;

typedef struct {
	int dx;
	int width;
	int xwid;
	int zoom;
	int scale;
	int mw;
	int opac;
	int xpm;
	int bpp;
	int cmask;
	png_color *pal;
} renderstate;

void setup_row(renderstate *r, int x0, int width, int zoom, int scale, int mw,
	int xpm, int opac, int bpp, png_color *pal);
void render_row(renderstate *r, unsigned char *rgb, chanlist base_img,
	int x, int y, chanlist xtra_img);

void stop_line();
void change_to_tool(int icon);

void spot_undo(int mode);		// Take snapshot for undo
void set_cursor(void **what);		// Set mouse cursor
int check_for_changes();		// 1=STOP, 2=IGNORE, 10=ESCAPE, -10=NOT CHECKED

//	Try to save file + warn if error + return < 0 if fail
int gui_save(char *filename, ls_settings *settings);

//	Load system clipboard like a file, return TRUE if successful
int import_clipboard(int mode);

void pressed_select(int all);
void pressed_opacity(int opacity);

int check_zoom_keys(int act_m);
int check_zoom_keys_real(int act_m);

void setup_language();		// Change language

//	Image/palette has just changed - update vars as needed
void notify_changed();
//	Image has just been unchanged: saved to file, or loaded if "filename" is NULL
void notify_unchanged(char *filename);
void update_titlebar();		// Update filename in titlebar

void force_main_configure();	// Force reconfigure of main drawing area - for centralizing code


void set_image(int state);	// Toggle image access (nestable)

int dock_focused();		// Check if focus is inside dock window

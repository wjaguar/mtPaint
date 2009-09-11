/*	mainwindow.h
	Copyright (C) 2004-2008 Mark Tyler and Dmitry Groshev

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
	ACT_UNDO,
	ACT_REDO,
	ACT_COPY,
	ACT_PASTE,
	ACT_PASTE_LR,
	ACT_COPY_PAL,
	ACT_PASTE_PAL,
	ACT_LOAD_CLIP,
	ACT_SAVE_CLIP,
	ACT_TBAR,
	ACT_DOCK,
	ACT_CENTER,
	ACT_GRID,
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
	ACT_LR_KILLALL,
	ACT_DOCS,
	ACT_REBIND_KEYS,

	DLG_BRCOSA,
	DLG_CMDLINE,
	DLG_PATTERN,
	DLG_BRUSH,
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

	FILT_2RGB,
	FILT_INVERT,
	FILT_GREY,
	FILT_EDGE,
	FILT_DOG,
	FILT_SHARPEN,
	FILT_UNSHARP,
	FILT_SOFTEN,
	FILT_GAUSS,
	FILT_EMBOSS,
	FILT_BACT,
	FILT_THRES,
	FILT_UALPHA
};

int wtf_pressed(GdkEventKey *event);

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
#define NEED_SEL2  (NEED_SEL | NEED_LASSO)

void mapped_dis_add(GtkWidget *widget, int actmap);

/* Notable menu items */
#define MENU_FACTION1  1
#define MENU_FACTION2  2
#define MENU_FACTION3  3
#define MENU_FACTION4  4
#define MENU_FACTION5  5
#define MENU_FACTION6  6
#define MENU_FACTION7  7
#define MENU_FACTION8  8
#define MENU_FACTION9  9
#define MENU_FACTION10 10
#define MENU_FACTION11 11
#define MENU_FACTION12 12
#define MENU_FACTION13 13
#define MENU_FACTION14 14
#define MENU_FACTION15 15
#define MENU_FACTION_S 16
#define MENU_RECENT_S  17
#define MENU_RECENT1   18
#define MENU_RECENT2   19
#define MENU_RECENT3   20
#define MENU_RECENT4   21
#define MENU_RECENT5   22
#define MENU_RECENT6   23
#define MENU_RECENT7   24
#define MENU_RECENT8   25
#define MENU_RECENT9   26
#define MENU_RECENT10  27
#define MENU_RECENT11  28
#define MENU_RECENT12  29
#define MENU_RECENT13  30
#define MENU_RECENT14  31
#define MENU_RECENT15  32
#define MENU_RECENT16  33
#define MENU_RECENT17  34
#define MENU_RECENT18  35
#define MENU_RECENT19  36
#define MENU_RECENT20  37
#define MENU_TBMAIN    38
#define MENU_TBTOOLS   39
#define MENU_TBSET     40
#define MENU_SHOWPAL   41
#define MENU_SHOWSTAT  42
#define MENU_CENTER    43
#define MENU_SHOWGRID  44
#define MENU_VIEW      45
#define MENU_VWFOCUS   46
#define MENU_CLINE     47
#define MENU_LAYER     48
#define MENU_PREFS     49
#define MENU_CHAN0     50
#define MENU_CHAN1     51
#define MENU_CHAN2     52
#define MENU_CHAN3     53
#define MENU_DCHAN0    54
#define MENU_DCHAN1    55
#define MENU_DCHAN2    56
#define MENU_DCHAN3    57
#define MENU_RGBA      58
#define MENU_HELP      59
#define MENU_DOCK      60
#define TOTAL_MENU_IDS 61

#define MAX_RECENT 20

const unsigned char greyz[2]; // For opacity squares

#define DOCK_CLINE		0
#define DOCK_TOOL_SETTINGS	1
#define DOCK_LAYERS		2

#define DOCK_TOTAL		3


char *channames[NUM_CHANNELS + 1], *allchannames[NUM_CHANNELS + 1];

GtkWidget *main_window, *main_split,
	*drawing_palette, *drawing_canvas, *vbox_right, *vw_scrolledwindow,
	*scrolledwindow_canvas, *main_hidden[4],

	*menu_undo[5], *menu_redo[5], *menu_crop[5], *menu_need_marquee[10],
	*menu_need_selection[20], *menu_need_clipboard[30], *menu_only_24[10],
	*menu_not_indexed[10], *menu_only_indexed[10], *menu_lasso[15],
	*menu_alphablend[2], *menu_chan_del[2], *menu_rgba[2],
	*menu_widgets[TOTAL_MENU_IDS],
	*dock_pane, *dock_area, *dock_vbox[DOCK_TOTAL];

int	view_image_only, viewer_mode, drag_index, q_quit, cursor_tool, show_menu_icons;
int	files_passed, file_arg_start, drag_index_vals[2], cursor_corner, show_dock;
char **global_argv;

GdkGC *dash_gc;

extern char mem_clip_file[];

void var_init();			// Load INI variables
void string_init();			// Translate static strings
void main_init();			// Initialise and display the main window

void men_item_state( GtkWidget *menu_items[], gboolean state );
	// Change state of preset menu items

void draw_rgb(int x, int y, int w, int h, unsigned char *rgb, int step, rgbcontext *ctx);

void canvas_size(int *w, int *h);	// Get zoomed canvas size
void main_update_area(int x, int y, int w, int h);	// Update x,y,w,h area of current image
void repaint_canvas( int px, int py, int pw, int ph );		// Redraw area of canvas
void repaint_perim(rgbcontext *ctx);	// Draw perimeter around mouse cursor
void clear_perim();			// Clear perimeter around mouse cursor
void setup_row(int x0, int width, double czoom, int mw, int xpm, int opac,
	int bpp, png_color *pal);
void render_row(unsigned char *rgb, chanlist base_img, int x, int y,
	chanlist xtra_img);
void overlay_row(unsigned char *rgb, chanlist base_img, int x, int y,
	chanlist xtra_img);

void stop_line();

void spot_undo(int mode);		// Take snapshot for undo
void set_cursor();			// Set mouse cursor
int check_for_changes();		// 1=STOP, 2=IGNORE, 10=ESCAPE, -10=NOT CHECKED

//	Try to save file + warn if error + return < 0 if fail
int gui_save(char *filename, ls_settings *settings);

void pressed_select_none();
void pressed_opacity( int opacity );

gint check_zoom_keys(int act_m);
gint check_zoom_keys_real(int act_m);

void zoom_in();
void zoom_out();

void setup_language();		// Change language

void notify_changed();		// Image/palette has just changed - update vars as needed
void notify_unchanged();	// Image/palette has just been unchanged (saved) - update vars as needed
void update_titlebar();		// Update filename in titlebar

void force_main_configure();	// Force reconfigure of main drawing area - for centralizing code


void toolbar_icon_event2(GtkWidget *widget, gpointer data);
void toolbar_icon_event (GtkWidget *widget, gpointer data);

void set_image(gboolean state);	// Toggle image access (nestable)

int dock_focused();		// Check if focus is inside dock window

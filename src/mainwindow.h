/*	mainwindow.h
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


/* Keyboard action codes */
#define ACT_DUMMY	-1 /* Means action is done already */
#define ACT_QUIT	1
#define ACT_ZOOM_IN	2
#define ACT_ZOOM_OUT	3
#define ACT_ZOOM_01	4
#define ACT_ZOOM_025	5
#define ACT_ZOOM_05	6
#define ACT_ZOOM_1	7
#define ACT_ZOOM_4	8
#define ACT_ZOOM_8	9
#define ACT_ZOOM_12	10
#define ACT_ZOOM_16	11
#define ACT_ZOOM_20	12
#define ACT_VIEW	13
#define ACT_BRCOSA	14
#define ACT_PAN		15
#define ACT_CROP	16
#define ACT_SWAP_AB	17
#define ACT_CMDLINE	18
#define ACT_PATTERN	19
#define ACT_BRUSH	20
#define ACT_PAINT	21
#define ACT_SELECT	22
#define ACT_SEL_2LEFT	23
#define ACT_SEL_2RIGHT	24
#define ACT_SEL_2DOWN	25
#define ACT_SEL_2UP	26
#define ACT_SEL_LEFT	27
#define ACT_SEL_RIGHT	28
#define ACT_SEL_DOWN	29
#define ACT_SEL_UP	30
#define ACT_OPAC_01	31
#define ACT_OPAC_02	32
#define ACT_OPAC_03	33
#define ACT_OPAC_04	34
#define ACT_OPAC_05	35
#define ACT_OPAC_06	36
#define ACT_OPAC_07	37
#define ACT_OPAC_08	38
#define ACT_OPAC_09	39
#define ACT_OPAC_1	40
#define ACT_OPAC_P	41
#define ACT_OPAC_M	42
#define ACT_LR_2LEFT	43
#define ACT_LR_2RIGHT	44
#define ACT_LR_2DOWN	45
#define ACT_LR_2UP	46
#define ACT_LR_LEFT	47
#define ACT_LR_RIGHT	48
#define ACT_LR_DOWN	49
#define ACT_LR_UP	50
#define ACT_ESC		51
#define ACT_SCALE	52
#define ACT_SIZE	53
#define ACT_COMMIT	54
#define ACT_RCLICK	55
#define ACT_ARROW	56
#define ACT_ARROW3	57
#define ACT_A_PREV	58
#define ACT_A_NEXT	59
#define ACT_B_PREV	60
#define ACT_B_NEXT	61
#define ACT_TO_IMAGE	62
#define ACT_TO_ALPHA	63
#define ACT_TO_SEL	64
#define ACT_TO_MASK	65
#define ACT_VWZOOM_IN	66
#define ACT_VWZOOM_OUT	67

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
#define TOTAL_MENU_IDS 60

#define MAX_RECENT 20

char *channames[NUM_CHANNELS + 1], *allchannames[NUM_CHANNELS + 1];

GtkWidget *main_window, *main_split,
	*drawing_palette, *drawing_canvas, *vbox_right, *vw_scrolledwindow,
	*scrolledwindow_canvas, *main_hidden[4],

	*menu_undo[5], *menu_redo[5], *menu_crop[5], *menu_need_marquee[10],
	*menu_need_selection[20], *menu_need_clipboard[30], *menu_only_24[10],
	*menu_not_indexed[10], *menu_only_indexed[10], *menu_lasso[15],
	*menu_alphablend[2], *menu_chan_del[2], *menu_rgba[2],
	*menu_widgets[TOTAL_MENU_IDS];

int view_image_only, viewer_mode, drag_index, q_quit, cursor_tool;
int files_passed, file_arg_start, drag_index_vals[2], cursor_corner;
char **global_argv;

GdkGC *dash_gc;

char mem_clip_file[256];

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

void pressed_select_none( GtkMenuItem *menu_item, gpointer user_data );
void pressed_opacity( int opacity );

void pressed_choose_patterns( GtkMenuItem *menu_item, gpointer user_data );
void pressed_choose_brush( GtkMenuItem *menu_item, gpointer user_data );

gint check_zoom_keys(int action);
gint check_zoom_keys_real(int action);

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

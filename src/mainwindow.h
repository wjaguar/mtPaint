/*	mainwindow.h
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


#include <gtk/gtk.h>

/* Keyboard action codes */
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
#define ACT_ARROW	55
#define ACT_ARROW3	56

typedef struct
{
	char *actname;
	int action, key, kmask, kflags;
} key_action;

extern key_action main_keys[];

int wtf_pressed(GdkEventKey *event, key_action *keys);

GtkWidget
	*main_window, *main_vsplit,
	*drawing_palette, *drawing_canvas,
	*scrolledwindow_canvas, *main_hidden[4],

	*menu_undo[5], *menu_redo[5], *menu_crop[5],
	*menu_need_marquee[10], *menu_need_selection[20], *menu_need_clipboard[30],
	*menu_help[2], *menu_only_24[20], *menu_only_indexed[10],
	*menu_recent[23], *menu_clip_load[15], *menu_clip_save[15],
	*menu_cline[2], *menu_view[2], *menu_iso[5], *menu_layer[2], *menu_lasso[15],
	*menu_prefs[2], *menu_frames[2], *menu_alphablend[2], *menu_chann_x[NUM_CHANNELS+1],
	*menu_chan_del[2], *menu_chan_dis[NUM_CHANNELS+1]
	;

gboolean view_image_only, viewer_mode, drag_index, q_quit;
int files_passed, file_arg_start, drag_index_vals[2], cursor_corner;
char **global_argv;

GdkGC *dash_gc;


void main_init();			// Initialise and display the main window
gint delete_event( GtkWidget *widget, GdkEvent *event, gpointer data );

void pop_men_dis( GtkItemFactory *item_factory, char *items[], GtkWidget *menu_items[] );
	// Populate disable menu item array

void men_item_state( GtkWidget *menu_items[], gboolean state );
	// Change state of preset menu items

void repaint_canvas( int px, int py, int pw, int ph );		// Redraw area of canvas
void repaint_perim();			// Draw perimeter around mouse cursor
void clear_perim();			// Clear perimeter around mouse cursor
void setup_row(int x0, int width, double czoom, int mw, int xpm, int opac,
	int bpp, png_color *pal);
void render_row(unsigned char *rgb, chanlist base_img, int x, int y,
	chanlist xtra_img);
void overlay_row(unsigned char *rgb, chanlist base_img, int x, int y,
	chanlist xtra_img);
void repaint_paste( int px1, int py1, int px2, int py2 );
void main_render_rgb( unsigned char *rgb, int px, int py, int pw, int ph );

void stop_line();

void spot_undo(int mode);		// Take snapshot for undo
void set_cursor();			// Set mouse cursor
int check_for_changes();		// 1=STOP, 2=IGNORE, 10=ESCAPE, -10=NOT CHECKED

int gui_save( char *filename );		// Try to save file + warn if error + return < 0 if fail

void pressed_select_none( GtkMenuItem *menu_item, gpointer user_data );
void pressed_opacity( int opacity );

void pressed_choose_patterns( GtkMenuItem *menu_item, gpointer user_data );
void pressed_choose_brush( GtkMenuItem *menu_item, gpointer user_data );

gint check_zoom_keys(int action);
gint check_zoom_keys_real(int action);

void zoom_in( GtkMenuItem *menu_item, gpointer user_data );
void zoom_out( GtkMenuItem *menu_item, gpointer user_data );

void setup_language();		// Change language

void notify_changed();		// Image/palette has just changed - update vars as needed
void notify_unchanged();	// Image/palette has just been unchanged (saved) - update vars as needed
void update_titlebar();		// Update filename in titlebar

void force_main_configure();	// Force reconfigure of main drawing area - for centralizing code


void toolbar_icon_event2(GtkWidget *widget, gpointer data);
void toolbar_icon_event (GtkWidget *widget, gpointer data);
void men_dis_add( GtkWidget *widget, GtkWidget *menu_items[] );

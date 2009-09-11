/*	mainwindow.h
	Copyright (C) 2004, 2005 Mark Tyler

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

GtkWidget
		*menu_undo[5], *menu_redo[5], *menu_crop[5],
		*menu_need_marquee[10], *menu_need_selection[20], *menu_need_clipboard[30],
		*menu_continuous[5], *menu_only_24[20], *menu_only_indexed[10], *menu_recent[23],
		*menu_opac[15], *menu_cline[2], *menu_view[2], *menu_help[2], *menu_iso[5],
		*menu_layer[2], *menu_lasso[15], *menu_prefs[2]
		;

GtkWidget *main_hidden[4];

gboolean view_image_only, viewer_mode, drag_index;
int files_passed, file_arg_start, drag_index_vals[2], cursor_corner;
char **global_argv;

GtkWidget *main_window, *main_vsplit;

GtkWidget *drawing_palette, *drawing_pat_prev, *drawing_col_prev, *drawing_canvas;
GtkWidget *scrolledwindow_canvas;
GtkWidget *label_A, *label_B, *label_bar1, *label_bar2, *label_bar3,
	*label_bar4, *label_bar5, *label_bar6, *label_bar7, *label_bar8, *label_bar9;
GtkWidget *viewport_palette;
GtkWidget *spinbutton_spray, *spinbutton_size;

GdkGC *dash_gc;

#define TOTAL_CURSORS 14
#define TOTAL_ICONS_TOOLBAR 16
#define DEFAULT_TOOL_ICON 7
#define PAINT_TOOL_ICON 1

GtkWidget *icon_buttons[TOTAL_ICONS_TOOLBAR];
GdkCursor *m_cursor[32];		// My mouse cursors

gboolean q_quit;			// Does q key quit the program?

void main_init();			// Initialise and display the main window
gint delete_event( GtkWidget *widget, GdkEvent *event, gpointer data );

GtkWidget *layer_iconbar(GtkWidget *window, GtkWidget *box, GtkWidget **icons);
	// Create iconbar for layers window

void pop_men_dis( GtkItemFactory *item_factory, char *items[], GtkWidget *menu_items[] );
	// Populate disable menu item array

void men_item_state( GtkWidget *menu_items[], gboolean state );
	// Change state of preset menu items

void repaint_canvas( int px, int py, int pw, int ph );		// Redraw area of canvas
void repaint_perim();			// Draw perimeter around mouse cursor
void clear_perim();			// Clear perimeter around mouse cursor
void repaint_paste( int px1, int py1, int px2, int py2 );
void main_render_rgb( unsigned char *rgb, int px, int py, int pw, int ph, float zoom );

void stop_line();

void spot_undo();			// Take snapshot for undo
void set_cursor();			// Set mouse cursor
int check_for_changes();		// 1=STOP, 2=IGNORE, 10=ESCAPE, -10=NOT CHECKED

int gui_save( char *filename );		// Try to save file + warn if error + return < 0 if fail

void pressed_select_none( GtkMenuItem *menu_item, gpointer user_data );
void pressed_opacity( GtkMenuItem *menu_item, gpointer user_data );

void pressed_choose_patterns( GtkMenuItem *menu_item, gpointer user_data );
void pressed_choose_brush( GtkMenuItem *menu_item, gpointer user_data );

gint check_zoom_keys( GdkEventKey *event );
gint check_zoom_keys_real( GdkEventKey *event );

void zoom_in( GtkMenuItem *menu_item, gpointer user_data );
void zoom_out( GtkMenuItem *menu_item, gpointer user_data );

void setup_language();		// Change language

void notify_changed();		// Image/palette has just changed - update vars as needed
void notify_unchanged();	// Image/palette has just been unchanged (saved) - update vars as needed
void update_titlebar();		// Update filename in titlebar

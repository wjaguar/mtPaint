/*	toolbar.h
	Copyright (C) 2006 Mark Tyler

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


//	DEFINITIONS


// If this order changes, main_init work on toolbar_menu_widgets will need changing

#define TOOLBAR_MAIN 1
#define TOOLBAR_TOOLS 2
#define TOOLBAR_SETTINGS 3
#define TOOLBAR_PALETTE 4
#define TOOLBAR_STATUS 5

#define TOOLBAR_MAX 6


#define PATTERN_WIDTH 32
#define PATTERN_HEIGHT 32

#define PREVIEW_WIDTH 72
#define PREVIEW_HEIGHT 24


#define TOTAL_CURSORS 14

#define TOTAL_ICONS_MAIN 10
#define TOTAL_ICONS_TOOLS 18
#define TOTAL_ICONS_SETTINGS 5

#define DEFAULT_TOOL_ICON 6
#define PAINT_TOOL_ICON 0


//	GLOBAL VARIABLES


GtkWidget *icon_buttons[TOTAL_ICONS_TOOLS];
GdkCursor *m_cursor[32];		// My mouse cursors
GdkCursor *move_cursor;

gboolean toolbar_status[TOOLBAR_MAX];		// True=show
GtkWidget *toolbar_boxes[TOOLBAR_MAX],		// Used for showing/hiding
	*toolbar_menu_widgets[TOOLBAR_MAX];	// Menu widgets



//	GLOBAL PROCEDURES

void toolbar_init(GtkWidget *vbox_main);	// Set up the widgets to the vbox
void toolbar_palette_init(GtkWidget *box);	// Set up the palette area
void toolbar_exit();				// Remember toolbar settings on program exit
void toolbar_showhide();			// Show/Hide all 4 toolbars
void toolbar_zoom_update();			// Update the zoom combos to reflect current zoom
void toolbar_viewzoom(gboolean visible);	// Show/hide the view zoom combo
void toolbar_update_settings();			// Update details in the settings toolbar

void pressed_toolbar_toggle( GtkMenuItem *menu_item, gpointer user_data, gint item );
						// Menu toggle for toolbars


void toolbar_preview_init();		// Initialize memory for preview area

void mem_set_brush(int val);		// Set brush, update size/flow/preview
void mem_pat_update();			// Update indexed and then RGB pattern preview
void repaint_top_swatch();		// Update selected colours A & B



GtkWidget *layer_iconbar(GtkWidget *window, GtkWidget *box, GtkWidget **icons);
	// Create iconbar for layers window



/*	toolbar.h
	Copyright (C) 2006-2008 Mark Tyler and Dmitry Groshev

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


//	DEFINITIONS

#define PATTERN_GRID_W 10
#define PATTERN_GRID_H 10

/* !!! Must match order of menu item IDs (MENU_TBMAIN etc.) */
#define TOOLBAR_MAIN 1
#define TOOLBAR_TOOLS 2
#define TOOLBAR_SETTINGS 3
#define TOOLBAR_PALETTE 4
#define TOOLBAR_STATUS 5

#define TOOLBAR_MAX 6

#define PREVIEW_WIDTH 72
#define PREVIEW_HEIGHT 48


//	Main toolbar buttons
#define MTB_NEW    0
#define MTB_OPEN   1
#define MTB_SAVE   2
#define MTB_CUT    3
#define MTB_COPY   4
#define MTB_PASTE  5
#define MTB_UNDO   6
#define MTB_REDO   7
#define MTB_BRCOSA 8
#define MTB_PAN    9

#define TOTAL_ICONS_MAIN 10


//	Tools toolbar buttons
#define TTB_PAINT    0
#define TTB_SHUFFLE  1
#define TTB_FLOOD    2
#define TTB_LINE     3
#define TTB_SMUDGE   4
#define TTB_CLONE    5
#define TTB_SELECT   6
#define TTB_POLY     7
#define TTB_GRAD     8
#define TTB_LASSO    9
#define TTB_TEXT     10
#define TTB_ELLIPSE  11
#define TTB_FELLIPSE 12
#define TTB_OUTLINE  13
#define TTB_FILL     14
#define TTB_SELFV    15
#define TTB_SELFH    16
#define TTB_SELRCW   17
#define TTB_SELRCCW  18

#define DEFAULT_TOOL_ICON TTB_SELECT
#define SMUDGE_TOOL_ICON TTB_SMUDGE

#define TTB_0 TOTAL_SETTINGS
#define TOTAL_ICONS_TOOLS 19

//	Settings toolbar buttons
#define SETB_CONT 0
#define SETB_OPAC 1
#define SETB_TINT 2
#define SETB_TSUB 3
#define SETB_CSEL 4
#define SETB_FILT 5
#define SETB_MASK 6
#define TOTAL_ICONS_SETTINGS 7
#define SETB_GRAD 7
#define TOTAL_SETTINGS 8

typedef struct
{
	char *tooltip;
	signed char radio;
	unsigned short ID;
	int actmap;
	char **xpm;
	short action, mode, action2, mode2;
} toolbar_item;

//	GLOBAL VARIABLES


GtkWidget *icon_buttons[TOTAL_ICONS_TOOLS];
GdkCursor *m_cursor[32];		// My mouse cursors
GdkCursor *move_cursor;

gboolean toolbar_status[TOOLBAR_MAX];		// True=show
GtkWidget *toolbar_boxes[TOOLBAR_MAX],		// Used for showing/hiding
	*drawing_col_prev;



//	GLOBAL PROCEDURES

void fill_toolbar(GtkToolbar *bar, toolbar_item *items, GtkWidget **wlist,
	GtkSignalFunc lclick, GtkSignalFunc rclick);

void toolbar_init(GtkWidget *vbox_main);	// Set up the widgets to the vbox
void toolbar_palette_init(GtkWidget *box);	// Set up the palette area
void toolbar_exit();				// Remember toolbar settings on program exit
void toolbar_showhide();			// Show/Hide all 4 toolbars
void toolbar_zoom_update();			// Update the zoom combos to reflect current zoom
void toolbar_viewzoom(gboolean visible);	// Show/hide the view zoom combo
void toolbar_update_settings();			// Update details in the settings toolbar

void pressed_toolbar_toggle(int state, int which);
						// Menu toggle for toolbars


void mem_set_brush(int val);		// Set brush, update size/flow/preview
void mem_pat_update();			// Update indexed and then RGB pattern preview
void update_top_swatch();		// Update selected colours A & B

unsigned char *render_patterns();	// Create RGB dump of patterns to display
void set_patterns(unsigned char *src);	// Set 0-1 indexed image as new patterns

void mode_change(int setting, int state);	// Drawing mode variables
void flood_settings();			// Flood fill step
void smudge_settings();			// Smudge opacity mode
void step_settings();			// Brush spacing
void blend_settings();			// Blend mode

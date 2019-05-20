/*	toolbar.h
	Copyright (C) 2006-2019 Mark Tyler and Dmitry Groshev

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

#define TOOLBAR_MAIN 1
#define TOOLBAR_TOOLS 2
#define TOOLBAR_SETTINGS 3
#define TOOLBAR_PALETTE 4
#define TOOLBAR_STATUS 5

#define TOOLBAR_MAX 6
#define TOOLBAR_LAYERS TOOLBAR_MAX

#define PREVIEW_WIDTH 72
#define PREVIEW_HEIGHT 48
#define PREVIEW_BRUSH_X 40
#define PREVIEW_BRUSH_Y 0

#define PATTERN_CELL (8 * 4 + 4)
#define DEF_PATTERNS 16


//	Main toolbar buttons
enum {
	MTB_NEW = 0,
	MTB_OPEN,
	MTB_SAVE,
	MTB_CUT,
	MTB_COPY,
	MTB_PASTE,
	MTB_UNDO,
	MTB_REDO,
	MTB_BRCOSA,
	MTB_PAN,

	TOTAL_ICONS_MAIN
};

//	Tools toolbar buttons
enum {
	TTB_PAINT = 0,
	TTB_SHUFFLE,
	TTB_FLOOD,
	TTB_LINE,
	TTB_SMUDGE,
	TTB_CLONE,
	TTB_SELECT,
	TTB_POLY,
	TTB_GRAD,

	TOTAL_TOOLS,
	TTB_LASSO = TOTAL_TOOLS,
	TTB_TEXT,
	TTB_ELLIPSE,
	TTB_FELLIPSE,
	TTB_OUTLINE,
	TTB_FILL,
	TTB_SELFV,
	TTB_SELFH,
	TTB_SELRCW,
	TTB_SELRCCW,

	TOTAL_ICONS_TOOLS
};
#define SMUDGE_TOOL_ICON TTB_SMUDGE

#define TTB_0 TOTAL_SETTINGS

//	Settings toolbar buttons
enum {
	SETB_CONT = 0,
	SETB_OPAC,
	SETB_TINT,
	SETB_TSUB,
	SETB_CSEL,
	SETB_FILT,
	SETB_MASK,
	SETB_GRAD,

	TOTAL_SETTINGS
};

//	GLOBAL VARIABLES


void **icon_buttons[TOTAL_TOOLS];
void **m_cursor[TOTAL_CURSORS];		// My mouse cursors
void **move_cursor, **busy_cursor, **corner_cursor[4]; // System cursors

int toolbar_status[TOOLBAR_MAX];		// True=show
void **drawing_col_prev, **drawing_palette;
void **toolbar_boxes[TOOLBAR_MAX],		// Used for showing/hiding
	**toolbar_zoom_view;

int patterns_grid_w, patterns_grid_h;

//	GLOBAL PROCEDURES

extern void *toolbar_code[];			// Set up the widgets to the vbox
extern void *toolbar_palette_code[];		// Set up the palette area

void toolbar_showhide();			// Show/Hide all 4 toolbars
void toolbar_zoom_update();			// Update the zoom combos to reflect current zoom
void toolbar_update_settings();			// Update details in the settings toolbar
void **create_settings_box();
void toolbar_settings_exit(void *dt, void **wdata);
#define toolbar_viewzoom(V) cmd_showhide(toolbar_zoom_view, V)
						// Show/hide the view zoom combo

void pressed_toolbar_toggle(int state, int which);
						// Menu toggle for toolbars


void init_clone();			// Init clone tool
void mem_set_brush(int val);		// Set brush, update size/flow/preview
void mem_pat_update();			// Update indexed and then RGB pattern preview
int get_default_tool_icon(); // get default tool icon
void update_top_swatch();		// Update selected colours A & B

int set_master_pattern(char *m);		// Make 4x4 dither patterns from master
void render_patterns(unsigned char *buf);	// Create RGB dump of patterns to display
int set_patterns(ls_settings *settings);	// Test/set image as new patterns

void mode_change(int setting, int state);	// Drawing mode variables
void flood_settings();			// Flood fill step
void smudge_settings();			// Smudge opacity mode
void clone_settings();			// Clone mode & position
void lasso_settings();			// Lasso selection channel
void step_settings();			// Brush spacing
void blend_settings();			// Blend mode

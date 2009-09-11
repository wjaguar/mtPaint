/*	layer.h
	Copyright (C) 2005-2008 Mark Tyler and Dmitry Groshev

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

#define MAX_LAYERS 100
#define LAYERS_HEADER "# mtPaint layers"
#define LAYERS_VERSION 1

#define ANIMATION_HEADER "# mtPaint animation"
#define MAX_POS_SLOTS 100

#define LAYER_NAMELEN 35

///	GLOBALS

typedef struct {
	int frame, x, y, opacity, effect;
} ani_slot;

typedef struct
{
	image_info image_;
	image_state state_;
	ani_slot ani_pos[MAX_POS_SLOTS];
} layer_image;				// All as per memory.h definitions

typedef struct
{
	char name[LAYER_NAMELEN];	// Layer text name
	int x, y, trans, opacity;	// Position of layer, transparency colour, opacity %
	gboolean visible, use_trans;	// Show layer, use transparency
	layer_image *image;		// Pointer to image data - malloc'd when created, free'd after
} layer_node;


GtkWidget *layers_window;

layer_node layer_table[MAX_LAYERS+1];	// Table of layer info

int	layers_total,			// Layers currently in use
	layer_selected,			// Layer currently selected in the layers window
	layers_changed;			// 0=Unchanged

char layers_filename[PATHBUF];	// Current filename for layers file
int	show_layers_main,		// Show all layers in main window
	layers_pastry_cut;		// Pastry cut layers in view area (for animation previews)



///	PROCEDURES

void layers_init();
void pressed_layers();
void pressed_paste_layer();
gint delete_layers_window();

int load_layers( char *file_name );
int save_layers( char *file_name );
void layer_press_save();
int layer_save_composite(char *fname, ls_settings *settings);

void layers_notify_changed();
void layer_press_remove_all();
int check_layers_for_changes();
int check_layers_all_saved();
void move_layer_relative(int l, int change_x, int change_y);	// Move a layer & update window labels
void layer_new( int w, int h, int type, int cols, int cmask );	// Types 1=indexed, 2=grey, 3=RGB
void layer_choose( int l );				// Select a new layer from the list

void layer_iconbar_click(GtkWidget *widget, gpointer data);

void string_chop( char *txt );
int read_file_num(FILE *fp, char *txt);

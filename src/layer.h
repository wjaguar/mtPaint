/*	layer.h
	Copyright (C) 2005-2016 Mark Tyler and Dmitry Groshev

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

#define LAYER_NAMELEN 35

///	GLOBALS

typedef struct {
	image_info image_;
	image_state state_;
	ani_info ani_;
} layer_image;

typedef struct {
	char name[LAYER_NAMELEN];	// Layer text name
	int x, y, opacity;		// Position of layer, opacity %
	int visible;			// Show layer
	layer_image *image;		// Pointer to image data - malloc'd when created, free'd after
} layer_node;


layer_node layer_table[(MAX_LAYERS + 1) * 2];	// Table of layer info & its backup
layer_node *layer_table_p;		// Unmodified layer table

int	layers_total,			// Layers currently in use
	layer_selected,			// Layer currently selected in the layers window
	layers_changed;			// 0=Unchanged

char layers_filename[PATHBUF];	// Current filename for layers file
int	show_layers_main,		// Show all layers in main window
	layer_overlay;			// Toggle overlays per layer

#define LAYERS_MAIN (show_layers_main && (ani_state != ANI_CONF))


///	PROCEDURES

void layers_init();
layer_image *alloc_layer(int w, int h, int bpp, int cmask, image_info *src);
void pressed_layers();
void pressed_paste_layer();
void delete_layers_window();
void **create_layers_box();

int load_layers( char *file_name );
int save_layers( char *file_name );
void layer_press_save();
int layer_save_composite(char *fname, ls_settings *settings);

int load_to_layers(char *file_name, int ftype, int ani_mode);

#define update_main_with_new_layer() update_stuff(UPD_LAYER)

void layers_notify_changed();
void layer_copy_from_main( int l );	// Copy info from main image to layer
void layer_copy_to_main( int l );	// Copy info from layer to main image
void layer_refresh_list();
void layer_press_remove_all();
int check_layers_for_changes();
int check_layers_all_saved();
void move_layer_relative(int l, int change_x, int change_y);	// Move a layer & update window labels
void layer_new(int w, int h, int bpp, int cols, png_color *pal, int cmask);
//	*Silently* add layer, return success
int layer_add(int w, int h, int bpp, int cols, png_color *pal, int cmask);
void layer_show_new();			// Show the last added layer
void layer_delete(int item);		// *Silently* delete layer
void layer_choose( int l );		// Select a new layer from the list
void layer_add_composite();		// Composite layers to new (invisible) layer
void shift_layer(int val);		// Move layer up or down
void layer_press_duplicate();
void layer_press_centre();		// Center layer on background
void layer_press_delete();		// Delete current layer

void string_chop( char *txt );
int read_file_num(FILE *fp, char *txt);

void layer_show_trans();		// Update transparency in layers window

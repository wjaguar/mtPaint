/*	ani.h
	Copyright (C) 2005-2006 Mark Tyler

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



#define MAX_CYC_SLOTS 100
#define TOT_CYC_ITEMS 52
// 52 = 2 items for start/finish, 50 for collection of layer #'s


#define MAX_FRAME 99999

#define MAX_DELAY 1000

#define ANI_PREFIX_LEN 16

#define GIFSICLE_CREATE "gifsicle --colors 256 -w -O2 -D 2 -l0 --careful"
	// global colourmaps, suppress warning, high optimizations, background removal method, infinite loops, ensure result works with Java & MS IE


int	ani_frame1, ani_frame2, ani_gif_delay;


void ani_init();			// Initialize variables/arrays etc.

void pressed_animate_window( GtkMenuItem *menu_item, gpointer user_data );
void pressed_set_key_frame();
void pressed_remove_key_frames();

void ani_read_file( FILE *fp );		// Read data from layers file already opened
void ani_write_file( FILE *fp );	// Write data to layers file already opened

void ani_but_preview();			// Preview the animation
void ani_set_key_frame(int key);	// Set key frame postions & cycles as per current layers

void wild_space_change( char *in, char *out, int length );
					// Copy string but replace " " with "\ " or "\" \"" - needed for filenames with spaces

int gifsicle( char *command );	// Execute Gifsicle/Gifview

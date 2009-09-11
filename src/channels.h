/*	channels.h
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

int overlay_alpha;
int hide_image;
int RGBA_mode;

unsigned char channel_rgb[NUM_CHANNELS][3];
unsigned char channel_opacity[NUM_CHANNELS];
unsigned char channel_inv[NUM_CHANNELS];

unsigned char channel_fill[NUM_CHANNELS];
unsigned char channel_col_A[NUM_CHANNELS];
unsigned char channel_col_B[NUM_CHANNELS];

void pressed_channel_create( GtkMenuItem *menu_item, gpointer user_data, gint item );
void pressed_channel_delete( GtkMenuItem *menu_item, gpointer user_data, gint item );
void pressed_channel_edit( GtkMenuItem *menu_item, gpointer user_data, gint item );
void pressed_channel_disable( GtkMenuItem *menu_item, gpointer user_data, gint item );
void pressed_threshold( GtkMenuItem *menu_item, gpointer user_data, gint item );
void pressed_channel_toggle( GtkMenuItem *menu_item, gpointer user_data, gint item );
void pressed_RGBA_toggle( GtkMenuItem *menu_item, gpointer user_data, gint item );
void pressed_channel_config_overlay();
void pressed_channel_load();
void pressed_channel_save();

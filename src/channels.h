/*	channels.h
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

int overlay_alpha;
int hide_image;
int RGBA_mode;

unsigned char channel_rgb[NUM_CHANNELS][3];
unsigned char channel_opacity[NUM_CHANNELS];
unsigned char channel_inv[NUM_CHANNELS];

unsigned char channel_fill[NUM_CHANNELS];
unsigned char channel_col_[2][NUM_CHANNELS];
#define channel_col_A channel_col_[0]
#define channel_col_B channel_col_[1]

int channel_dis[NUM_CHANNELS];

void pressed_channel_create(int channel);
void pressed_channel_delete();
void pressed_channel_edit(int state, int channel);
void pressed_channel_disable(int state, int channel);
void pressed_threshold();
void pressed_unassociate();
void pressed_channel_toggle(int state, int what);
void pressed_RGBA_toggle(int state);

/*	ani.h
	Copyright (C) 2005-2006 Mark Tyler

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

#define MAX_CYC_SLOTS 100
#define MAX_CYC_ITEMS 100
#define MAX_FRAME 99999
#define MAX_DELAY 1000
#define ANI_PREFIX_LEN 16

#if MAX_LAYERS > 128
#error "Layer indices cannot fit in ani_cycle structure"
#endif
typedef struct {
	int frame0, frame1, len;
	signed char layers[MAX_CYC_ITEMS];
} ani_cycle;

int ani_frame1, ani_frame2, ani_gif_delay;
ani_cycle ani_cycle_table[MAX_CYC_SLOTS];


void ani_init();			// Initialize variables/arrays etc.

void pressed_animate_window();
void pressed_set_key_frame();
void pressed_remove_key_frames();

void ani_read_file( FILE *fp );		// Read data from layers file already opened
void ani_write_file( FILE *fp );	// Write data to layers file already opened

void ani_but_preview();			// Preview the animation

/*	otherwindow.h
	Copyright (C) 2004-2013 Mark Tyler and Dmitry Groshev

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


#define COLSEL_OVERLAYS  1
#define COLSEL_EDIT_AB   2
#define COLSEL_EDIT_CSEL 3
#define COLSEL_GRID      4
#define COLSEL_EDIT_ALL  256

#define CHOOSE_PATTERN 0
#define CHOOSE_BRUSH   1
#define CHOOSE_COLOR   2

/// Generic V-code to handle UI needs of common image transform tasks

typedef int (*filterwindow_fn)(void *ddata, void **wdata);
#define FW_FN(X) (filterwindow_fn)(X)

typedef struct {
	char *name;
	void **code;
	filterwindow_fn evt;
} filterwindow_dd;

extern void *filterwindow_code[];

typedef struct {
	filterwindow_dd fw;
	int n[3];
} spin1_dd;

extern void *spin1_code[];


int mem_preview, mem_preview_clip, brcosa_auto;
int posterize_mode;
int sharper_reduce;
int spal_mode;
seg_state *seg_preview;

void generic_new_window(int type);

void pressed_add_cols();
void pressed_brcosa();
void pressed_bacteria();
void pressed_scale_size(int mode);

void pressed_sort_pal();
void pressed_quantize(int palette);
void pressed_pick_gradient();

void choose_pattern(int typ);				// Bring up pattern chooser

void colour_selector( int cs_type );			// Bring up GTK+ colour wheel

int do_new_one(int nw, int nh, int nc, png_color *pal, int bpp, int undo);
void do_new_chores(int undo);
void reset_tools();

void memory_errors(int type);

void gradient_setup(int mode);

void pressed_skew();

void bkg_setup();

void pressed_segment();

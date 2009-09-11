/*	csel.h
	Copyright (C) 2006 Dmitry Groshev

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

int csel_center, csel_center_a, csel_limit, csel_limit_a;
int csel_mode, csel_invert;
double csel_range;
int csel_preview, csel_preview_a, csel_overlay;

int csel_scan(int start, int step, int cnt, unsigned char *mask, unsigned char *img);
void csel_eval();
void csel_reset();
int csel_init();

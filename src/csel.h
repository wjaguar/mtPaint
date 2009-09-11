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

#define CMAPSIZE (64 * 64 * 64 / 16)

typedef struct
{
	/* Input fields */
	int center, limit, center_a, limit_a;
	int mode, invert;
	double range;
	/* Cache fields */
	guint32 colormap[CMAPSIZE * 2];
	guint32 pmap[256 / 32];
	int pcache[256], cbase, irange, amin, amax;
	double clxn[3], cvec, range2;
} csel_info;
#define CSEL_SVSIZE ((size_t)(&((csel_info *)0)->colormap))

csel_info *csel_data;
int csel_preview, csel_preview_a, csel_overlay;

void init_cols();
void get_lxn(double *lxn, int col);

int csel_scan(int start, int step, int cnt, unsigned char *mask,
	unsigned char *img, csel_info *info);
double csel_eval(int mode, int center, int limit);
void csel_reset(csel_info *info);
csel_info *csel_init();

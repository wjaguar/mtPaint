/*	csel.h
	Copyright (C) 2006-2009 Dmitry Groshev

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
double gamma256[256], gamma64[64];
double midgamma256[256], midgamma64[64];
double kgamma256, kgamma64;
extern unsigned char ungamma256[], ungamma64[];

/* This gamma table is for when we need numeric stability */
#ifdef NATIVE_DOUBLES
#define Fgamma256 gamma256
#else
float Fgamma256[256];
#endif

#define UNGAMMA64(X) (ungamma64[(int)((X) * kgamma64)] - \
	((X) < midgamma64[ungamma64[(int)((X) * kgamma64)]]))
#define UNGAMMA256(X) (ungamma256[(int)((X) * kgamma256)] - \
	((X) < midgamma256[ungamma256[(int)((X) * kgamma256)]]))

//double gamma65536(int idx);
//int ungamma65536(double v);
int ungamma65281(double v); // Used in gradient engine

double rgb2B(double r, double g, double b);
void rgb2LXN(double *tmp, double r, double g, double b);
void init_cols();
void get_lxn(double *lxn, int col);

int csel_scan(int start, int step, int cnt, unsigned char *mask,
	unsigned char *img, csel_info *info);
double csel_eval(int mode, int center, int limit);
void csel_reset(csel_info *info);
void csel_init();

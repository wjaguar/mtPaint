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
#define CSEL_SVSIZE offsetof(csel_info, colormap)

csel_info *csel_data;
int csel_preview, csel_preview_a, csel_overlay;
double gamma256[256], gamma64[64];
double midgamma256[256];
int kgamma256;
extern unsigned char ungamma256[];

/* This gamma table is for when we need numeric stability */
#ifdef NATIVE_DOUBLES
#define Fgamma256 gamma256
#else
float Fgamma256[256];
#endif

static inline int UNGAMMA256(double x)
{
	int j = (int)(x * kgamma256);
	return (ungamma256[j] - (x < midgamma256[ungamma256[j]]));
}

static inline int UNGAMMA256X(double x)
{
	int j = (int)(x * kgamma256);
	return (j < 0 ? 0 : j >= kgamma256 ? 255 :
		ungamma256[j] - (x < midgamma256[ungamma256[j]]));
}

//double gamma65536(int idx);
//int ungamma65536(double v);

/* Used in and around gradient engine */
double gamma65281(int idx);
int ungamma65281(double v);

double rgb2B(double r, double g, double b);
void rgb2LXN(double *tmp, double r, double g, double b);
//void rgb2Lab(double *tmp, double r, double g, double b);
void init_cols();
void get_lxn(double *lxn, int col);

int csel_scan(int start, int step, int cnt, unsigned char *mask,
	unsigned char *img, csel_info *info);
double csel_eval(int mode, int center, int limit);
void csel_reset(csel_info *info);
void csel_init();

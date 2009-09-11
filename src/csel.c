/*	csel.c
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

#include <math.h>
#include <stdlib.h>
#include <gtk/gtk.h>

#include "global.h"

#include "memory.h"
#include "otherwindow.h"
#include "channels.h"

int csel_center = -1, csel_center_a, csel_limit, csel_limit_a;
int csel_mode, csel_invert;
double csel_range;
int csel_preview = 0x00FF00, csel_preview_a = 128, csel_overlay;

#define CMAPSIZE (64 * 64 * 64 / 16)

/* !!! Or allocate 128 K statically? !!! */
static guint32 *colormap;

static double gamma256[256], gamma64[64];
static guint32 pmap[256 / 32];
static int pcache[256], cbase, amin, amax, irange;
static double clxn[3], llxn[3], range2, cvec, lvec;

/* This nightmarish code does conversion from CIE XYZ into my own perceptually
 * uniform colour space L*X*N*. To produce it, I combined McAdam's colour space
 * and CIE lightness function, like it was done for L*u*v* space - the result
 * is a bitch to evaluate and impossible to reverse, but works much better
 * than both L*a*b and L*u*v for colour comparison - WJ */
static double wXN[2];
static void xyz2XN(double *XN, double x, double y, double z)
{
	double xyz = x + y + z;
	double k, xk, yk, sxk;

	/* Black is a darker white ;) */
	if (xyz == 0.0)
	{
		XN[0] = wXN[0]; XN[1] = wXN[1];
		return;
	}
	x /= xyz; y /= xyz;
	k = (x * 2.4 + y * 34.0 + 1.0) / 10.0;
	xk = x / k; yk = y / k;
	sxk = sqrt(xk);
	XN[0] = (xk * xk * (3751.0 - xk * xk * 10.0) -
		yk * yk * (520.0 - yk * 13295.0) +
		xk * yk * (32327.0 - xk * 25491.0 - yk * 41672.0 + xk * xk * 10.0) -
		sxk * 5227.0 + sqrt(sxk) * 2952.0) / 900.0;
//	k = (y * 4.2 - x + 1.0) / 10.0;
/* The "k" value below is incorrect, but I feel that in reality it's better -
 * it compresses the colour plane so that green and blue are farther from red
 * than from each other, which conforms to human perception - WJ */
	k = (y * 5.2 - x + 1.0) / 10.0;
	xk = x / k; yk = y / k;
	XN[1] = (yk * (404.0 - yk * (185.0 - yk * 52.0)) +
		xk * (69.0 - yk * (yk * (69.0 - yk * 30.0) + xk * 3.0))) / 900.0;
}

#ifndef cbrt
#define cbrt(X) pow((X), 1.0 / 3.0)
#endif
#define XX1 (16.0 / 116.0)
#define XX2 ((116.0 * 116.0) / (24.0 * 24.0 * 3.0))
#define XX3 ((24.0 * 24.0 * 24.0) / (116.0 * 116.0 * 116.0))
static double CIEpow(double x)
{
	return ((x > XX3) ? cbrt(x) : x * XX2 + XX1);
}

static double rxyz[3], gxyz[3], bxyz[3];
void rgb2LXN(double *tmp, double r, double g, double b)
{
	double x = r * rxyz[0] + g * gxyz[0] + b * bxyz[0];
	double y = r * rxyz[1] + g * gxyz[1] + b * bxyz[1];
	double z = r * rxyz[2] + g * gxyz[2] + b * bxyz[2];
	double L = CIEpow(y) * 116.0 - 16.0;
	double XN[2];
	xyz2XN(XN, x, y, z);
	tmp[0] = L * sqrt(2.0);
	tmp[1] = (XN[0] - wXN[0]) * L * 13.0;
	tmp[2] = (XN[1] - wXN[1]) * L * 13.0;
}

#define RED_X 0.640
#define RED_Y 0.330
#define GREEN_X 0.300
#define GREEN_Y 0.600
#define BLUE_X 0.150
#define BLUE_Y 0.060
#define WHITE_X 0.3127
#define WHITE_Y 0.329
static double det(double x1, double y1, double x2, double y2, double x3, double y3)
{
	return ((y1 - y2) * x3 + (y2 - y3) * x1 + (y3 - y1) * x2);
}
static void make_rgb_xyz(void)
{
	double rgbdet = det(RED_X, RED_Y, GREEN_X, GREEN_Y, BLUE_X, BLUE_Y) * WHITE_Y;
	double wr = det(WHITE_X, WHITE_Y, GREEN_X, GREEN_Y, BLUE_X, BLUE_Y) / rgbdet;
	double wg = det(RED_X, RED_Y, WHITE_X, WHITE_Y, BLUE_X, BLUE_Y) / rgbdet;
	double wb = det(RED_X, RED_Y, GREEN_X, GREEN_Y, WHITE_X, WHITE_Y) / rgbdet;
	rxyz[0] = RED_X * wr;
	rxyz[1] = RED_Y * wr;
	rxyz[2] = (1.0 - RED_X - RED_Y) * wr;
	gxyz[0] = GREEN_X * wg;
	gxyz[1] = GREEN_Y * wg;
	gxyz[2] = (1.0 - GREEN_X - GREEN_Y) * wg;
	bxyz[0] = BLUE_X * wb;
	bxyz[1] = BLUE_Y * wb;
	bxyz[2] = (1.0 - BLUE_X - BLUE_Y) * wb;
	xyz2XN(wXN, WHITE_X / WHITE_Y, 1.0, (1 - WHITE_X - WHITE_Y) / WHITE_Y);
}

static void make_gamma(double *Gamma, int cnt)
{
	int i, k;
	double mult = 1.0 / (double)(cnt - 1);

	k = floor(0.081 * (cnt - 1)) + 1;

	for (i = k; i < cnt; i++)
	{
		Gamma[i] = pow(((double)i * mult + 0.099) / 1.099, 1.0 / 0.45);
	}
	mult /= 4.5;
	for (i = 0; i < k; i++)
	{
		Gamma[i] = (double)i * mult;
	}
}

static void init_cols(void)
{
	make_gamma(gamma256, 256);
	make_gamma(gamma64, 64);
	make_rgb_xyz();
}

/* Get L*X*N* triple */
static void get_lxn(double *lxn, int col)
{
	rgb2LXN(lxn, gamma256[INT_2_R(col)], gamma256[INT_2_G(col)],
		gamma256[INT_2_B(col)]);
}

/* Get hue vector (0..1529) */
static double get_vect(int col)
{
	static int ixx[5] = {0, 1, 2, 0, 1};
	int rgb[3] = {INT_2_R(col), INT_2_G(col), INT_2_B(col)};
	int c1, c2, minc, midc, maxc;

	if (!((rgb[0] ^ rgb[1]) | (rgb[0] ^ rgb[2]))) return (0.0);

	c2 = rgb[2] < rgb[0] ? (rgb[1] < rgb[2] ? 2 : 0) : (rgb[0] < rgb[1] ? 1 : 2);
	minc = rgb[ixx[c2 + 2]];
	midc = rgb[ixx[c2 + 1]];
	c1 = midc > rgb[c2];
	midc -= c1 ? rgb[c2] : minc;
	maxc = rgb[ixx[c2 + c1]] - minc;
	return ((c2 + c2 + c1) * 255 + midc * 255 / (double)maxc);
}

/* Answer which pixels are masked through selectivity */
int csel_scan(int start, int step, int cnt, unsigned char *mask, unsigned char *img)
{
	unsigned char res = 0;
	double d, dist = 0.0, lxn[3];
	int i, j, k, l, jj, st3 = step * 3;

	cnt = start + step * (cnt - 1) + 1;
	if (!mask)
	{
		mask = &res - start;
		cnt = start + 1;
	}
	if (mem_img_bpp == 1)	/* Indexed image */
	{
		for (i = start; i < cnt; i += step)
		{
			j = img[i];
			k = PNG_2_INT(mem_pal[j]);
			if (pcache[j] != k)
			{
				pcache[j] = k;
				if (csel_mode == 0) /* Sphere mode */
				{
					get_lxn(lxn, k);
					dist = (lxn[0] - clxn[0]) * (lxn[0] - clxn[0]) +
						(lxn[1] - clxn[1]) * (lxn[1] - clxn[1]) +
						(lxn[2] - clxn[2]) * (lxn[2] - clxn[2]);
				}
				else if (csel_mode == 1) /* Angle mode */
				{
					dist = fabs(get_vect(k) - cvec);
					if (dist > 765.0) dist = 1530.0 - dist;
				}
				else if (csel_mode == 2) /* Cube mode */
				{
					l = abs(INT_2_R(csel_center) - INT_2_R(k));
					jj = abs(INT_2_G(csel_center) - INT_2_G(k));
					if (l < jj) l = jj;
					jj = abs(INT_2_B(csel_center) - INT_2_B(k));
					dist = l > jj ? l : jj;
				}
				if (dist <= range2) pmap[j >> 5] |= 1 << (j & 31);
			}
			if (((pmap[j >> 5] >> (j & 31)) ^ csel_invert) & 1)
				mask[i] |= 255;
		}
	}
	else if (csel_mode == 0)	/* RGB image, sphere mode */
	{
		img += start * 3;
		for (i = start; i < cnt; i += step , img += st3)
		{
			k = MEM_2_INT(img, 0) - cbase;
			if (k & 0xFFC0C0C0) /* Coarse map */
			{
				j = ((img[0] & 0xFC) << 6) + (img[1] & 0xFC) +
					(img[2] >> 6);
				k = (img[2] >> 1) & 0x1E;
			}
			else /* Fine map */
			{
				j = ((k & 0x3F0000) >> 8) + ((k & 0x3F00) >> 6) +
					((k & 0x3F) >> 4) + CMAPSIZE;
				k = (k + k) & 0x1E;
			}
			l = (colormap[j] >> k) & 3;
			if (!l) /* Not mapped */
			{
				if (j < CMAPSIZE) rgb2LXN(lxn, gamma64[img[0] >> 2],
					gamma64[img[1] >> 2], gamma64[img[2] >> 2]);
				else rgb2LXN(lxn, gamma256[img[0]], gamma256[img[1]],
					gamma256[img[2]]);
				dist = (lxn[0] - clxn[0]) * (lxn[0] - clxn[0]) +
					(lxn[1] - clxn[1]) * (lxn[1] - clxn[1]) +
					(lxn[2] - clxn[2]) * (lxn[2] - clxn[2]);
				l = dist <= range2 ? 3 : 2;
				colormap[j] |= l << k;
			}
			if ((l ^ csel_invert) & 1) mask[i] |= 255;
		}
	}
	else if (csel_mode == 1)	/* RGB image, angle mode */
	{
		static int ixx[5] = {0, 1, 2, 0, 1};

		img += start * 3;
		for (i = start; i < cnt; i += step , img += st3)
		{
			k = img[2] < img[0] ? (img[1] < img[2] ? 2 : 0) :
				(img[0] < img[1] ? 1 : 2);
			j = (img[ixx[k]] << 8) + img[ixx[k + 1]] -
				img[ixx[k + 2]] * 257;
			l = (colormap[j >> 3] >> ((j & 7) << 2)) & 0xF;
			if (!l) /* Not mapped */
			{
				/* Map 3 sectors at once */
				d = get_vect(j << 8) - cvec;
				for (jj = 1; jj < 8; jj += jj)
				{
					dist = fabs(d);
					d += 510.0;
					if (dist > 765.0) dist = 1530.0 - dist;
					if (dist <= range2) l += jj;
				}
				colormap[j >> 3] |= (l + 8) << ((j & 7) << 2);
			}
			if (((l >> k) ^ csel_invert) & 1) mask[i] |= 255;
		}
	}
	else if (csel_mode == 2)	/* RGB image, cube mode */
	{
		j = INT_2_R(csel_center);
		k = INT_2_G(csel_center);
		l = INT_2_B(csel_center);
		jj = csel_invert & 1;
		img += start * 3;
		for (i = start; i < cnt; i += step , img += st3)
		{
			if (((abs(j - img[0]) <= irange) &&
				(abs(k - img[1]) <= irange) &&
				(abs(l - img[2]) <= irange)) ^ jj)
				mask[i] |= 255;
		}
	}
	return (res);
}

/* Re-evaluate center/limit/range */
void csel_eval()
{
	switch (csel_mode)
	{
	case 0: /* L*X*N* spherical */
		get_lxn(clxn, csel_center);
		if (csel_range < 0.0)
		{
			get_lxn(llxn, csel_limit);
			range2 = (llxn[0] - clxn[0]) * (llxn[0] - clxn[0]) +
				(llxn[1] - clxn[1]) * (llxn[1] - clxn[1]) +
				(llxn[2] - clxn[2]) * (llxn[2] - clxn[2]);
			csel_range = sqrt(range2);
		}
		else range2 = csel_range * csel_range;
		break;
	case 1: /* RGB angular */
		cvec = get_vect(csel_center);
		if (csel_range < 0.0)
		{
			lvec = get_vect(csel_limit);
			csel_range = fabs(lvec - cvec);
			if (csel_range > 765.0) csel_range = 1530.0 - csel_range;
		}
		range2 = csel_range;
		break;
	case 2: /* RGB cubic */
		if (csel_range < 0.0)
		{
			int i, j, k;
			i = abs(INT_2_R(csel_center) - INT_2_R(csel_limit));
			j = abs(INT_2_G(csel_center) - INT_2_G(csel_limit));
			k = abs(INT_2_B(csel_center) - INT_2_B(csel_limit));
			irange = i > j ? i : j;
			if (irange < k) irange = k;
			csel_range = range2 = irange;
		}
		else irange = range2 = floor(csel_range);
		break;
	}
	if (csel_center_a < csel_limit_a)
	{
		amin = csel_center_a;
		amax = csel_limit_a;
	}
	else
	{
		amax = csel_center_a;
		amin = csel_limit_a;
	}
}

/* Clear bitmaps and setup range */
void csel_reset()
{
	int i, j, k, l;

	csel_eval();
	memset(colormap, 0, CMAPSIZE * 2 * sizeof(guint32));
	memset(pmap, 0, sizeof(pmap));
	memset(pcache, 255, sizeof(pcache));
	/* Find fine-precision base for L*X*N* */
	if (csel_mode == 0)
	{
		l = csel_center & 0xFCFCFC;
		i = INT_2_R(l);
		j = INT_2_G(l);
		k = INT_2_B(l);
		i = i > 32 ? i - 32 : 0;
		j = j > 32 ? j - 32 : 0;
		k = k > 32 ? k - 32 : 0;
		cbase = RGB_2_INT(i, j, k);
	}
}

/* Set center at A, limit at B */
int csel_init()
{
	colormap = malloc(2 * CMAPSIZE * sizeof(guint32));
	if (!colormap)
	{
		memory_errors(1);
		return (1);
	}
	init_cols();
	csel_center = PNG_2_INT(mem_col_A24);
	csel_limit = PNG_2_INT(mem_col_B24);
	csel_center_a = channel_col_A[CHN_ALPHA];
	csel_limit_a = channel_col_B[CHN_ALPHA];
	csel_range = -1.0;
	csel_reset();
	return (0);
}

/*	csel.c
	Copyright (C) 2006-2019 Dmitry Groshev

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

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "otherwindow.h"
#include "channels.h"
#include "csel.h"
#include "thread.h"

/* Use sRGB gamma if defined, ITU-R 709 gamma otherwise */
/* From my point of view, ITU-R 709 is a better model of a real CRT */
// #define SRGB

/* Use distorted L*X*N* model; the distortion possibly makes it better for *
 * smaller colour differences, but at extrema, error becomes unacceptable  */
// #define DISTORT_LXN

#define CIENUM 8192
#define EXPNUM 1024
#define EXPLOW (-0.1875)

csel_info *csel_data;
int csel_preview = 0x00FF00, csel_preview_a = 128, csel_overlay;

double gamma256[256], gamma64[64];
double midgamma256[256];

#ifndef NATIVE_DOUBLES
float Fgamma256[256];
#endif

static float CIE[CIENUM + 2];
static float EXP[EXPNUM];

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
	k = 10.0 / (x * 2.4 + y * 34.0 + xyz);
	xk = x * k; yk = y * k;
	sxk = sqrt(xk);
	XN[0] = (xk * xk * (3751.0 - xk * xk * 10.0) -
		yk * yk * (520.0 - yk * 13295.0) +
		xk * yk * (32327.0 - xk * 25491.0 - yk * 41672.0 + xk * xk * 10.0) -
		sxk * 5227.0 + sqrt(sxk) * 2952.0) / 900.0;
#ifndef DISTORT_LXN
	/* Do transform properly */
	k = 10.0 / (y * 4.2 - x + xyz);
#else
	/* This equation is incorrect, but I felt that in reality it's better -
	 * it compresses the colour plane so that green and blue are farther
	 * from red than from each other, which conforms to human perception.
	 * Yet for pure colours, distortion tends to become too high. - WJ */
	k = 10.0 / (y * 5.2 - x + xyz);
#endif
	xk = x * k; yk = y * k;
	XN[1] = (yk * (404.0 - yk * (185.0 - yk * 52.0)) +
		xk * (69.0 - yk * (yk * (69.0 - yk * 30.0) + xk * 3.0))) / 900.0;
}

static inline double CIElum(double x)
{
	int n;

	x *= CIENUM;
	n = x;
	return (CIE[n] + (CIE[n + 1] - CIE[n]) * (x - n));
}

static inline double exp10(double x)
{
	int n;

	x = (x - EXPLOW) * (EXPNUM + EXPNUM);
	n = x;
	return (EXP[n] + (EXP[n + 1] - EXP[n]) * (x - n));
}

/* MinGW has cube root function, glibc hasn't */
#ifndef WIN32
#define cbrt(X) pow((X), 1.0 / 3.0)
#endif

#define XX1 (16.0 / 116.0)
#define XX2 ((116.0 * 116.0) / (24.0 * 24.0 * 3.0))
#define XX3 ((24.0 * 24.0 * 24.0) / (116.0 * 116.0 * 116.0))
static inline double CIEpow(double x)
{
	return ((x > XX3) ? cbrt(x) : x * XX2 + XX1);
}

static double rxyz[3], gxyz[3], bxyz[3], wy;
void rgb2LXN(double *tmp, double r, double g, double b)
{
	double x = r * rxyz[0] + g * gxyz[0] + b * bxyz[0];
	double y = r * rxyz[1] + g * gxyz[1] + b * bxyz[1];
	double z = r * rxyz[2] + g * gxyz[2] + b * bxyz[2];
	double L = CIElum(y);
//	double L = CIEpow(y) * 116.0 - 16.0;
	double XN[2];
	xyz2XN(XN, x, y, z);
	/* Luminance's range must be near to chrominance's _diameter_ */
#ifndef DISTORT_LXN
	/* As recommended in literature (but sqrt(3) may be better) */
	tmp[0] = L * 2.0;
#else
	/* As felt good in practice */
	tmp[0] = L * M_SQRT2;
#endif
	tmp[1] = (XN[0] - wXN[0]) * L * 13.0;
	tmp[2] = (XN[1] - wXN[1]) * L * 13.0;
}

/* Get subjective brightness measure, by using Ware-Cowan formula */
double rgb2B(double r, double g, double b)
{
	double x = r * rxyz[0] + g * gxyz[0] + b * bxyz[0];
	double y = r * rxyz[1] + g * gxyz[1] + b * bxyz[1];
	double z = r * rxyz[2] + g * gxyz[2] + b * bxyz[2];
	z += x + y;
	if (z <= 0.0) return (y);
	z = 1.0 / z;
	x *= z;
	z *= y; /* It's not Z now, but y */
	z = 0.256 + (-0.184 + (-2.527 + 4.656 * x * x + 4.657 * z * z * z) * x) * z;
	y *= exp10(z) * wy;
	return (y > 1.0 ? 1.0 : y);
}

#if 0 /* Disable while not in use */
void rgb2Lab(double *tmp, double r, double g, double b)
{
	double x = r * rxyz[0] + g * gxyz[0] + b * bxyz[0];
	double y = r * rxyz[1] + g * gxyz[1] + b * bxyz[1];
	double z = r * rxyz[2] + g * gxyz[2] + b * bxyz[2];
	double L = CIElum(y);
	tmp[0] = L;
	tmp[1] = (CIElum(x * (WHITE_Y / WHITE_X)) - L) * (500.0 / 116.0);
	tmp[2] = (L - CIElum(z * (WHITE_Y / (1.0 - WHITE_X - WHITE_Y))) * (200.0 / 116.0);
}
#endif

/* ITU-R 709 */

#define RED_X 0.640
#define RED_Y 0.330
#define GREEN_X 0.300
#define GREEN_Y 0.600
#define BLUE_X 0.150
#define BLUE_Y 0.060

#if 1 /* 6500K whitepoint (D65) */
#define WHITE_X 0.3127
#define WHITE_Y 0.3290
/* Some, like lcms, use y=0.3291, but that isn't how ITU-R BT.709-5 defines D65,
 * but instead how SMPTE 240M does - WJ */
#else /* 9300K whitepoint */
#define WHITE_X 0.2848
#define WHITE_Y 0.2932
#endif

static inline double det(double x1, double y1, double x2, double y2, double x3, double y3)
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
	/* Normalize brightness of white */
	wy = 1.0 / exp10(0.256 + (-0.184 + (-2.527 + 4.656 * WHITE_X * WHITE_X +
		4.657 * WHITE_Y * WHITE_Y * WHITE_Y) * WHITE_X) * WHITE_Y);
}

#ifdef SRGB

#define GAMMA_POW 2.4
#define GAMMA_OFS 0.055
#if 1 /* Standard */
#define GAMMA_SPLIT 0.04045
#define GAMMA_DIV 12.92
#else /* C1 continuous */
#define GAMMA_SPLIT 0.03928
#define GAMMA_DIV 12.92321
#endif
#define KGAMMA 800
#define KGAMMA64K 12900

#else /* ITU-R 709 */

#define GAMMA_POW (1.0 / 0.45)
#define GAMMA_OFS 0.099
#define GAMMA_SPLIT 0.081
#define GAMMA_DIV 4.5
#define KGAMMA 300
#define KGAMMA64K 4550

#endif

static void make_gamma(double *Gamma, int cnt)
{
	int i, k;
	double mult = 1.0 / (double)(cnt - 1);

	k = (int)(GAMMA_SPLIT * (cnt - 1)) + 1;

	for (i = k; i < cnt; i++)
	{
		Gamma[i] = pow(((double)i * mult + GAMMA_OFS) /
			(1.0 + GAMMA_OFS), GAMMA_POW);
	}
	mult /= GAMMA_DIV;
	for (i = 0; i < k; i++)
	{
		Gamma[i] = (double)i * mult;
	}
}

int kgamma256 = KGAMMA * 4;
unsigned char ungamma256[KGAMMA * 4 + 1];

static void make_ungamma(double *Gamma, double *Midgamma,
	unsigned char *Ungamma,	int kgamma, int cnt)
{
	int i, j, k = 0;

	Midgamma[0] = 0.0;
	for (i = 1; i < cnt; i++)
	{
		Midgamma[i] = (Gamma[i - 1] + Gamma[i]) / 2.0;
		j = Midgamma[i] * kgamma;
		for (; k < j; k++) Ungamma[k] = i - 1;
	}
	for (; k <= kgamma; k++) Ungamma[k] = cnt - 1;
}

/* 16-bit gamma correction */

static double gamma64K[255 * 4 + 2];
static double gammaslope64K[255 * 4 + 1];
static unsigned short ungamma64K[KGAMMA64K + 1];

#if 0 /* Disable while not in use */
double gamma65536(int idx)
{
	int n;

	idx *= 4;
	n = idx / 257;
	return ((idx % 257) * (1.0 / 257.0) * (gamma64K[n + 1] - gamma64K[n]) +
		gamma64K[n]);
}

int ungamma65536(double v)
{
	int n = ungamma64K[(int)(v * KGAMMA64K)];

	n -= v < gamma64K[n];
	return ((int)((n + (v - gamma64K[n]) * gammaslope64K[n]) * 64.25 + 0.5));
}
#endif

double gamma65281(int idx)
{
	int n;

	n = idx >> 6;
	return ((idx & 0x3F) * (1.0 / 64.0) * (gamma64K[n + 1] - gamma64K[n]) +
		gamma64K[n]);
}

int ungamma65281(double v)
{
	int n = ungamma64K[(int)(v * KGAMMA64K)];

	n -= v < gamma64K[n];
	return ((int)((n + (v - gamma64K[n]) * gammaslope64K[n]) * 64.0 + 0.5));
}

static void make_ungamma64K()
{
	int i, j, k = 0;

	for (i = 1; i < 255 * 4 + 1; i++)
	{
		gammaslope64K[i - 1] = 1.0 / (gamma64K[i] - gamma64K[i - 1]);
		j = gamma64K[i] * KGAMMA64K;
		for (; k < j; k++) ungamma64K[k] = i - 1;
	}
	gammaslope64K[255 * 4] = 0.0;
	for (; k <= KGAMMA64K; k++) ungamma64K[k] = 255 * 4;
}

static void make_CIE(void)
{
	int i;

	for (i = 0; i < CIENUM; i++)
	{
		CIE[i] = CIEpow(i * (1.0 / CIENUM)) * 116.0 - 16.0;
	}
	CIE[CIENUM] = CIE[CIENUM + 1] = 100.0;
}

static void make_EXP(void)
{
	int i;

	for (i = 0; i < EXPNUM; i++)
	{
		EXP[i] = exp(M_LN10 * (i * (0.5 / EXPNUM) + EXPLOW));
	}
}

void init_cols(void)
{
	make_gamma(gamma64K, 255 * 4 + 1);
	gamma64K[255 * 4 + 1] = 1.0;
	make_gamma(gamma256, 256);
	make_gamma(gamma64, 64);
	make_ungamma64K();
	make_ungamma(gamma256, midgamma256, ungamma256, kgamma256, 256);
	make_CIE();
	make_EXP();
	make_rgb_xyz();
#ifndef NATIVE_DOUBLES
	/* Fill reduced-precision gamma table */
	{
		int i;
		for (i = 0; i < 256; i++) Fgamma256[i] = gamma256[i];
	}
#endif
}

/* Get L*X*N* triple */
void get_lxn(double *lxn, int col)
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

#if defined(U_THREADS) && defined(HAVE__SFA)
/* Hope this and __sync_fetch_and_add() always come as a package */
#define SETBIT(A,B) __sync_fetch_and_or(&(A), (B))
#else
#define SETBIT(A,B) (A) |= (B)
#endif


/* Answer which pixels are masked through selectivity */
int csel_scan(int start, int step, int cnt, unsigned char *mask,
	unsigned char *img, csel_info *info)
{
	unsigned char res = 0;
	double d, dist = 0.0, lxn[3];
	int i, j, k, l, jj, st3 = step * 3;
#ifndef HAVE__SFA
	DEF_MUTEX(csel_lock); // To prevent concurrent writes to *info


	LOCK_MUTEX(csel_lock);
#endif
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
			if (info->pcache[j] != k)
			{
				if (info->mode == 0) /* Sphere mode */
				{
					get_lxn(lxn, k);
					dist = (lxn[0] - info->clxn[0]) *
						(lxn[0] - info->clxn[0]) +
						(lxn[1] - info->clxn[1]) *
						(lxn[1] - info->clxn[1]) +
						(lxn[2] - info->clxn[2]) *
						(lxn[2] - info->clxn[2]);
				}
				else if (info->mode == 1) /* Angle mode */
				{
					dist = fabs(get_vect(k) - info->cvec);
					if (dist > 765.0) dist = 1530.0 - dist;
				}
				else if (info->mode == 2) /* Cube mode */
				{
					l = abs(INT_2_R(info->center) - INT_2_R(k));
					jj = abs(INT_2_G(info->center) - INT_2_G(k));
					if (l < jj) l = jj;
					jj = abs(INT_2_B(info->center) - INT_2_B(k));
					dist = l > jj ? l : jj;
				}
				if (dist <= info->range2)
					SETBIT(info->pmap[j >> 5], 1 << (j & 31));
				info->pcache[j] = k;
			}
			if (((info->pmap[j >> 5] >> (j & 31)) ^ info->invert) & 1)
				mask[i] |= 255;
		}
	}
	else if (info->mode == 0)	/* RGB image, sphere mode */
	{
		img += start * 3;
		for (i = start; i < cnt; i += step , img += st3)
		{
			k = MEM_2_INT(img, 0) - info->cbase;
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
			l = (info->colormap[j] >> k) & 3;
			if (!l) /* Not mapped */
			{
				if (j < CMAPSIZE) rgb2LXN(lxn, gamma64[img[0] >> 2],
					gamma64[img[1] >> 2], gamma64[img[2] >> 2]);
				else rgb2LXN(lxn, gamma256[img[0]], gamma256[img[1]],
					gamma256[img[2]]);
				dist = (lxn[0] - info->clxn[0]) *
					(lxn[0] - info->clxn[0]) +
					(lxn[1] - info->clxn[1]) *
					(lxn[1] - info->clxn[1]) +
					(lxn[2] - info->clxn[2]) *
					(lxn[2] - info->clxn[2]);
				l = dist <= info->range2 ? 3 : 2;
				SETBIT(info->colormap[j], l << k);
			}
			if ((l ^ info->invert) & 1) mask[i] |= 255;
		}
	}
	else if (info->mode == 1)	/* RGB image, angle mode */
	{
		static int ixx[5] = {0, 1, 2, 0, 1};

		img += start * 3;
		for (i = start; i < cnt; i += step , img += st3)
		{
			k = img[2] < img[0] ? (img[1] < img[2] ? 2 : 0) :
				(img[0] < img[1] ? 1 : 2);
			j = (img[ixx[k]] << 8) + img[ixx[k + 1]] -
				img[ixx[k + 2]] * 257;
			l = (info->colormap[j >> 3] >> ((j & 7) << 2)) & 0xF;
			if (!l) /* Not mapped */
			{
				/* Map 3 sectors at once */
				d = get_vect(j << 8) - info->cvec;
				for (jj = 1; jj < 8; jj += jj)
				{
					dist = fabs(d);
					d += 510.0;
					if (dist > 765.0) dist = 1530.0 - dist;
					if (dist <= info->range2) l += jj;
				}
				SETBIT(info->colormap[j >> 3], (l + 8) << ((j & 7) << 2));
			}
			if (((l >> k) ^ info->invert) & 1) mask[i] |= 255;
		}
	}
	else if (info->mode == 2)	/* RGB image, cube mode */
	{
		j = INT_2_R(info->center);
		k = INT_2_G(info->center);
		l = INT_2_B(info->center);
		jj = info->invert & 1;
		img += start * 3;
		for (i = start; i < cnt; i += step , img += st3)
		{
			if (((abs(j - img[0]) <= info->irange) &&
				(abs(k - img[1]) <= info->irange) &&
				(abs(l - img[2]) <= info->irange)) ^ jj)
				mask[i] |= 255;
		}
	}
#ifndef HAVE__SFA
	UNLOCK_MUTEX(csel_lock);
#endif
	return (res);
}

/* Evaluate range */
double csel_eval(int mode, int center, int limit)
{
	double cw[3], lw[3], cwv, lwv, r2;
	int i, j, k, ir;

	switch (mode)
	{
	case 0: /* L*X*N* spherical */
		get_lxn(cw, center);
		get_lxn(lw, limit);
		r2 = (lw[0] - cw[0]) * (lw[0] - cw[0]) +
			(lw[1] - cw[1]) * (lw[1] - cw[1]) +
			(lw[2] - cw[2]) * (lw[2] - cw[2]);
		return (sqrt(r2));
	case 1: /* RGB angular */
		cwv = get_vect(center);
		lwv = get_vect(limit);
		r2 = fabs(lwv - cwv);
		if (r2 > 765.0) r2 = 1530.0 - r2;
		return (r2);
	case 2: /* RGB cubic */
		i = abs(INT_2_R(center) - INT_2_R(limit));
		j = abs(INT_2_G(center) - INT_2_G(limit));
		k = abs(INT_2_B(center) - INT_2_B(limit));
		ir = i > j ? i : j;
		if (ir < k) ir = k;
		return ((double)ir);
	}
	return (0.0);
}

/* Clear bitmaps and setup extra vars */
void csel_reset(csel_info *info)
{
	int i, j, k, l;

	memset(info->colormap, 0, sizeof(info->colormap));
	memset(info->pmap, 0, sizeof(info->pmap));
	memset(info->pcache, 255, sizeof(info->pcache));
	switch (info->mode)
	{
	case 0: /* L*X*N* sphere */
		get_lxn(info->clxn, info->center);
		info->range2 = info->range * info->range;
		/* Find fine-precision base */
		l = info->center & 0xFCFCFC;
		i = INT_2_R(l);
		j = INT_2_G(l);
		k = INT_2_B(l);
		i = i > 32 ? i - 32 : 0;
		j = j > 32 ? j - 32 : 0;
		k = k > 32 ? k - 32 : 0;
		info->cbase = RGB_2_INT(i, j, k);
		break;
	case 1: /* RGB angular */
		info->cvec = get_vect(info->center);
		info->range2 = info->range;
		break;
	case 2: /* RGB cubic */
		info->irange = info->range2 = (int)(info->range);
		break;
	}
	if (info->center_a < info->limit_a)
	{
		info->amin = info->center_a;
		info->amax = info->limit_a;
	}
	else
	{
		info->amax = info->center_a;
		info->amin = info->limit_a;
	}
}

/* Set center at A, limit at B */
void csel_init()
{
	csel_info *info;

	info = ALIGN(calloc(1, sizeof(csel_info) + sizeof(double)));
	if (!info)
	{
		memory_errors(1);
		return;
	}
	info->center = PNG_2_INT(mem_col_A24);
	info->limit = PNG_2_INT(mem_col_B24);
	info->center_a = channel_col_A[CHN_ALPHA];
	info->limit_a = channel_col_B[CHN_ALPHA];
	info->range = csel_eval(0, info->center, info->limit);
	csel_reset(info);
	csel_data = info;
}

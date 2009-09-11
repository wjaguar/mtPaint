/*	mtlib.c
	Copyright (C) 2005-2006 Mark Tyler

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

#include <stdio.h>
#include <time.h>
#include <math.h>

#include "mtlib.h"


MT_Coor MT_coze()			// Return zero coordinates
{
	MT_Coor ret;
	ret.x = 0;	ret.y = 0;	ret.z = 0;
	return ret;
}

MT_Coor MT_co_div_k(MT_Coor AA, double BB)			// Divide coords/vector by constant
{
	MT_Coor ret;
	ret.x = AA.x / BB;	ret.y = AA.y / BB;	ret.z = AA.z / BB;
	return ret;
}

MT_Coor MT_co_mul_k(MT_Coor AA, double BB)			// Multiply coords/vector by constant
{
	MT_Coor ret;
	ret.x = AA.x * BB;	ret.y = AA.y * BB;	ret.z = AA.z * BB;
	return ret;
}

MT_Coor MT_addco(MT_Coor AA, MT_Coor BB)			// Add two coords together (AA+BB)
{
	MT_Coor ret;
	ret.x = AA.x + BB.x;	ret.y = AA.y + BB.y;	ret.z = AA.z + BB.z;
	return ret;
}

MT_Coor MT_subco(MT_Coor AA, MT_Coor BB)			// Add two coords together (AA-BB)
{
	MT_Coor ret;
	ret.x = AA.x - BB.x;	ret.y = AA.y - BB.y;	ret.z = AA.z - BB.z;
	return ret;
}

double MT_lin_len(MT_Coor AA, MT_Coor BB)		// Return length of line between two coordinates
{
	return sqrt( (BB.x-AA.x)*(BB.x-AA.x) + (BB.y-AA.y)*(BB.y-AA.y) + (BB.z-AA.z)*(BB.z-AA.z) );
}

double MT_lin_len2(MT_Coor AA)		// Return length of vector
{
	return sqrt( AA.x*AA.x + AA.y*AA.y + AA.z*AA.z );
}

MT_Coor MT_uni_vec(MT_Coor AA, MT_Coor BB)	// Return unit vector between two coords (A to B)
{
	MT_Coor ret;
	double lenny = MT_lin_len(AA, BB);

	if (lenny != 0)
	{
		ret.x = (BB.x - AA.x) / lenny;
		ret.y = (BB.y - AA.y) / lenny;
		ret.z = (BB.z - AA.z) / lenny;
	}
	else
	{
		ret.x = 0;
		ret.y = 0;
		ret.z = 0;
	}

	return ret;
}

MT_Coor MT_uni_vec2(MT_Coor AA)	// Return unit vector
{
	MT_Coor ret;
	double lenny = MT_lin_len2(AA);

	ret = MT_co_div_k(AA, lenny);

	return ret;
}

MT_Coor MT_palin(double position, double ratio, MT_Coor p1, MT_Coor p2, MT_Coor p3, MT_Coor p4, MT_Coor lenz)
{	//	Parabolic Linear Interpolation from point 2 to point 3 at position (0-1) and ratio (0=flat, 0.25=curvy, 1=very bendy).  lenz contains 3 valuess for the number of frames in each of the 3 lines : p1->p2, p2->p3, p3->p4
	MT_Coor res, mmm, mmm2, d[4], dd[4];
	double lenny, lenny1, lenny2, qa, qb, fac, fbc;

	lenny = ratio * MT_lin_len(p2, p3);		// Distance of mid line
	lenny1 = ratio * MT_lin_len(p1, p2);		// Distance of 1st line
	lenny2 = ratio * MT_lin_len(p3, p4);		// Distance of 3rd line

	if (lenny == 0)
	{
		mmm = MT_coze();
		mmm2 = MT_coze();
	}
	else
	{
		qa = lenny1 / lenny;			// Adjust acceleration for line length
		qb = lenny2 / lenny;
		if (qa > 1) qa = 0; else qa = (qa-1.0) / 3.0;
		if (qb > 1) qb = 0; else qb = -(qb-1.0) / 3.0;
		fac = 1.0/3.0 + qa;
		fbc = 2.0/3.0 + qb;
		position = 3*position*(1-position)*(1-position)*fac + 3*position*position*(1-position)*fbc + position*position*position;

		qa = lenz.y / lenz.x;			// Adjust acceleration for points in between
		qb = lenz.y / lenz.z;
		if (qa >= 1) qa = 0; else qa = (qa-1.0) / 3.0;
		if (qb >= 1) qb = 0; else qb = -(qb-1.0) / 3.0;
		fac = 1.0/3.0 + qa;
		fbc = 2.0/3.0 + qb;
		position = 3*position*(1-position)*(1-position)*fac + 3*position*position*(1-position)*fbc + position*position*position;


		d[0] = MT_uni_vec(p1, p2);		// Get unit vectors for 1st 2 lines
		d[1] = MT_uni_vec(p2, p3);		// Get unit vectors for 1st 2 lines
		mmm = MT_addco(d[0], d[1]);
		mmm = MT_uni_vec2(mmm);
		mmm = MT_co_mul_k(mmm, lenny);		// mult by lenny

		d[2] = MT_uni_vec(p4, p3);		// Get unit vectors for 2nd 2 lines
		d[3] = MT_uni_vec(p3, p2);
		mmm2 = MT_addco(d[2], d[3]);
		mmm2 = MT_uni_vec2(mmm2);
		mmm2 = MT_co_mul_k(mmm2, lenny);	// mult by lenny
	}
	dd[1] = MT_addco(p2, mmm);			// Control point 1
	dd[2] = MT_addco(p3, mmm2);			// Control point 2

	res.x = (1-position)*(1-position)*(1-position)*p2.x + 3*position*(1-position)*(1-position)*dd[1].x + 3*(1-position)*position*position*dd[2].x + position*position*position*p3.x;
	res.y = (1-position)*(1-position)*(1-position)*p2.y + 3*position*(1-position)*(1-position)*dd[1].y + 3*(1-position)*position*position*dd[2].y + position*position*position*p3.y;
	res.z = (1-position)*(1-position)*(1-position)*p2.z + 3*position*(1-position)*(1-position)*dd[1].z + 3*(1-position)*position*position*dd[2].z + position*position*position*p3.z;

	return res;
}

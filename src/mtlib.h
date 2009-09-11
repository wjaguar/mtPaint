/*	mtlib.h
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


typedef struct
{	double x,y,z;	}	MT_Coor;


MT_Coor MT_coze();				// Return zero coordinates
MT_Coor MT_co_div_k(MT_Coor AA, double BB);	// Divide coords/vector by constant
MT_Coor MT_co_mul_k(MT_Coor AA, double BB);	// Multiply coords/vector by constant
MT_Coor MT_addco(MT_Coor AA, MT_Coor BB);	// Add two coords together (AA+BB)
MT_Coor MT_subco(MT_Coor AA, MT_Coor BB);	// Add two coords together (AA-BB)
double MT_lin_len(MT_Coor AA, MT_Coor BB);	// Return length of line between two coordinates
double MT_lin_len2(MT_Coor AA);			// Return length of vector
MT_Coor MT_uni_vec(MT_Coor AA, MT_Coor BB);	// Return unit vector between two coords (A to B)
MT_Coor MT_uni_vec2(MT_Coor AA);		// Return unit vector
MT_Coor MT_palin(double position, double ratio, MT_Coor p1, MT_Coor p2, MT_Coor p3, MT_Coor p4, MT_Coor lenz);
						// Parabolic Linear Interpolation from point 2 to point 3 at position (0-1) and ratio (0=flat, 0.25=curvy, 1=very bendy)

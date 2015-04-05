/*	polygon.h
	Copyright (C) 2005-2015 Mark Tyler and Dmitry Groshev

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

///	STRUCTURES / GLOBALS

		
#define MAX_POLY 1000
				// Maximum points on any polygon

int poly_points;
int poly_mem[MAX_POLY][2];
		// Coords in poly_mem are raw coords as plotted over image
int poly_xy[4];

#define poly_min_x poly_xy[0]
#define poly_min_y poly_xy[1]
#define poly_max_x poly_xy[2]
#define poly_max_y poly_xy[3]

///	PROCEDURES

void poly_add(int x, int y);	// Add point to polygon
void poly_bounds();		// Determine polygon boundaries

void poly_draw(int filled, unsigned char *buf, int wbuf);
void poly_mask();		// Paint polygon onto clipboard mask
void poly_paint();		// Paint polygon onto image
void poly_outline();		// Paint polygon outline onto image
void poly_lasso(int poly);	// Lasso around current clipboard

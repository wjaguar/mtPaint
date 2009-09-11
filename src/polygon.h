/*	polygon.h
	Copyright (C) 2005-2008 Mark Tyler and Dmitry Groshev

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

int poly_mem[MAX_POLY][2], poly_points, poly_min_x, poly_max_x, poly_min_y, poly_max_y;
				// Coords in poly_mem are raw coords as plotted over image


///	PROCEDURES

void poly_add(int x, int y);	// Add point to polygon

int poly_init();		// Setup max/min -> Requires points in poly_mem: needed for all below:

void poly_draw(int filled, unsigned char *buf, int wbuf);
void poly_mask();		// Paint polygon onto clipboard mask
void poly_paint();		// Paint polygon onto image
void poly_outline();		// Paint polygon outline onto image
void poly_lasso();		// Lasso around current clipboard

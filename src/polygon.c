/*	polygon.c
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

#include <stdlib.h>
#include <math.h>

#include <gtk/gtk.h>

#include "polygon.h"
#include "mygtk.h"
#include "memory.h"

/* !!! Currently, poly_points should be set to 0 when there's no polygonal
 * selection, because poly_lasso() depends on that - WJ */
int poly_points;
int poly_mem[MAX_POLY][2];
		// Coords in poly_mem are raw coords as plotted over image
int poly_xy[4];


static int cmp_spans(const void *span1, const void *span2)
{
	return (((int *)span1)[2] - ((int *)span2)[2]);
}

/* !!! This code clips polygon to image boundaries, and when using buffer
 * assumes it covers the intersection area - WJ */
void poly_draw(int filled, unsigned char *buf, int wbuf)
{
#define SPAN_STEP 5 /* x0, x1, y0, y1, next */
	linedata line;
	unsigned char borders[MAX_WIDTH];
	int spans[(MAX_POLY + 1) * SPAN_STEP], *span, *span2;
	int i, j, k, nspans, y, rxy[4];
	int oldmode = mem_undo_opacity;


	/* Intersect image & polygon rectangles */
	clip(rxy, 0, 0, mem_width - 1, mem_height - 1, poly_xy);
	// !!! clip() can intersect inclusive rectangles, but not check result
	if ((rxy[0] > rxy[2]) || (rxy[1] > rxy[3])) return;

	/* Adjust buffer pointer */
	if (buf) buf -= rxy[1] * wbuf + rxy[0];

	mem_undo_opacity = TRUE;

	j = poly_points - 1;
	for (i = 0; i < poly_points; j = i++)
	{
		int x0 = poly_mem[j][0], y0 = poly_mem[j][1];
		int x1 = poly_mem[i][0], y1 = poly_mem[i][1];

		if (!filled)
		{
			f_circle(x0, y0, tool_size);
			tline(x0, y0, x1, y1, tool_size);
		}
		else if (!buf) sline(x0, y0, x1, y1);
		else
		{
			int tk;

			line_init(line, x0, y0, x1, y1);
			if (line_clip(line, rxy, &tk) < 0) continue;
			for (; line[2] >= 0; line_step(line))
			{
				buf[line[0] + line[1] * wbuf] = 255;
			}
		}
		// Outline is needed to properly edge the polygon
	}

	if (!filled) goto done;	// If drawing outline only, finish now

	/* Build array of vertical spans */
	span = spans + SPAN_STEP;
	j = poly_points - 1;
	for (i = 0; i < poly_points; j = i++)
	{
		int x0 = poly_mem[j][0], y0 = poly_mem[j][1];
		int x1 = poly_mem[i][0], y1 = poly_mem[i][1];

		// No use for horizontal spans
		if (y0 == y1) continue;
		// Order points by increasing Y
		k = y0 > y1;
		span[k] = x0;
		span[k + 2] = y0;
		k ^= 1;
		span[k] = x1;
		span[k + 2] = y1;
		// Check vertical boundaries
		if ((span[3] <= 0) || (span[2] >= mem_height - 1)) continue;
		// Accept the span
		span += SPAN_STEP;
	}
	nspans = (span - spans) / SPAN_STEP - 1;
	if (!nspans) goto done; // No interior to fill

	/* Sort and link spans */
	qsort(spans + SPAN_STEP, nspans, SPAN_STEP * sizeof(int), cmp_spans);
	for (i = 0; i < nspans; i++) spans[i * SPAN_STEP + 4] = i + 1;
	spans[nspans * SPAN_STEP + 4] = 0; // Chain terminator
	spans[2] = spans[3] = MAX_HEIGHT; // Loops breaker

	/* Let's scan! */
	memset(borders, 0, mem_width);
	y = spans[SPAN_STEP + 2] + 1;
	if (y < 0) y = 0;
	for (; y < mem_height; y++)
	{
		unsigned char *bp, tv = 0;
		int i, x, x0 = mem_width, x1 = 0;

		/* Label the intersections */
		if (!spans[4]) break; // List is empty
		span = spans;
		while (TRUE)
		{
			int dx, dy;

			// Unchain used-up spans
			while ((span2 = spans + span[4] * SPAN_STEP)[3] < y)
				span[4] = span2[4];
			if (y <= span2[2]) break; // Y too small yet
			span = span2;
			dx = span[1] - span[0];
			dy = span[3] - span[2];
			x = (dx * 2 * (y - span[2]) + dy) / (dy * 2) + span[0];
			if (x >= mem_width) x1 = mem_width; // Fill to end
			else
			{
				if (x < 0) x = 0;
				if (x0 > x) x0 = x;
				if (x1 <= x) x1 = x + 1;
				borders[x] ^= 1;
			}
		}

		/* Draw the runs */
		if (x0 >= mem_width) continue; // No pixels
		bp = buf ? buf + y * wbuf : NULL;
		for (i = x0; i < x1; i++)
		{
			if (!(tv ^= borders[i])) continue;
			if (bp) bp[i] = 255;
			else put_pixel(i, y);
		}
		memset(borders + x0, 0, x1 - x0);
	}	

done:	mem_undo_opacity = oldmode;
#undef SPAN_STEP
}

void poly_mask()	// Paint polygon onto clipboard mask
{
	mem_clip_mask_init(0);		/* Clear mask */
	if (!mem_clip_mask) return;	/* Failed to get memory */
	poly_draw(TRUE, mem_clip_mask, mem_clip_w);
}

void poly_paint()	// Paint polygon onto image
{
	poly_draw(TRUE, NULL, 0);
}

void poly_outline()	// Paint polygon outline onto image
{
	poly_draw(FALSE, NULL, 0);
}

void poly_add(int x, int y)	// Add point to list
{
	if (!poly_points)
	{
		poly_min_x = poly_max_x = x;
		poly_min_y = poly_max_y = y;
	}
	else
	{
		if (poly_points >= MAX_POLY) return;
		if (poly_min_x > x) poly_min_x = x;
		if (poly_max_x < x) poly_max_x = x;
		if (poly_min_y > y) poly_min_y = y;
		if (poly_max_y < y) poly_max_y = y;
	}

	poly_mem[poly_points][0] = x;
	poly_mem[poly_points][1] = y;
	poly_points++;
}


void flood_fill_poly( int x, int y, unsigned int target )
{
	int minx = x, maxx = x, ended = 0, newx = 0;

	mem_clip_mask[ x + y*mem_clip_w ] = 1;
	while ( ended == 0 )				// Search left for target pixels
	{
		minx--;
		if ( minx < 0 ) ended = 1;
		else
		{
			if ( mem_clipboard[ minx + y*mem_clip_w ] == target )
				mem_clip_mask[ minx + y*mem_clip_w ] = 1;
			else ended = 1;
		}
	}
	minx++;

	ended = 0;
	while ( ended == 0 )				// Search right for target pixels
	{
		maxx++;
		if ( maxx >= mem_clip_w ) ended = 1;
		else
		{
			if ( mem_clipboard[ maxx + y*mem_clip_w ] == target )
				mem_clip_mask[ maxx + y*mem_clip_w ] = 1;
			else ended = 1;
		}
	}
	maxx--;

	if ( (y-1) >= 0 )				// Recurse upwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( mem_clipboard[ newx + (y-1)*mem_clip_w ] == target &&
				mem_clip_mask[newx + mem_clip_w*(y-1)] != 1 )
					flood_fill_poly( newx, y-1, target );

	if ( (y+1) < mem_clip_h )			// Recurse downwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( mem_clipboard[ newx + (y+1)*mem_clip_w ] == target &&
				mem_clip_mask[newx + mem_clip_w*(y+1)] != 1 )
					flood_fill_poly( newx, y+1, target );
}

void flood_fill24_poly( int x, int y, int target )
{
	int minx = x, maxx = x, ended = 0, newx = 0;

	mem_clip_mask[ x + y*mem_clip_w ] = 1;
	while ( ended == 0 )				// Search left for target pixels
	{
		minx--;
		if ( minx < 0 ) ended = 1;
		else
		{
			if ( MEM_2_INT(mem_clipboard, 3*(minx + mem_clip_w*y) ) == target )
				mem_clip_mask[ minx + y*mem_clip_w ] = 1;
			else ended = 1;
		}
	}
	minx++;

	ended = 0;
	while ( ended == 0 )				// Search right for target pixels
	{
		maxx++;
		if ( maxx >= mem_clip_w ) ended = 1;
		else
		{
			if ( MEM_2_INT(mem_clipboard, 3*(maxx + mem_clip_w*y) ) == target )
				mem_clip_mask[ maxx + y*mem_clip_w ] = 1;
			else ended = 1;
		}
	}
	maxx--;

	if ( (y-1) >= 0 )				// Recurse upwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( MEM_2_INT(mem_clipboard, 3*(newx + mem_clip_w*(y-1)) ) == target
				&& mem_clip_mask[newx + mem_clip_w*(y-1)] != 1 )
					flood_fill24_poly( newx, y-1, target );

	if ( (y+1) < mem_clip_h )			// Recurse downwards
		for ( newx = minx; newx <= maxx; newx++ )
			if ( MEM_2_INT(mem_clipboard, 3*(newx + mem_clip_w*(y+1)) ) == target
				&& mem_clip_mask[newx + mem_clip_w*(y+1)] != 1 )
					flood_fill24_poly( newx, y+1, target );
}

void poly_lasso()		// Lasso around current clipboard
{
	int i, j, x = 0, y = 0;

	if (!mem_clip_mask) return;	/* Nothing to do */

	/* Fill seed is the first point of polygon, if any,
	 * or top left corner of the clipboard by default */
	if (poly_points)
	{
		x = poly_mem[0][0] - poly_min_x;
		y = poly_mem[0][1] - poly_min_y;
		if ((x < 0) || (x >= mem_clip_w) || (y < 0) || (y >= mem_clip_h))
			x = y = 0; // Point is outside clipboard
	}

	if ( mem_clip_bpp == 1 )
	{
		j = mem_clipboard[x + y*mem_clip_w];
		flood_fill_poly( x, y, j );
	}
	if ( mem_clip_bpp == 3 )
	{
		i = 3*(x + y*mem_clip_w);
		j = MEM_2_INT(mem_clipboard, i);
		flood_fill24_poly( x, y, j );
	}

	j = mem_clip_w*mem_clip_h;
	for ( i=0; i<j; i++ )
		if ( mem_clip_mask[i] == 1 ) mem_clip_mask[i] = 0;
			// Turn flood (1) into clear (0)
}

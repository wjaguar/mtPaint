/*	polygon.c
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

#include <stdlib.h>

#include "polygon.h"
#include "memory.h"
#include "otherwindow.h"


int poly_mem[MAX_POLY][2], poly_points=0, poly_min_x, poly_max_x, poly_min_y, poly_max_y;
				// Coords in poly_mem are raw coords as plotted over image



int poly_init()			// Setup max/min -> Requires points in poly_mem
{
	int i;

	if ( poly_points>2 && poly_points<=MAX_POLY )
	{
		poly_min_x = poly_mem[0][0];
		poly_max_x = poly_mem[0][0];
		poly_min_y = poly_mem[0][1];
		poly_max_y = poly_mem[0][1];
		for (i=1; i<poly_points; i++)
		{
			mtMIN( poly_min_x, poly_min_x, poly_mem[i][0] )
			mtMAX( poly_max_x, poly_max_x, poly_mem[i][0] )
			mtMIN( poly_min_y, poly_min_y, poly_mem[i][1] )
			mtMAX( poly_max_y, poly_max_y, poly_mem[i][1] )
		}
	}
	else return 1;		// Bogus polygon

	return 0;		// Success
}

void memline( int x1, int y1, int x2, int y2 )	// Draw single thickness straight line to clipboard mask
{
	int i, xdo, ydo, px, py, todo;
	float rat;

	xdo = x2 - x1;
	ydo = y2 - y1;
	mtMAX( todo, abs(xdo), abs(ydo) )
	if (todo==0) todo=1;

	for ( i=0; i<=todo; i++ )
	{
		rat = ((float) i ) / todo;
		px = mt_round(x1 + (x2 - x1) * rat);
		py = mt_round(y1 + (y2 - y1) * rat);
		mem_clip_mask[px + py*mem_clip_w] = 255;
	}
}

void poly_draw(int type)	// 0=mask, 1=indexed, 3=RGB
{
	int i, i2, j, j2, j3, cuts, maxx = mem_width, maxy = mem_height;
	int poly_lines[MAX_POLY][2][2], poly_cuts[MAX_POLY];
	int oldmode = mem_undo_opacity;
	float ratio;

	if ( type==0 )
	{
		mem_clip_mask_init(0);		// Setup & Clear mask
		if ( mem_clip_mask == NULL ) return;	// Failed to get memory
	}

	mem_undo_opacity = TRUE;
	for ( i=0; i<poly_points; i++ )		// Populate poly_lines - smallest Y is first point
	{
		i2 = i+1;
		if ( i2 >= poly_points ) i2 = 0;	// Remember last point back to 1st point

		if ( poly_mem[i][1] < poly_mem[i2][1] )
			j = 0;
		else
			j = 1;

		poly_lines[i][j][0] = poly_mem[i][0];
		poly_lines[i][j][1] = poly_mem[i][1];
		poly_lines[i][1-j][0] = poly_mem[i2][0];
		poly_lines[i][1-j][1] = poly_mem[i2][1];

		if ( type>10 )
		{
			f_circle( poly_mem[i][0], poly_mem[i][1], tool_size );
			tline( poly_mem[i][0], poly_mem[i][1],
				poly_mem[i2][0], poly_mem[i2][1], tool_size );
		}
		if ( type==1 || type ==3 )
			sline( poly_mem[i][0], poly_mem[i][1], poly_mem[i2][0], poly_mem[i2][1] );
		if ( type==0 )
			memline( poly_mem[i][0] - poly_min_x, poly_mem[i][1] - poly_min_y,
				poly_mem[i2][0] - poly_min_x, poly_mem[i2][1] - poly_min_y );
			// Outline is needed to properly edge the polygon
	}

	if ( type>10 )		// If drawing outline only, finish now
	{
		mem_undo_opacity = oldmode;
		return;
	}

	if ( poly_min_y < 0 ) poly_min_y = 0;		// Vertical clipping
	if ( poly_max_y >= maxy ) poly_max_y = maxy-1;

	for ( j=poly_min_y; j<=poly_max_y; j++ )	// Scanline
	{
		cuts = 0;			
		for ( i=0; i<poly_points; i++ )		// Count up line intersections - X value cuts
		{
			if ( j >= poly_lines[i][0][1] && j <= poly_lines[i][1][1] )
			{
				if ( poly_lines[i][0][1] == poly_lines[i][1][1] )
				{	// Line is horizontal so use each end point as a cut
					poly_cuts[cuts++] = poly_lines[i][0][0];
					poly_cuts[cuts++] = poly_lines[i][1][0];
				}
				else	// Calculate cut X value - intersection on y=j
				{
					ratio = ( (float) j - poly_lines[i][0][1] ) /
						( poly_lines[i][1][1] - poly_lines[i][0][1] );
					poly_cuts[cuts++] = poly_lines[i][0][0] + mt_round(
						ratio * ( poly_lines[i][1][0] - poly_lines[i][0][0] ) );
					if ( j == poly_lines[i][0][1] )	cuts--;
							// Don't count start point
				}
			}
		}
		for ( i=cuts-1; i>0; i-- )	// Sort cuts table - the venerable bubble sort
		{
			for ( i2=0; i2<i; i2++ )
			{
				if ( poly_cuts[i2] > poly_cuts[i2+1] )
				{
					j2 = poly_cuts[i2];
					poly_cuts[i2] = poly_cuts[i2+1];
					poly_cuts[i2+1] = j2;
				}
			}
		}

			// Paint from first X to 2nd, gap from 2-3, paint 3-4, gap 4-5 ...

		for ( i=0; i<(cuts-1); i=i+2 )
		{
			if ( poly_cuts[i] < maxx && poly_cuts[i+1] >= 0 )
			{
				if ( poly_cuts[i] < 0 ) poly_cuts[i] = 0;
				if ( poly_cuts[i] >= maxx ) poly_cuts[i] = maxx-1;
				if ( poly_cuts[i+1] < 0 ) poly_cuts[i+1] = 0;	// Horizontal Clipping
				if ( poly_cuts[i+1] >= maxx ) poly_cuts[i+1] = maxx-1;

				if ( type == 0 )
				{
					j3 = (j-poly_min_y)*mem_clip_w;
					for ( i2=poly_cuts[i]-poly_min_x; i2<=poly_cuts[i+1]-poly_min_x; 	i2++ )
						mem_clip_mask[ i2 + j3  ] = 255;
				}
				else
				{
					for ( i2=poly_cuts[i]; i2<=poly_cuts[i+1]; i2++ )
						put_pixel( i2, j );
				}
			}
		}
	}
	mem_undo_opacity = oldmode;
}

void poly_mask()		// Paint polygon onto clipboard mask
{
	poly_draw(0);
}

void poly_paint()		// Paint polygon onto image - poly_init() must have been called
{
	poly_draw(MEM_BPP);
}

void poly_outline()		// Paint polygon outline onto image
{
	poly_draw(MEM_BPP + 10);
}

void poly_add(int x, int y)	// Add point to list
{
	if ( poly_points > 0 )		// Point must be different from previous one
	{
		if ( x == poly_mem[poly_points-1][0] && y == poly_mem[poly_points-1][1] ) return;
	}
	if ( poly_points < MAX_POLY )
	{
		poly_mem[poly_points][0] = x;
		poly_mem[poly_points][1] = y;
		poly_points++;
	}
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
	int i, j, x = poly_mem[0][0] - poly_min_x, y = poly_mem[0][1] - poly_min_y,
		minx = mem_clip_w-1, miny = mem_clip_h - 1, maxx = 0, maxy = 0,
		offs, offd, nw, nh;
	unsigned char *t_clip, *t_mask, *t_alpha = NULL;

	poly_mask();	// Initialize mask to all clear - 0 & Polygon on clipboard mask to 255
	if ( mem_clip_mask == NULL ) return;	// Failed to get memory

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

	for ( j=0; j<mem_clip_h; j++ )			// Find max & min values for shrink wrapping
	{
		offs = j*mem_clip_w;
		for ( i=0; i<mem_clip_w; i++ )
		{
			if (mem_clip_mask[offs + i])
			{
				mtMAX( maxx, maxx, i )
				mtMAX( maxy, maxy, j )
				mtMIN( minx, minx, i )
				mtMIN( miny, miny, j )
			}
		}
	}
	if ( minx>maxx ) return;	// No live pixels found so bail out

	nw = maxx - minx + 1;
	nh = maxy - miny + 1;

	/* No decrease so no resize either */
	if ((nw == mem_clip_w) && (nh == mem_clip_h)) return;

	/* Try to malloc memory for smaller clipboard */
	t_clip = malloc(nw * nh * mem_clip_bpp);
	t_mask = malloc(nw * nh);
	if (mem_clip_alpha) t_alpha = malloc(nw * nh);
	if (!t_clip || !t_mask || (!t_alpha && mem_clip_alpha))
	{
		free(t_clip); free(t_mask); free(t_alpha);
		memory_errors(1);
		return;
	}
	for (j = miny; j <= maxy; j++)	// Copy the data
	{
		offs = j * mem_clip_w + minx;
		offd = (j - miny) * nw;
		memcpy(t_mask + offd, mem_clip_mask + offs, nw);
		if (mem_clip_alpha)
			memcpy(t_alpha + offd, mem_clip_alpha + offs, nw);
		memcpy(t_clip + offd * mem_clip_bpp,
			mem_clipboard + offs * mem_clip_bpp, nw * mem_clip_bpp);
	}

	free(mem_clipboard);		// Free old clipboard
	free(mem_clip_mask);		// Free old mask
	free(mem_clip_alpha);		// Free old mask
	mem_clipboard = t_clip;
	mem_clip_mask = t_mask;
	mem_clip_alpha = t_alpha;
	mem_clip_w = nw;
	mem_clip_h = nh;
	mem_clip_x += minx;
	mem_clip_y += miny;
}

void poly_lasso_cut()		// Cut out area that was just lasso'd
{				// Pre-requisite - Must have clipboard mask from poly_lasso()
	int i, j, offm;

		// Check geometry within limits, i.e. (mem_clip_x+mem_clip_w)<=mem_width + y
	if ( 	(mem_clip_x + mem_clip_w) > mem_width ||
		(mem_clip_y + mem_clip_h) > mem_height) return;

	for ( j=mem_clip_y; j<(mem_clip_y + mem_clip_h); j++ )		// Cut out area
	{
		offm = (j - mem_clip_y) * mem_clip_w;
		for ( i=mem_clip_x; i<(mem_clip_x + mem_clip_w); i++ )
		{
			if (mem_clip_mask[offm++]) put_pixel(i, j);
		}
	}
}

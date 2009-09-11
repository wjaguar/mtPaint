/*	cpick.h
	Copyright (C) 2008 Mark Tyler

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

GtkWidget *cpick_create();

void cpick_get_colour( GtkWidget *w, int *r, int *g, int *b, int *opacity );

void cpick_set_colour( GtkWidget *w, int r, int g, int b, int opacity );
void cpick_set_colour_previous( GtkWidget *w, int r, int g, int b, int opacity );

void cpick_set_palette_visibility( GtkWidget *w, int visible );
void cpick_set_opacity_visibility( GtkWidget *w, int visible );


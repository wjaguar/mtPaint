/*	cpick.h
	Copyright (C) 2008-2014 Mark Tyler and Dmitry Groshev

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

#ifdef U_CPICK_MTPAINT		/* mtPaint dialog */
GtkWidget *eyedropper(void **r);
void set_hexentry(GtkWidget *entry, int c);
GtkWidget *hexentry(int c, void **r);
#endif

GtkWidget *cpick_create(int opacity);
void cpick_set_evt(GtkWidget *w, void **r);

int cpick_get_colour(GtkWidget *w, int *opacity);

void cpick_set_colour(GtkWidget *w, int rgb, int opacity);
void cpick_set_colour_previous(GtkWidget *w, int rgb, int opacity);

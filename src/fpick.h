/*	fpick.h
	Copyright (C) 2007 Mark Tyler and Dmitry Groshev

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

GtkWidget *fpick_create(char *title);
void fpick_destroy(GtkWidget *fp);
void fpick_setup(GtkWidget *fp, GtkWidget *xtra, GtkSignalFunc ok_fn,
	GtkSignalFunc cancel_fn, int dirs_only);
const char *fpick_get_filename(GtkWidget *fp, int raw);
void fpick_set_filename(GtkWidget *fp, char *name, int raw);

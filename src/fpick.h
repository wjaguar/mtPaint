/*	fpick.h
	Copyright (C) 2007-2014 Mark Tyler and Dmitry Groshev

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

#define FPICK_ENTRY	1
#define FPICK_LOAD	2
#define FPICK_DIRS_ONLY	4

GtkWidget *fpick(GtkWidget ***wpp, char *ddata, void **pp, void **r);
void fpick_get_filename(GtkWidget *fp, char *buf, int len, int raw);
void fpick_set_filename(GtkWidget *fp, char *name, int raw);

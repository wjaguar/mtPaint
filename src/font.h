/*	font.h
	Copyright (C) 2007-2016 Mark Tyler and Dmitry Groshev

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

#define TEXT_PASTE_NONE 0		// Not using text paste in the clipboard
#define TEXT_PASTE_GTK  1		// GTK+ text paste
#define TEXT_PASTE_FT   2		// FreeType text paste

#ifdef U_FREETYPE

int font_obl, font_bmsize, font_size;
int font_dirs;
int ft_setdpi;

void pressed_mt_text();
void ft_render_text();			// FreeType equivalent of render_text()

#else
#define pressed_mt_text()
#define ft_render_text()
#endif


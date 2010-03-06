/*	font.h
	Copyright (C) 2007-2008 Mark Tyler

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

#define MT_TEXT_MONO      1		// Force mono rendering
#define MT_TEXT_ROTATE_NN 2		// Use nearest neighbour rotation on bitmap fonts
#define MT_TEXT_OBLIQUE   4		// Apply Oblique matrix transformation to scalable fonts


unsigned char *mt_text_render(		// NULL return = failure, otherwise points to 8bpp memory chunk
		char	*text,		// Text to render
		int	characters,	// Characters to render (may be less if 0 is encountered first)
		char	*filename,	// Font file
		char	*encoding,	// Encoding of text, e.g. ASCII, UTF-8, UNICODE
		double	size,		// Scalable font size
		int	face_index,	// Usually 0, but maybe higher for bitmap fonts like *.FON
		double	angle,		// Rotation, anticlockwise
		int	flags,		// MT_TEXT_* flags
		int	*width,		// Resulting width of memory chunk allocated
		int	*height		// Resulting height of memory chunk allocated
		);

void pressed_mt_text();
void ft_render_text();			// FreeType equivalent of render_text()


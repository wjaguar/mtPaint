/*	icons.h
	Copyright (C) 2007-2010 Mark Tyler and Dmitry Groshev

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

#if GTK_MAJOR_VERSION == 1

#define XPM_ICON(X) xpm_##X##_xpm
#define DEF_XPM_ICON(X) extern char *xpm_##X##_xpm[];

#else /* if GTK_MAJOR_VERSION == 2 */

#define XPM_ICON(X) desc_##X##_xpm
#ifdef DEFINE_ICONS
#define DEF_XPM_ICON(X) xpm_icon_desc desc_##X##_xpm = { #X, xpm_##X##_xpm };
#else
#define DEF_XPM_ICON(X) extern xpm_icon_desc desc_##X##_xpm;
#endif

extern char *xpm_open_xpm[];
extern char *xpm_new_xpm[];

#endif

extern char *icon_xpm[];

DEF_XPM_ICON(brcosa);
DEF_XPM_ICON(case);
DEF_XPM_ICON(centre);
DEF_XPM_ICON(clone);
DEF_XPM_ICON(close);
DEF_XPM_ICON(copy);
DEF_XPM_ICON(cut);
DEF_XPM_ICON(down);
DEF_XPM_ICON(ellipse2);
DEF_XPM_ICON(ellipse);
DEF_XPM_ICON(flip_hs);
DEF_XPM_ICON(flip_vs);
DEF_XPM_ICON(flood);
DEF_XPM_ICON(grad_place);
DEF_XPM_ICON(hidden);
DEF_XPM_ICON(home);
DEF_XPM_ICON(lasso);
DEF_XPM_ICON(line);
DEF_XPM_ICON(mode_blend);
DEF_XPM_ICON(mode_cont);
DEF_XPM_ICON(mode_csel);
DEF_XPM_ICON(mode_mask);
DEF_XPM_ICON(mode_opac);
DEF_XPM_ICON(mode_tint2);
DEF_XPM_ICON(mode_tint);
DEF_XPM_ICON(new);
DEF_XPM_ICON(newdir);
DEF_XPM_ICON(open);
DEF_XPM_ICON(paint);
DEF_XPM_ICON(pan);
DEF_XPM_ICON(paste);
DEF_XPM_ICON(polygon);
DEF_XPM_ICON(rect1);
DEF_XPM_ICON(rect2);
DEF_XPM_ICON(redo);
DEF_XPM_ICON(rotate_as);
DEF_XPM_ICON(rotate_cs);
DEF_XPM_ICON(save);
DEF_XPM_ICON(select);
DEF_XPM_ICON(shuffle);
DEF_XPM_ICON(smudge);
DEF_XPM_ICON(text);
DEF_XPM_ICON(undo);
DEF_XPM_ICON(up);
DEF_XPM_ICON(cline);
DEF_XPM_ICON(layers);
//DEF_XPM_ICON(config);

extern unsigned char
	xbm_backslash_bits[],
	xbm_backslash_mask_bits[],
	xbm_circle_bits[],
	xbm_circle_mask_bits[],
	xbm_clone_bits[],
	xbm_clone_mask_bits[],
	xbm_flood_bits[],
	xbm_flood_mask_bits[],
	xbm_grad_bits[],
	xbm_grad_mask_bits[],
	xbm_horizontal_bits[],
	xbm_horizontal_mask_bits[],
	xbm_line_bits[],
	xbm_line_mask_bits[],
	xbm_picker_bits[],
	xbm_picker_mask_bits[],
	xbm_polygon_bits[],
	xbm_polygon_mask_bits[],
	xbm_ring4_bits[],
	xbm_ring4_mask_bits[],
	xbm_select_bits[],
	xbm_select_mask_bits[],
	xbm_shuffle_bits[],
	xbm_shuffle_mask_bits[],
	xbm_slash_bits[],
	xbm_slash_mask_bits[],
	xbm_smudge_bits[],
	xbm_smudge_mask_bits[],
	xbm_spray_bits[],
	xbm_spray_mask_bits[],
	xbm_square_bits[],
	xbm_square_mask_bits[],
	xbm_vertical_bits[],
	xbm_vertical_mask_bits[];

#define xbm_ring4_width 9
#define xbm_ring4_height 9
#define xbm_ring4_x_hot 4
#define xbm_ring4_y_hot 4

#define xbm_picker_width 17
#define xbm_picker_height 17
#define xbm_picker_x_hot 2
#define xbm_picker_y_hot 16

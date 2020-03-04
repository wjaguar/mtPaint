/*	icons.c
	Copyright (C) 2007-2010 Mark Tyler

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

#include "mygtk.h"

#define static

#include "graphics/icon.xpm"
#include "graphics/xbm_backslash.xbm"
#include "graphics/xbm_backslash_mask.xbm"
#include "graphics/xbm_circle.xbm"
#include "graphics/xbm_circle_mask.xbm"
#include "graphics/xbm_clone.xbm"
#include "graphics/xbm_clone_mask.xbm"
#include "graphics/xbm_flood.xbm"
#include "graphics/xbm_flood_mask.xbm"
#include "graphics/xbm_grad.xbm"
#include "graphics/xbm_grad_mask.xbm"
#include "graphics/xbm_horizontal.xbm"
#include "graphics/xbm_horizontal_mask.xbm"
#include "graphics/xbm_line.xbm"
#include "graphics/xbm_line_mask.xbm"
#include "graphics/xbm_picker.xbm"
#include "graphics/xbm_picker_mask.xbm"
#include "graphics/xbm_polygon.xbm"
#include "graphics/xbm_polygon_mask.xbm"
#include "graphics/xbm_ring4.xbm"
#include "graphics/xbm_ring4_mask.xbm"
#include "graphics/xbm_select.xbm"
#include "graphics/xbm_select_mask.xbm"
#include "graphics/xbm_shuffle.xbm"
#include "graphics/xbm_shuffle_mask.xbm"
#include "graphics/xbm_slash.xbm"
#include "graphics/xbm_slash_mask.xbm"
#include "graphics/xbm_smudge.xbm"
#include "graphics/xbm_smudge_mask.xbm"
#include "graphics/xbm_spray.xbm"
#include "graphics/xbm_spray_mask.xbm"
#include "graphics/xbm_square.xbm"
#include "graphics/xbm_square_mask.xbm"
#include "graphics/xbm_vertical.xbm"
#include "graphics/xbm_vertical_mask.xbm"
#include "graphics/xpm_brcosa.xpm"
#include "graphics/xpm_case.xpm"
#include "graphics/xpm_centre.xpm"
#include "graphics/xpm_clone.xpm"
#include "graphics/xpm_close.xpm"
#include "graphics/xpm_copy.xpm"
#include "graphics/xpm_cut.xpm"
#include "graphics/xpm_down.xpm"
#include "graphics/xpm_ellipse.xpm"
#include "graphics/xpm_ellipse2.xpm"
#include "graphics/xpm_flip_hs.xpm"
#include "graphics/xpm_flip_vs.xpm"
#include "graphics/xpm_flood.xpm"
#include "graphics/xpm_grad_place.xpm"
#include "graphics/xpm_hidden.xpm"
#include "graphics/xpm_home.xpm"
#include "graphics/xpm_lasso.xpm"
#include "graphics/xpm_line.xpm"
#include "graphics/xpm_mode_blend.xpm"
#include "graphics/xpm_mode_cont.xpm"
#include "graphics/xpm_mode_csel.xpm"
#include "graphics/xpm_mode_mask.xpm"
#include "graphics/xpm_mode_opac.xpm"
#include "graphics/xpm_mode_tint.xpm"
#include "graphics/xpm_mode_tint2.xpm"
#include "graphics/xpm_new.xpm"
#include "graphics/xpm_newdir.xpm"
#include "graphics/xpm_open.xpm"
#include "graphics/xpm_paint.xpm"
#include "graphics/xpm_pan.xpm"
#include "graphics/xpm_paste.xpm"
#include "graphics/xpm_polygon.xpm"
#include "graphics/xpm_rect1.xpm"
#include "graphics/xpm_rect2.xpm"
#include "graphics/xpm_redo.xpm"
#include "graphics/xpm_rotate_as.xpm"
#include "graphics/xpm_rotate_cs.xpm"
#include "graphics/xpm_save.xpm"
#include "graphics/xpm_select.xpm"
#include "graphics/xpm_shuffle.xpm"
#include "graphics/xpm_smudge.xpm"
#include "graphics/xpm_text.xpm"
#include "graphics/xpm_undo.xpm"
#include "graphics/xpm_up.xpm"
#include "graphics/xpm_cline.xpm"
#include "graphics/xpm_layers.xpm"
#include "graphics/xpm_picker.xpm"
//#include "graphics/xpm_config.xpm"

#undef static

#if GTK_MAJOR_VERSION >= 2
/* Create icon descriptors */
#define DEFINE_ICONS
#include "icons.h"
#endif

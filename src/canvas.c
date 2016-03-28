/*	canvas.c
	Copyright (C) 2004-2016 Mark Tyler and Dmitry Groshev

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

#include "global.h"
#undef _
#define _(X) X

#include "mygtk.h"
#include "memory.h"
#include "vcode.h"
#include "ani.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "inifile.h"
#include "canvas.h"
#include "viewer.h"
#include "layer.h"
#include "polygon.h"
#include "wu.h"
#include "prefs.h"
#include "spawn.h"
#include "channels.h"
#include "toolbar.h"
#include "font.h"

float can_zoom = 1;				// Zoom factor 1..MAX_ZOOM
int margin_main_xy[2];				// Top left of image from top left of canvas
int margin_view_xy[2];
int marq_status = MARQUEE_NONE, marq_xy[4] = { -1, -1, -1, -1 };	// Selection marquee
int marq_drag_x, marq_drag_y;						// Marquee dragging offset
int line_status = LINE_NONE, line_xy[4];				// Line tool
int poly_status = POLY_NONE;						// Polygon selection tool
int clone_status, clone_x, clone_y, clone_dx, clone_dy;			// Clone tool state
int clone_mode = TRUE, clone_x0 = -1, clone_y0 = -1,			// Clone settings
	clone_dx0, clone_dy0;

int recent_files;					// Current recent files setting

int	show_paste,					// Show contents of clipboard while pasting
	col_reverse,					// Painting with right button
	text_paste,					// Are we pasting text?
	canvas_image_centre,				// Are we centering the image?
	chequers_optimize,				// Are we optimizing the chequers for speed?
	cursor_zoom					// Are we zooming at cursor position?
	;

int brush_spacing;	// Step in non-continuous mode; 0 means use event coords
int lasso_sel;		// Lasso by selection channel (just trim it) if present

int preserved_gif_delay = 10, undo_load;

static int update_later;


///	STATUS BAR

void **label_bar[STATUS_ITEMS];

static void update_image_bar()
{
	char txt[128], txt2[16], *tmp = cspnames[CSPACE_RGB];


	if (!status_on[STATUS_GEOMETRY]) return;

	if (mem_img_bpp == 1) sprintf(tmp = txt2, "%i", mem_cols);

	tmp = txt + snprintf(txt, 80, "%s %i x %i x %s",
		channames[mem_channel], mem_width, mem_height, tmp);

	if ( mem_img[CHN_ALPHA] || mem_img[CHN_SEL] || mem_img[CHN_MASK] )
	{
		strcpy(tmp, " + "); tmp += 3;
		if (mem_img[CHN_ALPHA]) *tmp++ = 'A';
		if (mem_img[CHN_SEL])   *tmp++ = 'S';
		if (mem_img[CHN_MASK])  *tmp++ = 'M';
	// !!! String not NUL-terminated at this point
	}

	if ( layers_total>0 )
		tmp += sprintf(tmp, "  (%i/%i)", layer_selected, layers_total);
	if ( mem_xpm_trans>=0 )
		tmp += sprintf(tmp, "  (T=%i)", mem_xpm_trans);
	strcpy(tmp, "  ");
	cmd_setv(label_bar[STATUS_GEOMETRY], txt, LABEL_VALUE);
}

void update_sel_bar(int now)		// Update selection stats on status bar
{
	char txt[64] = "";
	int rect[4];
	float lang, llen;
	grad_info *grad = gradient + mem_channel;


	if (!status_on[STATUS_SELEGEOM]) return;
	update_later |= CF_SELBAR;
	if (script_cmds && !now) return;
	update_later &= ~CF_SELBAR;

	if ((((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON)) &&
		(marq_status > MARQUEE_NONE)) ||
		((tool_type == TOOL_GRADIENT) && (grad->status != GRAD_NONE)) ||
		((tool_type == TOOL_LINE) && (line_status != LINE_NONE)))
	{
		if (tool_type == TOOL_GRADIENT)
		{
			copy4(rect, grad->xy);
			rect[2] -= rect[0];
			rect[3] -= rect[1];
			lang = (180.0 / M_PI) * atan2(rect[2], -rect[3]);
		}
		else if (tool_type == TOOL_LINE)
		{
			copy4(rect, line_xy);
			rect[2] -= rect[0];
			rect[3] -= rect[1];
			lang = (180.0 / M_PI) * atan2(rect[2], -rect[3]);
		}
		else
		{
			marquee_at(rect);
			lang = (180.0 / M_PI) * atan2(marq_x2 - marq_x1,
				marq_y1 - marq_y2);
		}
		llen = sqrt(rect[2] * rect[2] + rect[3] * rect[3]);
		snprintf(txt, 60, "  %i,%i : %i x %i   %.1f' %.1f\"",
			rect[0], rect[1], rect[2], rect[3], lang, llen);
	}

	else if (tool_type == TOOL_POLYGON)
	{
		snprintf(txt, 60, "  (%i)%c", poly_points,
			poly_status != POLY_DONE ? '+' : '\0');
	}

	cmd_setv(label_bar[STATUS_SELEGEOM], txt, LABEL_VALUE);
}

static char *chan_txt_cat(char *txt, int chan, int x, int y)
{
	if (!mem_img[chan]) return (txt);
	return (txt + sprintf(txt, "%i", mem_img[chan][x + mem_width*y]));
}

void update_xy_bar(int x, int y)
{
	char txt[96], *tmp = txt;
	int pixel;

	if (status_on[STATUS_CURSORXY])
	{
		snprintf(txt, 60, "%i,%i", x, y);
		cmd_setv(label_bar[STATUS_CURSORXY], txt, LABEL_VALUE);
	}

	if (!status_on[STATUS_PIXELRGB]) return;
	*tmp = '\0';
	if ((x >= 0) && (x < mem_width) && (y >= 0) && (y < mem_height))
	{
		pixel = get_pixel_img(x, y);
		if (mem_img_bpp == 1)
			tmp += sprintf(tmp, "[%u] = {%i,%i,%i}", pixel,
				mem_pal[pixel].red, mem_pal[pixel].green,
				mem_pal[pixel].blue);
		else
			tmp += sprintf(tmp, "{%i,%i,%i}", INT_2_R(pixel),
				INT_2_G(pixel), INT_2_B(pixel));
		if (mem_img[CHN_ALPHA] || mem_img[CHN_SEL] || mem_img[CHN_MASK])
		{
			strcpy(tmp, " + {"); tmp += 4;
			tmp = chan_txt_cat(tmp, CHN_ALPHA, x, y);
			*tmp++ = ',';
			tmp = chan_txt_cat(tmp, CHN_SEL, x, y);
			*tmp++ = ',';
			tmp = chan_txt_cat(tmp, CHN_MASK, x, y);
			strcpy(tmp, "}");
		}
	}
	cmd_setv(label_bar[STATUS_PIXELRGB], txt, LABEL_VALUE);
}

static void update_undo_bar()
{
	char txt[32];

	if (status_on[STATUS_UNDOREDO])
	{
		sprintf(txt, "%i+%i", mem_undo_done, mem_undo_redo);
		cmd_setv(label_bar[STATUS_UNDOREDO], txt, LABEL_VALUE);
	}
}

void init_status_bar()
{
	int i;

	for (i = 0; i < STATUS_ITEMS; i++)
		cmd_showhide(label_bar[i], status_on[i]);
	update_image_bar();
	update_undo_bar();
}


void commit_paste(int swap, int *update)
{
	image_info ti;
	unsigned char *image, *xbuf, *mask, *alpha = NULL;
	unsigned char *old_image, *old_alpha;
	int op = 255, opacity = tool_opacity, bpp = MEM_BPP;
	int fx, fy, fw, fh, fx2, fy2;		// Screen coords
	int i, ua, cmask, ofs, iofs, upd = UPD_IMGP, fail = TRUE;


	fx = marq_x1 > 0 ? marq_x1 : 0;
	fy = marq_y1 > 0 ? marq_y1 : 0;
	fx2 = marq_x2 < mem_width ? marq_x2 : mem_width - 1;
	fy2 = marq_y2 < mem_height ? marq_y2 : mem_height - 1;

	fw = fx2 - fx + 1;
	fh = fy2 - fy + 1;

	mask = multialloc(MA_SKIP_ZEROSIZE, &mask, fw, &alpha,
		((mem_channel == CHN_IMAGE) && RGBA_mode && mem_img[CHN_ALPHA] &&
		!mem_clip_alpha && !channel_dis[CHN_ALPHA]) * fw,
		&xbuf, NEED_XBUF_PASTE * fw * bpp, NULL);
	if (!mask) goto quit; // Not enough memory
	if (alpha) memset(alpha, channel_col_A[CHN_ALPHA], fw);

	/* Ignore clipboard alpha if disabled */
	ua = channel_dis[CHN_ALPHA] | !mem_clip_alpha;

	if (swap) /* Prepare to convert image contents into new clipboard */
	{
		cmask = CMASK_IMAGE | CMASK_SEL;
		if ((mem_channel == CHN_IMAGE) && mem_img[CHN_ALPHA] &&
			 !channel_dis[CHN_ALPHA]) cmask |= CMASK_ALPHA;
		if (!mem_alloc_image(AI_CLEAR | AI_NOINIT, &ti, fw, fh, MEM_BPP,
			cmask, NULL)) goto quit;
		copy_area(&ti, &mem_image, fx, fy);
	}

	/* Offset in memory */
	ofs = (fy - marq_y1) * mem_clip_w + (fx - marq_x1);
	image = mem_clipboard + ofs * mem_clip_bpp;
	iofs = fy * mem_width + fx;

	mem_undo_next(UNDO_PASTE);	// Do memory stuff for undo

	old_image = mem_img[mem_channel];
	old_alpha = mem_img[CHN_ALPHA];
	if (mem_undo_opacity)
	{
		old_image = mem_undo_previous(mem_channel);
		old_alpha = mem_undo_previous(CHN_ALPHA);
	}
	if (IS_INDEXED) op = opacity = 0;

	for (i = 0; i < fh; i++)
	{
		unsigned char *wa = ua ? alpha : mem_clip_alpha + ofs;
		unsigned char *wm = mem_clip_mask ? mem_clip_mask + ofs : NULL;
		unsigned char *img = image;

		row_protected(fx, fy + i, fw, mask);
		if (swap)
		{
			unsigned char *ws = ti.img[CHN_SEL] + i * fw;

			memcpy(ws, mask, fw);
			process_mask(0, 1, fw, ws, NULL, NULL,
				ti.img[CHN_ALPHA] ? NULL : wa, wm, op, 0);
		}

		process_mask(0, 1, fw, mask, mem_img[CHN_ALPHA] && wa ?
			mem_img[CHN_ALPHA] + iofs : NULL, old_alpha + iofs,
			wa, wm, opacity, 0);

		if (mem_clip_bpp < bpp)
		{
			/* Convert paletted clipboard to RGB */
			do_convert_rgb(0, 1, fw, xbuf, img,
				mem_clip_paletted ? mem_clip_pal : mem_pal);
			img = xbuf;
		}

		process_img(0, 1, fw, mask, mem_img[mem_channel] + iofs * bpp,
			old_image + iofs * bpp, img,
			xbuf, bpp, opacity);

		image += mem_clip_w * mem_clip_bpp;
		ofs += mem_clip_w;
		iofs += mem_width;
	}

	if (swap)
	{
		if ((fw - mem_clip_w) | (fh - mem_clip_h))
			upd |= UPD_CGEOM & ~UPD_IMGMASK;
		/* Remove new mask if it's all 255 */
		if (is_filled(ti.img[CHN_SEL], 255, fw * fh))
		{
			free(ti.img[CHN_SEL]);
			ti.img[CHN_SEL] = NULL;
		}
		mem_clip_new(fw, fh, MEM_BPP, 0, NULL);
		memcpy(mem_clip.img, ti.img, sizeof(chanlist));
		// !!! marq_x2, marq_y2 will be set by update_stuff()
		mem_clip_x = marq_x1 = fx;
		mem_clip_y = marq_y1 = fy;
	}

	fail = FALSE;
quit:	free(mask);

	if (fail) memory_errors(1); /* Warn and not update */
	else if (!update) /* Update right now */
	{
		update_stuff(upd);
		vw_update_area(fx, fy, fw, fh);
		main_update_area(fx, fy, fw, fh);
	}
	else /* Accumulate update area for later */
	{
	/* !!! Swap does not use this branch, and isn't supported here */
		fw += fx; fh += fy;
		if (fx < update[0]) update[0] = fx;
		if (fy < update[1]) update[1] = fy;
		if (fw > update[2]) update[2] = fw;
		if (fh > update[3]) update[3] = fh;
	}
}

void iso_trans(int mode)
{
	int i = mem_isometrics(mode);

	if (!i) update_stuff(UPD_GEOM);
	else if (i == -5) alert_box(_("Error"),
		_("The image is too large to transform."), NULL);
	else memory_errors(i);
}

void pressed_invert()
{
	spot_undo(UNDO_INV);

	mem_invert();
	mem_undo_prepare();

	update_stuff(UPD_COL);
}

static int edge_mode;

static int do_edge(filterwindow_dd *dt, void **wdata)
{
	static const unsigned char fxmap[] = { FX_EDGE, FX_SOBEL, FX_PREWITT,
		FX_KIRSCH, FX_GRADIENT, FX_ROBERTS, FX_LAPLACE, FX_MORPHEDGE };

	run_query(wdata);
	spot_undo(UNDO_FILT);
	do_effect(fxmap[edge_mode], 0);
	mem_undo_prepare();

	return TRUE;
}

static char *fnames_txt[] = { _("MT"), _("Sobel"), _("Prewitt"), _("Kirsch"),
	_("Gradient"), _("Roberts"), _("Laplace"), _("Morphological"), NULL };

#define WBbase filterwindow_dd
static void *edge_code[] = {
	BORDER(RPACK, 0),
	RPACKv(fnames_txt, 0, 4, edge_mode), RET
};
#undef WBbase

void pressed_edge_detect()
{
	static filterwindow_dd tdata = {
		_("Edge Detect"), edge_code, FW_FN(do_edge) };
	run_create_(filterwindow_code, &tdata, sizeof(tdata), script_cmds);
}

typedef struct {
	spin1_dd s1;
	int fx;
} spin1f_dd;

static int do_fx(spin1f_dd *dt, void **wdata)
{
	run_query(wdata);
	spot_undo(UNDO_FILT);
	do_effect(dt->fx, dt->s1.n[0]);
	mem_undo_prepare();

	return TRUE;
}

void pressed_sharpen()
{
	static spin1f_dd tdata = { {
		{ _("Edge Sharpen"), spin1_code, FW_FN(do_fx) },
		{ 50, 1, 100 } }, FX_SHARPEN };
	run_create_(filterwindow_code, &tdata, sizeof(tdata), script_cmds);
}

void pressed_soften()
{
	static spin1f_dd tdata = { {
		{ _("Edge Soften"), spin1_code, FW_FN(do_fx) },
		{ 50, 1, 100 } }, FX_SOFTEN };
	run_create_(filterwindow_code, &tdata, sizeof(tdata), script_cmds);
}

void pressed_fx(int what)
{
	spot_undo(UNDO_FILT);
	do_effect(what, 0);
	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

typedef struct {
	filterwindow_dd fw;
	int rgb;
	int x, y, xy, gamma;
	void **yspin;
} gauss_dd;

static int do_gauss(gauss_dd *dt, void **wdata)
{
	int radiusX, radiusY, gcor = FALSE;

	run_query(wdata);
	if (mem_channel == CHN_IMAGE) gcor = dt->gamma;

	radiusX = radiusY = dt->x;
	if (dt->xy) radiusY = dt->y;

	spot_undo(UNDO_DRAW);
	mem_gauss(radiusX * 0.01, radiusY * 0.01, gcor);
	mem_undo_prepare();

	return TRUE;
}

static void gauss_xy_click(gauss_dd *dt, void **wdata, int what, void **where)
{
	cmd_read(where, dt);
	cmd_sensitive(dt->yspin, dt->xy);
}

#define WBbase gauss_dd
static void *gauss_code[] = {
	VBOXPS,
	BORDER(SPIN, 0),
	FSPIN(x, 0, 20000), ALTNAME("X"),
	REF(yspin), FSPIN(y, 0, 20000), INSENS, OPNAME("Y"),
	CHECK(_("Different X/Y"), xy), EVENT(CHANGE, gauss_xy_click),
	IF(rgb), CHECK(_("Gamma corrected"), gamma),
	WDONE, RET
};
#undef WBbase

void pressed_gauss()
{
	gauss_dd tdata = {
		{ _("Gaussian Blur"), gauss_code, FW_FN(do_gauss) },
		mem_channel == CHN_IMAGE, 100, 100, FALSE, use_gamma };
	run_create_(filterwindow_code, &tdata, sizeof(tdata), script_cmds);
}

typedef struct {
	filterwindow_dd fw;
	int rgb;
	int radius, amount, threshold, gamma;
} unsharp_dd;

static int do_unsharp(unsharp_dd *dt, void **wdata)
{
	run_query(wdata);
	// !!! No RGBA mode for now, so UNDO_DRAW isn't needed
	spot_undo(UNDO_FILT);
	mem_unsharp(dt->radius * 0.01, dt->amount * 0.01, dt->threshold,
		(mem_channel == CHN_IMAGE) && dt->gamma);
	mem_undo_prepare();

	return TRUE;
}

#define WBbase unsharp_dd
static void *unsharp_code[] = {
	VBOXPS,
	BORDER(TABLE, 0),
	TABLE2(3), OPNAME0,
	TFSPIN(_("Radius"), radius, 0, 20000),
	TFSPIN(_("Amount"), amount, 0, 1000),
	TSPIN(_("Threshold "), threshold, 0, 255),
	WDONE,
	IF(rgb), CHECK(_("Gamma corrected"), gamma),
	WDONE, RET
};
#undef WBbase

void pressed_unsharp()
{
	unsharp_dd tdata = {
		{ _("Unsharp Mask"), unsharp_code, FW_FN(do_unsharp) },
		mem_channel == CHN_IMAGE, 500, 50, 0, use_gamma };
	run_create_(filterwindow_code, &tdata, sizeof(tdata), script_cmds);
}

typedef struct {
	filterwindow_dd fw;
	int rgb;
	int outer, inner, norm, gamma;
} dog_dd;

static int do_dog(dog_dd *dt, void **wdata)
{
	run_query(wdata);
	if (dt->outer <= dt->inner) return (FALSE); /* Invalid parameters */

	spot_undo(UNDO_FILT);
	mem_dog(dt->outer * 0.01, dt->inner * 0.01, dt->norm,
		(mem_channel == CHN_IMAGE) && dt->gamma);
	mem_undo_prepare();

	return TRUE;
}

#define WBbase dog_dd
static void *dog_code[] = {
	VBOXPS,
	BORDER(TABLE, 0),
	TABLE2(2), OPNAME0,
	TFSPIN(_("Outer radius"), outer, 0, 20000),
	TFSPIN(_("Inner radius"), inner, 0, 20000),
	WDONE,
	CHECK(_("Normalize"), norm),
	IF(rgb), CHECK(_("Gamma corrected"), gamma),
	WDONE, RET
};
#undef WBbase

void pressed_dog()
{
	dog_dd tdata = {
		{ _("Difference of Gaussians"), dog_code, FW_FN(do_dog) },
		mem_channel == CHN_IMAGE, 300, 100, TRUE, use_gamma };
	run_create_(filterwindow_code, &tdata, sizeof(tdata), script_cmds);
}

typedef struct {
	filterwindow_dd fw;
	int r, detail, gamma;
} kuw_dd;

static int do_kuwahara(kuw_dd *dt, void **wdata)
{
	run_query(wdata);
	spot_undo(UNDO_COL); // Always processes RGB image channel
	mem_kuwahara(dt->r, dt->gamma, dt->detail);
	mem_undo_prepare();

	return (TRUE);
}

#define WBbase kuw_dd
static void *kuw_code[] = {
	VBOXPS,
	BORDER(SPIN, 0),
	SPIN(r, 1, 127),
	CHECK(_("Protect details"), detail),
	CHECK(_("Gamma corrected"), gamma),
	WDONE, RET
};
#undef WBbase

void pressed_kuwahara()
{
	kuw_dd tdata = {
		{ _("Kuwahara-Nagao Blur"), kuw_code, FW_FN(do_kuwahara) },
		1, FALSE, use_gamma };
	run_create_(filterwindow_code, &tdata, sizeof(tdata), script_cmds);
}

void pressed_convert_rgb()
{
	unsigned char *old_img = mem_img[CHN_IMAGE];
	int res = undo_next_core(UC_NOCOPY, mem_width, mem_height, 3, CMASK_IMAGE);
	if (res) memory_errors(res);
	else
	{
		do_convert_rgb(0, 1, mem_width * mem_height, mem_img[CHN_IMAGE],
			old_img, mem_pal);
		update_stuff(UPD_2RGB);
	}
}

void pressed_greyscale(int mode)
{
	spot_undo(UNDO_COL);

	mem_greyscale(mode);
	mem_undo_prepare();

	update_stuff(UPD_COL);
}

void pressed_rotate_image(int dir)
{
	int i = mem_image_rot(dir);
	if (i) memory_errors(i);
	else update_stuff(UPD_GEOM);
}

void pressed_rotate_sel(int dir)
{
	if (mem_sel_rot(dir)) memory_errors(1);
	else update_stuff(UPD_CGEOM);
}

static int angle = 4500;

typedef struct {
	filterwindow_dd fw;
	int rgb;
	int smooth, gamma;
} rfree_dd;

static int do_rotate_free(rfree_dd *dt, void **wdata)
{
	int j, smooth = 0, gcor = 0;

	run_query(wdata);
	if (mem_img_bpp == 3)
	{
		gcor = dt->gamma;
		smooth = dt->smooth;
	}
	j = mem_rotate_free(angle * 0.01, smooth, gcor, FALSE);
	if (!j) update_stuff(UPD_GEOM);
	else
	{
		if (j == -5) alert_box(_("Error"),
			_("The image is too large for this rotation."), NULL);
		else memory_errors(j);
	}

	return TRUE;
}

#define WBbase rfree_dd
static void *rfree_code[] = {
	VBOXPS,
	BORDER(SPIN, 0),
	FSPINv(angle, -36000, 36000),
	IFx(rgb, 1),
		CHECK(_("Gamma corrected"), gamma),
		CHECK(_("Smooth"), smooth),
	ENDIF(1),
	WDONE, RET
};
#undef WBbase

void pressed_rotate_free()
{
	rfree_dd tdata = {
		{ _("Free Rotate"), rfree_code, FW_FN(do_rotate_free) },
		mem_img_bpp == 3, TRUE, use_gamma };
	run_create_(filterwindow_code, &tdata, sizeof(tdata), script_cmds);
}


void pressed_clip_mask(int val)
{
	int i;

	if ( mem_clip_mask == NULL )
	{
		i = mem_clip_mask_init(val ^ 255);
		if (i)
		{
			memory_errors(1);	// Not enough memory
			return;
		}
	}
	mem_clip_mask_set(val);
	update_stuff(UPD_CLIP);
}

static int do_clip_alphamask()
{
	unsigned char *old_mask = mem_clip_mask;
	int i, j = mem_clip_w * mem_clip_h, k;

	if (!mem_clipboard || !mem_clip_alpha) return FALSE;

	mem_clip_mask = mem_clip_alpha;
	mem_clip_alpha = NULL;

	if (old_mask)
	{
		for (i = 0; i < j; i++)
		{
			k = old_mask[i] * mem_clip_mask[i];
			mem_clip_mask[i] = (k + (k >> 8) + 1) >> 8;
		}
		free(old_mask);
	}

	return TRUE;
}

void pressed_clip_alphamask()
{
	if (do_clip_alphamask()) update_stuff(UPD_CLIP);
}

void pressed_clip_alpha_scale()
{
	if (!mem_clipboard || (mem_clip_bpp != 3)) return;
	if (!mem_clip_mask) mem_clip_mask_init(255);
	if (!mem_clip_mask) return;

	if (mem_scale_alpha(mem_clipboard, mem_clip_mask,
		mem_clip_w, mem_clip_h, TRUE)) return;

	update_stuff(UPD_CLIP);
}

void pressed_clip_mask_all()
{
	if (mem_clip_mask_init(0))
		memory_errors(1);	// Not enough memory
	else update_stuff(UPD_CLIP);
}

void pressed_clip_mask_clear()
{
	if (!mem_clip_mask) return;
	mem_clip_mask_clear();
	update_stuff(UPD_CLIP);
}

void pressed_flip_image_v()
{
	int i;
	unsigned char *temp;

	temp = malloc(mem_width * mem_img_bpp);
	if (!temp) return; /* Not enough memory for temp buffer */
	spot_undo(UNDO_XFORM);
	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!mem_img[i]) continue;
		mem_flip_v(mem_img[i], temp, mem_width, mem_height, BPP(i));
	}
	free(temp);
	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

void pressed_flip_image_h()
{
	int i;

	spot_undo(UNDO_XFORM);
	for (i = 0; i < NUM_CHANNELS; i++)
	{
		if (!mem_img[i]) continue;
		mem_flip_h(mem_img[i], mem_width, mem_height, BPP(i));
	}
	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

void pressed_flip_sel_v()
{
	unsigned char *temp;
	int i, bpp = mem_clip_bpp;

	temp = malloc(mem_clip_w * mem_clip_bpp);
	if (!temp) return; /* Not enough memory for temp buffer */
	for (i = 0; i < NUM_CHANNELS; i++ , bpp = 1)
	{
		if (!mem_clip.img[i]) continue;
		mem_flip_v(mem_clip.img[i], temp, mem_clip_w, mem_clip_h, bpp);
	}
	update_stuff(UPD_CLIP);
}

void pressed_flip_sel_h()
{
	int i, bpp = mem_clip_bpp;
	for (i = 0; i < NUM_CHANNELS; i++ , bpp = 1)
	{
		if (!mem_clip.img[i]) continue;
		mem_flip_h(mem_clip.img[i], mem_clip_w, mem_clip_h, bpp);
	}
	update_stuff(UPD_CLIP);
}

static void locate_marquee(int *xy, int snap);

#define MIN_VISIBLE 16 /* No less than a square this large must be visible */

void pressed_paste(int centre)
{
	if (!mem_clipboard) return;

	pressed_select(FALSE);
	change_to_tool(TTB_SELECT);

	marq_status = MARQUEE_PASTE;
	cursor_corner = -1;
	marq_x1 = mem_clip_x;
	marq_y1 = mem_clip_y;
	while (centre)
	{
		int marq0[4], mxy[4], vxy[4];

		canvas_center(mem_ic);
		marq_x1 = mem_width * mem_icx - mem_clip_w * 0.5;
		marq_y1 = mem_height * mem_icy - mem_clip_h * 0.5;
		if (!tgrid_snap) break;
		/* Snap to grid */
		copy4(marq0, marq_xy);
		locate_marquee(mxy, TRUE);
		if (script_cmds) break; // Scripting must behave consistently
		/* Undo snap if not enough of paste area is left visible */
		// !!! Could use CSCROLL_XYSIZE here instead
		cmd_peekv(drawing_canvas, vxy, sizeof(vxy), CANVAS_VPORT);
		if (!clip(vxy, vxy[0] - margin_main_x, vxy[1] - margin_main_y,
			vxy[2] - margin_main_x, vxy[3] - margin_main_y, mxy) ||
			((vxy[2] - vxy[0] < MIN_VISIBLE) &&
			 (vxy[2] - vxy[0] < mxy[2] - mxy[0])) ||
			((vxy[3] - vxy[1] < MIN_VISIBLE) &&
			 (vxy[3] - vxy[1] < mxy[3] - mxy[1])))
			copy4(marq_xy, marq0);
		break;
	}
	// !!! marq_x2, marq_y2 will be set by update_stuff()
	update_stuff(UPD_PASTE);
}

#undef MIN_VISIBLE

void pressed_rectangle(int filled)
{
	int sb;

	spot_undo(UNDO_DRAW);

	/* Shapeburst mode */
	sb = STROKE_GRADIENT;

	if ( tool_type == TOOL_POLYGON )
	{
		if (sb)
		{
			int l2, l3, ixy[4] = { 0, 0, mem_width, mem_height };

			l2 = l3 = filled ? 1 : tool_size;
			l2 >>= 1; l3 -= l2;
			clip(sb_rect, poly_min_x - l2, poly_min_y - l2,
				poly_max_x + l3, poly_max_y + l3, ixy);
			sb_rect[2] -= sb_rect[0];
			sb_rect[3] -= sb_rect[1];
			sb = init_sb();
		}
		if (!filled) poly_outline();
		else poly_paint();
	}
	else
	{
		int l2 = 2 * tool_size, rect[4];

		marquee_at(rect);
		if (sb)
		{
			copy4(sb_rect, rect);
			sb = init_sb();
		}

		if (filled || (l2 >= rect[2]) || (l2 >= rect[3]))
			f_rectangle(rect[0], rect[1], rect[2], rect[3]);
		else
		{
			f_rectangle(rect[0], rect[1],
				rect[2], tool_size);
			f_rectangle(rect[0], rect[1] + rect[3] - tool_size,
				rect[2], tool_size);
			f_rectangle(rect[0], rect[1] + tool_size,
				tool_size, rect[3] - l2);
			f_rectangle(rect[0] + rect[2] - tool_size, rect[1] + tool_size,
				tool_size, rect[3] - l2);
		}
	}

	if (sb) render_sb(NULL);

	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

void pressed_ellipse(int filled)
{
	spot_undo(UNDO_DRAW);
	mem_ellipse(marq_x1, marq_y1, marq_x2, marq_y2, filled ? 0 : tool_size);
	mem_undo_prepare();
	update_stuff(UPD_IMG);
}

static int copy_clip()
{
	int rect[4], bpp = MEM_BPP, cmask = CMASK_IMAGE;


	if ((mem_channel == CHN_IMAGE) && mem_img[CHN_ALPHA] &&
		 !channel_dis[CHN_ALPHA]) cmask = CMASK_RGBA;
	marquee_at(rect);
	mem_clip_new(rect[2], rect[3], bpp, cmask, NULL);

	if (!mem_clipboard)
	{
		alert_box(_("Error"), _("Not enough memory to create clipboard"), NULL);
		return (FALSE);
	}

	mem_clip_x = rect[0];
	mem_clip_y = rect[1];

	copy_area(&mem_clip, &mem_image, mem_clip_x, mem_clip_y);

	return (TRUE);
}

static int channel_mask()
{
	int i, j, ofs, delta;

	if (!mem_img[CHN_SEL] || channel_dis[CHN_SEL]) return (FALSE);
	if (mem_channel > CHN_ALPHA) return (FALSE);

	if (!mem_clip_mask) mem_clip_mask_init(255);
	if (!mem_clip_mask) return (FALSE);

	ofs = mem_clip_y * mem_width + mem_clip_x;
	delta = 0;
	for (i = 0; i < mem_clip_h; i++)
	{
		for (j = 0; j < mem_clip_w; j++)
			mem_clip_mask[delta + j] &= mem_img[CHN_SEL][ofs + j];
		ofs += mem_width;
		delta += mem_clip_w;
	}
	return (TRUE);
}

static void cut_clip()
{
	int i, sb = 0;

	spot_undo(UNDO_DRAW);
	/* Shapeburst mode */
	if (STROKE_GRADIENT)
	{
		sb_rect[0] = mem_clip_x;
		sb_rect[1] = mem_clip_y;
		sb_rect[2] = mem_clip_w;
		sb_rect[3] = mem_clip_h;
		sb = init_sb();
	}
	for (i = 0; i < mem_clip_h; i++)
	{
		put_pixel_row(mem_clip_x, mem_clip_y + i, mem_clip_w,
			mem_clip_mask ? mem_clip_mask + i * mem_clip_w : NULL);
	}
	if (sb) render_sb(mem_clip_mask);
	mem_undo_prepare();
}

static void trim_clip()
{
	chanlist old_img;
	unsigned char *tmp;
	int i, j, k, offs, offd, maxx, maxy, minx, miny, nw, nh;

	minx = MAX_WIDTH; miny = MAX_HEIGHT; maxx = maxy = 0;

	/* Find max & min values for shrink wrapping */
	for (j = 0; j < mem_clip_h; j++)
	{
		offs = mem_clip_w * j;
		for (i = 0; i < mem_clip_w; i++)
		{
			if (!mem_clip_mask[offs + i]) continue;
			if (i < minx) minx = i;
			if (i > maxx) maxx = i;
			if (j < miny) miny = j;
			if (j > maxy) maxy = j;
		}
	}

	/* No live pixels found */
	if (minx > maxx) return;

	nw = maxx - minx + 1;
	nh = maxy - miny + 1;

	/* No decrease so no resize either */
	if ((nw == mem_clip_w) && (nh == mem_clip_h)) return;

	/* Pack data to front */
	for (j = miny; j <= maxy; j++)
	{
		offs = j * mem_clip_w + minx;
		offd = (j - miny) * nw;
		memmove(mem_clipboard + offd * mem_clip_bpp,
			mem_clipboard + offs * mem_clip_bpp, nw * mem_clip_bpp);
		for (k = 1; k < NUM_CHANNELS; k++)
		{
			if (!(tmp = mem_clip.img[k])) continue;
			memmove(tmp + offd, tmp + offs, nw);
		}
	}

	/* Try to realloc memory for smaller clipboard */
	tmp = realloc(mem_clipboard, nw * nh * mem_clip_bpp);
	if (tmp) mem_clipboard = tmp;
	for (k = 1; k < NUM_CHANNELS; k++)
	{
		if (!(tmp = mem_clip.img[k])) continue;
		tmp = realloc(tmp, nw * nh);
		if (tmp) mem_clip.img[k] = tmp;
	}

	/* Reset clipboard to new size */
	mem_clip_new(nw, nh, mem_clip_bpp, 0, old_img);
	memcpy(mem_clip.img, old_img, sizeof(chanlist));
	mem_clip_x += minx;
	mem_clip_y += miny;

	if (marq_status >= MARQUEE_PASTE) // We're trimming live paste area
	{
		marq_x2 = (marq_x1 += minx) + nw - 1;
		marq_y2 = (marq_y1 += miny) + nh - 1;
	}
}

void pressed_copy(int cut)
{
	if (!copy_clip()) return;
	if (tool_type == TOOL_POLYGON) poly_mask();
	channel_mask();
	if (cut) cut_clip();
	update_stuff(cut ? UPD_CUT : UPD_COPY);
}

void pressed_lasso(int cut)
{
	/* Lasso a new selection */
	if (((marq_status > MARQUEE_NONE) && (marq_status < MARQUEE_PASTE)) ||
		(poly_status == POLY_DONE))
	{
		if (!copy_clip()) return;
		if (tool_type == TOOL_POLYGON) poly_mask();
		else mem_clip_mask_init(255);
		if (!lasso_sel || !channel_mask())
		{
			poly_lasso(poly_status == POLY_DONE);
			channel_mask();
		}
		trim_clip();
		if (cut) cut_clip();
		pressed_paste(TRUE);
	}
	/* Trim an existing clipboard */
	else
	{
		if (!lasso_sel || !mem_clip_mask)
		{
			unsigned char *oldmask = mem_clip_mask;

			mem_clip_mask = NULL;
			mem_clip_mask_init(255);
			poly_lasso(FALSE);
			if (mem_clip_mask && oldmask)
			{
				int i, j = mem_clip_w * mem_clip_h;
				for (i = 0; i < j; i++)
					oldmask[i] &= mem_clip_mask[i];
				mem_clip_mask_clear();
			}
			if (!mem_clip_mask) mem_clip_mask = oldmask;
		}
		trim_clip();
		update_stuff(UPD_CGEOM);
	}
}

void update_menus()			// Update edit/undo menu
{
	static int memwarn;
	int i, j, statemap;

	if (mem_undo_fail && !memwarn) memwarn = alert_box(_("Warning"),
		_("You have not allocated enough memory in the Preferences window to undo this action."),
		"", NULL); // Not an error
	update_undo_bar();

	statemap = mem_img_bpp == 3 ? NEED_24 | NEED_NOIDX : NEED_IDX;
	if (mem_channel != CHN_IMAGE) statemap |= NEED_NOIDX;
	if ((mem_img_bpp == 3) && mem_img[CHN_ALPHA]) statemap |= NEED_RGBA;

	if (mem_clipboard) statemap |= NEED_PCLIP;
	if (mem_clipboard && (mem_clip_bpp == 3)) statemap |= NEED_ACLIP;

	if ( marq_status == MARQUEE_NONE )
	{
//		statemap &= ~(NEED_SEL | NEED_CROP);
		if (poly_status == POLY_DONE) statemap |= NEED_MARQ | NEED_LASSO;
	}
	else
	{
		statemap |= NEED_MARQ;

		/* If we are pasting disallow copy/cut/crop */
		if (marq_status < MARQUEE_PASTE)
			statemap |= NEED_SEL | NEED_CROP | NEED_LASSO;

		/* Only offer the crop option if the user hasn't selected everything */
		if (!((abs(marq_x1 - marq_x2) < mem_width - 1) ||
			(abs(marq_y1 - marq_y2) < mem_height - 1)))
			statemap &= ~NEED_CROP;
	}

	/* Forbid RGB-to-indexed paste, but allow indexed-to-RGB */
	if (mem_clipboard && (mem_clip_bpp <= MEM_BPP)) statemap |= NEED_CLIP;

	if (mem_undo_done) statemap |= NEED_UNDO;
	if (mem_undo_redo) statemap |= NEED_REDO;

	cmd_set(menu_slots[MENU_CHAN0 + mem_channel], TRUE);
	cmd_set(menu_slots[MENU_DCHAN0], hide_image);
	cmd_set(menu_slots[MENU_OALPHA], overlay_alpha);

	for (i = j = 0; i < NUM_CHANNELS; i++)	// Enable/disable channel enable/disable
	{
		if (mem_img[i]) j++;
		cmd_sensitive(menu_slots[MENU_DCHAN0 + i], !!mem_img[i]);
	}
	if (j > 1) statemap |= NEED_CHAN;

	cmd_setv(main_window_, (void *)statemap, WDATA_ACTMAP);

	/* Switch to default tool if active smudge tool got disabled */
	if ((tool_type == TOOL_SMUDGE) &&
		!cmd_checkv(icon_buttons[SMUDGE_TOOL_ICON], SLOT_SENSITIVE))
		change_to_tool(DEFAULT_TOOL_ICON);
}

void update_stuff(int flags)
{
	/* Always check current channel first */
	if (!mem_img[mem_channel])
	{
		mem_channel = CHN_IMAGE;
		flags |= UPD_CHAN;
	}

	/* And check paste validity too */
	if ((marq_status >= MARQUEE_PASTE) &&
		(!mem_clipboard || (mem_clip_bpp > MEM_BPP)))
		pressed_select(FALSE);

	if (flags & CF_CAB)
		flags |= mem_channel == CHN_IMAGE ? UPD_AB : UPD_GRAD;
	if (flags & CF_GEOM)
	{
		int wh[2];
		canvas_size(wh + 0, wh + 1);
		cmd_setv(drawing_canvas, wh, CANVAS_SIZE);
	}
	if (flags & CF_CGEOM)
		if (marq_status >= MARQUEE_PASTE) flags |= CF_DRAW;
	if (flags & (CF_GEOM | CF_CGEOM))
		check_marquee();
	if (flags & CF_PAL)
	{
		int wh[2] = { PALETTE_WIDTH,
			mem_cols * PALETTE_SWATCH_H + PALETTE_SWATCH_Y * 2 };

		if (mem_col_A >= mem_cols) mem_col_A = 0;
		if (mem_col_B >= mem_cols) mem_col_B = 0;
		mem_mask_init();	// Reinit RGB masks
		cmd_setv(drawing_palette, wh, CANVAS_SIZE);
	}
	if (flags & CF_AB)
	{
		mem_pat_update();
		if (text_paste && (marq_status >= MARQUEE_PASTE))
		{
			if (text_paste == TEXT_PASTE_FT) ft_render_text();
			else /* if ( text_paste == TEXT_PASTE_GTK ) */
				render_text();
			check_marquee();
			flags |= CF_PMODE;
		}
	}
	if (flags & CF_GRAD)
		grad_def_update(-1);
	if (flags & CF_PREFS)
	{
		update_undo_depth();	// If undo depth was changed
		update_recent_files();
		init_status_bar();	// Takes care of all statusbar parts
	}
	if (flags & CF_MENU)
		update_menus();
	if (flags & CF_SET)
		toolbar_update_settings();
#if 0
// !!! Too risky for now - need a safe path which only calls update_xy_bar()
	if (flags & CF_PIXEL)
		move_mouse(0, 0, 0);	// To cause update of XY bar
#endif
	if (flags & CF_PMODE)
		if ((marq_status >= MARQUEE_PASTE) && show_paste) flags |= CF_DRAW;
	if (flags & CF_GMODE)
		if ((tool_type == TOOL_GRADIENT) && grad_opacity) flags |= CF_DRAW;

	/* The next parts can be done later in a cumulative update */
	flags |= update_later;
	if (script_cmds && !(flags & CF_NOW)) flags ^= update_later = flags &
		(CF_NAME | CF_IMGBAR | CF_SELBAR | CF_TRANS | CF_CURSOR |
		 CF_DRAW | CF_VDRAW | CF_PDRAW | CF_TDRAW |
		 CF_ALIGN | CF_VALIGN);
	update_later &= ~flags;

	if (flags & CF_NAME)
		update_titlebar();
	if (flags & CF_IMGBAR)
		update_image_bar();
	if (flags & CF_SELBAR)
		update_sel_bar(TRUE);
	if (flags & CF_CURSOR)
		set_cursor(NULL);
	if (flags & CF_DRAW)
		if (drawing_canvas) cmd_repaint(drawing_canvas);
	if (flags & CF_VDRAW)
		if (view_showing && vw_drawing) cmd_repaint(vw_drawing);
	if (flags & CF_PDRAW)
	{
		mem_pal_init();		// Update palette RGB on screen
		cmd_repaint(drawing_palette);
	}
	if (flags & CF_TDRAW)
	{
		update_top_swatch();
		cmd_repaint(drawing_col_prev);
	}
	if (flags & CF_ALIGN)
		realign_size();
	if (flags & CF_VALIGN)
		vw_realign();
	if (flags & CF_TRANS)
		layer_show_trans();
}

void pressed_do_undo(int redo)
{
	mem_do_undo(redo);
	update_stuff(UPD_ALL | CF_NAME);
}

static int load_pal(char *filename)		// Load palette file
{
	int ftype, res = -1;

	ftype = detect_palette_format(filename);
	if (ftype < 0) return (-1); /* Silently fail if no file */

	if (ftype != FT_NONE) res = load_image(filename, FS_PALETTE_LOAD, ftype);

	/* Successful... */
	if (res == 1) update_stuff(UPD_UPAL);
	/* ...or not */
	else alert_box(_("Error"), _("Invalid palette file"), NULL);

	return (res == 1 ? 0 : -1);
}


void set_new_filename(int layer, char *fname)
{
	mem_replace_filename(layer, fname);
	if (layer == layer_selected) update_stuff(UPD_NAME);
}

static int populate_channel(char *filename)
{
	int ftype, res = -1;

	ftype = detect_image_format(filename);
	if (ftype < 0) return (-1); /* Silently fail if no file */

	/* Don't bother with mismatched formats */
	if (file_formats[ftype].flags & (MEM_BPP == 1 ? FF_IDX : FF_RGB))
		res = load_image(filename, FS_CHANNEL_LOAD, ftype);

	/* Successful */
	if (res == 1) update_stuff(UPD_UIMG);

	/* Not enough memory available */
	else if (res == FILE_MEM_ERROR) memory_errors(1);

	/* Unspecified error */
	else alert_box(_("Error"), _("Invalid channel file."), NULL);

	return (res == 1 ? 0 : -1);
}



///	FILE SELECTION WINDOW

static int anim_mode = ANM_COMP;

typedef struct {
	int is_anim;
	char *what;
	void **lload, **load1, **explode;
	void **res;
} animfile_dd;

static char *modes_txt[] = { _("Raw frames"), _("Composited frames"),
		_("Composited frames with nonzero delay") };

#define WBbase animfile_dd
static void *animfile_code[] = {
	WINDOWm(_("Load Frames")),
	MLABELcp(what),
	EQBOXB, // !!! why not HBOXB ?
	REF(lload), BUTTON(_("Load into Layers"), dialog_event), FOCUS,
	WDONE,
	BORDER(RPACK, 0),
	IF(is_anim), RPACKv(modes_txt, 3, 0, anim_mode),
	HSEP,
	EQBOXB,
	REF(load1), CANCELBTN(_("Load First Frame"), dialog_event),
	REF(explode), BUTTON(_("Explode Frames"), dialog_event),
#ifndef WIN32
	IF(is_anim), BUTTON(_("View Animation"), dialog_event),
#endif
	WDONE,
	RAISED, WDIALOG(res)
};
#undef WBbase

static int anim_file_dialog(int ftype, int is_anim)
{
	animfile_dd tdata, *dt;
	void **res, **where;
	char *tmp;
	int i;


	tdata.is_anim = is_anim;
	tmp = g_strdup_printf(is_anim ? __("This is an animated %s file.") :
		__("This is a multipage %s file."),
		file_formats[ftype & FTM_FTYPE].name);
	tdata.what = tmp;
	res = run_create(animfile_code, &tdata, sizeof(tdata)); // run dialog
	g_free(tmp);

	/* Retrieve results */
	run_query(res);
	dt = GET_DDATA(res);
	where = origin_slot(dt->res);
	i = where == dt->lload ? 3 : where == dt->load1 ? 0 :
		where == dt->explode ? 1 : 2;
	run_destroy(res);

	return (i);
}

static void handle_file_error(int res)
{
	char mess[256], *txt = NULL;

	/* Image was too large for OS */
	if (res == FILE_MEM_ERROR) memory_errors(1);
	else if (res == TOO_BIG)
		snprintf(txt = mess, 250, __("File is too big, must be <= to width=%i height=%i"), MAX_WIDTH, MAX_HEIGHT);
	else if (res == EXPLODE_FAILED)
		txt = _("Unable to explode frames");
	else if (res <= 0)
		txt = _("Unable to load file");
	else if (res == FILE_LIB_ERROR)
		txt = _("The file import library had to terminate due to a problem with the file (possibly corrupt image data or a truncated file). I have managed to load some data as the header seemed fine, but I would suggest you save this image to a new file to ensure this does not happen again.");
	else if (res == FILE_TOO_LONG)
		txt = _("The animation is too long to load all of it into layers.");
	else if (res == FILE_EXP_BREAK)
		txt = _("Could not explode all the frames in the animation.");
	if (txt) alert_box(_("Error"), txt, NULL);
}

typedef struct {
	char *what;
	void **ok, **res;
	int w, h;
} scale_dd;

#define WBbase scale_dd
static void *scale_code[] = {
	WINDOWm(_("Size")),
	MLABELpx(what, 5, 5, 4),
	TABLE2(2),
	TSPIN(_("Width"), w, 0, MAX_WIDTH),
	TSPIN(_("Height"), h, 0, MAX_HEIGHT),
	WDONE,
	HSEP,
	EQBOXB,
	CANCELBTN(_("Cancel"), dialog_event),
	REF(ok), OKBTN(_("OK"), dialog_event),
	WDONE,
	RAISED, WDIALOG(res)
};
#undef WBbase

static void scale_file_dialog(int ftype, int *w, int *h)
{
	scale_dd tdata, *dt;
	void **res;
	char *tmp;


	memset(&tdata, 0, sizeof(tdata));
	tdata.what = tmp = g_strdup_printf(__("This is a scalable %s file."),
		file_formats[ftype & FTM_FTYPE].name);
	res = run_create(scale_code, &tdata, sizeof(tdata)); // run dialog
	g_free(tmp);

	/* Retrieve results */
	run_query(res);
	dt = GET_DDATA(res);
	if (origin_slot(dt->res) == dt->ok) *w = dt->w , *h = dt->h;
	run_destroy(res);
}

typedef struct {
	void **pathbox;
	char *title, **ftnames, **tiffnames;
	char *frames_name, *ex_path;
	int frames_ftype, frames_anim, load_lr;
	int mode, fpmode;
	int fmask, ftype, ftypes[NUM_FTYPES];
	int need_save, need_anim, need_undo, need_icc, script;
	int jpeg_c, png_c, tga_c, jp2_c, xtrans[3], xx[3], xy[3];
	int tiff_m, lzma_c;
	int gif_delay, undo, icc;
	int w, h;
	/* Note: filename is in system encoding */
	char filename[PATHBUF];
} fselector_dd;

int do_a_load_x(char *fname, int undo, void *v)
{
	char real_fname[PATHBUF];
	int res, rres, ftype, mult = 0, w = 0, h = 0;


	resolve_path(real_fname, PATHBUF, fname);
	ftype = detect_image_format(real_fname);

	if ((ftype < 0) || (ftype == FT_NONE))
	{
		alert_box(_("Error"), ftype < 0 ? _("Cannot open file") :
			_("Unsupported file format"), NULL);
		return (1);
	}

	set_image(FALSE);

	if (ftype == FT_LAYERS1) mult = res = load_layers(real_fname);
	else
	{
		if (script_cmds && v)
		{
			fselector_dd *dt = v;
			w = dt->w;
			h = dt->h;
		}
		else if (!script_cmds && (file_formats[ftype].flags & FF_SCALE))
			scale_file_dialog(ftype, &w, &h);
		res = load_image_scale(real_fname, FS_PNG_LOAD,
			ftype | (undo ? FTM_UNDO : 0), w, h);
	}
	rres = res;

loaded:
	/* Multiframe file was loaded so tell user */
	if ((res == FILE_HAS_FRAMES) || (res == FILE_HAS_ANIM))
	{
		int i, is_anim = res == FILE_HAS_ANIM;

		/* Don't ask user in viewer mode */
// !!! When implemented, load as frameset & run animation in that case instead
		if (viewer_mode && view_image_only) i = 0;
		/* Don't ask in script mode too */
		else if (script_cmds && v)
		{
			fselector_dd *dt = v;
			i = dt->ex_path ? -1 : dt->load_lr ? 3 : 0;
		}
		else if (script_cmds) i = 0;
		else i = anim_file_dialog(ftype, is_anim);
		is_anim = is_anim ? anim_mode : ANM_PAGE;

		if (i == 3)
		{
			/* Make current layer, with first frame in it, background */
			if (layer_selected)
			{
				/* Simply swap layer data pointers */
				layer_image *tip = layer_table[layer_selected].image;
				layer_table[layer_selected].image = layer_table[0].image;
				layer_table[0].image = tip;
				layer_selected = 0;
			}
			mult = res = load_to_layers(real_fname, ftype, is_anim);
			goto loaded;
		}
		else if (i == -1) /* Explode frames into preset directory */
		{
			fselector_dd *dt = v;
			i = dt->ftypes[dt->ftype];
			res = explode_frames(dt->ex_path, is_anim, real_fname,
				ftype, i == FT_NONE ? ftype : i);
			goto loaded;
		}
		else if (i == 1) /* Ask for directory to explode frames to */
		{
			void *xdata[3];

			/* Needed when starting new mtpaint process later */
			xdata[0] = real_fname;
			xdata[1] = (void *)ftype;
			xdata[2] = (void *)is_anim;
			file_selector_x(FS_EXPLODE_FRAMES, xdata);
		}
		else if (i == 2) run_def_action(DA_GIF_PLAY, real_fname, NULL, 0);
	}

	/* An error happened */
	else if (res != 1)
	{
		handle_file_error(res);
		if (res <= 0) // Hard error
		{
			set_image(TRUE);
			return (1);
		}
	}

	/* Whether we loaded something or failed to, old image is gone anyway */
	if (!script_cmds) register_file(real_fname); // Ignore what scripts do
	if (!mult) /* A single image */
	{
		/* To prevent 1st frame overwriting a multiframe file */
		char *nm = g_strconcat(real_fname, rres == FILE_HAS_FRAMES ?
			".000" : NULL, NULL);
		set_new_filename(layer_selected, nm);
		g_free(nm);

		if ( layers_total>0 )
			layers_notify_changed(); // We loaded an image into the layers, so notify change
	}
	else /* A whole bunch of layers */
	{
//		pressed_layers();
		// We have just loaded a layers file so ensure view window is open
		view_show();
	}

	/* Show new image */
	if (!undo) reset_tools();
	else // No reason to reset tools in undoable mode
	{
		notify_unchanged(NULL);
		update_stuff(UPD_ALL);
	}

	set_image(TRUE);
	return (0);
}

int check_file( char *fname )		// Does file already exist?  Ask if OK to overwrite
{
	char *msg, *f8;
	int res = 0;

	if ( valid_file(fname) == 0 )
	{
		f8 = gtkuncpy(NULL, fname, 0);
		msg = g_strdup_printf(__("File: %s already exists. Do you want to overwrite it?"), f8);
		res = alert_box(_("File Found"), msg, _("NO"), _("YES"), NULL) != 2;
		g_free(msg);
		g_free(f8);
	}

	return (res);
}

static void change_image_format(fselector_dd *dt, void **wdata, int what,
	void **where)
{
	int ftype;
	unsigned int flags;

	if (!dt->need_save) return; // no use
	cmd_read(where, dt);
	ftype = dt->ftypes[dt->ftype];
	flags = ftype != FT_TIFF ? file_formats[ftype].flags :
		tiff_formats[dt->tiff_m].flags;
	/* Hide/show name/value widget pairs */
	cmd_setv(wdata, (void *)flags, WDATA_ACTMAP);
}

// !!! GCC inlining this, is a waste of space
int ftype_selector(int mask, char *ext, int def, char **names, int *ftypes)
{
	fformat *ff;
	int i, j, k, l, ft_sort[NUM_FTYPES], ft_key[NUM_FTYPES];

	ft_key[0] = ft_sort[0] = 0;
	k = def == FT_NONE;	// Include FT_NONE if default
	for (i = 0; i < NUM_FTYPES; i++)	// Populate sorted filetype list
	{
		ff = file_formats + i;
		if (ff->flags & FF_NOSAVE) continue;
		if (!(ff->flags & mask)) continue;
		l = (ff->name[0] << 16) + (ff->name[1] << 8) + ff->name[2];
		for (j = k; j > 0; j--)
		{
			if (ft_key[j - 1] < l) break;
			ft_sort[j] = ft_sort[j - 1];
			ft_key[j] = ft_key[j - 1];
		}
		ft_key[j] = l;
		ft_sort[j] = i;
		k++;
	}
	j = -1;
	for (l = 0; l < k; l++)		// Prepare to generate option menu
	{
		i = ft_sort[l];
		if ((j < 0) && (i == def)) j = l;	// Default type if not saved yet
		ff = file_formats + i;
		if (!strncasecmp(ext, ff->ext, LONGEST_EXT) || (ff->ext2[0] &&
			!strncasecmp(ext, ff->ext2, LONGEST_EXT))) j = l;
		names[l] = ff->name;
	}

	names[k] = NULL;
	memcpy(ftypes, ft_sort, sizeof(int) * k);
	return (j);
}

int tiff_type_selector(int mask, int def, char **names)
{
	int i, n;

	for (n = i = 0; tiff_formats[i].name; i++)
	{
		names[i] = "";
		if (tiff_formats[i].flags & mask)
		{
			names[i] = tiff_formats[i].name;
			if (i == def) n = i;
		}
	}
	names[i] = NULL;
	return (n);
}

void init_ls_settings(ls_settings *settings, void **wdata)
{
	png_color *pal = mem_pal;

	/* Set defaults */
	memset(settings, 0, sizeof(ls_settings));
	settings->ftype = FT_NONE;
	settings->xpm_trans = mem_xpm_trans;
	settings->hot_x = mem_xbm_hot_x;
	settings->hot_y = mem_xbm_hot_y;
	settings->req_w = settings->req_h = 0;
	settings->jpeg_quality = jpeg_quality;
	settings->png_compression = png_compression;
	settings->lzma_preset = lzma_preset;
	settings->tiff_type = -1; /* Use default */
	settings->tga_RLE = tga_RLE;
	settings->jp2_rate = jp2_rate;
	settings->gif_delay = preserved_gif_delay;

	/* Read in settings */
	if (wdata)
	{
		fselector_dd *dt = GET_DDATA(wdata);
		settings->xpm_trans = dt->xtrans[0];
		settings->hot_x = dt->xx[0];
		settings->hot_y = dt->xy[0];
		settings->jpeg_quality = dt->jpeg_c;
		settings->png_compression = dt->png_c;
		settings->lzma_preset = dt->lzma_c;
		settings->tiff_type = dt->tiff_m;
		settings->tga_RLE = dt->tga_c;
		settings->jp2_rate = dt->jp2_c;
		settings->gif_delay = dt->gif_delay;

		settings->mode = dt->mode;
		settings->ftype = dt->fmask ? dt->ftypes[dt->ftype] :
			/* For animation, hardcoded GIF format for now */
			dt->mode == FS_EXPORT_GIF ? FT_GIF : FT_NONE;

		undo_load = dt->undo;
#ifdef U_LCMS
		apply_icc = dt->icc;
#endif
		// Use background's palette
		if ((dt->mode == FS_COMPOSITE_SAVE) && layer_selected)
			pal = layer_table[0].image->image_.pal;
	}

	/* Default expansion of xpm_trans */
	settings->rgb_trans = settings->xpm_trans < 0 ? -1 :
		PNG_2_INT(pal[settings->xpm_trans]);
}

static void store_ls_settings(ls_settings *settings)
{
	int ftype = settings->ftype, ttype = settings->tiff_type;
	unsigned int fflags = (ftype == FT_TIFF) && (ttype >= 0) ?
		tiff_formats[ttype].flags : file_formats[ftype].flags;

	switch (settings->mode)
	{
	case FS_PNG_SAVE:
		if (fflags & FF_TRANS)
			mem_set_trans(settings->xpm_trans);
		if (fflags & FF_SPOT)
		{
			mem_xbm_hot_x = settings->hot_x;
			mem_xbm_hot_y = settings->hot_y;
		}
		// Fallthrough
	case FS_CHANNEL_SAVE:
	case FS_COMPOSITE_SAVE:
		if (fflags & FF_COMPJ)
			jpeg_quality = settings->jpeg_quality;
		if (fflags & (FF_COMPZ | FF_COMPZT))
			png_compression = settings->png_compression;
		if (fflags & FF_COMPLZ)
			lzma_preset = settings->lzma_preset;
		if (fflags & FF_COMPR)
			tga_RLE = settings->tga_RLE;
		if (fflags & FF_COMPJ2)
			jp2_rate = settings->jp2_rate;
		if ((fflags & FF_COMPT) && (ttype >= 0))
		{
			/* Remember selection for incompatible types separately;
			 * image is not set up yet, so use the mode instead */
			*(settings->mode == FS_COMPOSITE_SAVE ? &tiff_rtype :
				(settings->mode == FS_CHANNEL_SAVE) &&
				(mem_channel != CHN_IMAGE) ? &tiff_itype :
				mem_img_bpp == 3 ? &tiff_rtype :
				mem_cols > 2 ? &tiff_itype : &tiff_btype) = ttype;
		}
		break;
	case FS_EXPORT_GIF:
		preserved_gif_delay = settings->gif_delay;
		break;
	}
}

static void fs_ok(fselector_dd *dt, void **wdata)
{
	ls_settings settings;
	char fname[PATHTXT], *msg, *f8;
	char *c, *ext, *ext2, *gif, *gif2;
	int i, j, res, redo = 1;

	run_query(wdata);
	/* Pick up extra info */
	init_ls_settings(&settings, wdata);

	/* Needed to show progress in Windows GTK+2 */
// !!! Disable for now, see what happens
//	gtk_window_set_modal(GTK_WINDOW(GET_REAL_WINDOW(wdata)), FALSE);

	/* Looks better if no dialog under progressbar */
	cmd_showhide(GET_WINDOW(wdata), FALSE);

	/* File extension */
	cmd_peekv(GET_WINDOW(wdata), fname, PATHTXT, FPICK_RAW); // raw filename
	c = strrchr(fname, '.');
	while (TRUE)
	{
		/* Cut the extension off */
		if ((settings.mode == FS_CLIP_FILE) ||
			(settings.mode == FS_EXPLODE_FRAMES) ||
			(settings.mode == FS_EXPORT_UNDO) ||
			(settings.mode == FS_EXPORT_UNDO2))
		{
			if (!c) break;
			*c = '\0';
		}
		/* Modify the file extension if needed */
		else if (settings.mode == FS_PNG_LOAD) break;
		else
		{
			ext = file_formats[settings.ftype].ext;
			if (!ext[0]) break;
		
			if (c) /* There is an extension */
			{
				/* Same extension? */
				if (!strncasecmp(c + 1, ext, 256)) break;
				/* Alternate extension? */
				ext2 = file_formats[settings.ftype].ext2;
				if (ext2[0] && !strncasecmp(c + 1, ext2, 256))
					break;
				/* Another file type? */
				for (i = 0; i < NUM_FTYPES; i++)
				{
					if (strncasecmp(c + 1, file_formats[i].ext, 256) &&
						strncasecmp(c + 1, file_formats[i].ext2, 256))
						continue;
					/* Truncate */
					*c = '\0';
					break;
				}
			}
			i = strlen(fname);
			j = strlen(ext);
			if (i + j >= PATHTXT - 1) break; /* Too long */
			fname[i] = '.';
			strncpy(fname + i + 1, ext, j + 1);
		}
		cmd_setv(GET_WINDOW(wdata), fname, FPICK_RAW); // raw filename
		break;
	}

	/* Get filename the proper way, in system filename encoding */
	cmd_read(GET_WINDOW(wdata), dt);

	switch (settings.mode)
	{
	case FS_PNG_LOAD:
		if (do_a_load_x(dt->filename, undo_load, dt) == 1) break;
		redo = 0;
		break;
	case FS_PNG_SAVE:
		if (check_file(dt->filename)) break;
		store_ls_settings(&settings);	// Update data in memory
		if (gui_save(dt->filename, &settings) < 0) break;
		if (layers_total > 0)
		{
			/* Filename has changed so layers file needs re-saving to be correct */
			if (!mem_filename || strcmp(dt->filename, mem_filename))
				layers_notify_changed();
		}
		set_new_filename(layer_selected, dt->filename);
		redo = 0;
		break;
	case FS_PALETTE_LOAD:
		if (load_pal(dt->filename)) break;
		notify_changed();
		redo = 0;
		break;
	case FS_PALETTE_SAVE:
		if (check_file(dt->filename)) break;
		settings.pal = mem_pal;
		settings.colors = mem_cols;
		redo = 2;
		if (save_image(dt->filename, &settings)) break;
		redo = 0;
		break;
	case FS_CLIP_FILE:
	case FS_SELECT_FILE:
	case FS_SELECT_DIR:
		if (dt->pathbox) cmd_setv(dt->pathbox, dt->filename, PATH_VALUE);
		redo = 0;
		break;
	case FS_EXPORT_UNDO:
	case FS_EXPORT_UNDO2:
		if (export_undo(dt->filename, &settings))
			alert_box(_("Error"), _("Unable to export undo images"), NULL);
		redo = 0;
		break;
	case FS_EXPORT_ASCII:
		if (check_file(dt->filename)) break;
		if (export_ascii(dt->filename))
			alert_box(_("Error"), _("Unable to export ASCII file"), NULL);
		redo = 0;
		break;
	case FS_LAYER_SAVE:
		if (check_file(dt->filename)) break;
		if (save_layers(dt->filename) != 1) break;
		redo = 0;
		break;
	case FS_EXPLODE_FRAMES:
		gif = dt->frames_name;
		res = dt->frames_ftype;
		i = dt->frames_anim;
		res = explode_frames(dt->filename, i, gif, res, settings.ftype);
		if (res != 1) handle_file_error(res);
		if (res > 0) // Success or nonfatal error
		{
			c = strrchr(gif, DIR_SEP);
			if (!c) c = gif;
			else c++;
			c = file_in_dir(NULL, dt->filename, c, PATHBUF);
			run_def_action(DA_GIF_EDIT, c, NULL, preserved_gif_delay);
			free(c);
			redo = 0;
		}
		break;
	case FS_EXPORT_GIF:
		if (check_file(dt->filename)) break;
		store_ls_settings(&settings);	// Update data in memory
		gif2 = g_strdup(mem_filename);	// Guaranteed to be non-NULL
		for (i = strlen(gif2) - 1; i >= 0; i--)
		{
			if (gif2[i] == DIR_SEP) break;
			if ((unsigned char)(gif2[i] - '0') <= 9) gif2[i] = '?';
		}
		run_def_action(DA_GIF_CREATE, gif2, dt->filename, settings.gif_delay);
		if (!cmd_mode) // Don't launch GUI from commandline
			run_def_action(DA_GIF_PLAY, dt->filename, NULL, 0);
		g_free(gif2);
		redo = 0;
		break;
	case FS_CHANNEL_LOAD:
		if (populate_channel(dt->filename)) break;
		redo = 0;
		break;
	case FS_CHANNEL_SAVE:
		if (check_file(dt->filename)) break;
		store_ls_settings(&settings);	// Update data in memory
		settings.img[CHN_IMAGE] = mem_img[mem_channel];
		settings.width = mem_width;
		settings.height = mem_height;
		if (mem_channel == CHN_IMAGE)
		{
			settings.pal = mem_pal;
			settings.bpp = mem_img_bpp;
			settings.colors = mem_cols;
		}
		else
		{
			settings.pal = NULL; /* Greyscale one 'll be created */
			settings.bpp = 1;
			settings.colors = 256;
			settings.xpm_trans = -1;
		}
		redo = 2;
		if (save_image(dt->filename, &settings)) break;
		redo = 0;
		break;
	case FS_COMPOSITE_SAVE:
		if (check_file(dt->filename)) break;
		store_ls_settings(&settings);	// Update data in memory
		redo = 2;
		if (layer_save_composite(dt->filename, &settings)) break;
		redo = 0;
		break;
	default: // Paranoia
		redo = 0;
		break;
	}

	if (redo > 1) /* Report error */
	{
		f8 = gtkuncpy(NULL, dt->filename, 0);
		msg = g_strdup_printf(__("Unable to save file: %s"), f8);
		alert_box(_("Error"), msg, NULL);
		g_free(msg);
		g_free(f8);
	}
	if (redo && !script_cmds) /* Redo */
	{
		cmd_showhide(GET_WINDOW(wdata), TRUE);
// !!! Disable for now, see what happens
//		gtk_window_set_modal(GTK_WINDOW(GET_REAL_WINDOW(wdata)), TRUE);
		return;
	}
	/* Done */
	user_break |= redo; // Paranoia
	update_menus();
	run_destroy(wdata);
}

#define WBbase fselector_dd
static void *fselector_code[] = {
	FPICKpm(title, fpmode, filename, fs_ok, NULL),
	IFx(fmask, 1),
		MLABEL(_("File Format")),
		OPTDe(ftnames, ftype, change_image_format), TRIGGER,
	ENDIF(1),
	IFx(need_save, 1),
		VISMASK(~0),
		MLABELr(_("Transparency index")), ACTMAP(FF_TRANS),
			SPINa(xtrans), ACTMAP(FF_TRANS),
		MLABELr(_("TIFF Compression")), ACTMAP(FF_COMPT),
		OPTDe(tiffnames, tiff_m, change_image_format), ACTMAP(FF_COMPT),
		MLABELr(_("JPEG Save Quality (100=High)")), ACTMAP(FF_COMPJ),
			SPIN(jpeg_c, 0, 100), ACTMAP(FF_COMPJ),
		MLABELr(_("ZIP Compression (0=None)")), ACTMAP(FF_COMPZT),
		MLABELr(_("PNG Compression (0=None)")), ACTMAP(FF_COMPZ),
			SPIN(png_c, 0, 9), ACTMAP(FF_COMPZ | FF_COMPZT),
			ALTNAME("ZIP Compression (0=None)"),
		MLABELr(_("LZMA2 Compression (0=None)")), ACTMAP(FF_COMPLZ),
			SPIN(lzma_c, 0, 9), ACTMAP(FF_COMPLZ),
		MLABELr(_("TGA RLE Compression")), ACTMAP(FF_COMPR),
			SPIN(tga_c, 0, 1), ACTMAP(FF_COMPR),
		MLABELr(_("JPEG2000 Compression (0=Lossless)")), ACTMAP(FF_COMPJ2),
			SPIN(jp2_c, 0, 100), ACTMAP(FF_COMPJ2),
		MLABELr(_("Hotspot at X =")), ACTMAP(FF_SPOT),
			SPINa(xx), ACTMAP(FF_SPOT),
		MLABELr(_("Y =")), ACTMAP(FF_SPOT),
			SPINa(xy), ACTMAP(FF_SPOT),
	ENDIF(1),
	IFx(need_anim, 1),
		MLABEL(_("Animation delay")),
		SPIN(gif_delay, 1, MAX_DELAY),
	ENDIF(1),
	IF(need_undo), CHECK(_("Undoable"), undo),
#ifdef U_LCMS
	IF(need_icc), CHECK(_("Apply colour profile"), icc),
#endif
	WDONE,
	IFx(script, 1),
		/* For loading multiframe files */
		CHECK("Load into Layers", load_lr),
		uPATHSTR(ex_path), OPNAME("Explode Frames"),
		OPTv(modes_txt, 3, anim_mode), OPNAME("Frames"),
		/* For loading scalable images */
		SPIN(w, 0, MAX_WIDTH), OPNAME("Width"),
		SPIN(h, 0, MAX_HEIGHT), OPNAME("Height"),
	ENDIF(1),
	WXYWH("fs_window", 550, 500),
	CLEANUP(frames_name),
	RAISED, WSHOW
};
#undef WBbase

void file_selector_x(int action_type, void **xdata)
{
	fselector_dd tdata;
	char *names[NUM_FTYPES + 1], *tiffnames[TIFF_MAX_TYPES], *ext = NULL;
	int def = FT_PNG;


	memset(&tdata, 0, sizeof(tdata));
	tdata.script = !!script_cmds;
	tdata.mode = action_type;
	tdata.fpmode = FPICK_ENTRY;
	tdata.xtrans[0] = mem_xpm_trans;
	tdata.xtrans[1] = -1;
	tdata.xtrans[2] = mem_cols - 1;
	switch (action_type)
	{
	case FS_PNG_LOAD:
		if ((!script_cmds || !undo_load) && ((layers_total ?
			check_layers_for_changes() : check_for_changes()) == 1))
			return;
		tdata.title = _("Load Image File");
		tdata.fpmode = FPICK_LOAD;
		tdata.need_undo = TRUE;
#ifdef U_LCMS
		tdata.need_icc = TRUE;
#endif
		if (script_cmds)
		{
			tdata.fmask = FF_IMAGE;
			ext = "";
			def = FT_NONE;
		}
		break;
	case FS_PNG_SAVE:
		tdata.fmask = FF_SAVE_MASK;
		tdata.title = _("Save Image File");
		if (mem_filename) strncpy(tdata.filename, mem_filename, PATHBUF);
		tdata.need_save = TRUE;
		break;
	case FS_PALETTE_LOAD:
		tdata.title = _("Load Palette File");
		tdata.fpmode = FPICK_LOAD;
		break;
	case FS_PALETTE_SAVE:
		tdata.fmask = FF_PALETTE;
		def = FT_GPL;
		tdata.title = _("Save Palette File");
		break;
	case FS_EXPORT_UNDO:
		if (!mem_undo_done) return;
		tdata.fmask = FF_IMAGE;
		tdata.title = _("Export Undo Images");
		break;
	case FS_EXPORT_UNDO2:
		if (!mem_undo_done) return;
		tdata.fmask = FF_IMAGE;
		tdata.title = _("Export Undo Images (reversed)");
		break;
	case FS_EXPORT_ASCII:
		if (mem_cols > 16)
		{
			alert_box( _("Error"), _("You must have 16 or fewer palette colours to export ASCII art."), NULL);
			return;
		}
		tdata.title = _("Export ASCII Art");
		break;
	case FS_LAYER_SAVE: /* !!! No selectable layer file format yet */
		if (check_layers_all_saved()) return;
		tdata.title = _("Save Layer Files");
		strncpy(tdata.filename, layers_filename, PATHBUF);
		break;
	case FS_EXPLODE_FRAMES:
		tdata.frames_name = strdup(xdata[0]);
		tdata.frames_ftype = (int)xdata[1];
		tdata.frames_anim = (int)xdata[2];

		ext = file_formats[tdata.frames_ftype].ext;
		tdata.fmask = FF_IMAGE;
		tdata.title = _("Choose frames directory");
		tdata.fpmode = FPICK_DIRS_ONLY;
		break;
	case FS_EXPORT_GIF: /* !!! No selectable formats yet */
		if (!mem_filename)
		{
			alert_box(_("Error"), _("You must save at least one frame to create an animated GIF."), NULL);
			return;
		}
		tdata.title = _("Export GIF animation");
		tdata.need_anim = TRUE;
		break;
	case FS_CHANNEL_LOAD:
		tdata.title = _("Load Channel");
		tdata.fpmode = FPICK_LOAD;
#ifdef U_LCMS
		tdata.need_icc = MEM_BPP == 3;
#endif
		break;
	case FS_CHANNEL_SAVE:
		tdata.fmask = mem_channel != CHN_IMAGE ? FF_256 : FF_SAVE_MASK;
		tdata.title = _("Save Channel");
		tdata.need_save = TRUE;
		break;
	case FS_COMPOSITE_SAVE:
		tdata.fmask = FF_RGB;
		tdata.title = _("Save Composite Image");
		tdata.need_save = TRUE;
		if (layer_selected) // Use background's transparency
		{
			image_info *image = &layer_table[0].image->image_;
			tdata.xtrans[0] = image->trans;
			tdata.xtrans[2] = image->cols - 1;
		}
		break;
	case FS_CLIP_FILE:
	case FS_SELECT_FILE:
	case FS_SELECT_DIR:
		tdata.fpmode = action_type == FS_SELECT_DIR ?
			FPICK_LOAD | FPICK_DIRS_ONLY : FPICK_LOAD;
		tdata.title = xdata[0];
		tdata.pathbox = xdata[1]; // pathbox slot
		cmd_peekv(xdata[1], tdata.filename, PATHBUF, PATH_VALUE);
		break;
	default: /*
	FS_LAYER_LOAD,
	FS_PATTERN_LOAD,
	FS_CLIPBOARD,
	FS_PALETTE_DEF */
		return;	/* These are not for here */
	}

	/* Default filename */
	if (!tdata.filename[0]) file_in_dir(tdata.filename,
		inifile_get("last_dir", get_home_directory()),
		action_type == FS_LAYER_SAVE ? "layers.txt" : "", PATHBUF);
	if (!ext)
	{
		ext = strrchr(tdata.filename, '.');
		ext = ext ? ext + 1 : "";
	}

	/* Filetype selectors */
	if (tdata.fmask)
	{
		tdata.ftype = ftype_selector(tdata.fmask, ext, def,
			tdata.ftnames = names, tdata.ftypes);
		tdata.tiff_m = tiff_type_selector(tdata.fmask,
			tdata.fmask & FF_RGB ? tiff_rtype :
			tdata.fmask & FF_BW ? tiff_btype : tiff_itype,
			tdata.tiffnames = tiffnames);
	}

	tdata.xx[0] = mem_xbm_hot_x;
	tdata.xx[1] = -1;
	tdata.xx[2] = mem_width - 1;
	tdata.xy[0] = mem_xbm_hot_y;
	tdata.xy[1] = -1;
	tdata.xy[2] = mem_height - 1;

	tdata.jpeg_c = jpeg_quality;
	tdata.png_c = png_compression;
	tdata.tga_c = tga_RLE;
	tdata.jp2_c = jp2_rate;
	tdata.lzma_c = lzma_preset;

	tdata.gif_delay = preserved_gif_delay;
	tdata.undo = undo_load;
#ifdef U_LCMS
	tdata.icc = apply_icc;
#endif

	run_create_(fselector_code, &tdata, sizeof(tdata), script_cmds);
}

void file_selector(int action_type)
{
	file_selector_x(action_type, NULL);
}

void canvas_center(float ic[2])		// Center of viewable area
{
	int w, h, xyhv[4];

	ic[0] = ic[1] = 0.5;
	if (script_cmds) return;
	cmd_peekv(scrolledwindow_canvas, xyhv, sizeof(xyhv), CSCROLL_XYSIZE);
	canvas_size(&w, &h);
	if (xyhv[2] < w) ic[0] = (xyhv[0] + xyhv[2] * 0.5) / w;
	if (xyhv[3] < h) ic[1] = (xyhv[1] + xyhv[3] * 0.5) / h;
}

void align_size(float new_zoom)		// Set new zoom level
{
	static int zoom_flag;

	if (zoom_flag) return;		// Needed as we could be called twice per iteration

	if (new_zoom < MIN_ZOOM) new_zoom = MIN_ZOOM;
	if (new_zoom > MAX_ZOOM) new_zoom = MAX_ZOOM;
	if (new_zoom == can_zoom) return;

	zoom_flag = 1;
	if (!mem_ics && !cmd_mode)
	{
		int xc, yc, dx, dy, w, h, x, y, xyhv[4];

		cmd_peekv(scrolledwindow_canvas, xyhv, sizeof(xyhv), CSCROLL_XYSIZE);
		xc = xyhv[0] * 2 + (w = xyhv[2]);
		yc = xyhv[1] * 2 + (h = xyhv[3]);
		dx = dy = 0;

		if (cursor_zoom)
		{
			mouse_ext m;
			cmd_peekv(drawing_canvas, &m, sizeof(m), CANVAS_FIND_MOUSE);
			x = m.x - xyhv[0];
			y = m.y - xyhv[1];
			if ((x >= 0) && (x < w) && (y >= 0) && (y < h))
				dx = x * 2 - w , dy = y * 2 - h;
		}

		mem_icx = ((xc + dx - margin_main_x * 2) / can_zoom -
			dx / new_zoom) / (mem_width * 2);
		mem_icy = ((yc + dy - margin_main_y * 2) / can_zoom -
			dy / new_zoom) / (mem_height * 2);
	}
	mem_ics = 0;

	can_zoom = new_zoom;
	realign_size();
	zoom_flag = 0;
}

void realign_size()		// Reapply old zoom
{
	int xyhv[4], xywh[4];

	if (cmd_mode) return;
	cmd_peekv(scrolledwindow_canvas, xyhv, sizeof(xyhv), CSCROLL_XYSIZE);
	canvas_size(xywh + 2, xywh + 3);
	xywh[0] = xywh[1] = 0;	// New positions of scrollbar
	if (xyhv[2] < xywh[2]) xywh[0] = rint(xywh[2] * mem_icx - xyhv[2] * 0.5);
	if (xyhv[3] < xywh[3]) xywh[1] = rint(xywh[3] * mem_icy - xyhv[3] * 0.5);

	/* !!! CSCROLL's self-updating for CANVAS resize is delayed in GTK+, as
	 * is the actual resize; so to communicate new position to that latter
	 * resize, I preset both position and range of CSCROLL here - WJ */
	cmd_setv(scrolledwindow_canvas, xywh, CSCROLL_XYRANGE);
 
	cmd_setv(drawing_canvas, xywh + 2, CANVAS_SIZE);
	vw_focus_view();	// View window position may need updating
	toolbar_zoom_update();	// Zoom factor may have been reset
}

/* This tool is seamless: doesn't draw pixels twice if not requested to - WJ */
// !!! With GCC inlining this, weird size fluctuations can happen
void rec_continuous(int nx, int ny, int w, int h)
{
	linedata line1, line2, line3, line4;
	int ws2 = w >> 1, hs2 = h >> 1;
	int i, j, i2, j2, *xv;
	int dx[3] = {-ws2, w - ws2 - 1, -ws2};
	int dy[3] = {-hs2, h - hs2 - 1, -hs2};

	i = nx < tool_ox;
	j = ny < tool_oy;

	/* Redraw starting square only if need to fill in possible gap when
	 * size changes, or to draw stroke gradient in the proper direction */
	if (!tablet_working && !script_cmds &&
		((tool_type == TOOL_CLONE) || !STROKE_GRADIENT))
	{
		i2 = tool_ox + dx[i + 1] + 1 - i * 2;
		j2 = tool_oy + dy[j + 1] + 1 - j * 2;
		xv = &line3[0];
	}
	else
	{
		i2 = tool_ox + dx[i];
		j2 = tool_oy + dy[j];
		xv = &i2;
	}

	if (tool_ox == nx)
	{
		line_init(line1, tool_ox + dx[i], j2,
			tool_ox + dx[i], ny + dy[j + 1]);
		line_init(line3, tool_ox + dx[i + 1], j2,
			tool_ox + dx[i + 1], ny + dy[j + 1]);
		line2[2] = line4[2] = -1;
	}
	else
	{
		line_init(line2, tool_ox + dx[i], tool_oy + dy[j + 1],
			nx + dx[i], ny + dy[j + 1]);
		line_nudge(line2, i2, j2);
		line_init(line3, tool_ox + dx[i + 1], tool_oy + dy[j],
			nx + dx[i + 1], ny + dy[j]);
		line_nudge(line3, i2, j2);
		line_init(line1, *xv, line3[1], *xv, line2[1]);
		line_init(line4, nx + dx[i + 1], ny + dy[j],
			nx + dx[i + 1], ny + dy[j + 1]);
	}

	/* Prevent overwriting clone source */
	while (tool_type == TOOL_CLONE)
	{
		if ((j * 2 - 1) * clone_dy <= 0) break; // Ahead of dest
		if (mem_undo_opacity && (mem_undo_previous(mem_channel) !=
			mem_img[mem_channel])) break; // In another frame
#if 0 /* No real reason to spend code on avoiding the flip */
		i = abs(ny - tool_oy) + h - 1;
		if ((clone_dy < -i) || (clone_dy > i)) break; // Out Y range
		i = abs(nx - tool_ox) + w - 1;
		if ((clone_dx < -i) || (clone_dx > i)) break; // Out X range
#endif
		line_flip(line1);
		line_flip(line3);
		if (tool_ox == nx) break; // Only 2 lines
		line_flip(line2);
		line_flip(line4);
		draw_quad(line2, line1, line4, line3);
		return;
	}

	draw_quad(line1, line2, line3, line4);
}


static struct {
	float c_zoom;
	int points;
	int xy[2 * MAX_POLY + 2];
//	int step[MAX_POLY + 1];
} poly_cache;

static void poly_update_cache()
{
//	int dx, dy, *ps;
	int i, i0, last, *pxy, ds, zoom = 1, scale = 1;

	i0 = poly_cache.c_zoom == can_zoom ? poly_cache.points : 0;
	last = poly_points + (poly_status == POLY_DONE);
	if (i0 >= last) return; // Up to date

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);
	ds = scale >> 1;

	poly_cache.c_zoom = can_zoom;
	poly_cache.points = last;
	/* Get locations */
	pxy = poly_cache.xy + i0 * 2;
	for (i = i0; i < poly_points; i++)
	{
		*pxy++ = floor_div(poly_mem[i][0] * scale, zoom) + ds;
		*pxy++ = floor_div(poly_mem[i][1] * scale, zoom) + ds;
	}
	/* Join 1st & last point if finished */
	if (poly_status == POLY_DONE)
	{
		*pxy++ = poly_cache.xy[0];
		*pxy++ = poly_cache.xy[1];
	}
#if 0 /* No need for now */
	/* Get distances */
	poly_cache.step[0] = 0;
	if (!i0) i0 = 1;
	ps = poly_cache.step + i0 - 1;
	pxy = poly_cache.xy + i0 * 2 - 2;
	for (i = i0; i < last; i++ , pxy += 2 , ps++)
	{
		dx = abs(pxy[2] - pxy[0]);
		dy = abs(pxy[3] - pxy[1]);
		ps[1] = ps[0] + (dx > dy ? dx : dy);
	}
#endif
}

void stretch_poly_line(int x, int y)			// Clear old temp line, draw next temp line
{
	int old[4];

	if (!poly_points || (poly_points >= MAX_POLY)) return;
	if ((line_x2 == x) && (line_y2 == y)) return;	// This check reduces flicker

	copy4(old, line_xy);
	line_x2 = x;
	line_y2 = y;
	line_x1 = poly_mem[poly_points - 1][0];
	line_y1 = poly_mem[poly_points - 1][1];
	repaint_line(old);
}

static void refresh_lines(const int xy0[4], const int xy1[4]);

static void poly_conclude()
{
	if (!poly_points) poly_status = POLY_NONE;
	else
	{
		int n = poly_points - 1, edge[4] = {
			poly_mem[0][0], poly_mem[0][1],
			poly_mem[n][0], poly_mem[n][1] };

		poly_status = POLY_DONE;
		poly_bounds();
		check_marquee();
		poly_update_cache();
		/* Combine erasing old line with drawing final segment:
		 * there is no new line to be drawn */
		refresh_lines(line_xy, edge);
	}
	update_stuff(UPD_PSEL);
}

static void poly_add_po(int x, int y)
{
	if (!poly_points) poly_cache.c_zoom = 0; // Invalidate
	else if (!((x - poly_mem[poly_points - 1][0]) |
		(y - poly_mem[poly_points - 1][1]))) return; // Never stack
	poly_add(x, y);
	if (poly_points >= MAX_POLY) poly_conclude();
	else
	{
		int n = poly_points > 1 ? poly_points - 2 : 0;
		int old[4], edge[4] = {	poly_mem[n][0], poly_mem[n][1], x, y };

		poly_update_cache();
		copy4(old, line_xy);
		line_x1 = line_x2 = x;
		line_y1 = line_y2 = y;
		/* Combine erasing old line with redrawing new segment:
		 * the 1-point new line will be drawn as part of it */
		refresh_lines(old, edge);
		update_sel_bar(FALSE);
	}
}

static void poly_delete_po(int x, int y)
{
	if ((poly_status != POLY_SELECTING) && (poly_status != POLY_DONE))
		return; // Do nothing
	if (poly_points < 2) // Remove the only point
	{
		pressed_select(FALSE);
		return;
	}
	if (poly_status == POLY_SELECTING) // Delete last
	{
		poly_points--;
		/* New place for line */
		line_x2 = x;
		line_y2 = y;
		line_x1 = poly_mem[poly_points - 1][0];
		line_y1 = poly_mem[poly_points - 1][1];
	}
	else // Delete nearest
	{
		int i, ll, idx = 0, l = INT_MAX;

		for (i = 0; i < poly_points; i++)
		{
			ll = (poly_mem[i][0] - x) * (poly_mem[i][0] - x) +
				(poly_mem[i][1] - y) * (poly_mem[i][1] - y);
			if (ll < l) idx = i , l = ll;
		}
		memmove(poly_mem[idx], poly_mem[idx + 1], sizeof(poly_mem[0]) *
			(--poly_points - idx));
		poly_bounds();
		check_marquee();
	}
	poly_cache.c_zoom = 0; // Invalidate
	update_stuff(UPD_PSEL | UPD_RENDER);
}

static int tool_draw(int x, int y, int first_point, int *update)
{
	static int ncx, ncy;
	int minx, miny, xw, yh, ts2, tr2;
	int i, j, k, ox, oy, px, py, rx, ry, sx, sy, off1, off2;

	ts2 = tool_size >> 1;
	tr2 = tool_size - ts2 - 1;
	minx = x - ts2;
	miny = y - ts2;
	xw = yh = tool_size;

	/* Save the brush coordinates for next step */
	ox = ncx; oy = ncy;
	ncx = x; ncy = y;

	switch (tool_type)
	{
	case TOOL_SQUARE: case TOOL_CLONE:
		f_rectangle(x - ts2, y - ts2, tool_size, tool_size);
		break;
	case TOOL_CIRCLE:
		f_circle(x, y, tool_size);
		break;
	case TOOL_HORIZONTAL:
		miny = y; yh = 1;
		sline(x - ts2, y, x + tr2, y);
		break;
	case TOOL_VERTICAL:
		minx = x; xw = 1;
		sline(x, y - ts2, x, y + tr2);
		break;
	case TOOL_SLASH:
		sline(x + tr2, y - ts2, x - ts2, y + tr2);
		break;
	case TOOL_BACKSLASH:
		sline(x - ts2, y - ts2, x + tr2, y + tr2);
		break;
	case TOOL_SPRAY:
		for (j = 0; j < tool_flow; j++)
		{
			rx = x - ts2 + rand() % tool_size;
			ry = y - ts2 + rand() % tool_size;
			IF_IN_RANGE(rx, ry) put_pixel(rx, ry);
		}
		break;
	case TOOL_SHUFFLE:
		for (j = 0; j < tool_flow; j++)
		{
			rx = x - ts2 + rand() % tool_size;
			ry = y - ts2 + rand() % tool_size;
			sx = x - ts2 + rand() % tool_size;
			sy = y - ts2 + rand() % tool_size;
			IF_IN_RANGE(rx, ry) IF_IN_RANGE(sx, sy)
			{
		/* !!! Or do something for partial mask too? !!! */
				if (pixel_protected(rx, ry) ||
					pixel_protected(sx, sy))
					continue;
				off1 = rx + ry * mem_width;
				off2 = sx + sy * mem_width;
				if ((mem_channel == CHN_IMAGE) &&
					RGBA_mode && mem_img[CHN_ALPHA])
				{
					px = mem_img[CHN_ALPHA][off1];
					py = mem_img[CHN_ALPHA][off2];
					mem_img[CHN_ALPHA][off1] = py;
					mem_img[CHN_ALPHA][off2] = px;
				}
				k = MEM_BPP;
				off1 *= k; off2 *= k;
				for (i = 0; i < k; i++)
				{
					px = mem_img[mem_channel][off1];
					py = mem_img[mem_channel][off2];
					mem_img[mem_channel][off1++] = py;
					mem_img[mem_channel][off2++] = px;
				}
			}
		}
		break;
	case TOOL_SMUDGE:
		if (first_point) return (FALSE);
		mem_smudge(ox, oy, x, y);
		break;
	default: return (FALSE); /* Stop this nonsense now! */
	}

	/* Accumulate update info */
	if (minx < update[0]) update[0] = minx;
	if (miny < update[1]) update[1] = miny;
	xw += minx; yh += miny;
	if (xw > update[2]) update[2] = xw;
	if (yh > update[3]) update[3] = yh;

	return (TRUE);
}

void line_to_gradient()
{
	if (STROKE_GRADIENT)
	{
		grad_info *grad = gradient + mem_channel;
		grad->gmode = GRAD_MODE_LINEAR;
		grad->status = GRAD_DONE;
		copy4(grad->xy, line_xy);
		grad_update(grad);
	}
}

void do_tool_action(int cmd, int x, int y, int pressure)
{
	static double lstep;
	linedata ncline;
	double len1;
	int update_area[4];
	int minx = -1, miny = -1, xw = -1, yh = -1;
	tool_info o_tool = tool_state;
	int i, j, k, ts2, tr2, ox, oy;
	int oox, ooy;	// Continuous smudge stuff
	int first_point, pswap = FALSE;

	// Only do something with a new point
	if (!(first_point = !pen_down) && (cmd & TCF_ONCE) &&
		(tool_ox == x) && (tool_oy == y)) return;

	// Apply pressure
	if ((cmd & TCF_PRES) && (tablet_working || script_cmds))
	{
// !!! Later maybe switch the calculations to integer
		double p = pressure <= (MAX_PRESSURE * 2 / 10) ? -1.0 :
			(pressure - MAX_PRESSURE) * (10.0 / (8 * MAX_PRESSURE));
		p /= MAX_TF;

		for (i = 0; i < 3; i++)
		{
			if (!tablet_tool_use[i]) continue;
			tool_state.var[i] *= (tablet_tool_factor[i] > 0) +
				tablet_tool_factor[i] * p;
			if (tool_state.var[i] < 1) tool_state.var[i] = 1;
		}
	}

	ts2 = tool_size >> 1;
	tr2 = tool_size - ts2 - 1;

	switch (cmd &= TC_OPMASK)
	{
	case TC_LINE_START: case TC_LINE_ARROW:
		line_x2 = x;
		line_y2 = y;
		if (line_status == LINE_NONE)
		{
			line_x1 = x;
			line_y1 = y;
		}

		// Draw circle at x, y
		if (line_status == LINE_LINE)
		{
			grad_info svgrad = gradient[mem_channel];

			/* If not called from draw_arrow() */
			if (cmd != TC_LINE_ARROW) line_to_gradient();

			mem_undo_next(UNDO_TOOL);
			if (tool_size > 1)
			{
				int oldmode = mem_undo_opacity;
				mem_undo_opacity = TRUE;
				f_circle(line_x1, line_y1, tool_size);
				f_circle(line_x2, line_y2, tool_size);
				// Draw tool_size thickness line from 1-2
				tline(line_x1, line_y1, line_x2, line_y2, tool_size);
				mem_undo_opacity = oldmode;
			}
			else sline(line_x1, line_y1, line_x2, line_y2);

			minx = (line_x1 < line_x2 ? line_x1 : line_x2) - ts2;
			miny = (line_y1 < line_y2 ? line_y1 : line_y2) - ts2;
			xw = abs( line_x2 - line_x1 ) + 1 + tool_size;
			yh = abs( line_y2 - line_y1 ) + 1 + tool_size;

			line_x1 = line_x2;
			line_y1 = line_y2;

			gradient[mem_channel] = svgrad;
		}
		line_status = LINE_START;
		break;
	case TC_LINE_NEXT:
		line_status = LINE_LINE;
		repaint_line(NULL);
		break;
	case TC_LINE_STOP:
		stop_line();
		break;
	case TC_SEL_CLEAR:
		pressed_select(FALSE);
		break;
	case TC_SEL_START:
	case TC_SEL_SET_0: case TC_SEL_SET_1:
	case TC_SEL_SET_2: case TC_SEL_SET_3:
		if (cmd == TC_SEL_START)
		{
			marq_x1 = x;
			marq_y1 = y;
		}
		else
		{
			paint_marquee(MARQ_HIDE, 0, 0, NULL);
			i = cmd - TC_SEL_SET_0;
			if (!(i & 1) ^ (marq_x1 > marq_x2))
				marq_x1 = marq_x2;
			if (!(i & 2) ^ (marq_y1 > marq_y2))
				marq_y1 = marq_y2;
			set_cursor(NULL);
		}
		marq_x2 = x;
		marq_y2 = y;
		marq_status = MARQUEE_SELECTING;
		paint_marquee(MARQ_SNAP, 0, 0, NULL);
		break;
	case TC_SEL_TO:
		paint_marquee(MARQ_SIZE, x, y, NULL);
		break;
	case TC_SEL_STOP:
		marq_status = marq_status == MARQUEE_PASTE_DRAG ? MARQUEE_PASTE :
			MARQUEE_DONE;
		cursor_corner = -1;
// !!! Here fallthrough to setting cursor
		break;
	case TC_POLY_START: case TC_POLY_START_D:
		poly_status = cmd == TC_POLY_START ? POLY_SELECTING : POLY_DRAGGING;
		// Fallthrough
	case TC_POLY_ADD:
		poly_add_po(x, y);
		break;
	case TC_POLY_DEL:
		poly_delete_po(x, y);
		break;
	case TC_POLY_CLOSE:
		poly_conclude();
		break;
	case TC_PASTE_PSWAP:
		pswap = TRUE;
		// Fallthrough
	case TC_PASTE_DRAG:
	case TC_PASTE_PAINT:
		if (marq_status != MARQUEE_PASTE_DRAG)
		{
			marq_status = MARQUEE_PASTE_DRAG;
			marq_drag_x = x - marq_x1;
			marq_drag_y = y - marq_y1;
		}
		else paint_marquee(MARQ_MOVE, x - marq_drag_x, y - marq_drag_y, NULL);
		if (cmd == TC_PASTE_DRAG) break;
		cmd = TC_PASTE_COMMIT;
		// Fallthrough
	case TC_PAINT_B:
		if (cmd == TC_PAINT_B)
		{
			tint_mode[2] = 3; /* Swap tint +/- */
			if (first_point && !tint_mode[0])
			{
				col_reverse = TRUE;
				mem_swap_cols(FALSE);
			}
		}
		// Fallthrough
	case TC_PAINT: case TC_PASTE_COMMIT:
		// Update stroke gradient
		if (STROKE_GRADIENT) grad_stroke(x, y);

		/* Handle floodfill here, as too irregular a non-continuous tool */
		if (tool_type == TOOL_FLOOD)
		{
			/* Non-masked start point */
			if (pixel_protected(x, y) < 255)
			{
				j = get_pixel(x, y);
				k = mem_channel != CHN_IMAGE ? channel_col_A[mem_channel] :
					mem_img_bpp == 1 ? mem_col_A : PNG_2_INT(mem_col_A24);
				if (j != k) /* And never start on colour A */
				{
					spot_undo(UNDO_TOOL);
					flood_fill(x, y, j);
					// All pixels could change
					minx = miny = 0;
					xw = mem_width;
					yh = mem_height;
				}
			}
			/* Undo the color swap if fill failed */
			if (!pen_down && col_reverse)
			{
				col_reverse = FALSE;
				mem_swap_cols(FALSE);
			}
			break;
		}

		// Relativize source coords if that isn't yet done
		if (tool_type == TOOL_CLONE)
		{
			if ((clone_status == CLONE_ABS) ||
				(clone_status == (CLONE_REL | CLONE_TRACK)))
				clone_dx = clone_x - x , clone_dy = clone_y - y;
			clone_status = (clone_status & CLONE_ABS) | CLONE_DRAG;
		}

		// Do memory stuff for undo
		mem_undo_next(UNDO_TOOL);	

		/* Handle continuous mode */
		if (mem_continuous && !first_point)
		{
			minx = tool_ox < x ? tool_ox : x;
			xw = (tool_ox > x ? tool_ox : x) - minx + tool_size;
			minx -= ts2;

			miny = tool_oy < y ? tool_oy : y;
			yh = (tool_oy > y ? tool_oy : y) - miny + tool_size;
			miny -= ts2;

			if ((tool_type == TOOL_CLONE) ||
				(ts2 ? tool_type == TOOL_SQUARE : tool_type < TOOL_SPRAY))
			{
				rec_continuous(x, y, tool_size, tool_size);
				break;
			}
			if (tool_type == TOOL_CIRCLE)
			{
				/* Redraw stroke gradient in proper direction */
				if (STROKE_GRADIENT)
					f_circle(tool_ox, tool_oy, tool_size);
				tline(tool_ox, tool_oy, x, y, tool_size);
				f_circle(x, y, tool_size);
				break;
			}
			if (tool_type == TOOL_HORIZONTAL)
			{
				miny += ts2; yh -= tool_size - 1;
				rec_continuous(x, y, tool_size, 1);
				break;
			}
			if (tool_type == TOOL_VERTICAL)
			{
				minx += ts2; xw -= tool_size - 1;
				rec_continuous(x, y, 1, tool_size);
				break;
			}
			if (tool_type == TOOL_SLASH)
			{
				g_para(x + tr2, y - ts2, x - ts2, y + tr2,
					tool_ox - x, tool_oy - y);
				break;
			}
			if (tool_type == TOOL_BACKSLASH)
			{
				g_para(x - ts2, y - ts2, x + tr2, y + tr2,
					tool_ox - x, tool_oy - y);
				break;
			}
			if (tool_type == TOOL_SMUDGE)
			{
				linedata line;

				line_init(line, tool_ox, tool_oy, x, y);
				while (TRUE)
				{
					oox = line[0];
					ooy = line[1];
					if (line_step(line) < 0) break;
					mem_smudge(oox, ooy, line[0], line[1]);
				}
				break;
			}
			xw = yh = -1; /* Nothing was done */
		}

		/* Handle non-continuous mode & tools */
		update_area[0] = update_area[1] = MAX_WIDTH;
		update_area[2] = update_area[3] = 0;

		if (first_point) lstep = 0.0;

		if (first_point || !brush_spacing) /* Single point */
		{
			if (cmd == TC_PASTE_COMMIT)
				commit_paste(pswap, update_area); // At marquee
			else tool_draw(x, y, first_point, update_area);
		}
		else /* Multiple points */
		{
			/* Use marquee coords for paste */
			i = j = 0;
			ox = marq_x1;
			oy = marq_y1;
			if (cmd == TC_PASTE_COMMIT)
			{
				i = marq_x1 - x;
				j = marq_y1 - y;
			}

			line_init(ncline, tool_ox + i, tool_oy + j, x + i, y + j);
			i = abs(x - tool_ox);
			j = abs(y - tool_oy);
			len1 = sqrt(i * i + j * j) / (i > j ? i : j);
			
			while (TRUE)
			{
				if (lstep + (1.0 / 65536.0) >= brush_spacing)
				{
					/* Drop error for 1-pixel step */
					lstep = brush_spacing == 1 ? 0.0 :
						lstep - brush_spacing;
					if (cmd == TC_PASTE_COMMIT)
					{
						/* Adjust paste location */
						marq_x2 += ncline[0] - marq_x1;
						marq_y2 += ncline[1] - marq_y1;
						marq_x1 = ncline[0];
						marq_y1 = ncline[1];
						commit_paste(pswap, update_area);
					}
					else if (!tool_draw(ncline[0], ncline[1],
						first_point, update_area)) break;
				}
				if (line_step(ncline) < 0) break;
				lstep += len1;
			}
			marq_x2 += ox - marq_x1;
			marq_y2 += oy - marq_y1;
			marq_x1 = ox;
			marq_y1 = oy;
		}

		/* Convert update limits */
		minx = update_area[0];
		miny = update_area[1];
		xw = update_area[2] - minx;
		yh = update_area[3] - miny;
		break;
	}

	if ((xw > 0) && (yh > 0)) /* Some drawing action */
	{
		if (xw + minx > mem_width) xw = mem_width - minx;
		if (yh + miny > mem_height) yh = mem_height - miny;
		if (minx < 0) xw += minx , minx = 0;
		if (miny < 0) yh += miny , miny = 0;

		if ((xw > 0) && (yh > 0))
		{
			main_update_area(minx, miny, xw, yh);
			vw_update_area(minx, miny, xw, yh);
		}
	}
	tool_ox = x;	// Remember the coords just used as they are needed in continuous mode
	tool_oy = y;

	tint_mode[2] = 0; /* Default */
	if (tablet_working || script_cmds) tool_state = o_tool;
}

int tool_action(int count, int button, int x, int y)
{
	int cmd = TC_NONE;

	/* Handle "exceptional" tools */
	if (tool_type == TOOL_LINE)
// !!! Is pressure sensitivity even useful here, or is it a misfeature?
		cmd = button == 1 ? TC_LINE_START | TCF_PRES : TC_LINE_STOP;
	else if ((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON))
	{
		// User wants to drag the paste box
		if ((button == 1) || (button == 13) || (button == 2))
		{
			if ((marq_status == MARQUEE_PASTE_DRAG) ||
				((marq_status == MARQUEE_PASTE) &&
				(x >= marq_x1) && (x <= marq_x2) &&
				(y >= marq_y1) && (y <= marq_y2)))
				cmd = TC_PASTE_DRAG;
		}
		// User wants to commit the paste
		if (((marq_status == MARQUEE_PASTE_DRAG) || (marq_status == MARQUEE_PASTE)) &&
			(((button == 3) && (count == 1)) || ((button == 13) && !count)))
			cmd = cmd == TC_PASTE_DRAG ? TC_PASTE_PAINT | TCF_PRES :
				TC_PASTE_COMMIT | TCF_PRES;

		if ((tool_type == TOOL_SELECT) && (button == 3) && (marq_status == MARQUEE_DONE))
			cmd = TC_SEL_CLEAR;
		if ((tool_type == TOOL_SELECT) && (button == 1) &&
			((marq_status == MARQUEE_NONE) || (marq_status == MARQUEE_DONE)))
			// Starting a selection
		{
			if (marq_status == MARQUEE_DONE)
				cmd = TC_SEL_SET_0 + close_to(x, y);
			else cmd = TC_SEL_START;
		}
		else
		{
			if (marq_status == MARQUEE_SELECTING)
				cmd = TC_SEL_TO; // Continuing to make a selection
		}

		if (tool_type == TOOL_POLYGON)
		{
			if ((poly_status == POLY_NONE) && (marq_status == MARQUEE_NONE))
			{
				// Start doing something
				if (button == 1) cmd = TC_POLY_START;
				else if (button) cmd = TC_POLY_START_D;
			}
			if (poly_status == POLY_SELECTING)
			{
				/* Add another point to polygon */
				if (button == 1) cmd = TC_POLY_ADD;
				/* Stop adding points */
				else if (button == 3) cmd = TC_POLY_CLOSE;
			}
			// Add point to polygon
			if (poly_status == POLY_DRAGGING) cmd = TC_POLY_ADD;
		}
	}
	else /* Some other kind of tool */
	{
		if (button == 1) cmd = TC_PAINT | TCF_PRES;
		/* Does tool draw with color B when right button pressed? */
		else if ((button == 3) && ((tool_type <= TOOL_SPRAY) ||
			(tool_type == TOOL_FLOOD))) cmd = TC_PAINT_B | TCF_PRES;
	}
	return (cmd);
}

void check_marquee()	// Check marquee boundaries are OK - may be outside limits via arrow keys
{
	if (marq_status >= MARQUEE_PASTE)
	{
		marq_x1 = marq_x1 < 1 - mem_clip_w ? 1 - mem_clip_w :
			marq_x1 > mem_width - 1 ? mem_width - 1 : marq_x1;
		marq_y1 = marq_y1 < 1 - mem_clip_h ? 1 - mem_clip_h :
			marq_y1 > mem_height - 1 ? mem_height - 1 : marq_y1;
		marq_x2 = marq_x1 + mem_clip_w - 1;
		marq_y2 = marq_y1 + mem_clip_h - 1;
		return;
	}
	/* Reinit marquee from polygon bounds */
	if (poly_status == POLY_DONE)
		copy4(marq_xy, poly_xy);
	else if (marq_status == MARQUEE_NONE) return;
	/* Selection mode in operation */
	marq_x1 = marq_x1 < 0 ? 0 : marq_x1 >= mem_width ? mem_width - 1 : marq_x1;
	marq_x2 = marq_x2 < 0 ? 0 : marq_x2 >= mem_width ? mem_width - 1 : marq_x2;
	marq_y1 = marq_y1 < 0 ? 0 : marq_y1 >= mem_height ? mem_height - 1 : marq_y1;
	marq_y2 = marq_y2 < 0 ? 0 : marq_y2 >= mem_height ? mem_height - 1 : marq_y2;
}

void paint_poly_marquee(rgbcontext *ctx)	// Paint polygon marquee
{
	if ((tool_type != TOOL_POLYGON) || !poly_points) return;
// !!! Maybe check boundary clipping too
	poly_update_cache();
	draw_poly(poly_cache.xy, poly_cache.points, 0, margin_main_x, margin_main_y, ctx);
}


static void repaint_clipped(int x0, int y0, int x1, int y1, const int *vxy)
{
	int rxy[4];

	if (clip(rxy, x0, y0, x1, y1, vxy))
		repaint_canvas(margin_main_x + rxy[0], margin_main_y + rxy[1],
			rxy[2] - rxy[0], rxy[3] - rxy[1]);
}

void marquee_at(int *rect)			// Read marquee location & size
{
	rect[0] = marq_x1 < marq_x2 ? marq_x1 : marq_x2;
	rect[1] = marq_y1 < marq_y2 ? marq_y1 : marq_y2;
	rect[2] = abs(marq_x2 - marq_x1) + 1;
	rect[3] = abs(marq_y2 - marq_y1) + 1;
}

static void locate_marquee(int *xy, int snap)
{
	int rxy[4];
	int x1, y1, x2, y2, w, h, zoom = 1, scale = 1;

	if (snap && tgrid_snap)
	{
		copy4(rxy, marq_xy);
		snap_xy(marq_xy);
		if (marq_status < MARQUEE_PASTE)
		{
			snap_xy(marq_xy + 2);
			marq_xy[(rxy[2] >= rxy[0]) * 2 + 0] += tgrid_dx - 1;
			marq_xy[(rxy[3] >= rxy[1]) * 2 + 1] += tgrid_dy - 1;
		}
	}
	check_marquee();

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	/* Get onscreen coords */
	x1 = floor_div(marq_x1 * scale, zoom);
	y1 = floor_div(marq_y1 * scale, zoom);
	x2 = floor_div(marq_x2 * scale, zoom);
	y2 = floor_div(marq_y2 * scale, zoom);
	w = abs(x2 - x1) + scale;
	h = abs(y2 - y1) + scale;
	xy[2] = (xy[0] = x1 < x2 ? x1 : x2) + w;
	xy[3] = (xy[1] = y1 < y2 ? y1 : y2) + h;
}


void paint_marquee(int action, int new_x, int new_y, rgbcontext *ctx)
{
	int xy[4], vxy[4], nxy[4], rxy[4], clips[4 * 3];
	int i, nc, rgb, rw, rh, offx, offy, wx, wy, mst = marq_status;


	vxy[0] = vxy[1] = 0;
	canvas_size(vxy + 2, vxy + 3);

	locate_marquee(xy, action == MARQ_SNAP);
	copy4(nxy, xy);
	copy4(clips, xy);
	nc = action < MARQ_HIDE ? 0 : 4; // No clear if showing anew

	/* Determine which parts moved outside */
	while (action >= MARQ_MOVE)
	{
		if (action == MARQ_MOVE) // Move
		{
			marq_x2 += new_x - marq_x1;
			marq_x1 = new_x;
			marq_y2 += new_y - marq_y1;
			marq_y1 = new_y;
		}
		else marq_x2 = new_x , marq_y2 = new_y; // Resize
		locate_marquee(nxy, TRUE);

		/* No intersection? */
		if (!clip(rxy, xy[0], xy[1], xy[2], xy[3], nxy)) break;

		/* Horizontal slab */
		if (rxy[1] > xy[1]) clips[3] = rxy[1]; // Top
		else if (rxy[3] < xy[3]) clips[1] = rxy[3]; // Bottom
		else nc = 0; // None

		/* Inside area, if left unfilled */
		if (!(show_paste && (mst >= MARQUEE_PASTE)))
		{
			clips[nc + 0] = nxy[0] + 1;
			clips[nc + 1] = nxy[1] + 1;
			clips[nc + 2] = nxy[2] - 1;
			clips[nc + 3] = nxy[3] - 1;
			nc += 4;
		}

		/* Vertical block */
		if (rxy[0] > xy[0]) // Left
			clips[nc + 0] = xy[0] , clips[nc + 2] = rxy[0];
		else if (rxy[2] < xy[2]) // Right
			clips[nc + 0] = rxy[2] , clips[nc + 2] = xy[2];
		else break; // None
		clips[nc + 1] = rxy[1]; clips[nc + 3] = rxy[3];
		nc += 4;
		break;
	}

	/* Clear - only happens in void context */
	marq_status = 0;
	for (i = 0; i < nc; i += 4)
	{
		/* Clip to visible portion */
		if (!clip(rxy, clips[i + 0], clips[i + 1],
			clips[i + 2], clips[i + 3], vxy)) continue;
		/* Redraw entire area */
		if (show_paste && (mst >= MARQUEE_PASTE))
			repaint_clipped(xy[0], xy[1], xy[2], xy[3], rxy);
		/* Redraw only borders themselves */
		else
		{
			repaint_clipped(xy[0], xy[1] + 1, xy[0] + 1, xy[3] - 1, rxy);
			repaint_clipped(xy[2] - 1, xy[1] + 1, xy[2], xy[3] - 1, rxy);
			repaint_clipped(xy[0], xy[1], xy[2], xy[1] + 1, rxy);
			repaint_clipped(xy[0], xy[3] - 1, xy[2], xy[3], rxy);
		}
	}
	marq_status = mst;
	if (action == MARQ_HIDE) return; // All done for clear

	/* Determine visible area */
	if (ctx) clip(vxy, ctx->xy[0] - margin_main_x, ctx->xy[1] - margin_main_y,
		ctx->xy[2] - margin_main_x, ctx->xy[3] - margin_main_y, vxy);
	if (!clip(rxy, nxy[0], nxy[1], nxy[2], nxy[3], vxy)) return;

	/* Draw */
	rgb = RGB_2_INT(255, 0, 0); /* Draw in red */
	if (marq_status >= MARQUEE_PASTE)
	{
		/* Display paste RGB, only if not being called from repaint_canvas */
		if (show_paste && !ctx) repaint_clipped(nxy[0] + 1, nxy[1] + 1,
			nxy[2] - 1, nxy[3] - 1, vxy);
		rgb = RGB_2_INT(0, 0, 255); /* Draw in blue */
	}

	rw = rxy[2] - rxy[0];
	rh = rxy[3] - rxy[1];
	wx = margin_main_x + rxy[0];
	wy = margin_main_y + rxy[1];
	offx = rxy[0] - nxy[0];
	offy = rxy[1] - nxy[1];

	if ((nxy[0] >= rxy[0]) && (marq_x1 >= 0) && (marq_x2 >= 0))
		draw_dash(rgb, RGB_2_INT(255, 255, 255), offy,
			wx, wy, 1, rh, ctx);

	if ((nxy[2] <= rxy[2]) && (marq_x1 < mem_width) && (marq_x2 < mem_width))
		draw_dash(rgb, RGB_2_INT(255, 255, 255), offy,
			wx + rw - 1, wy, 1, rh, ctx);

	if ((nxy[1] >= rxy[1]) && (marq_y1 >= 0) && (marq_y2 >= 0))
		draw_dash(rgb, RGB_2_INT(255, 255, 255), offx,
			wx, wy, rw, 1, ctx);

	if ((nxy[3] <= rxy[3]) && (marq_y1 < mem_height) && (marq_y2 < mem_height))
		draw_dash(rgb, RGB_2_INT(255, 255, 255), offx,
			wx, wy + rh - 1, rw, 1, ctx);
}


int close_to( int x1, int y1 )		// Which corner of selection is coordinate closest to?
{
	return ((x1 + x1 <= marq_x1 + marq_x2 ? 0 : 1) +
		(y1 + y1 <= marq_y1 + marq_y2 ? 0 : 2));
}


static void line_get_xy(int xy[4], linedata line, int y1)
{
	int k, x0, x1, y;

	xy[1] = y = line[1];
	x0 = x1 = line[0];
	while ((line[1] < y1) && (line_step(line) >= 0))
		x1 = line[0] , y = line[1];
	xy[3] = y;
	k = (x0 > x1) * 2; xy[k] = x0; xy[k ^ 2] = x1;
}

static void repaint_xy(int xy[4], int scale)
{
	repaint_canvas(margin_main_x + xy[0] * scale, margin_main_y + xy[1] * scale,
		(xy[2] - xy[0] + 1) * scale, (xy[3] - xy[1] + 1) * scale);
}

// !!! For now, this function is hardcoded to merge 2 areas
static int merge_xy(int cnt, int *xy, int step)
{
	if ((xy[0 + 0] > xy[4 + 2] + step + 1) ||
		(xy[4 + 0] > xy[0 + 2] + step + 1)) return (2);
	if (xy[0 + 0] > xy[4 + 0]) xy[0 + 0] = xy[4 + 0];
	if (xy[0 + 1] > xy[4 + 1]) xy[0 + 1] = xy[4 + 1];
	if (xy[0 + 2] < xy[4 + 2]) xy[0 + 2] = xy[4 + 2];
	if (xy[0 + 3] < xy[4 + 3]) xy[0 + 3] = xy[4 + 3];
	return (1);
}

/* Only 2 line-quads for now, but will be extended to 4 for line-join drag */
static void refresh_lines(const int xy0[4], const int xy1[4])
{
	linedata ll1, ll2;
	int ixy[4], getxy[8], *lines[2] = { ll1, ll2 };
	int i, j, y, y1, y2, cnt, step, zoom = 1, scale = 1;

	if (cmd_mode) return;
	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	// !!! Could use CSCROLL_XYSIZE here instead
	cmd_peekv(drawing_canvas, ixy, sizeof(ixy), CANVAS_VPORT);
	prepare_line_clip(ixy, ixy, scale);

	for (i = j = 0; j < 2; j++)
	{
		const int *xy;
		int tmp;

		xy = j ? xy1 : xy0;
		if (!xy) continue;
		line_init(lines[i],
			floor_div(xy[0], zoom), floor_div(xy[1], zoom),
			floor_div(xy[2], zoom), floor_div(xy[3], zoom));
		if (line_clip(lines[i], ixy, &tmp) < 0) continue;
		if (lines[i][9] < 0) line_flip(lines[i]);
		i++;
	}

	step = scale < 8 ? (16 + scale - 1) / scale : 2;
	for (cnt = i , y1 = ixy[1]; cnt; y1 += step)
	{
		y2 = ixy[3] + 1;
		for (j = i = 0; i < cnt; i++)
		{
			y = lines[i][1];
			if (lines[i][2] < 0) // Remove used-up line
			{
				lines[i--] = lines[--cnt];
			}
			else if (y >= y1) // Remember not-yet-started line
			{
				if (y2 > y) y2 = y;
			}
			else line_get_xy(getxy + j++ * 4, lines[i], y1);
		}
		if (j)
		{
			if (j > 1) j = merge_xy(j, getxy, step);
			for (i = 0; i < j; i++)
				repaint_xy(getxy + i * 4, scale);
		}
		else y1 = y2;
	}
}

static void render_line(int mode, linedata line, int ofs, rgbcontext *ctx)
{
	int rxy[4], cxy[4];
	int i, j, x, y, tx, ty, w3, rgb = 0, scale = 1;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom > 1.0) scale = rint(can_zoom);

	copy4(cxy, ctx->xy);
	w3 = (cxy[2] - cxy[0]) * 3;
	for (i = ofs; line[2] >= 0; line_step(line) , i++)
	{
		x = (tx = line[0]) * scale + margin_main_x;
		y = (ty = line[1]) * scale + margin_main_y;

		if (mode == 1) /* Drawing */
		{
			j = ((ty & 7) * 8 + (tx & 7)) * 3;
			rgb = MEM_2_INT(mem_col_pat24, j);
		}
		else if (mode == 2) /* Tracking */
		{
			rgb = ((i >> 2) & 1) * 0xFFFFFF;
		}
		else if (mode == 3) /* Gradient */
		{
			rgb = ((i >> 2) & 1) * 0xFFFFFF ^
				((i >> 1) & 1) * 0x00FF00;
		}
		if (clip(rxy, x, y, x + scale, y + scale, cxy))
		{
			unsigned char *dest, *tmp;
			int i, h, l;

			tmp = dest = ctx->rgb + (rxy[1] - cxy[1]) * w3 +
				(rxy[0] - cxy[0]) * 3;
			*tmp++ = INT_2_R(rgb);
			*tmp++ = INT_2_G(rgb);
			*tmp++ = INT_2_B(rgb);
			l = (rxy[2] - rxy[0]) * 3;
			for (i = l - 3; i; i-- , tmp++)
				*tmp = *(tmp - 3);
			tmp = dest;
			for (h = rxy[3] - rxy[1] - 1; h; h--)
			{
				tmp += w3;
				memcpy(tmp, dest, l);
			}
		}
	}
}

void repaint_grad(const int *old)
{
	refresh_lines(gradient[mem_channel].xy, old);
}

void repaint_line(const int *old)
{
	refresh_lines(line_xy, old);
}

/* lxy is ctx's bounds scaled to "line space" (unchanged for zoom < 0, image
 * coordinates otherwise) */
void refresh_line(int mode, const int *lxy, rgbcontext *ctx)
{
	linedata line;
	int j, zoom = 1, *xy;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);

	xy = mode == 3 ? gradient[mem_channel].xy : line_xy;
	line_init(line, floor_div(xy[0], zoom), floor_div(xy[1], zoom),
		floor_div(xy[2], zoom), floor_div(xy[3], zoom));
	if (line_clip(line, lxy, &j) >= 0) render_line(mode, line, j, ctx);
}

void update_recent_files()			// Update the menu items
{
	char txt[64], *t, txt2[PATHTXT];
	int i, count = 0;

	for (i = 0; i < recent_files; i++)	// Display recent filenames
	{
		sprintf(txt, "file%i", i + 1);

		t = inifile_get(txt, "");
		if (t[0])
		{
			gtkuncpy(txt2, t, PATHTXT);
			cmd_setv(menu_slots[MENU_RECENT1 + i], txt2, LABEL_VALUE);
			count++;
		}
		cmd_showhide(menu_slots[MENU_RECENT1 + i], t[0]); // Hide if empty
	}
	for (; i < MAX_RECENT; i++)		// Hide extra items
		cmd_showhide(menu_slots[MENU_RECENT1 + i], FALSE);

	// Hide separator if not needed
	cmd_showhide(menu_slots[MENU_RECENT_S], count);
}

void register_file( char *filename )		// Called after successful load/save
{
	char txt[64], txt1[64], *c;
	int i, f;

	c = strrchr( filename, DIR_SEP );
	if (c)
	{
		i = *c;
		*c = '\0';		// Strip off filename
		inifile_set("last_dir", filename);
		*c = i;
	}

	// Is it already in used file list?  If so shift relevant filenames down and put at top.
	i = 1;
	f = 0;
	while ( i<MAX_RECENT && f==0 )
	{
		sprintf( txt, "file%i", i );
		c = inifile_get(txt, "");
		if ( strcmp( filename, c ) == 0 ) f = 1;	// Filename found in list
		else i++;
	}
	if ( i>1 )			// If file is already most recent, do nothing
	{
		while ( i>1 )
		{
			sprintf( txt, "file%i", i-1 );
			sprintf( txt1, "file%i", i );
			inifile_set(txt1, inifile_get(txt, ""));

			i--;
		}
		inifile_set("file1", filename);		// Strip off filename
	}

	update_recent_files();
}

void create_default_image()			// Create default new image
{
	int	nw = inifile_get_gint32("lastnewWidth", DEFAULT_WIDTH ),
		nh = inifile_get_gint32("lastnewHeight", DEFAULT_HEIGHT ),
		nc = inifile_get_gint32("lastnewCols", 256 ),
		nt = inifile_get_gint32("lastnewType", 2 );

	do_new_one(nw, nh, nc, nt == 1 ? NULL : mem_pal_def,
		(nt == 0) || (nt > 2) ? 3 : 1, FALSE);
}

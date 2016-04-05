/*	viewer.c
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
#include "viewer.h"
#include "canvas.h"
#include "inifile.h"
#include "layer.h"
#include "channels.h"
#include "toolbar.h"
#include "font.h"
#include "thread.h"

int font_aa, font_bk, font_r;
int font_bkg, font_angle, font_align;
int font_setdpi, font_dpi, sys_dpi;

int view_showing;	// 0: hidden, 1: horizontal split, 2: vertical split
float vw_zoom = 1;

int opaque_view;


///	HELP WINDOW

#include "help.c"
#undef _
#define _(X) X

typedef struct {
	char *name, *help[HELP_PAGE_COUNT];
} help_dd;

static void click_help_end(help_dd *dt, void **wdata)
{
	// Make sure the user can only open 1 help window
	cmd_sensitive(menu_slots[MENU_HELP], TRUE);
	run_destroy(wdata);
}

#if HELP_PAGE_COUNT != 4
#error Wrong number of help pages defined
#endif

#define WBbase help_dd
static void *help_code[] = {
	HEIGHT(400), WINDOWp(name),
	DEFSIZE(600, 2),
	XVBOXbp(0, 4, 0), // originally the window was that way
	BORDER(NBOOK, 1),
	BORDER(FRAME, 10), /* BORDER(SCROLL, 0), */
	NBOOKl,
	CLEANUP(help[0]),
	PAGEvp(help_titles[0]),
	FSCROLL(0, 2), // never/always
	WIDTH(380), HLABELp(help[0]),
	WDONE, // page 0
	PAGEvp(help_titles[1]),
	FSCROLL(0, 2), // never/always
	WIDTH(380), HLABELmp(help[1]),
	WDONE, // page 1
	PAGEvp(help_titles[2]),
	FSCROLL(0, 2), // never/always
	WIDTH(380), HLABELmp(help[2]),
	WDONE, // page 2
	PAGEvp(help_titles[3]),
	FSCROLL(0, 2), // never/always
	WIDTH(380), HLABELp(help[3]),
	WDONE, // page 3
	WDONE, // nbook
	BORDER(BUTTON, 1),
	UDONEBTN(_("Close"), click_help_end),
	// !!! originally had GTK_CAN_DEFAULT set on button
	WDONE, // xvbox
	ONTOP0,
	WSHOW
};
#undef WBbase

void pressed_help()
{
	help_dd tdata;
	memx2 mem;
	char **tmp, txt[128];
	int i, j, ofs[HELP_PAGE_COUNT];

	// Make sure the user can only open 1 help help_window
	cmd_sensitive(menu_slots[MENU_HELP], FALSE);

	snprintf(txt, 120, "%s - %s", MT_VERSION, __("About"));
	tdata.name = txt;

	memset(&mem, 0, sizeof(mem));
	getmemx2(&mem, 4000); // default size
	for (i = 0; i < HELP_PAGE_COUNT; i++)
	{
		ofs[i] = mem.here;
		tmp = help_pages[i];
		for (j = 0; tmp[j]; j++)
		{
			addchars(&mem, '\n', 1);
			addstr(&mem, __(tmp[j]), 1);
		}
		addchars(&mem, '\0', 1);
	}
	for (i = 0; i < HELP_PAGE_COUNT; i++)
		tdata.help[i] = mem.buf + ofs[i];

	run_create(help_code, &tdata, sizeof(tdata));
}



///	PAN WINDOW

int max_pan;

typedef struct {
	int wh[3];
	int wait_h, wait_v, in_pan;
	unsigned char *rgb;
	void **img;
} pan_dd;

void draw_pan_thumb(pan_dd *dt, int x1, int y1, int x2, int y2)
{
	int i, j, k, ix, iy, zoom = 1, scale = 1;
	int pan_w = dt->wh[0], pan_h = dt->wh[1];
	unsigned char *dest, *src;

	/* Create thumbnail */
	dest = dt->rgb;
	for (i = 0; i < pan_h; i++)
	{
		iy = (i * mem_height) / pan_h;
		src = mem_img[CHN_IMAGE] + iy * mem_width * mem_img_bpp;
		if (mem_img_bpp == 3) /* RGB */
		{
			for (j = 0; j < pan_w; j++ , dest += 3)
			{
				ix = ((j * mem_width) / pan_w) * 3;
				dest[0] = src[ix + 0];
				dest[1] = src[ix + 1];
				dest[2] = src[ix + 2];
			}
		}
		else /* Indexed */
		{
			for (j = 0; j < pan_w; j++ , dest += 3)
			{
				ix = src[(j * mem_width) / pan_w];
				dest[0] = mem_pal[ix].red;
				dest[1] = mem_pal[ix].green;
				dest[2] = mem_pal[ix].blue;
			}
		}
	}


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	/* Canvas coords to image coords */
	x2 = ((x1 + x2) / scale) * zoom;
	y2 = ((y1 + y2) / scale) * zoom;
	x1 = (x1 / scale) * zoom;
	y1 = (y1 / scale) * zoom;
	x1 = x1 < 0 ? 0 : x1 >= mem_width ? mem_width - 1 : x1;
	x2 = x2 < 0 ? 0 : x2 >= mem_width ? mem_width - 1 : x2;
	y1 = y1 < 0 ? 0 : y1 >= mem_height ? mem_height - 1 : y1;
	y2 = y2 < 0 ? 0 : y2 >= mem_height ? mem_height - 1 : y2;

	/* Image coords to thumbnail coords */
	x1 = (x1 * pan_w) / mem_width;
	x2 = (x2 * pan_w) / mem_width;
	y1 = (y1 * pan_h) / mem_height;
	y2 = (y2 * pan_h) / mem_height;

	/* Draw the border */
	dest = src = dt->rgb + (y1 * pan_w + x1) * 3;
	j = y2 - y1;
	k = (x2 - x1) * 3;
	for (i = 0; i <= j; i++)
	{
		dest[k + 0] = dest[k + 1] = dest[k + 2] =
			dest[0] = dest[1] = dest[2] = ((i >> 2) & 1) * 255;
		dest += pan_w * 3;
	}
	j = x2 - x1;
	k = (y2 - y1) * pan_w * 3;
	for (i = 0; i <= j; i++)
	{
		src[k + 0] = src[k + 1] = src[k + 2] =
			src[0] = src[1] = src[2] = ((i >> 2) & 1) * 255;
		src += 3;
	}

	cmd_repaint(dt->img);
}

static void pan_thumbnail(pan_dd *dt)	// Create thumbnail and selection box
{
	int xyhv[4];

	// Update main window first to get new scroll positions if necessary
	handle_events();

	cmd_peekv(scrolledwindow_canvas, xyhv, sizeof(xyhv), CSCROLL_XYSIZE);
	draw_pan_thumb(dt, xyhv[0], xyhv[1], xyhv[2], xyhv[3]);
}

static void do_pan(pan_dd *dt, int w, int h, int nv_h, int nv_v)
{
	int mx[2], *xy;

	cmd_peekv(scrolledwindow_canvas, mx, sizeof(mx), CSCROLL_LIMITS);
	nv_h = nv_h < 0 ? 0 : nv_h > mx[0] ? mx[0] : nv_h;
	nv_v = nv_v < 0 ? 0 : nv_v > mx[1] ? mx[1] : nv_v;

	if (dt->in_pan) /* Delay reaction */
	{
		dt->wait_h = nv_h; dt->wait_v = nv_v;
		dt->in_pan |= 2;
		return;
	}

	xy = slot_data(scrolledwindow_canvas, NULL);
	while (TRUE)
	{
		dt->in_pan = 1;

		/* Update selection box */
		draw_pan_thumb(dt, nv_h, nv_v, w, h);

		/* Update position of main window scrollbars */
		xy[0] = nv_h;
		xy[1] = nv_v;
		cmd_reset(scrolledwindow_canvas, NULL);

		/* Process events */
		handle_events();
		if (dt->in_pan < 2) break;

		/* Do delayed update */
		nv_h = dt->wait_h;
		nv_v = dt->wait_v;
	}
	dt->in_pan = 0;
}

static int key_pan(pan_dd *dt, void **wdata, int what, void **where, key_ext *key)
{
	int nv_h, nv_v, hm, vm, xyhv[4];

	if (!check_zoom_keys_real(wtf_pressed(key)))
	{
		/* xine-ui sends bogus keypresses so don't delete on this */
		if (!arrow_key_(key->key, key->state, &hm, &vm, 4) &&
			!XINE_FAKERY(key->key)) run_destroy(wdata);
		else
		{
			cmd_peekv(scrolledwindow_canvas, xyhv, sizeof(xyhv), CSCROLL_XYSIZE);

			nv_h = xyhv[0] + hm * (xyhv[2] / 4);
			nv_v = xyhv[1] + vm * (xyhv[3] / 4);

			do_pan(dt, xyhv[2], xyhv[3], nv_h, nv_v);
		}
	}
	else pan_thumbnail(dt);	// Update selection box as user may have zoomed in/out

	return (TRUE);
}

static int pan_button(pan_dd *dt, void **wdata, int what, void **where,
	mouse_ext *mouse)
{
	int nv_h, nv_v, xyhv[4];
	float cent_x, cent_y;

	if (mouse->button == 1)	// Left click = pan window
	{
		cmd_peekv(scrolledwindow_canvas, xyhv, sizeof(xyhv), CSCROLL_XYSIZE);

		cent_x = ((float) mouse->x) / dt->wh[0];
		cent_y = ((float) mouse->y) / dt->wh[1];

		nv_h = mem_width * can_zoom * cent_x - xyhv[2] * 0.5;
		nv_v = mem_height * can_zoom * cent_y - xyhv[3] * 0.5;

		do_pan(dt, xyhv[2], xyhv[3], nv_h, nv_v);
	}
	else if ((mouse->button == 3) || (mouse->button == 13))
		run_destroy(wdata);	// Right click = kill window

	return (TRUE);
}

#define WBbase pan_dd
void *pan_code[] = {
	BORDER(POPUP, 2),
	WPMOUSE, POPUP(_("Pan Window")),
	EVENT(KEY, key_pan),
	TALLOC(rgb, wh[2]),
	REF(img), RGBIMAGE(rgb, wh),
	EVENT(MOUSE, pan_button), EVENT(MMOUSE, pan_button),
	WEND
};
#undef WBbase

void pressed_pan()
{
	pan_dd tdata;
	void **wdata;
	int pan_w, pan_h;


	pan_w = pan_h = max_pan;
	if (mem_width < mem_height) pan_w = (max_pan * mem_width) / mem_height;
	else pan_h = (max_pan * mem_height) / mem_width;
	if (pan_w < 1) pan_w = 1;
	if (pan_h < 1) pan_h = 1;

	tdata.wh[0] = pan_w;
	tdata.wh[1] = pan_h;
	tdata.wh[2] = pan_w * pan_h * 3;
	tdata.wait_h = tdata.wait_v = tdata.in_pan = 0;

	wdata = run_create(pan_code, &tdata, sizeof(tdata));
	if (!wdata) return; // can fail if no memory

	pan_thumbnail(GET_DDATA(wdata));

	cmd_showhide(wdata, TRUE);
}



////	VIEW WINDOW

static int vw_width = 1, vw_height = 1;
static int vw_last_x, vw_last_y, vw_move_layer;
static int vwxy[2];	// view window position
static int vw_mouse_status;
static void **vw_scrolledwindow;

void **vw_drawing;
int vw_focus_on;

size_t render_layers(unsigned char *rgb, int cxy[4], int pw, int zoom, int scale,
	int lr0, int lr1, int view)
{
	renderstate rs;
	int rxy[4], txy[4] = { cxy[2], cxy[3], cxy[0], cxy[1] };
	image_info *image;
	unsigned char *tmp, **img;
	int i, j, ii, jj, ll, wx0, wy0, wx1, wy1, xpm, opac;
	int dx, dy, ddx, ddy, mx, mw, my, mh;
	int px = cxy[0], py = cxy[1];
	size_t npix = 0, nrow = 0;

	/* Align view on background, canvas on image */
	dx = layer_table[0].x;
	dy = layer_table[0].y;
	if (!view)
	{
		dx = layer_table_p[layer_selected].x;
		dy = layer_table_p[layer_selected].y;
	}

	/* Clip to background if needed */
	if (view && ani_state)
	{
		image = layer_selected ? &layer_table[0].image->image_ : &mem_image;

		ddx = (layer_table[0].x - dx) * scale - 1;
		ddy = (layer_table[0].y - dy) * scale - 1;

		if (!clip(cxy, floor_div(ddx + zoom, zoom),
			floor_div(ddy + zoom, zoom),
			floor_div(ddx + image->width * scale, zoom) + 1,
			floor_div(ddy + image->height * scale, zoom) + 1,
			cxy)) return (0);
	}

	/* Get image-space bounds */
	wx0 = floor_div(cxy[0] * zoom, scale);
	wy0 = floor_div(cxy[1] * zoom, scale);
	wx1 = floor_div((cxy[2] - 1) * zoom, scale);
	wy1 = floor_div((cxy[3] - 1) * zoom, scale);

	for (ll = lr0; ll <= lr1; ll++)
	{
		layer_node *t = (view ? layer_table : layer_table_p) + ll;

		image = ll == layer_selected ? &mem_image : &t->image->image_;
		/* !!! When sizing canvas, do not skip selected layer */
		if (!t->visible && (view || (ll != layer_selected))) continue;
		i = t->x - dx;
		j = t->y - dy;
		ii = i + image->width;
		jj = j + image->height;
		if ((i > wx1) || (j > wy1) || (ii <= wx0) || (jj <= wy0))
			continue;
		if (!clip(rxy, ceil_div(i * scale, zoom),
			ceil_div(j * scale, zoom),
			floor_div(ii * scale - 1, zoom) + 1,
			floor_div(jj * scale - 1, zoom) + 1, cxy)) continue;

#ifdef U_THREADS
		if (!rgb) /* Calculating area & load */
		{
			int mw = rxy[2] - rxy[0], mh = rxy[3] - rxy[1];
			npix += mw * mh;
			nrow += mh;
			if (txy[0] > rxy[0]) txy[0] = rxy[0];
			if (txy[1] > rxy[1]) txy[1] = rxy[1];
			if (txy[2] < rxy[2]) txy[2] = rxy[2];
			if (txy[3] < rxy[3]) txy[3] = rxy[3];
			continue;
		}
#endif

		xpm = ll ? image->trans : -1; // above background
		opac = (t->opacity * 255 + 50) / 100;
		mw = rxy[2] - (mx = rxy[0]);
		setup_row(&rs, mx, mw, zoom, scale, image->width, xpm, opac,
			image->bpp, image->pal);
		mh = rxy[3] - (my = rxy[1]);
		tmp = rgb + (my - py) * pw + (mx - px) * 3;
		ddx = floor_div(mx * zoom, scale) - i;
		ddy = floor_div(my * zoom, scale) - j;

		i = my % scale;
		if (i < 0) i += scale;
		mh = mh * zoom + i;
		img = image->img;
		for (j = -1; i < mh; i += zoom , tmp += pw)
		{
			if ((i / scale == j) && !async_bk)
			{
				memcpy(tmp, tmp - pw, mw * 3);
				continue;
			}
			j = i / scale;
			render_row(&rs, tmp, img, ddx, ddy + j, NULL);
		}
	}

#ifdef U_THREADS
	if (rgb) return (0);

	/* Total work to do */
	if (async_bk) scale = 1;
	// Weight coefficients are empirical
	npix = npix / 6 + npix / (scale * scale) + (nrow / scale) * 75;
	copy4(cxy, txy);
#endif
	return (npix);
}

typedef struct {
	unsigned char *rgb;
	int cxy[4];
	int pw, zoom, scale;
	threaddata *tdata; // For simplicity
} lr_render_state;

#ifdef U_THREADS

static void do_layers_render(tcb *thread)
{
	lr_render_state *ls = thread->data;
	int rxy[4];
	int scale = ls->scale, h = thread->nsteps * scale;
	int y0, d, ofs = 0;

	copy4(rxy, ls->cxy);
	d = floor_mod(y0 = rxy[1], scale);
	if (thread->step0) rxy[1] = y0 += ofs = thread->step0 * scale - d;
	else h -= d;
	if (y0 + h < rxy[3]) rxy[3] = y0 + h;

	/* Always align on background layer */
	render_layers(ls->rgb + ofs * ls->pw, rxy, ls->pw, ls->zoom, scale,
		0, layers_total, TRUE);
}

#endif

void view_render_rgb( unsigned char *rgb, int px, int py, int pw, int ph, double czoom )
{
	lr_render_state ls = { rgb, { px, py, px + pw, py + ph }, pw * 3, 1, 1 };
	int tmp = overlay_alpha;
#ifdef U_THREADS
	int wh, nt, nt2;
	size_t vpix;
#endif

	if (!rgb) return; /* Paranoia */
	/* Control transparency separately */
	overlay_alpha = opaque_view;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (czoom < 1.0) ls.zoom = rint(1.0 / czoom);
	else ls.scale = rint(czoom);

#ifdef U_THREADS
	/* Calculate amount of work for threads */
	vpix = render_layers(NULL, ls.cxy, 0, ls.zoom, ls.scale,
		0, layers_total, TRUE);

	wh = xy_span(ls.cxy, ls.scale, 1);
	nt = image_threads(xy_span(ls.cxy, ls.scale, 0), wh);
	vpix /= 4; // !!! Heuristic weight; maybe 1/8 would be better?
	nt2 = ceil_div(vpix, kpix_threads * 1024);
	if (nt2 > nt) nt2 = nt;

	ls.rgb += ((ls.cxy[1] - py) * pw + ls.cxy[0] - px) * 3;

	ls.tdata = talloc(MA_SKIP_ZEROSIZE | MA_FLAG_NONE, nt2,
		&ls, sizeof(ls), NULL, NULL);

	if (ls.tdata && (ls.tdata != MEM_NONE)) // 2+ threads w/allocation
	{
		nt /= ls.tdata->count;
		if (nt > MAX_TH_STRIPS) nt = MAX_TH_STRIPS;
		ls.tdata->chunks = nt;
		ls.tdata->silent = TRUE;
		launch_threads(do_layers_render, ls.tdata, NULL, wh);
		free(ls.tdata);
	}
	else // No alloc - single thread
#endif
	{
		/* Always align on background layer */
		render_layers(ls.rgb, ls.cxy, ls.pw, ls.zoom, ls.scale,
			0, layers_total, TRUE);
	}
	overlay_alpha = tmp;
}

////	COMPOSITE ALPHA

int comp_need_alpha(int ftype)
{
	layer_node *t = layer_table;
	image_info *image = layer_selected ? &t->image->image_ : &mem_image;
	unsigned char *alpha = image->img[CHN_ALPHA];

	/* Format has to support alpha */
	ftype &= FTM_FTYPE;
	if ((ftype != FT_NONE) && !(file_formats[ftype].flags & FF_ALPHAR))
		return (FALSE);
	/* Background has to not be solid */
	return (!t->visible || (!opaque_view && ((t->opacity < 100) ||
		(alpha && !is_filled(alpha, 255, image->width * image->height)))));
}

void collect_alpha(unsigned char *alpha, int pw, int ph)
{
	int rxy[4], cxy[4] = { 0, 0, pw, ph };
	unsigned char buf[MAX_WIDTH];
	image_info *image;
	unsigned char *tmp, *src, **img;
	int i, j, k, ll, xpm, opac;
	int dx, dy, ddx, ddy, mx, mw, my, mh, loc;

	/* Align on background */
	dx = layer_table[0].x;
	dy = layer_table[0].y;

	memset(alpha, 0, pw * ph);
	for (ll = 0; ll <= layers_total; ll++)
	{
		layer_node *t = layer_table + ll;

		if (!t->visible) continue;
		i = t->x - dx;
		j = t->y - dy;
		image = ll == layer_selected ? &mem_image : &t->image->image_;
		if (!clip(rxy, i, j, i + image->width, j + image->height, cxy))
			continue;

		xpm = t != layer_table ? image->trans : -1; // above background
		if ((xpm > -1) && (image->bpp == 3))
			xpm = PNG_2_INT(image->pal[xpm]);
		opac = opaque_view ? 255 : (t->opacity * 255 + 50) / 100;
		mw = rxy[2] - (mx = rxy[0]);
		mh = rxy[3] - (my = rxy[1]);
		tmp = alpha + my * pw + mx;
		ddx = mx - i;
		ddy = my - j;

		img = image->img;
		for (i = 0; i < mh; i++ , tmp += pw)
		{
			loc = (ddy + i) * image->width + ddx;
			/* Prepare effective source alpha */
			if (!img[CHN_ALPHA] || opaque_view) memset(buf, 255, mw);
			else memcpy(buf, img[CHN_ALPHA] + loc, mw);
			if (opaque_view); // Forced opaque
			else if (image->bpp == 1) // Indexed
			{
				/* Force on-off transparency */
				for (j = 0; j < mw; j++)
					if (buf[j]) buf[j] = 255;
				/* Apply transparent color */
				if (xpm > -1)
				{
					src = img[CHN_IMAGE] + loc;
					for (j = 0; j < mw; j++)
						if (src[j] == xpm) buf[j] = 0;
				}
			}
			else if (xpm > -1) // RGB with transparent color
			{
				src = img[CHN_IMAGE] + loc * 3;
				for (j = 0; j < mw; j++)
					if (MEM_2_INT(src, j * 3) == xpm) buf[j] = 0;
			}

			/* Mix into destination */
			for (j = 0; j < mw; j++)
			{
				k = opac * buf[j];
				k = (k + (k >> 8) + 1) >> 8;
				k = (tmp[j] + k) * 255 - tmp[j] * k;
				tmp[j] = (k + (k >> 8) + 1) >> 8;
			}
		}
	}
}

static guint idle_focus;

void vw_focus_view()						// Focus view window to main window
{
	int w0, h0, xyhv[4], nv_hv[2] = { 0, 0 };
	float px, py, main_hv[2];

	if (idle_focus) gtk_idle_remove(idle_focus);
	idle_focus = 0;
	if (!view_showing) return;		// Bail out if not visible
	if (!vw_focus_on) return;		// Only focus if user wants to

	if (vw_mouse_status)	/* Dragging in progress - delay focus */
	{
		vw_mouse_status |= 2;
		return;
	}

	canvas_center(main_hv);

	/* If we are editing a layer above the background make adjustments */
	if (layers_total && layer_selected)
	{
		w0 = layer_table[0].image->image_.width;
		h0 = layer_table[0].image->image_.height;
		px = main_hv[0] * mem_width + layer_table[layer_selected].x -
			layer_table[0].x;
		py = main_hv[1] * mem_height + layer_table[layer_selected].y -
			layer_table[0].y;
		px = px < 0.0 ? 0.0 : px >= w0 ? w0 - 1 : px;
		py = py < 0.0 ? 0.0 : py >= h0 ? h0 - 1 : py;
		main_hv[0] = px / w0;
		main_hv[1] = py / h0;
	}

	cmd_peekv(vw_scrolledwindow, xyhv, sizeof(xyhv), CSCROLL_XYSIZE);

	if (xyhv[2] < vw_width)
	{
		nv_hv[0] = vw_width * main_hv[0] - xyhv[2] * 0.5;
		if (nv_hv[0] + xyhv[2] > vw_width)
			nv_hv[0] = vw_width - xyhv[2];
		if (nv_hv[0] < 0) nv_hv[0] = 0;
	}
	if (xyhv[3] < vw_height)
	{
		nv_hv[1] = vw_height * main_hv[1] - xyhv[3] * 0.5;
		if (nv_hv[1] + xyhv[3] > vw_height)
			nv_hv[1] = vw_height - xyhv[3];
		if (nv_hv[1] < 0) nv_hv[1] = 0;
	}

	/* Do nothing if nothing changed */
	if (!((xyhv[0] ^ nv_hv[0]) | (xyhv[1] ^ nv_hv[1]))) return;

	/* Update position of view window scrollbars */
	vwxy[0] = nv_hv[0];
	vwxy[1] = nv_hv[1];
	cmd_reset(vw_scrolledwindow, NULL);
}

void vw_focus_idle()
{
	if (idle_focus) return;
	if (!view_showing) return;
	if (!vw_focus_on) return;
	idle_focus = threads_idle_add_priority(GTK_PRIORITY_REDRAW + 5,
		(GtkFunction)vw_focus_view, NULL);
}

void vw_configure()
{
	int new_margin_x = 0, new_margin_y = 0;

	if (canvas_image_centre)
	{
		int wh[2];
		cmd_peekv(vw_drawing, wh, sizeof(wh), CANVAS_SIZE);
		if ((wh[0] -= vw_width) > 0) new_margin_x = wh[0] >> 1;
		if ((wh[1] -= vw_height) > 0) new_margin_y = wh[1] >> 1;
	}

	if ((new_margin_x != margin_view_x) || (new_margin_y != margin_view_y))
	{
		margin_view_x = new_margin_x;
		margin_view_y = new_margin_y;
		/* Force redraw of whole canvas as the margin has shifted */
		cmd_repaint(vw_drawing);
	}
	if (idle_focus) vw_focus_view(); // Time to refocus is NOW
}

void vw_align_size(float new_zoom)
{
	if (!view_showing) return;

	if (new_zoom < MIN_ZOOM) new_zoom = MIN_ZOOM;
	if (new_zoom > MAX_ZOOM) new_zoom = MAX_ZOOM;
	if (new_zoom == vw_zoom) return;

	vw_zoom = new_zoom;
	vw_realign();
	toolbar_zoom_update();	// View zoom doesn't get changed elsewhere
}

void vw_realign()
{
	int sw = mem_width, sh = mem_height, i;

	if (!view_showing || cmd_mode) return;

	if (layers_total && layer_selected)
	{
		sw = layer_table[0].image->image_.width;
		sh = layer_table[0].image->image_.height;
	}

	if (vw_zoom < 1.0)
	{
		i = rint(1.0 / vw_zoom);
		sw = (sw + i - 1) / i;
		sh = (sh + i - 1) / i;
	}
	else
	{
		i = rint(vw_zoom);
		sw *= i; sh *= i;
	}

	if ((vw_width != sw) || (vw_height != sh))
	{
		int wh[2] = { sw, sh };
		vw_width = sw;
		vw_height = sh;
		cmd_setv(vw_drawing, wh, CANVAS_SIZE);
	}
	/* !!! Let refocus wait a bit - if window is being resized, view pane's
	 * allocation could be not yet updated (canvas is done first) - WJ */
	vw_focus_idle();
}

static int vw_repaint(void *dt, void **wdata, int what, void **where,
	rgbcontext *ctx)
{
	unsigned char *rgb = ctx->rgb;
	int px, py, pw, ph;

	pw = ctx->xy[2] - (px = ctx->xy[0]);
	ph = ctx->xy[3] - (py = ctx->xy[1]);

	memset(rgb, mem_background, pw * ph * 3);
	view_render_rgb(rgb, px - margin_view_x, py - margin_view_y,
		pw, ph, vw_zoom);

	return (TRUE); // now draw this
}

void vw_update_area(int x, int y, int w, int h)	// Update x,y,w,h area of current image
{
	int zoom, scale, rxy[4];

	if (!view_showing) return;
	
	if (layer_selected)
	{
		x += layer_table[layer_selected].x - layer_table[0].x;
		y += layer_table[layer_selected].y - layer_table[0].y;
	}

	if (vw_zoom < 1.0)
	{
		zoom = rint(1.0 / vw_zoom);
		w += x;
		h += y;
		x = floor_div(x + zoom - 1, zoom);
		y = floor_div(y + zoom - 1, zoom);
		w = (w - x * zoom + zoom - 1) / zoom;
		h = (h - y * zoom + zoom - 1) / zoom;
		if ((w <= 0) || (h <= 0)) return;
	}
	else
	{
		scale = rint(vw_zoom);
		x *= scale;
		y *= scale;
		w *= scale;
		h *= scale;
	}

	rxy[2] = (rxy[0] = x + margin_view_x) + w;
	rxy[3] = (rxy[1] = y + margin_view_y) + h;
	cmd_setv(vw_drawing, rxy, CANVAS_REPAINT);
}


static int vw_mouse_event(void *dt, void **wdata, int what, void **where,
	mouse_ext *mouse)
{
	image_info *image;
	unsigned char *rgb, **img;
	int x, y, dx, dy, i, lx, ly, lw, lh, bpp, tpix, ppix, ofs;
	int pflag = mouse->count >= 0, zoom = 1, scale = 1;
	png_color *pal;

	/* Steal focus from dock window */
	if ((mouse->count > 0) && dock_focused())
		cmd_setv(main_window_, NULL, WINDOW_FOCUS);

	/* If cursor got warped, will have another movement event to handle */
	if (!mouse->count && mouse->button && cmd_checkv(where, MOUSE_BOUND))
		return (TRUE);

	i = vw_mouse_status;
	if (!pflag || !mouse->button || !layers_total)
	{
		vw_mouse_status = 0;
		if (i & 2) vw_focus_view(); /* Delayed focus event */
		return (pflag);
	}

	if (vw_zoom < 1.0) zoom = rint(1.0 / vw_zoom);
	else scale = rint(vw_zoom);

	dx = vw_last_x;
	dy = vw_last_y;
	vw_last_x = x = floor_div((mouse->x - margin_view_x) * zoom, scale);
	vw_last_y = y = floor_div((mouse->y - margin_view_y) * zoom, scale);

	vw_mouse_status |= 1;
	if (i & 1)
	{
		if (vw_move_layer > 0)
			move_layer_relative(vw_move_layer, x - dx, y - dy);
	}
	else
	{
		dx = layer_table[0].x;
		dy = layer_table[0].y;
		vw_move_layer = -1;		// Which layer has the user clicked?
		for (i = layers_total; i > 0; i--)
		{
			lx = layer_table[i].x - dx;
			ly = layer_table[i].y - dy;
			image = i == layer_selected ? &mem_image :
				&layer_table[i].image->image_;
			lw = image->width;
			lh = image->height;
			bpp = image->bpp;
			img = image->img;
			pal = image->pal;
			rgb = img[CHN_IMAGE];

			/* Is click within layer box? */
			if ( x>=lx && x<(lx + lw) && y>=ly && y<(ly + lh) &&
				layer_table[i].visible )
			{
				ofs = (x-lx) + lw*(y-ly);
				/* Is transparency disabled? */
				if (opaque_view) break;
				/* Is click on a non transparent pixel? */
				if (img[CHN_ALPHA])
				{
					if (img[CHN_ALPHA][ofs] < (bpp == 1 ? 255 : 1))
						continue;
				}
				tpix = image->trans;
				if (tpix >= 0)
				{
					if (bpp == 1) ppix = rgb[ofs];
					else
					{
						tpix = PNG_2_INT(pal[tpix]);
						ppix = MEM_2_INT(rgb, ofs * 3);
					}
					if (tpix == ppix) continue;
				}
				break;
			}
		}
		if (i > 0) vw_move_layer = i;
		layer_choose(i);
	}

	return (pflag);
}

void view_show()
{
	if (view_showing == view_vsplit + 1) return;
	cmd_set(main_split, view_vsplit + 1);
	view_showing = view_vsplit + 1;
	toolbar_viewzoom(TRUE);
	cmd_set(menu_slots[MENU_VIEW], TRUE);
	vw_realign();
}

void view_hide()
{
	if (!view_showing) return;
	view_showing = 0;
	cmd_set(main_split, 0);
	toolbar_viewzoom(FALSE);
	cmd_set(menu_slots[MENU_VIEW], FALSE);
}


void pressed_centralize(int state)
{
	canvas_image_centre = state;
	force_main_configure();		// Force configure of main window - for centalizing code
}

void pressed_view_focus(int state)
{
	vw_focus_on = state;
	vw_focus_view();
}

#define REPAINT_VIEW_COST 256

void *init_view_code[] = {
	REFv(vw_scrolledwindow), CSCROLLv(vwxy), HIDDEN,
	REFv(vw_drawing), CANVAS(1, 1, REPAINT_VIEW_COST, vw_repaint),
	EVENT(CHANGE, vw_configure),
	EVENT(MOUSE, vw_mouse_event), EVENT(RMOUSE, vw_mouse_event),
	EVENT(MMOUSE, vw_mouse_event),
	/* !!! Caller adds another event to vw_drawing */
	RET
};


////	TEXT TOOL

/* !!! This function invalidates "img" (may free or realloc it) */
int make_text_clipboard(unsigned char *img, int w, int h, int src_bpp)
{
	unsigned char bkg[3], *src, *dest, *tmp, *pix = img, *mask = NULL;
	int i, l = w *h;
	int idx, masked, aa, ab, back, dest_bpp = MEM_BPP;

	idx = (mem_channel == CHN_IMAGE) && (mem_img_bpp == 1);
	/* Indexed image can't be antialiased */
	aa = !idx && font_aa;
	ab = font_bk;

	back = font_bkg;
// !!! Bug - who said palette is unchanged?
	bkg[0] = mem_pal[back].red;
	bkg[1] = mem_pal[back].green;
	bkg[2] = mem_pal[back].blue;

// !!! Inconsistency - why not use mask for utility channels, too?
	masked = !ab && (mem_channel == CHN_IMAGE);
	if (masked)
	{
		if ((src_bpp == 3) && (dest_bpp == 3)) mask = calloc(1, l);
			else mask = img , pix = NULL;
		if (!mask) goto fail;
	}
	else if (src_bpp < dest_bpp) pix = NULL;

	if (mask) /* Set up clipboard mask */
	{ 
		src = img; dest = mask;
		for (i = 0; i < l; i++ , src += src_bpp)
			*dest++ = *src; /* Image is white on black */
		if (!aa) mem_threshold(mask, l, 128);
	}

	if ((mask == img) && (src_bpp == 3)) /* Release excess memory */
		if ((tmp = realloc(mask, l))) mask = img = tmp;

	if (!pix) pix = malloc(l * dest_bpp);
	if (!pix)
	{
fail:		free(img);
		return (FALSE);
	}

	src = img; dest = pix;

	/* Utility channel - have inversion instead of masking */
	if (mem_channel != CHN_IMAGE)
	{ 
		int i, j = ab ? 0 : 255;

		for (i = 0; i < l; i++ , src += src_bpp)
			*dest++ = *src ^ j; /* Image is white on black */
		if (!aa) mem_threshold(pix, l, 128);
	}

	/* Image with mask */
	else if (mask)
	{
		int i, j, k = w * dest_bpp, l8 = 8 * dest_bpp, k8 = k * 8;
		int h8 = h < 8 ? h : 8,  w8 = w < 8 ? k : l8;
		unsigned char *tmp = dest_bpp == 1 ? mem_col_pat : mem_col_pat24;

		for (j = 0; j < h8; j++) /* First strip */
		{
			dest = pix + w * j * dest_bpp;
			memcpy(dest, tmp + l8 * j, w8);
			for (i = l8; i < k; i++ , dest++)
				dest[l8] = *dest;
		}
		src = pix;
		for (j = 8; j < h; j++ , src += k) /* Repeat strips */
			memcpy(src + k8, src, k);
	}

	/* Indexed image */
	else if (dest_bpp == 1)
	{
		int i, j;
		unsigned char *tmp;

		for (j = 0; j < h; j++)
		{
			tmp = mem_col_pat + (j & 7) * 8;
			for (i = 0; i < w; i++ , src += src_bpp)
				*dest++ = *src < 128 ? back : tmp[i & 7];
		}
	}

	/* Non-antialiased RGB */
	else if (!aa)
	{
		int i, j;
		unsigned char *tmp;

		for (j = 0; j < h; j++)
		{
			tmp = mem_col_pat24 + (j & 7) * (8 * 3);
			for (i = 0; i < w; i++ , src += src_bpp , dest += 3)
			{
				unsigned char *t2 = *src < 128 ? bkg :
					tmp + (i & 7) * 3;
				dest[0] = t2[0];
				dest[1] = t2[1];
				dest[2] = t2[2];
			}
		}
	}

	/* Background-merged RGB */
	else
	{
		int i, j;
		unsigned char *tmp;

		for (j = 0; j < h; j++)
		{
			tmp = mem_col_pat24 + (j & 7) * (8 * 3);
			for (i = 0; i < w; i++ , src += src_bpp , dest += 3)
			{
				unsigned char *t2 = tmp + (i & 7) * 3;
				int m = *src ^ 255, r = t2[0], g = t2[1], b = t2[2];
				int kk;

				kk = 255 * r + m * (bkg[0] - r);
				dest[0] = (kk + (kk >> 8) + 1) >> 8;
				kk = 255 * g + m * (bkg[1] - g);
				dest[1] = (kk + (kk >> 8) + 1) >> 8;
				kk = 255 * b + m * (bkg[2] - b);
				dest[2] = (kk + (kk >> 8) + 1) >> 8;
			}
		}
	}

	/* Release excess memory */
	if ((pix == img) && (dest_bpp < src_bpp))
		if ((tmp = realloc(pix, l * dest_bpp))) pix = img = tmp;
	if ((img != pix) && (img != mask)) free(img);

	mem_clip_new(w, h, dest_bpp, 0, NULL);
	mem_clipboard = pix;
	mem_clip_mask = mask;

	return (TRUE);
}

void render_text()
{
	texteng_dd td = { inifile_get("textString", ""),
		inifile_get("lastTextFont", ""),
		font_r ? font_angle : 0,
		font_setdpi ? font_dpi : 0,
		font_align };

	td.ctx.rgb = NULL;
	cmd_setv(main_window_, &td, WINDOW_TEXTENG);
	text_paste = TEXT_PASTE_NONE;
	if (td.ctx.rgb &&
		make_text_clipboard(td.ctx.rgb, td.ctx.xy[2], td.ctx.xy[3], 3))
		text_paste = TEXT_PASTE_GTK;
	else alert_box(_("Error"), _("Not enough memory to create clipboard"), NULL);
}

typedef struct {
	char *fsel[2];
	int bkg[3], dpi[3];
	int img, idx;
	int script, angle;
	void **book, **fs;
} text_dd;

static void paste_text_ok(text_dd *dt, void **wdata, int what)
{
	run_query(wdata);

	if (dt->fsel[0]) inifile_set("lastTextFont", dt->fsel[0]);
	else
	{
		alert_box(_("Error"), _("No font selected"), NULL);
		if (script_cmds) run_destroy(wdata);
		return;
	}

	if (mem_channel == CHN_IMAGE)
	{
		if (!script_cmds || (font_bk = dt->bkg[0] >= 0))
			font_bkg = dt->bkg[0];
	}
	if (!script_cmds || (font_r = !!dt->angle))
		font_angle = dt->angle;
	if (!script_cmds || (font_setdpi = !!dt->dpi[0]))
		font_dpi = dt->dpi[0];

	inifile_set("textString", dt->fsel[1]);

	render_text();
	update_stuff(UPD_XCOPY);
	if (mem_clipboard) pressed_paste(TRUE);

	run_destroy(wdata);
}

static void dpi_changed(text_dd *dt, void **wdata, int what, void **where)
{
	if (cmd_read(where, dt) == &font_setdpi) cmd_set(dt->book, font_setdpi);
	cmd_setv(dt->fs, (void *)(font_setdpi ? dt->dpi[0] : 0), FONTSEL_DPI);
}

char *align_txt[] = { _("Left"), _("Centre"), _("Right") };

#define WBbase text_dd
static void *text_code[] = {
	WINDOWm(_("Paste Text")),
	DEFSIZE(400, 400),
	REF(fs), FONTSEL(fsel), FOCUS, OPNAME("Font"),
	IFx(script, 1),
		XENTRY(fsel[1]), OPNAME("Text"),
	ENDIF(1),
	HSEPl(200),
	HBOXP,
	IFvx(texteng_aa, 1), UNLESSx(idx, 1), /* && */
		CHECKv(_("Antialias"), font_aa),
	ENDIF(1),
	UNLESS(img), CHECKv(_("Invert"), font_bk),
	IFx(img, 1), UNLESSx(script, 1), /* && */
		CHECKv(_("Background colour ="), font_bk),
	ENDIF(1),
	IFx(img, 1),
		SPINa(bkg), OPNAME("Background colour ="),
	ENDIF(1),
	IFvx(texteng_dpi, 2),
		UNLESSx(script, 1),
			CHECKv(_("DPI ="), font_setdpi),
				EVENT(CHANGE, dpi_changed), TRIGGER,
		ENDIF(1),
		REF(book), PLAINBOOK,
		NOSPINv(sys_dpi), WDONE, // page 0
		SPINa(dpi), EVENT(CHANGE, dpi_changed), OPNAME("DPI ="),
		WDONE, // page 1
	ENDIF(2),
	WDONE,
	HBOX,
	IFvx(texteng_rot, 1),
		UNLESS(script), CHECKv(_("Angle of rotation ="), font_r),
		FSPIN(angle, -36000, 36000), OPNAME("Angle of rotation ="),
	ENDIF(1),
	IFvx(texteng_lf, 1),
		MLABEL(_("Align")), OPTv(align_txt, 3, font_align),
	ENDIF(1),
	WDONE,
	HSEPl(200),
	OKBOXP(_("Paste Text"), paste_text_ok, _("Cancel"), NULL),
	WSHOW
};
#undef WBbase

void pressed_text()
{
	text_dd tdata = { { inifile_get("lastTextFont",
		"-misc-fixed-bold-r-normal-*-*-120-*-*-c-*-iso8859-1"),
		inifile_get("textString", __("Enter Text Here")) },
		{ font_bkg % mem_cols, 0, mem_cols - 1 }, { font_dpi, 1, 65535 },
		mem_channel == CHN_IMAGE, tdata.img && (mem_img_bpp == 1),
		!!script_cmds, font_angle };

	cmd_peekv(main_window_, &sys_dpi, sizeof(sys_dpi), WINDOW_DPI);
	if (script_cmds) // Simplified controls - spins w/o toggles
	{
		if (!font_bk) tdata.bkg[0] = -1;
		tdata.bkg[1] = -1;
		if (!font_r) tdata.angle = 0;
		if (!font_setdpi) tdata.dpi[0] = 0;
		tdata.dpi[1] = 0;
	}
	run_create_(text_code, &tdata, sizeof(tdata), script_cmds);
}

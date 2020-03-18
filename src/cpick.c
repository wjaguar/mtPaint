/*	cpick.c
	Copyright (C) 2008-2020 Mark Tyler and Dmitry Groshev

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
#include "inifile.h"
#include "png.h"
#include "mainwindow.h"
#include "icons.h"
#include "cpick.h"


#ifdef U_CPICK_MTPAINT		/* mtPaint dialog */

#if GTK_MAJOR_VERSION == 1
#include <gdk/gdkx.h>			// Used by eye dropper
#endif


#define CPICK_PAL_STRIPS_MIN	1
#define CPICK_PAL_STRIPS_DEFAULT 2
#define CPICK_PAL_STRIPS_MAX	8	/* Max vertical strips the user can have */
#define CPICK_PAL_STRIP_ITEMS	8	/* Colours on each vertical strip */
#define CPICK_PAL_MAX (CPICK_PAL_STRIPS_MAX * CPICK_PAL_STRIP_ITEMS)

#define CPICK_SIZE_MIN		64	/* Minimum size of mixer/palette areas */
#define CPICK_SIZE_DEFAULT	128
#define CPICK_SIZE_MAX		1024	/* Maximum size of mixer/palette areas */

#define CPICK_INPUT_RED		0
#define CPICK_INPUT_GREEN	1
#define CPICK_INPUT_BLUE	2
#define CPICK_INPUT_HUE		3
#define CPICK_INPUT_SATURATION	4
#define CPICK_INPUT_VALUE	5
#define CPICK_INPUT_OPACITY	6
#define CPICK_INPUT_HEX		7
#define CPICK_INPUT_TOT		8		/* Manual inputs on the right hand side */

#define CPICK_AREA_PRECUR	0		/* Current / Previous colour swatch */
#define CPICK_AREA_PICKER	1		/* Picker drawing area - Main */
#define CPICK_AREA_HUE		2		/* Picker drawing area - Hue slider */
#define CPICK_AREA_PALETTE	3		/* Palette */
#define CPICK_AREA_OPACITY	4		/* Opacity */
#define CPICK_AREA_TOT		5

#define CPICK_AREA_CURRENT	1
#define CPICK_AREA_PREVIOUS	2

static clipform_dd xcolor = { "application/x-color", NULL, 8, 16 };

typedef struct
{
	int		size,				// Vertical/horizontal size of main mixer
			pal_strips,			// Number of palette strips
			input_vals[CPICK_INPUT_TOT],	// Current input values
			rgb_previous[4],		// Previous colour/opacity
			area_size[CPICK_AREA_TOT][3],	// Width / height of each wjpixmap
			lock,				// To block input handlers
			opmask;				// To ignore opacity setting
} cpicker;

typedef struct {
	void **evt;				// Event slot
	void **drag;				// Drag/drop format
	void **inputs[CPICK_INPUT_TOT],		// Spin buttons
		**areas[CPICK_AREA_TOT];	// Focusable pixmaps
	unsigned char *imgs[CPICK_AREA_TOT];	// Image buffers
	int xy[CPICK_AREA_TOT][2];		// Cursor positions
	int pal[CPICK_PAL_MAX];			// Palette colors
	int opacity_f;				// Opacity flag
	int drop;				// Eyedropper input
	cpicker c;
} cpick_dd;


static void cpick_area_picker_create(cpick_dd *dt)
{
	cpicker *win = &dt->c;
	unsigned char *rgb, *dest, *bw, full[3];
	int i, j, k, x, y, w, h, w1, h1, w3, col;
	double hsv[3];

	w = win->area_size[CPICK_AREA_PICKER][0];
	h = win->area_size[CPICK_AREA_PICKER][1];
	rgb = dt->imgs[CPICK_AREA_PICKER];
	w1 = w - 1; h1 = h - 1; w3 = w * 3;

	// Colour in top right corner

	hsv[0] = (double)win->input_vals[CPICK_INPUT_HUE] / 255.0;
	hsv[1] = 1;
	hsv[2] = 255;

	hsv2rgb( full, hsv );

	/* Bottom row is black->white */
	dest = bw = rgb + h1 * w3;
	for (i = 0; i < w; i++ , dest += 3)
		dest[0] = dest[1] = dest[2] = (255 * i) / w1;

	/* And now use it as multiplier for all other rows */
	for (y = 0; y < h1; y++)
	{
		dest = rgb + y * w3;
		// Colour on right side, i.e. corner->white
		k = (255 * (h1 - y)) / h1;
		for (i = 0; i < 3; i++)
		{
			col = (255 * 255) + k * (full[i] - 255);
			col = (col + (col >> 8) + 1) >> 8;
			for (x = i; x < w3; x += 3)
			{
				j = col * bw[x];
				dest[x] = (j + (j >> 8) + 1) >> 8;
			}
		}
	}

	cmd_reset(dt->areas[CPICK_AREA_PICKER], dt);
}

static void cpick_precur_paint(int *col, int opacity,
	unsigned char *rgb, int dx, int w, int ww, int h)
{
	int i, j, k, x, y;
	unsigned char cols[6], *dest = rgb + dx * 3;

	for (i = 0; i < 6; i++)
	{
		k = greyz[i & 1];
		j = 255 * k + opacity * (col[i >> 1] - k);
		cols[i] = (j + (j >> 8) + 1) >> 8;
	}

	ww = (ww - w) * 3;
	for (y = 0; y < h; y++)
	{
		j = (y >> 3) & 1;
		for (x = 0; x < w; x++)
		{
			k = (((x + dx) >> 3) & 1) ^ j;
			*dest++ = cols[k + 0];
			*dest++ = cols[k + 2];
			*dest++ = cols[k + 4];
		}
		dest += ww;
	}
}

static void cpick_area_precur_create(cpick_dd *dt, int flag)
{
	cpicker *win = &dt->c;
	unsigned char *rgb;
	int w, h, w2, w2p;

	w = win->area_size[CPICK_AREA_PRECUR][0];
	h = win->area_size[CPICK_AREA_PRECUR][1];
	w2 = w >> 1; w2p = w - w2;

	rgb = dt->imgs[CPICK_AREA_PRECUR];

	if (flag & CPICK_AREA_CURRENT)
		cpick_precur_paint(win->input_vals + CPICK_INPUT_RED,
			win->input_vals[CPICK_INPUT_OPACITY],
			rgb, w2, w2p, w, h);
// !!! If I just hide opacity spin, and reorder *_OPACITY after *_GREEN,
// it'll be possible to pass all 4 ints for RGBA with a single pointer
// Its label doesn't need to be hidden - can still skip creating it like now

	if (flag & CPICK_AREA_PREVIOUS)
		cpick_precur_paint(win->rgb_previous,
			win->rgb_previous[3],
			rgb, 0, w2, w, h);

	cmd_reset(dt->areas[CPICK_AREA_PRECUR], dt);
}

static void cpick_palette_paint(cpick_dd *dt)
{
	unsigned char *dest = dt->imgs[CPICK_AREA_PALETTE];
	int i, k, kk, dd, pal_strips = dt->c.pal_strips;

	dd = dt->c.size / CPICK_PAL_STRIP_ITEMS;

	for (k = 0; k < CPICK_PAL_STRIP_ITEMS; k++)
	{
		unsigned char *tmp;

		/* Draw one row */
		for (kk = 0; kk < pal_strips; kk++)
		{
			i = dt->pal[kk * CPICK_PAL_STRIP_ITEMS + k];
			tmp = dest + (k * dd * pal_strips + kk) * dd * 3;
			tmp[0] = INT_2_R(i);
			tmp[1] = INT_2_G(i);
			tmp[2] = INT_2_B(i);
			i = dd * 3 - 3;
			while (i-- > 0) tmp[3] = *tmp , tmp++;
		}
		/* Replicate it */
		kk = dd * pal_strips * 3;
		tmp = dest + k * dd * kk;
		for (i = 1; i < dd; i++) memcpy(tmp + kk * i, tmp, kk);
	}
}



// Forward references
static void cpick_area_update_cursors(cpick_dd *dt);
static void cpick_refresh_inputs_areas(cpick_dd *dt);
static void cpick_get_rgb(cpicker *cp);
static void cpick_get_hsv(cpicker *cp);

static void set_colour(cpick_dd *dt, int rgb, int opacity)
{
	cpicker *cp = &dt->c;
	cp->input_vals[CPICK_INPUT_RED] = INT_2_R(rgb);
	cp->input_vals[CPICK_INPUT_GREEN] = INT_2_G(rgb);
	cp->input_vals[CPICK_INPUT_BLUE] = INT_2_B(rgb);
	cp->input_vals[CPICK_INPUT_OPACITY] = (opacity & 0xFF) | cp->opmask;

	cpick_get_hsv(cp);

	cpick_refresh_inputs_areas(dt);		// Update everything
}

static void cpick_populate_inputs(cpick_dd *dt)
{
	cpicker *win = &dt->c;
	int i, n = CPICK_INPUT_OPACITY - !dt->opacity_f; // skip disabled opacity

	win->lock = TRUE;
	for (i = 0; i <= n; i++)
		cmd_set(dt->inputs[i], win->input_vals[i]);

	cmd_set(dt->inputs[CPICK_INPUT_HEX], RGB_2_INT(
		win->input_vals[CPICK_INPUT_RED],
		win->input_vals[CPICK_INPUT_GREEN],
		win->input_vals[CPICK_INPUT_BLUE]));
	win->lock = FALSE;
}

static void cpick_rgba_at(cpick_dd *dt, void **slot, int x, int y,
	unsigned char *get, unsigned char *set)
{
	cpicker *cp = &dt->c;
	if (slot == dt->areas[CPICK_AREA_PALETTE])
	{
		char txt[128];
		int col, ppc, ini_col;

		ppc = cp->area_size[CPICK_AREA_PALETTE][1] / CPICK_PAL_STRIP_ITEMS;
		x /= ppc; y /= ppc;
		col = y + CPICK_PAL_STRIP_ITEMS * x;

		if (get)
		{
			ini_col = dt->pal[col];
			get[0] = INT_2_R(ini_col);
			get[1] = INT_2_G(ini_col);
			get[2] = INT_2_B(ini_col);
			get[3] = cp->input_vals[CPICK_INPUT_OPACITY];
		}
		if (set)
		{
			dt->pal[col] = ini_col = MEM_2_INT(set, 0);
			snprintf(txt, 128, "cpick_pal_%i", col);
			inifile_set_gint32(txt, ini_col);
			cpick_palette_paint(dt);
			cmd_reset(dt->areas[CPICK_AREA_PALETTE], dt);
		}
	}
	else /* if (slot == dt->areas[CPICK_AREA_PRECUR]) */
	{
		int *irgb, *ialpha;
		int pc = x >= cp->area_size[CPICK_AREA_PRECUR][0] >> 1;

		if (pc || set) // Current
		{
			irgb = cp->input_vals + CPICK_INPUT_RED;
			ialpha = cp->input_vals + CPICK_INPUT_OPACITY;
		}
		else // Previous
		{
			irgb = cp->rgb_previous;
			ialpha = cp->rgb_previous + 3;
		}

		if (get)
		{
			get[0] = irgb[0]; get[1] = irgb[1]; get[2] = irgb[2];
			get[3] = *ialpha;
		}
		if (set)
		{
			irgb[0] = set[0]; irgb[1] = set[1]; irgb[2] = set[2];
			*ialpha = set[3];
			if (pc) // Current color changed - announce it
			{
				set_colour(dt, MEM_2_INT(set, 0), set[3]);
				if (dt->evt) do_evt_1_d(dt->evt);
			}
			else cpick_area_precur_create(dt, CPICK_AREA_PREVIOUS);
		}
	}
}

static void cpick_area_mouse(void **orig, cpick_dd *dt, int x, int y, int button)
{
	cpicker *cp = &dt->c;
	int idx, rx, ry, aw, ah, ah1;

	for (idx = 0; idx < CPICK_AREA_TOT; idx++)
		if (dt->areas[idx] == orig) break;
	if (idx >= CPICK_AREA_TOT) return;

	aw = cp->area_size[idx][0];
	ah = cp->area_size[idx][1];
	ah1 = ah - 1;

	rx = x < 0 ? 0 : x >= aw ? aw - 1 : x;
	ry = y < 0 ? 0 : y > ah1 ? ah1 : y;

	if ( idx == CPICK_AREA_OPACITY )
	{
		int xy[2] = { aw / 2, ry };
		cmd_setv(orig, xy, FCIMAGE_XY);

		cp->input_vals[CPICK_INPUT_OPACITY] = 255 - (ry * 255) / ah1;
	}
	else if ( idx == CPICK_AREA_HUE )
	{
		int xy[2] = { aw / 2, ry };
		cmd_setv(orig, xy, FCIMAGE_XY);

		cp->input_vals[CPICK_INPUT_HUE] = 1529 - (ry * 1529) / ah1;

		cpick_area_picker_create(dt);
		cpick_get_rgb(cp);
	}
	else if ( idx == CPICK_AREA_PICKER )
	{
		int xy[2] = { rx, ry };
		cmd_setv(orig, xy, FCIMAGE_XY);

		cp->input_vals[CPICK_INPUT_VALUE] = (rx * 255) / (aw - 1);
		cp->input_vals[CPICK_INPUT_SATURATION] = 255 - (ry * 255) / ah1;
		cpick_get_rgb(cp);
	}
	else if ( idx == CPICK_AREA_PALETTE )
	{
		unsigned char rgba[4];
		int ini_col;

		cpick_rgba_at(dt, orig, rx, ry, rgba, NULL);
		ini_col = MEM_2_INT(rgba, 0);
		// Only update if colour is different
		if (ini_col == RGB_2_INT(cp->input_vals[CPICK_INPUT_RED],
			cp->input_vals[CPICK_INPUT_GREEN],
			cp->input_vals[CPICK_INPUT_BLUE])) return;
		set_colour(dt, ini_col, cp->input_vals[CPICK_INPUT_OPACITY]);
	}
	else return;

	if (idx != CPICK_AREA_PALETTE) // set_colour() does that and more
	{
		cpick_populate_inputs(dt);
		cpick_area_precur_create(dt, CPICK_AREA_CURRENT);
	}
	if (dt->evt) do_evt_1_d(dt->evt);
}

static int cpick_drag_get(cpick_dd *dt, void **wdata, int what, void **where,
	drag_ext *drag)
{
	void **orig = origin_slot(where);
	unsigned char drag_rgba[4];
	guint16 vals[4];

	cpick_rgba_at(dt, orig, drag->x, drag->y, drag_rgba, NULL);

	if (!drag->format) /* Setting icon color */
		cmd_setv(where, (void *)MEM_2_INT(drag_rgba, 0), DRAG_ICON_RGB);
	else /* Setting drag data */
	{
		void *pp[2] = { vals, vals + 4 };

		vals[0] = drag_rgba[0] * 257;
		vals[1] = drag_rgba[1] * 257;
		vals[2] = drag_rgba[2] * 257;
		vals[3] = drag_rgba[3] * 257;

		cmd_setv(where, pp, DRAG_DATA);
	}
	return (TRUE); // Agree to drag
}

static void cpick_drag_set(cpick_dd *dt, void **wdata, int what, void **where,
	drag_ext *drag)
{
	void **orig = origin_slot(where);
	unsigned char rgba[4];
	int i;

	if (drag->len != 8) return;
	for (i = 0; i < 4; i++)
		rgba[i] = (((guint16 *)drag->data)[i] + 128) / 257;
	cpick_rgba_at(dt, orig, drag->x, drag->y, NULL, rgba);
}

static int cpick_area_event(cpick_dd *dt, void **wdata, int what, void **where,
	mouse_ext *mouse)
{
	if (mouse->button) cpick_area_mouse(origin_slot(where), dt,
		mouse->x, mouse->y, mouse->button);
	return (TRUE);
}

static void cpick_get_rgb(cpicker *cp)		// Calculate RGB values from HSV
{
	unsigned char rgb[3];
	double hsv[3] = {
		(double)cp->input_vals[CPICK_INPUT_HUE] / 255.0,
		(double)cp->input_vals[CPICK_INPUT_SATURATION] / 255.0,
		(double)cp->input_vals[CPICK_INPUT_VALUE] };

	hsv2rgb( rgb, hsv );
//printf("rgb = %i %i %i\n", rgb[0], rgb[1], rgb[2]);
	cp->input_vals[CPICK_INPUT_RED]   = rgb[0];
	cp->input_vals[CPICK_INPUT_GREEN] = rgb[1];
	cp->input_vals[CPICK_INPUT_BLUE]  = rgb[2];
}

static void cpick_get_hsv(cpicker *cp)		// Calculate HSV values from RGB
{
	unsigned char rgb[3] = {
		cp->input_vals[CPICK_INPUT_RED],
		cp->input_vals[CPICK_INPUT_GREEN],
		cp->input_vals[CPICK_INPUT_BLUE] };
	double hsv[3];

	rgb2hsv( rgb, hsv );
// !!! rint() maybe?
	cp->input_vals[CPICK_INPUT_HUE]        = 255 * hsv[0];
	cp->input_vals[CPICK_INPUT_SATURATION] = 255 * hsv[1];
	cp->input_vals[CPICK_INPUT_VALUE]      = hsv[2];
}

static void cpick_update(cpick_dd *dt, int what)
{
	cpicker *cp = &dt->c;
	int new_rgb = FALSE, new_h = FALSE, new_sv = FALSE;//, new_opac = FALSE;

	switch (what)
	{
	case CPICK_INPUT_RED:
	case CPICK_INPUT_GREEN:
	case CPICK_INPUT_BLUE:
	case CPICK_INPUT_HEX:
		new_rgb = TRUE;
		break;
	case CPICK_INPUT_HUE:
		new_h = TRUE;
		break;
	case CPICK_INPUT_SATURATION:
	case CPICK_INPUT_VALUE:
		new_sv = TRUE;
		break;
	case CPICK_INPUT_OPACITY:
//		new_opac = TRUE;
		break;
	default: return;
	}

	if (new_h || new_sv || new_rgb)
	{
		if (new_rgb) cpick_get_hsv(cp);
		else cpick_get_rgb(cp);

		cpick_populate_inputs(dt);	// Update all inputs in dialog

		// New RGB or Hue so recalc picker
		if (!new_sv) cpick_area_picker_create(dt);
	}

	cpick_area_update_cursors(dt);

	// Update current colour
	cpick_area_precur_create(dt, CPICK_AREA_CURRENT);

	if (dt->evt) do_evt_1_d(dt->evt);
}

static void cpick_hex_change(cpick_dd *dt, void **wdata, int what, void **where)
{
	int r, g, b, rgb;

	if (dt->c.lock) return;
	rgb = *(int *)cmd_read(where, dt);
	r = INT_2_R(rgb);
	g = INT_2_G(rgb);
	b = INT_2_B(rgb);
	if (!((r ^ dt->c.input_vals[CPICK_INPUT_RED]) |
		(g ^ dt->c.input_vals[CPICK_INPUT_GREEN]) |
		(b ^ dt->c.input_vals[CPICK_INPUT_BLUE]))) return; // no change
	dt->c.input_vals[CPICK_INPUT_RED] = r;
	dt->c.input_vals[CPICK_INPUT_GREEN] = g;
	dt->c.input_vals[CPICK_INPUT_BLUE] = b;
	cpick_update(dt, CPICK_INPUT_HEX);
}

static void cpick_spin_change(cpick_dd *dt, void **wdata, int what, void **where)
{
	int i, *input;

	if (dt->c.lock) return;
	where = origin_slot(where);
	i = *(input = slot_data(origin_slot(where), dt));
	if (*(int *)cmd_read(where, dt) == i) return; // no change
	cpick_update(dt, input - dt->c.input_vals);
}

static void dropper_terminate(GtkWidget *widget, gpointer user_data)
{
	gtk_signal_disconnect_by_data(GTK_OBJECT(widget), user_data);
	undo_grab(widget);
}

static void dropper_grab_colour(GtkWidget *widget, gint x, gint y,
	gpointer user_data)
{
	void **slot = user_data, **base = slot[0];
	unsigned char rgb[3];

#if GTK_MAJOR_VERSION == 1
	if (!wj_get_rgb_image((GdkWindow *)&gdk_root_parent, NULL,
		rgb, x, y, 1, 1)) return;
#else /* #if GTK_MAJOR_VERSION >= 2 */
	if (!wj_get_rgb_image(gdk_get_default_root_window(), NULL,
		rgb, x, y, 1, 1)) return;
#endif
	/* Ungrab before sending signal - better safe than sorry */
	dropper_terminate(widget, user_data);

	*(int *)slot_data(PREV_SLOT(slot), GET_DDATA(base)) =
		MEM_2_INT(rgb, 0); // self-updating
	do_evt_1_d(slot);
}

static gboolean dropper_key_press(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	int x, y;

	if (arrow_key(event, &x, &y, 20)) move_mouse_relative(x, y);
	else if (event->keyval == KEY(Escape)) dropper_terminate(widget, user_data);
	else if ((event->keyval == KEY(Return)) || (event->keyval == KEY(KP_Enter)) ||
		(event->keyval == KEY(space)) || (event->keyval == KEY(KP_Space)))
	{
#if GTK_MAJOR_VERSION == 1
		gdk_window_get_pointer((GdkWindow *)&gdk_root_parent,
			&x, &y, NULL);
#else /* #if GTK_MAJOR_VERSION >= 2 */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS
		gdk_display_get_pointer(gtk_widget_get_display(widget),	NULL,
			&x, &y, NULL);
G_GNUC_END_IGNORE_DEPRECATIONS
#endif
		dropper_grab_colour(widget, x, y, user_data);
	}

#if GTK_MAJOR_VERSION == 1
	/* Return value alone doesn't stop GTK1 from running other handlers */
	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
#endif
	return (TRUE);
}

static gboolean dropper_mouse_press(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	if (event->type != GDK_BUTTON_RELEASE) return (FALSE);
#if GTK_MAJOR_VERSION == 3
	/* GTK+3 sends the release event from button press to grab widget, too */
	if (event->window != gtk_widget_get_window(widget)) return (FALSE);
#endif
	dropper_grab_colour(widget, event->x_root, event->y_root, user_data);
	return (TRUE);
}

static void click_eyedropper(GtkButton *button, gpointer user_data)
{
	static GdkCursor *cursor;
	/* !!! If button itself is used for grab widget, it will receive mouse
	 * clicks, with obvious result */
	GtkWidget *grab_widget = gtk_widget_get_parent(GTK_WIDGET(button));

	if (!cursor) cursor = make_cursor(xbm_picker_bits, xbm_picker_mask_bits,
		xbm_picker_width, xbm_picker_height, xbm_picker_x_hot, xbm_picker_y_hot);
	if (do_grab(GRAB_FULL, grab_widget, cursor))
	{
		gtk_signal_connect(GTK_OBJECT(grab_widget), "button_release_event",
			GTK_SIGNAL_FUNC(dropper_mouse_press), user_data);
		gtk_signal_connect(GTK_OBJECT(grab_widget), "key_press_event",
			GTK_SIGNAL_FUNC(dropper_key_press), user_data);
	}
}

static int cpick_area_key(cpick_dd *dt, void **wdata, int what, void **where,
	key_ext *key)
{
	void **orig = origin_slot(where);
	cpicker *cp = &dt->c;
	int dx, dy;

	if (!arrow_key_(key->key, key->state, &dx, &dy, 16)) return (FALSE);

	if (orig == dt->areas[CPICK_AREA_PICKER])
	{
		int	new_sat = cp->input_vals[CPICK_INPUT_SATURATION] - dy,
			new_val = cp->input_vals[CPICK_INPUT_VALUE] + dx;

		new_sat = new_sat < 0 ? 0 : new_sat > 255 ? 255 : new_sat;
		new_val = new_val < 0 ? 0 : new_val > 255 ? 255 : new_val;

		if (	new_sat != cp->input_vals[CPICK_INPUT_SATURATION] ||
			new_val != cp->input_vals[CPICK_INPUT_VALUE] )
		{
			cp->input_vals[CPICK_INPUT_SATURATION] = new_sat;
			cp->input_vals[CPICK_INPUT_VALUE] = new_val;
			cpick_get_rgb(cp);				// Update RGB values
			cpick_area_update_cursors(dt);			// Update cursors
			cpick_refresh_inputs_areas(dt);			// Update inputs
			if (dt->evt) do_evt_1_d(dt->evt);
		}
	}
	else if (!dy); // X isn't used anywhere else
	else if (orig == dt->areas[CPICK_AREA_OPACITY])
	{
		int new_opac = cp->input_vals[CPICK_INPUT_OPACITY] - dy;
		new_opac = new_opac < 0 ? 0 : new_opac > 255 ? 255 : new_opac;

		if ( new_opac != cp->input_vals[CPICK_INPUT_OPACITY] )
		{
			cp->input_vals[CPICK_INPUT_OPACITY] = new_opac;
			cpick_area_update_cursors(dt);
			cpick_populate_inputs(dt);		// Update all inputs in dialog
			cpick_area_precur_create(dt, CPICK_AREA_CURRENT);
			if (dt->evt) do_evt_1_d(dt->evt);
		}
	}
	else if (orig == dt->areas[CPICK_AREA_HUE])
	{
		int new_hue = cp->input_vals[CPICK_INPUT_HUE] - 8*dy;

		new_hue = new_hue < 0 ? 0 : new_hue > 1529 ? 1529 : new_hue;

		if ( new_hue != cp->input_vals[CPICK_INPUT_HUE] )
		{
			cp->input_vals[CPICK_INPUT_HUE] = new_hue; // Change hue
			cpick_get_rgb(cp);		// Update RGB values
			cpick_area_update_cursors(dt);	// Update cursors
			cpick_area_picker_create(dt);	// Repaint picker
			cpick_refresh_inputs_areas(dt);	// Update inputs
			if (dt->evt) do_evt_1_d(dt->evt);
		}
	}

	return (TRUE);
}

#if GTK_MAJOR_VERSION == 1

#define MIN_ENTRY_WIDTH 150
static void hex_size_req(GtkWidget *widget, GtkRequisition *requisition,
	gpointer user_data)
{
	int l = gdk_string_width(widget->style->font, "#DDDDDDD");
//	if (l > MIN_ENTRY_WIDTH) requisition->width += l - MIN_ENTRY_WIDTH;
	requisition->width = l + 30; /* Set requisition to a sane value */
}

#endif

GtkWidget *eyedropper(void **r)
{
	GtkWidget *button = gtk_button_new();
	gtk_container_add(GTK_CONTAINER(button), xpm_image(XPM_ICON(picker)));

	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_eyedropper), NEXT_SLOT(r));

	return (button);
}

static gboolean hexentry_change(GtkWidget *widget, GdkEventFocus *event,
	gpointer user_data)
{
	void **slot = user_data, **base = slot[0];
	int c = parse_color((char *)gtk_entry_get_text(GTK_ENTRY(widget)));

	if (c >= 0) // Valid
	{
		*(int *)slot_data(PREV_SLOT(slot), GET_DDATA(base)) = c; // self-updating
		do_evt_1_d(slot);
	}

	return (FALSE);
}

void set_hexentry(GtkWidget *entry, int c)
{
	char txt[32];
	sprintf(txt, "#%06X", c);
	gtk_entry_set_text(GTK_ENTRY(entry), txt);
}

GtkWidget *hexentry(int c, void **r)
{
	GtkWidget *entry = gtk_entry_new();

#if GTK_MAJOR_VERSION == 1
	gtk_signal_connect(GTK_OBJECT(entry),
		"size_request", GTK_SIGNAL_FUNC(hex_size_req), NULL);
#else /* #if GTK_MAJOR_VERSION >= 2 */
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 9);
#endif
	set_hexentry(entry, c);
	gtk_signal_connect(GTK_OBJECT(entry), "focus_out_event",
		GTK_SIGNAL_FUNC(hexentry_change), NEXT_SLOT(r));

	return (entry);
}

#define WBbase cpick_dd
static void *cpick_code[] = {
	BORDER(TOPVBOX, 0), TOPVBOX,
	XHBOXbp(2, 0, 0),
	// --- Palette/Mixer table
	BORDER(TABLE, 0),
	TABLE(4, 2),
	REF(drag), CLIPFORM(xcolor, 1),
	/* Palette */
	TALLOC(imgs[CPICK_AREA_PALETTE], c.area_size[CPICK_AREA_PALETTE][2]),
	REF(areas[CPICK_AREA_PALETTE]),
	TLFCIMAGEPn(imgs[CPICK_AREA_PALETTE], c.area_size[CPICK_AREA_PALETTE], 0, 0),
		DRAGDROP(drag, cpick_drag_get, cpick_drag_set),
		EVENT(MOUSE, cpick_area_event), EVENT(MMOUSE, cpick_area_event),
	/* Picker drawing area - Main */
	TALLOC(imgs[CPICK_AREA_PICKER], c.area_size[CPICK_AREA_PICKER][2]),
	REF(areas[CPICK_AREA_PICKER]),
	TLFCIMAGEP(imgs[CPICK_AREA_PICKER], xy[CPICK_AREA_PICKER],
		c.area_size[CPICK_AREA_PICKER], 1, 0),
		EVENT(MOUSE, cpick_area_event), EVENT(MMOUSE, cpick_area_event),
		EVENT(KEY, cpick_area_key),
	/* Picker drawing area - Hue slider */
	TALLOC(imgs[CPICK_AREA_HUE], c.area_size[CPICK_AREA_HUE][2]),
	REF(areas[CPICK_AREA_HUE]),
	TLFCIMAGEP(imgs[CPICK_AREA_HUE], xy[CPICK_AREA_HUE],
		c.area_size[CPICK_AREA_HUE], 2, 0),
		EVENT(MOUSE, cpick_area_event), EVENT(MMOUSE, cpick_area_event),
		EVENT(KEY, cpick_area_key),
	/* Opacity */
	TALLOC(imgs[CPICK_AREA_OPACITY], c.area_size[CPICK_AREA_OPACITY][2]),
	REF(areas[CPICK_AREA_OPACITY]),
	TLFCIMAGEP(imgs[CPICK_AREA_OPACITY], xy[CPICK_AREA_OPACITY],
		c.area_size[CPICK_AREA_OPACITY], 3, 0),
		UNLESS(opacity_f), HIDDEN,
		EVENT(MOUSE, cpick_area_event), EVENT(MMOUSE, cpick_area_event),
		EVENT(KEY, cpick_area_key),
	/* Current / Previous colour swatch */
	TALLOC(imgs[CPICK_AREA_PRECUR], c.area_size[CPICK_AREA_PRECUR][2]),
	REF(areas[CPICK_AREA_PRECUR]),
	TLFCIMAGEPn(imgs[CPICK_AREA_PRECUR], c.area_size[CPICK_AREA_PRECUR], 1, 1),
		DRAGDROP(drag, cpick_drag_get, cpick_drag_set),
		EVENT(MOUSE, cpick_area_event), EVENT(MMOUSE, cpick_area_event),
	EYEDROPPER(drop, cpick_hex_change, 2, 1),
	WDONE, // table
	// --- Table for inputs on right hand side
	BORDER(LABEL, 2), BORDER(SPIN, 0),
	TABLE(2, 8),
	TLABELx(_("Red"), 0, 0, 10),
	REF(inputs[CPICK_INPUT_RED]),
	T1SPIN(c.input_vals[CPICK_INPUT_RED], 0, 255),
		EVENT(CHANGE, cpick_spin_change),
	TLABELx(_("Green"), 0, 0, 10),
	REF(inputs[CPICK_INPUT_GREEN]),
	T1SPIN(c.input_vals[CPICK_INPUT_GREEN], 0, 255),
		EVENT(CHANGE, cpick_spin_change),
	TLABELx(_("Blue"), 0, 0, 10),
	REF(inputs[CPICK_INPUT_BLUE]),
	T1SPIN(c.input_vals[CPICK_INPUT_BLUE], 0, 255),
		EVENT(CHANGE, cpick_spin_change),
	TLABELx(_("Hue"), 0, 0, 10),
	REF(inputs[CPICK_INPUT_HUE]),
	T1SPIN(c.input_vals[CPICK_INPUT_HUE], 0, 1529),
		EVENT(CHANGE, cpick_spin_change),
	TLABELx(_("Saturation"), 0, 0, 10),
	REF(inputs[CPICK_INPUT_SATURATION]),
	T1SPIN(c.input_vals[CPICK_INPUT_SATURATION], 0, 255),
		EVENT(CHANGE, cpick_spin_change),
	TLABELx(_("Value"), 0, 0, 10),
	REF(inputs[CPICK_INPUT_VALUE]),
	T1SPIN(c.input_vals[CPICK_INPUT_VALUE], 0, 255),
		EVENT(CHANGE, cpick_spin_change),
	TLABELx(_("Hex"), 0, 0, 10),
	REF(inputs[CPICK_INPUT_HEX]),
	HEXENTRY(c.input_vals[CPICK_INPUT_HEX], cpick_hex_change, 1, 6),
// !!! Or better just to hide it?
	IFx(opacity_f, 1),
		TLABELx(_("Opacity"), 0, 0, 10),
		REF(inputs[CPICK_INPUT_OPACITY]),
		T1SPIN(c.input_vals[CPICK_INPUT_OPACITY], 0, 255),
			EVENT(CHANGE, cpick_spin_change),
	ENDIF(1),
	WDONE, // table
	WDONE, // xhbox
	WEND
};
#undef WBbase

GtkWidget *cpick_create(int opacity)
{
	static const unsigned char hue[7][3] = {
		{255, 0, 0}, {255, 0, 255}, {0, 0, 255},
		{0, 255, 255}, {0, 255, 0}, {255, 255, 0},
		{255, 0, 0} };
	char txt[128];
	cpick_dd tdata, *dt;
	void **res;
	unsigned char *dest;
	int i, k, kk, w, h, w3, x, y, dd, d1, hy, oy, size, pal_strips;


	memset(&tdata, 0, sizeof(tdata));
	tdata.opacity_f = opacity;

	size = inifile_get_gint32("cpickerSize", CPICK_SIZE_DEFAULT);
	if ((size < CPICK_SIZE_MIN) || (size > CPICK_SIZE_MAX))
		size = CPICK_SIZE_DEFAULT;
	/* Ensure palette swatches are identical in size by adjusting size of
	 * whole area */
	tdata.c.size = size -= (size % CPICK_PAL_STRIP_ITEMS);

	pal_strips = inifile_get_gint32("cpickerStrips", CPICK_PAL_STRIPS_DEFAULT);
	if ((pal_strips < CPICK_PAL_STRIPS_MIN) || (pal_strips > CPICK_PAL_STRIPS_MAX))
		pal_strips = CPICK_PAL_STRIPS_DEFAULT;
	tdata.c.pal_strips = pal_strips;

	k = pal_strips * CPICK_PAL_STRIP_ITEMS;
	for (i = 0; i < k; i++)
	{
		snprintf(txt, 128, "cpick_pal_%i", i);
		tdata.pal[i] = inifile_get_gint32(txt, PNG_2_INT(mem_pal_def[i]));
	}

	tdata.c.rgb_previous[3] = 255;

	tdata.c.input_vals[CPICK_INPUT_SATURATION] =
		tdata.c.input_vals[CPICK_INPUT_VALUE] =
		tdata.c.input_vals[CPICK_INPUT_OPACITY] = 255;

	tdata.c.area_size[CPICK_AREA_PRECUR][0] = size;
	tdata.c.area_size[CPICK_AREA_PRECUR][1] = 3 * size / 16;
	tdata.c.area_size[CPICK_AREA_PICKER][0] = size;
	tdata.c.area_size[CPICK_AREA_PICKER][1] = size;
	tdata.c.area_size[CPICK_AREA_HUE][0] = 3 * size / 16;
	tdata.c.area_size[CPICK_AREA_HUE][1] = size;
	tdata.c.area_size[CPICK_AREA_PALETTE][0] = pal_strips * size / CPICK_PAL_STRIP_ITEMS;
	tdata.c.area_size[CPICK_AREA_PALETTE][1] = size;
	tdata.c.area_size[CPICK_AREA_OPACITY][0] = 3 * size / 16;
	tdata.c.area_size[CPICK_AREA_OPACITY][1] = size;

	for (i = 0; i < CPICK_AREA_TOT; i++)
		tdata.c.area_size[i][2] = tdata.c.area_size[i][0] *
			tdata.c.area_size[i][1] * 3;

	tdata.c.opmask = opacity ? 0 : 0xFF;

	res = run_create(cpick_code, &tdata, sizeof(tdata));
	dt = GET_DDATA(res);

	/* Prepare hue area */
	w = dt->c.area_size[CPICK_AREA_HUE][0];
	h = dt->c.area_size[CPICK_AREA_HUE][1];
	w3 = w * 3;

	dest = dt->imgs[CPICK_AREA_HUE];

	for (hy = y = k = 0; k < 6; k++)
	{
		oy = hy;
		hy = ((k + 1) * h) / 6;
		dd = hy - oy;
		for (; y < hy; y++)
		{
			d1 = y - oy;
			for (i = 0; i < 3; i++) *dest++ = hue[k][i] +
				((hue[k + 1][i] - hue[k][i]) * d1) / dd;
			for (; i < w3; i++ , dest++) *dest = *(dest - 3);
		}
	}

	/* Prepare palette area */
	cpick_palette_paint(dt);

	/* Prepare opacity area */
	w = dt->c.area_size[CPICK_AREA_OPACITY][0];
	h = dt->c.area_size[CPICK_AREA_OPACITY][1];

	dest = dt->imgs[CPICK_AREA_OPACITY];

	for (y = h - 1; y >= 0; y--)
	{
		k = 255 - (255 * y) / h;
		kk = (y >> 3) & 1;
		for (x = 0; x < w; x++ , dest += 3)
		{
			i = k * greyz[((x >> 3) & 1) ^ kk];
			dest[0] = dest[1] = dest[2] =
				(i + (i >> 8) + 1) >> 8;
		}
	}

	cpick_area_precur_create(dt, CPICK_AREA_CURRENT | CPICK_AREA_PREVIOUS);
	cpick_area_picker_create(dt);
	cpick_area_update_cursors(dt);

	return (GET_REAL_WINDOW(res));
}

void cpick_set_evt(GtkWidget *w, void **r)
{
	cpick_dd *dt = GET_DDATA(get_wdata(w, NULL));
	dt->evt = r;
}

/* These formulas perfectly reverse ones in cpick_area_mouse() when possible;
 * however, for sizes > 255 it's impossible in principle - WJ */
static void cpick_area_update_cursors(cpick_dd *dt)
{
	cpicker *cp = &dt->c;
	int xy[2], l;

	l = cp->area_size[CPICK_AREA_PICKER][0] - 1;
	xy[0] = (cp->input_vals[CPICK_INPUT_VALUE] * l + l - 1) / 255;
	l = cp->area_size[CPICK_AREA_PICKER][1] - 1;
	xy[1] = ((255 - cp->input_vals[CPICK_INPUT_SATURATION]) * l + l - 1) / 255;
	cmd_setv(dt->areas[CPICK_AREA_PICKER], xy, FCIMAGE_XY);

	xy[0] = cp->area_size[CPICK_AREA_HUE][0] / 2;
	l = cp->area_size[CPICK_AREA_HUE][1] - 1;
	xy[1] = ((1529 - cp->input_vals[CPICK_INPUT_HUE]) * l + l - 1) / 1529;
	cmd_setv(dt->areas[CPICK_AREA_HUE], xy, FCIMAGE_XY);

	xy[0] = cp->area_size[CPICK_AREA_OPACITY][0] / 2;
	l = cp->area_size[CPICK_AREA_OPACITY][1] - 1;
	xy[1] = ((255 - cp->input_vals[CPICK_INPUT_OPACITY]) * l + l - 1) / 255;
	cmd_setv(dt->areas[CPICK_AREA_OPACITY], xy, FCIMAGE_XY);
}

/* Update whole dialog according to values */
static void cpick_refresh_inputs_areas(cpick_dd *dt)
{
	cpick_populate_inputs(dt);		// Update all inputs in dialog

	cpick_area_precur_create(dt, CPICK_AREA_CURRENT);	// Update current colour
	cpick_area_picker_create(dt);		// Update picker colours
	cpick_area_update_cursors(dt);		// Update area cursors
}

int cpick_get_colour(GtkWidget *w, int *opacity)
{
	cpick_dd *dt = GET_DDATA(get_wdata(w, NULL));
	cpicker *cp = &dt->c;

	if (opacity) *opacity = cp->input_vals[CPICK_INPUT_OPACITY];
	return (RGB_2_INT(cp->input_vals[CPICK_INPUT_RED],
		cp->input_vals[CPICK_INPUT_GREEN],
		cp->input_vals[CPICK_INPUT_BLUE]));
}

void cpick_set_colour(GtkWidget *w, int rgb, int opacity)
{
	cpick_dd *dt = GET_DDATA(get_wdata(w, NULL));
	set_colour(dt, rgb, opacity);
}

void cpick_set_colour_previous(GtkWidget *w, int rgb, int opacity)
{
	cpick_dd *dt = GET_DDATA(get_wdata(w, NULL));
	cpicker *cp = &dt->c;

	cp->rgb_previous[0] = INT_2_R(rgb);
	cp->rgb_previous[1] = INT_2_G(rgb);
	cp->rgb_previous[2] = INT_2_B(rgb);
	cp->rgb_previous[3] = (opacity & 0xFF) | cp->opmask;

	// Update previous colour
	cpick_area_precur_create(dt, CPICK_AREA_PREVIOUS);
}


#endif				/* mtPaint dialog */



#ifdef U_CPICK_GTK		/* GtkColorSelection dialog */

G_GNUC_BEGIN_IGNORE_DEPRECATIONS /* Obviously */

GtkWidget *cpick_create(int opacity)
{
	GtkWidget *w = gtk_color_selection_new();
#if GTK_MAJOR_VERSION == 1
	gtk_color_selection_set_opacity(GTK_COLOR_SELECTION(w), opacity);
#else /* #if GTK_MAJOR_VERSION >= 2 */
	gtk_color_selection_set_has_palette(GTK_COLOR_SELECTION(w), TRUE);
	gtk_color_selection_set_has_opacity_control(GTK_COLOR_SELECTION(w), opacity);
#endif
	return (w);
}

void cpick_set_evt(GtkWidget *w, void **r)
{
	gtk_signal_connect_object(GTK_OBJECT(w), "color_changed",
		GTK_SIGNAL_FUNC(do_evt_1_d), (gpointer)r);
}

#if GTK_MAJOR_VERSION == 1

int cpick_get_colour(GtkWidget *w, int *opacity)
{
	gdouble color[4];

	gtk_color_selection_get_color(GTK_COLOR_SELECTION(w), color);
	if (opacity) *opacity = rint(255 * color[3]);
	return (RGB_2_INT((int)rint(255 * color[0]), (int)rint(255 * color[1]),
		(int)rint(255 * color[2])));
}

void cpick_set_colour(GtkWidget *w, int rgb, int opacity)
{
	GtkColorSelection *cs = GTK_COLOR_SELECTION(w);
	gdouble current[4] = { (gdouble)INT_2_R(rgb) / 255.0,
		(gdouble)INT_2_G(rgb) / 255.0, (gdouble)INT_2_B(rgb) / 255.0,
		(gdouble)opacity / 255.0 };
	gdouble previous[4];

	// Set current without losing previous
	memcpy(previous, cs->old_values + 3, sizeof(previous));
	gtk_color_selection_set_color( cs, previous );
	gtk_color_selection_set_color( cs, current );
}

void cpick_set_colour_previous(GtkWidget *w, int rgb, int opacity)
{
	gdouble current[4], previous[4] = { (gdouble)INT_2_R(rgb) / 255.0,
		(gdouble)INT_2_G(rgb) / 255.0, (gdouble)INT_2_B(rgb) / 255.0,
		(gdouble)opacity / 255.0 };
	GtkColorSelection *cs = GTK_COLOR_SELECTION(w);

	// Set previous without losing current
	memcpy(current, cs->values + 3, sizeof(current));
	gtk_color_selection_set_color( cs, previous );
	gtk_color_selection_set_color( cs, current );
}

#else /* #if GTK_MAJOR_VERSION >= 2 */

int cpick_get_colour(GtkWidget *w, int *opacity)
{
	GdkColor c;

	gtk_color_selection_get_current_color(GTK_COLOR_SELECTION(w), &c);
	if (opacity) *opacity = (gtk_color_selection_get_current_alpha(
		GTK_COLOR_SELECTION(w)) + 128) / 257;
	return (RGB_2_INT((c.red + 128) / 257, (c.green + 128) / 257,
		(c.blue + 128) / 257));
}

void cpick_set_colour(GtkWidget *w, int rgb, int opacity)
{
	GdkColor c;

	c.pixel = 0; c.red = INT_2_R(rgb) * 257; c.green = INT_2_G(rgb) * 257;
	c.blue = INT_2_B(rgb) * 257;
	gtk_color_selection_set_current_color(GTK_COLOR_SELECTION(w), &c);
	gtk_color_selection_set_current_alpha(GTK_COLOR_SELECTION(w), opacity * 257);
}

void cpick_set_colour_previous(GtkWidget *w, int rgb, int opacity)
{
	GdkColor c;

	c.pixel = 0; c.red = INT_2_R(rgb) * 257; c.green = INT_2_G(rgb) * 257;
	c.blue = INT_2_B(rgb) * 257;
	gtk_color_selection_set_previous_color(GTK_COLOR_SELECTION(w), &c);
	gtk_color_selection_set_previous_alpha(GTK_COLOR_SELECTION(w), opacity * 257);
}

#endif /* GTK+2&3 */

G_GNUC_END_IGNORE_DEPRECATIONS

#endif		/* GtkColorSelection dialog */

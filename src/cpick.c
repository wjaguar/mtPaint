/*	cpick.c
	Copyright (C) 2008 Mark Tyler and Dmitry Groshev

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
#include "mygtk.h"
#include "memory.h"
#include "inifile.h"
#include "png.h"
#include "mainwindow.h"
#include "icons.h"
#include "cpick.h"


#ifdef U_CPICK_MTPAINT		/* mtPaint dialog */

#if GTK_MAJOR_VERSION == 1
#include <gdk/gdkx.h>			// Used by eye dropper
#endif

#define CPICKER(obj)		GTK_CHECK_CAST (obj, cpicker_get_type (), cpicker)
#define CPICKER_CLASS(klass)	GTK_CHECK_CLASS_CAST (klass, cpicker_get_type (), cpickerClass)
#define IS_CPICKER(obj)		GTK_CHECK_TYPE (obj, cpicker_get_type ())


#define CPICK_KEY "mtPaint.cpicker"

#define CPICK_PAL_STRIPS_MIN	1
#define CPICK_PAL_STRIPS_DEFAULT 2
#define CPICK_PAL_STRIPS_MAX	8	/* Max vertical strips the user can have */
#define CPICK_PAL_STRIP_ITEMS	8	/* Colours on each vertical strip */

#define CPICK_SIZE_MIN		64	/* Minimum size of mixer/palette areas */
#define CPICK_SIZE_DEFAULT	128
#define CPICK_SIZE_MAX		1024	/* Maximum size of mixer/palette areas */

#define CPICK_INPUT_RED		0
#define CPICK_INPUT_GREEN	1
#define CPICK_INPUT_BLUE	2
#define CPICK_INPUT_HUE		3
#define CPICK_INPUT_SATURATION	4
#define CPICK_INPUT_VALUE	5
#define CPICK_INPUT_HEX		6
#define CPICK_INPUT_OPACITY	7
#define CPICK_INPUT_TOT		8		/* Manual inputs on the right hand side */

#define CPICK_AREA_PRECUR	0		/* Current / Previous colour swatch */
#define CPICK_AREA_PICKER	1		/* Picker drawing area - Main */
#define CPICK_AREA_HUE		2		/* Picker drawing area - Hue slider */
#define CPICK_AREA_PALETTE	3		/* Palette */
#define CPICK_AREA_OPACITY	4		/* Opacity */
#define CPICK_AREA_TOT		5

#define CPICK_AREA_CURRENT	1
#define CPICK_AREA_PREVIOUS	2

typedef struct
{
	GtkVBox		vbox;				// Parent class

	GtkWidget	*hbox,				// Main hbox
			*inputs[CPICK_INPUT_TOT],	// Spin buttons / Hex input
			*opacity_label,
			*areas[CPICK_AREA_TOT]		// wj_fpixmap's
			;

	int		size,				// Vertical/horizontal size of main mixer
			pal_strips,			// Number of palette strips
			input_vals[CPICK_INPUT_TOT],	// Current input values
			rgb_previous[4],		// Previous colour/opacity
			area_size[CPICK_AREA_TOT][2],	// Width / height of each wj_fpixmap
			lock;				// To block input handlers
} cpicker;

typedef struct
{
	GtkVBoxClass parent_class;
	void (*color_changed)(cpicker *cp);
} cpickerClass;

enum {
	COLOR_CHANGED,
	LAST_SIGNAL
};

static void cpicker_class_init	(cpickerClass	*klass);
static void cpicker_init	(cpicker	*cp);

static gint cpicker_signals[LAST_SIGNAL] = { 0 };
static GtkType cpicker_type;

static GtkType cpicker_get_type()
{
	if (!cpicker_type)
	{
		static const GtkTypeInfo cpicker_info = {
			"cpicker", sizeof (cpicker), sizeof(cpickerClass),
			(GtkClassInitFunc)cpicker_class_init,
			(GtkObjectInitFunc)cpicker_init,
			NULL, NULL, NULL };
		cpicker_type = gtk_type_unique(GTK_TYPE_HBOX, &cpicker_info);
	}

	return cpicker_type;
}

static void cpicker_class_init( cpickerClass *class )
{
	GTK_WIDGET_CLASS(class)->show_all = gtk_widget_show;

	cpicker_signals[COLOR_CHANGED] = gtk_signal_new ("color_changed",
		GTK_RUN_FIRST, cpicker_type,
		GTK_SIGNAL_OFFSET(cpickerClass, color_changed),
		gtk_signal_default_marshaller, GTK_TYPE_NONE, 0);

#if GTK_MAJOR_VERSION == 1
	gtk_object_class_add_signals(GTK_OBJECT_CLASS(class), cpicker_signals,
		LAST_SIGNAL);
#endif
	class->color_changed = NULL;
}


static void cpick_area_picker_create(cpicker *win)
{
	unsigned char *rgb, *dest, *bw, full[3];
	int i, j, k, x, y, w, h, w1, h1, w3, col;
	double hsv[3];

	w = win->area_size[CPICK_AREA_PICKER][0];
	h = win->area_size[CPICK_AREA_PICKER][1];
	rgb = malloc(w * h * 3);
	if (!rgb) return;
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

	wj_fpixmap_draw_rgb(win->areas[CPICK_AREA_PICKER], 0, 0, w, h, rgb, w3);
	free(rgb);
}

static void cpick_precur_paint(cpicker *win, int *col, int opacity,
	unsigned char *rgb, int dx, int w, int h)
{
	int i, j, k, x, y;
	unsigned char cols[6], *dest = rgb;

	for (i = 0; i < 6; i++)
	{
		k = greyz[i & 1];
		j = 255 * k + opacity * (col[i >> 1] - k);
		cols[i] = (j + (j >> 8) + 1) >> 8;
	}

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
	}

	wj_fpixmap_draw_rgb(win->areas[CPICK_AREA_PRECUR], dx, 0, w, h, rgb, w * 3);
}

static void cpick_area_precur_create( cpicker *win, int flag )
{
	unsigned char *rgb;
	int w, h, w2, w2p;

	w = win->area_size[CPICK_AREA_PRECUR][0];
	h = win->area_size[CPICK_AREA_PRECUR][1];
	w2 = w >> 1; w2p = w - w2;

	rgb = malloc(w2p * h * 3);
	if ( !rgb ) return;

	if (flag & CPICK_AREA_CURRENT)
		cpick_precur_paint(win, win->input_vals + CPICK_INPUT_RED,
			win->input_vals[CPICK_INPUT_OPACITY], rgb, w2, w2p, h);

	if (flag & CPICK_AREA_PREVIOUS)
		cpick_precur_paint(win, win->rgb_previous,
			win->rgb_previous[3], rgb, 0, w2, h);

	free(rgb);
}



// Forward references
static void cpick_area_update_cursors(cpicker *cp);
static void cpick_refresh_inputs_areas(cpicker *cp);
static void cpick_get_rgb(cpicker *cp);

static void cpick_populate_inputs( cpicker *win )
{
	int i;
	char txt[32];

	win->lock = TRUE;
	for ( i=0; i<CPICK_INPUT_TOT; i++ )
	{
		if (i != CPICK_INPUT_HEX) gtk_spin_button_set_value(
			GTK_SPIN_BUTTON(win->inputs[i]), win->input_vals[i]);
	}

	sprintf(txt, "#%06X", RGB_2_INT(
		win->input_vals[CPICK_INPUT_RED],
		win->input_vals[CPICK_INPUT_GREEN],
		win->input_vals[CPICK_INPUT_BLUE]));
	gtk_entry_set_text( GTK_ENTRY(win->inputs[CPICK_INPUT_HEX]), txt );
	win->lock = FALSE;
}

static void cpick_area_mouse( GtkWidget *widget, cpicker *cp, int x, int y, int button )
{
	int idx, rx, ry, aw, ah, ah1;

	for (idx = 0; idx < CPICK_AREA_TOT; idx++)
		if (cp->areas[idx] == widget) break;
	if (idx >= CPICK_AREA_TOT) return;

	aw = cp->area_size[idx][0];
	ah = cp->area_size[idx][1];
	ah1 = ah - 1;

	wj_fpixmap_xy(widget, x, y, &rx, &ry);

	rx = rx < 0 ? 0 : rx >= aw ? aw - 1 : rx;
	ry = ry < 0 ? 0 : ry > ah1 ? ah1 : ry;

//printf("x=%i y=%i b=%i\n", rx, ry, button);
	if ( idx == CPICK_AREA_OPACITY )
	{
		wj_fpixmap_move_cursor(widget, aw / 2, ry);

		cp->input_vals[CPICK_INPUT_OPACITY] = 255 - (ry * 255) / ah1;

		cpick_populate_inputs( cp );		// Update input
		cpick_area_precur_create( cp, CPICK_AREA_CURRENT );		// Update current colour

		gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[COLOR_CHANGED] );
	}
	else if ( idx == CPICK_AREA_HUE )
	{
		wj_fpixmap_move_cursor(widget, aw / 2, ry);

		cp->input_vals[CPICK_INPUT_HUE] = 1529 - (ry * 1529) / ah1;

		cpick_area_picker_create(cp);
	}
	else if ( idx == CPICK_AREA_PICKER )
	{
		wj_fpixmap_move_cursor(widget, rx, ry);

		cp->input_vals[CPICK_INPUT_VALUE] = (rx * 255) / (aw - 1);
		cp->input_vals[CPICK_INPUT_SATURATION] = 255 - (ry * 255) / ah1;
	}
	else if ( idx == CPICK_AREA_PALETTE )
	{
		char txt[128];
		int col, ppc, ini_col;

		ppc = ah / CPICK_PAL_STRIP_ITEMS;
		col = (ry / ppc) + CPICK_PAL_STRIP_ITEMS * (rx / ppc);

		snprintf(txt, 128, "cpick_pal_%i", col);
		ini_col = inifile_get_gint32(txt, RGB_2_INT(mem_pal_def[col].red,
				mem_pal_def[col].green, mem_pal_def[col].blue ) );
		if (ini_col != RGB_2_INT(cp->input_vals[CPICK_INPUT_RED],
			cp->input_vals[CPICK_INPUT_GREEN],
			cp->input_vals[CPICK_INPUT_BLUE]))
		{
			// Only update if colour is different
			cpick_set_colour(GTK_WIDGET(cp), ini_col,
				cp->input_vals[CPICK_INPUT_OPACITY]);
			gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[COLOR_CHANGED] );
		}
	}

	if ((idx == CPICK_AREA_HUE) || (idx == CPICK_AREA_PICKER))
	{
		cpick_get_rgb( cp );

		cpick_populate_inputs( cp );		// Update all inputs in dialog
		cpick_area_precur_create( cp, CPICK_AREA_CURRENT );		// Update current colour

		gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[COLOR_CHANGED] );
	}
}

static gboolean cpick_click_area(GtkWidget *widget, GdkEventButton *event, cpicker *cp)
{
	int x = event->x, y = event->y;

	gtk_widget_grab_focus(widget);

	if (event->button) cpick_area_mouse( widget, cp, x, y, event->button );

	return TRUE;
}

static gboolean cpick_motion_area(GtkWidget *widget, GdkEventMotion *event, cpicker *cp)
{
	int x, y, button = 0;
	GdkModifierType state;

#if GTK_MAJOR_VERSION == 1
	if (event->is_hint)
	{
		gdk_input_window_get_pointer(event->window, event->deviceid,
			NULL, NULL, NULL, NULL, NULL, &state);
		gdk_window_get_pointer(event->window, &x, &y, &state);
	}
	else
	{
		x = event->x;
		y = event->y;
		state = event->state;
	}
#endif
#if GTK_MAJOR_VERSION == 2
	if (event->is_hint) gdk_device_get_state(event->device, event->window,
		NULL, &state);
	x = event->x;
	y = event->y;
	state = event->state;
#endif

	if ((state & (GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)) ==
		(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)) button = 13;
	else if (state & GDK_BUTTON1_MASK) button = 1;
	else if (state & GDK_BUTTON3_MASK) button = 3;
	else if (state & GDK_BUTTON2_MASK) button = 2;

	if (button) cpick_area_mouse( widget, cp, x, y, button );

	return TRUE;
}

static void cpick_realize_area(GtkWidget *widget, cpicker *cp)
{
	static const unsigned char hue[7][3] = {
		{255, 0, 0}, {255, 0, 255}, {0, 0, 255},
		{0, 255, 255}, {0, 255, 0}, {255, 255, 0},
		{255, 0, 0} };
	unsigned char *dest, *rgb = NULL;
	char txt[128];
	int i, k, kk, w, h, w3, sz, x, y, dd, d1, hy, oy, idx;

	if (!wj_fpixmap_pixmap(widget)) return;
	if (!IS_CPICKER(cp)) return;

	for (idx = 0; idx < CPICK_AREA_TOT; idx++)
		if (cp->areas[idx] == widget) break;
	if (idx >= CPICK_AREA_TOT) return;

	w = cp->area_size[idx][0];
	h = cp->area_size[idx][1];
	w3 = w * 3; sz = w3 * h;

	switch (idx)
	{
	case CPICK_AREA_PRECUR:
	case CPICK_AREA_PICKER:
		wj_fpixmap_fill_rgb(widget, 0, 0, w, h, 0);
		break;
	case CPICK_AREA_HUE:
		if (!(dest = rgb = malloc(sz))) break;

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
		break;
	case CPICK_AREA_PALETTE:
		dd = cp->size / CPICK_PAL_STRIP_ITEMS;

		for (kk = 0; kk < cp->pal_strips; kk++)
		for (k = 0; k < CPICK_PAL_STRIP_ITEMS; k++)
		{
			i = kk * CPICK_PAL_STRIP_ITEMS + k;
			snprintf(txt, 128, "cpick_pal_%i", i);
			i = i < 256 ? RGB_2_INT(mem_pal_def[i].red,
				mem_pal_def[i].green, mem_pal_def[i].blue) : 0;
			i = inifile_get_gint32(txt, i);
			wj_fpixmap_fill_rgb(widget, dd * kk, dd * k, dd, dd, i);
		}
		break;
	case CPICK_AREA_OPACITY:
		if (!(dest = rgb = malloc(sz))) break;

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
		break;
	}

	if (rgb) wj_fpixmap_draw_rgb(widget, 0, 0, w, h, rgb, w * 3);
	free(rgb);

	if ((idx == CPICK_AREA_HUE) || (idx == CPICK_AREA_PICKER) ||
		(idx == CPICK_AREA_OPACITY))
	{
		wj_fpixmap_set_cursor(widget, xbm_ring4_bits, xbm_ring4_mask_bits,
			xbm_ring4_width, xbm_ring4_height,
			xbm_ring4_x_hot, xbm_ring4_y_hot, FALSE);
		wj_fpixmap_move_cursor(widget, w/2, h/2);
	}
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

static void cpick_update(cpicker *cp, int what)
{
	int new_rgb = FALSE, new_h = FALSE, new_sv = FALSE, new_opac = FALSE;

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
		new_opac = TRUE;
		break;
	default: return;
	}

	if (new_h || new_sv || new_rgb)
	{
		if (new_rgb) cpick_get_hsv(cp);
		else cpick_get_rgb(cp);

		cpick_populate_inputs(cp);	// Update all inputs in dialog

		// New RGB or Hue so recalc picker
		if (!new_sv) cpick_area_picker_create(cp);
	}

	cpick_area_update_cursors(cp);

	// Update current colour
	cpick_area_precur_create(cp, CPICK_AREA_CURRENT);

	gtk_signal_emit(GTK_OBJECT(cp), cpicker_signals[COLOR_CHANGED]);
}

static gboolean cpick_hex_change(GtkWidget *widget, GdkEventFocus *event,
	gpointer user_data)
{
	cpicker *cp = gtk_object_get_data(GTK_OBJECT(widget), CPICK_KEY);
	GdkColor col;
	int r, g, b;

	if (!cp || cp->lock) return (FALSE);
	if (!gdk_color_parse(gtk_entry_get_text(
		GTK_ENTRY(cp->inputs[CPICK_INPUT_HEX])), &col)) return (FALSE);
	r = ((int)col.red + 128) / 257;
	g = ((int)col.green + 128) / 257;
	b = ((int)col.blue + 128) / 257;
	if (!((r ^ cp->input_vals[CPICK_INPUT_RED]) |
		(g ^ cp->input_vals[CPICK_INPUT_GREEN]) |
		(b ^ cp->input_vals[CPICK_INPUT_BLUE]))) return (FALSE);
	cp->input_vals[CPICK_INPUT_RED] = r;
	cp->input_vals[CPICK_INPUT_GREEN] = g;
	cp->input_vals[CPICK_INPUT_BLUE] = b;
	cpick_update(cp, CPICK_INPUT_HEX);
	return (FALSE);
}

static void cpick_spin_change(GtkAdjustment *adjustment, gpointer user_data)
{
	cpicker *cp = gtk_object_get_data(GTK_OBJECT(adjustment), CPICK_KEY);
	int i, input = (int)user_data;

	if (!cp || cp->lock) return;
	i = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(cp->inputs[input]) );
	if (cp->input_vals[input] == i) return;
	cp->input_vals[input] = i;
	cpick_update(cp, input);
}

static void dropper_terminate(GtkWidget *widget1, gpointer user_data)
{
	cpicker *cp = user_data;
	GtkWidget *widget = cp->hbox;

#if GTK_MAJOR_VERSION == 1
	gdk_keyboard_ungrab( GDK_CURRENT_TIME );
	gdk_pointer_ungrab( GDK_CURRENT_TIME );
#else /* #if GTK_MAJOR_VERSION == 2 */
	guint32 time = gtk_get_current_event_time ();
	GdkDisplay *display = gtk_widget_get_display (widget);
	gdk_display_keyboard_ungrab (display, time);
	gdk_display_pointer_ungrab (display, time);
#endif
	gtk_signal_disconnect_by_data(GTK_OBJECT(widget), user_data);
	gtk_grab_remove(widget);
}

static void dropper_grab_colour(GtkWidget *widget, gint x, gint y,
	gpointer user_data)
{
	cpicker *cp = user_data;
	unsigned char rgb[3];

#if GTK_MAJOR_VERSION == 1
	if (!wj_get_rgb_image((GdkWindow *)&gdk_root_parent, NULL,
		rgb, x, y, 1, 1)) return;
#else /* #if GTK_MAJOR_VERSION == 2 */
	if (!wj_get_rgb_image(gdk_get_default_root_window(), NULL,
		rgb, x, y, 1, 1)) return;
#endif
	/* Ungrab before sending signal - better safe than sorry */
	dropper_terminate(widget, user_data);

	cpick_set_colour(GTK_WIDGET(cp), MEM_2_INT(rgb, 0),
		cp->input_vals[CPICK_INPUT_OPACITY]);
	gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[COLOR_CHANGED] );
}

static gboolean dropper_key_press(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	int x, y;

	if (arrow_key(event, &x, &y, 20)) move_mouse_relative(x, y);
	else if (event->keyval == GDK_Escape) dropper_terminate(widget, user_data);
	else if ((event->keyval == GDK_Return) || (event->keyval == GDK_KP_Enter) ||
		(event->keyval == GDK_space) || (event->keyval == GDK_KP_Space))
	{
#if GTK_MAJOR_VERSION == 1
		gdk_window_get_pointer((GdkWindow *)&gdk_root_parent,
			&x, &y, NULL);
#else /* #if GTK_MAJOR_VERSION == 2 */
		gdk_display_get_pointer(gtk_widget_get_display(widget),	NULL,
			&x, &y, NULL);
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
	dropper_grab_colour(widget, event->x_root, event->y_root, user_data);
	return (TRUE);
}

static void cpick_eyedropper(GtkButton *button, gpointer user_data)
{
	cpicker *cp = user_data;
	GtkWidget *widget = cp->hbox;
		// Using hbox avoids problems with mouse clicks in GTK+1
	GdkCursor *cursor;

#if GTK_MAJOR_VERSION == 1
	gint grab_status;

	if (gdk_keyboard_grab (widget->window, FALSE, GDK_CURRENT_TIME))
		return;

	cursor = make_cursor(xbm_picker_bits, xbm_picker_mask_bits,
		xbm_picker_width, xbm_picker_height, xbm_picker_x_hot, xbm_picker_y_hot);
	grab_status = gdk_pointer_grab (widget->window, FALSE,
		GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK,
		NULL, cursor, GDK_CURRENT_TIME);
	gdk_cursor_destroy(cursor);

	if (grab_status)
	{
		gdk_keyboard_ungrab( GDK_CURRENT_TIME );
		return;
	}
#else /* #if GTK_MAJOR_VERSION == 2 */
	GdkGrabStatus grab_status;

	if (gdk_keyboard_grab(widget->window, FALSE, gtk_get_current_event_time())
		!= GDK_GRAB_SUCCESS) return;

	cursor = make_cursor(xbm_picker_bits, xbm_picker_mask_bits,
		xbm_picker_width, xbm_picker_height, xbm_picker_x_hot, xbm_picker_y_hot);
	grab_status = gdk_pointer_grab (widget->window, FALSE,
		GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK,
		NULL, cursor, gtk_get_current_event_time ());
	gdk_cursor_destroy(cursor);

	if (grab_status != GDK_GRAB_SUCCESS)
	{
		gdk_display_keyboard_ungrab (gtk_widget_get_display (widget), GDK_CURRENT_TIME);
		return;
	}
#endif
	gtk_grab_add(widget);

	gtk_signal_connect(GTK_OBJECT(widget), "button_release_event",
		GTK_SIGNAL_FUNC(dropper_mouse_press), cp);
	gtk_signal_connect(GTK_OBJECT(widget), "key_press_event",
		GTK_SIGNAL_FUNC(dropper_key_press), cp);
}

static gboolean cpick_area_key(GtkWidget *widget, GdkEventKey *event, cpicker *cp)
{
	int dx, dy;

	if (!arrow_key(event, &dx, &dy, 16)) return (FALSE);

	if (widget == cp->areas[CPICK_AREA_PICKER])
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
			cpick_area_update_cursors(cp);			// Update cursors
			cpick_refresh_inputs_areas(cp);			// Update inputs
			gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[COLOR_CHANGED] );
		}
	}
	else if (!dy); // X isn't used anywhere else
	else if (widget == cp->areas[CPICK_AREA_OPACITY])
	{
		int new_opac = cp->input_vals[CPICK_INPUT_OPACITY] - dy;
		new_opac = new_opac < 0 ? 0 : new_opac > 255 ? 255 : new_opac;

		if ( new_opac != cp->input_vals[CPICK_INPUT_OPACITY] )
		{
			cp->input_vals[CPICK_INPUT_OPACITY] = new_opac;
			cpick_area_update_cursors(cp);
			cpick_populate_inputs( cp );		// Update all inputs in dialog
			cpick_area_precur_create( cp, CPICK_AREA_CURRENT );
			gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[COLOR_CHANGED] );
		}
	}
	else if (widget == cp->areas[CPICK_AREA_HUE])
	{
		int new_hue = cp->input_vals[CPICK_INPUT_HUE] - 8*dy;

		new_hue = new_hue < 0 ? 0 : new_hue > 1529 ? 1529 : new_hue;

		if ( new_hue != cp->input_vals[CPICK_INPUT_HUE] )
		{
			cp->input_vals[CPICK_INPUT_HUE] = new_hue; // Change hue
			cpick_get_rgb(cp);		// Update RGB values
			cpick_area_update_cursors(cp);	// Update cursors
			cpick_area_picker_create(cp);	// Repaint picker
			cpick_refresh_inputs_areas(cp);	// Update inputs
			gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[COLOR_CHANGED] );
		}
	}

#if GTK_MAJOR_VERSION == 1
	/* Return value alone doesn't stop GTK1 from running other handlers */
	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
#endif
	return (TRUE);
}

static void cpicker_init( cpicker *cp )
{
	static const unsigned char pos[CPICK_AREA_TOT][2] = {
		{1,1}, {0,1}, {0,2}, {0,0}, {0,3} };
	static const short input_vals[CPICK_INPUT_TOT][3] = {
		{0,0,255}, {0,0,255}, {0,0,255}, {0,0,1529},
		{255,0,255}, {255,0,255}, {-1,-1,-1}, {128,0,255} };
	char *in_txt[CPICK_INPUT_TOT] = { _("Red"), _("Green"), _("Blue"), _("Hue"), _("Saturation"),
			_("Value"), _("Hex"), _("Opacity") };
	GtkWidget *hbox, *button, *table, *label, *iconw;
	GtkObject *obj;
	GdkPixmap *icon, *mask;
	int i;


	cp->size = inifile_get_gint32( "cpickerSize", CPICK_SIZE_DEFAULT );
	if ( cp->size < CPICK_SIZE_MIN || cp->size > CPICK_SIZE_MAX )
		cp->size = CPICK_SIZE_DEFAULT;
	/* Ensure palette swatches are identical in size by adjusting size of
	 * whole area */
	cp->size = cp->size - (cp->size % CPICK_PAL_STRIP_ITEMS);

	cp->pal_strips = inifile_get_gint32( "cpickerStrips", CPICK_PAL_STRIPS_DEFAULT );
	if ( cp->pal_strips < CPICK_PAL_STRIPS_MIN || cp->pal_strips > CPICK_PAL_STRIPS_MAX )
		cp->pal_strips = CPICK_PAL_STRIPS_DEFAULT;

	cp->rgb_previous[3] = 255;
	cp->input_vals[CPICK_INPUT_OPACITY] = 255;

	cp->area_size[CPICK_AREA_PRECUR][0] = cp->size;
	cp->area_size[CPICK_AREA_PRECUR][1] = 3 * cp->size / 16;
	cp->area_size[CPICK_AREA_PICKER][0] = cp->size;
	cp->area_size[CPICK_AREA_PICKER][1] = cp->size;
	cp->area_size[CPICK_AREA_HUE][0] = 3 * cp->size / 16;
	cp->area_size[CPICK_AREA_HUE][1] = cp->size;
	cp->area_size[CPICK_AREA_PALETTE][0] = cp->pal_strips * cp->size / CPICK_PAL_STRIP_ITEMS;
	cp->area_size[CPICK_AREA_PALETTE][1] = cp->size;
	cp->area_size[CPICK_AREA_OPACITY][0] = 3 * cp->size / 16;
	cp->area_size[CPICK_AREA_OPACITY][1] = cp->size;

	hbox = gtk_hbox_new(FALSE, 2);
	gtk_widget_show( hbox );

	cp->hbox = hbox;
	gtk_container_add( GTK_CONTAINER (cp), hbox );

	// --- Palette/Mixer table

	table = add_a_table( 2, 4, 0, hbox );

	for (i = 0; i < CPICK_AREA_TOT; i++)
	{
		cp->areas[i] = wj_fpixmap(cp->area_size[i][0], cp->area_size[i][1]);
		gtk_table_attach(GTK_TABLE(table), cp->areas[i],
			pos[i][1], pos[i][1] + 1, pos[i][0], pos[i][0] + 1,
			(GtkAttachOptions)0, (GtkAttachOptions)0, 0, 0);
		gtk_signal_connect(GTK_OBJECT(cp->areas[i]), "realize",
			GTK_SIGNAL_FUNC(cpick_realize_area), (gpointer)cp);
		gtk_signal_connect(GTK_OBJECT(cp->areas[i]), "button_press_event",
			GTK_SIGNAL_FUNC(cpick_click_area), (gpointer)cp);

// FIXME - add drag n drop for palette & previous/current colour areas

		if ((i != CPICK_AREA_PICKER) && (i != CPICK_AREA_HUE) &&
			(i != CPICK_AREA_OPACITY)) continue;
		gtk_signal_connect(GTK_OBJECT(cp->areas[i]), "motion_notify_event",
			GTK_SIGNAL_FUNC(cpick_motion_area), (gpointer)cp);
		gtk_signal_connect(GTK_OBJECT(cp->areas[i]), "key_press_event",
			GTK_SIGNAL_FUNC(cpick_area_key), (gpointer)cp);
	}

	button = gtk_button_new();

	icon = gdk_pixmap_create_from_data(main_window->window, xbm_picker_bits,
		xbm_picker_width, xbm_picker_height,
		-1, &main_window->style->white, &main_window->style->black);
	mask = gdk_bitmap_create_from_data(main_window->window, xbm_picker_mask_bits,
		xbm_picker_width, xbm_picker_height );

	iconw = gtk_pixmap_new(icon, mask);
	gtk_widget_show(iconw);
	gdk_pixmap_unref( icon );
	gdk_pixmap_unref( mask );
	gtk_container_add( GTK_CONTAINER (button), iconw );

	gtk_table_attach (GTK_TABLE (table), button, 2, 3, 1, 2,
		(GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (GTK_FILL),
		2, 2);
	gtk_widget_show(button);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(cpick_eyedropper), cp);


	// --- Table for inputs on right hand side

	table = add_a_table( 8, 2, 0, hbox );

	for ( i=0; i<CPICK_INPUT_TOT; i++ )
	{
		label = add_to_table( in_txt[i], table, i, 0, 2);
		if ( i == CPICK_INPUT_OPACITY ) cp->opacity_label = label;
		gtk_misc_set_alignment( GTK_MISC( label ), 1.0, 0.5 );

		if ( i == CPICK_INPUT_HEX )
		{
			cp->inputs[i] = gtk_entry_new();
			gtk_widget_set_usize(cp->inputs[i], 64, -1);

			obj = GTK_OBJECT(cp->inputs[i]);

			gtk_signal_connect(obj, "focus_out_event",
				GTK_SIGNAL_FUNC(cpick_hex_change), (gpointer)i);
		}
		else
		{
			cp->inputs[i] = add_a_spin( input_vals[i][0],
				input_vals[i][1], input_vals[i][2] );

			obj = GTK_OBJECT(GTK_SPIN_BUTTON(cp->inputs[i])->adjustment);

			gtk_signal_connect(obj, "value_changed",
				GTK_SIGNAL_FUNC(cpick_spin_change), (gpointer)i);
		}
		gtk_object_set_data( obj, CPICK_KEY, cp );
		gtk_widget_show (cp->inputs[i]);
		gtk_table_attach (GTK_TABLE (table), cp->inputs[i],
				1, 2, i, i + 1,
				(GtkAttachOptions) (GTK_FILL),
				(GtkAttachOptions) (0), 0, 0);
	}
}

GtkWidget *cpick_create()
{
	return (gtk_widget_new(cpicker_get_type(), NULL));
}

/* These formulas perfectly reverse ones in cpick_area_mouse() when possible;
 * however, for sizes > 255 it's impossible in principle - WJ */
static void cpick_area_update_cursors(cpicker *cp)
{
	int x, y, l;

	l = cp->area_size[CPICK_AREA_PICKER][0] - 1;
	x = (cp->input_vals[CPICK_INPUT_VALUE] * l + l - 1) / 255;
	l = cp->area_size[CPICK_AREA_PICKER][1] - 1;
	y = ((255 - cp->input_vals[CPICK_INPUT_SATURATION]) * l + l - 1) / 255;
	wj_fpixmap_move_cursor(cp->areas[CPICK_AREA_PICKER], x, y);

	x = cp->area_size[CPICK_AREA_HUE][0] / 2;
	l = cp->area_size[CPICK_AREA_HUE][1] - 1;
	y = ((1529 - cp->input_vals[CPICK_INPUT_HUE]) * l + l - 1) / 1529;
	wj_fpixmap_move_cursor(cp->areas[CPICK_AREA_HUE], x, y);

	x = cp->area_size[CPICK_AREA_OPACITY][0] / 2;
	l = cp->area_size[CPICK_AREA_OPACITY][1] - 1;
	y = ((255 - cp->input_vals[CPICK_INPUT_OPACITY]) * l + l - 1) / 255;
	wj_fpixmap_move_cursor(cp->areas[CPICK_AREA_OPACITY], x, y);
}

/* Update whole dialog according to values */
static void cpick_refresh_inputs_areas(cpicker *cp)
{
	cpick_populate_inputs( cp );		// Update all inputs in dialog

	cpick_area_precur_create( cp, CPICK_AREA_CURRENT );	// Update current colour
	cpick_area_picker_create( cp );		// Update picker colours
	cpick_area_update_cursors( cp );	// Update area cursors
}

int cpick_get_colour(GtkWidget *w, int *opacity)
{
	cpicker *cp = CPICKER(w);

	if (!IS_CPICKER(cp)) return (0);

	if (opacity) *opacity = cp->input_vals[CPICK_INPUT_OPACITY];
	return (RGB_2_INT(cp->input_vals[CPICK_INPUT_RED],
		cp->input_vals[CPICK_INPUT_GREEN],
		cp->input_vals[CPICK_INPUT_BLUE]));
}

void cpick_set_colour(GtkWidget *w, int rgb, int opacity)
{
	cpicker *cp = CPICKER(w);

	if (!IS_CPICKER(cp)) return;

	cp->input_vals[CPICK_INPUT_RED] = INT_2_R(rgb);
	cp->input_vals[CPICK_INPUT_GREEN] = INT_2_G(rgb);
	cp->input_vals[CPICK_INPUT_BLUE] = INT_2_B(rgb);
	cp->input_vals[CPICK_INPUT_OPACITY] = opacity & 0xFF;

	cpick_get_hsv(cp);

	cpick_refresh_inputs_areas(cp);		// Update everything
}

void cpick_set_colour_previous(GtkWidget *w, int rgb, int opacity)
{
	cpicker *cp = CPICKER(w);

	if (!IS_CPICKER(cp)) return;

	cp->rgb_previous[0] = INT_2_R(rgb);
	cp->rgb_previous[1] = INT_2_G(rgb);
	cp->rgb_previous[2] = INT_2_B(rgb);
	cp->rgb_previous[3] = opacity & 0xFF;

	// Update previous colour
	cpick_area_precur_create(cp, CPICK_AREA_PREVIOUS);
}

void cpick_set_opacity_visibility( GtkWidget *w, int visible )
{
	void (*showhide)(GtkWidget *w);
	cpicker *cp = CPICKER(w);

	if (!IS_CPICKER(cp)) return;
	showhide = visible ? gtk_widget_show : gtk_widget_hide;

	showhide(cp->areas[CPICK_AREA_OPACITY]);
	showhide(cp->inputs[CPICK_INPUT_OPACITY]);
	showhide(cp->opacity_label);
}


#endif				/* mtPaint dialog */



#ifdef U_CPICK_GTK		/* GtkColorSelection dialog */

GtkWidget *cpick_create()
{
	GtkWidget *w = gtk_color_selection_new();
#if GTK_MAJOR_VERSION == 2
	gtk_color_selection_set_has_palette(GTK_COLOR_SELECTION(w), TRUE);
#endif
	return (w);
}

int cpick_get_colour(GtkWidget *w, int *opacity)
{
	gdouble color[4];

	gtk_color_selection_get_color(GTK_COLOR_SELECTION(w), color);
	if (opacity) *opacity = rint(255 * color[3]);
	return (RGB_2_INT(rint(255 * color[0]), rint(255 * color[1]),
		rint(255 * color[2])));
}

void cpick_set_colour(GtkWidget *w, int rgb, int opacity)
{
	GtkColorSelection *cs = GTK_COLOR_SELECTION(w);
	gdouble current[4] = { (gdouble)INT_2_R(rgb) / 255.0,
		(gdouble)INT_2_G(rgb) / 255.0, (gdouble)INT_2_B(rgb) / 255.0,
		(gdouble)opacity / 255.0 };

#if GTK_MAJOR_VERSION == 1
	gdouble previous[4];

	// Set current without losing previous
	memcpy(previous, cs->old_values + 3, sizeof(previous));
	gtk_color_selection_set_color( cs, previous );
#endif
	gtk_color_selection_set_color( cs, current );
}

void cpick_set_colour_previous(GtkWidget *w, int rgb, int opacity)
{
#if GTK_MAJOR_VERSION == 1
	gdouble current[4], previous[4] = { (gdouble)INT_2_R(rgb) / 255.0,
		(gdouble)INT_2_G(rgb) / 255.0, (gdouble)INT_2_B(rgb) / 255.0,
		(gdouble)opacity / 255.0 };
	GtkColorSelection *cs = GTK_COLOR_SELECTION(w);

	// Set previous without losing current
	memcpy(current, cs->values + 3, sizeof(current));
	gtk_color_selection_set_color( cs, previous );
	gtk_color_selection_set_color( cs, current );
#else /* #if GTK_MAJOR_VERSION == 2 */
	GdkColor c;

	c.pixel = 0; c.red = INT_2_R(rgb) * 257; c.green = INT_2_G(rgb) * 257;
	c.blue = INT_2_B(rgb) * 257;
	gtk_color_selection_set_previous_color(GTK_COLOR_SELECTION(w), &c);
	gtk_color_selection_set_previous_alpha(GTK_COLOR_SELECTION(w), opacity * 257);
#endif
}

void cpick_set_opacity_visibility( GtkWidget *w, int visible )
{
#if GTK_MAJOR_VERSION == 1
	gtk_color_selection_set_opacity(GTK_COLOR_SELECTION(w), visible);
#else /* #if GTK_MAJOR_VERSION == 2 */
	gtk_color_selection_set_has_opacity_control(GTK_COLOR_SELECTION(w), visible);
#endif
}



#endif		/* GtkColorSelection dialog */

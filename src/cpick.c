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

#if GTK_MAJOR_VERSION == 1
#include <gdk/gdkx.h>			// Used by eye dropper
#endif




#ifdef U_CPICK_MTPAINT		/* mtPaint dialog */

#define CPICKER(obj)		GTK_CHECK_CAST (obj, cpicker_get_type (), cpicker)
#define CPICKER_CLASS(klass)	GTK_CHECK_CLASS_CAST (klass, cpicker_get_type (), cpickerClass)
#define IS_CPICKER(obj)		GTK_CHECK_TYPE (obj, cpicker_get_type ())


#define CPICK_KEY "mtPaint.cpicker"
#define CPICK_SIGNAL_NAME "color_changed"

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
			area_size[CPICK_AREA_TOT][2]	// Width / height of each wj_fpixmap
			;
} cpicker;

typedef struct
{
	GtkVBoxClass parent_class;
	void (* cpicker) (cpicker *cp);
} cpickerClass;

enum {
	CPICKER_SIGNAL,
	LAST_SIGNAL
};

static void cpicker_class_init	(cpickerClass	*klass);
static void cpicker_init	(cpicker	*cp);

static gint cpicker_signals[LAST_SIGNAL] = { 0 };
static guint cpicker_type = 0;

static guint cpicker_get_type ()
{
	if (!cpicker_type)
	{
		GtkTypeInfo cpicker_info =
		{
		"cpicker",
		sizeof (cpicker),
		sizeof (cpickerClass),
		(GtkClassInitFunc) cpicker_class_init,
		(GtkObjectInitFunc) cpicker_init,
		NULL,
		NULL
		};

		cpicker_type = gtk_type_unique( gtk_hbox_get_type (), &cpicker_info );
	}

	return cpicker_type;
}

static void cpicker_class_init( cpickerClass *class )
{
	GtkObjectClass *object_class;

	object_class = (GtkObjectClass*) class;

	cpicker_signals[CPICKER_SIGNAL] = gtk_signal_new (CPICK_SIGNAL_NAME,
				GTK_RUN_FIRST,
				cpicker_type,
				GTK_SIGNAL_OFFSET (cpickerClass, cpicker),
				gtk_signal_default_marshaller,
				GTK_TYPE_NONE, 0);

#if GTK_MAJOR_VERSION == 1
	gtk_object_class_add_signals (object_class, cpicker_signals, LAST_SIGNAL);
#endif

	class->cpicker = NULL;
}


static void cpick_area_picker_create( cpicker *win )
{
	unsigned char *rgb, *dest, col[3], full[3];
	int x, y, w=win->area_size[CPICK_AREA_PICKER][0], h=win->area_size[CPICK_AREA_PICKER][1];
	gdouble hsv[3];

	rgb = malloc( w*h*3 );
	if ( !rgb ) return;

	// Colour in top right corner

	hsv[0] = (double)win->input_vals[CPICK_INPUT_HUE] / 255.0;
	hsv[1] = 1;
	hsv[2] = 255;

	hsv2rgb( full, hsv );

	dest = rgb;
	for ( y=0; y<h; y++ )
	{
		// Colour on right side, i.e. corner->white
		col[0] = (255*y + full[0]*(h-y)) / (h);
		col[1] = (255*y + full[1]*(h-y)) / (h);
		col[2] = (255*y + full[2]*(h-y)) / (h);
		for ( x=0; x<w; x++ )
		{
			*dest++ = col[0]*x / (w);
			*dest++ = col[1]*x / (w);
			*dest++ = col[2]*x / (w);
		}
	}

	wj_fpixmap_draw_rgb( win->areas[CPICK_AREA_PICKER], 0, 0, w, h, rgb, w*3 );
	free(rgb);
}

static void cpick_precur_paint( cpicker *win, unsigned char col[3], int opacity,
	unsigned char *rgb, int x_offset, int w, int h )
{
	int x, y;
	unsigned char *dest, greyz[2] = {153, 102}, o1 = opacity, o2 = 255-opacity, o3, o4;

	dest = rgb;

	for ( y=0; y<h; y++ )
	{
		o3 = (y / 8) % 2;
		for ( x=0; x<w/2; x++ )
		{
			o4 = greyz[ ( o3 + ((x/8)%2) ) % 2 ];
			*dest++ = (col[0] * o1 + o4 * o2) / 255;
			*dest++ = (col[1] * o1 + o4 * o2) / 255;
			*dest++ = (col[2] * o1 + o4 * o2) / 255;
		}
	}

	wj_fpixmap_draw_rgb( win->areas[CPICK_AREA_PRECUR], x_offset, 0, w/2, h, rgb, 3*w/2 );
}

static void cpick_area_precur_create( cpicker *win, int flag )
{
	unsigned char *rgb, col[3];
	int w=win->area_size[CPICK_AREA_PRECUR][0], h=win->area_size[CPICK_AREA_PRECUR][1];

	rgb = malloc( w*h*3 / 2);
	if ( !rgb ) return;

	if ( flag & CPICK_AREA_CURRENT )
	{
		col[0] = win->input_vals[CPICK_INPUT_RED];
		col[1] = win->input_vals[CPICK_INPUT_GREEN];
		col[2] = win->input_vals[CPICK_INPUT_BLUE];

		cpick_precur_paint( win, col, win->input_vals[CPICK_INPUT_OPACITY], rgb, w/2, w, h );
	}

	if ( flag & CPICK_AREA_PREVIOUS )
	{
		col[0] = win->rgb_previous[0];
		col[1] = win->rgb_previous[1];
		col[2] = win->rgb_previous[2];

		cpick_precur_paint( win, col, win->rgb_previous[3], rgb, 0, w, h );
	}

	free(rgb);
}



// Forward references
static gboolean cpick_input_change( GtkWidget *widget, GdkEventExpose *event );
static void cpick_area_update_cursors(cpicker *cp);
static void cpick_refresh_inputs_areas(cpicker *cp);
static void cpick_get_rgb(cpicker *cp);

static void cpick_populate_inputs( cpicker *win )
{
	int i;
	char txt[32];

	for ( i=0; i<CPICK_INPUT_TOT; i++ )
	{
		if (i != CPICK_INPUT_HEX) gtk_spin_button_set_value(
			GTK_SPIN_BUTTON(win->inputs[i]), win->input_vals[i]);
	}

	snprintf(txt, 30, "%06X",
		RGB_2_INT(win->input_vals[CPICK_INPUT_RED],
		win->input_vals[CPICK_INPUT_GREEN],
		win->input_vals[CPICK_INPUT_BLUE]));
	gtk_entry_set_text( GTK_ENTRY(win->inputs[CPICK_INPUT_HEX]), txt );
}

static void cpick_block_inputs( cpicker *win )
{
	int i;

	for ( i=0; i<CPICK_INPUT_TOT; i++ )
	{
		if ( i == CPICK_INPUT_HEX)
			gtk_signal_handler_block_by_func(GTK_OBJECT(win->inputs[i]),
				(GtkSignalFunc)cpick_input_change, (gpointer)i);
		else
			gtk_signal_handler_block_by_func(
				GTK_OBJECT( GTK_SPIN_BUTTON(win->inputs[i])->adjustment ),
				(GtkSignalFunc)cpick_input_change, (gpointer)i);
	}
}

static void cpick_unblock_inputs( cpicker *win )
{
	int i;

	for ( i=0; i<CPICK_INPUT_TOT; i++ )
	{
		if ( i == CPICK_INPUT_HEX)
			gtk_signal_handler_unblock_by_func(GTK_OBJECT(win->inputs[i]),
				(GtkSignalFunc)cpick_input_change, (gpointer)i);
		else
			gtk_signal_handler_unblock_by_func(
				GTK_OBJECT( GTK_SPIN_BUTTON(win->inputs[i])->adjustment ),
				(GtkSignalFunc)cpick_input_change, (gpointer)i);
	}
}

static void cpick_area_mouse( GtkWidget *widget, cpicker *cp, int x, int y, int button )
{
	int idx, rx, ry, aw, ah;

	if (!IS_CPICKER(cp)) return;

	for (idx = 0; idx < CPICK_AREA_TOT; idx++)
		if (cp->areas[idx] == widget) break;
	if (idx >= CPICK_AREA_TOT) return;

	aw = cp->area_size[idx][0];
	ah = cp->area_size[idx][1];

	wj_fpixmap_xy(widget, x, y, &rx, &ry);

	rx = rx < 0 ? 0 : rx >= aw ? aw - 1 : rx;
	ry = ry < 0 ? 0 : ry >= ah ? ah - 1 : ry;

//printf("x=%i y=%i b=%i\n", rx, ry, button);
	if ( idx == CPICK_AREA_OPACITY )
	{
		wj_fpixmap_move_cursor(widget, aw / 2, ry);

		cp->input_vals[CPICK_INPUT_OPACITY] = 255 - (ry * 255) / (ah - 1);

		cpick_block_inputs( cp );
		cpick_populate_inputs( cp );		// Update input
		cpick_unblock_inputs( cp );
		cpick_area_precur_create( cp, CPICK_AREA_CURRENT );		// Update current colour

		gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[CPICKER_SIGNAL] );
	}
	else if ( idx == CPICK_AREA_HUE )
	{
		wj_fpixmap_move_cursor(widget, aw / 2, ry);

		cp->input_vals[CPICK_INPUT_HUE] = 1529 - (ry * 1529) / (ah - 1);

		cpick_area_picker_create(cp);
	}
	else if ( idx == CPICK_AREA_PICKER )
	{
		wj_fpixmap_move_cursor(widget, rx, ry);

		cp->input_vals[CPICK_INPUT_VALUE] = (rx * 255) / (aw - 1);
		cp->input_vals[CPICK_INPUT_SATURATION] = 255 - (ry * 255) / (ah - 1);
	}
	else if ( idx == CPICK_AREA_PALETTE )
	{
		char txt[128];
		unsigned char new[3], col, ppc;
		int ini_col;

		ppc = ah / CPICK_PAL_STRIP_ITEMS;
		col = (ry / ppc) + CPICK_PAL_STRIP_ITEMS * (rx / ppc);

		snprintf(txt, 128, "cpick_pal_%i", col);
		ini_col = inifile_get_gint32(txt, RGB_2_INT(mem_pal_def[col].red,
				mem_pal_def[col].green, mem_pal_def[col].blue ) );
		new[0] = INT_2_R(ini_col);
		new[1] = INT_2_G(ini_col);
		new[2] = INT_2_B(ini_col);

		if ((new[0] ^ cp->input_vals[CPICK_INPUT_RED]) |
			(new[1] ^ cp->input_vals[CPICK_INPUT_GREEN]) |
			(new[2] ^ cp->input_vals[CPICK_INPUT_BLUE]))
		{
					// Only update if colour is different
			cpick_set_colour( GTK_WIDGET(cp), new[0], new[1], new[2], -1 );
			gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[CPICKER_SIGNAL] );
		}
	}

	if ((idx == CPICK_AREA_HUE) || (idx == CPICK_AREA_PICKER))
	{
		cpick_get_rgb( cp );

		cpick_block_inputs( cp );
		cpick_populate_inputs( cp );		// Update all inputs in dialog
		cpick_unblock_inputs( cp );
		cpick_area_precur_create( cp, CPICK_AREA_CURRENT );		// Update current colour

		gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[CPICKER_SIGNAL] );
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
		{255, 0, 0} },
		greyz[2] = {153, 102};
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
		if (!(rgb = malloc(sz))) break;

		dd = cp->size / CPICK_PAL_STRIP_ITEMS;
		d1 = dd * 3;

		for (kk = 0; kk < cp->pal_strips; kk++)
		for (k = 0; k < CPICK_PAL_STRIP_ITEMS; k++)
		{
			i = kk * CPICK_PAL_STRIP_ITEMS + k;
			snprintf(txt, 128, "cpick_pal_%i", i);
			i = i < 256 ? RGB_2_INT(mem_pal_def[i].red,
				mem_pal_def[i].green, mem_pal_def[i].blue) : 0;
			i = inifile_get_gint32(txt, i);

			dest = rgb + dd * (k * w3 + kk * 3);
			for (y = 0; y < dd; y++)
			{
				*dest++ = INT_2_R(i);
				*dest++ = INT_2_G(i);
				*dest++ = INT_2_B(i);
				for (x = 3; x < d1; x++ , dest++)
					*dest = *(dest - 3);
				dest += w3 - d1;
			}
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

static gboolean cpick_input_change( GtkWidget *widget, GdkEventExpose *event )
{
	gboolean new_rgb = FALSE, new_h = FALSE, new_sv = FALSE, new_opac = FALSE;
	cpicker *cp = gtk_object_get_data(GTK_OBJECT(widget), CPICK_KEY);
	int input = (int) gtk_object_get_data(GTK_OBJECT(widget), "user_data"),
		i, r, g, b;
	char *txtp;

	if (!IS_CPICKER(cp)) return FALSE;
	if ( !cp || input<0 || input>=CPICK_INPUT_TOT ) return FALSE;

	if ( input == CPICK_INPUT_HEX )
	{
		txtp = (char *)gtk_entry_get_text( GTK_ENTRY(cp->inputs[input]) );
		sscanf( txtp, "%x", &i );
		if ( i<0 || i>0xffffff ) i=0;
		r = INT_2_R(i);
		g = INT_2_G(i);
		b = INT_2_B(i);
//printf("input %i changed - %s to %i RGB %i %i %i\n", input, txtp, i, r, g, b );

		if ((r ^ cp->input_vals[CPICK_INPUT_RED]) |
			(g ^ cp->input_vals[CPICK_INPUT_GREEN]) |
			(b ^ cp->input_vals[CPICK_INPUT_BLUE]))
		{
			new_rgb = TRUE;
			cp->input_vals[CPICK_INPUT_RED] = r;
			cp->input_vals[CPICK_INPUT_GREEN] = g;
			cp->input_vals[CPICK_INPUT_BLUE] = b;
		}
	}
	else
	{
		i = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(cp->inputs[input]) );
//printf("input %i changed - %i\n", input, i );

		if (cp->input_vals[input] != i)
			switch (input)
			{
			case CPICK_INPUT_RED:
			case CPICK_INPUT_GREEN:
			case CPICK_INPUT_BLUE:
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
			}
		cp->input_vals[input] = i;
	}

	if ( new_h || new_sv ) cpick_get_rgb(cp);
	if ( new_rgb ) cpick_get_hsv(cp);

	if ( new_h || new_sv || new_rgb )
	{
		cpick_block_inputs( cp );
		cpick_populate_inputs( cp );		// Update all inputs in dialog
		cpick_unblock_inputs( cp );

		cpick_area_update_cursors(cp);

		if ( new_h || new_rgb )		// New RGB or Hue so recalc picker
		{
			cpick_area_picker_create(cp);
		}

		cpick_area_precur_create( cp, CPICK_AREA_CURRENT );		// Update current colour

		gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[CPICKER_SIGNAL] );
	}

	if ( new_opac )
	{
		cpick_area_precur_create( cp, CPICK_AREA_CURRENT );		// Update current colour
		cpick_area_update_cursors(cp);

		gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[CPICKER_SIGNAL] );
	}

	return FALSE;
}

// Defined later
static void dropper_terminate(GtkWidget *widget);

static void dropper_grab_colour( GtkWidget *widget, gint x, gint y, gpointer data)
{
	cpicker *cp = gtk_object_get_data(GTK_OBJECT(widget), CPICK_KEY);
	GdkWindow *root_window;
	GdkImage *image;
	GdkColormap *cmap;
	GdkColor color;
	guint32 pixel;
	int r, g, b;

	if ( !cp ) return;
#if GTK_MAJOR_VERSION == 1
	root_window = (GdkWindow *)&gdk_root_parent;
	image = gdk_image_get(root_window, x, y, 1, 1);
	pixel = gdk_image_get_pixel(image, 0, 0);
	gdk_image_destroy(image);
#else /* #if GTK_MAJOR_VERSION == 2 */
	root_window = gdk_get_default_root_window();
	image = gdk_drawable_get_image(root_window, x, y, 1, 1);
	pixel = gdk_image_get_pixel(image, 0, 0);
	g_object_unref(image);
#endif
	cmap = gdk_colormap_get_system();
	gdk_colormap_query_color(cmap, pixel, &color);
	r = ((int)color.red + 128) / 257;
	g = ((int)color.green + 128) / 257;
	b = ((int)color.blue + 128) / 257;
	cpick_set_colour( GTK_WIDGET(cp), r, g, b, -1 );
	gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[CPICKER_SIGNAL] );

//printf("grab colour at %i,%i = %i,%i,%i\n", x, y, INT_2_R(pixel), INT_2_G(pixel), INT_2_B(pixel));
}

static gboolean dropper_key_press (GtkWidget *widget, GdkEventKey *event, gpointer data)
{
	gint x, y;
#if GTK_MAJOR_VERSION == 1
	GdkModifierType state;

	gdk_window_get_pointer((GdkWindow *)&gdk_root_parent, &x, &y, &state);
#endif
#if GTK_MAJOR_VERSION == 2
	GdkDisplay *display = gtk_widget_get_display (widget);

	gdk_display_get_pointer (display, NULL, &x, &y, NULL);
#endif

	dropper_grab_colour(widget, x, y, data);
	dropper_terminate(widget);

#if GTK_MAJOR_VERSION == 1
	/* Return value alone doesn't stop GTK1 from running other handlers */
	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
#endif

	return TRUE;
}


static gboolean dropper_mouse_press( GtkWidget *widget, GdkEventButton *event, gpointer data)
{
	if (event->type == GDK_BUTTON_RELEASE )
	{
		dropper_grab_colour(widget, event->x_root, event->y_root, data); 
		dropper_terminate(widget);
		return TRUE;
	}

	return FALSE;
}

static void dropper_terminate(GtkWidget *widget1)
{
	GtkWidget *widget;
	cpicker *cp = gtk_object_get_data(GTK_OBJECT(widget1), CPICK_KEY);
#if GTK_MAJOR_VERSION == 2
	GdkDisplay *display;
	guint32 time = gtk_get_current_event_time ();

	if (!cp) return;
	widget = cp->hbox;

	display = gtk_widget_get_display (widget);
	gdk_display_keyboard_ungrab (display, time);
	gdk_display_pointer_ungrab (display, time);
#endif

#if GTK_MAJOR_VERSION == 1
	if (!cp) return;
	widget = cp->hbox;

	gdk_keyboard_ungrab( GDK_CURRENT_TIME );
	gdk_pointer_ungrab( GDK_CURRENT_TIME );
#endif

	gtk_signal_disconnect_by_func( GTK_OBJECT(widget),
			GTK_SIGNAL_FUNC(dropper_mouse_press), widget);
	gtk_signal_disconnect_by_func( GTK_OBJECT(widget),
			GTK_SIGNAL_FUNC(dropper_key_press), widget);

	gtk_grab_remove(widget);
}


static void cpick_eyedropper( GtkWidget *widget1, GdkEvent *event, gpointer user_data )
{
	cpicker *cp = gtk_object_get_data(GTK_OBJECT(widget1), CPICK_KEY);
#if GTK_MAJOR_VERSION == 1
	gint grab_status;
#endif
#if GTK_MAJOR_VERSION == 2
	GdkGrabStatus grab_status;
#endif
	GtkWidget *widget;
	GdkCursor *cursor;
// !!! And what about non-RGB modes?
	GdkColor bg = { 0, 0xffff, 0xffff, 0xffff };
	GdkColor fg = { 0, 0x0000, 0x0000, 0x0000 };


	GdkPixmap *pixmap = gdk_bitmap_create_from_data (NULL, xbm_picker_bits,
			xbm_picker_width, xbm_picker_height);

	GdkPixmap *mask = gdk_bitmap_create_from_data (NULL, xbm_picker_mask_bits,
			xbm_picker_width, xbm_picker_height);

	if ( !cp ) return;
	widget = cp->hbox;

	cursor = gdk_cursor_new_from_pixmap (pixmap, mask, &fg, &bg,
			xbm_picker_x_hot, xbm_picker_y_hot);

	gdk_pixmap_unref( pixmap );
	gdk_pixmap_unref( mask );

#if GTK_MAJOR_VERSION == 1
	if (gdk_keyboard_grab (widget->window, FALSE, GDK_CURRENT_TIME) )
	{
		gdk_cursor_destroy(cursor);
		return;
	}

	grab_status = gdk_pointer_grab (widget->window, FALSE,
		GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK,
		NULL, cursor, GDK_CURRENT_TIME);

	if ( grab_status )
	{
		gdk_cursor_destroy(cursor);
		gdk_keyboard_ungrab( GDK_CURRENT_TIME );
		return;
	}
#endif
#if GTK_MAJOR_VERSION == 2
	if (gdk_keyboard_grab (widget->window, FALSE, gtk_get_current_event_time ())
			!= GDK_GRAB_SUCCESS)
	{
		gdk_cursor_destroy(cursor);
		return;
	}

	grab_status = gdk_pointer_grab (widget->window, FALSE,
		GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK,
		NULL, cursor, gtk_get_current_event_time ());

	if (grab_status != GDK_GRAB_SUCCESS)
	{
		gdk_cursor_destroy(cursor);
		gdk_display_keyboard_ungrab (gtk_widget_get_display (widget), GDK_CURRENT_TIME);
		return;
	}
#endif

	gdk_cursor_destroy(cursor);
	gtk_grab_add(widget);

	gtk_signal_connect(GTK_OBJECT(widget), "button_release_event",
		GTK_SIGNAL_FUNC(dropper_mouse_press), widget);
	gtk_signal_connect(GTK_OBJECT(widget), "key_press_event",
		GTK_SIGNAL_FUNC(dropper_key_press), widget);
}

static gboolean cpick_area_key(GtkWidget *widget, GdkEventKey *event, cpicker *cp)
{
	int dx, dy;

	if (!IS_CPICKER(cp)) return (FALSE);

	if (!arrow_key(event, &dx, &dy, 0)) return (FALSE);

	if ( widget == cp->areas[CPICK_AREA_PICKER] )
	{
		if ( dx || dy )
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
				gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[CPICKER_SIGNAL] );
			}
		}
	}

	if ( widget == cp->areas[CPICK_AREA_OPACITY] && dy )
	{
		int new_opac = cp->input_vals[CPICK_INPUT_OPACITY] - dy;
		new_opac = new_opac < 0 ? 0 : new_opac > 255 ? 255 : new_opac;

		if ( new_opac != cp->input_vals[CPICK_INPUT_OPACITY] )
		{
			cp->input_vals[CPICK_INPUT_OPACITY] = new_opac;
			cpick_area_update_cursors(cp);
			cpick_block_inputs( cp );
			cpick_populate_inputs( cp );		// Update all inputs in dialog
			cpick_unblock_inputs( cp );
			cpick_area_precur_create( cp, CPICK_AREA_CURRENT );
			gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[CPICKER_SIGNAL] );
		}
	}

	if ( widget == cp->areas[CPICK_AREA_HUE] )
	{
		if ( dy )
		{
			int new_hue = cp->input_vals[CPICK_INPUT_HUE] - 8*dy;

			new_hue = new_hue < 0 ? 0 : new_hue > 1529 ? 1529 : new_hue;

			if ( new_hue != cp->input_vals[CPICK_INPUT_HUE] )
			{
				cp->input_vals[CPICK_INPUT_HUE] = new_hue;	// Change hue
				cpick_get_rgb(cp);				// Update RGB values
				cpick_area_update_cursors(cp);			// Update cursors
				cpick_area_picker_create(cp);			// Repaint picker
				cpick_refresh_inputs_areas(cp);			// Update inputs
				gtk_signal_emit( GTK_OBJECT(cp), cpicker_signals[CPICKER_SIGNAL] );
			}
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
	cp->size = cp->size - (cp->size % CPICK_PAL_STRIP_ITEMS);
		// Ensure palette swatches are identical in size by adjusting size of whole area

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

	for ( i=0; i<CPICK_AREA_TOT; i++ )
	{
		cp->areas[i] = wj_fpixmap( cp->area_size[i][0], cp->area_size[i][1] );
		gtk_signal_connect( GTK_OBJECT(cp->areas[i]), "realize",
			GTK_SIGNAL_FUNC(cpick_realize_area), (gpointer)(cp));
		gtk_signal_connect( GTK_OBJECT(cp->areas[i]), "button_press_event",
			GTK_SIGNAL_FUNC(cpick_click_area), (gpointer)(cp));
		if ( i == CPICK_AREA_PICKER || i == CPICK_AREA_HUE || i == CPICK_AREA_OPACITY )
		{
			gtk_signal_connect( GTK_OBJECT(cp->areas[i]), "motion_notify_event",
				GTK_SIGNAL_FUNC(cpick_motion_area), (gpointer)(cp));
			gtk_signal_connect( GTK_OBJECT(cp->areas[i]), "key_press_event",
				GTK_SIGNAL_FUNC(cpick_area_key), (gpointer)(cp));
		}

// FIXME - add drag n drop for palette & previous/current colour areas

		gtk_table_attach (GTK_TABLE (table), cp->areas[i],
			pos[i][1], pos[i][1]+1, pos[i][0], pos[i][0]+1,
			(GtkAttachOptions) (0), (GtkAttachOptions) (0), 0, 0);
	}

	button = gtk_button_new();

	icon = gdk_pixmap_create_from_data( main_window->window, xbm_picker_bits,
			xbm_picker_width, xbm_picker_height,
			-1, &main_window->style->black, &main_window->style->white );
	mask = gdk_bitmap_create_from_data(main_window->window, xbm_picker_mask_bits,
			xbm_picker_width, xbm_picker_height );

	iconw = gtk_pixmap_new(icon, mask);
	gtk_widget_show(iconw);
	gdk_pixmap_unref( icon );
	gdk_pixmap_unref( mask );
	gtk_container_add( GTK_CONTAINER (button), iconw );

	gtk_object_set_data( GTK_OBJECT(button), CPICK_KEY, cp );
	gtk_object_set_data( GTK_OBJECT(cp->hbox), CPICK_KEY, cp );
				// Using hbox avoids problems with mouse clicks in GTK+1
	gtk_table_attach (GTK_TABLE (table), button, 2, 2+1, 1, 1+1,
		(GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (GTK_FILL),
		2, 2);
	gtk_widget_show(button);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(cpick_eyedropper), button);


	// --- Table for inputs on right hand side

	table = add_a_table( 8, 2, 0, hbox );

	for ( i=0; i<CPICK_INPUT_TOT; i++ )
	{
		label = add_to_table( in_txt[i], table, i, 0, 2);
		if ( i == CPICK_INPUT_OPACITY ) cp->opacity_label = label;
		gtk_misc_set_alignment( GTK_MISC( label ), 1.0, 0.5 );

		if ( i == CPICK_INPUT_HEX )
		{
			cp->inputs[i] = gtk_entry_new_with_max_length(6);
			gtk_widget_set_usize(cp->inputs[i], 64, -1);

			obj = GTK_OBJECT(cp->inputs[i]);

			gtk_signal_connect(obj, "focus_out_event",
				GTK_SIGNAL_FUNC(cpick_input_change), (gpointer)(i));
		}
		else
		{
			cp->inputs[i] = add_a_spin( input_vals[i][0],
				input_vals[i][1], input_vals[i][2] );

			obj = GTK_OBJECT( GTK_SPIN_BUTTON(cp->inputs[i])->adjustment );

			gtk_signal_connect(obj, "value_changed",
				GTK_SIGNAL_FUNC(cpick_input_change), (gpointer)(i));
		}
		gtk_object_set_data( obj, CPICK_KEY, cp );
		gtk_object_set_data( obj, "user_data", (gpointer)i );
		gtk_widget_show (cp->inputs[i]);
		gtk_table_attach (GTK_TABLE (table), cp->inputs[i],
				1, 2, i, i+1,
				(GtkAttachOptions) (GTK_FILL),
				(GtkAttachOptions) (0), 0, 0);
	}
}

GtkWidget *cpick_create()
{
	GtkWidget *cp = GTK_WIDGET( gtk_type_new(cpicker_get_type()) );

	return cp;
}

void cpick_get_colour( GtkWidget *w, int *r, int *g, int *b, int *opacity )
{
	cpicker *cp = CPICKER(w);

	if (!IS_CPICKER(cp)) return;

	if ( r ) *r = cp->input_vals[CPICK_INPUT_RED];
	if ( g ) *g = cp->input_vals[CPICK_INPUT_GREEN];
	if ( b ) *b = cp->input_vals[CPICK_INPUT_BLUE];
	if ( opacity ) *opacity = cp->input_vals[CPICK_INPUT_OPACITY];
}

static void cpick_area_update_cursors(cpicker *cp)
{
	int x, y;

	x = (cp->input_vals[CPICK_INPUT_VALUE] * cp->area_size[CPICK_AREA_PICKER][0] ) / 255;
	y = ((255 -cp->input_vals[CPICK_INPUT_SATURATION]) * cp->area_size[CPICK_AREA_PICKER][1] ) / 255;
	wj_fpixmap_move_cursor(cp->areas[CPICK_AREA_PICKER], x, y);

	x = cp->area_size[CPICK_AREA_HUE][0] / 2;
	y = ((1529 - cp->input_vals[CPICK_INPUT_HUE]) * cp->area_size[CPICK_AREA_HUE][1] ) / 1529;
	wj_fpixmap_move_cursor(cp->areas[CPICK_AREA_HUE], x, y);

	x = cp->area_size[CPICK_AREA_OPACITY][0] / 2;
	y = ((255 - cp->input_vals[CPICK_INPUT_OPACITY]) * cp->area_size[CPICK_AREA_OPACITY][1] ) / 255;
	wj_fpixmap_move_cursor(cp->areas[CPICK_AREA_OPACITY], x, y);
}

static void cpick_refresh_inputs_areas(cpicker *cp)	// Update whole dialog according to values
{
	cpick_block_inputs( cp );
	cpick_populate_inputs( cp );		// Update all inputs in dialog
	cpick_unblock_inputs( cp );

	cpick_area_precur_create( cp, CPICK_AREA_CURRENT );		// Update current colour
	cpick_area_picker_create( cp );		// Update picker colours
	cpick_area_update_cursors( cp );	// Update area cursors
}

void cpick_set_colour( GtkWidget *w, int r, int g, int b, int opacity )
{
	cpicker *cp = CPICKER(w);

	if (!IS_CPICKER(cp)) return;

//printf("cpick_set_colour %i %i %i %i\n", r, g, b, opacity);
	if ( r>-1 ) cp->input_vals[CPICK_INPUT_RED] = r;
	if ( g>-1 ) cp->input_vals[CPICK_INPUT_GREEN] = g;
	if ( b>-1 ) cp->input_vals[CPICK_INPUT_BLUE] = b;
	if ( opacity>-1 ) cp->input_vals[CPICK_INPUT_OPACITY] = opacity;

	cpick_get_hsv(cp);

	cpick_refresh_inputs_areas(cp);		// Update everything
}

void cpick_set_colour_previous( GtkWidget *w, int r, int g, int b, int opacity )
{
	cpicker *cp = CPICKER(w);

	if (!IS_CPICKER(cp)) return;

	if ( r>-1 ) cp->rgb_previous[0] = r;
	if ( g>-1 ) cp->rgb_previous[1] = g;
	if ( b>-1 ) cp->rgb_previous[2] = b;
	if ( opacity>-1 ) cp->rgb_previous[3] = opacity;

	cpick_refresh_inputs_areas(cp);		// Update everything
	cpick_area_precur_create( cp, CPICK_AREA_PREVIOUS );		// Update previous colour
}

void cpick_set_palette_visibility( GtkWidget *w, int visible )
{
	cpicker *cp = CPICKER(w);

	if (!IS_CPICKER(cp)) return;

	if ( visible )
	{
		gtk_widget_show( cp->inputs[CPICK_AREA_PALETTE] );
	}
	else
	{
		gtk_widget_hide( cp->inputs[CPICK_AREA_PALETTE] );
	}
}

void cpick_set_opacity_visibility( GtkWidget *w, int visible )
{
	cpicker *cp = CPICKER(w);

	if (!IS_CPICKER(cp)) return;

	if ( visible )
	{
		gtk_widget_show( cp->areas[CPICK_AREA_OPACITY] );
		gtk_widget_show( cp->inputs[CPICK_INPUT_OPACITY] );
		gtk_widget_show( cp->opacity_label );
	}
	else
	{
		gtk_widget_hide( cp->areas[CPICK_AREA_OPACITY] );
		gtk_widget_hide( cp->inputs[CPICK_INPUT_OPACITY] );
		gtk_widget_hide( cp->opacity_label );
	}
}


#endif				/* mtPaint dialog */



#ifdef U_CPICK_GTK		/* GtkColorSelection dialog */

GtkWidget *cpick_create()
{
	return gtk_color_selection_new();
}

void cpick_get_colour( GtkWidget *w, int *r, int *g, int *b, int *opacity )
{
	gdouble color[4];

	gtk_color_selection_get_color( GTK_COLOR_SELECTION(w), color );
	*r = rint( 255 * color[0] );
	*g = rint( 255 * color[1] );
	*b = rint( 255 * color[2] );
	*opacity = rint( 255 * color[3] );
}

void cpick_set_colour( GtkWidget *w, int r, int g, int b, int opacity )
{
	GtkColorSelection *cs = GTK_COLOR_SELECTION(w);
	gdouble current[4] = { (gdouble) r / 255.0, (gdouble) g / 255.0,
		(gdouble) b / 255.0, (gdouble) opacity / 255.0 };

#if GTK_MAJOR_VERSION == 1
	gdouble previous[4];
	int i;

	for ( i=0; i<4; i++ ) previous[i] = cs->old_values[i+3];
	gtk_color_selection_set_color( cs, previous );	// Set current without losing previous
#endif
	gtk_color_selection_set_color( cs, current );
}

void cpick_set_colour_previous( GtkWidget *w, int r, int g, int b, int opacity )
{
#if GTK_MAJOR_VERSION == 1
	gdouble current[4], previous[4] = { (gdouble) r / 255.0, (gdouble) g / 255.0,
		(gdouble) b / 255.0, (gdouble) opacity / 255.0 };
	int i;
	GtkColorSelection *cs = GTK_COLOR_SELECTION(w);

	for ( i=0; i<4; i++ ) current[i] = cs->values[i+3];

	gtk_color_selection_set_color( cs, previous );	// Set previous without losing current
	gtk_color_selection_set_color( cs, current );
#endif
#if GTK_MAJOR_VERSION == 2
	GdkColor c;

	c.pixel = 0;
	c.red = r * 257; c.green = g * 257; c.blue = b * 257;
	gtk_color_selection_set_previous_color(GTK_COLOR_SELECTION(w), &c);
	gtk_color_selection_set_previous_alpha(GTK_COLOR_SELECTION(w), opacity * 257);
#endif
}

void cpick_set_palette_visibility( GtkWidget *w, int visible )
{
#if GTK_MAJOR_VERSION == 2
	gtk_color_selection_set_has_palette (GTK_COLOR_SELECTION(w), visible);
#endif
}

void cpick_set_opacity_visibility( GtkWidget *w, int visible )
{
#if GTK_MAJOR_VERSION == 1
	gtk_color_selection_set_opacity(GTK_COLOR_SELECTION(w), visible);
#endif
#if GTK_MAJOR_VERSION == 2
	gtk_color_selection_set_has_opacity_control(GTK_COLOR_SELECTION(w), visible);
#endif
}



#endif		/* GtkColorSelection dialog */

/*	mygtk.c
	Copyright (C) 2004-2021 Mark Tyler and Dmitry Groshev

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
#include "png.h"
#include "mainwindow.h"
#include "canvas.h"
#include "inifile.h"

#if GTK_MAJOR_VERSION == 1
#include <gtk/gtkprivate.h>
#endif

#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <gdk/gdkx.h>
#if GTK_MAJOR_VERSION == 3
#include <cairo-xlib.h>
#endif

#elif defined GDK_WINDOWING_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gdk/gdkwin32.h>
#endif

GtkWidget *main_window;

///	GENERIC WIDGET PRIMITIVES

#if GTK_MAJOR_VERSION == 3
#define GtkAdjustment_t GtkAdjustment

/* I'm totally sick and tired of this "deprecation" game. Deprecate them players
 * and the scooter they rode in on - WJ */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS

#else
#define GtkAdjustment_t GtkObject
#endif

static GtkWidget *spin_new_x(GtkAdjustment_t *adj, int fpart);

GtkWidget *add_a_window(GtkWindowType type, char *title, GtkWindowPosition pos)
{
	GtkWidget *win = gtk_window_new(type);
	gtk_window_set_title(GTK_WINDOW(win), title);
	gtk_window_set_position(GTK_WINDOW(win), pos);

	return win;
}

GtkWidget *add_a_spin( int value, int min, int max )
{
	return (spin_new_x(gtk_adjustment_new(value, min, max, 1, 10, 0), 0));
}

// Write UTF-8 text to console

static void console_printf(char *format, ...)
{
#ifdef WIN32
	static char codepage[16];
#endif
	va_list args;
	char *txt, *tx2;

	va_start(args, format);
	txt = g_strdup_vprintf(format, args);
	va_end(args);
#if GTK_MAJOR_VERSION == 1
	/* Same encoding as console */
	fputs(txt, stdout);
#else /* if GTK_MAJOR_VERSION >= 2 */
	/* UTF-8 */
#ifdef WIN32
	if (!codepage[0]) sprintf(codepage, "cp%d", GetConsoleCP());
	/* !!! Iconv on Windows knows "utf-8" but no "utf8" */
	tx2 = g_convert_with_fallback(txt, -1, codepage, "utf-8", "?",
		NULL, NULL, NULL);
#else
	tx2 = g_locale_from_utf8(txt, -1, NULL, NULL, NULL);
#endif
	fputs(tx2, stdout);
	g_free(tx2);
#endif
	g_free(txt);
}


int user_break;

////	PROGRESS WINDOW

static void **progress_window;

typedef struct {
	int stop, can_stop;
	char *what;
	void **pbar;
} progress_dd;

static void do_cancel_progress(progress_dd *dt)
{
	dt->stop = 1;
	user_break = TRUE;
}

static void delete_progress()
{
	// This stops the user closing the window via the window manager
}

#define WBbase progress_dd
static void *progress_code[] = {
	WIDTH(400), WINDOWm(_("Please Wait ...")),
	EVENT(CANCEL, delete_progress),
	BORDER(FRAME, 0),
	EFVBOX, // originally was box in viewport
	REF(pbar), PROGRESSp(what),
	IFx(can_stop, 1),
		HSEP,
		BUTTON(_("STOP"), do_cancel_progress),
	ENDIF(1),
	WSHOW
};
#undef WBbase

/* Print stars for a progress indicator */
#define STARS_IN_ROW 20
static void add_stars(double val)
{
	int i, l, n = rint(val * STARS_IN_ROW) + 1;
	for (i = l = (int)progress_window; i < n; i++) putc('*', stdout);
	if (l < n) fflush(stdout);
	progress_window = (void *)n;
}

void progress_init(char *text, int canc)		// Initialise progress window
{
	progress_dd tdata = { 0, canc, text };

	if (cmd_mode) // Console
	{
		console_printf("%s - %s\n", __(text), __("Please Wait ..."));
		progress_window = (void *)(1 + 0);
		return;
	}
	// GUI

	/* Break pointer grabs, to avoid originating widget misbehaving later on */
	release_grab();
	update_stuff(CF_NOW);

	progress_window = run_create(progress_code, &tdata, sizeof(tdata));

	progress_update(0.0);
}

int progress_update(float val)		// Update progress window
{
	if (!progress_window);
	else if (cmd_mode) add_stars(val); // Console
	else // GUI
	{
		progress_dd *dt = GET_DDATA(progress_window);
		cmd_setv(dt->pbar, (void *)(int)(val * 100), PROGRESS_PERCENT);
		handle_events();
		// !!! Depends on window not being closeable by user
		return (dt->stop);
	}
	return (FALSE);
}

void progress_end()			// Close progress window
{
	if (!progress_window);
	else if (cmd_mode) // Console
	{
		add_stars(1.0);
		putc('\n', stdout);
	}
	// GUI
	else run_destroy(progress_window);
	progress_window = NULL;
}



////	ALERT BOX

/* !!! Only up to 2 choices for now */
typedef struct {
	char *title, *what;
	char *cancel, *ok;
	int have2;
	void **cb, **res;
} alert_dd;

#define WBbase alert_dd
static void *alert_code[] = {
	DIALOGpm(title),
	BORDER(LABEL, 8),
	WLABELp(what),
	WDONE, // vbox
	BORDER(BUTTON, 2),
	REF(cb), CANCELBTNp(cancel, dialog_event),
	IF(have2), BUTTONp(ok, dialog_event),
	RAISED, WDIALOG(res)
};
#undef WBbase

int alert_box(char *title, char *message, char *text1, ...)
{
	alert_dd *dt, tdata = { title, message, _("OK"), NULL, 0 };
	va_list args;
	char *txt;
	void **dd;
	int res, aok = FALSE;

	// Empty string here means, "OK" is ok: no choice but no error
	if (text1 && !(aok = !text1[0]))
	{
		tdata.cancel = text1;
		va_start(args, text1);
		if ((txt = va_arg(args, char *)))
		{
			tdata.ok = txt;
			tdata.have2 = TRUE;
		}
		va_end(args);
	}

	if (cmd_mode) // Console
	{
		console_printf("%s\n[ %s ]\n", __(message),
			__(tdata.ok ? tdata.ok : tdata.cancel));
		res = tdata.ok ? 2 : 1; /* Assume "yes" in commandline mode */
	}
	else // GUI
	{
		update_stuff(CF_NOW);

		dd = run_create(alert_code, &tdata, sizeof(tdata)); // run dialog
		dt = GET_DDATA(dd);
		res = origin_slot(dt->res) == dt->cb ? 1 : 2;
		run_destroy(dd);
	}

	if (aok) res = 2;
	if (res == 1) user_break = TRUE;
	return (res);
}

//	Tablet handling

#if GTK_MAJOR_VERSION == 1
GdkDeviceInfo *tablet_device;
#else /* #if GTK_MAJOR_VERSION >= 2 */
GdkDevice *tablet_device;
#endif

void init_tablet()
{
	GList *devs;
	char *name, buf[64];
	int i, n, mode;

	/* Do nothing if tablet wasn't working the last time */
	if (!inifile_get_gboolean("tablet_USE", FALSE)) return;

	name = inifile_get("tablet_name", "?");
	mode = inifile_get_gint32("tablet_mode", 0);

#if GTK_MAJOR_VERSION == 1
	for (devs = gdk_input_list_devices(); devs; devs = devs->next)
	{
		GdkDeviceInfo *device = devs->data;
		GdkAxisUse *u;

		if (strcmp(device->name, name)) continue;
		/* Found the one that was working the last time */
		tablet_device = device;
		gdk_input_set_mode(device->deviceid, mode);
		n = device->num_axes;
		u = calloc(n, sizeof(*u));
		for (i = 0; i < n; i++)
		{
			sprintf(buf, "tablet_axes_v%d", i);
			u[i] = inifile_get_gint32(buf, GDK_AXIS_IGNORE);
		}
		gdk_input_set_axes(device->deviceid, u);
		free(u);
		break;
	}
#else /* #if GTK_MAJOR_VERSION >= 2 */
#if GTK_MAJOR_VERSION == 3
	devs = gdk_device_manager_list_devices(gdk_display_get_device_manager(
		gdk_display_get_default()), GDK_DEVICE_TYPE_SLAVE);
#else
	devs = gdk_devices_list();
#endif
	for (; devs; devs = devs->next)
	{
		GdkDevice *device = devs->data;

		if (strcmp(gdk_device_get_name(device), name)) continue;
		/* Found the one that was working the last time */
		tablet_device = device;
		gdk_device_set_mode(device, mode);
		n = gdk_device_get_n_axes(device);
		for (i = 0; i < n; i++)
		{
			sprintf(buf, "tablet_axes_v%d", i);
			gdk_device_set_axis_use(device, i,
				inifile_get_gint32(buf, GDK_AXIS_IGNORE));
		}
		break;
	}
#endif

	inifile_set_gboolean("tablet_USE", !!tablet_device);
}

//	TABLETBTN widget

static void **tablet_slot;
static void *tablet_dlg;

#if GTK_MAJOR_VERSION == 3

#define MAX_AXES 128 /* Unlikely to exist, & too long a list to show this many */

typedef struct {
	int dev;
	int mode; // GDK_MODE_DISABLED / GDK_MODE_SCREEN / GDK_MODE_WINDOW
	int ax[7], ax0[7]; // GDK_AXIS_IGNORE .. GDK_AXIS_WHEEL
	int lock;
	char **devnames, **axes;
	GdkDevice **devices;
	char *xtra;
	void **group, **use[7];
} tablet_dd;

#endif

#if GTK_MAJOR_VERSION == 1

static GdkDeviceInfo *tablet_find(gint deviceid)
{
	GList *devs;

	for (devs = gdk_input_list_devices(); devs; devs = devs->next)
	{
		GdkDeviceInfo *device = devs->data;
		if (device->deviceid == deviceid) return (device);
	}
	return (NULL);
}

#endif

void conf_done(void *cause)
{
	char buf[64];
	int i, n;

	if (!tablet_slot) return;

	/* Use last selected device if it's active */
	{
#if GTK_MAJOR_VERSION == 1
		GdkDeviceInfo *dev = tablet_find(GTK_INPUT_DIALOG(tablet_dlg)->current_device);
#elif GTK_MAJOR_VERSION == 2
		GdkDevice *dev = GTK_INPUT_DIALOG(tablet_dlg)->current_device;
#else /* #if GTK_MAJOR_VERSION == 3 */
		tablet_dd *dt = GET_DDATA((void **)tablet_dlg);
		GdkDevice *dev = dt->devices[dt->dev];
#endif
		if (dev && (gdk_device_get_mode(dev) != GDK_MODE_DISABLED))
		{
			tablet_device = dev;
			// Skip event if within do_destroy()
			if (cause) cmd_event(tablet_slot, op_EVT_CHANGE);
		}
	}

	if (tablet_device)
	{
		inifile_set("tablet_name", (char *)gdk_device_get_name(tablet_device));
		inifile_set_gint32("tablet_mode", gdk_device_get_mode(tablet_device));

		n = gdk_device_get_n_axes(tablet_device);
		for (i = 0; i < n; i++)
		{
			sprintf(buf, "tablet_axes_v%d", i);
			inifile_set_gint32(buf,
#if GTK_MAJOR_VERSION == 1
				tablet_device->axes[i]);
#elif GTK_MAJOR_VERSION == 2
				tablet_device->axes[i].use);
#else /* #if GTK_MAJOR_VERSION == 3 */
				gdk_device_get_axis_use(tablet_device, i));
#endif
		}
	}
	inifile_set_gboolean("tablet_USE", !!tablet_device);

#if GTK_MAJOR_VERSION == 3
	run_destroy(tablet_dlg);
#else
	gtk_widget_destroy(tablet_dlg);
#endif
	tablet_slot = NULL;
}

#if GTK_MAJOR_VERSION == 3

static void tablet_changed(tablet_dd *dt, void **wdata, int what, void **where)
{
	int i, n, u, u0, axis, *cause = cmd_read(where, dt);
	GdkDevice *dev = dt->devices[dt->dev];
	GList *ids, *id;


	if (dt->lock) return;
	dt->lock++;

	/* Select device */
	if (cause == &dt->dev)
	{
		dt->mode = gdk_device_get_mode(dev);
		memset(dt->ax, 0, sizeof(dt->ax)); // Clear axis use
		n = gdk_device_get_n_axes(dev);
		ids = id = gdk_device_list_axes(dev);
		if (n > MAX_AXES) n = MAX_AXES; // Paranoia
		for (i = 0; i < n; i++ , id = id->next)
		{
			/* Put axis name into list */
			dt->axes[i + 1] = gdk_atom_name(GDK_POINTER_TO_ATOM(id->data));
			/* Attach to use or not-use */
			u = gdk_device_get_axis_use(dev, i);
			dt->ax[u] = i + 1;
		}
		dt->axes[n + 1] = NULL;
		g_list_free(ids);

		/* Display all that */
		cmd_reset(dt->group, dt);
	}
	/* Change mode */
	else if (cause == &dt->mode)
	{
		if (!gdk_device_set_mode(dev, dt->mode))
			/* Display actual mode if failed to set */
			cmd_set(origin_slot(where), gdk_device_get_mode(dev));
		else /* Report the change */
		{
			tablet_device = dt->mode == GDK_MODE_DISABLED ? NULL : dev;
			cmd_event(tablet_slot, op_EVT_CHANGE);
		}
	}
	/* Change axis use */
	else
	{
		u = cause - dt->ax; // Use
		axis = dt->ax[u];
		u0 = axis ? gdk_device_get_axis_use(dev, axis - 1) : 0;
		if (u0) cmd_set(dt->use[u0], 0); // Steal from other use
		/* Previous axis, if there was one, becomes unused */
		if (dt->ax0[u]) gdk_device_set_axis_use(dev, dt->ax0[u] - 1, 0);
		/* The new one gets new use */
		gdk_device_set_axis_use(dev, axis - 1, u);
	}

	memcpy(dt->ax0, dt->ax, sizeof(dt->ax)); // Save current state

	dt->lock--;
}

#undef _
#define _(X) X

static char *input_modes[3] = { _("Disabled"), _("Screen"), _("Window") };

#define WBbase tablet_dd
static void *tablet_code[] = {
	WINDOW(_("Input")), // nonmodal
	EVENT(DESTROY, conf_done),
	MKSHRINK, // shrinkable
	HBOX,
	MLABEL(_("Device:")), XOPTDe(devnames, dev, tablet_changed), TRIGGER,
	REF(group), GROUPR,
	MLABEL(_("Mode:")), OPTe(input_modes, 3, mode, tablet_changed),
	WDONE,
	BORDER(NBOOK, 0),
	NBOOK,
	BORDER(TABLE, 10),
	PAGE(_("Axes")),
	BORDER(SCROLL, 0),
	WANTMAX, // max size
	XSCROLL(1, 1), // auto/auto
	TABLE2(6),
	REF(use[1]), TOPTDe(_("X:"), axes, ax[1], tablet_changed),
	REF(use[2]), TOPTDe(_("Y:"), axes, ax[2], tablet_changed),
	REF(use[3]), TOPTDe(_("Pressure:"), axes, ax[3], tablet_changed),
	REF(use[4]), TOPTDe(_("X tilt:"), axes, ax[4], tablet_changed),
	REF(use[5]), TOPTDe(_("Y tilt:"), axes, ax[5], tablet_changed),
	REF(use[6]), TOPTDe(_("Wheel:"), axes, ax[6], tablet_changed),
	WDONE, // table
	WDONE, // page
	WDONE, // nbook
	HBOX,
	DEFBORDER(BUTTON),
	OKBTN(_("OK"), conf_done),
	CLEANUP(xtra),
	WSHOW
};
#undef WBbase

#undef _
#define _(X) __(X)

void conf_tablet(void **slot)
{
	tablet_dd tdata;
	GList *devs, *d;
	int n, i;

	if (tablet_slot) return;	// There can be only one
	tablet_slot = slot;

	memset(&tdata, 0, sizeof(tdata));
	devs = gdk_device_manager_list_devices(gdk_display_get_device_manager(
		gdk_display_get_default()), GDK_DEVICE_TYPE_SLAVE);
	n = g_list_length(devs);
	tdata.xtra = multialloc(MA_ALIGN_DEFAULT,
		&tdata.devices, sizeof(GdkDevice *) * n,
		&tdata.devnames, sizeof(char *) * (n + 1),
		&tdata.axes, sizeof(char *) * (MAX_AXES + 1),
		NULL);

	/* Store devices as array */
	devs = g_list_reverse(devs); // To get GTK+2-like ordering
	for (i = 0 , d = devs; d; d = d->next)
	{
		/* Skip keyboards */
		if (gdk_device_get_source(d->data) == GDK_SOURCE_KEYBOARD)
			continue;
		tdata.devices[i] = d->data;
		tdata.devnames[i] = (char *)gdk_device_get_name(d->data);
		i++;
	}
	tdata.devnames[i] = NULL;
	g_list_free(devs);

	tdata.axes[0] = _("none");

	tablet_dlg = run_create(tablet_code, &tdata, sizeof(tdata));
}

#else

/* Use GtkInputDialog on GTK+1&2 */
#if GTK_MAJOR_VERSION == 1

static void tablet_toggle(GtkInputDialog *inputdialog, gint deviceid,
	gpointer user_data)
{
	GdkDeviceInfo *dev = tablet_find(deviceid);
	tablet_device = !dev || (dev->mode == GDK_MODE_DISABLED) ? NULL : dev;
	cmd_event(user_data, op_EVT_CHANGE);
}

#else /* #if GTK_MAJOR_VERSION == 2 */

static void tablet_toggle(GtkInputDialog *inputdialog, GdkDevice *deviceid,
	gpointer user_data)
{
	tablet_device = gdk_device_get_mode(deviceid) == GDK_MODE_DISABLED ?
		NULL : deviceid;
	cmd_event(user_data, op_EVT_CHANGE);
}

#endif

static gboolean conf_del(GtkWidget *widget)
{
	conf_done(widget);
	return (TRUE);
}

void conf_tablet(void **slot)
{
	GtkWidget *inputd;
	GtkInputDialog *inp;
	GtkAccelGroup *ag;

	if (tablet_slot) return;	// There can be only one
	tablet_slot = slot;
	tablet_dlg = inputd = gtk_input_dialog_new();
	gtk_window_set_position(GTK_WINDOW(inputd), GTK_WIN_POS_CENTER);
	inp = GTK_INPUT_DIALOG(inputd);

	ag = gtk_accel_group_new();
	gtk_signal_connect(GTK_OBJECT(inp->close_button), "clicked",
		GTK_SIGNAL_FUNC(conf_done), NULL);
	gtk_widget_add_accelerator(inp->close_button, "clicked", ag,
		GDK_Escape, 0, (GtkAccelFlags)0);
	gtk_signal_connect(GTK_OBJECT(inputd), "delete_event",
		GTK_SIGNAL_FUNC(conf_del), NULL);

	gtk_signal_connect(GTK_OBJECT(inputd), "enable-device",
		GTK_SIGNAL_FUNC(tablet_toggle), slot);
	gtk_signal_connect(GTK_OBJECT(inputd), "disable-device",
		GTK_SIGNAL_FUNC(tablet_toggle), slot);

	if (inp->keys_list) gtk_widget_hide(inp->keys_list);
	if (inp->keys_listbox) gtk_widget_hide(inp->keys_listbox);
	gtk_widget_hide(inp->save_button);

	gtk_window_add_accel_group(GTK_WINDOW(inputd), ag);
	gtk_widget_show(inputd);
}

#endif /* GTK+1&2 */

// Slider-spin combo (a decorated spinbutton)

GtkWidget *mt_spinslide_new(int swidth, int sheight)
{
	GtkWidget *box, *slider, *spin;
	GtkAdjustment_t *adj = gtk_adjustment_new(0, 0, 1, 1, 10, 0);
#if GTK_MAJOR_VERSION == 3
	box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);

	slider = gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, GTK_ADJUSTMENT(adj));
	gtk_widget_set_size_request(slider, swidth, sheight);
#else
	box = gtk_hbox_new(FALSE, 0);

	slider = gtk_hscale_new(GTK_ADJUSTMENT(adj));
	gtk_widget_set_usize(slider, swidth, sheight);
#endif
	gtk_box_pack_start(GTK_BOX(box), slider, swidth < 0, TRUE, 0);
	gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);
	gtk_scale_set_digits(GTK_SCALE(slider), 0);

	spin = spin_new_x(adj, 0);
	gtk_box_pack_start(GTK_BOX(box), spin, swidth >= 0, TRUE, 2);

	gtk_widget_show_all(box);
	return (spin);
}

// GTK+3 specific support code

#if GTK_MAJOR_VERSION == 3

void cairo_surface_fdestroy(cairo_surface_t *s)
{
	cairo_surface_flush(s); // In case it's actually needed
	cairo_surface_destroy(s);
}

cairo_surface_t *cairo_upload_rgb(cairo_surface_t *ref, GdkWindow *win,
	unsigned char *src, int w, int h, int len)
{
	cairo_surface_t *s;
	unsigned char *dst0;
	int i, n, st;

	if (ref) s = cairo_surface_create_similar_image(ref, CAIRO_FORMAT_RGB24, w, h);
	else s = gdk_window_create_similar_image_surface(win, CAIRO_FORMAT_RGB24, w, h, 1);
// !!! See below CAIRO_FORMAT_ARGB32 // !!! But not for exported pixmap
	cairo_surface_flush(s);
	dst0 = cairo_image_surface_get_data(s);
	st = cairo_image_surface_get_stride(s);
	len -= w * 3;
	for (i = h; i-- > 0; dst0 += st , src += len)
	{
		guint32 *dest = (void *)dst0;
		for (n = w; n-- > 0; src += 3) *dest++ = MEM_2_INT(src, 0);
// !!! Maybe need to upconvert to ARGB (by OR 0xFF000000U), at least for Windows:
// see Cairo win32/cairo-win32-display-surface.c
//		for (n = w; n-- > 0; src += 3) *dest++ = MEM_2_INT(src, 0) | 0xFF000000U;
	}
	cairo_surface_mark_dirty(s);
	return (s);
}

void cairo_set_rgb(cairo_t *cr, int c)
{
	cairo_set_source_rgb(cr, INT_2_R(c) / 255.0, INT_2_G(c) / 255.0,
		INT_2_B(c) / 255.0);
}

/* Prevent color bleed on HiDPI */
void cairo_unfilter(cairo_t *cr)
{
	cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_NEAREST);
}

void css_restyle(GtkWidget *w, char *css, char *class, char *name)
{
	static GData *d;
	static int dset;
	GQuark q = g_quark_from_string(css);
	GtkCssProvider *p;
	GtkStyleContext *c;

	if (!dset) g_datalist_init(&d); // First time
	dset = TRUE;
	p = g_datalist_id_get_data(&d, q);
	if (!p)
	{
		p = gtk_css_provider_new();
		gtk_css_provider_load_from_data(p, css, -1, NULL);
		g_datalist_id_set_data(&d, q, p);
	}
	c = gtk_widget_get_style_context(w);
	gtk_style_context_add_provider(c, GTK_STYLE_PROVIDER(p),
		GTK_STYLE_PROVIDER_PRIORITY_USER);
	if (class) gtk_style_context_add_class(c, class);
	if (name) g_object_set(w, "name", name, NULL);
}

void add_css_class(GtkWidget *w, char *class)
{
	gtk_style_context_add_class(gtk_widget_get_style_context(w), class);
}

/* Add CSS, builtin and user-provided, to default screen */
void init_css(char *cssfile)
{
	GdkScreen *scr = gdk_display_get_default_screen(gdk_display_get_default());
	GtkCssProvider *p;
	GtkIconTheme *theme;
	char *s, *cp = NULL;

	/* GTK+ 3.20 switched from classes to "CSS nodes", and added "min-width" &
	 * "min-height" style properties, themed to some crazy values - WJ */
	if (gtk3version >= 20) // Make entries same height as buttons
	{
		GtkWidget *btn = gtk_button_new();
		GtkStyleContext *ctx = gtk_widget_get_style_context(btn);
		gint n;

		gtk_style_context_get(ctx, gtk_style_context_get_state(ctx),
			"min-height", &n, NULL);
		cp = g_strdup_printf("spinbutton, entry { min-height:%dpx; }", n);
		g_object_ref_sink(btn);
		g_object_unref(btn);
	}
	s = g_strconcat((gtk3version < 20 ? ".spinbutton *,.grid-child" :
		"spinbutton button,flowboxchild"),
		" { padding:0; }"
		".image-button { padding:4px; }"
		".wjpixmap { padding:4px; outline-offset:-1px; }"
		".mtPaint_gradbar_button { padding:4px; }",
		(gtk3version < 20 ? "" :
		".mtPaint_gradbar_button { min-width:0; min-height:0; }"),
		cp, NULL);
	p = gtk_css_provider_new();
	gtk_css_provider_load_from_data(p, s, -1, NULL);
	gtk_style_context_add_provider_for_screen(scr, GTK_STYLE_PROVIDER(p),
		GTK_STYLE_PROVIDER_PRIORITY_USER);
	g_free(s);
	g_free(cp);

	if (!cssfile || !cssfile[0]) return;

	/* Load user-provided CSS */
	p = gtk_css_provider_new();
	gtk_css_provider_load_from_path(p, cssfile, NULL);
	gtk_style_context_add_provider_for_screen(scr, GTK_STYLE_PROVIDER(p),
		GTK_STYLE_PROVIDER_PRIORITY_USER + 100);

	/* Make the directory with it the first search dir for icons */
	s = resolve_path(NULL, 0, cssfile);
	cp = strrchr(s, DIR_SEP);
	theme = gtk_icon_theme_get_for_screen(scr);
	if (cp && theme) // Paranoia
	{
		*cp = '\0'; // Cut off filename
		gtk_icon_theme_prepend_search_path(theme, s);
	}
	free(s);
}

static void combobox_scan(GtkWidget *widget, gpointer data)
{
	GtkWidget **scan = data;
	if (GTK_IS_BOX(widget)) scan[1] = widget;
	else if (GTK_IS_BUTTON(widget)) scan[0] = widget;
}

/* Find button widget of a GtkComboBox with entry */
GtkWidget *combobox_button(GtkWidget *cbox)
{
	GtkWidget *scan[2] = { NULL, NULL };
	gtk_container_forall(GTK_CONTAINER(cbox), combobox_scan, scan);
	if (!scan[0] && scan[1]) // Structure changed after 3.18
		gtk_container_forall(GTK_CONTAINER(scan[1]), combobox_scan, scan);
	return (scan[0]);
}

static GQuark radio_key;

/* Properties for GtkScrollable */
static char *scroll_pnames[] = { NULL, "hadjustment", "vadjustment",
	"hscroll-policy", "vscroll-policy" };
enum {
	P_HADJ = 1,
	P_VADJ,
	P_HSCP,
	P_VSCP
};

static void get_padding_and_border(GtkStyleContext *ctx, GtkBorder *pad,
	GtkBorder *bor, GtkBorder *both)
{
	GtkStateFlags state = gtk_style_context_get_state(ctx);
	GtkBorder tmp;

	if (both)
	{
		if (!pad) pad = &tmp;
		if (!bor) bor = both;
	}
	if (pad) gtk_style_context_get_padding(ctx, state, pad); // ~ xthickness
	if (bor) gtk_style_context_get_border(ctx, state, bor);
	if (both)
	{
		both->left = pad->left + bor->left;
		both->right = pad->right + bor->right;
		both->top = pad->top + bor->top;
		both->bottom = pad->bottom + bor->bottom;
	}
}

#endif

// Managing batches of radio buttons with minimum of fuss

/* void handler(GtkWidget *btn, gpointer user_data); */
GtkWidget *wj_radio_pack(char **names, int cnt, int vnum, int idx, void **r,
	GtkSignalFunc handler)
{
	int i, j, x;
	GtkWidget *table, *button = NULL;

#if GTK_MAJOR_VERSION == 3
	radio_key = g_quark_from_static_string("mtPaint.radio");
	table = gtk_grid_new();
	gtk_widget_set_hexpand(table, FALSE); // No "inheriting" it from buttons
#else
	table = gtk_table_new(1, 1, FALSE);
#endif
	for (i = j = x = 0; (i != cnt) && names[i]; i++)
	{
		if (!names[i][0]) continue;
		button = gtk_radio_button_new_with_label_from_widget(
			GTK_RADIO_BUTTON_0(button), __(names[i]));
		if (vnum > 0) x = j / vnum;
#if GTK_MAJOR_VERSION == 3
		g_object_set_qdata(G_OBJECT(button), radio_key, (gpointer)i);
		/* Adjusted to account for GTK+3 adding more padding */
		gtk_container_set_border_width(GTK_CONTAINER(button), 2);
		gtk_grid_attach(GTK_GRID(table), button, x, j - x * vnum, 1, 1);
		if (vnum != 1) gtk_widget_set_hexpand(button, TRUE);
#else
		gtk_object_set_user_data(GTK_OBJECT(button), (gpointer)i);
		gtk_container_set_border_width(GTK_CONTAINER(button), 5);
		gtk_table_attach(GTK_TABLE(table), button, x, x + 1,
			j - x * vnum, j - x * vnum + 1,
			vnum != 1 ? GTK_EXPAND | GTK_FILL : GTK_FILL, 0, 0, 0);
#endif
		if (i == idx) gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(button), TRUE);
		if (handler) gtk_signal_connect(GTK_OBJECT(button), "toggled",
			handler, r);
		j++;
	}
	gtk_widget_show_all(table);

	return (table);
}

#if GTK_MAJOR_VERSION == 3

int wj_radio_pack_get_active(GtkWidget *widget)
{
	GList *curr, *ch = gtk_container_get_children(GTK_CONTAINER(widget));
	int res = 0;

	for (curr = ch; curr; curr = curr->next)
	{
		widget = curr->data;
		if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) continue;
		res = (int)g_object_get_qdata(G_OBJECT(widget), radio_key);
		break;
	}
	g_list_free(ch);
	return (res);
}

#else

int wj_radio_pack_get_active(GtkWidget *widget)
{
	GList *curr;

	for (curr = GTK_TABLE(widget)->children; curr; curr = curr->next)
	{
		widget = ((GtkTableChild *)curr->data)->widget;
		if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
			return ((int)gtk_object_get_user_data(GTK_OBJECT(widget)));
	}
	return (0);
}

#endif

// Easier way with spinbuttons

int read_spin(GtkWidget *spin)
{
	/* Needed in GTK+2 for late changes */
	gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
	return (gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)));
}

double read_float_spin(GtkWidget *spin)
{
	/* Needed in GTK+2 for late changes */
	gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
#if GTK_MAJOR_VERSION == 3
	return (gtk_spin_button_get_value(GTK_SPIN_BUTTON(spin)));
#else
	return (GTK_SPIN_BUTTON(spin)->adjustment->value);
#endif
}

#if (GTK_MAJOR_VERSION == 1) && !defined(U_MTK)

#define MIN_SPIN_BUTTON_WIDTH 30

/* More-or-less correctly evaluate spinbutton size */
static void spin_size_req(GtkWidget *widget, GtkRequisition *requisition,
	gpointer user_data)
{
	GtkSpinButton *spin = GTK_SPIN_BUTTON(widget);
	char num[128];
	int l1, l2;

	num[0] = '0';
	sprintf(num + 1, "%.*f", spin->digits, spin->adjustment->lower);
	l1 = gdk_string_width(widget->style->font, num);
	sprintf(num + 1, "%.*f", spin->digits, spin->adjustment->upper);
	l2 = gdk_string_width(widget->style->font, num);
	if (l1 < l2) l1 = l2;
	if (l1 > MIN_SPIN_BUTTON_WIDTH)
		requisition->width += l1 - MIN_SPIN_BUTTON_WIDTH;
}

#endif

static GtkWidget *spin_new_x(GtkAdjustment_t *adj, int fpart)
{
	GtkWidget *spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, fpart);
#if (GTK_MAJOR_VERSION == 1) && !defined(U_MTK)
	gtk_signal_connect(GTK_OBJECT(spin), "size_request",
		GTK_SIGNAL_FUNC(spin_size_req), NULL);
#endif
	gtk_widget_show(spin);
	gtk_spin_button_set_numeric(GTK_SPIN_BUTTON(spin), TRUE);
	return (spin);
}

GtkWidget *add_float_spin(double value, double min, double max)
{
	return (spin_new_x(gtk_adjustment_new(value, min, max, 1, 10, 0), 2));
}

/* void handler(GtkAdjustment *adjustment, gpointer user_data); */
void spin_connect(GtkWidget *spin, GtkSignalFunc handler, gpointer user_data)
{
	GtkAdjustment *adj;
	
	adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin));
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed", handler, user_data);
}

#if GTK_MAJOR_VERSION == 1

void spin_set_range(GtkWidget *spin, int min, int max)
{
	GtkAdjustment *adj = GTK_SPIN_BUTTON(spin)->adjustment;

	adj->lower = min;
	adj->upper = max;
	gtk_adjustment_set_value(adj, adj->value);
	gtk_adjustment_changed(adj);
}

#endif

// Wrapper for utf8->C and C->utf8 translation

char *gtkxncpy(char *dest, const char *src, int cnt, int u)
{
#if GTK_MAJOR_VERSION >= 2
	char *c = (u ? g_locale_to_utf8 : g_locale_from_utf8)((gchar *)src, -1,
		NULL, NULL, NULL);
	if (c)
	{
		if (!dest) return (c);
		g_strlcpy(dest, c, cnt);
		g_free(c);
	}
	else
#endif
	{
		if (!dest) return (g_strdup(src));
		u = strlen(src);
		if (u >= cnt) u = cnt - 1;
		/* Allow for overlapping buffers */
		memmove(dest, src, u);
		dest[u] = 0;
	}
	return (dest);
}

// A more sane replacement for strncat()

char *strnncat(char *dest, const char *src, int max)
{
	int l = strlen(dest);
	if (max > l) strncpy(dest + l, src, max - l - 1);
	dest[max - 1] = 0;
	return (dest);
}

// Add C strings to a string with explicit length

char *wjstrcat(char *dest, int max, const char *s0, int l, ...)
{
	va_list args;
	char *s, *w;
	int ll;

	if (!dest)
	{
		max = l + 1;
		va_start(args, l);
		while ((s = va_arg(args, char *))) max += strlen(s);
		va_end(args);
		dest = malloc(max);
		if (!dest) return (NULL);
	}

	va_start(args, l);
	w = dest;
	s = (char *)s0; ll = l;
	while (TRUE)
	{
		if (ll >= max) ll = max - 1;
		memcpy(w, s, ll);
		w += ll;
		if ((max -= ll) <= 1) break;
		s = va_arg(args, char *);
		if (!s) break;
		ll = strlen(s);
	}
	va_end(args);
	*w = 0;
	return (dest);
}

// Add directory to filename

char *file_in_dir(char *dest, const char *dir, const char *file, int cnt)
{
	int dl = strlen(dir);
	return wjstrcat(dest, cnt, dir, dl - (dir[dl - !!dl] == DIR_SEP),
		DIR_SEP_STR, file, NULL);
}

char *file_in_homedir(char *dest, const char *file, int cnt)
{
	return (file_in_dir(dest, get_home_directory(), file, cnt));
}

#if GTK_MAJOR_VERSION <= 2

// Set minimum size for a widget

static void widget_size_req(GtkWidget *widget, GtkRequisition *requisition,
	gpointer user_data)
{
	int h = (guint32)user_data >> 16, w = (guint32)user_data & 0xFFFF;

	if (h && (requisition->height < h)) requisition->height = h;
	if (w && (requisition->width < w)) requisition->width = w;
}

/* !!! Warning: this function can't extend box containers in their "natural"
 * direction, because GTK+ takes shortcuts with their allocation, abusing
 * requisition value. */
void widget_set_minsize(GtkWidget *widget, int width, int height)
{
	guint32 hw;

	if ((width <= 0) && (height <= 0)) return;

	hw = (height < 0 ? 0 : height & 0xFFFF) << 16 |
		(width < 0 ? 0 : width & 0xFFFF);
	gtk_signal_connect(GTK_OBJECT(widget), "size_request",
		GTK_SIGNAL_FUNC(widget_size_req), (gpointer)hw);
}

/* This function is a workaround for boxes and the like, wrapping a widget in a
 * GtkAlignment and setting size on that - or it can be seen as GtkAlignment
 * widget with extended functionality - WJ */
GtkWidget *widget_align_minsize(GtkWidget *widget, int width, int height)
{
	GtkWidget *align = gtk_alignment_new(0.5, 0.5, 1.0, 1.0);
	gtk_widget_show(align);
	gtk_container_add(GTK_CONTAINER(align), widget);
	widget_set_minsize(align, width, height);
	return (align);
}

#endif

// Make widget request no less size than before (in one direction)

#if GTK_MAJOR_VERSION == 3

static void widget_size_keep(GtkWidget *widget, GtkAllocation *alloc,
	gpointer user_data)
{
	gint w, h, w0, h0;

	gtk_widget_get_size_request(widget, &w0, &h0);
	if (user_data) // Adjust height if set, width if clear
	{
		gtk_widget_get_preferred_height(widget, &h, NULL);
		h -= gtk_widget_get_margin_top(widget) +
			gtk_widget_get_margin_bottom(widget);
		if (h0 >= h) return;
		h0 = h;
	}
	else
	{
		gtk_widget_get_preferred_width(widget, &w, NULL);
		w -= gtk_widget_get_margin_start(widget) +
			gtk_widget_get_margin_end(widget);
		if (w0 >= w) return;
		w0 = w;
	}
	gtk_widget_set_size_request(widget, w0, h0);
}

void widget_set_keepsize(GtkWidget *widget, int keep_height)
{
	g_signal_connect(widget, "size_allocate",
		G_CALLBACK(widget_size_keep), (gpointer)keep_height);
}

#else /* #if GTK_MAJOR_VERSION <= 2 */

#define KEEPSIZE_KEY "mtPaint.keepsize"

static GQuark keepsize_key;

/* And if user manages to change theme on the fly... well, more fool him ;-) */
static void widget_size_keep(GtkWidget *widget, GtkRequisition *requisition,
	gpointer user_data)
{
	int l, l0;

	l = (int)gtk_object_get_data_by_id(GTK_OBJECT(widget), keepsize_key);
	if (user_data) // Adjust height if set, width if clear
	{
		if ((l0 = requisition->height) < l) requisition->height = l;
	}
	else if ((l0 = requisition->width) < l) requisition->width = l;

	if (l0 > l) gtk_object_set_data_by_id(GTK_OBJECT(widget), keepsize_key,
		(gpointer)l0);
}

/* !!! Warning: this function can't extend box containers in their "natural"
 * direction, because GTK+ takes shortcuts with their allocation, abusing
 * requisition value. */
void widget_set_keepsize(GtkWidget *widget, int keep_height)
{
	if (!keepsize_key) keepsize_key = g_quark_from_static_string(KEEPSIZE_KEY);
	gtk_signal_connect(GTK_OBJECT(widget), "size_request",
		GTK_SIGNAL_FUNC(widget_size_keep), (gpointer)keep_height);
}

#endif

// Workaround for GtkCList reordering bug

/* This bug is the favorite pet of GNOME developer Behdad Esfahbod
 * See http://bugzilla.gnome.org/show_bug.cgi?id=400249#c2 */

#if GTK_MAJOR_VERSION == 2

static void clist_drag_fix(GtkWidget *widget, GdkDragContext *drag_context,
	gpointer user_data)
{
	g_dataset_remove_data(drag_context, "gtk-clist-drag-source");
}

void clist_enable_drag(GtkWidget *clist)
{
	gtk_signal_connect(GTK_OBJECT(clist), "drag_begin",
		GTK_SIGNAL_FUNC(clist_drag_fix), NULL);
	gtk_signal_connect(GTK_OBJECT(clist), "drag_end",
		GTK_SIGNAL_FUNC(clist_drag_fix), NULL);
	gtk_clist_set_reorderable(GTK_CLIST(clist), TRUE);
}

#elif GTK_MAJOR_VERSION == 1 /* GTK1 doesn't have this bug */

void clist_enable_drag(GtkWidget *clist)
{
	gtk_clist_set_reorderable(GTK_CLIST(clist), TRUE);
}

#endif

// Most common use of boxes

GtkWidget *pack(GtkWidget *box, GtkWidget *widget)
{
	gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 0);
	return (widget);
}

GtkWidget *xpack(GtkWidget *box, GtkWidget *widget)
{
	gtk_box_pack_start(GTK_BOX(box), widget, TRUE, TRUE, 0);
	return (widget);
}

GtkWidget *pack_end(GtkWidget *box, GtkWidget *widget)
{
	gtk_box_pack_end(GTK_BOX(box), widget, FALSE, FALSE, 0);
	return (widget);
}

// Put vbox into container

GtkWidget *add_vbox(GtkWidget *cont)
{
#if GTK_MAJOR_VERSION == 3
	GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
#else
	GtkWidget *box = gtk_vbox_new(FALSE, 0);
#endif
	gtk_widget_show(box);
	gtk_container_add(GTK_CONTAINER(cont), box);
	return (box);
}

// Fix for paned widgets losing focus in GTK+1

#if GTK_MAJOR_VERSION == 1

static void fix_gdk_events(GdkWindow *window)
{
	XWindowAttributes attrs;
	GdkWindowPrivate *private = (GdkWindowPrivate *)window;

	if (!private || private->destroyed) return;
	XGetWindowAttributes(GDK_WINDOW_XDISPLAY(window), private->xwindow, &attrs);
	XSelectInput(GDK_WINDOW_XDISPLAY(window), private->xwindow,
		attrs.your_event_mask & ~OwnerGrabButtonMask);
}

static void paned_realize(GtkWidget *widget, gpointer user_data)
{
	fix_gdk_events(widget->window);
	fix_gdk_events(GTK_PANED(widget)->handle);
}

void paned_mouse_fix(GtkWidget *widget)
{
	gtk_signal_connect(GTK_OBJECT(widget), "realize",
		GTK_SIGNAL_FUNC(paned_realize), NULL);
}

#endif

// Init-time bugfixes

/* Bugs: GtkViewport size request in GTK+1; GtkHScale breakage in Smooth Theme
 * Engine in GTK+1; GtkListItem and GtkCList stealing Esc key in GTK+1; GtkEntry
 * and GtkSpinButton stealing Enter key and mishandling keypad Enter key in
 * GTK+1; mixing up keys in GTK+2/Windows; opaque focus rectangle in Gtk-Qt theme
 * engine (v0.8) in GTK+2/X */

#if GTK_MAJOR_VERSION == 1

/* This is gtk_viewport_size_request() from GTK+ 1.2.10 with stupid bugs fixed */
static void gtk_viewport_size_request_fixed(GtkWidget *widget,
	GtkRequisition *requisition)
{
	GtkBin *bin;
	GtkRequisition child_requisition;

	g_return_if_fail(widget != NULL);
	g_return_if_fail(GTK_IS_VIEWPORT(widget));
	g_return_if_fail(requisition != NULL);

	bin = GTK_BIN(widget);

	requisition->width = requisition->height =
		GTK_CONTAINER(widget)->border_width * 2;

	if (GTK_VIEWPORT(widget)->shadow_type != GTK_SHADOW_NONE)
	{
		requisition->width += widget->style->klass->xthickness * 2;
		requisition->height += widget->style->klass->ythickness * 2;
	}

	if (bin->child && GTK_WIDGET_VISIBLE(bin->child))
	{
		gtk_widget_size_request(bin->child, &child_requisition);
		requisition->width += child_requisition.width;
		requisition->height += child_requisition.height;
	}
}

/* This is for preventing Smooth Engine from ruining horizontal sliders */
static void (*hsizereq)(GtkWidget *widget, GtkRequisition *requisition);
static void gtk_hscale_size_request_smooth_fixed(GtkWidget *widget,
	GtkRequisition *requisition)
{
	int realf = GTK_WIDGET_FLAGS(widget) & GTK_REALIZED;
	GTK_WIDGET_UNSET_FLAGS(widget, GTK_REALIZED);
	hsizereq(widget, requisition);
	GTK_WIDGET_SET_FLAGS(widget, realf);
}

typedef struct {
  GtkThemeEngine engine;
  
  void *library;
  void *name;

  void (*init) (GtkThemeEngine *);
  void (*exit) (void);

  guint refcount;
} GtkThemeEnginePrivate;

/* This is for routing Enter keys around GtkEntry's default handler */
static gboolean (*ekeypress)(GtkWidget *widget, GdkEventKey *event);
static gboolean gtk_entry_key_press_fixed(GtkWidget *widget, GdkEventKey *event)
{
	if (event && ((event->keyval == GDK_Return) ||
		(event->keyval == GDK_KP_Enter))) return (FALSE);
	return (ekeypress(widget, event));
}

void gtk_init_bugfixes()
{
	GtkWidget *hs;
	GtkStyle *st;
	GtkWidgetClass *wc;
	char *engine = "";


	((GtkWidgetClass*)gtk_type_class(GTK_TYPE_VIEWPORT))->size_request =
		gtk_viewport_size_request_fixed;

	/* Detect if Smooth Engine is active, and fix its bugs */
	st = gtk_rc_get_style(hs = gtk_hscale_new(NULL));
	if (st && st->engine)
		engine = ((GtkThemeEnginePrivate *)(st->engine))->name;
	if (!strcmp(engine, "smooth"))
	{
		wc = gtk_type_class(GTK_TYPE_HSCALE);
		hsizereq = wc->size_request;
		wc->size_request = gtk_hscale_size_request_smooth_fixed;
	}
	gtk_object_sink(GTK_OBJECT(hs)); /* Destroy a floating-ref thing */

	gtk_binding_entry_remove(gtk_binding_set_by_class(gtk_type_class(
		GTK_TYPE_CLIST)), GDK_Escape, 0);
	gtk_binding_entry_remove(gtk_binding_set_by_class(gtk_type_class(
		GTK_TYPE_LIST_ITEM)), GDK_Escape, 0);

	wc = gtk_type_class(GTK_TYPE_ENTRY);
	ekeypress = wc->key_press_event;
	wc->key_press_event = gtk_entry_key_press_fixed;
}

#elif GTK_MAJOR_VERSION == 2
#if defined GDK_WINDOWING_WIN32

static int win_last_vk;
static guint32 win_last_lp;

/* Event filter to look at WM_KEYDOWN and WM_SYSKEYDOWN */
static GdkFilterReturn win_keys_peek(GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
	MSG *msg = xevent;

	if ((msg->message == WM_KEYDOWN) || (msg->message == WM_SYSKEYDOWN))
	{
		/* No matter that these fields are longer in Win64 */
		win_last_vk = msg->wParam;
		win_last_lp = msg->lParam;
	}
	return (GDK_FILTER_CONTINUE);
}

#elif defined GDK_WINDOWING_X11

/* Gtk-Qt's author was deluded when he decided he knows how to draw focus;
 * doing nothing is *FAR* preferable to opaque box over a widget - WJ */
static void fake_draw_focus()
{
	return;
}

#endif

static void do_shutup(const gchar *log_domain, GLogLevelFlags log_level,
	const gchar *message, gpointer user_data)
{
	char *s = "Invalid UTF-8"; // The words to never utter
	if (strncmp(message, s, strlen(s))) g_log_default_handler(log_domain,
		log_level, message, user_data);
}

void gtk_init_bugfixes()
{
#if defined GDK_WINDOWING_WIN32
	gdk_window_add_filter(NULL, (GdkFilterFunc)win_keys_peek, NULL);
#elif defined GDK_WINDOWING_X11
	GtkWidget *bt;
	GtkStyleClass *sc;
	GType qtt;


	/* Detect if Gtk-Qt engine is active, and fix its bugs */
	bt = gtk_button_new();
	qtt = g_type_from_name("QtEngineStyle");
	if (qtt)
	{
		sc = g_type_class_ref(qtt); /* Have to ref to get it to init */
		sc->draw_focus = fake_draw_focus;
	}
	gtk_object_sink(GTK_OBJECT(bt)); /* Destroy a floating-ref thing */
#endif /* X11 */
	/* Cut spam from Pango about bad UTF8 */
	g_log_set_handler("Pango", G_LOG_LEVEL_WARNING, (GLogFunc)do_shutup, NULL);

#ifndef U_LISTS_GTK1
	/* Remove crazier keybindings from GtkTreeView */
	{
		GtkBindingSet *bs = gtk_binding_set_by_class(g_type_class_ref(
			GTK_TYPE_TREE_VIEW));
		gtk_binding_entry_remove(bs, GDK_space, 0); // Activate
		gtk_binding_entry_remove(bs, GDK_KP_Space, 0);
		gtk_binding_entry_remove(bs, GDK_f, GDK_CONTROL_MASK); // Search
		gtk_binding_entry_remove(bs, GDK_F, GDK_CONTROL_MASK);
		gtk_binding_entry_remove(bs, GDK_p, GDK_CONTROL_MASK); // Up
		gtk_binding_entry_remove(bs, GDK_n, GDK_CONTROL_MASK); // Down
	}
#endif
}

#else /* if GTK_MAJOR_VERSION == 3 */

#if defined GDK_WINDOWING_WIN32
#error "GTK+3/Win32 not supported yet"
#endif

int gtk3version;

void gtk_init_bugfixes()
{
	GtkContainerClass *c;
	GtkBindingSet *bs;

	gtk3version = gtk_get_minor_version();

	if (gtk3version < 20)
	{
		/* Fix counting border width twice */
		c = g_type_class_ref(GTK_TYPE_RADIO_BUTTON);
		c->_handle_border_width = 0;
		c = g_type_class_ref(GTK_TYPE_CHECK_BUTTON);
		c->_handle_border_width = 0;
/* !!! The wrong idea is introduced at GTK_CHECK_BUTTON level; descendants of
 * GTK_BUTTON should let gtk_container_class_handle_border_width() do its thing,
 * not add gtk_container_get_border_width() to size by themselves. */
	}

	/* Remove crazier keybindings from GtkTreeView */
	bs = gtk_binding_set_by_class(g_type_class_ref(GTK_TYPE_TREE_VIEW));
	gtk_binding_entry_remove(bs, KEY(space), 0); // Activate
	gtk_binding_entry_remove(bs, KEY(KP_Space), 0);
	gtk_binding_entry_remove(bs, KEY(f), GDK_CONTROL_MASK); // Search
	gtk_binding_entry_remove(bs, KEY(F), GDK_CONTROL_MASK);
}

#endif /* GTK+3 */

// Whatever is needed to move mouse pointer 

#if GTK_MAJOR_VERSION == 3

int move_mouse_relative(int dx, int dy)
{
	gint x0, y0;
	GdkScreen *screen;
	GdkDisplay *dp = gtk_widget_get_display(main_window);
	GdkDevice *dev;

	dev = gdk_device_manager_get_client_pointer(gdk_display_get_device_manager(dp));

	gdk_device_get_position(dev, &screen, &x0, &y0);
	gdk_device_warp(dev, screen, x0 + dx, y0 + dy);
	return (TRUE);
}

#elif (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11 /* Call X */

int move_mouse_relative(int dx, int dy)
{
	XWarpPointer(GDK_WINDOW_XDISPLAY(main_window->window),
		None, None, 0, 0, 0, 0, dx, dy);
	return (TRUE);
}

#elif defined GDK_WINDOWING_WIN32 /* Call GDI */

int move_mouse_relative(int dx, int dy)
{
	POINT point;
	if (GetCursorPos(&point))
	{
		SetCursorPos(point.x + dx, point.y + dy);
		return (TRUE);
	}
	else return (FALSE);
}

#elif GTK2VERSION >= 8 /* GTK+ 2.8+ */

int move_mouse_relative(int dx, int dy)
{
	gint x0, y0;
	GdkScreen *screen;
	GdkDisplay *display = gtk_widget_get_display(main_window);

	gdk_display_get_pointer(display, &screen, &x0, &y0, NULL);
	gdk_display_warp_pointer(display, screen, x0 + dx, y0 + dy);
	return (TRUE);
}

#else /* Always fail */

int move_mouse_relative(int dx, int dy)
{
	return (FALSE);
}

#endif

// Whatever is needed to map keyval to key

#if GTK_MAJOR_VERSION == 1 /* Call X */

guint real_key(GdkEventKey *event)
{
	return (XKeysymToKeycode(GDK_WINDOW_XDISPLAY(main_window->window),
		event->keyval));
}

guint low_key(GdkEventKey *event)
{
	return (gdk_keyval_to_lower(event->keyval));
}

guint keyval_key(guint keyval)
{
	return (XKeysymToKeycode(GDK_WINDOW_XDISPLAY(main_window->window),
		keyval));
}

#else /* Use GDK */

guint real_key(GdkEventKey *event)
{
	return (event->hardware_keycode);
}

#ifdef GDK_WINDOWING_WIN32

/* Keypad translation helpers */
static unsigned char keypad_vk[] = {
	VK_CLEAR, VK_PRIOR, VK_NEXT, VK_END, VK_HOME,
	VK_LEFT, VK_UP, VK_RIGHT, VK_DOWN, VK_INSERT, VK_DELETE,
	VK_NUMPAD0, VK_NUMPAD1, VK_NUMPAD2, VK_NUMPAD3, VK_NUMPAD4,
	VK_NUMPAD5, VK_NUMPAD6, VK_NUMPAD7, VK_NUMPAD8, VK_NUMPAD9,
	VK_DECIMAL, 0
};
static unsigned short keypad_wgtk[] = {
	GDK_Clear, GDK_Page_Up, GDK_Page_Down, GDK_End, GDK_Home,
	GDK_Left, GDK_Up, GDK_Right, GDK_Down, GDK_Insert, GDK_Delete,
	GDK_0, GDK_1, GDK_2, GDK_3, GDK_4,
	GDK_5, GDK_6, GDK_7, GDK_8, GDK_9,
	GDK_period, 0
};
static unsigned short keypad_xgtk[] = {
	GDK_KP_Begin, GDK_KP_Page_Up, GDK_KP_Page_Down, GDK_KP_End, GDK_KP_Home,
	GDK_KP_Left, GDK_KP_Up, GDK_KP_Right, GDK_KP_Down, GDK_KP_Insert, GDK_KP_Delete,
	GDK_KP_0, GDK_KP_1, GDK_KP_2, GDK_KP_3, GDK_KP_4,
	GDK_KP_5, GDK_KP_6, GDK_KP_7, GDK_KP_8, GDK_KP_9,
	GDK_KP_Decimal, 0
};

guint low_key(GdkEventKey *event)
{
	/* Augment braindead GDK translation by recognizing keypad keys */
	if (win_last_vk == event->hardware_keycode) /* Paranoia */
	{
		if (win_last_lp & 0x01000000) /* Extended key */
		{
			if (event->keyval == GDK_Return) return (GDK_KP_Enter);
		}
		else /* Normal key */
		{
			unsigned char *cp = strchr(keypad_vk, event->hardware_keycode);
			if (cp && (event->keyval == keypad_wgtk[cp - keypad_vk]))
				return (keypad_xgtk[cp - keypad_vk]);
		}
	}
	return (gdk_keyval_to_lower(event->keyval));
}

#else /* X11/Quartz/whatever */

guint low_key(GdkEventKey *event)
{
	return (gdk_keyval_to_lower(event->keyval));
}

#endif

guint keyval_key(guint keyval)
{
	GdkDisplay *display = gtk_widget_get_display(main_window);
	GdkKeymap *keymap = gdk_keymap_get_for_display(display);
	GdkKeymapKey *key;
	gint nkeys;

	if (!gdk_keymap_get_entries_for_keyval(keymap, keyval, &key, &nkeys))
	{
#ifdef GDK_WINDOWING_WIN32
		/* Keypad keys need specialcasing on Windows */
		for (nkeys = 0; keypad_xgtk[nkeys] &&
			(keyval != keypad_xgtk[nkeys]); nkeys++);
		return (keypad_vk[nkeys]);
#endif
		return (0);
	}
	if (!nkeys) return (0);
	keyval = key[0].keycode;
	g_free(key);
	return (keyval);
}

#endif

// Interpreting arrow keys

int arrow_key_(unsigned key, unsigned state, int *dx, int *dy, int mult)
{
	if ((state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) !=
		GDK_SHIFT_MASK) mult = 1;
	*dx = *dy = 0;
	switch (key)
	{
		case KEY(KP_Left): case KEY(Left):
			*dx = -mult; break;
		case KEY(KP_Right): case KEY(Right):
			*dx = mult; break;
		case KEY(KP_Up): case KEY(Up):
			*dy = -mult; break;
		case KEY(KP_Down): case KEY(Down):
			*dy = mult; break;
	}
	return (*dx || *dy);
}

// Create pixmap cursor

#if GTK_MAJOR_VERSION == 3

/* Assemble two XBMs into one BW ARGB32 surface */
static cairo_surface_t *xbms_to_surface(unsigned char *image, unsigned char *mask,
	int w, int h)
{
	cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
	unsigned char *dst0 = cairo_image_surface_get_data(s);
	int st = cairo_image_surface_get_stride(s);
	unsigned int b = 0;

	cairo_surface_flush(s);
	while (h-- > 0)
	{
		guint32 *dest = (void *)dst0;
		int n;

		for (n = w; n-- > 0; b++)
			*dest++ = ((image[b >> 3] >> (b & 7)) & 1) * 0x00FFFFFFU +
				((mask[b >> 3] >> (b & 7)) & 1) * 0xFF000000U;
		dst0 += st; b += (~b + 1) & 7;
	}
	cairo_surface_mark_dirty(s);
	return (s);
}

GdkCursor *make_cursor(char *icon, char *mask, int w, int h, int tip_x, int tip_y)
{
	cairo_surface_t *s = xbms_to_surface(icon, mask, w, h);
	GdkCursor *cursor = gdk_cursor_new_from_surface(
		gtk_widget_get_display(main_window), s, tip_x, tip_y);
	cairo_surface_fdestroy(s);
	return (cursor);
}

#else /* GTK_MAJOR_VERSION <= 2 */

GdkCursor *make_cursor(char *icon, char *mask, int w, int h, int tip_x, int tip_y)
{
	static GdkColor cfg = { -1, -1, -1, -1 }, cbg = { 0, 0, 0, 0 };
	GdkPixmap *icn, *msk;
	GdkCursor *cursor;

	icn = gdk_bitmap_create_from_data(NULL, icon, w, h);
	msk = gdk_bitmap_create_from_data(NULL, mask, w, h);
	cursor = gdk_cursor_new_from_pixmap(icn, msk, &cfg, &cbg, tip_x, tip_y);
	gdk_pixmap_unref(icn);
	gdk_pixmap_unref(msk);
	return (cursor);
}

#endif

// Menu-like combo box

#if GTK_MAJOR_VERSION == 3

/* Making GtkComboBox behave is quite nontrivial, here */

static gboolean wj_combo_click(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	if (user_data)
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(user_data), TRUE);
	return (TRUE);
}

/* void handler(GtkWidget *combo, gpointer user_data); */
GtkWidget *wj_combo_box(char **names, int cnt, int u, int idx, void **r,
	GCallback handler)
{
	GtkWidget *cbox, *entry, *button;
	int i;

	if (idx >= cnt) idx = 0;
	cbox = gtk_combo_box_text_new_with_entry();
	/* Find the button */
	button = combobox_button(cbox);

	/* Make the entry a dumb display */
	entry = gtk_bin_get_child(GTK_BIN(cbox));
	g_object_set(entry, "editable", FALSE, NULL);
	gtk_widget_set_can_focus(entry, FALSE);
	/* Make click on entry do popup too */
	g_signal_connect(entry, "button_press_event", G_CALLBACK(wj_combo_click), NULL);
	g_signal_connect(entry, "button_release_event", G_CALLBACK(wj_combo_click), button);

	for (i = 0; i < cnt; i++)
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cbox), __(names[i]));
	gtk_combo_box_set_active(GTK_COMBO_BOX(cbox), idx);
	if (handler) g_signal_connect(G_OBJECT(cbox), "changed",
		handler, r);

	return (cbox);
}

int wj_combo_box_get_history(GtkWidget *combobox)
{
	return (gtk_combo_box_get_active(GTK_COMBO_BOX(combobox)));
}

/* Use GtkComboBox when available */
#elif GTK2VERSION >= 4 /* GTK+ 2.4+ */

/* Tweak style settings for combo box */
static void wj_combo_restyle(GtkWidget *cbox)
{
	static int done;
	if (!done)
	{
		gtk_rc_parse_string("style \"mtPaint_cblist\" {\n"
			"GtkComboBox::appears-as-list = 1\n}\n");
		gtk_rc_parse_string("widget \"*.mtPaint_cbox\" "
			"style \"mtPaint_cblist\"\n");
		done = TRUE;
	}
	gtk_widget_set_name(cbox, "mtPaint_cbox");
}

/* void handler(GtkWidget *combo, gpointer user_data); */
GtkWidget *wj_combo_box(char **names, int cnt, int u, int idx, void **r,
	GtkSignalFunc handler)
{
	GtkWidget *cbox;
	GtkComboBox *combo;
	int i;


	if (idx >= cnt) idx = 0;
	combo = GTK_COMBO_BOX(cbox = gtk_combo_box_new_text());
	wj_combo_restyle(cbox);
	for (i = 0; i < cnt; i++) gtk_combo_box_append_text(combo, __(names[i]));
	gtk_combo_box_set_active(combo, idx);
	if (handler) gtk_signal_connect(GTK_OBJECT(cbox), "changed",
		handler, r);

	return (cbox);
}

int wj_combo_box_get_history(GtkWidget *combobox)
{
	return (gtk_combo_box_get_active(GTK_COMBO_BOX(combobox)));
}

#else /* Use GtkCombo before GTK+ 2.4.0 */

/* !!! In GTK+2, this handler is called twice for each change; in GTK+1,
 * once if using cursor keys, twice if selecting from list */
static void wj_combo(GtkWidget *entry, gpointer handler)
{
	GtkWidget *combo = entry->parent;
	gpointer user_data;

	/* GTK+1 updates the entry constantly - wait it out */
#if GTK_MAJOR_VERSION == 1
	if (GTK_WIDGET_VISIBLE(GTK_COMBO(combo)->popwin)) return;
#endif
	user_data = gtk_object_get_user_data(GTK_OBJECT(entry));
	((void (*)(GtkWidget *, gpointer))handler)(combo, user_data);
}

#if GTK_MAJOR_VERSION == 1
/* Notify the main handler that meaningless updates are finished */
static void wj_combo_restart(GtkWidget *widget, GtkCombo *combo)
{
	gtk_signal_emit_by_name(GTK_OBJECT(combo->entry), "changed");
}
#endif

#if GTK_MAJOR_VERSION == 2
/* Tweak style settings for combo entry */
static void wj_combo_restyle(GtkWidget *entry)
{
	static int done;
	if (!done)
	{
		gtk_rc_parse_string("style \"mtPaint_extfocus\" {\n"
			"GtkWidget::interior-focus = 0\n}\n");
		gtk_rc_parse_string("widget \"*.mtPaint_cbentry\" "
			"style \"mtPaint_extfocus\"\n");
		done = TRUE;
	}
	gtk_widget_set_name(entry, "mtPaint_cbentry");
}

/* Prevent cursor from appearing */
static gboolean wj_combo_kill_cursor(GtkWidget *widget, GdkEventExpose *event,
	gpointer user_data)
{
	/* !!! Private field - future binary compatibility isn't guaranteed */
	GTK_ENTRY(widget)->cursor_visible = FALSE;
	return (FALSE);
}
#endif

/* void handler(GtkWidget *combo, gpointer user_data); */
GtkWidget *wj_combo_box(char **names, int cnt, int u, int idx, void **r,
	GtkSignalFunc handler)
{
	GtkWidget *cbox;
	GtkCombo *combo;
	GtkEntry *entry;
	GList *list = NULL;
	int i;


	if (idx >= cnt) idx = 0;
	combo = GTK_COMBO(cbox = gtk_combo_new());
#if GTK_MAJOR_VERSION == 2
	wj_combo_restyle(combo->entry);
	gtk_signal_connect(GTK_OBJECT(combo->entry), "expose_event",
		GTK_SIGNAL_FUNC(wj_combo_kill_cursor), NULL);
#endif
	gtk_combo_set_value_in_list(combo, TRUE, FALSE);
	for (i = 0; i < cnt; i++) list = g_list_append(list, __(names[i]));
	gtk_combo_set_popdown_strings(combo, list);
	g_list_free(list);
	gtk_widget_show_all(cbox);
	entry = GTK_ENTRY(combo->entry);
	gtk_entry_set_editable(entry, FALSE);
	gtk_entry_set_text(entry, names[idx]);
	if (!handler) return (cbox);

	/* Install signal handler */
	gtk_object_set_user_data(GTK_OBJECT(combo->entry), r);
	gtk_signal_connect(GTK_OBJECT(combo->entry), "changed",
		GTK_SIGNAL_FUNC(wj_combo), (gpointer)handler);
#if GTK_MAJOR_VERSION == 1
	gtk_signal_connect(GTK_OBJECT(combo->popwin), "hide",
		GTK_SIGNAL_FUNC(wj_combo_restart), combo);
#endif

	return (cbox);
}

int wj_combo_box_get_history(GtkWidget *combobox)
{
	GtkList *list = GTK_LIST(GTK_COMBO(combobox)->list);

	if (!list->selection || !list->selection->data) return (-1);
	return(gtk_list_child_position(list, GTK_WIDGET(list->selection->data)));
}

#endif

#if GTK_MAJOR_VERSION == 3

// Bin widget with customizable size handling

/* The only way to intercept size requests in GTK+3 is a wrapper widget. This is
 * such a widget, with installable handlers */

#define WJSIZEBIN(obj)		G_TYPE_CHECK_INSTANCE_CAST(obj, wjsizebin_get_type(), wjsizebin)
#define IS_WJSIZEBIN(obj)	G_TYPE_CHECK_INSTANCE_TYPE(obj, wjsizebin_get_type())

typedef void (*size_alloc_f)(GtkWidget *widget, GtkAllocation *allocation,
	gpointer user_data);
typedef void (*get_size_f)(GtkWidget *widget, gint vert, gint *min, gint *nat,
	gint for_width, gpointer user_data);

typedef struct
{
	GtkBin		bin;		// Parent class
	size_alloc_f	size_alloc;	// Allocate child
	get_size_f	get_size;	// Modify requested size
	gpointer	udata;
} wjsizebin;

typedef struct
{
	GtkBinClass parent_class;
} wjsizebinClass;

G_DEFINE_TYPE(wjsizebin, wjsizebin, GTK_TYPE_BIN)

static void wjsizebin_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	wjsizebin *sbin = WJSIZEBIN(widget);
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(widget));

	gtk_widget_set_allocation(widget, allocation);
	if (!child || !gtk_widget_get_visible(child)) return;
	/* Call handler if installed, do default thing if not */
	if (sbin->size_alloc) sbin->size_alloc(child, allocation, sbin->udata);
	else gtk_widget_size_allocate(child, allocation);
}

static void wjsizebin_get_size(GtkWidget *widget, gint vert, gint *min, gint *nat,
	gint for_width)
{
	wjsizebin *sbin = WJSIZEBIN(widget);
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(widget));

	/* Preset the size */
	*min = *nat = 0; // Default
	if (!child || !gtk_widget_get_visible(child)); // Invisible doesn't matter
	else if (for_width >= 0)
		gtk_widget_get_preferred_height_for_width(child, for_width, min, nat);
	else (vert ? gtk_widget_get_preferred_height :
		gtk_widget_get_preferred_width)(child, min, nat);
	/* Let handler modify it */
	if (sbin->get_size)
		sbin->get_size(child, vert, min, nat, for_width, sbin->udata);
}

static void wjsizebin_get_preferred_width(GtkWidget *widget, gint *min, gint *nat)
{
	wjsizebin_get_size(widget, FALSE, min, nat, -1);
}

static void wjsizebin_get_preferred_height(GtkWidget *widget, gint *min, gint *nat)
{
	wjsizebin_get_size(widget, TRUE, min, nat, -1);
}

static void wjsizebin_get_preferred_width_for_height(GtkWidget *widget, gint h,
	 gint *min, gint *nat)
{
	wjsizebin_get_size(widget, FALSE, min, nat, -1);
}

/* Specialcase only height-for-width, same as GtkFrame does */
static void wjsizebin_get_preferred_height_for_width(GtkWidget *widget, gint w,
	 gint *min, gint *nat)
{
	wjsizebin_get_size(widget, TRUE, min, nat, w);
}

static void wjsizebin_class_init(wjsizebinClass *class)
{
	GtkWidgetClass *wclass = GTK_WIDGET_CLASS(class);

	wclass->size_allocate = wjsizebin_size_allocate;
	wclass->get_preferred_width = wjsizebin_get_preferred_width;
	wclass->get_preferred_height = wjsizebin_get_preferred_height;
	wclass->get_preferred_width_for_height = wjsizebin_get_preferred_width_for_height;
	wclass->get_preferred_height_for_width = wjsizebin_get_preferred_height_for_width;
	/* !!! Leave my bin alone */
	wclass->style_updated = NULL;
}

static void wjsizebin_init(wjsizebin *sbin)
{
	gtk_widget_set_has_window(GTK_WIDGET(sbin), FALSE);
}

GtkWidget *wjsizebin_new(GCallback get_size, GCallback size_alloc, gpointer user_data)
{
	GtkWidget *w = g_object_new(wjsizebin_get_type(), NULL);
	wjsizebin *sbin = WJSIZEBIN(w);
	sbin->get_size = (get_size_f)get_size;
	sbin->size_alloc = (size_alloc_f)size_alloc;
	sbin->udata = user_data;
	return (w);
}

#else /* #if GTK_MAJOR_VERSION <= 2 */

// Box widget with customizable size handling

/* There exist no way to override builtin handlers for GTK_RUN_FIRST signals,
 * such as size-request and size-allocate; so instead of building multiple
 * custom widget classes with different resize handling, it's better to build
 * one with no builtin sizing at all - WJ */

GtkWidget *wj_size_box()
{
	static GtkType size_box_type;
	GtkWidget *widget;

	if (!size_box_type)
	{
		static const GtkTypeInfo info = {
			"WJSizeBox", sizeof(GtkBox),
			sizeof(GtkBoxClass), NULL /* class init */,
			NULL /* instance init */, NULL, NULL, NULL };
		GtkWidgetClass *wc;
		size_box_type = gtk_type_unique(GTK_TYPE_BOX, &info);
		wc = gtk_type_class(size_box_type);
		wc->size_request = NULL;
		wc->size_allocate = NULL;
	}
	widget = gtk_widget_new(size_box_type, NULL);
	GTK_WIDGET_SET_FLAGS(widget, GTK_NO_WINDOW);
#if GTK_MAJOR_VERSION == 2
	gtk_widget_set_redraw_on_allocate(widget, FALSE);
#endif
	gtk_widget_show(widget);
	return (widget);
}

// Disable visual updates while tweaking container's contents

/* This is an evil hack, and isn't guaranteed to work in future GTK+ versions;
 * still, not providing such a function is a design mistake in GTK+, and it'll
 * be easier to update this code if it becomes broken sometime in dim future,
 * than deal with premature updates right here and now - WJ */

typedef struct {
	int flags, pf, mode;
} lock_state;

gpointer toggle_updates(GtkWidget *widget, gpointer unlock)
{
	lock_state *state;
	GtkContainer *cont = GTK_CONTAINER(widget);

	if (!unlock) /* Locking... */
	{
		state = calloc(1, sizeof(lock_state));
		state->mode = cont->resize_mode;
		cont->resize_mode = GTK_RESIZE_IMMEDIATE;
		state->flags = GTK_WIDGET_FLAGS(widget);
#if GTK_MAJOR_VERSION == 1
		GTK_WIDGET_UNSET_FLAGS(widget, GTK_VISIBLE);
		state->pf = GTK_WIDGET_IS_OFFSCREEN(widget);
		GTK_PRIVATE_SET_FLAG(widget, GTK_IS_OFFSCREEN);
#else /* if GTK_MAJOR_VERSION == 2 */
		GTK_WIDGET_UNSET_FLAGS(widget, GTK_VISIBLE | GTK_MAPPED);
#endif
	}
	else /* Unlocking... */
	{
		state = unlock;
		cont->resize_mode = state->mode;
#if GTK_MAJOR_VERSION == 1
		GTK_WIDGET_SET_FLAGS(widget, state->flags & GTK_VISIBLE);
		if (!state->pf) GTK_PRIVATE_UNSET_FLAG(widget, GTK_IS_OFFSCREEN);
#else /* if GTK_MAJOR_VERSION == 2 */
		GTK_WIDGET_SET_FLAGS(widget, state->flags & (GTK_VISIBLE | GTK_MAPPED));
#endif
		free(state);
		state = NULL;
	}
	return (state);
}

#endif

// Maximized state

#if GTK_MAJOR_VERSION == 1

static Atom netwm[3];
static int netwm_set;

static int init_netwm(GdkWindow *w)
{
	static char *nm[3] = {
		"_NET_WM_STATE",
		"_NET_WM_STATE_MAXIMIZED_VERT",
		"_NET_WM_STATE_MAXIMIZED_HORZ" };
	return (XInternAtoms(GDK_WINDOW_XDISPLAY(w), nm, 3, FALSE, netwm));
}

int is_maximized(GtkWidget *window)
{
	Atom type, *atoms;
	int format, vh = 0;
	unsigned long i, nitems, after;
	unsigned char *data;
	GdkWindow *w = window->window;

	if (!netwm_set) netwm_set = init_netwm(w);

	gdk_error_trap_push();
	XGetWindowProperty(GDK_WINDOW_XDISPLAY(w), GDK_WINDOW_XWINDOW(w),
		netwm[0], 0, G_MAXLONG, False, XA_ATOM,
		&type, &format, &nitems, &after, &data);
	gdk_error_trap_pop();

	atoms = (void *)data;
	for (i = 0; i < nitems; i++)
	{
		if (atoms[i] == netwm[1]) vh |= 1;
		else if (atoms[i] == netwm[2]) vh |= 2;
        }
	XFree(atoms);
	return (vh == 3);
}

void set_maximized(GtkWidget *window)
{
	XEvent xev;
	GdkWindow *w = window->window;

	if (!netwm_set) netwm_set = init_netwm(w);

	xev.xclient.type = ClientMessage;
	xev.xclient.serial = 0;
	xev.xclient.send_event = True;
	xev.xclient.window = GDK_WINDOW_XWINDOW(w);
	xev.xclient.message_type = netwm[0];
	xev.xclient.format = 32;
	xev.xclient.data.l[0] = 1; /* _NET_WM_STATE_ADD */
	xev.xclient.data.l[1] = netwm[1];
	xev.xclient.data.l[2] = netwm[2];
	xev.xclient.data.l[3] = 0;
	xev.xclient.data.l[4] = 0;

	XSendEvent(GDK_WINDOW_XDISPLAY(w), DefaultRootWindow(GDK_WINDOW_XDISPLAY(w)),
		False, SubstructureRedirectMask | SubstructureNotifyMask, &xev);
}

#endif

// Drawable to RGB

/* This code exists to read back both windows and pixmaps. In GTK+1 pixmap
 * handling capabilities are next to nonexistent, so a GdkWindow must always
 * be passed in, to serve as source of everything but actual pixels.
 * The only exception is when the pixmap passed in is definitely a bitmap.
 * Parsing GdkImage pixels loosely follows the example of convert_real_slow()
 * in GTK+2 (gdk/gdkpixbuf-drawable.c) - WJ
 */

#if GTK_MAJOR_VERSION == 1

unsigned char *wj_get_rgb_image(GdkWindow *window, GdkPixmap *pixmap,
	unsigned char *buf, int x, int y, int width, int height)
{
	GdkImage *img;
	GdkColormap *cmap;
	GdkVisual *vis, fake_vis;
	GdkColor bw[2], *cols = NULL;
	unsigned char *dest, *wbuf = NULL;
	guint32 rmask, gmask, bmask, pix;
	int mode, rshift, gshift, bshift;
	int i, j;


	if (!window) /* No window - we got us a bitmap */
	{
		vis = &fake_vis;
		vis->type = GDK_VISUAL_STATIC_GRAY;
		vis->depth = 1;
		cmap = NULL;
	}
	else if (window == (GdkWindow *)&gdk_root_parent) /* Not a proper window */
	{
		vis = gdk_visual_get_system();
		cmap = gdk_colormap_get_system();
	}
	else
	{
		vis = gdk_window_get_visual(window);
		cmap = gdk_window_get_colormap(window);
	}

	if (!vis) return (NULL);
	mode = vis->type;

	if (cmap) cols = cmap->colors;
	else if ((mode != GDK_VISUAL_TRUE_COLOR) && (vis->depth != 1))
		return (NULL); /* Can't handle other types w/o colormap */

	if (!buf) buf = wbuf = malloc(width * height * 3);
	if (!buf) return (NULL);

	img = gdk_image_get(pixmap ? pixmap : window, x, y, width, height);
	if (!img)
	{
		free(wbuf);
		return (NULL);
	}

	rmask = vis->red_mask;
	gmask = vis->green_mask;
	bmask = vis->blue_mask;
	rshift = vis->red_shift;
	gshift = vis->green_shift;
	bshift = vis->blue_shift;

	if (mode == GDK_VISUAL_TRUE_COLOR)
	{
		/* !!! Unlikely to happen, but it's cheap to be safe */
		if (vis->red_prec > 8) rshift += vis->red_prec - 8;
		if (vis->green_prec > 8) gshift += vis->green_prec - 8;
		if (vis->blue_prec > 8) bshift += vis->blue_prec - 8;
	}
	else if (!cmap && (vis->depth == 1)) /* Bitmap */
	{
		/* Make a palette for it */
		mode = GDK_VISUAL_PSEUDO_COLOR;
		bw[0].red = bw[0].green = bw[0].blue = 0;
		bw[1].red = bw[1].green = bw[1].blue = 65535;
		cols = bw;
	}

	dest = buf;
	for (i = 0; i < height; i++)
	for (j = 0; j < width; j++ , dest += 3)
	{
		pix = gdk_image_get_pixel(img, j, i);
		if (mode == GDK_VISUAL_TRUE_COLOR)
		{
			dest[0] = (pix & rmask) >> rshift;
			dest[1] = (pix & gmask) >> gshift;
			dest[2] = (pix & bmask) >> bshift;
		}
		else if (mode == GDK_VISUAL_DIRECT_COLOR)
		{
			dest[0] = cols[(pix & rmask) >> rshift].red >> 8;
			dest[1] = cols[(pix & gmask) >> gshift].green >> 8;
			dest[2] = cols[(pix & bmask) >> bshift].blue >> 8;
		}
		else /* Paletted */
		{
			dest[0] = cols[pix].red >> 8;
			dest[1] = cols[pix].green >> 8;
			dest[2] = cols[pix].blue >> 8;
		}
	}

	/* Now extend the precision to 8 bits where necessary */
	if (mode == GDK_VISUAL_TRUE_COLOR)
	{
		unsigned char xlat[128], *dest;
		int i, j, k, l = width * height;

		for (i = 0; i < 3; i++)
		{
			k = !i ? vis->red_prec : i == 1 ? vis->green_prec :
				vis->blue_prec;
			if (k >= 8) continue;
			set_xlate(xlat, k);
			dest = buf + i;
			for (j = 0; j < l; j++ , dest += 3)
				*dest = xlat[*dest];
		}
	}

	gdk_image_destroy(img);
	return (buf);
}

#elif GTK_MAJOR_VERSION == 2

unsigned char *wj_get_rgb_image(GdkWindow *window, GdkPixmap *pixmap,
	unsigned char *buf, int x, int y, int width, int height)
{
	GdkColormap *cmap = NULL;
	GdkPixbuf *pix, *res;
	unsigned char *wbuf = NULL;

	if (!buf) buf = wbuf = malloc(width * height * 3);
	if (!buf) return (NULL);

	if (pixmap && window)
	{
		cmap = gdk_drawable_get_colormap(pixmap);
		if (!cmap) cmap = gdk_drawable_get_colormap(window);
	}
	pix = gdk_pixbuf_new_from_data(buf, GDK_COLORSPACE_RGB,
		FALSE, 8, width, height, width * 3, NULL, NULL);
	if (pix)
	{
		res = gdk_pixbuf_get_from_drawable(pix,
			pixmap ? pixmap : window, cmap,
			x, y, 0, 0, width, height);
		g_object_unref(pix);
		if (res) return (buf);
	}
	free(wbuf);
	return (NULL);
}

#else /* if GTK_MAJOR_VERSION == 3 */

unsigned char *wj_get_rgb_image(GdkWindow *window, cairo_surface_t *s,
	unsigned char *buf, int x, int y, int width, int height)
{
	GdkPixbuf *pix;

	if (s) pix = gdk_pixbuf_get_from_surface(s, x, y, width, height);
	else pix = gdk_pixbuf_get_from_window(window, x, y, width, height);
	if (!pix) return (NULL);

	if (!buf) buf = calloc(1, width * height * 3);
	if (buf) /* Copy data to 3bpp continuous buffer */
	{
		unsigned char *dest, *src;
		int x, y, d, ww, wh, nc, stride;

		ww = gdk_pixbuf_get_width(pix);
		wh = gdk_pixbuf_get_height(pix);
		nc = gdk_pixbuf_get_n_channels(pix);
		stride = gdk_pixbuf_get_rowstride(pix);

		/* If result is somehow larger (scaling?), clip it */
		if (ww > width) ww = width;
		if (wh > height) wh = height;
		src = gdk_pixbuf_get_pixels(pix);
		dest = buf;
		stride -= nc * ww;
		d = (width - ww) * 3;
		for (y = 0; y < wh; y++)
		{
			for (x = 0; x < ww; x++)
			{
				*dest++= src[0];
				*dest++= src[1];
				*dest++= src[2];
				src += nc;
			}
			src += stride;
			dest += d;
		}
	}
	g_object_unref(pix);
	return (buf);
}

#endif

// Clipboard

#ifdef GDK_WINDOWING_WIN32

/* Detect if current clipboard belongs to something in the program itself;
 * on Windows, GDK is purposely lying to us about it, so use WinAPI instead */
int internal_clipboard(int which)
{
	DWORD pid;

	if (which) return (TRUE); // No "PRIMARY" clipboard exists on Windows
	GetWindowThreadProcessId(GetClipboardOwner(), &pid);
	return (pid == GetCurrentProcessId());
}

#elif defined GDK_WINDOWING_QUARTZ

/* Detect if current clipboard belongs to something in the program itself;
 * on Quartz, GDK is halfbaked, so use a workaround instead */
int internal_clipboard(int which)
{
	return (!!gtk_clipboard_get_owner(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD)));
}

#else

/* Detect if current clipboard belongs to something in the program itself */
int internal_clipboard(int which)
{
	gpointer widget = NULL;
	GdkWindow *win = gdk_selection_owner_get(
		gdk_atom_intern(which ? "PRIMARY" : "CLIPBOARD", FALSE));
	if (!win) return (FALSE); // Unknown window
	gdk_window_get_user_data(win, &widget);
	return (!!widget); // Real widget or foreign window?
}

#endif

// Clipboard pixmaps

#ifdef HAVE_PIXMAPS

/* Make code not compile if unthinkable happens */
typedef char Mismatched_XID_type[2 * (sizeof(Pixmap) == sizeof(XID_type)) - 1];

/* It's unclear who should free clipboard pixmaps and when, so I do the same
 * thing Qt does, destroying the next-to-last allocated pixmap each time a new
 * one is allocated - WJ */

#if GTK_MAJOR_VERSION == 3

int export_pixmap(pixmap_info *p, int w, int h)
{
	static cairo_surface_t *exported[2];
	cairo_surface_t *s = gdk_window_create_similar_surface(
		gtk_widget_get_window(main_window), CAIRO_CONTENT_COLOR, w, h);

	if (cairo_surface_get_type(s) != CAIRO_SURFACE_TYPE_XLIB)
	{
		cairo_surface_destroy(s);
		return (FALSE);
	}
	if (exported[0])
	{
		if (exported[1])
		{
			/* Someone might have destroyed the X pixmap already,
			 * so get ready to live through an X error */
			GdkDisplay *d = gtk_widget_get_display(main_window);
			gdk_x11_display_error_trap_push(d);
			cairo_surface_destroy(exported[1]);
			gdk_x11_display_error_trap_pop_ignored(d);
		}
		exported[1] = exported[0];
	}
	exported[0] = p->pm = s;
	p->w = w;
	p->h = h;
	p->depth = -1;
	p->xid = cairo_xlib_surface_get_drawable(s);
	
	return (TRUE);
}

void pixmap_put_rows(pixmap_info *p, unsigned char *src, int y, int cnt)
{
	cairo_surface_t *s;
	cairo_t *cr;

	s = cairo_upload_rgb(p->pm, NULL, src, p->w, cnt, p->w * 3);
	cr = cairo_create(p->pm);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, s, 0, y);
	cairo_rectangle(cr, 0, y, p->w, cnt);
	cairo_fill(cr);
	cairo_destroy(cr);

	cairo_surface_fdestroy(s);
}

#else /* if GTK_MAJOR_VERSION <= 2 */

int export_pixmap(pixmap_info *p, int w, int h)
{
	static GdkPixmap *exported[2];

	if (exported[0])
	{
		if (exported[1])
		{
			/* Someone might have destroyed the X pixmap already,
			 * so get ready to live through an X error */
			gdk_error_trap_push();
			gdk_pixmap_unref(exported[1]);
			gdk_error_trap_pop();
		}
		exported[1] = exported[0];
	}
	exported[0] = p->pm = gdk_pixmap_new(main_window->window, w, h, -1);
	if (!exported[0]) return (FALSE);

	p->w = w;
	p->h = h;
	p->depth = -1;
	p->xid = GDK_WINDOW_XWINDOW(exported[0]);
	
	return (TRUE);
}

void pixmap_put_rows(pixmap_info *p, unsigned char *src, int y, int cnt)
{
	gdk_draw_rgb_image(p->pm, main_window->style->black_gc,
		0, y, p->w, cnt, GDK_RGB_DITHER_NONE, src, p->w * 3);
}

#endif

#endif /* HAVE_PIXMAPS */

int import_pixmap(pixmap_info *p, XID_type *xid)
{
	if (xid) // Pixmap by ID
	{
/* This ugly code imports X Window System's pixmaps; this allows mtPaint to
 * receive images from programs such as XPaint */
#if (GTK_MAJOR_VERSION == 3) && defined GDK_WINDOWING_X11
		cairo_surface_t *s;
		GdkDisplay *d = gtk_widget_get_display(main_window);
		Display *disp = GDK_DISPLAY_XDISPLAY(d);
		XWindowAttributes attr;
		Window root;
		unsigned int x, y, w, h, bor, depth;
		int res;

		gdk_x11_display_error_trap_push(d); // No guarantee that we got a valid pixmap
		res = XGetGeometry(disp, *xid, &root, &x, &y, &w, &h, &bor, &depth);
		if (res) res = XGetWindowAttributes(disp, root, &attr);
		gdk_x11_display_error_trap_pop_ignored(d);
		if (!res) return (FALSE);

		s = cairo_xlib_surface_create(disp, *xid, attr.visual, w, h);
		if (cairo_surface_get_type(s) == CAIRO_SURFACE_TYPE_XLIB)
		{
			p->xid = *xid;
			p->pm = s;
			p->w = w;
			p->h = h;
			p->depth = depth;
			return (TRUE);
		}
		cairo_surface_destroy(s);
#elif (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
		int w, h, d, dd;

		gdk_error_trap_push(); // No guarantee that we got a valid pixmap
		p->pm = gdk_pixmap_foreign_new(p->xid = *xid);
		gdk_error_trap_pop(); // The above call returns NULL on failure anyway
		if (!p->pm) return (FALSE);
		dd = gdk_visual_get_system()->depth;
#if GTK_MAJOR_VERSION == 1
		gdk_window_get_geometry(p->pm, NULL, NULL, &w, &h, &d);
#else /* #if GTK_MAJOR_VERSION == 2 */
		gdk_drawable_get_size(p->pm, &w, &h);
		d = gdk_drawable_get_depth(p->pm);
#endif
		if ((d == 1) || (d == dd))
		{
			p->w = w;
			p->h = h;
			p->depth = d;
			return (TRUE);
		}
		drop_pixmap(p);
#endif
		return (FALSE);
	}
	else // NULL means a screenshot
	{
// !!! Should be the screen where gdk_get_default_root_window() is
		p->w = gdk_screen_width();
		p->h = gdk_screen_height();
		p->depth = 3;
		p->pm = NULL;
		p->xid = 0;
	}
	return (TRUE);
}

void drop_pixmap(pixmap_info *p)
{
#if (GTK_MAJOR_VERSION == 3) && defined GDK_WINDOWING_X11
	if (!p->pm) return;
	cairo_surface_destroy(p->pm);
#elif (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
	if (!p->pm) return;
#if GTK_MAJOR_VERSION == 1
	/* Don't let gdk_pixmap_unref() destroy another process's pixmap -
	 * implement freeing the GdkPixmap structure here instead */
	gdk_xid_table_remove(((GdkWindowPrivate *)p->pm)->xwindow);
	g_dataset_destroy(p->pm);
	g_free(p->pm);
#else /* #if GTK_MAJOR_VERSION == 2 */
	gdk_pixmap_unref(p->pm);
#endif
#endif
}

int pixmap_get_rows(pixmap_info *p, unsigned char *dest, int y, int cnt)
{
	return (!!wj_get_rgb_image(p->depth == 1 ? NULL :
#if GTK_MAJOR_VERSION == 1
		(GdkWindow *)&gdk_root_parent,
#else /* #if GTK_MAJOR_VERSION >= 2 */
		gdk_get_default_root_window(),
#endif
		p->pm, dest, 0, y, p->w, cnt));
}

// Render stock icons to pixmaps

#if GTK_MAJOR_VERSION == 3

/* Actually loads a named icon, not stock */
static GdkPixbuf *render_stock_pixbuf(GtkWidget *widget, const gchar *stock_id)
{
	GtkIconTheme *theme;
	gint w, h;

// !!! The theme need be modifiable by theme file loaded from prefs
	theme = gtk_icon_theme_get_for_screen(gtk_style_context_get_screen(
		gtk_widget_get_style_context(widget)));
	if (!theme) return (NULL); // Paranoia
	gtk_icon_size_lookup(GTK_ICON_SIZE_SMALL_TOOLBAR, &w, &h);
	return (gtk_icon_theme_load_icon(theme, stock_id, (w < h ? w : h),
		GTK_ICON_LOOKUP_USE_BUILTIN, NULL));
}

#elif GTK_MAJOR_VERSION == 2

static GdkPixbuf *render_stock_pixbuf(GtkWidget *widget, const gchar *stock_id)
{
	GtkIconSet *icon_set;

	/* !!! Doing this for widget itself in some cases fails (!) */
	icon_set = gtk_style_lookup_icon_set(main_window->style, stock_id);
	if (!icon_set) return (NULL);
// !!! Is this "widget" here at all useful, or is "main_window" good enough?
	gtk_widget_ensure_style(widget);
	return (gtk_icon_set_render_icon(icon_set, widget->style,
		gtk_widget_get_direction(widget), GTK_WIDGET_STATE(widget),
		GTK_ICON_SIZE_SMALL_TOOLBAR, widget, NULL));
}

GdkPixmap *render_stock_pixmap(GtkWidget *widget, const gchar *stock_id,
	GdkBitmap **mask)
{
	GdkPixmap *pmap;
	GdkPixbuf *buf;

	buf = render_stock_pixbuf(widget, stock_id);
	if (!buf) return (NULL);
	gdk_pixbuf_render_pixmap_and_mask_for_colormap(buf,
		gtk_widget_get_colormap(widget), &pmap, mask, 127);
	g_object_unref(buf);
	return (pmap);
}

#endif

// Image widget

/* !!! GtkImage is broken on GTK+ 2.12.9 at least, with regard to pixmaps -
 * background gets corrupted when widget is made insensitive, so GtkPixmap is
 * the only choice in that case - WJ */

#if GTK_MAJOR_VERSION == 2
/* Guard against different depth visuals */
static void xpm_realize(GtkWidget *widget, gpointer user_data)
{
	if (gdk_drawable_get_depth(widget->window) !=
		gdk_drawable_get_depth(GTK_PIXMAP(widget)->pixmap))
	{
		GdkPixmap *icon, *mask;
		icon = gdk_pixmap_create_from_xpm_d(widget->window, &mask, NULL,
			user_data);
		gtk_pixmap_set(GTK_PIXMAP(widget), icon, mask);
		gdk_pixmap_unref(icon);
		gdk_pixmap_unref(mask);
	}
}
#endif

GtkWidget *xpm_image(XPM_TYPE xpm)
{
	GtkWidget *widget;
#if GTK_MAJOR_VERSION >= 2
	GdkPixbuf *buf;
	char name[256];

	snprintf(name, sizeof(name), "mtpaint_%s", (char *)xpm[0]);
	buf = render_stock_pixbuf(main_window, name);
	if (buf) /* Found a themed icon - use it */
	{
		widget = gtk_image_new_from_pixbuf(buf);
		g_object_unref(buf);
		gtk_widget_show(widget);
		return (widget);
	}
#endif
	/* Fall back to builtin XPM icon */
#if GTK_MAJOR_VERSION == 3
	buf = gdk_pixbuf_new_from_xpm_data((const char **)xpm[1]);
	widget = gtk_image_new_from_pixbuf(buf);
	g_object_unref(buf);
#else /* if GTK_MAJOR_VERSION <= 2 */
	{
		GdkPixmap *icon, *mask;
		icon = gdk_pixmap_create_from_xpm_d(main_window->window, &mask,
#if GTK_MAJOR_VERSION == 2
			NULL, (char **)xpm[1]);
#else /* if GTK_MAJOR_VERSION == 1 */
			NULL, xpm);
#endif
		widget = gtk_pixmap_new(icon, mask);
		gdk_pixmap_unref(icon);
		gdk_pixmap_unref(mask);
	}
#endif
	gtk_widget_show(widget);
#if GTK_MAJOR_VERSION == 2
	gtk_signal_connect(GTK_OBJECT(widget), "realize",
		GTK_SIGNAL_FUNC(xpm_realize), (char **)xpm[1]);
#endif
	return (widget);
}

// Release outstanding pointer grabs

#if GTK_MAJOR_VERSION == 3

int release_grab()
{
	GdkDisplay *dp = gdk_display_get_default();
	GList *l, *ll;
	int res = FALSE;

	ll = gdk_device_manager_list_devices(gdk_display_get_device_manager(dp),
		GDK_DEVICE_TYPE_MASTER);
	for (l = ll; l; l = l->next)
	{
		GdkDevice *dev = l->data;
		if ((gdk_device_get_source(dev) == GDK_SOURCE_MOUSE) &&
			gdk_display_device_is_grabbed(dp, dev))
		{
			gdk_device_ungrab(dev, GDK_CURRENT_TIME);
			res = TRUE;
		}
	}
	g_list_free(ll);

	return (res);
}

#else /* if GTK_MAJOR_VERSION <= 2 */

int release_grab()
{
	if (!gdk_pointer_is_grabbed()) return (FALSE);
	gdk_pointer_ungrab(GDK_CURRENT_TIME);
	return (TRUE);
}

#endif

// Frame widget with passthrough scrolling

#if GTK_MAJOR_VERSION == 3

#define WJFRAME(obj)		G_TYPE_CHECK_INSTANCE_CAST(obj, wjframe_get_type(), wjframe)
#define IS_WJFRAME(obj)		G_TYPE_CHECK_INSTANCE_TYPE(obj, wjframe_get_type())

typedef struct
{
	GtkBin		bin;		// Parent class
	GtkAdjustment	*adjustments[2];
	GtkAllocation	inside;
} wjframe;

typedef struct
{
	GtkBinClass parent_class;
} wjframeClass;

G_DEFINE_TYPE_WITH_CODE(wjframe, wjframe, GTK_TYPE_BIN,
	G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL))

#define WJFRAME_SHADOW 1 /* Line width, limited only by common sense */

static gboolean wjframe_draw(GtkWidget *widget, cairo_t *cr)
{
	wjframe *frame = WJFRAME(widget);
	/* !!! Using deprecated struct to avoid recalculating the colors */
	GtkStyle *style = gtk_widget_get_style(widget);
	GtkAllocation alloc;
	double dxy = WJFRAME_SHADOW * 0.5;
	int x, y, x1, y1;


	gtk_widget_get_allocation(widget, &alloc);
	x1 = (x = frame->inside.x - alloc.x) + frame->inside.width - 1;
	y1 = (y = frame->inside.y - alloc.y) + frame->inside.height - 1;

	cairo_save(cr);
	cairo_set_line_width(cr, WJFRAME_SHADOW);
	cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
	gdk_cairo_set_source_color(cr, style->light + GTK_STATE_NORMAL);
	cairo_move_to(cr, x1 + dxy,  y - dxy);
	cairo_line_to(cr, x1 + dxy, y1 + dxy); // Right
	cairo_line_to(cr,  x - dxy, y1 + dxy); // Bottom
	cairo_stroke(cr);
	gdk_cairo_set_source_color(cr, style->dark + GTK_STATE_NORMAL);
	cairo_move_to(cr, x1 + dxy,  y - dxy);
	cairo_line_to(cr,  x - dxy,  y - dxy); // Top
	cairo_line_to(cr,  x - dxy, y1 + dxy); // Left
	cairo_stroke(cr);
	cairo_restore(cr);

	/* To draw child widget */
	GTK_WIDGET_CLASS(wjframe_parent_class)->draw(widget, cr);

	return (FALSE);
}

static void wjframe_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	wjframe *frame = WJFRAME(widget);
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(widget));
	int border = gtk_container_get_border_width(GTK_CONTAINER(widget)) +
		WJFRAME_SHADOW;
	GtkAllocation now_inside;

	gtk_widget_set_allocation(widget, allocation);
	now_inside.x = allocation->x + border;
	now_inside.y = allocation->y + border;
	now_inside.width = MAX(allocation->width - border * 2, 1);
	now_inside.height = MAX(allocation->height - border * 2, 1);

	/* Redraw if inside moved while visible */
	if (gtk_widget_get_mapped(widget) &&
		((frame->inside.x ^ now_inside.x) |
		(frame->inside.y ^ now_inside.y) |
		(frame->inside.width ^ now_inside.width) |
		(frame->inside.height ^ now_inside.height)))
		gdk_window_invalidate_rect(gtk_widget_get_window(widget),
			allocation, FALSE);
	frame->inside = now_inside;

	if (child && gtk_widget_get_visible(child))
		gtk_widget_size_allocate(child, &now_inside);
}

static void wjframe_get_size(GtkWidget *widget, gint vert, gint *min, gint *nat)
{
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(widget));
	int border = gtk_container_get_border_width(GTK_CONTAINER(widget)) +
		WJFRAME_SHADOW;
	gint cmin = 0, cnat = 0;

	if (child && gtk_widget_get_visible(child))
		(vert ? gtk_widget_get_preferred_height :
			gtk_widget_get_preferred_width)(child, &cmin, &cnat);
	*min = cmin + border * 2;
	*nat = cnat + border * 2;
}

static void wjframe_get_preferred_width(GtkWidget *widget, gint *min, gint *nat)
{
	wjframe_get_size(widget, FALSE, min, nat);
}

static void wjframe_get_preferred_height(GtkWidget *widget, gint *min, gint *nat)
{
	wjframe_get_size(widget, TRUE, min, nat);
}

static void wjframe_get_preferred_width_for_height(GtkWidget *widget, gint h,
	 gint *min, gint *nat)
{
	wjframe_get_size(widget, FALSE, min, nat);
}

/* Specialcase only height-for-width, same as GtkFrame does */
static void wjframe_get_preferred_height_for_width(GtkWidget *widget, gint w,
	 gint *min, gint *nat)
{
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(widget));
	int border = gtk_container_get_border_width(GTK_CONTAINER(widget)) +
		WJFRAME_SHADOW;
	gint cmin = 0, cnat = 0;

	if (child && gtk_widget_get_visible(child))
		gtk_widget_get_preferred_height_for_width(child, w - border * 2,
			&cmin, &cnat);
	*min = cmin + border * 2;
	*nat = cnat + border * 2;
}

static void wjframe_set_property(GObject *object, guint prop_id,
	const GValue *value, GParamSpec *pspec)
{
	wjframe *frame = WJFRAME(object);
	GtkWidget *child = gtk_bin_get_child(GTK_BIN(object));

	if ((prop_id == P_HADJ) || (prop_id == P_VADJ))
	{
		/* Cache the object */
		GtkAdjustment *adj, **slot = frame->adjustments + prop_id - P_HADJ;

		adj = g_value_get_object(value);
		if (adj) g_object_ref(adj);
		if (*slot) g_object_unref(*slot);
		*slot = adj;
	}
	// !!! React to "*scroll-policy" as an invalid ID when empty
	else if (child && ((prop_id == P_HSCP) || (prop_id == P_VSCP)));
	else
	{
		G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
		return;
	}
	/* Send the thing to child to handle */
	if (child) g_object_set_property(G_OBJECT(child), scroll_pnames[prop_id], value);
}

static void wjframe_get_property(GObject *object, guint prop_id, GValue *value,
	GParamSpec *pspec)
{
	wjframe *frame = WJFRAME(object);

	if ((prop_id == P_HADJ) || (prop_id == P_VADJ))
		/* Returning cached object */
		g_value_set_object(value, frame->adjustments[prop_id - P_HADJ]);
	else if ((prop_id == P_HSCP) || (prop_id == P_VSCP))
	{
		/* Proxying for child */
		GtkWidget *child = gtk_bin_get_child(GTK_BIN(object));
		if (child) g_object_get_property(G_OBJECT(child),
			scroll_pnames[prop_id], value);
		else g_value_set_enum(value, GTK_SCROLL_NATURAL); // Default
	}
	else G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
}

static void wjframe_add(GtkContainer *container, GtkWidget *child)
{
	wjframe *frame = WJFRAME(container);

	if (gtk_bin_get_child(GTK_BIN(container))) return;
	GTK_CONTAINER_CLASS(wjframe_parent_class)->add(container, child);

	/* Set only existing adjustments */
	if (frame->adjustments[0] || frame->adjustments[1])
		g_object_set(child, "hadjustment", frame->adjustments[0],
			"vadjustment", frame->adjustments[1], NULL);
}

static void wjframe_remove(GtkContainer *container, GtkWidget *child)
{
	wjframe *frame = WJFRAME(container);
	GtkWidget *child0 = gtk_bin_get_child(GTK_BIN(container));

	if (!child || (child0 != child)) return;

	/* Remove only existing adjustments */
	if (frame->adjustments[0] || frame->adjustments[1])
		g_object_set(child, "hadjustment", NULL, "vadjustment", NULL, NULL);

	GTK_CONTAINER_CLASS(wjframe_parent_class)->remove(container, child);
}

static void wjframe_destroy(GtkWidget *widget)
{
	wjframe *frame = WJFRAME(widget);

	if (frame->adjustments[0]) g_object_unref(frame->adjustments[0]);
	if (frame->adjustments[1]) g_object_unref(frame->adjustments[1]);
	frame->adjustments[0] = frame->adjustments[1] = NULL;
	GTK_WIDGET_CLASS(wjframe_parent_class)->destroy(widget);
}

static void wjframe_class_init(wjframeClass *class)
{
	GtkContainerClass *cclass = GTK_CONTAINER_CLASS(class);
	GtkWidgetClass *wclass = GTK_WIDGET_CLASS(class);
	GObjectClass *oclass = G_OBJECT_CLASS(class);

	oclass->set_property = wjframe_set_property;
	oclass->get_property = wjframe_get_property;
	wclass->destroy = wjframe_destroy;
	wclass->draw = wjframe_draw;
	wclass->size_allocate = wjframe_size_allocate;
	wclass->get_preferred_width = wjframe_get_preferred_width;
	wclass->get_preferred_height = wjframe_get_preferred_height;
	wclass->get_preferred_width_for_height = wjframe_get_preferred_width_for_height;
	wclass->get_preferred_height_for_width = wjframe_get_preferred_height_for_width;
	/* !!! Leave my frame alone */
	wclass->style_updated = NULL;
	cclass->add = wjframe_add;
	cclass->remove = wjframe_remove;

	g_object_class_override_property(oclass, P_HADJ, "hadjustment");
	g_object_class_override_property(oclass, P_VADJ, "vadjustment");
	g_object_class_override_property(oclass, P_HSCP, "hscroll-policy");
	g_object_class_override_property(oclass, P_VSCP, "vscroll-policy");
}

static void wjframe_init(wjframe *frame)
{
//	gtk_widget_set_has_window(GTK_WIDGET(frame), FALSE); // GtkBin done it
	frame->adjustments[0] = frame->adjustments[1] = NULL;
}

#else /* if GTK_MAJOR_VERSION <= 2 */

/* !!! Windows builds of GTK+ are made with G_ENABLE_DEBUG, which means also
 * marshallers checking what passes through. Since "OBJECT" is, from their
 * point of view, not "POINTER", we need our own marshaller without the checks,
 * or our "set-scroll-adjustments" handlers won't receive anything but NULLs.
 * Just in case someone does the same on Unix, we use our marshaller with GTK+2
 * regardless of the host OS - WJ */

/* #if defined GDK_WINDOWING_WIN32 */
#if GTK_MAJOR_VERSION == 2

/* Function autogenerated by "glib-genmarshal" utility, then improved a bit - WJ */
#define g_marshal_value_peek_pointer(v) (v)->data[0].v_pointer
static void unchecked_gtk_marshal_VOID__POINTER_POINTER(GClosure *closure,
	GValue *return_value, guint n_param_values, const GValue *param_values,
	gpointer invocation_hint, gpointer marshal_data)
{
	void (*callback)(gpointer data1, gpointer arg1, gpointer arg2, gpointer data2);
	gpointer data1, data2;

	if (n_param_values != 3) return;

	data1 =	data2 = g_value_peek_pointer(param_values + 0);
	if (G_CCLOSURE_SWAP_DATA(closure)) data1 = closure->data;
	else data2 = closure->data;
	callback = marshal_data ? marshal_data : ((GCClosure *)closure)->callback;

	callback(data1, g_marshal_value_peek_pointer(param_values + 1),
		g_marshal_value_peek_pointer(param_values + 2), data2);
}

#undef gtk_marshal_NONE__POINTER_POINTER
#define gtk_marshal_NONE__POINTER_POINTER unchecked_gtk_marshal_VOID__POINTER_POINTER

#endif

#define WJFRAME(obj)		GTK_CHECK_CAST(obj, wjframe_get_type(), wjframe)
#define IS_WJFRAME(obj)		GTK_CHECK_TYPE(obj, wjframe_get_type())

typedef struct
{
	GtkBin bin;	// Parent class
	GtkAdjustment *adjustments[2];
} wjframe;

typedef struct
{
	GtkBinClass parent_class;
	void (*set_scroll_adjustments)(wjframe *frame,
		GtkAdjustment *hadjustment, GtkAdjustment *vadjustment);
} wjframeClass;

static GtkBinClass *bin_class;
static GtkType wjframe_type;

#define WJFRAME_SHADOW 1

static GtkType wjframe_get_type();

static void wjframe_realize(GtkWidget *widget)
{
	GdkWindowAttr attrs;
	int border = GTK_CONTAINER(widget)->border_width;


	GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

	/* Make widget window */
	attrs.x = widget->allocation.x + border;
	attrs.y = widget->allocation.y + border;
	attrs.width = widget->allocation.width - 2 * border;
	attrs.height = widget->allocation.height - 2 * border;
	attrs.window_type = GDK_WINDOW_CHILD;
	attrs.wclass = GDK_INPUT_OUTPUT;
	attrs.visual = gtk_widget_get_visual(widget);
	attrs.colormap = gtk_widget_get_colormap(widget);
	// Window exists only to render the shadow
	attrs.event_mask = gtk_widget_get_events(widget) | GDK_EXPOSURE_MASK;
	widget->window = gdk_window_new(gtk_widget_get_parent_window(widget),
		&attrs, GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP);
	gdk_window_set_user_data(widget->window, widget);

	widget->style = gtk_style_attach(widget->style, widget->window);
	/* Background clear is for wimps :-) */
	gdk_window_set_back_pixmap(widget->window, NULL, FALSE);
// !!! Do this instead if the widget is ever used for non-canvaslike stuff
//	gtk_style_set_background(widget->style, widget->window, GTK_STATE_NORMAL);
}

static void wjframe_paint(GtkWidget *widget, GdkRectangle *area)
{
	GdkWindow *window = widget->window;
	GdkGC *light, *dark;
	gint w, h;
	int x1, y1;

	if (!window || !widget->style) return;
	gdk_window_get_size(window, &w, &h);

#if 0 /* !!! Useless with canvaslike widgets */
	if ((w > WJFRAME_SHADOW * 2) && (h > WJFRAME_SHADOW * 2))
		gtk_paint_flat_box(widget->style, window,
			widget->state, GTK_SHADOW_NONE,
			area, widget, NULL,
			WJFRAME_SHADOW, WJFRAME_SHADOW,
			w - WJFRAME_SHADOW * 2, h - WJFRAME_SHADOW * 2);
#endif

	/* State, shadow, and widget type are hardcoded */
	light = widget->style->light_gc[GTK_STATE_NORMAL];
	dark = widget->style->dark_gc[GTK_STATE_NORMAL];
	gdk_gc_set_clip_rectangle(light, area);
	gdk_gc_set_clip_rectangle(dark, area);

	x1 = w - 1; y1 = h - 1;
	gdk_draw_line(window, light, 0, y1, x1, y1);
	gdk_draw_line(window, light, x1, 0, x1, y1);
	gdk_draw_line(window, dark, 0, 0, x1, 0);
	gdk_draw_line(window, dark, 0, 0, 0, y1);

	gdk_gc_set_clip_rectangle(light, NULL);
	gdk_gc_set_clip_rectangle(dark, NULL);
}
 
#if GTK_MAJOR_VERSION == 1

static void wjframe_draw(GtkWidget *widget, GdkRectangle *area)
{
	GdkRectangle tmp, child;
	GtkBin *bin = GTK_BIN(widget);
	int border = GTK_CONTAINER(widget)->border_width;

	if (!area || !GTK_WIDGET_DRAWABLE(widget)) return;

	tmp = *area;
	tmp.x -= border; tmp.y -= border;
	wjframe_paint(widget, &tmp);

	if (bin->child && gtk_widget_intersect(bin->child, &tmp, &child))
		gtk_widget_draw(bin->child, &child);
}

static gboolean wjframe_expose(GtkWidget *widget, GdkEventExpose *event)
{
	GtkBin *bin = GTK_BIN(widget);

	if (!GTK_WIDGET_DRAWABLE(widget)) return (FALSE);
	wjframe_paint(widget, &event->area);

	if (bin->child && GTK_WIDGET_NO_WINDOW(bin->child))
	{
		GdkEventExpose tmevent = *event;
		if (gtk_widget_intersect(bin->child, &event->area, &tmevent.area))
			gtk_widget_event(bin->child, (GdkEvent *)&tmevent);
	}
	return (FALSE);
}

#else /* if GTK_MAJOR_VERSION == 2 */

static gboolean wjframe_expose(GtkWidget *widget, GdkEventExpose *event)
{
	if (!GTK_WIDGET_DRAWABLE(widget)) return (FALSE);
	wjframe_paint(widget, &event->area);

	GTK_WIDGET_CLASS(bin_class)->expose_event(widget, event);
	return (FALSE);
}

#endif

static void wjframe_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	GtkRequisition req;
	GtkBin *bin = GTK_BIN(widget);
	int border = GTK_CONTAINER(widget)->border_width;

	requisition->width = requisition->height = (WJFRAME_SHADOW + border) * 2;
	if (bin->child && GTK_WIDGET_VISIBLE(bin->child))
	{
		gtk_widget_size_request(bin->child, &req);
		requisition->width += req.width;
		requisition->height += req.height;
	}
}

static void wjframe_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	GtkAllocation alloc;
	GtkBin *bin = GTK_BIN(widget);
	int border = GTK_CONTAINER(widget)->border_width;


	widget->allocation = *allocation;
	alloc.x = alloc.y = WJFRAME_SHADOW;
	alloc.width = MAX(allocation->width - (WJFRAME_SHADOW + border) * 2, 0);
	alloc.height = MAX(allocation->height - (WJFRAME_SHADOW + border) * 2, 0);

	if (GTK_WIDGET_REALIZED(widget)) gdk_window_move_resize(widget->window,
		allocation->x + border, allocation->y + border,
		allocation->width - border * 2, allocation->height - border * 2);

	if (bin->child) gtk_widget_size_allocate(bin->child, &alloc);
}

static void wjframe_set_adjustments(wjframe *frame,
	GtkAdjustment *hadjustment, GtkAdjustment *vadjustment)
{
	if (hadjustment) gtk_object_ref(GTK_OBJECT(hadjustment));
	if (frame->adjustments[0])
		gtk_object_unref(GTK_OBJECT(frame->adjustments[0]));
	frame->adjustments[0] = hadjustment;
	if (vadjustment) gtk_object_ref(GTK_OBJECT(vadjustment));
	if (frame->adjustments[1])
		gtk_object_unref(GTK_OBJECT(frame->adjustments[1]));
	frame->adjustments[1] = vadjustment;
}

static void wjframe_set_scroll_adjustments(wjframe *frame,
	GtkAdjustment *hadjustment, GtkAdjustment *vadjustment)
{
	GtkBin *bin = GTK_BIN(frame);

	if ((hadjustment == frame->adjustments[0]) &&
		(vadjustment == frame->adjustments[1])) return;

	wjframe_set_adjustments(frame, hadjustment, vadjustment);

	if (bin->child) gtk_widget_set_scroll_adjustments(bin->child,
		hadjustment, vadjustment);
}

static void wjframe_add(GtkContainer *container, GtkWidget *child)
{
	wjframe *frame = WJFRAME(container);
	GtkBin *bin = GTK_BIN(container);
	GtkWidget *widget = GTK_WIDGET(container);


	if (bin->child) return;
	bin->child = child;
	gtk_widget_set_parent(child, widget);

	/* Set only existing adjustments */
	if (frame->adjustments[0] || frame->adjustments[1])
		gtk_widget_set_scroll_adjustments(child,
			frame->adjustments[0], frame->adjustments[1]);

#if GTK_MAJOR_VERSION == 1
	if (GTK_WIDGET_REALIZED(widget)) gtk_widget_realize(child);

	if (GTK_WIDGET_VISIBLE(widget) && GTK_WIDGET_VISIBLE(child))
	{
		if (GTK_WIDGET_MAPPED(widget)) gtk_widget_map(child);
		gtk_widget_queue_resize(child);
	}
#endif
}

static void wjframe_remove(GtkContainer *container, GtkWidget *child)
{
	wjframe *frame = WJFRAME(container);
	GtkBin *bin = GTK_BIN(container);
	

	if (!child || (bin->child != child)) return;

	/* Remove only existing adjustments */
	if (frame->adjustments[0] || frame->adjustments[1])
		gtk_widget_set_scroll_adjustments(child, NULL, NULL);

	GTK_CONTAINER_CLASS(bin_class)->remove(container, child);
}

static void wjframe_destroy(GtkObject *object)
{
	wjframe_set_adjustments(WJFRAME(object), NULL, NULL);

	GTK_OBJECT_CLASS(bin_class)->destroy(object);
}

static void wjframe_class_init(wjframeClass *class)
{
	GtkWidgetClass *wclass = GTK_WIDGET_CLASS(class);
	GtkContainerClass *cclass = GTK_CONTAINER_CLASS(class);

	bin_class = gtk_type_class(GTK_TYPE_BIN);
	GTK_OBJECT_CLASS(class)->destroy = wjframe_destroy;
#if GTK_MAJOR_VERSION == 1
	wclass->draw = wjframe_draw;
#endif
	wclass->realize = wjframe_realize;
	wclass->expose_event = wjframe_expose;
	wclass->size_request = wjframe_size_request;
	wclass->size_allocate = wjframe_size_allocate;
	// !!! Default "style_set" handler can reenable background clear
	wclass->style_set = NULL;
	cclass->add = wjframe_add;
	cclass->remove = wjframe_remove;
	class->set_scroll_adjustments = wjframe_set_scroll_adjustments;

	wclass->set_scroll_adjustments_signal = gtk_signal_new(
		"set_scroll_adjustments", GTK_RUN_LAST, wjframe_type,
		GTK_SIGNAL_OFFSET(wjframeClass, set_scroll_adjustments),
		gtk_marshal_NONE__POINTER_POINTER, GTK_TYPE_NONE,
		2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);
}

static void wjframe_init(wjframe *frame)
{
	GTK_WIDGET_UNSET_FLAGS(frame, GTK_NO_WINDOW);
#if GTK_MAJOR_VERSION == 2
	// !!! Would only waste time, with canvaslike child widgets
	GTK_WIDGET_UNSET_FLAGS(frame, GTK_DOUBLE_BUFFERED);
#endif
	frame->adjustments[0] = frame->adjustments[1] = NULL;
}

static GtkType wjframe_get_type()
{
	if (!wjframe_type)
	{
		static const GtkTypeInfo wjframe_info = {
			"wjFrame", sizeof(wjframe), sizeof(wjframeClass),
			(GtkClassInitFunc)wjframe_class_init,
			(GtkObjectInitFunc)wjframe_init,
			NULL, NULL, NULL };
		wjframe_type = gtk_type_unique(GTK_TYPE_BIN, &wjframe_info);
	}

	return (wjframe_type);
}

#endif /* GTK+1&2 */

GtkWidget *wjframe_new()
{
	return (gtk_widget_new(wjframe_get_type(), NULL));
}

// Scrollable canvas widget

#if GTK_MAJOR_VERSION == 3

#define WCACHE_STEP 64	/* Dimensions are multiples of this */
#define WCACHE_FRAC 2	/* Can be this larger than necessary */

typedef struct {
	cairo_surface_t *s;	// Surface
	int	xy[4];		// Viewport
	int	dx;		// X offset
	int	dy;		// Y offset
	int	w;		// Cache line width
	int	h;		// Cache height including boundary rows
	int	scale;		// Window scale factor
} wcache;

//	Create cache for viewport
static void wcache_init(wcache *cache, int *vport, GdkWindow *win)
{
	cache->w = vport[2] - vport[0] + WCACHE_STEP - 1;
	cache->h = vport[3] - vport[1] + WCACHE_STEP - 1;
	cache->w -= cache->w % WCACHE_STEP;
	cache->h -= cache->h % WCACHE_STEP;
	copy4(cache->xy, vport);
	cache->dx = cache->dy = 0;
	cache->scale = gdk_window_get_scale_factor(win);
	cache->s = gdk_window_create_similar_surface(win,
		CAIRO_CONTENT_COLOR, cache->w, cache->h);
}	

//	Check if cache need be replaced, align it to vport if not
static int wcache_check(wcache *cache, int *vport, int empty)
{
	int tw = vport[2] - vport[0], th = vport[3] - vport[1];

	if (!cache->s) return (TRUE);
	while ((tw <= cache->w) && (th <= cache->h))
	{
		tw += WCACHE_STEP - 1; tw -= tw % WCACHE_STEP;
		th += WCACHE_STEP - 1; th -= th % WCACHE_STEP;
		if (tw * th * WCACHE_FRAC < cache->w * cache->h) break;
		/* Adjust for new vport */
		if (empty)
		{
			cache->dx = 0;
			cache->dy = 0;
		}
		else if (memcmp(cache->xy, vport, sizeof(cache->xy)))
		{
			cache->dx = floor_mod(cache->dx + vport[0] - cache->xy[0],
				cache->w);
			cache->dy = floor_mod(cache->dy + vport[1] - cache->xy[1],
				cache->h);
		}
		copy4(cache->xy, vport);

		return (FALSE); // Leave be
	}
	/* Drop the cache if no data inside */
	if (empty)
	{
		cairo_surface_fdestroy(cache->s);
		cache->s = NULL;
	}
	return (TRUE);
}

//	Move data from old cache to new
static void wcache_move(wcache *new, wcache *old)
{
	int rxy[4];
	cairo_t *cr;

	if (!old->s) return;
	/* Copy matching part of old's spans into new */
	if (clip(rxy, old->xy[0], old->xy[1], old->xy[2], old->xy[3], new->xy))
	{
		cr = cairo_create(new->s);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(cr, old->s, old->xy[0] - new->xy[0] - old->dx, 
			old->xy[1] - new->xy[1] - old->dy);
		cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
		cairo_rectangle(cr, rxy[0] - new->xy[0], rxy[1] - new->xy[1],
			rxy[2] - rxy[0], rxy[3] - rxy[1]);
		cairo_fill(cr);
		cairo_destroy(cr);
	}

	/* Drop the old cache */
	cairo_surface_fdestroy(old->s);
	old->s = NULL;
}

static void wcache_render(wcache *cache, cairo_t *cr)
{
	int h = cache->xy[3] - cache->xy[1], w = cache->xy[2] - cache->xy[0];

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, cache->s, -cache->dx, -cache->dy);
	cairo_pattern_set_extend(cairo_get_source(cr), CAIRO_EXTEND_REPEAT);
	cairo_rectangle(cr, 0, 0, w, h);
	cairo_fill(cr);
	cairo_restore(cr);
}

#define WJCANVAS(obj)		G_TYPE_CHECK_INSTANCE_CAST(obj, wjcanvas_get_type(), wjcanvas)
#define IS_WJCANVAS(obj)	G_TYPE_CHECK_INSTANCE_TYPE(obj, wjcanvas_get_type())

typedef void (*wjc_expose_f)(GtkWidget *widget, cairo_region_t *clip, gpointer user_data);

typedef struct
{
	GtkWidget	widget;		// Parent class
	GtkAdjustment	*adjustments[2];
	cairo_region_t	*r;		// Cached region
	int		xy[4];		// Viewport
	int		size[2];	// Requested (virtual) size
	int		resize;		// Resize was requested
	int		resizing;	// Requested resize is happening
	guint32		scrolltime;	// For autoscroll rate-limiting
	wcache		cache;		// Pixel cache
	wjc_expose_f	expose;		// Drawing function
	gpointer	udata;		// Link to slot
} wjcanvas;

typedef struct
{
	GtkWidgetClass parent_class;
} wjcanvasClass;

G_DEFINE_TYPE_WITH_CODE(wjcanvas, wjcanvas, GTK_TYPE_WIDGET,
	G_IMPLEMENT_INTERFACE(GTK_TYPE_SCROLLABLE, NULL))

static gboolean wjcanvas_draw(GtkWidget *widget, cairo_t *cr)
{
	wjcanvas *canvas = WJCANVAS(widget);
	cairo_region_t *clip;
	cairo_rectangle_list_t *rl;
	cairo_rectangle_int_t re;
	wcache ncache;
	int i;

	/* Check if anything useful remains in cache */
	if (canvas->r)
	{
		cairo_rectangle_int_t vport = { canvas->xy[0], canvas->xy[1],
			canvas->xy[2] - canvas->xy[0], canvas->xy[3] - canvas->xy[1] };
		cairo_region_intersect_rectangle(canvas->r, &vport);
		if (cairo_region_is_empty(canvas->r))
		{
			cairo_region_destroy(canvas->r);
			canvas->r = NULL;
		}
	}

	/* Check if need a new cache */
	if (wcache_check(&canvas->cache, canvas->xy, !canvas->r))
	{
		wcache_init(&ncache, canvas->xy, gtk_widget_get_window(widget)); // Create it
		wcache_move(&ncache, &canvas->cache); // Move contents if needed
		canvas->cache = ncache;
	}

	/* Convert clip to image-space region */
	clip = cairo_region_create();
	rl = cairo_copy_clip_rectangle_list(cr);
	if (rl->status != CAIRO_STATUS_SUCCESS) // Paranoia fallback
	{
		// The two rectangle types documented as identical in GTK+3 docs
		if (gdk_cairo_get_clip_rectangle(cr, (GdkRectangle*)&re))
			cairo_region_union_rectangle(clip, &re);
	}
	else for (i = 0; i < rl->num_rectangles; i++)
	{
		cairo_rectangle_t *rd = rl->rectangles + i;
		// GTK+3 assumes the values are converted ints
		re.x = rd->x; re.y = rd->y;
		re.width = rd->width; re.height = rd->height;
		cairo_region_union_rectangle(clip, &re);
	}
	cairo_rectangle_list_destroy(rl);
	cairo_region_translate(clip, canvas->xy[0], canvas->xy[1]);

	/* Check if we need draw anything anew */
	if (canvas->r) cairo_region_subtract(clip, canvas->r);
	if (!cairo_region_is_empty(clip) && canvas->expose) // Do nothing if unset
		canvas->expose((GtkWidget *)canvas, clip, canvas->udata);
	cairo_region_destroy(clip);

	wcache_render(&canvas->cache, cr);

	return (FALSE);
}

static void wjcanvas_send_configure(GtkWidget *widget, GtkAllocation *alloc)
{
	GdkEvent *event = gdk_event_new(GDK_CONFIGURE);

	event->configure.window = g_object_ref(gtk_widget_get_window(widget));
	event->configure.send_event = TRUE;
	event->configure.x = alloc->x;
	event->configure.y = alloc->y;
	event->configure.width = alloc->width;
	event->configure.height = alloc->height;

	gtk_widget_event(widget, event);
	gdk_event_free(event);
}

static void wjcanvas_realize(GtkWidget *widget)
{
//	static const GdkRGBA black = { 0, 0, 0, 0 };
	GdkWindow *win;
	GdkWindowAttr attrs;
	GtkAllocation alloc;

	gtk_widget_set_realized(widget, TRUE);
	gtk_widget_get_allocation(widget, &alloc);

	attrs.x = alloc.x;
	attrs.y = alloc.y;
	attrs.width = alloc.width;
	attrs.height = alloc.height;
	attrs.window_type = GDK_WINDOW_CHILD;
	attrs.wclass = GDK_INPUT_OUTPUT;
	attrs.visual = gtk_widget_get_visual(widget);
	/* !!! GtkViewport also sets GDK_TOUCH_MASK and GDK_SMOOTH_SCROLL_MASK,
	 * but the latter blocks non-smooth scroll events if device sends
	 * smooth ones, and the former, I do not (yet?) handle anyway - WJ */
	attrs.event_mask = gtk_widget_get_events(widget) | GDK_SCROLL_MASK;
	/* !!! GDK_EXPOSURE_MASK seems really not be needed (as advertised),
	 * despite GtkDrawingArea still having it */
	win = gdk_window_new(gtk_widget_get_parent_window(widget),
		&attrs, GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);
	/* !!! Versions without this (below 3.12) are useless */
	gdk_window_set_event_compression(win, FALSE);

	gtk_widget_register_window(widget, win);
	gtk_widget_set_window(widget, win);

//	gdk_window_set_background_rgba(win, &black);
// !!! In hope this (parent's bkg) isn't drawn twice
	gdk_window_set_background_pattern(win, NULL);

	/* Replicate behaviour of GtkDrawingCanvas */
	wjcanvas_send_configure(widget, &alloc);
}

static int wjcanvas_readjust(wjcanvas *canvas, int which, GtkAllocation *alloc)
{
	GtkAdjustment *adj = canvas->adjustments[which];
	int sz, wp;
	double oldv, newv;

	oldv = gtk_adjustment_get_value(adj);
	wp = which ? alloc->height : alloc->width;
	sz = canvas->size[which];
	if (sz < wp) sz = wp;
	newv = oldv < 0.0 ? 0.0 : oldv > sz - wp ? sz - wp : oldv;
	gtk_adjustment_configure(adj, newv, 0, sz, wp * 0.1, wp * 0.9, wp);

	return (newv != oldv);
}

static void wjcanvas_set_extents(wjcanvas *canvas, GtkAllocation *alloc)
{
	canvas->xy[2] = alloc->width + (canvas->xy[0] = 
		(int)rint(gtk_adjustment_get_value(canvas->adjustments[0])));
	canvas->xy[3] = alloc->height + (canvas->xy[1] =
		(int)rint(gtk_adjustment_get_value(canvas->adjustments[1])));
}

static void wjcanvas_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	wjcanvas *canvas = WJCANVAS(widget);
	GtkAllocation alloc0;
	int conf;


	/* Don't send useless configure events */
	gtk_widget_get_allocation(widget, &alloc0);
	conf = canvas->resize | (allocation->width ^ alloc0.width) |
		(allocation->height ^ alloc0.height);
	canvas->resizing = canvas->resize;
	canvas->resize = FALSE;

	gtk_widget_set_allocation(widget, allocation);

	g_object_freeze_notify(G_OBJECT(canvas->adjustments[0]));
	g_object_freeze_notify(G_OBJECT(canvas->adjustments[1]));
	wjcanvas_readjust(canvas, 0, allocation);
	wjcanvas_readjust(canvas, 1, allocation);
	wjcanvas_set_extents(canvas, allocation);

	if (gtk_widget_get_realized(widget))
	{
		/* In GTK+3 this'll do whole window redraw for any change */
		gdk_window_move_resize(gtk_widget_get_window(widget),
			allocation->x, allocation->y,
			allocation->width, allocation->height);

		/* Replicate behaviour of GtkDrawingCanvas */
		if (conf) wjcanvas_send_configure(widget, allocation);
	}

	g_object_thaw_notify(G_OBJECT(canvas->adjustments[0]));
	g_object_thaw_notify(G_OBJECT(canvas->adjustments[1]));
	canvas->resizing = FALSE;
}

static void wjcanvas_get_preferred_width(GtkWidget *widget, gint *min, gint *nat)
{
	*min = 1;
	*nat = WJCANVAS(widget)->size[0];
}

static void wjcanvas_get_preferred_height(GtkWidget *widget, gint *min, gint *nat)
{
	*min = 1;
	*nat = WJCANVAS(widget)->size[1];
}

#if 0 /* Direct descendants of GtkWidget can let it redirect these two */
static void wjcanvas_get_preferred_width_for_height(GtkWidget *widget, gint h,
	 gint *min, gint *nat)
{
	*min = 1;
	*nat = WJCANVAS(widget)->size[0];
}

static void wjcanvas_get_preferred_height_for_width(GtkWidget *widget, gint w,
	 gint *min, gint *nat)
{
	*min = 1;
	*nat = WJCANVAS(widget)->size[1];
}
#endif

/* We do scrolling in both directions at once if possible, to avoid ultimately
 * useless repaint ops and associated flickers - WJ */
static void wjcanvas_adjustment_value_changed(GtkAdjustment *adjustment, gpointer data)
{
	GtkWidget *widget = data;
	wjcanvas *canvas = data;
	GtkAllocation alloc;
	int oxy[4];

	if (!GTK_IS_ADJUSTMENT(adjustment) || !IS_WJCANVAS(data)) return;
	gtk_widget_get_allocation(widget, &alloc);
	copy4(oxy, canvas->xy);
	wjcanvas_set_extents(data, &alloc); // Set new window extents
	/* No scrolling in GTK+3, rely on caching */
	if (!gtk_widget_get_mapped(widget)) return;
	if ((oxy[0] ^ canvas->xy[0]) | (oxy[1] ^ canvas->xy[1])) // If moved
		gtk_widget_queue_draw(widget);
}

static void wjcanvas_drop_adjustment(wjcanvas *canvas, int which)
{
	GtkAdjustment **slot = canvas->adjustments + which;

	if (!*slot) return;
	g_signal_handlers_disconnect_by_func(*slot,
		wjcanvas_adjustment_value_changed, canvas);
	g_object_unref(*slot);
	*slot = NULL;
}

static GtkAdjustment *wjcanvas_prepare_adjustment(wjcanvas *canvas, int which,
	GtkAdjustment *adjustment)
{
	GtkAdjustment **slot = canvas->adjustments + which;

	if (adjustment && (adjustment == *slot)) return (NULL); // Leave alone
	if (!adjustment) adjustment = gtk_adjustment_new(0, 0, 0, 0, 0, 0);
	wjcanvas_drop_adjustment(canvas, which);
	g_object_ref_sink(*slot = adjustment);
	g_signal_connect(adjustment, "value-changed",
		G_CALLBACK(wjcanvas_adjustment_value_changed), canvas);
	return (adjustment);
}

static void wjcanvas_set_property(GObject *object, guint prop_id,
	const GValue *value, GParamSpec *pspec)
{
	if ((prop_id == P_HADJ) || (prop_id == P_VADJ))
	{
		wjcanvas *canvas = WJCANVAS(object);
		GtkAllocation alloc;
		GtkAdjustment *adj;
		int which = prop_id - P_HADJ;

		gtk_widget_get_allocation(GTK_WIDGET(canvas), &alloc);
		adj = wjcanvas_prepare_adjustment(
			canvas, which, g_value_get_object(value));
		if (adj && !wjcanvas_readjust(canvas, which, &alloc))
			wjcanvas_adjustment_value_changed(adj, canvas); // Readjust anyway
	}
	// !!! React to "*scroll-policy" as an invalid ID if trying to set
	else G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
}

static void wjcanvas_get_property(GObject *object, guint prop_id, GValue *value,
	GParamSpec *pspec)
{
	wjcanvas *canvas = WJCANVAS(object);

	if ((prop_id == P_HADJ) || (prop_id == P_VADJ))
		g_value_set_object(value, canvas->adjustments[prop_id - P_HADJ]);
	else if ((prop_id == P_HSCP) || (prop_id == P_VSCP))
		g_value_set_enum(value, GTK_SCROLL_NATURAL);
	else G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
}

static void wjcanvas_drop_cache(wjcanvas *canvas)
{
	cairo_region_destroy(canvas->r);
	canvas->r = NULL;
	if (canvas->cache.s)
	{
		cairo_surface_fdestroy(canvas->cache.s);
		canvas->cache.s = NULL;
	}
}

static void wjcanvas_scale_change(GObject *object, GParamSpec *pspec, gpointer user_data)
{
	wjcanvas *canvas = WJCANVAS(object);

	if (canvas->cache.s && (canvas->cache.scale != 
		gtk_widget_get_scale_factor(GTK_WIDGET(canvas))))
		wjcanvas_drop_cache(canvas);
}

static void wjcanvas_unmap(GtkWidget *widget)
{
	wjcanvas *canvas = WJCANVAS(widget);

	GTK_WIDGET_CLASS(wjcanvas_parent_class)->unmap(widget);
	wjcanvas_drop_cache(canvas);
}

static void wjcanvas_destroy(GtkWidget *widget)
{
	wjcanvas *canvas = WJCANVAS(widget);

	wjcanvas_drop_adjustment(canvas, 0);
	wjcanvas_drop_adjustment(canvas, 1);
	wjcanvas_drop_cache(canvas);
	GTK_WIDGET_CLASS(wjcanvas_parent_class)->destroy(widget);
}

static void wjcanvas_class_init(wjcanvasClass *class)
{
	GtkWidgetClass *wclass = GTK_WIDGET_CLASS(class);
	GObjectClass *oclass = G_OBJECT_CLASS(class);

	oclass->set_property = wjcanvas_set_property;
	oclass->get_property = wjcanvas_get_property;
	wclass->destroy = wjcanvas_destroy;
	wclass->realize = wjcanvas_realize;
	wclass->unmap = wjcanvas_unmap;
	wclass->draw = wjcanvas_draw;
	wclass->size_allocate = wjcanvas_size_allocate;
	wclass->get_preferred_width = wjcanvas_get_preferred_width;
	wclass->get_preferred_height = wjcanvas_get_preferred_height;
	/* Default handlers in GtkWidget redirect these two anyway */
//	wclass->get_preferred_width_for_height = wjcanvas_get_preferred_width_for_height;
//	wclass->get_preferred_height_for_width = wjcanvas_get_preferred_height_for_width;
	/* !!! Do not disturb my circles */
	wclass->style_updated = NULL;

	g_object_class_override_property(oclass, P_HADJ, "hadjustment");
	g_object_class_override_property(oclass, P_VADJ, "vadjustment");
	g_object_class_override_property(oclass, P_HSCP, "hscroll-policy");
	g_object_class_override_property(oclass, P_VSCP, "vscroll-policy");
}

static void wjcanvas_init(wjcanvas *canvas)
{
	gtk_widget_set_has_window(GTK_WIDGET(canvas), TRUE);
	gtk_widget_set_redraw_on_allocate(GTK_WIDGET(canvas), FALSE);
	/* Ensure 1x1 at least */
	canvas->size[0] = canvas->size[1] = 1;
	/* Install fake adjustments */
	canvas->adjustments[0] = canvas->adjustments[1] = NULL;
	wjcanvas_prepare_adjustment(canvas, 0, NULL);
	wjcanvas_prepare_adjustment(canvas, 1, NULL);
	/* No cached things yet */
	canvas->r = NULL;
	/* And no cache, too */
	memset(&canvas->cache, 0, sizeof(canvas->cache));
	/* Track scale */
	g_signal_connect(canvas, "notify::scale-factor",
		G_CALLBACK(wjcanvas_scale_change), NULL);
}

void wjcanvas_draw_rgb(GtkWidget *widget, int x, int y, int w, int h,
	unsigned char *rgb, int step, int fill, int repaint)
{
	wjcanvas *canvas;
	wcache *cache;
	cairo_surface_t *s = NULL;
	cairo_t *cr;
	cairo_rectangle_int_t re;
	int y1, x1, rxy[4];

	if (!IS_WJCANVAS(widget)) return;
	canvas = WJCANVAS(widget);
	cache = &canvas->cache;
	if (!cache->s) return; // Not visible yet, draw it later
	if (!clip(rxy, x, y, x + w, y + h, cache->xy)) return;

	if (rgb) s = cairo_upload_rgb(cache->s, NULL, rgb + (rxy[1] - y) * step +
		(rxy[0] - x) * 3, rxy[2] - rxy[0], rxy[3] - rxy[1], step);

	/* Find out whether area wraps around on X */
	x = (cache->dx + rxy[0] - cache->xy[0]) % cache->w;
	w = rxy[2] - rxy[0];
	x1 = (x + w) % cache->w;
	if (x1 < w) w -= x1;
	else x1 = 0;

	/* Same thing on Y */
	y = (cache->dy + rxy[1] - cache->xy[1]) % cache->h;
	h = rxy[3] - rxy[1];
	y1 = (y + h) % cache->h;
	if (y1 < h) h -= y1;
	else y1 = 0;

	cr = cairo_create(cache->s);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	if (!s) cairo_set_rgb(cr, fill); // If no bitmap, fill by color

	/* Paint the area into cache, in up to 4 pieces */
	if (s) cairo_set_source_surface(cr, s, x, y);
	cairo_unfilter(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);
	if (y1)
	{
		if (s)
		{
			cairo_set_source_surface(cr, s, x, -h);
			cairo_unfilter(cr);
		}
		cairo_rectangle(cr, x, 0, w, y1);
		cairo_fill(cr);
	}
	if (x1)
	{
		if (s)
		{
			cairo_set_source_surface(cr, s, -w, y);
			cairo_unfilter(cr);
		}
		cairo_rectangle(cr, 0, y, x1, h);
		cairo_fill(cr);
		if (y1)
		{
			if (s)
			{
				cairo_set_source_surface(cr, s, -w, -h);
				cairo_unfilter(cr);
			}
			cairo_rectangle(cr, 0, 0, x1, y1);
			cairo_fill(cr);
		}
	}
	cairo_destroy(cr);

	if (s) cairo_surface_fdestroy(s);

	/* Remember this part is up to date */
	if (!canvas->r) canvas->r = cairo_region_create();
	re.width = rxy[2] - (re.x = rxy[0]);
	re.height = rxy[3] - (re.y = rxy[1]);
	cairo_region_union_rectangle(canvas->r, &re);

	/* Invalidate part of window if asked to */
	if (repaint)
	{
		re.x -= canvas->xy[0];
		re.y -= canvas->xy[1];
		// The two rectangle types documented as identical in GTK+3 docs
		gdk_window_invalidate_rect(gtk_widget_get_window(widget),
			(GdkRectangle*)&re, FALSE);
	}
}

// !!! To be called from CANVAS_REPAINT w/area, & from cmd_repaint() with NULL
void wjcanvas_uncache(GtkWidget *widget, int *rxy)
{
	wjcanvas *canvas = WJCANVAS(widget);
	if (!canvas->r); // Nothing more to do
	else if (!rxy) // Total clear
	{
		cairo_region_destroy(canvas->r);
		canvas->r = NULL;
	}
	else // Partial clear
	{
		cairo_rectangle_int_t re = {
			rxy[0], rxy[1], rxy[2] - rxy[0], rxy[3] - rxy[1] };
		cairo_region_subtract_rectangle(canvas->r, &re);
	}
}

void wjcanvas_set_expose(GtkWidget *widget, GCallback handler, gpointer user_data)
{
	wjcanvas *canvas;

	if (!IS_WJCANVAS(widget)) return;
	canvas = WJCANVAS(widget);
	canvas->expose = (wjc_expose_f)handler;
	canvas->udata = user_data;
}

void wjcanvas_size(GtkWidget *widget, int width, int height)
{
	wjcanvas *canvas;

	if (!IS_WJCANVAS(widget)) return;
	canvas = WJCANVAS(widget);
	if ((canvas->size[0] == width) && (canvas->size[1] == height)) return;
	canvas->size[0] = width;
	canvas->size[1] = height;
	canvas->resize = TRUE;
	/* Forget cached rectangles */
	cairo_region_destroy(canvas->r);
	canvas->r = NULL;
	gtk_widget_queue_resize(widget);
}

static int wjcanvas_offset(GtkAdjustment *adj, int dv)
{
	double nv, up, v;
	int step, dw = dv;

	step = (int)(gtk_adjustment_get_step_increment(adj) + 0.5);
	if (step)
	{
		dw = abs(dw) + step - 1;
		dw -= dw % step;
		if (dv < 0) dw = -dw;
	}

	up = gtk_adjustment_get_upper(adj) - gtk_adjustment_get_page_size(adj);
	nv = (v = gtk_adjustment_get_value(adj)) + dw;
	if (nv > up) nv = up;
	if (nv < 0.0) nv = 0.0;
	gtk_adjustment_set_value(adj, nv);
	return (v != nv);
}

int wjcanvas_scroll_in(GtkWidget *widget, int x, int y)
{
	wjcanvas *canvas = WJCANVAS(widget);
	int dx = 0, dy = 0;

	if (!canvas->adjustments[0] || !canvas->adjustments[1]) return (FALSE);

	if (x < canvas->xy[0]) dx = (x < 0 ? 0 : x) - canvas->xy[0];
	else if (x >= canvas->xy[2]) dx = (x >= canvas->size[0] ?
		canvas->size[0] : x + 1) - canvas->xy[2];
	if (y < canvas->xy[1]) dy = (y < 0 ? 0 : y) - canvas->xy[1];
	else if (y >= canvas->xy[3]) dy = (y >= canvas->size[1] ?
		canvas->size[1] : y + 1) - canvas->xy[3];
	if (!(dx | dy)) return (FALSE);

	g_object_freeze_notify(G_OBJECT(canvas->adjustments[0]));
	g_object_freeze_notify(G_OBJECT(canvas->adjustments[1]));

	dx = wjcanvas_offset(canvas->adjustments[0], dx);
	dy = wjcanvas_offset(canvas->adjustments[1], dy);

	g_object_thaw_notify(G_OBJECT(canvas->adjustments[0]));
	g_object_thaw_notify(G_OBJECT(canvas->adjustments[1]));
	return (dx | dy);
}

#define WJCANVAS_SCROLL_LIMIT 333 /* 3 steps/second */

/* If mouse moved outside canvas, scroll canvas & warp cursor back in */
int wjcanvas_bind_mouse(GtkWidget *widget, GdkEventMotion *event, int x, int y)
{
	wjcanvas *canvas = WJCANVAS(widget);
	int oldv[4];

	copy4(oldv, canvas->xy);
	x += oldv[0]; y += oldv[1];
	if ((x >= oldv[0]) && (x < oldv[2]) && (y >= oldv[1]) && (y < oldv[3]))
		return (FALSE);
	/* Limit scrolling rate for absolute pointing devices */
	if ((gdk_device_get_source(event->device) != GDK_SOURCE_MOUSE) &&
		(event->time < canvas->scrolltime + WJCANVAS_SCROLL_LIMIT))
		return (FALSE);
	if (!wjcanvas_scroll_in(widget, x, y)) return (FALSE);
	canvas->scrolltime = event->time;
	return (move_mouse_relative(oldv[0] - canvas->xy[0], oldv[1] - canvas->xy[1]));
}

#else /* if GTK_MAJOR_VERSION <= 2 */

#define WJCANVAS(obj)		GTK_CHECK_CAST(obj, wjcanvas_get_type(), wjcanvas)
#define IS_WJCANVAS(obj)	GTK_CHECK_TYPE(obj, wjcanvas_get_type())

typedef struct
{
	GtkWidget	widget;		// Parent class
	GtkAdjustment	*adjustments[2];
	GdkGC		*scroll_gc;	// For scrolling in GTK+1
	int		xy[4];		// Viewport
	int		size[2];	// Requested (virtual) size
	int		resize;		// Resize was requested
	int		resizing;	// Requested resize is happening
	guint32		scrolltime;	// For autoscroll rate-limiting
} wjcanvas;

typedef struct
{
	GtkWidgetClass parent_class;
	void  (*set_scroll_adjustments)(wjcanvas *canvas,
		GtkAdjustment *hadjustment, GtkAdjustment *vadjustment);
} wjcanvasClass;

static GtkWidgetClass *widget_class;
static GtkType wjcanvas_type;

static GtkType wjcanvas_get_type();

static void wjcanvas_send_configure(GtkWidget *widget)
{
#if GTK2VERSION >= 2 /* GTK+ 2.2+ */
	GdkEvent *event = gdk_event_new(GDK_CONFIGURE);

	event->configure.window = g_object_ref(widget->window);
	event->configure.send_event = TRUE;
	event->configure.x = widget->allocation.x;
	event->configure.y = widget->allocation.y;
	event->configure.width = widget->allocation.width;
	event->configure.height = widget->allocation.height;

	gtk_widget_event(widget, event);
	gdk_event_free(event);
#else /* GTK+ 1.x or 2.0 */
	GdkEventConfigure event;

	event.type = GDK_CONFIGURE;
	event.window = widget->window;
	event.send_event = TRUE;
	event.x = widget->allocation.x;
	event.y = widget->allocation.y;
	event.width = widget->allocation.width;
	event.height = widget->allocation.height;

	gdk_window_ref(event.window);
	gtk_widget_event(widget, (GdkEvent *)&event);
	gdk_window_unref(event.window);
#endif
}

static void wjcanvas_realize(GtkWidget *widget)
{
	GdkWindowAttr attrs;


	GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

	attrs.x = widget->allocation.x;
	attrs.y = widget->allocation.y;
	attrs.width = widget->allocation.width;
	attrs.height = widget->allocation.height;
	attrs.window_type = GDK_WINDOW_CHILD;
	attrs.wclass = GDK_INPUT_OUTPUT;
	attrs.visual = gtk_widget_get_visual(widget);
	attrs.colormap = gtk_widget_get_colormap(widget);
	// Add the same events as GtkViewport does
	attrs.event_mask = gtk_widget_get_events(widget) | GDK_EXPOSURE_MASK |
		GDK_BUTTON_PRESS_MASK;
	widget->window = gdk_window_new(gtk_widget_get_parent_window(widget),
		&attrs, GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP);
	gdk_window_set_user_data(widget->window, widget);

#if GTK_MAJOR_VERSION == 1
	fix_gdk_events(widget->window);
	WJCANVAS(widget)->scroll_gc = gdk_gc_new(widget->window);
	gdk_gc_set_exposures(WJCANVAS(widget)->scroll_gc, TRUE);
#endif

	widget->style = gtk_style_attach(widget->style, widget->window);
	gdk_window_set_back_pixmap(widget->window, NULL, FALSE);

	/* Replicate behaviour of GtkDrawingCanvas */
	wjcanvas_send_configure(widget);
}

#if GTK_MAJOR_VERSION == 1

static void wjcanvas_unrealize(GtkWidget *widget)
{
	wjcanvas *canvas = WJCANVAS(widget);

	gdk_gc_destroy(canvas->scroll_gc);
	canvas->scroll_gc = NULL;

	if (widget_class->unrealize) widget_class->unrealize(widget);
}

#endif

static int wjcanvas_readjust(wjcanvas *canvas, int which)
{
	GtkWidget *widget = GTK_WIDGET(canvas);
	GtkAdjustment *adj = canvas->adjustments[which];
	int sz, wp;
	double oldv, newv;

	oldv = adj->value;
	wp = which ? widget->allocation.height : widget->allocation.width;
	sz = canvas->size[which];
	if (sz < wp) sz = wp;
	adj->page_size = wp;
	adj->step_increment = wp * 0.1;
	adj->page_increment = wp * 0.9;
	adj->lower = 0;
	adj->upper = sz;
	adj->value = newv = oldv < 0.0 ? 0.0 : oldv > sz - wp ? sz - wp : oldv;

	return (newv != oldv);
}

static void wjcanvas_set_extents(wjcanvas *canvas)
{
	GtkWidget *widget = GTK_WIDGET(canvas);

	canvas->xy[2] = (canvas->xy[0] = ADJ2INT(canvas->adjustments[0])) +
		widget->allocation.width;
	canvas->xy[3] = (canvas->xy[1] = ADJ2INT(canvas->adjustments[1])) +
		widget->allocation.height;
}

#if GTK_MAJOR_VERSION == 1

static void wjcanvas_send_expose(GtkWidget *widget, int x, int y, int w, int h)
{
	GdkEventExpose event;

	event.type = GDK_EXPOSE;
	event.send_event = TRUE;
	event.window = widget->window;
	event.area.x = x;
	event.area.y = y;
	event.area.width = w;
	event.area.height = h;
	event.count = 0;

	gdk_window_ref(event.window);
	gtk_widget_event(widget, (GdkEvent *)&event);
	gdk_window_unref(event.window);
}

#endif

static void wjcanvas_scroll(GtkWidget *widget, int *oxy)
{
	wjcanvas *canvas = WJCANVAS(widget);
	int nxy[4], rxy[4], fulldraw;


	if (!GTK_WIDGET_DRAWABLE(widget)) return; // No use
	if (canvas->resizing) return; // No reason
	copy4(nxy, canvas->xy);
	if (!((oxy[0] ^ nxy[0]) | (oxy[1] ^ nxy[1]))) return; // No scrolling

	/* Scroll or redraw? */
	fulldraw = !clip(rxy, nxy[0], nxy[1], nxy[2], nxy[3], oxy);
#if GTK_MAJOR_VERSION == 1
	/* Check for resize - GTK+1 operates with ForgetGravity */
	if ((oxy[2] - oxy[0] - nxy[2] + nxy[0]) | (oxy[3] - oxy[1] - nxy[3] + nxy[1]))
		return; // A full expose event should be queued anyway
	/* Check for in-flight draw ops */
	if (GTK_WIDGET_FULLDRAW_PENDING(widget)) return;
	fulldraw |= GTK_WIDGET_REDRAW_PENDING(widget);
	// Updating draws in queue might be possible, but sure is insanely hard
#endif

	if (fulldraw) gtk_widget_queue_draw(widget); // Just redraw everything
	else // Scroll
	{
#if GTK_MAJOR_VERSION == 1
		GdkEvent *event;

		gdk_window_copy_area(widget->window, canvas->scroll_gc,
			rxy[0] - nxy[0], rxy[1] - nxy[1],
			widget->window,
			rxy[0] - oxy[0], rxy[1] - oxy[1],
			rxy[2] - rxy[0], rxy[3] - rxy[1]);

		/* Have to process GraphicsExpose events before next scrolling */
		while ((event = gdk_event_get_graphics_expose(widget->window)))
		{
			gtk_widget_event(widget, event);
			if (event->expose.count == 0)
			{
				gdk_event_free(event);
				break;
			}
			gdk_event_free(event);
		}

		/* Now draw the freed-up part(s) */
		if (rxy[1] > nxy[1]) wjcanvas_send_expose(widget,
			0, 0, nxy[2] - nxy[0], rxy[1] - nxy[1]);
		if (rxy[3] < nxy[3]) wjcanvas_send_expose(widget,
			0, rxy[3] - nxy[1], nxy[2] - nxy[0], nxy[3] - rxy[3]);
		if (rxy[0] > nxy[0]) wjcanvas_send_expose(widget,
			0, rxy[1] - nxy[1], rxy[0] - nxy[0], rxy[3] - rxy[1]);
		if (rxy[2] < nxy[2]) wjcanvas_send_expose(widget,
			rxy[2] - nxy[0], rxy[1] - nxy[1], nxy[2] - rxy[2], rxy[3] - rxy[1]);
#elif defined GDK_WINDOWING_WIN32
		/* !!! On Windows, gdk_window_scroll() had been badly broken in
		 * GTK+ 2.6.5, then fixed in 2.8.10, but with changed behaviour
		 * with regard to screen updates.
		 * Unlike all that, my own window scrolling code here behaves
		 * consistently :-) - WJ */
		GdkRegion *tmp;
		int dx = oxy[0] - nxy[0], dy = oxy[1] - nxy[1];

		/* Adjust the pending update region, let system add the rest
		 * through WM_PAINT (but, in Wine WM_PAINTs can lag behind) */
		if ((tmp = gdk_window_get_update_area(widget->window)))
		{
			gdk_region_offset(tmp, dx, dy);
			gdk_window_invalidate_region(widget->window, tmp, FALSE);
			gdk_region_destroy(tmp);
		}

		/* Tell Windows to scroll window AND send us invalidations */
		ScrollWindowEx(GDK_WINDOW_HWND(widget->window), dx, dy,
			NULL, NULL, NULL, NULL, SW_INVALIDATE);

		/* Catch the invalidations; this cures Wine, while on Windows
		 * just improves render consistency a bit.
		 * And needs by-region expose to not waste a lot of work - WJ */
		while (TRUE)
		{
			GdkEvent *event = gdk_event_get_graphics_expose(widget->window);
			if (!event) break;
			/* !!! The above function is buggy in GTK+2/Win32: it
			 * doesn't ref the window, so we must do it here - WJ */
			gdk_window_ref(widget->window);
			gdk_window_invalidate_region(widget->window,
				event->expose.region, FALSE);
			gdk_event_free(event);
		}

		/* And tell GTK+ to redraw it */
		gdk_window_process_updates(widget->window, FALSE);
#else
		gdk_window_scroll(widget->window,
			oxy[0] - nxy[0], oxy[1] - nxy[1]);
#endif
	}

/* !!! _Maybe_ we need gdk_window_process_updates(widget->window, FALSE) here
 * in GTK+2 - but then, maybe we don't; only practice will tell.
 * Rule of thumb is, you need atrociously slow render to make forced updates
 * useful - otherwise, they just create a slowdown. - WJ */

}

static void wjcanvas_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	wjcanvas *canvas = WJCANVAS(widget);
	int hchg, vchg, conf, oxy[4];


	/* Don't send useless configure events */
	conf = canvas->resize | (allocation->width ^ widget->allocation.width) |
		(allocation->height ^ widget->allocation.height);
	canvas->resizing = canvas->resize;
	canvas->resize = FALSE;

	copy4(oxy, canvas->xy);
	widget->allocation = *allocation;
	hchg = wjcanvas_readjust(canvas, 0);
	vchg = wjcanvas_readjust(canvas, 1);
	wjcanvas_set_extents(canvas);

	if (GTK_WIDGET_REALIZED(widget))
	{
		gdk_window_move_resize(widget->window,
			allocation->x, allocation->y,
			allocation->width, allocation->height);
		wjcanvas_scroll(widget, oxy);

		/* Replicate behaviour of GtkDrawingCanvas */
		if (conf) wjcanvas_send_configure(widget);
	}

	gtk_adjustment_changed(canvas->adjustments[0]);
	gtk_adjustment_changed(canvas->adjustments[1]);
	if (hchg) gtk_adjustment_value_changed(canvas->adjustments[0]);
	if (vchg) gtk_adjustment_value_changed(canvas->adjustments[1]);
	canvas->resizing = FALSE;
}

/* We do scrolling in both directions at once if possible, to avoid ultimately
 * useless repaint ops and associated flickers - WJ */
static void wjcanvas_adjustment_value_changed(GtkAdjustment *adjustment, gpointer data)
{
	GtkWidget *widget = data;
	wjcanvas *canvas;
	int oxy[4];


	if (!GTK_IS_ADJUSTMENT(adjustment) || !IS_WJCANVAS(data)) return;
	canvas = WJCANVAS(data);

	copy4(oxy, canvas->xy);
	wjcanvas_set_extents(canvas); // Set new window extents
	wjcanvas_scroll(widget, oxy);
}

static void wjcanvas_drop_adjustment(wjcanvas *canvas, int which)
{
	GtkAdjustment **slot = canvas->adjustments + which;

	if (*slot)
	{
		GtkObject *adj = GTK_OBJECT(*slot);
		gtk_signal_disconnect_by_func(adj,
			GTK_SIGNAL_FUNC(wjcanvas_adjustment_value_changed), canvas);
		gtk_object_unref(adj);
		*slot = NULL;
	}
}

static GtkAdjustment *wjcanvas_prepare_adjustment(wjcanvas *canvas,
	GtkAdjustment *adjustment)
{
	GtkObject *adj;

	if (!adjustment)
		adjustment = GTK_ADJUSTMENT(gtk_adjustment_new(0, 0, 0, 0, 0, 0));
	adj = GTK_OBJECT(adjustment);
	gtk_object_ref(adj);
	gtk_object_sink(adj);
	gtk_signal_connect(adj, "value_changed",
		GTK_SIGNAL_FUNC(wjcanvas_adjustment_value_changed), (gpointer)canvas);
	return (adjustment);
}

static void wjcanvas_set_adjustment(wjcanvas *canvas, int which,
	GtkAdjustment *adjustment)
{
	int changed;

	wjcanvas_drop_adjustment(canvas, which);
	adjustment = wjcanvas_prepare_adjustment(canvas, adjustment);
	canvas->adjustments[which] = adjustment;

	changed = wjcanvas_readjust(canvas, which);
	gtk_adjustment_changed(adjustment);
	if (changed) gtk_adjustment_value_changed(adjustment);
	else wjcanvas_adjustment_value_changed(adjustment, canvas); // Readjust anyway
}

static void wjcanvas_set_scroll_adjustments(wjcanvas *canvas,
	GtkAdjustment *hadjustment, GtkAdjustment *vadjustment)
{
	if (canvas->adjustments[0] != hadjustment)
		wjcanvas_set_adjustment(canvas, 0, hadjustment);
	if (canvas->adjustments[1] != vadjustment)
		wjcanvas_set_adjustment(canvas, 1, vadjustment);
}

static void wjcanvas_destroy(GtkObject *object)
{
	wjcanvas *canvas = WJCANVAS(object);

	wjcanvas_drop_adjustment(canvas, 0);
	wjcanvas_drop_adjustment(canvas, 1);
	GTK_OBJECT_CLASS(widget_class)->destroy(object);
}

static void wjcanvas_class_init(wjcanvasClass *class)
{
	GtkWidgetClass *wclass = GTK_WIDGET_CLASS(class);


	widget_class = gtk_type_class(GTK_TYPE_WIDGET);
	GTK_OBJECT_CLASS(class)->destroy = wjcanvas_destroy;
	wclass->realize = wjcanvas_realize;
#if GTK_MAJOR_VERSION == 1
	wclass->unrealize = wjcanvas_unrealize;
#endif
	wclass->size_allocate = wjcanvas_size_allocate;
	// !!! Default "style_set" handler can reenable background clear
	wclass->style_set = NULL;
	class->set_scroll_adjustments = wjcanvas_set_scroll_adjustments;

	wclass->set_scroll_adjustments_signal = gtk_signal_new(
		"set_scroll_adjustments", GTK_RUN_LAST, wjcanvas_type,
		GTK_SIGNAL_OFFSET(wjcanvasClass, set_scroll_adjustments),
		gtk_marshal_NONE__POINTER_POINTER, GTK_TYPE_NONE,
		2, GTK_TYPE_ADJUSTMENT, GTK_TYPE_ADJUSTMENT);
}

static void wjcanvas_init(wjcanvas *canvas)
{
	GTK_WIDGET_UNSET_FLAGS(canvas, GTK_NO_WINDOW);
#if GTK_MAJOR_VERSION == 2
	GTK_WIDGET_UNSET_FLAGS(canvas, GTK_DOUBLE_BUFFERED);
	gtk_widget_set_redraw_on_allocate(GTK_WIDGET(canvas), FALSE);
#endif
	/* Install fake adjustments */
	canvas->adjustments[0] = wjcanvas_prepare_adjustment(canvas, NULL);
	canvas->adjustments[1] = wjcanvas_prepare_adjustment(canvas, NULL);
}

static GtkType wjcanvas_get_type()
{
	if (!wjcanvas_type)
	{
		static const GtkTypeInfo wjcanvas_info = {
			"wjCanvas", sizeof(wjcanvas), sizeof(wjcanvasClass),
			(GtkClassInitFunc)wjcanvas_class_init,
			(GtkObjectInitFunc)wjcanvas_init,
			NULL, NULL, NULL };
		wjcanvas_type = gtk_type_unique(GTK_TYPE_WIDGET, &wjcanvas_info);
	}

	return (wjcanvas_type);
}

void wjcanvas_set_expose(GtkWidget *widget, GtkSignalFunc handler, gpointer user_data)
{
	gtk_signal_connect(GTK_OBJECT(widget), "expose_event", handler, user_data);
}

void wjcanvas_size(GtkWidget *widget, int width, int height)
{
	wjcanvas *canvas;

	if (!IS_WJCANVAS(widget)) return;
	canvas = WJCANVAS(widget);
	if ((canvas->size[0] == width) && (canvas->size[1] == height)) return;
	canvas->size[0] = width;
	canvas->size[1] = height;
	canvas->resize = TRUE;
#if GTK_MAJOR_VERSION == 1
	/* !!! The fields are limited to 16-bit signed, and the values aren't */
	widget->requisition.width = MIN(width, 32767);
	widget->requisition.height = MIN(height, 32767);
#else /* if GTK_MAJOR_VERSION == 2 */
	widget->requisition.width = width;
	widget->requisition.height = height;
#endif
	gtk_widget_queue_resize(widget);
}

static int wjcanvas_offset(GtkAdjustment *adj, int dv)
{
	double nv, up;
	int step, dw = dv;

	step = (int)(adj->step_increment + 0.5);
	if (step)
	{
		dw = abs(dw) + step - 1;
		dw -= dw % step;
		if (dv < 0) dw = -dw;
	}

	up = adj->upper - adj->page_size;
	nv = adj->value + dw;
	if (nv > up) nv = up;
	if (nv < 0.0) nv = 0.0;
	up = adj->value;
	adj->value = nv;
	return (up != nv);
}

int wjcanvas_scroll_in(GtkWidget *widget, int x, int y)
{
	wjcanvas *canvas = WJCANVAS(widget);
	int dx = 0, dy = 0;

	if (!canvas->adjustments[0] || !canvas->adjustments[1]) return (FALSE);

	if (x < canvas->xy[0]) dx = (x < 0 ? 0 : x) - canvas->xy[0];
	else if (x >= canvas->xy[2]) dx = (x >= canvas->size[0] ?
		canvas->size[0] : x + 1) - canvas->xy[2];
	if (y < canvas->xy[1]) dy = (y < 0 ? 0 : y) - canvas->xy[1];
	else if (y >= canvas->xy[3]) dy = (y >= canvas->size[1] ?
		canvas->size[1] : y + 1) - canvas->xy[3];
	if (!(dx | dy)) return (FALSE);

	dx = wjcanvas_offset(canvas->adjustments[0], dx);
	dy = wjcanvas_offset(canvas->adjustments[1], dy);
	if (dx) gtk_adjustment_value_changed(canvas->adjustments[0]);
	if (dy) gtk_adjustment_value_changed(canvas->adjustments[1]);
	return (dx | dy);
}

#define WJCANVAS_SCROLL_LIMIT 333 /* 3 steps/second */

/* If mouse moved outside canvas, scroll canvas & warp cursor back in */
int wjcanvas_bind_mouse(GtkWidget *widget, GdkEventMotion *event, int x, int y)
{
	wjcanvas *canvas = WJCANVAS(widget);
	int oldv[4];

	copy4(oldv, canvas->xy);
	x += oldv[0]; y += oldv[1];
	if ((x >= oldv[0]) && (x < oldv[2]) && (y >= oldv[1]) && (y < oldv[3]))
		return (FALSE);
	/* Limit scrolling rate for absolute pointing devices */
#if GTK_MAJOR_VERSION == 1
	if ((event->source != GDK_SOURCE_MOUSE) &&
#else /* if GTK_MAJOR_VERSION == 2 */
	if ((event->device->source != GDK_SOURCE_MOUSE) &&
#endif
		(event->time < canvas->scrolltime + WJCANVAS_SCROLL_LIMIT))
		return (FALSE);
	if (!wjcanvas_scroll_in(widget, x, y)) return (FALSE);
	canvas->scrolltime = event->time;
	return (move_mouse_relative(oldv[0] - canvas->xy[0], oldv[1] - canvas->xy[1]));
}

#endif /* GTK+1&2 */

GtkWidget *wjcanvas_new()
{
	return (gtk_widget_new(wjcanvas_get_type(), NULL));
}

void wjcanvas_get_vport(GtkWidget *widget, int *vport)
{
	copy4(vport, WJCANVAS(widget)->xy);
}

#if GTK_MAJOR_VERSION == 3

// Focusable pixmap widget (on Cairo surfaces, but whatever)

#define WJPIXMAP(obj)		G_TYPE_CHECK_INSTANCE_CAST(obj, wjpixmap_get_type(), wjpixmap)
#define IS_WJPIXMAP(obj)	G_TYPE_CHECK_INSTANCE_TYPE(obj, wjpixmap_get_type())

typedef struct
{
	GtkWidget widget;	// Parent class
	GdkWindow *pixwindow;
	cairo_surface_t *pixmap;
	cairo_surface_t *cursor;
	GdkRectangle pm, cr;	// pm is allocation relative, unlike in GTK+1&2
	int width, height;	// Requested pixmap size
	int xc, yc;
	int focused_cursor;
} wjpixmap;

typedef struct
{
	GtkWidgetClass parent_class;
} wjpixmapClass;

G_DEFINE_TYPE(wjpixmap, wjpixmap, GTK_TYPE_WIDGET)

#define WJPIXMAP_FRAME 1 /* Line width, limited only by common sense */

static gboolean wjpixmap_draw(GtkWidget *widget, cairo_t *cr)
{
	wjpixmap *pix = WJPIXMAP(widget);
	GtkStyleContext *ctx = gtk_widget_get_style_context(widget);
	GtkAllocation alloc;

	cairo_save(cr);
	gtk_widget_get_allocation(widget, &alloc);
	/* Outer window */
	if (gtk_cairo_should_draw_window(cr, gtk_widget_get_window(widget)))
	{
		gtk_render_background(ctx, cr, 0, 0, alloc.width, alloc.height);
// !!! Maybe render pixmap frame here: _before_ focus, as in GTK+1/2?
		/* Focus frame */
		if (gtk_widget_has_visible_focus(widget))
		{
			/* !!! Is inside-the-border the right place? */
			GtkBorder border;
			get_padding_and_border(ctx, NULL, &border, NULL);
			gtk_render_focus(ctx, cr, border.left, border.top,
				alloc.width - border.left - border.right,
				alloc.height - border.top - border.bottom);
		}
		/* Pixmap frame */
		cairo_set_line_width(cr, WJPIXMAP_FRAME);
		cairo_set_line_cap(cr, CAIRO_LINE_CAP_SQUARE);
		cairo_set_source_rgb(cr, 0, 0, 0); // Black
		cairo_rectangle(cr,
			pix->pm.x - WJPIXMAP_FRAME * 0.5,
			pix->pm.y - WJPIXMAP_FRAME * 0.5,
			pix->pm.width + WJPIXMAP_FRAME,
			pix->pm.height + WJPIXMAP_FRAME);
		cairo_stroke(cr);
	}
	/* Pixmap window */
	while (gtk_cairo_should_draw_window(cr, pix->pixwindow))
	{
		/* This widget may get moved to differently-scaled monitor while
		 * retaining the buffer surfaces, so cairo_unfilter() to at least
		 * prevent blurring if that happens */
		gtk_cairo_transform_to_window(cr, widget, pix->pixwindow);
		cairo_set_source_surface(cr, pix->pixmap, 0, 0);
		cairo_unfilter(cr);
		cairo_rectangle(cr, 0, 0, pix->pm.width, pix->pm.height);
//		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_fill(cr); // Hope Cairo won't waste much time on clipped-out parts
		/* Cursor pixmap */
		if (!pix->cursor) break;
		if (pix->focused_cursor && !gtk_widget_has_focus(widget)) break;
		cairo_set_source_surface(cr, pix->cursor, pix->cr.x, pix->cr.y);
		cairo_unfilter(cr);
//		cairo_set_operator(cr, CAIRO_OPERATOR_OVER);
		cairo_rectangle(cr, pix->cr.x, pix->cr.y, pix->cr.width, pix->cr.height);
		cairo_fill(cr); // Hope Cairo won't waste time if outside of clip
		break;
	}
	cairo_restore(cr);

	return (FALSE);
}

static void wjpixmap_position(wjpixmap *pix, GtkAllocation *alloc)
{
	int x, y, w, h;
	GtkBorder border;

	get_padding_and_border(gtk_widget_get_style_context(GTK_WIDGET(pix)),
		NULL, NULL, &border);
	x = alloc->width - border.left - border.right;
	pix->pm.width = w = pix->width <= x ? pix->width : x;
	pix->pm.x = (x - w) / 2 + border.left;
	y = alloc->height - border.top - border.bottom;
	pix->pm.height = h = pix->height <= y ? pix->height : y;
	pix->pm.y = (y - h) / 2 + border.top;
}

static void wjpixmap_realize(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);
	GdkWindow *win;
	GdkWindowAttr attrs;
	GtkAllocation alloc;

	gtk_widget_set_realized(widget, TRUE);
	gtk_widget_get_allocation(widget, &alloc);

	win = gtk_widget_get_parent_window(widget);
	gtk_widget_set_window(widget, win);
	g_object_ref(win);

	wjpixmap_position(pix, &alloc);

	attrs.x = pix->pm.x + alloc.x;
	attrs.y = pix->pm.y + alloc.y;
	attrs.width = pix->pm.width;
	attrs.height = pix->pm.height;
	attrs.window_type = GDK_WINDOW_CHILD;
	attrs.wclass = GDK_INPUT_OUTPUT;
	attrs.visual = gtk_widget_get_visual(widget);
	// Add the same events as GtkEventBox does
	attrs.event_mask = gtk_widget_get_events(widget) | GDK_BUTTON_MOTION_MASK |
		GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
		GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK;
	/* !!! GDK_EXPOSURE_MASK seems really not be needed (as advertised),
	 * despite GtkDrawingArea still having it */
	pix->pixwindow = win = gdk_window_new(win, &attrs,
		GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL);
	/* !!! Versions without this (below 3.12) are useless */
	gdk_window_set_event_compression(win, FALSE);

	gtk_widget_register_window(widget, win);
}

static void wjpixmap_unrealize(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (pix->pixwindow)
	{
		gtk_widget_unregister_window(widget, pix->pixwindow);
		gdk_window_destroy(pix->pixwindow);
		pix->pixwindow = NULL;
	}

	GTK_WIDGET_CLASS(wjpixmap_parent_class)->unrealize(widget);
}

static void wjpixmap_map(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);

	GTK_WIDGET_CLASS(wjpixmap_parent_class)->map(widget);
	if (pix->pixwindow) gdk_window_show(pix->pixwindow);
}

static void wjpixmap_unmap(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (pix->pixwindow) gdk_window_hide(pix->pixwindow);
	GTK_WIDGET_CLASS(wjpixmap_parent_class)->unmap(widget);
}

static void wjpixmap_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	wjpixmap *pix = WJPIXMAP(widget);

	gtk_widget_set_allocation(widget, allocation);
	wjpixmap_position(pix, allocation);

	if (gtk_widget_get_realized(widget)) gdk_window_move_resize(pix->pixwindow,
		pix->pm.x + allocation->x, pix->pm.y + allocation->y,
		pix->pm.width, pix->pm.height);
}

static void wjpixmap_get_size(GtkWidget *widget, gint vert, gint *min, gint *nat)
{
	wjpixmap *pix = WJPIXMAP(widget);
	GtkBorder border;

	get_padding_and_border(gtk_widget_get_style_context(widget), NULL, NULL, &border);

	*min = *nat = (vert ? border.top + border.bottom + pix->height :
		border.left + border.right + pix->width) + WJPIXMAP_FRAME * 2;
}

static void wjpixmap_get_preferred_width(GtkWidget *widget, gint *min, gint *nat)
{
	wjpixmap_get_size(widget, FALSE, min, nat);
}

static void wjpixmap_get_preferred_height(GtkWidget *widget, gint *min, gint *nat)
{
	wjpixmap_get_size(widget, TRUE, min, nat);
}

static void wjpixmap_destroy(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (pix->pixmap)
	{
		cairo_surface_fdestroy(pix->pixmap);
		pix->pixmap = NULL;
	}
	if (pix->cursor)
	{
		cairo_surface_fdestroy(pix->cursor);
		pix->cursor = NULL;
	}
	GTK_WIDGET_CLASS(wjpixmap_parent_class)->destroy(widget);
}

static void wjpixmap_class_init(wjpixmapClass *class)
{
	GtkWidgetClass *wclass = GTK_WIDGET_CLASS(class);

//	wclass->screen_changed = wjpixmap_screen_changed; // as proxy for changing visual?
	wclass->destroy = wjpixmap_destroy;
	wclass->realize = wjpixmap_realize;
	wclass->unrealize = wjpixmap_unrealize;
	wclass->map = wjpixmap_map;
	wclass->unmap = wjpixmap_unmap;
	wclass->draw = wjpixmap_draw;
	wclass->size_allocate = wjpixmap_size_allocate;
	wclass->get_preferred_width = wjpixmap_get_preferred_width;
	wclass->get_preferred_height = wjpixmap_get_preferred_height;
	/* Default handlers in GtkWidget redirect these two anyway */
//	wclass->get_preferred_width_for_height = wjpixmap_get_preferred_width_for_height;
//	wclass->get_preferred_height_for_width = wjpixmap_get_preferred_height_for_width;
	/* !!! Do not disturb my circles */
//	wclass->style_updated = NULL;
}

static void wjpixmap_init(wjpixmap *pix)
{
	gtk_widget_set_has_window(GTK_WIDGET(pix), FALSE);
	pix->pixwindow = NULL;
	pix->pixmap = pix->cursor = NULL;
	add_css_class(GTK_WIDGET(pix), "wjpixmap");
}

GtkWidget *wjpixmap_new(int width, int height)
{
	GtkWidget *widget = gtk_widget_new(wjpixmap_get_type(), NULL);
	wjpixmap *pix = WJPIXMAP(widget);

	gtk_widget_set_can_focus(widget, TRUE);
	pix->width = width; pix->height = height;
	return (widget);
}

/* Must be called first to init, and afterwards to access pixmap */
cairo_surface_t *wjpixmap_pixmap(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (!pix->pixmap)
	{
		GdkWindow *win = pix->pixwindow;
		if (!win) win = gdk_screen_get_root_window(gtk_widget_get_screen(widget));
		pix->pixmap = gdk_window_create_similar_surface(win,
			CAIRO_CONTENT_COLOR, pix->width, pix->height);
	}
	return (pix->pixmap);
}

void wjpixmap_draw_rgb(GtkWidget *widget, int x, int y, int w, int h,
	unsigned char *rgb, int step)
{
	wjpixmap *pix = WJPIXMAP(widget);
	cairo_surface_t *s;
	cairo_t *cr;

	if (!pix->pixmap) return;
	s = cairo_upload_rgb(pix->pixmap, NULL, rgb, w, h, step);

	cr = cairo_create(pix->pixmap);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, s, x, y);
	cairo_unfilter(cr);
	cairo_rectangle(cr, x, y, w, h);
	cairo_fill(cr);
	cairo_destroy(cr);

	cairo_surface_fdestroy(s);
	if (pix->pixwindow)
	{
		GdkRectangle wr = { x, y, w, h };
		gdk_window_invalidate_rect(pix->pixwindow, &wr, FALSE);
	}
}

void wjpixmap_move_cursor(GtkWidget *widget, int x, int y)
{
	wjpixmap *pix = WJPIXMAP(widget);
	GdkRectangle pm, ocr, tcr1, tcr2, *rcr = NULL;
	int dx = x - pix->xc, dy = y - pix->yc;

	if (!(dx | dy)) return;
	ocr = pix->cr;
	pix->cr.x += dx; pix->cr.y += dy;
	pix->xc = x; pix->yc = y;

	if (!pix->pixmap || !pix->cursor) return;
	if (pix->focused_cursor && !gtk_widget_has_focus(widget)) return;

	/* Anything visible? */
	if (!gtk_widget_get_visible(widget)) return;
	pm = pix->pm; pm.x = pm.y = 0;
	if (gdk_rectangle_intersect(&ocr, &pm, &tcr1)) rcr = &tcr1;
	if (gdk_rectangle_intersect(&pix->cr, &pm, &tcr2))
	{
		if (rcr) gdk_rectangle_union(&tcr1, &tcr2, rcr = &ocr);
		else rcr = &tcr2;
	}
	if (!rcr) return; /* Both positions invisible */
	if (pix->pixwindow) gdk_window_invalidate_rect(pix->pixwindow, rcr, FALSE);
}

/* Input is two compiled-in XBMs */
void wjpixmap_set_cursor(GtkWidget *widget, char *image, char *mask,
	int width, int height, int hot_x, int hot_y, int focused)
{
	wjpixmap *pix = WJPIXMAP(widget);


	if (pix->cursor)
	{
		cairo_surface_fdestroy(pix->cursor);
		pix->cursor = NULL;
	}
	pix->focused_cursor = focused;

	if (image)
	{
		GdkWindow *win = pix->pixwindow;
		cairo_surface_t *s;
		cairo_t *cr;

		if (!win) win = gdk_screen_get_root_window(gtk_widget_get_screen(widget));
		pix->cursor = gdk_window_create_similar_surface(win,
			CAIRO_CONTENT_COLOR_ALPHA, width, height);

		s = xbms_to_surface(image, mask, width, height);
		cr = cairo_create(pix->cursor);
		cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
		cairo_set_source_surface(cr, s, 0, 0);
		cairo_paint(cr);
		cairo_destroy(cr);
		cairo_surface_fdestroy(s);

		pix->cr.x = pix->xc - hot_x;
		pix->cr.y = pix->yc - hot_y;
		pix->cr.width = width;
		pix->cr.height = height;
	}

	/* Optimizing redraw in a rare operation is useless */
	if (pix->pixmap) gtk_widget_queue_draw(widget);
}

/* Translate allocation-relative coords to pixmap-relative */
int wjpixmap_rxy(GtkWidget *widget, int x, int y, int *xr, int *yr)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (!pix->pixmap) return (FALSE);
	x -= pix->pm.x;
	y -= pix->pm.y;
	*xr = x; *yr = y;
	return ((x >= 0) && (x < pix->pm.width) && (y >= 0) && (y < pix->pm.height));
}

#else /* if GTK_MAJOR_VERSION <= 2 */

// Focusable pixmap widget

#define WJPIXMAP(obj)		GTK_CHECK_CAST(obj, wjpixmap_get_type(), wjpixmap)
#define IS_WJPIXMAP(obj)	GTK_CHECK_TYPE(obj, wjpixmap_get_type())

typedef struct
{
	GtkWidget widget;	// Parent class
	GdkWindow *pixwindow;
	int width, height;	// Requested pixmap size
	int xc, yc;
	int focused_cursor;
	GdkRectangle pm, cr;
	GdkPixmap *pixmap, *cursor;
	GdkBitmap *cmask;
} wjpixmap;

static GtkType wjpixmap_type;

static GtkType wjpixmap_get_type();

static void wjpixmap_position(wjpixmap *pix)
{
	int x, y, w, h;

	x = pix->widget.allocation.width;
	pix->pm.width = w = pix->width <= x ? pix->width : x;
	pix->pm.x = (x - w) / 2 + pix->widget.allocation.x;
	y = pix->widget.allocation.height;
	pix->pm.height = h = pix->height <= y ? pix->height : y;
	pix->pm.y = (y - h) / 2 + pix->widget.allocation.y;
}

static void wjpixmap_realize(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);
	GdkWindowAttr attrs;


	GTK_WIDGET_SET_FLAGS(widget, GTK_REALIZED);

	widget->window = gtk_widget_get_parent_window(widget);
	gdk_window_ref(widget->window);

	wjpixmap_position(pix);
	attrs.x = pix->pm.x;
	attrs.y = pix->pm.y;
	attrs.width = pix->pm.width;
	attrs.height = pix->pm.height;
	attrs.window_type = GDK_WINDOW_CHILD;
	attrs.wclass = GDK_INPUT_OUTPUT;
	attrs.visual = gtk_widget_get_visual(widget);
	attrs.colormap = gtk_widget_get_colormap(widget);
	// Add the same events as GtkEventBox does
	attrs.event_mask = gtk_widget_get_events(widget) | GDK_BUTTON_MOTION_MASK |
		GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK |
		GDK_EXPOSURE_MASK | GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK;
	pix->pixwindow = gdk_window_new(widget->window, &attrs,
		GDK_WA_X | GDK_WA_Y | GDK_WA_VISUAL | GDK_WA_COLORMAP);
	gdk_window_set_user_data(pix->pixwindow, widget);
#if GTK_MAJOR_VERSION == 1
	fix_gdk_events(pix->pixwindow);
#endif
	gdk_window_set_back_pixmap(pix->pixwindow, NULL, FALSE);

	widget->style = gtk_style_attach(widget->style, widget->window);
}

static void wjpixmap_unrealize(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (pix->pixwindow)
	{
		gdk_window_set_user_data(pix->pixwindow, NULL);
		gdk_window_destroy(pix->pixwindow);
		pix->pixwindow = NULL;
	}

	if (widget_class->unrealize) widget_class->unrealize(widget);
}

static void wjpixmap_map(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (widget_class->map) widget_class->map(widget);
	if (pix->pixwindow) gdk_window_show(pix->pixwindow);
}

static void wjpixmap_unmap(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (pix->pixwindow) gdk_window_hide(pix->pixwindow);
	if (widget_class->unmap) widget_class->unmap(widget);
}

static void wjpixmap_size_request(GtkWidget *widget, GtkRequisition *requisition)
{
	wjpixmap *pix = WJPIXMAP(widget);
	gint xp, yp;

#if GTK_MAJOR_VERSION == 1
	xp = widget->style->klass->xthickness * 2 + 4;
	yp = widget->style->klass->ythickness * 2 + 4;
#else
	gtk_widget_style_get(GTK_WIDGET (widget),
		"focus-line-width", &xp, "focus-padding", &yp, NULL);
	yp = (xp + yp) * 2 + 2;
	xp = widget->style->xthickness * 2 + yp;
	yp = widget->style->ythickness * 2 + yp;
#endif
	requisition->width = pix->width + xp;
	requisition->height = pix->height + yp;
}

static void wjpixmap_size_allocate(GtkWidget *widget, GtkAllocation *allocation)
{
	wjpixmap *pix = WJPIXMAP(widget);

	widget->allocation = *allocation;
	wjpixmap_position(pix);

	if (GTK_WIDGET_REALIZED(widget)) gdk_window_move_resize(pix->pixwindow,
		pix->pm.x, pix->pm.y, pix->pm.width, pix->pm.height);
}

static void wjpixmap_paint(GtkWidget *widget, int inner, GdkRectangle *area)
{
	wjpixmap *pix = WJPIXMAP(widget);
	GdkRectangle cdest;


#if GTK_MAJOR_VERSION == 1
	/* Preparation */
	gdk_gc_set_clip_rectangle(widget->style->black_gc, area);
#endif

	if (!inner) // Drawing borders
	{
#if GTK_MAJOR_VERSION == 1
		gdk_window_clear_area(widget->window,
			area->x, area->y, area->width, area->height);
#endif
		/* Frame */
		gdk_draw_rectangle(widget->window, widget->style->black_gc,
			FALSE, pix->pm.x - 1, pix->pm.y - 1,
			pix->pm.width + 1, pix->pm.height + 1);
		/* Focus rectangle */
		if (GTK_WIDGET_HAS_FOCUS(widget))
			gtk_paint_focus(widget->style, widget->window,
#if GTK_MAJOR_VERSION == 2
			GTK_WIDGET_STATE(widget),
#endif
			area, widget, NULL,
			widget->allocation.x, widget->allocation.y,
			widget->allocation.width - 1, widget->allocation.height - 1);
	}

	while (inner) // Drawing pixmap & cursor
	{
		/* Contents pixmap */
		gdk_draw_pixmap(pix->pixwindow, widget->style->black_gc,
			pix->pixmap, area->x, area->y,
			area->x, area->y, area->width, area->height);

		/* Cursor pixmap */
		if (!pix->cursor) break;
		if (pix->focused_cursor && !GTK_WIDGET_HAS_FOCUS(widget)) break;
		if (!gdk_rectangle_intersect(&pix->cr, area, &cdest)) break;
		if (pix->cmask)
		{
			gdk_gc_set_clip_mask(widget->style->black_gc, pix->cmask);
			gdk_gc_set_clip_origin(widget->style->black_gc,
				pix->cr.x, pix->cr.y);
		}
		gdk_draw_pixmap(pix->pixwindow, widget->style->black_gc,
			pix->cursor, cdest.x - pix->cr.x, cdest.y - pix->cr.y,
			cdest.x, cdest.y, cdest.width, cdest.height);
		if (pix->cmask)
		{
			gdk_gc_set_clip_mask(widget->style->black_gc, NULL);
			gdk_gc_set_clip_origin(widget->style->black_gc, 0, 0);
		}
		break;
	}

#if GTK_MAJOR_VERSION == 1
	/* Cleanup */
	gdk_gc_set_clip_rectangle(widget->style->black_gc, NULL);
#endif
}

static gboolean wjpixmap_expose(GtkWidget *widget, GdkEventExpose *event)
{
	if (GTK_WIDGET_DRAWABLE(widget)) wjpixmap_paint(widget,
		event->window != widget->window, &event->area);
	return (FALSE);
}

#if GTK_MAJOR_VERSION == 1

static void wjpixmap_draw(GtkWidget *widget, GdkRectangle *area)
{
	wjpixmap *pix = WJPIXMAP(widget);
	GdkRectangle ir;

	if (!GTK_WIDGET_DRAWABLE(widget)) return;
	/* If inner window is touched */
	if (gdk_rectangle_intersect(area, &pix->pm, &ir))
	{
		ir.x -= pix->pm.x; ir.y -= pix->pm.y;
		wjpixmap_paint(widget, TRUE, &ir);
		/* If outer window isn't */
		if (!((area->width - ir.width) | (area->height - ir.height)))
			return;
	}
	wjpixmap_paint(widget, FALSE, area);
}

static void wjpixmap_draw_focus(GtkWidget *widget)
{
	gtk_widget_draw(widget, NULL);
}

static gint wjpixmap_focus_event(GtkWidget *widget, GdkEventFocus *event)
{
	if (event->in) GTK_WIDGET_SET_FLAGS(widget, GTK_HAS_FOCUS);
	else GTK_WIDGET_UNSET_FLAGS(widget, GTK_HAS_FOCUS);
	gtk_widget_draw_focus(widget);
	return (FALSE);
}

#endif

static void wjpixmap_destroy(GtkObject *object)
{
	wjpixmap *pix = WJPIXMAP(object);

	if (pix->pixmap) gdk_pixmap_unref(pix->pixmap);
	if (pix->cursor) gdk_pixmap_unref(pix->cursor);
	if (pix->cmask) gdk_bitmap_unref(pix->cmask);
	/* !!! Unlike attached handler, default handler can get called twice */
	pix->pixmap = pix->cursor = pix->cmask = NULL;
	GTK_OBJECT_CLASS(widget_class)->destroy(object);
}

static void wjpixmap_class_init(GtkWidgetClass *class)
{
	widget_class = gtk_type_class(GTK_TYPE_WIDGET);
	GTK_OBJECT_CLASS(class)->destroy = wjpixmap_destroy;
	class->realize = wjpixmap_realize;
	class->unrealize = wjpixmap_unrealize;
	class->map = wjpixmap_map;
	class->unmap = wjpixmap_unmap;
#if GTK_MAJOR_VERSION == 1
	class->draw = wjpixmap_draw;
	class->draw_focus = wjpixmap_draw_focus;
	class->focus_in_event = class->focus_out_event = wjpixmap_focus_event;
#endif
	class->expose_event = wjpixmap_expose;
	class->size_request = wjpixmap_size_request;
	class->size_allocate = wjpixmap_size_allocate;
}

static GtkType wjpixmap_get_type()
{
	if (!wjpixmap_type)
	{
		static const GtkTypeInfo wjpixmap_info = {
			"wjPixmap", sizeof(wjpixmap), sizeof(GtkWidgetClass),
			(GtkClassInitFunc)wjpixmap_class_init,
//			(GtkObjectInitFunc)wjpixmap_init,
			NULL, /* No instance init */
			NULL, NULL, NULL };
		wjpixmap_type = gtk_type_unique(GTK_TYPE_WIDGET, &wjpixmap_info);
	}

	return (wjpixmap_type);
}

GtkWidget *wjpixmap_new(int width, int height)
{
	GtkWidget *widget = gtk_widget_new(wjpixmap_get_type(), NULL);
	wjpixmap *pix = WJPIXMAP(widget);

	GTK_WIDGET_SET_FLAGS(widget, GTK_CAN_FOCUS | GTK_NO_WINDOW);
	pix->width = width; pix->height = height;
	return (widget);
}

/* Must be called first to init, and afterwards to access pixmap */
GdkPixmap *wjpixmap_pixmap(GtkWidget *widget)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (!pix->pixmap)
	{
		GdkWindow *win = NULL;
		int depth = -1;
		if (GTK_WIDGET_REALIZED(widget)) win = widget->window;
		else depth = gtk_widget_get_visual(widget)->depth;
		pix->pixmap = gdk_pixmap_new(win, pix->width, pix->height, depth);
	}
	return (pix->pixmap);
}

void wjpixmap_draw_rgb(GtkWidget *widget, int x, int y, int w, int h,
	unsigned char *rgb, int step)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (!pix->pixmap) return;
	gdk_draw_rgb_image(pix->pixmap, widget->style->black_gc,
		x, y, w, h, GDK_RGB_DITHER_NONE, rgb, step);
#if GTK_MAJOR_VERSION == 1
	gtk_widget_queue_draw_area(widget, x + pix->pm.x, y + pix->pm.y, w, h);
#else /* if GTK_MAJOR_VERSION == 2 */
	if (pix->pixwindow)
	{
		GdkRectangle wr = { x, y, w, h };
		gdk_window_invalidate_rect(pix->pixwindow, &wr, FALSE);
	}
#endif
}

void wjpixmap_move_cursor(GtkWidget *widget, int x, int y)
{
	wjpixmap *pix = WJPIXMAP(widget);
	GdkRectangle pm, ocr, tcr1, tcr2, *rcr = NULL;
	int dx = x - pix->xc, dy = y - pix->yc;

	if (!(dx | dy)) return;
	ocr = pix->cr;
	pix->cr.x += dx; pix->cr.y += dy;
	pix->xc = x; pix->yc = y;

	if (!pix->pixmap || !pix->cursor) return;
	if (pix->focused_cursor && !GTK_WIDGET_HAS_FOCUS(widget)) return;

	/* Anything visible? */
	if (!GTK_WIDGET_VISIBLE(widget)) return;
	pm = pix->pm; pm.x = pm.y = 0;
	if (gdk_rectangle_intersect(&ocr, &pm, &tcr1)) rcr = &tcr1;
	if (gdk_rectangle_intersect(&pix->cr, &pm, &tcr2))
	{
		if (rcr) gdk_rectangle_union(&tcr1, &tcr2, rcr = &ocr);
		else rcr = &tcr2;
	}
	if (!rcr) return; /* Both positions invisible */
#if GTK_MAJOR_VERSION == 1
	gtk_widget_queue_draw_area(widget,
		rcr->x + pix->pm.x, rcr->y + pix->pm.y, rcr->width, rcr->height);
#else /* if GTK_MAJOR_VERSION == 2 */
	if (pix->pixwindow) gdk_window_invalidate_rect(pix->pixwindow, rcr, FALSE);
#endif
}

void wjpixmap_set_cursor(GtkWidget *widget, char *image, char *mask,
	int width, int height, int hot_x, int hot_y, int focused)
{
	wjpixmap *pix = WJPIXMAP(widget);


	if (pix->cursor) gdk_pixmap_unref(pix->cursor);
	if (pix->cmask) gdk_bitmap_unref(pix->cmask);
	pix->cursor = pix->cmask = NULL;
	pix->focused_cursor = focused;

	if (image)
	{
		GdkWindow *win = NULL;
		int depth = -1;
		if (GTK_WIDGET_REALIZED(widget)) win = widget->window;
		else depth = gtk_widget_get_visual(widget)->depth;

		pix->cursor = gdk_pixmap_create_from_data(win, image,
			width, height, depth,
			&widget->style->white, &widget->style->black);
		pix->cr.x = pix->xc - hot_x;
		pix->cr.y = pix->yc - hot_y;
		pix->cr.width = width;
		pix->cr.height = height;
		if (mask) pix->cmask = gdk_bitmap_create_from_data(win, mask,
			width, height);
	}

	/* Optimizing redraw in a rare operation is useless */
	if (pix->pixmap) gtk_widget_queue_draw(widget);
}

/* Translate allocation-relative coords to pixmap-relative */
int wjpixmap_rxy(GtkWidget *widget, int x, int y, int *xr, int *yr)
{
	wjpixmap *pix = WJPIXMAP(widget);

	if (!pix->pixmap) return (FALSE);
	x -= pix->pm.x - widget->allocation.x;
	y -= pix->pm.y - widget->allocation.y;
	*xr = x; *yr = y;
	return ((x >= 0) && (x < pix->pm.width) && (y >= 0) && (y < pix->pm.height));
}

#endif /* GTK+1&2 */

// Type of pathname

int path_type(char *path)
{
#ifdef WIN32
	return ((path[0] == '/') || (path[0] == '\\') ? PT_DRIVE_ABS :
		path[1] != ':' ? PT_REL :
		(path[2] != '/') && (path[2] != '\\') ? PT_DRIVE_REL : PT_ABS);
#else
	return (path[0] != '/' ? PT_REL : PT_ABS);
#endif
}

// Convert pathname to absolute

char *resolve_path(char *buf, int buflen, char *path)
{
	char wbuf[PATHBUF], *tmp, *src, *dest, *tm2 = "";
	int ch, dot, dots, pt;

	pt = path_type(path);
	wbuf[0] = '\0';
	/* Relative name to absolute */
	if (pt != PT_ABS)
	{
		getcwd(wbuf, PATHBUF - 1);
		tm2 = DIR_SEP_STR;
	}
#ifdef WIN32
	/* Drive-absolute pathname needs current drive */
	if (pt == PT_DRIVE_ABS) wbuf[2] = '\0';
	/* Drive-relative pathname needs drive's current directory */
	else if (pt == PT_DRIVE_REL)
	{
		char tbuf[PATHBUF];

		tbuf[0] = path[0]; tbuf[1] = ':'; tbuf[2] = '\0';
		if (!chdir(tbuf)) getcwd(tbuf, PATHBUF - 1);
		chdir(wbuf);
		memcpy(wbuf, tbuf, PATHBUF);
		path += 2;
	}
#endif
	tmp = wjstrcat(NULL, 0, "", 0, wbuf, tm2, path, NULL);

	/* Canonicalize path the way "realpath -s" does, i.e., symlinks
	 * followed by ".." will get resolved wrong - WJ */
	src = dest = tmp;
	dots = dot = 0;
	while (TRUE)
	{
		ch = *src++;
#ifdef WIN32
		if (ch == '/') ch = DIR_SEP;
#endif
		if (ch == '.') dots += dot;
		else if (!ch || (ch == DIR_SEP))
		{
			if ((dots > 0) && (dots < 4)) /* // /./ /../ */
			{
				dest -= dots;
				if (dots == 3) /* /../ */
				{
					*dest = '\0';
					if ((tm2 = strrchr(tmp, DIR_SEP)))
						dest = tm2;
				}
				/* Do not lose trailing separator */
				if (!ch) *dest++ = DIR_SEP;
			}
			dots = dot = 1;
		}
		else dots = dot = 0;
		*dest++ = ch;
		if (!ch) break;
	}

	/* Return the result */
	if (buf)
	{
		strncpy(buf, tmp, buflen);
		buf[buflen - 1] = 0;
		free(tmp);
		tmp = buf;
	}
	return (tmp);
}

// A (better) substitute for fnmatch(), in case one is needed

/* One is necessary in case of Win32 or GTK+ 2.0/2.2 */
#if defined(WIN32) || ((GTK_MAJOR_VERSION == 2) && (GTK2VERSION < 4))

#ifdef WIN32

/* Convert everything to lowercase */
static gunichar nxchar(const char **str)
{
	gunichar c = g_utf8_get_char(*str);
	*str = g_utf8_next_char(*str);
	return (g_unichar_tolower(c));
}

/* Slash isn't an escape char in Windows */
#define UNQUOTE_CHAR(C,P)

#else 

static gunichar nxchar(const char **str)
{
	gunichar c = g_utf8_get_char(*str);
	*str = g_utf8_next_char(*str);
	return (c);
}

#define UNQUOTE_CHAR(C,P) if ((C) == '\\') (C) = nxchar(P)

#endif

static int match_char_set(const char **maskp, gunichar cs)
{
	const char *mask = *maskp;
	gunichar ch, cstart, cend;
	int inv;

	ch = *mask;
	if ((inv = (ch == '^') | (ch == '!'))) mask++;
	ch = nxchar(&mask);
	while (TRUE)
	{
		UNQUOTE_CHAR(ch, &mask);
		if (!ch) return (0); // Failed
		cstart = cend = ch;
		if ((*mask == '-') && (mask[1] != ']'))
		{
			mask++;
			ch = nxchar(&mask);
			UNQUOTE_CHAR(ch, &mask);
			if (!ch) return (0); // Failed
			cend = ch;
		}
		if ((cs >= cstart) && (cs <= cend))
		{
			if (inv) return (-1); // Didn't match
			while ((ch = nxchar(&mask)) != ']')
			{
				UNQUOTE_CHAR(ch, &mask);
				if (!ch) return (0); // Failed
			}
			break;
		}
		ch = nxchar(&mask);
		if (ch != ']') continue;
		if (!inv) return (-1); // Didn't match
		break;
	}
	*maskp = mask;
	return (1); // Matched
}

/* The limiting of recursion to one level in this algorithm is based on the
 * observation that in case of a "*X*Y" subsequence, moving a match for "X" to
 * the right can not improve the outcome - so only the part after the last
 * encountered star may ever need to be rematched at another position - WJ */

int wjfnmatch(const char *mask, const char *str, int utf)
{
	char *xmask, *xstr;
	const char *nstr, *omask, *wmask, *tstr = NULL, *tmask = NULL;
	gunichar ch, cs, cw;
	int res, ret = FALSE;


	/* Convert locale to utf8 */
	if (!utf)
	{
		mask = xmask = g_locale_to_utf8((gchar *)mask, -1, NULL, NULL, NULL);
		str = xstr = g_locale_to_utf8((gchar *)str, -1, NULL, NULL, NULL);
		// Fail the match if conversion failed
		if (!xmask || !xstr) return (FALSE);
	}

	while (TRUE)
	{
		nstr = str;
		cs = nxchar(&nstr);
		if (!cs) break;

		omask = mask;
		ch = nxchar(&mask);
		if ((cs == DIR_SEP) && (ch != cs)) goto nomatch;
		if (ch == '?')
		{
			str = nstr;
			continue;
		}
		if (ch == '[')
		{
			str = nstr;
			res = match_char_set(&mask, cs);
			if (res < 0) goto nomatch;
			if (!res) goto fail;
			continue;
		}
		if (ch == '*')
		{
			while (TRUE)
			{
				omask = mask;
				ch = nxchar(&mask);
				if (!ch)
				{
					ret = !strchr(str, DIR_SEP);
					goto fail;
				}
				if (ch == '*') continue;
				if (ch != '?') break;
				cs = nxchar(&str);
				if (!cs || (cs == DIR_SEP)) goto fail;
			}
		}
		else
		{
			str = nstr;
			UNQUOTE_CHAR(ch, &mask);
			if (ch == cs) continue;
nomatch:		if (!tmask) goto fail;
			omask = mask = tmask;
			str = tstr;
			ch = nxchar(&mask);
		}
		cw = ch;
		UNQUOTE_CHAR(ch, &mask);
		if (!ch) goto fail; // Escape char at end of mask
		wmask = mask;
		while (TRUE)
		{
			cs = nxchar(&str);
			if (!cs || ((cs == DIR_SEP) && (ch != cs))) goto fail;
			if (cw == '[')
			{
				res = match_char_set(&mask, cs);
				if (res > 0) break;
				if (!res) goto fail;
				mask = wmask;
			}
			else if (ch == cs) break;
		}
		tmask = omask;
		tstr = str;
	}
	while ((ch = nxchar(&mask)) == '*');
	ret = !ch;

fail:	if (!utf)
	{
		g_free(xmask);
		g_free(xstr);
	}
	return (ret);
}

#endif

// Replace '/' path separators

#ifdef WIN32

void reseparate(char *str)
{
	while ((str = strchr(str, '/'))) *str++ = DIR_SEP;
}

#endif

// Process event queue

void handle_events()
{
	int i = 20; /* To prevent endless waiting */
	while ((i-- > 0) && gtk_events_pending()) gtk_main_iteration();
}

// Make GtkEntry accept Ctrl+Enter as a character

static gboolean convert_ctrl_enter(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	if (((event->keyval == KEY(Return)) || (event->keyval == KEY(KP_Enter))) &&
		(event->state & GDK_CONTROL_MASK))
	{
#if GTK_MAJOR_VERSION == 1
		GtkEditable *edit = GTK_EDITABLE(widget);
		gint pos = edit->current_pos;

		gtk_editable_delete_selection(edit);
		gtk_editable_insert_text(edit, "\n", 1, &pos);
		edit->current_pos = pos;
#else /* if GTK_MAJOR_VERSION >= 2 */
		gtk_signal_emit_by_name(GTK_OBJECT(widget), "insert_at_cursor", "\n");
#endif
		return (TRUE);
	}
	return (FALSE);
}

void accept_ctrl_enter(GtkWidget *entry)
{
	gtk_signal_connect(GTK_OBJECT(entry), "key_press_event",
		GTK_SIGNAL_FUNC(convert_ctrl_enter), NULL);
}

// Grab/ungrab input

#define GRAB_KEY "mtPaint.grab"

/* !!! Widget is expected to have window visible & w/appropriate event masks */
int do_grab(int mode, GtkWidget *widget, GdkCursor *cursor)
{
	int owner_events = mode != GRAB_FULL;

#if GTK_MAJOR_VERSION == 1
	if (!widget->window) return (FALSE);
	if (gdk_keyboard_grab(widget->window, owner_events, GDK_CURRENT_TIME))
		return (FALSE);
	if (gdk_pointer_grab(widget->window, owner_events,
		GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK,
		NULL, cursor, GDK_CURRENT_TIME))
	{
		gdk_keyboard_ungrab(GDK_CURRENT_TIME);
		return (FALSE);
	}
	if (mode != GRAB_PROGRAM) gtk_grab_add(widget);
#elif GTK_MAJOR_VERSION == 2
	guint32 time;

	if (!widget->window) return (FALSE);
	time = gtk_get_current_event_time();
	if (gdk_keyboard_grab(widget->window, owner_events, time) != GDK_GRAB_SUCCESS)
		return (FALSE);

	if (gdk_pointer_grab(widget->window, owner_events,
		GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK | GDK_POINTER_MOTION_MASK,
		NULL, cursor, time) != GDK_GRAB_SUCCESS)
	{
		gdk_display_keyboard_ungrab(gtk_widget_get_display(widget), time);
		return (FALSE);
	}
	if (mode != GRAB_PROGRAM) gtk_grab_add(widget);
#else /* #if GTK_MAJOR_VERSION == 3 */
	guint32 time;
	GdkDevice *kbd, *mouse, *dev;
	GdkWindow *win = gtk_widget_get_window(widget);

	if (!win) return (FALSE); // Lose
	time = gtk_get_current_event_time();
	mouse = dev = gtk_get_current_event_device();
	if (!dev) return (FALSE); // If called out of the blue by mistake
	kbd = gdk_device_get_associated_device(dev);
	if (gdk_device_get_source(dev) == GDK_SOURCE_KEYBOARD) // Missed the guess
		mouse = kbd , kbd = dev;

	if (gdk_device_grab(kbd, win, GDK_OWNERSHIP_APPLICATION, owner_events,
		GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK, NULL, time) != GDK_GRAB_SUCCESS)
		return (FALSE);
	if (gdk_device_grab(mouse, win, GDK_OWNERSHIP_APPLICATION, owner_events,
		GDK_BUTTON_RELEASE_MASK | GDK_BUTTON_PRESS_MASK |
		GDK_POINTER_MOTION_MASK, cursor, time) != GDK_GRAB_SUCCESS)
	{
		gdk_device_ungrab(kbd, time);
		return (FALSE);
	}
	if (mode != GRAB_PROGRAM) gtk_device_grab_add(widget, mouse, TRUE);
	g_object_set_data(G_OBJECT(widget), GRAB_KEY, mouse);
#endif
	return (TRUE);
}

void undo_grab(GtkWidget *widget)
{
#if GTK_MAJOR_VERSION == 1
	gdk_keyboard_ungrab(GDK_CURRENT_TIME);
	gdk_pointer_ungrab(GDK_CURRENT_TIME);
	gtk_grab_remove(widget);
#elif GTK_MAJOR_VERSION == 2
	guint32 time = gtk_get_current_event_time();
	GdkDisplay *display = gtk_widget_get_display(widget);
	gdk_display_keyboard_ungrab(display, time);
	gdk_display_pointer_ungrab(display, time);
	gtk_grab_remove(widget);
#else /* #if GTK_MAJOR_VERSION == 3 */
	/* !!! Only removes what do_grab() set, here */
	guint32 time = gtk_get_current_event_time();
	GdkDevice *mouse = g_object_get_data(G_OBJECT(widget), GRAB_KEY);
	if (!mouse) return;
	gdk_device_ungrab(gdk_device_get_associated_device(mouse), time); // kbd
	gdk_device_ungrab(mouse, time);
	gtk_device_grab_remove(widget, mouse);
	g_object_set_data(G_OBJECT(widget), GRAB_KEY, NULL);
#endif
}

#if GTK_MAJOR_VERSION == 1

// Workaround for crazy GTK+1 resize handling

/* !!! GTK+1 may "short-circuit" a resize just redoing an existing allocation -
 * unless resize is queued on a toplevel and NOT from within its check_resize
 * handler. As all size_request and size_allocate handlers ARE called from
 * within it, here is a way to postpone queuing till after it finishes - WJ */

#define RESIZE_KEY "mtPaint.resize"

static guint resize_key;

static void repeat_resize(GtkContainer *cont, gpointer user_data)
{
	GtkObject *obj = GTK_OBJECT(cont);

	if (gtk_object_get_data_by_id(obj, resize_key) != (gpointer)2) return;
	gtk_object_set_data_by_id(obj, resize_key, (gpointer)1);
	gtk_widget_queue_resize(GTK_WIDGET(cont));
}

void force_resize(GtkWidget *widget)
{
	GtkObject *obj = GTK_OBJECT(gtk_widget_get_toplevel(widget));

	if (!resize_key) resize_key = g_quark_from_static_string(RESIZE_KEY);
	if (!gtk_object_get_data_by_id(obj, resize_key))
		gtk_signal_connect_after(obj, "check_resize",
			GTK_SIGNAL_FUNC(repeat_resize), NULL);
	gtk_object_set_data_by_id(obj, resize_key, (gpointer)2);
}

// Workaround for broken GTK_SHADOW_NONE viewports in GTK+1

/* !!! gtk_viewport_draw() adjusts for shadow thickness even with shadow
 * disabled, resulting in lower right boundary left undrawn; easiest fix is
 * to set thickness to 0 for such widgets - WJ */

void vport_noshadow_fix(GtkWidget *widget)
{
	static GtkStyle *defstyle;
	GtkStyleClass *class;

	if (!defstyle)
	{
		defstyle = gtk_style_new();
		class = g_malloc(sizeof(GtkStyleClass));
		memcpy(class, defstyle->klass, sizeof(GtkStyleClass));
		defstyle->klass = class;
		class->xthickness = class->ythickness = 0;
	}
	gtk_widget_set_style(widget, defstyle);
}

#endif

// Helper for accessing scrollbars

void get_scroll_adjustments(GtkWidget *win, GtkAdjustment **h, GtkAdjustment **v)
{
	GtkScrolledWindow *scroll = GTK_SCROLLED_WINDOW(win);
	*h = gtk_scrolled_window_get_hadjustment(scroll);
	*v = gtk_scrolled_window_get_vadjustment(scroll);
}

// Helper for widget show/hide

void widget_showhide(GtkWidget *widget, int what)
{
	(what ? gtk_widget_show : gtk_widget_hide)(widget);
}

// Color name to value

int parse_color(char *what)
{
#if GTK_MAJOR_VERSION == 3
	GdkRGBA col;

	if (!gdk_rgba_parse(&col, what)) return (-1);
	return (RGB_2_INT(((int)(col.red * 65535) + 128) / 257,
		((int)(col.green * 65535) + 128) / 257,
		((int)(col.blue * 65535) + 128) / 257));
#else /* if GTK_MAJOR_VERSION <= 2 */
	GdkColor col;

	if (!gdk_color_parse(what, &col)) return (-1);
	return (RGB_2_INT(((int)col.red + 128) / 257,
		((int)col.green + 128) / 257,
		((int)col.blue + 128) / 257));
#endif
}

//	DPI value

double window_dpi(GtkWidget *win)
{
#if GTK_MAJOR_VERSION == 3
	GValue v;
	GdkScreen *sc = gtk_widget_get_screen(win);
	double d = gdk_screen_get_resolution(sc);
	if (d > 0) return (d); // Seems good

	memset(&v, 0, sizeof(v));
	g_value_init(&v, G_TYPE_INT);
	if (gdk_screen_get_setting(sc, "gtk-xft-dpi", &v))
		return (g_value_get_int(&v) / (double)1024.0);
#ifdef GDK_WINDOWING_X11
	{
		/* Get DPI from Xft */
		char *e, *v = XGetDefault(GDK_SCREEN_XDISPLAY(sc), "Xft", "dpi");
		if (v)
		{
			d = g_strtod(v, &e);
			if (e != v) return (d); // Seems good
		}
	}
#endif
	return ((gdk_screen_get_height(sc) * (double)25.4) / gdk_screen_get_height_mm(sc));
#else /* if GTK_MAJOR_VERSION <= 2 */
#if GTK2VERSION >= 10
	double d = gdk_screen_get_resolution(gdk_drawable_get_screen(win->window));
	if (d > 0) return (d); // Seems good
#endif
#if GTK2VERSION >= 4
	{
		GValue v;
		memset(&v, 0, sizeof(v));
		g_value_init(&v, G_TYPE_INT);
		if (gdk_screen_get_setting(gdk_drawable_get_screen(win->window),
			"gtk-xft-dpi", &v))
			return (g_value_get_int(&v) / (double)1024.0);
	}
#endif
#if defined(U_MTK) || defined(GDK_WINDOWING_X11) /* GTK+2/X */
	{
		/* Get DPI from Xft */
		char *e, *v = XGetDefault(GDK_WINDOW_XDISPLAY(win->window),
			"Xft", "dpi");
		if (v)
		{
			double d = g_strtod(v, &e);
			if (e != v) return (d); // Seems good
		}
	}
#endif
#if GTK2VERSION >= 2
	{
		GdkScreen *sc = gdk_drawable_get_screen(win->window);
		return ((gdk_screen_get_height(sc) * (double)25.4) /
			gdk_screen_get_height_mm(sc));
	}
#else
	return ((gdk_screen_height() * (double)25.4) /
		gdk_screen_height_mm());
#endif
#endif /* GTK+1&2 */
}

//	Memory size (Mb)

#ifndef WIN32
#ifndef _SC_PHYS_PAGES
#include <sys/types.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#endif
#endif

unsigned sys_mem_size()
{
#ifdef WIN32
	MEMORYSTATUS mem;
	mem.dwLength = sizeof(mem);
	GlobalMemoryStatus(&mem);
	return (mem.dwTotalPhys / (1024 * 1024));
#elif defined _SC_PHYS_PAGES
	size_t n;
#ifdef _SC_PAGESIZE
	n = sysconf(_SC_PAGESIZE);
#else
	n = sysconf(_SC_PAGE_SIZE);
#endif
	return (((n / 1024) * sysconf(_SC_PHYS_PAGES)) / 1024);
#elif defined CTL_HW
#undef FAIL
	int mib[2] = { CTL_HW };
	size_t n;
#ifdef HW_MEMSIZE 
	uint64_t v;
	mib[1] = HW_MEMSIZE;
#elif defined HW_PHYSMEM64
	uint64_t v;
	mib[1] = HW_PHYSMEM64;
#elif defined HW_REALMEM
	unsigned long v;
	mib[1] = HW_REALMEM;
#elif defined HW_PHYSMEM
	unsigned long v;
	mib[1] = HW_PHYSMEM;
#else
#define FAIL
#endif
#ifndef FAIL
	n = sizeof(v);
	if (!sysctl(mib, 2, &v, &n, NULL, 0) && (n == sizeof(v)))
		return ((unsigned)(v / (1024 * 1024)));
#endif
#endif
	return (0); // Fail
}

// Threading helpers

#if 0 /* Not needed for now - GTK+/Win32 still isn't thread-safe anyway */
//#ifdef U_THREADS

/* The following is a replacement for gdk_threads_add_*() functions - which are
 * useful, but hadn't been implemented before GTK+ 2.12 - WJ */

#if GTK_MAJOR_VERSION == 1

/* With GLib 1.2, a g_source_is_destroyed()-like check cannot be done from
 * outside GLib, so thread-safety is limited (see GTK+ bug #321866) */

typedef struct {
	GSourceFunc callback;
	gpointer user_data;
} dispatch_info;

static gboolean do_dispatch(dispatch_info *info)
{
	gboolean res;

	gdk_threads_enter();
	res = info->callback(info->user_data);
	gdk_threads_leave();
	return (res);
}

guint threads_idle_add_priority(gint priority, GtkFunction function, gpointer data)
{
	dispatch_info *disp = g_malloc(sizeof(dispatch_info));

	disp->callback = function;
	disp->user_data = data;
	return (g_idle_add_full(priority, (GSourceFunc)do_dispatch, disp,
		(GDestroyNotify)g_free));
}

guint threads_timeout_add(guint32 interval, GSourceFunc function, gpointer data)
{
	dispatch_info *disp = g_malloc(sizeof(dispatch_info));

	disp->callback = function;
	disp->user_data = data;
	return (g_timeout_add_full(G_PRIORITY_DEFAULT, interval,
		(GSourceFunc)do_dispatch, disp, (GDestroyNotify)g_free));
}

#else /* if GTK_MAJOR_VERSION == 2 */

static GSourceFuncs threads_timeout_funcs, threads_idle_funcs;

static gboolean threads_timeout_dispatch(GSource *source, GSourceFunc callback,
	gpointer user_data)
{
	gboolean res = FALSE;

	gdk_threads_enter();
	/* The test below is what g_source_is_destroyed() in GLib 2.12+ does */
	if (source->flags & G_HOOK_FLAG_ACTIVE)
		res = g_timeout_funcs.dispatch(source, callback, user_data);
	gdk_threads_leave();
	return (res);
}

static gboolean threads_idle_dispatch(GSource *source, GSourceFunc callback,
	gpointer user_data)
{
	gboolean res = FALSE;

	gdk_threads_enter();
	/* The test below is what g_source_is_destroyed() in GLib 2.12+ does */
	if (source->flags & G_HOOK_FLAG_ACTIVE)
		res = g_idle_funcs.dispatch(source, callback, user_data);
	gdk_threads_leave();
	return (res);
}

guint threads_timeout_add(guint32 interval, GSourceFunc function, gpointer data)
{
	GSource *source = g_timeout_source_new(interval);
	guint id;

	if (!threads_timeout_funcs.dispatch)
	{
		threads_timeout_funcs = g_timeout_funcs;
		threads_timeout_funcs.dispatch = threads_timeout_dispatch;
	}
	source->source_funcs = &threads_timeout_funcs;

	g_source_set_callback(source, function, data, NULL);
	id = g_source_attach(source, NULL);
	g_source_unref(source);

	return (id);
}

guint threads_idle_add_priority(gint priority, GtkFunction function, gpointer data)
{
	GSource *source = g_idle_source_new();
	guint id;

	if (!threads_idle_funcs.dispatch)
	{
		threads_idle_funcs = g_idle_funcs;
		threads_idle_funcs.dispatch = threads_idle_dispatch;
	}
	source->source_funcs = &threads_idle_funcs;

	if (priority != G_PRIORITY_DEFAULT_IDLE)
		g_source_set_priority(source, priority);
	g_source_set_callback(source, function, data, NULL);
	id = g_source_attach(source, NULL);
	g_source_unref(source);

	return (id);
}

#endif

#endif

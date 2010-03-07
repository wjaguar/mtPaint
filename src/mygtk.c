/*	mygtk.c
	Copyright (C) 2004-2010 Mark Tyler and Dmitry Groshev

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
#include "png.h"
#include "mainwindow.h"
#include "canvas.h"
#include "inifile.h"
#include "fpick.h"

#if GTK_MAJOR_VERSION == 1
#include <gtk/gtkprivate.h>
#endif

#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#elif defined GDK_WINDOWING_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <gdk/gdkwin32.h>
#endif


///	GENERIC WIDGET PRIMITIVES

static GtkWidget *spin_new_x(GtkObject *adj, int fpart);

GtkWidget *add_a_window( GtkWindowType type, char *title, GtkWindowPosition pos, gboolean modal )
{
	GtkWidget *win = gtk_window_new(type);
	gtk_window_set_title(GTK_WINDOW(win), title);
	gtk_window_set_position(GTK_WINDOW(win), pos);
	gtk_window_set_modal(GTK_WINDOW(win), modal);

	return win;
}

GtkWidget *add_a_button( char *text, int bord, GtkWidget *box, gboolean filler )
{
	GtkWidget *button = gtk_button_new_with_label(text);
	gtk_widget_show (button);
	gtk_box_pack_start (GTK_BOX (box), button, filler, filler, 0);
	gtk_container_set_border_width (GTK_CONTAINER (button), bord);

	return button;
}

GtkWidget *add_a_spin( int value, int min, int max )
{
	return (spin_new_x(gtk_adjustment_new(value, min, max, 1, 10, 0), 0));
}

GtkWidget *add_a_table( int rows, int columns, int bord, GtkWidget *box )
{
	GtkWidget *table = pack(box, gtk_table_new(rows, columns, FALSE));
	gtk_widget_show(table);
	gtk_container_set_border_width(GTK_CONTAINER(table), bord);

	return table;
}

GtkWidget *add_a_toggle( char *label, GtkWidget *box, gboolean value )
{
	return (pack(box, sig_toggle(label, value, NULL, NULL)));
}

GtkWidget *add_to_table_l(char *text, GtkWidget *table, int row, int column,
	int l, int spacing)
{
	GtkWidget *label;

	label = gtk_label_new(text);
	gtk_widget_show(label);
	gtk_table_attach(GTK_TABLE(table), label, column, column + l, row, row + 1,
		(GtkAttachOptions) (GTK_FILL), (GtkAttachOptions) (0), spacing, spacing);
	gtk_label_set_justify(GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC (label), 0.0, 0.5);

	return (label);
}

GtkWidget *add_to_table(char *text, GtkWidget *table, int row, int column, int spacing)
{
	return (add_to_table_l(text, table, row, column, 1, spacing));
}

GtkWidget *to_table(GtkWidget *widget, GtkWidget *table, int row, int column, int spacing)
{
	gtk_table_attach(GTK_TABLE(table), widget, column, column + 1, row, row + 1,
		(GtkAttachOptions)(GTK_EXPAND | GTK_FILL),
		(GtkAttachOptions) (0), 0, spacing);
	return (widget);
}

GtkWidget *to_table_l(GtkWidget *widget, GtkWidget *table, int row, int column,
	int l, int spacing)
{
	gtk_table_attach(GTK_TABLE(table), widget, column, column + l, row, row + 1,
		(GtkAttachOptions)(GTK_FILL), (GtkAttachOptions) (0), 0, spacing);
	return (widget);
}

GtkWidget *spin_to_table(GtkWidget *table, int row, int column, int spacing,
	int value, int min, int max)
{
	GtkWidget *spin = add_a_spin( value, min, max );
	gtk_table_attach(GTK_TABLE(table), spin, column, column+1, row, row+1,
		(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		(GtkAttachOptions) (0), 0, spacing);
	return (spin);
}

GtkWidget *float_spin_to_table(GtkWidget *table, int row, int column, int spacing,
	double value, double min, double max)
{
	GtkWidget *spin = add_float_spin(value, min, max);
	gtk_table_attach(GTK_TABLE(table), spin, column, column+1, row, row+1,
		(GtkAttachOptions) (GTK_EXPAND | GTK_FILL),
		(GtkAttachOptions) (0), 0, spacing);
	return (spin);
}

void add_hseparator( GtkWidget *widget, int xs, int ys )
{
	GtkWidget *sep = pack(widget, gtk_hseparator_new());
	gtk_widget_show(sep);
	gtk_widget_set_usize(sep, xs, ys);
}



////	PROGRESS WINDOW

static GtkWidget *progress_window, *progress_bar;
static int prog_stop;

static void do_cancel_progress()
{
	prog_stop = 1;
}

static gboolean delete_progress(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	return TRUE;			// This stops the user closing the window via the window manager
}

void progress_init(char *text, int canc)		// Initialise progress window
{
	GtkWidget *vbox6, *button_cancel, *viewport;

	progress_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Please Wait ..."),
		GTK_WIN_POS_CENTER, TRUE );
	gtk_widget_set_usize (progress_window, 400, -2);

	viewport = gtk_viewport_new (NULL, NULL);
	gtk_widget_show(viewport);
	gtk_container_add( GTK_CONTAINER( progress_window ), viewport );
	gtk_viewport_set_shadow_type( GTK_VIEWPORT(viewport), GTK_SHADOW_ETCHED_OUT );
	vbox6 = add_vbox(viewport);

	progress_bar = pack(vbox6, gtk_progress_bar_new());
	gtk_progress_set_format_string( GTK_PROGRESS (progress_bar), text );
	gtk_progress_set_show_text( GTK_PROGRESS (progress_bar), TRUE );
	gtk_container_set_border_width (GTK_CONTAINER (vbox6), 10);
	gtk_widget_show( progress_bar );

	if ( canc == 1 )
	{
		add_hseparator( vbox6, -2, 10 );
		button_cancel = add_a_button(_("STOP"), 5, vbox6, TRUE);
		gtk_signal_connect(GTK_OBJECT(button_cancel), "clicked",
			GTK_SIGNAL_FUNC(do_cancel_progress), NULL);
	}

	gtk_signal_connect_object (GTK_OBJECT (progress_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_progress), NULL);

	prog_stop = 0;
	gtk_widget_show( progress_window );
	progress_update(0.0);
}

int progress_update(float val)		// Update progress window
{
	gtk_progress_set_percentage( GTK_PROGRESS (progress_bar), val );
	handle_events();
	return (prog_stop);
}

void progress_end()			// Close progress window
{
	if ( progress_window != 0 )
	{
		gtk_widget_destroy( progress_window );
		progress_window = NULL;
	}
}



////	ALERT BOX

static int alert_result;

static void alert_reply(gpointer data)
{
	if (!alert_result) alert_result = (int)data;
}

int alert_box(char *title, char *message, char *text1, ...)
{
	va_list args;
	GtkWidget *alert, *button, *label;
	char *txt;
	int i = 0;
	GtkAccelGroup* ag = gtk_accel_group_new();

	/* This function must be immune to pointer grabs */
	release_grab();

	alert = gtk_dialog_new();
	gtk_window_set_title( GTK_WINDOW(alert), title );
	gtk_window_set_modal( GTK_WINDOW(alert), TRUE );
	gtk_window_set_position( GTK_WINDOW(alert), GTK_WIN_POS_CENTER );
	gtk_container_set_border_width( GTK_CONTAINER(alert), 6 );
	
	label = gtk_label_new( message );
	gtk_label_set_line_wrap( GTK_LABEL(label), TRUE );
	gtk_box_pack_start( GTK_BOX(GTK_DIALOG(alert)->vbox), label, TRUE, FALSE, 8 );
	gtk_widget_show( label );

	txt = text1 ? text1 : _("OK");
	va_start(args, text1);
	while (TRUE)
	{
		button = add_a_button(txt, 2, GTK_DIALOG(alert)->action_area, TRUE);
		if (!i) gtk_widget_add_accelerator(button, "clicked", ag,
			GDK_Escape, 0, (GtkAccelFlags)0);
		gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(alert_reply), (gpointer)(++i));
		if (!text1) break;
		if (!(txt = va_arg(args, char *))) break;
	}
	va_end(args);

	gtk_signal_connect_object(GTK_OBJECT(alert), "destroy",
		GTK_SIGNAL_FUNC(alert_reply), (gpointer)(++i));
	gtk_window_set_transient_for( GTK_WINDOW(alert), GTK_WINDOW(main_window) );
	gtk_widget_show(alert);
	gdk_window_raise(alert->window);
	alert_result = 0;
	gtk_window_add_accel_group(GTK_WINDOW(alert), ag);

	while (!alert_result) gtk_main_iteration();
	if (alert_result != i) gtk_widget_destroy(alert);
	else alert_result = 1;

	return (alert_result);
}

// Add page to notebook

GtkWidget *add_new_page(GtkWidget *notebook, char *name)
{
	GtkWidget *page, *label;

	page = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(page);
	label = gtk_label_new(name);
	gtk_widget_show(label);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), page, label);
	return (page);
}

// Slider-spin combo (practically a new widget class)

GtkWidget *mt_spinslide_new(int swidth, int sheight)
{
	GtkWidget *box, *slider, *spin;
	GtkObject *adj;

	adj = gtk_adjustment_new(0, 0, 1, 1, 10, 0);
	box = gtk_hbox_new(FALSE, 0);

	slider = gtk_hscale_new(GTK_ADJUSTMENT(adj));
	gtk_box_pack_start(GTK_BOX(box), slider, swidth < 0, TRUE, 0);
	gtk_widget_set_usize(slider, swidth, sheight);
	gtk_scale_set_draw_value(GTK_SCALE(slider), FALSE);
	gtk_scale_set_digits(GTK_SCALE(slider), 0);

	spin = spin_new_x(adj, 0);
	gtk_box_pack_start(GTK_BOX(box), spin, swidth >= 0, TRUE, 2);

	gtk_widget_show_all(box);
	return (box);
}

void mt_spinslide_set_range(GtkWidget *spinslide, int minv, int maxv)
{
	GtkAdjustment *adj;
	
	adj = gtk_range_get_adjustment(GTK_RANGE(BOX_CHILD_0(spinslide)));
	adj->lower = minv;
	adj->upper = maxv;
	gtk_adjustment_changed(adj);
}

int mt_spinslide_get_value(GtkWidget *spinslide)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON(BOX_CHILD_1(spinslide));
	gtk_spin_button_update(spin);
	return (gtk_spin_button_get_value_as_int(spin));
}

/* Different in that this doesn't force slider to integer-value position */
int mt_spinslide_read_value(GtkWidget *spinslide)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON(BOX_CHILD_1(spinslide));
	return (gtk_spin_button_get_value_as_int(spin));
}

void mt_spinslide_set_value(GtkWidget *spinslide, int value)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON(BOX_CHILD_1(spinslide));
	gtk_spin_button_set_value(spin, value);
}

/* void handler(GtkAdjustment *adjustment, gpointer user_data); */
void mt_spinslide_connect(GtkWidget *spinslide, GtkSignalFunc handler,
	gpointer user_data)
{
	GtkAdjustment *adj;
	
	adj = gtk_range_get_adjustment(GTK_RANGE(BOX_CHILD_0(spinslide)));
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed", handler, user_data);
}

// Managing batches of radio buttons with minimum of fuss

static void wj_radio_toggle(GtkWidget *btn, gpointer idx)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) return;
	*(int *)gtk_object_get_user_data(GTK_OBJECT(btn->parent)) = (int)idx;
}

/* void handler(GtkWidget *btn, gpointer user_data); */
GtkWidget *wj_radio_pack(char **names, int cnt, int vnum, int idx, gpointer var,
	GtkSignalFunc handler)
{
	int i, j;
	GtkWidget *box, *wbox, *button = NULL;
	GtkSignalFunc hdl = handler;

	if (!hdl && var) hdl = GTK_SIGNAL_FUNC(wj_radio_toggle);
	box = wbox = vnum > 0 ? gtk_hbox_new(FALSE, 0) : gtk_vbox_new(FALSE, 0);
	if (vnum < 2) gtk_object_set_user_data(GTK_OBJECT(wbox), var);

	for (i = j = 0; (i != cnt) && names[i]; i++)
	{
		if (!names[i][0]) continue;
		button = gtk_radio_button_new_with_label_from_widget(
			GTK_RADIO_BUTTON_0(button), names[i]);
		if ((vnum > 1) && !(j % vnum))
		{
			wbox = xpack(box, gtk_vbox_new(FALSE, 0));
			gtk_object_set_user_data(GTK_OBJECT(wbox), var);
		}
		gtk_container_set_border_width(GTK_CONTAINER(button), 5);
		pack(wbox, button);
		if (i == idx) gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(button), TRUE);
		if (hdl) gtk_signal_connect(GTK_OBJECT(button), "toggled", hdl,
			(gpointer)(i));
		j++;
	}
	if (hdl != handler) *(int *)var = idx < i ? idx : 0;
	gtk_widget_show_all(box);

	return (box);
}

// Convert window close into a button click ("Cancel" or whatever)

static gboolean do_delete_to_click(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	gtk_signal_emit_by_name(GTK_OBJECT(data), "clicked");
	return (TRUE); // Click handler can delete window, or let it be
}

void delete_to_click(GtkWidget *window, GtkWidget *button)
{
	gtk_signal_connect(GTK_OBJECT(window), "delete_event",
		GTK_SIGNAL_FUNC(do_delete_to_click), button);
}

// Buttons for standard dialogs

GtkWidget *OK_box(int border, GtkWidget *window, char *nOK, GtkSignalFunc OK,
	char *nCancel, GtkSignalFunc Cancel)
{
	GtkWidget *hbox, *ok_button, *cancel_button;
	GtkAccelGroup* ag = gtk_accel_group_new();

	hbox = gtk_hbox_new(TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), border);
	cancel_button = xpack(hbox, gtk_button_new_with_label(nCancel));
	gtk_container_set_border_width(GTK_CONTAINER(cancel_button), 5);
	gtk_signal_connect_object(GTK_OBJECT(cancel_button), "clicked",
		Cancel, GTK_OBJECT(window));
	gtk_widget_add_accelerator(cancel_button, "clicked", ag, GDK_Escape, 0,
		(GtkAccelFlags)0);

	ok_button = xpack(hbox, gtk_button_new_with_label(nOK));
	gtk_container_set_border_width(GTK_CONTAINER(ok_button), 5);
	gtk_signal_connect_object(GTK_OBJECT(ok_button), "clicked",
		OK, GTK_OBJECT(window));
	gtk_widget_add_accelerator(ok_button, "clicked", ag, GDK_Return, 0,
		(GtkAccelFlags)0);
	gtk_widget_add_accelerator(ok_button, "clicked", ag, GDK_KP_Enter, 0,
		(GtkAccelFlags)0);
 
	gtk_window_add_accel_group(GTK_WINDOW(window), ag);
	delete_to_click(window, cancel_button);
	gtk_object_set_user_data(GTK_OBJECT(hbox), (gpointer)window);
	gtk_widget_show_all(hbox);
	return (hbox);
}

static GtkObject *OK_box_ins(GtkWidget *box, GtkWidget *button)
{
	xpack(box, button);
	gtk_box_reorder_child(GTK_BOX(box), button, 1);
	gtk_container_set_border_width(GTK_CONTAINER(button), 5);
	gtk_widget_show(button);
	return (GTK_OBJECT(gtk_object_get_user_data(GTK_OBJECT(box))));
}

GtkWidget *OK_box_add(GtkWidget *box, char *name, GtkSignalFunc Handler)
{
	GtkWidget *button = gtk_button_new_with_label(name);
	GtkObject *win = OK_box_ins(box, button);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked", Handler, win);
	return (button);
}

GtkWidget *OK_box_add_toggle(GtkWidget *box, char *name, GtkSignalFunc Handler)
{
	GtkWidget *button = gtk_toggle_button_new_with_label(name);
	GtkObject *win = OK_box_ins(box, button);
	gtk_signal_connect(GTK_OBJECT(button), "toggled", Handler, win);
	return (button);
}

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
	return (GTK_SPIN_BUTTON(spin)->adjustment->value);
}

#if (GTK_MAJOR_VERSION == 1) && !U_MTK

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

static GtkWidget *spin_new_x(GtkObject *adj, int fpart)
{
	GtkWidget *spin = gtk_spin_button_new(GTK_ADJUSTMENT(adj), 1, fpart);
#if (GTK_MAJOR_VERSION == 1) && !U_MTK
	gtk_signal_connect_after(GTK_OBJECT(spin), "size_request",
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
#if GTK_MAJOR_VERSION == 2
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

// Extracting widget from GtkTable

GtkWidget *table_slot(GtkWidget *table, int row, int col)
{
	GList *curr;

	for (curr = GTK_TABLE(table)->children; curr; curr = curr->next)
	{
		if ((((GtkTableChild *)curr->data)->left_attach == col) &&
			(((GtkTableChild *)curr->data)->top_attach == row))
			return (((GtkTableChild *)curr->data)->widget);
	}
	return (NULL);
}

// Packing framed widget

GtkWidget *add_with_frame_x(GtkWidget *box, char *text, GtkWidget *widget,
	int border, int expand)
{
	GtkWidget *frame = gtk_frame_new(text);
	gtk_widget_show(frame);
	if (box) gtk_box_pack_start(GTK_BOX(box), frame, !!expand, !!expand, 0);
	gtk_container_set_border_width(GTK_CONTAINER(frame), border);
	gtk_container_add(GTK_CONTAINER(frame), widget);
	return (frame);
}

GtkWidget *add_with_frame(GtkWidget *box, char *text, GtkWidget *widget)
{
	return (add_with_frame_x(box, text, widget, 5, FALSE));
}

// Option menu

static void wj_option(GtkMenuItem *menuitem, gpointer user_data)
{
	*(int *)user_data = (int)gtk_object_get_user_data(GTK_OBJECT(menuitem));
}

#if GTK_MAJOR_VERSION == 2

/* Cause the size to be properly reevaluated */
void wj_option_realize(GtkWidget *widget, gpointer user_data)
{
	gtk_signal_emit_by_name(GTK_OBJECT(gtk_option_menu_get_menu(
		GTK_OPTION_MENU(widget))), "selection_done");
}

#endif

/* void handler(GtkMenuItem *menuitem, gpointer user_data); */
GtkWidget *wj_option_menu(char **names, int cnt, int idx, gpointer var,
	GtkSignalFunc handler)
{
	int i, j;
	GtkWidget *opt, *menu, *item;
	GtkSignalFunc hdl = handler;

	if (!hdl && var) hdl = GTK_SIGNAL_FUNC(wj_option);
	opt = gtk_option_menu_new();
	menu = gtk_menu_new();
	for (i = j = 0; (i != cnt) && names[i]; i++)
	{
		if (!names[i][0]) continue;
		item = gtk_menu_item_new_with_label(names[i]);
		gtk_object_set_user_data(GTK_OBJECT(item), (gpointer)i);
		if (hdl) gtk_signal_connect(GTK_OBJECT(item), "activate", hdl, var);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		if (i == idx) j = i;
  	}
	if (hdl != handler) *(int *)var = idx < i ? idx : 0;
	gtk_widget_show_all(menu);
	gtk_widget_show(opt); /* !!! Show now - or size won't be set properly */
	gtk_option_menu_set_menu(GTK_OPTION_MENU(opt), menu);
	gtk_option_menu_set_history(GTK_OPTION_MENU(opt), j);

	FIX_OPTION_MENU_SIZE(opt);

	return (opt);
}

int wj_option_menu_get_history(GtkWidget *optmenu)
{
	optmenu = gtk_option_menu_get_menu(GTK_OPTION_MENU(optmenu));
	optmenu = gtk_menu_get_active(GTK_MENU(optmenu));
	return ((int)gtk_object_get_user_data(GTK_OBJECT(optmenu)));
}

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
	gtk_signal_connect_after(GTK_OBJECT(widget), "size_request",
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

// Make widget request no less size than before (in one direction)

#define KEEPSIZE_KEY "mtPaint.keepsize"

static guint keepsize_key;

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
	gtk_signal_connect_after(GTK_OBJECT(widget), "size_request",
		GTK_SIGNAL_FUNC(widget_size_keep), (gpointer)keep_height);
}

// Signalled toggles

static void sig_toggle_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	*(int *)user_data = gtk_toggle_button_get_active(togglebutton);
}

static void make_sig_toggle(GtkWidget *tog, int value, gpointer var, GtkSignalFunc handler)
{
	if (!handler && var)
	{
		*(int *)var = value;
		handler = GTK_SIGNAL_FUNC(sig_toggle_toggled);
	}

	gtk_widget_show(tog);
	gtk_container_set_border_width(GTK_CONTAINER(tog), 5);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tog), value);
	if (handler) gtk_signal_connect(GTK_OBJECT(tog), "toggled", handler,
		(gpointer)var);
}

GtkWidget *sig_toggle(char *label, int value, gpointer var, GtkSignalFunc handler)
{
	GtkWidget *tog = gtk_check_button_new_with_label(label);
	make_sig_toggle(tog, value, var, handler);
	return (tog);
}

GtkWidget *sig_toggle_button(char *label, int value, gpointer var, GtkSignalFunc handler)
{
	GtkWidget *tog = gtk_toggle_button_new_with_label(label);
	make_sig_toggle(tog, value, var, handler);
	return (tog);
}

// Path box

static void click_file_browse(GtkWidget *widget, gpointer data)
{
	int flag = FPICK_LOAD;
	GtkWidget *fs;

	if ((int)data == FS_SELECT_DIR) flag |= FPICK_DIRS_ONLY;

	fs = fpick_create((char *)gtk_object_get_user_data(GTK_OBJECT(widget)), flag);
	gtk_object_set_data(GTK_OBJECT(fs), FS_ENTRY_KEY,
		BOX_CHILD_0(widget->parent));
	fs_setup(fs, (int)data);
}

GtkWidget *mt_path_box(char *name, GtkWidget *box, char *title, int fsmode)
{
	GtkWidget *hbox, *entry, *button;

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_widget_show(hbox);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);

	add_with_frame(box, name, hbox);
	entry = xpack5(hbox, gtk_entry_new());
	gtk_widget_show(entry);
	button = add_a_button(_("Browse"), 2, hbox, FALSE);
	gtk_object_set_user_data(GTK_OBJECT(button), title);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_file_browse), (gpointer)fsmode);

	return (entry);
}

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

#else /* GTK1 doesn't have this bug */

void clist_enable_drag(GtkWidget *clist)
{
	gtk_clist_set_reorderable(GTK_CLIST(clist), TRUE);
}

#endif

// Move browse-mode selection in GtkCList without invoking callbacks

void clist_reselect_row(GtkCList *clist, int n)
{
	GtkWidget *widget;

	if (n < 0) return;
#if GTK_MAJOR_VERSION == 1
	GTK_CLIST_CLASS(((GtkObject *)clist)->klass)->select_row(clist, n, -1, NULL);
#else /* if GTK_MAJOR_VERSION == 2 */
	GTK_CLIST_GET_CLASS(clist)->select_row(clist, n, -1, NULL);
#endif
	/* !!! Focus fails to follow selection in browse mode - have to move
	 * it here; but it means a full redraw is necessary afterwards */
	if (clist->focus_row == n) return;
	clist->focus_row = n;
	widget = GTK_WIDGET(clist);
	if (GTK_WIDGET_HAS_FOCUS(widget) && !clist->freeze_count)
		gtk_widget_queue_draw(widget);
}

// Move browse-mode selection in GtkList

/* !!! An evil hack for reliably moving focus around in GtkList */
void list_select_item(GtkWidget *list, GtkWidget *item)
{
	GtkWidget *win, *fw = NULL;

	win = gtk_widget_get_toplevel(list);
	if (GTK_IS_WINDOW(win)) fw = GTK_WINDOW(win)->focus_widget;

	/* Focus is somewhere in list - move it, selection will follow */
	if (fw && gtk_widget_is_ancestor(fw, list))
		gtk_widget_grab_focus(item);
	else /* Focus is elsewhere - move whatever remains, then */
	{
	/* !!! For simplicity, an undocumented field is used; a bit less hacky
	 * but longer is to set focus child to item, NULL, and item again - WJ */
		gtk_container_set_focus_child(GTK_CONTAINER(list), item);
		GTK_LIST(list)->last_focus_child = item;
	}
}

// Properly destroy transient window

void destroy_dialog(GtkWidget *window)
{
	/* Needed in Windows to stop GTK+ lowering the main window */
	gtk_window_set_transient_for(GTK_WINDOW(window), NULL);
	gtk_widget_destroy(window);
}

// Settings notebook

GtkWidget *plain_book(GtkWidget **pages, int npages)
{
	GtkWidget *notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
	while (npages--)
	{
		*pages = gtk_vbox_new(FALSE, 0);
		gtk_notebook_append_page(GTK_NOTEBOOK(notebook), *pages, NULL);
		pages++;
	}
	gtk_widget_show_all(notebook);
	return (notebook);
}

static void toggle_book(GtkToggleButton *button, GtkNotebook *book)
{
	int i = gtk_toggle_button_get_active(button);
	gtk_notebook_set_page(book, i ? 1 : 0);
}

GtkWidget *buttoned_book(GtkWidget **page0, GtkWidget **page1,
	GtkWidget **button, char *button_label)
{
	GtkWidget *notebook, *pages[2];

	notebook = plain_book(pages, 2);
	*page0 = pages[0];
	*page1 = pages[1];
	*button = sig_toggle_button(button_label, FALSE, GTK_NOTEBOOK(notebook),
		GTK_SIGNAL_FUNC(toggle_book));
	return (notebook);
}

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

GtkWidget *pack5(GtkWidget *box, GtkWidget *widget)
{
	gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, 5);
	return (widget);
}

GtkWidget *xpack5(GtkWidget *box, GtkWidget *widget)
{
	gtk_box_pack_start(GTK_BOX(box), widget, TRUE, TRUE, 5);
	return (widget);
}

GtkWidget *pack_end5(GtkWidget *box, GtkWidget *widget)
{
	gtk_box_pack_end(GTK_BOX(box), widget, FALSE, FALSE, 5);
	return (widget);
}

// Put vbox into container

GtkWidget *add_vbox(GtkWidget *cont)
{
	GtkWidget *box = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(box);
	gtk_container_add(GTK_CONTAINER(cont), box);
	return (box);
}

// Save/restore window positions

void win_store_pos(GtkWidget *window, char *inikey)
{
	char name[128];
	gint xywh[4];
	int i, l = strlen(inikey);

	memcpy(name, inikey, l);
	name[l++] = '_'; name[l + 1] = '\0';
	gdk_window_get_size(window->window, xywh + 2, xywh + 3);
	gdk_window_get_root_origin(window->window, xywh + 0, xywh + 1);
	for (i = 0; i < 4; i++)
	{
		name[l] = "xywh"[i];
		inifile_set_gint32(name, xywh[i]);
	}
}

void win_restore_pos(GtkWidget *window, char *inikey, int defx, int defy,
	int defw, int defh)
{
	char name[128];
	int i, l = strlen(inikey), xywh[4] = { defx, defy, defw, defh };

	memcpy(name, inikey, l);
	name[l++] = '_'; name[l + 1] = '\0';
	for (i = 0; i < 4; i++)
	{
		if (xywh[i] < 0) continue; /* Default of -1 means auto-size */
		name[l] = "xywh"[i];
		xywh[i] = inifile_get_gint32(name, xywh[i]);
	}
	gtk_window_set_default_size(GTK_WINDOW(window), xywh[2], xywh[3]);
	gtk_widget_set_uposition(window, xywh[0], xywh[1]);
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
	gtk_signal_connect_after(GTK_OBJECT(widget), "realize",
		GTK_SIGNAL_FUNC(paned_realize), NULL);
}

#endif

// Init-time bugfixes

/* Bugs: GtkViewport size request in GTK+1; GtkHScale breakage in Smooth Theme
 * Engine in GTK+1; mixing up keys in GTK+2/Windows; opaque focus rectangle in
 * Gtk-Qt theme engine (v0.8) in GTK+2/X */

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
}

#elif defined GDK_WINDOWING_WIN32

static int win_last_vk;
static guint32 win_last_lp;

/* Event filter to look at WM_KEYDOWN and WM_SYSKEYDOWN */
static GdkFilterReturn win_keys_peek(GdkXEvent *xevent, GdkEvent *event, gpointer data)
{
	MSG *msg = xevent;

	if ((msg->message == WM_KEYDOWN) || (msg->message == WM_SYSKEYDOWN))
	{
		win_last_vk = msg->wParam;
		win_last_lp = msg->lParam;
	}
	return (GDK_FILTER_CONTINUE);
}

void gtk_init_bugfixes()
{
	gdk_window_add_filter(NULL, (GdkFilterFunc)win_keys_peek, NULL);
}

#else /* if defined GDK_WINDOWING_X11 */

/* Gtk-Qt's author was deluded when he decided he knows how to draw focus;
 * doing nothing is *FAR* preferable to opaque box over a widget - WJ */
static void fake_draw_focus()
{
	return;
}

void gtk_init_bugfixes()
{
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
}

#endif

// Whatever is needed to move mouse pointer 

#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11 /* Call X */

int move_mouse_relative(int dx, int dy)
{
	XWarpPointer(GDK_WINDOW_XDISPLAY(drawing_canvas->window),
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

#elif (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION >= 8) /* GTK+ 2.8+ */

int move_mouse_relative(int dx, int dy)
{
	gint x0, y0;
	GdkScreen *screen;
	GdkDisplay *display = gtk_widget_get_display(drawing_canvas);

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
	return (XKeysymToKeycode(GDK_WINDOW_XDISPLAY(drawing_canvas->window),
		event->keyval));
}

guint low_key(GdkEventKey *event)
{
	return (gdk_keyval_to_lower(event->keyval));
}

guint keyval_key(guint keyval)
{
	return (XKeysymToKeycode(GDK_WINDOW_XDISPLAY(drawing_canvas->window),
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

#else /* X11 */

guint low_key(GdkEventKey *event)
{
	return (gdk_keyval_to_lower(event->keyval));
}

#endif

guint keyval_key(guint keyval)
{
	GdkDisplay *display = gtk_widget_get_display(drawing_canvas);
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

int arrow_key(GdkEventKey *event, int *dx, int *dy, int mult)
{
	if ((event->state & (GDK_SHIFT_MASK | GDK_CONTROL_MASK)) !=
		GDK_SHIFT_MASK) mult = 1;
	*dx = *dy = 0;
	switch (event->keyval)
	{
		case GDK_KP_Left: case GDK_Left:
			*dx = -mult; break;
		case GDK_KP_Right: case GDK_Right:
			*dx = mult; break;
		case GDK_KP_Up: case GDK_Up:
			*dy = -mult; break;
		case GDK_KP_Down: case GDK_Down:
			*dy = mult; break;
	}
	return (*dx || *dy);
}

// Create pixmap cursor

GdkCursor *make_cursor(const char *icon, const char *mask, int w, int h,
	int tip_x, int tip_y)
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

// Menu-like combo box

/* Use GtkComboBox when available */
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION >= 4) /* GTK+ 2.4+ */

static void wj_combo(GtkComboBox *widget, gpointer user_data)
{
	int i = gtk_combo_box_get_active(widget);
	if (i >= 0) *(int *)user_data = i;
}

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
GtkWidget *wj_combo_box(char **names, int cnt, int idx, gpointer var,
	GtkSignalFunc handler)
{
	GtkWidget *cbox;
	GtkComboBox *combo;
	int i;


	if (idx >= cnt) idx = 0;
	if (!handler && var)
	{
		*(int *)var = idx;
		handler = GTK_SIGNAL_FUNC(wj_combo);
	}
	combo = GTK_COMBO_BOX(cbox = gtk_combo_box_new_text());
	wj_combo_restyle(cbox);
	for (i = 0; i < cnt; i++) gtk_combo_box_append_text(combo, names[i]);
	gtk_combo_box_set_active(combo, idx);
	if (handler) gtk_signal_connect(GTK_OBJECT(cbox), "changed",
		handler, var);

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
	if (handler) ((void (*)(GtkWidget *, gpointer))handler)(combo, user_data);
	else
	{			
		int i = wj_combo_box_get_history(combo);
		if (i >= 0) *(int *)user_data = i;
	}
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
GtkWidget *wj_combo_box(char **names, int cnt, int idx, gpointer var,
	GtkSignalFunc handler)
{
	GtkWidget *cbox;
	GtkCombo *combo;
	GtkEntry *entry;
	GList *list = NULL;
	int i;


	if (idx >= cnt) idx = 0;
	if (!handler && var) *(int *)var = idx;
	combo = GTK_COMBO(cbox = gtk_combo_new());
#if GTK_MAJOR_VERSION == 2
	wj_combo_restyle(combo->entry);
	gtk_signal_connect(GTK_OBJECT(combo->entry), "expose_event",
		GTK_SIGNAL_FUNC(wj_combo_kill_cursor), NULL);
#endif
	gtk_combo_set_value_in_list(combo, TRUE, FALSE);
	for (i = 0; i < cnt; i++) list = g_list_append(list, names[i]);
	gtk_combo_set_popdown_strings(combo, list);
	g_list_free(list);
	gtk_widget_show_all(cbox);
	entry = GTK_ENTRY(combo->entry);
	gtk_entry_set_editable(entry, FALSE);
	gtk_entry_set_text(entry, names[idx]);
	if (!handler && !var) return (cbox);

	/* Install signal handler */
	gtk_object_set_user_data(GTK_OBJECT(combo->entry), var);
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

// Bin widget with customizable size handling

/* There exist no way to override builtin handlers for GTK_RUN_FIRST signals,
 * such as size-request and size-allocate; so instead of building multiple
 * custom widget classes with different resize handling, it's better to build
 * one with no builtin sizing at all - WJ */

GtkWidget *wj_size_bin()
{
	static GtkType size_bin_type;
	GtkWidget *widget;

	if (!size_bin_type)
	{
		static const GtkTypeInfo info = {
			"WJSizeBin", sizeof(GtkBin),
			sizeof(GtkBinClass), NULL /* class init */,
			NULL /* instance init */, NULL, NULL, NULL };
		GtkWidgetClass *wc;
		size_bin_type = gtk_type_unique(GTK_TYPE_BIN, &info);
		wc = gtk_type_class(size_bin_type);
		wc->size_request = NULL;
		wc->size_allocate = NULL;
	}
	widget = gtk_widget_new(size_bin_type, NULL);
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

#else /* #if GTK_MAJOR_VERSION == 2 */

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

/* While GTK+2 allows for synchronous clipboard handling, it's implemented
 * through copying the entire clipboard data - and when the data might be
 * a huge image which mtPaint is bound to allocate *yet again*, this would be
 * asking for trouble - WJ */

typedef int (*clip_function)(GtkSelectionData *data, gpointer user_data);

typedef struct {
	int flag;
	clip_function handler;
	gpointer data;
} clip_info;

#if GTK_MAJOR_VERSION == 1

static void selection_callback(GtkWidget *widget, GtkSelectionData *data,
	guint time, clip_info *res)
{
	res->flag = (data->length >= 0) && res->handler(data, res->data);
}

int process_clipboard(int which, char *what, GtkSignalFunc handler, gpointer data)
{
	clip_info res = { -1, (clip_function)handler, data };

	gtk_signal_connect(GTK_OBJECT(main_window), "selection_received",
		GTK_SIGNAL_FUNC(selection_callback), &res);
	gtk_selection_convert(main_window,
		gdk_atom_intern(which ? "PRIMARY" : "CLIPBOARD", FALSE),
		gdk_atom_intern(what, FALSE), GDK_CURRENT_TIME);
	while (res.flag == -1) gtk_main_iteration();
	gtk_signal_disconnect_by_func(GTK_OBJECT(main_window), 
		GTK_SIGNAL_FUNC(selection_callback), &res);
	return (res.flag);
}

static void selection_get_callback(GtkWidget *widget, GtkSelectionData *data,
	guint info, guint time, gpointer user_data)
{
	clip_function cf = g_dataset_get_data(main_window,
		gdk_atom_name(data->selection));
	if (cf) cf(data, (gpointer)(int)info);
}

static void selection_clear_callback(GtkWidget *widget, GdkEventSelection *event,
	gpointer user_data)
{
	clip_function cf = g_dataset_get_data(main_window,
		gdk_atom_name(event->selection));
	if (cf) cf(NULL, (gpointer)0);
}

// !!! GTK+ 1.2 internal type (gtk/gtkselection.c)
typedef struct {
	GdkAtom selection;
	GtkTargetList *list;
} GtkSelectionTargetList;

int offer_clipboard(int which, GtkTargetEntry *targets, int ntargets,
	GtkSignalFunc handler)
{
	static int connected;
	GtkSelectionTargetList *slist;
	GList *list, *tmp;
	GdkAtom sel = gdk_atom_intern(which ? "PRIMARY" : "CLIPBOARD", FALSE);

	if (!gtk_selection_owner_set(main_window, sel, GDK_CURRENT_TIME))
		return (FALSE);

	/* Don't have gtk_selection_clear_targets() in GTK+1 - have to
	 * reimplement */
	list = gtk_object_get_data(GTK_OBJECT(main_window), "gtk-selection-handlers");
	for (tmp = list; tmp; tmp = tmp->next)
	{
		if ((slist = tmp->data)->selection != sel) continue;
		list = g_list_remove_link(list, tmp);
		gtk_target_list_unref(slist->list);
		g_free(slist);
		break;
	}
	gtk_object_set_data(GTK_OBJECT(main_window), "gtk-selection-handlers", list);

	/* !!! Have to resort to this to allow for multiple clipboards in X */
	g_dataset_set_data(main_window, which ? "PRIMARY" : "CLIPBOARD",
		(gpointer)handler);
	if (!connected)
	{
		gtk_signal_connect(GTK_OBJECT(main_window), "selection_get",
			GTK_SIGNAL_FUNC(selection_get_callback), NULL);
		gtk_signal_connect(GTK_OBJECT(main_window), "selection_clear_event",
			GTK_SIGNAL_FUNC(selection_clear_callback), NULL);
		connected = TRUE;
	}

	gtk_selection_add_targets(main_window, sel, targets, ntargets);
	return (TRUE);
}

#else /* #if GTK_MAJOR_VERSION == 2 */

static void clipboard_callback(GtkClipboard *clipboard,
	GtkSelectionData *selection_data, gpointer data)
{
	clip_info *res = data;
	res->flag = (selection_data->length >= 0) &&
		res->handler(selection_data, res->data);
}

int process_clipboard(int which, char *what, GtkSignalFunc handler, gpointer data)
{
	clip_info res = { -1, (clip_function)handler, data };

	gtk_clipboard_request_contents(gtk_clipboard_get(which ?
		GDK_SELECTION_PRIMARY : GDK_SELECTION_CLIPBOARD),
		gdk_atom_intern(what, FALSE), clipboard_callback, &res);
	while (res.flag == -1) gtk_main_iteration();
	return (res.flag);
}

static void clipboard_get_callback(GtkClipboard *clipboard,
	GtkSelectionData *selection_data, guint info, gpointer user_data)
{
	((clip_function)user_data)(selection_data, (gpointer)(int)info);
}

static void clipboard_clear_callback(GtkClipboard *clipboard, gpointer user_data)
{
	((clip_function)user_data)(NULL, (gpointer)0);
}

int offer_clipboard(int which, GtkTargetEntry *targets, int ntargets,
	GtkSignalFunc handler)
{
	int i;

	/* Two attempts, for GTK+2 function can fail for strange reasons */
	for (i = 0; i < 2; i++)
	{
		if (gtk_clipboard_set_with_data(gtk_clipboard_get(which ?
			GDK_SELECTION_PRIMARY : GDK_SELECTION_CLIPBOARD),
			targets, ntargets, clipboard_get_callback,
			clipboard_clear_callback, (gpointer)handler))
			return (TRUE);
	}
	return (FALSE);
}

#endif

// Allocate a memory chunk which is freed along with a given widget

void *bound_malloc(GtkWidget *widget, int size)
{
	void *mem = g_malloc0(size);
	if (mem) gtk_signal_connect_object(GTK_OBJECT(widget), "destroy",
		GTK_SIGNAL_FUNC(g_free), (gpointer)mem);
	return (mem);
}

// Gamma correction toggle

GtkWidget *gamma_toggle()
{
	return (sig_toggle(_("Gamma corrected"),
		inifile_get_gboolean("defaultGamma", FALSE), NULL, NULL));
}

// Render stock icons to pixmaps

#if GTK_MAJOR_VERSION == 2

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
GtkWidget *xpm_image(XPM_TYPE xpm)
{
	GdkPixmap *icon, *mask;
	GtkWidget *widget;
#if GTK_MAJOR_VERSION == 2
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
	/* Fall back to builtin XPM icon */
	icon = gdk_pixmap_create_from_xpm_d(main_window->window, &mask, NULL,
		(char **)xpm[1]);
#else /* if GTK_MAJOR_VERSION == 1 */
	icon = gdk_pixmap_create_from_xpm_d(main_window->window, &mask, NULL, xpm);
#endif
	widget = gtk_pixmap_new(icon, mask);
	gdk_pixmap_unref(icon);
	gdk_pixmap_unref(mask);
	gtk_widget_show(widget);

	return (widget);
}

// Release outstanding pointer grabs

int release_grab()
{
	GtkWidget *grab = gtk_grab_get_current();
	int res = gdk_pointer_is_grabbed();

	if (grab) gtk_grab_remove(grab);
	if (res) gdk_pointer_ungrab(GDK_CURRENT_TIME);

	return (res || grab);
}

// Frame widget with passthrough scrolling

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

GtkWidget *wjframe_new()
{
	return (gtk_widget_new(wjframe_get_type(), NULL));
}

void add_with_wjframe(GtkWidget *bin, GtkWidget *widget)
{
	GtkWidget *frame = wjframe_new();

	gtk_widget_show(frame);
	gtk_container_add(GTK_CONTAINER(frame), widget);
	gtk_container_add(GTK_CONTAINER(bin), frame);
}

// Scrollable canvas widget

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
#if (GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION >= 2) /* GTK+ 2.2+ */
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

GtkWidget *wjcanvas_new()
{
	return (gtk_widget_new(wjcanvas_get_type(), NULL));
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

void wjcanvas_get_vport(GtkWidget *widget, int *vport)
{
	copy4(vport, WJCANVAS(widget)->xy);
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

// Focusable pixmap widget

#define WJPIXMAP(obj)		GTK_CHECK_CAST(obj, wjpixmap_get_type(), wjpixmap)
#define IS_WJPIXMAP(obj)	GTK_CHECK_TYPE(obj, wjpixmap_get_type())

// !!! Implement 2nd cursor later !!!

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

void wjpixmap_fill_rgb(GtkWidget *widget, int x, int y, int w, int h, int rgb)
{
	GdkGCValues sv;
	wjpixmap *pix = WJPIXMAP(widget);

	if (!pix->pixmap) return;
	gdk_gc_get_values(widget->style->black_gc, &sv);
	gdk_rgb_gc_set_foreground(widget->style->black_gc, rgb);
	gdk_draw_rectangle(pix->pixmap, widget->style->black_gc, TRUE, x, y, w, h);
	gdk_gc_set_foreground(widget->style->black_gc, &sv.foreground);
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

void wjpixmap_cursor(GtkWidget *widget, int *x, int *y)
{
	wjpixmap *pix = WJPIXMAP(widget);
	if (pix->cursor) *x = pix->xc , *y = pix->yc;
	else *x = *y = 0;
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

// Repaint expose region

#if GTK_MAJOR_VERSION == 2

// !!! For now, repaint_func() is expected to know widget & window to repaint
void repaint_expose(GdkEventExpose *event, int *vport, repaint_func repaint, int cost)
{
	GdkRectangle *rects;
	gint nrects;
	int i, sz, dx = vport[0], dy = vport[1];
 
	gdk_region_get_rectangles(event->region, &rects, &nrects);
	for (i = sz = 0; i < nrects; i++)
		sz += rects[i].width * rects[i].height;

	/* Don't bother with regions if not worth it */
	if (event->area.width * event->area.height - sz > cost * (nrects - 1))
	{
		for (i = 0; i < nrects; i++)
			repaint(rects[i].x + dx, rects[i].y + dy,
				rects[i].width, rects[i].height);
	}
	else repaint(event->area.x + dx, event->area.y + dy,
		event->area.width, event->area.height);
	g_free(rects);
}

#endif

// Track updates of multiple widgets (by whatever means necessary)

/* void handler(GtkWidget *widget) */
void track_updates(GtkSignalFunc handler, GtkWidget *widget, ...)
{
	va_list args;
	GtkWidget *w;

	va_start(args, widget);
	for (w = widget; w; w = va_arg(args, GtkWidget *))
	{
		if (GTK_IS_SPIN_BUTTON(w))
		{
			GtkAdjustment *adj = gtk_spin_button_get_adjustment(
				GTK_SPIN_BUTTON(w));
			gtk_signal_connect_object(GTK_OBJECT(adj),
				"value_changed", handler, GTK_OBJECT(w));
		}
		else if (GTK_IS_TOGGLE_BUTTON(w)) gtk_signal_connect(
			GTK_OBJECT(w), "toggled", handler, NULL);
		else if (GTK_IS_ENTRY(w)) gtk_signal_connect(
			GTK_OBJECT(w), "changed", handler, NULL);
// !!! Add other widget types here
	}
	va_end(args);
}

// Convert pathname to absolute

char *resolve_path(char *buf, int buflen, char *path)
{
	char wbuf[PATHBUF], *tmp, *tm2, *src, *dest;
	int ch, dot, dots;


	/* Relative name to absolute */
#ifdef WIN32
	tm2 = "\\";
	getcwd(wbuf, PATHBUF - 1);
	if ((path[0] == '/') || (path[0] == '\\')) wbuf[2] = '\0';
	else if (path[1] != ':');
	else if ((path[2] != '/') && (path[2] != '\\'))
	{
		char tbuf[PATHBUF];

		tbuf[0] = path[0]; tbuf[1] = ':'; tbuf[2] = '\0';
		if (!chdir(tbuf)) getcwd(tbuf, PATHBUF - 1);
		chdir(wbuf);
		memcpy(wbuf, tbuf, PATHBUF);
		path += 2;
	}
	else *(tm2 = wbuf) = '\0';
	tmp = g_strdup_printf("%s%s%s", wbuf, tm2, path);
#else
	wbuf[0] = '\0';
	if (path[0] != '/') getcwd(wbuf, PATHBUF - 1);
	tmp = g_strdup_printf("%s%c%s", wbuf, DIR_SEP, path);
#endif

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
		g_free(tmp);
		tmp = buf;
	}
	return (tmp);
}

// A (better) substitute for fnmatch(), in case one is needed

/* One is necessary in case of Win32 or GTK+ 2.0/2.2 */
#if defined(WIN32) || ((GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION < 4))

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

// Prod the focused spinbutton, if any, to finally update its value

void update_window_spin(GtkWidget *window)
{
#if GTK_MAJOR_VERSION == 1
	gtk_container_focus(GTK_CONTAINER(window), GTK_DIR_TAB_FORWARD);
#else
	gtk_widget_child_focus(window, GTK_DIR_TAB_FORWARD);
#endif
}

// Process event queue

void handle_events()
{
	while (gtk_events_pending()) gtk_main_iteration();
}

// Make GtkEntry accept Ctrl+Enter as a character

static gboolean convert_ctrl_enter(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	if (((event->keyval == GDK_Return) || (event->keyval == GDK_KP_Enter)) &&
		(event->state & GDK_CONTROL_MASK))
	{
#if GTK_MAJOR_VERSION == 1
		GtkEditable *edit = GTK_EDITABLE(widget);
		gint pos = edit->current_pos;

		gtk_editable_delete_selection(edit);
		gtk_editable_insert_text(edit, "\n", 1, &pos);
		edit->current_pos = pos;
#else
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

// Maybe this will be needed someday...

#if 0
/* This transmutes a widget into one of different type. If the two types aren't 
 * bit-for-bit compatible, Bad Things (tm) will happen - WJ */

static void steal_widget(GtkWidget *widget, GtkType wrapper_type)
{
#if GTK_MAJOR_VERSION == 1
	GTK_OBJECT(widget)->klass = gtk_type_class(wrapper_type);
#else
	G_OBJECT(widget)->g_type_instance.g_class = gtk_type_class(wrapper_type);
#endif
}
#endif

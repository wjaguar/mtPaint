/*	mygtk.c
	Copyright (C) 2004-2007 Mark Tyler and Dmitry Groshev

	This file is part of mtPaint.

	mtPaint is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	mtPaint is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with mtPaint in the file COPYING.
*/

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"
#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "mygtk.h"


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
	return (spin_new_x(gtk_adjustment_new(value, min, max, 1, 10, 10), 0));
}

GtkWidget *add_a_table( int rows, int columns, int bord, GtkWidget *box )
{
	GtkWidget *table = gtk_table_new(rows, columns, FALSE);
	gtk_widget_show(table);
	gtk_box_pack_start(GTK_BOX(box), table, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (table), bord);

	return table;
}

GtkWidget *add_a_toggle( char *label, GtkWidget *box, gboolean value )
{
	GtkWidget *tog;

	tog = sig_toggle(label, value, NULL, NULL);
	gtk_box_pack_start(GTK_BOX(box), tog, FALSE, FALSE, 0);

	return tog;
}

GtkWidget *add_to_table(char *text, GtkWidget *table, int row, int column, int spacing)
{
	GtkWidget *label;

	label = gtk_label_new ( text );
	gtk_widget_show (label);
	gtk_table_attach (GTK_TABLE (table), label, column, column+1, row, row+1,
		(GtkAttachOptions) (GTK_FILL),
		(GtkAttachOptions) (0), spacing, spacing);
	gtk_label_set_justify(GTK_LABEL (label), GTK_JUSTIFY_LEFT);
	gtk_misc_set_alignment(GTK_MISC (label), 0.0, 0.5);

	return label;
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

void add_hseparator( GtkWidget *widget, int xs, int ys )
{
	GtkWidget *sep = gtk_hseparator_new ();
	gtk_widget_show (sep);
	gtk_box_pack_start (GTK_BOX (widget), sep, FALSE, FALSE, 0);
	gtk_widget_set_usize (sep, xs, ys);
}



////	PROGRESS WINDOW

GtkWidget *progress_window = NULL, *progress_bar;
int prog_stop;

static gint do_cancel_progress(GtkWidget *widget, GdkEvent *event, gpointer data)
{
	prog_stop = 1;

	return FALSE;
}

static gint delete_progress(GtkWidget *widget, GdkEvent *event, gpointer data)
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

	vbox6 = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vbox6);
	gtk_container_add(GTK_CONTAINER(viewport), vbox6);

	progress_bar = gtk_progress_bar_new ();
	gtk_box_pack_start( GTK_BOX (vbox6), progress_bar, FALSE, FALSE, 0 );
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

	while (gtk_events_pending()) gtk_main_iteration();

	return prog_stop;
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

gint alert_result = 0;

gint alert_reply( GtkWidget *widget, gpointer data )
{
	if ( alert_result == 0 ) alert_result = (gint) data;
	if ( alert_result == 10 ) gtk_widget_destroy( widget );
	
	return FALSE;
}

int alert_box( char *title, char *message, char *text1, char *text2, char *text3 )
{
	GtkWidget *alert, *buttons[3], *label;
	char *butxt[3] = {text1, text2, text3};
	gint i;
	GtkAccelGroup* ag = gtk_accel_group_new();

	alert = gtk_dialog_new();
	gtk_window_set_title( GTK_WINDOW(alert), title );
	gtk_window_set_modal( GTK_WINDOW(alert), TRUE );
	gtk_window_set_position( GTK_WINDOW(alert), GTK_WIN_POS_CENTER );
	gtk_container_set_border_width( GTK_CONTAINER(alert), 6 );
	gtk_signal_connect( GTK_OBJECT(alert), "destroy",
			GTK_SIGNAL_FUNC(alert_reply), (gpointer) 10 );
	
	label = gtk_label_new( message );
	gtk_label_set_line_wrap( GTK_LABEL(label), TRUE );
	gtk_box_pack_start( GTK_BOX(GTK_DIALOG(alert)->vbox), label, TRUE, FALSE, 8 );
	gtk_widget_show( label );

	for ( i=0; i<=2; i++ )
	{
		if ( butxt[i] )
		{
			buttons[i] = add_a_button( butxt[i], 2, GTK_DIALOG(alert)->action_area, TRUE );
			gtk_signal_connect( GTK_OBJECT(buttons[i]), "clicked",
				GTK_SIGNAL_FUNC(alert_reply), (gpointer) (i+1) );
		}
	}
	gtk_widget_add_accelerator (buttons[0], "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_window_set_transient_for( GTK_WINDOW(alert), GTK_WINDOW(main_window) );
	gtk_widget_show(alert);
	alert_result = 0;
	gtk_window_add_accel_group(GTK_WINDOW(alert), ag);

	while ( alert_result == 0 ) gtk_main_iteration();
	if ( alert_result != 10 ) gtk_widget_destroy( alert );

	return alert_result;
}

// Slider-spin combo (practically a new widget class)

GtkWidget *mt_spinslide_new(gint swidth, gint sheight)
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

void mt_spinslide_set_range(GtkWidget *spinslide, gint minv, gint maxv)
{
	GtkAdjustment *adj;
	
	adj = gtk_range_get_adjustment(GTK_RANGE(((GtkBoxChild*)
		GTK_BOX(spinslide)->children->data)->widget));
	adj->lower = minv;
	adj->upper = maxv;
	gtk_adjustment_changed(adj);
}

gint mt_spinslide_get_value(GtkWidget *spinslide)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON(((GtkBoxChild*)GTK_BOX(spinslide)->children->
		next->data)->widget);
	gtk_spin_button_update(spin);
	return (gtk_spin_button_get_value_as_int(spin));
}

/* Different in that this doesn't force slider to integer-value position */
gint mt_spinslide_read_value(GtkWidget *spinslide)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON(((GtkBoxChild*)GTK_BOX(spinslide)->children->
		next->data)->widget);
	return (gtk_spin_button_get_value_as_int(spin));
}

void mt_spinslide_set_value(GtkWidget *spinslide, gint value)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON(((GtkBoxChild*)GTK_BOX(spinslide)->children->
		next->data)->widget);
	gtk_spin_button_set_value(spin, value);
}

/* void handler(GtkAdjustment *adjustment, gpointer user_data); */
void mt_spinslide_connect(GtkWidget *spinslide, GtkSignalFunc handler,
	gpointer user_data)
{
	GtkAdjustment *adj;
	
	adj = gtk_range_get_adjustment(GTK_RANGE(((GtkBoxChild*)
		GTK_BOX(spinslide)->children->data)->widget));
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed", handler, user_data);
}

// Managing batches of radio buttons with minimum of fuss

static void wj_radio_toggle(GtkWidget *btn, gpointer idx)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) return;
	*(int *)gtk_object_get_user_data(GTK_OBJECT(btn->parent)) = (int)idx;
}

/* void handler(GtkWidget *btn, gpointer user_data); */
GtkWidget *wj_radio_pack(char **names, int cnt, int vnum, int idx, int *var,
	GtkSignalFunc handler)
{
	int i, j;
	GtkWidget *box, *wbox, *button = NULL;

	if (!handler) handler = GTK_SIGNAL_FUNC(wj_radio_toggle);
	box = wbox = vnum > 0 ? gtk_hbox_new(FALSE, 0) : gtk_vbox_new(FALSE, 0);
	if (vnum < 2) gtk_object_set_user_data(GTK_OBJECT(wbox), var);

	for (i = j = 0; (i != cnt) && names[i]; i++)
	{
		if (!names[i][0]) continue;
		button = gtk_radio_button_new_with_label_from_widget(
			GTK_RADIO_BUTTON_0(button), names[i]);
		if ((vnum > 1) && !(j % vnum))
		{
			wbox = gtk_vbox_new(FALSE, 0);
			gtk_box_pack_start(GTK_BOX(box), wbox, TRUE, TRUE, 0);
			gtk_object_set_user_data(GTK_OBJECT(wbox), var);
		}
		gtk_container_set_border_width(GTK_CONTAINER(button), 5);
		gtk_box_pack_start(GTK_BOX(wbox), button, FALSE, TRUE, 0);
		if (i == idx) gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(button), TRUE);
		gtk_signal_connect(GTK_OBJECT(button), "toggled", handler,
			(gpointer)(i));
		j++;
	}
	gtk_widget_show_all(box);

	if (var) *var = idx < i ? idx : 0;
	return (box);
}

// Buttons for standard dialogs

GtkWidget *OK_box(int border, GtkWidget *window, char *nOK, GtkSignalFunc OK,
	char *nCancel, GtkSignalFunc Cancel)
{
	GtkWidget *hbox, *button;
	GtkAccelGroup* ag = gtk_accel_group_new();

	hbox = gtk_hbox_new(TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), border);
	button = gtk_button_new_with_label(nCancel);
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(button), 5);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
		Cancel, GTK_OBJECT(window));
	gtk_widget_add_accelerator(button, "clicked", ag, GDK_Escape, 0,
		(GtkAccelFlags)0);

	button = gtk_button_new_with_label(nOK);
	gtk_box_pack_start(GTK_BOX(hbox), button, TRUE, TRUE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(button), 5);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
		OK, GTK_OBJECT(window));
	gtk_widget_add_accelerator(button, "clicked", ag, GDK_Return, 0,
		(GtkAccelFlags)0);
	gtk_widget_add_accelerator(button, "clicked", ag, GDK_KP_Enter, 0,
		(GtkAccelFlags)0);
 
	gtk_window_add_accel_group(GTK_WINDOW(window), ag);
	gtk_signal_connect(GTK_OBJECT(window), "delete_event", Cancel, NULL);
	gtk_object_set_user_data(GTK_OBJECT(hbox), (gpointer)window);
	gtk_widget_show_all(hbox);
	return (hbox);
}

GtkWidget *OK_box_add(GtkWidget *box, char *name, GtkSignalFunc Handler, int idx)
{
	GtkWidget *button;

	button = gtk_button_new_with_label(name);
	gtk_box_pack_start(GTK_BOX(box), button, TRUE, TRUE, 0);
	gtk_box_reorder_child(GTK_BOX(box), button, idx);
	gtk_container_set_border_width(GTK_CONTAINER(button), 5);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
		Handler, GTK_OBJECT(gtk_object_get_user_data(GTK_OBJECT(box))));
	gtk_widget_show(button);
	return (button);
}

// Easier way with spinbuttons

int read_spin(GtkWidget *spin)
{
	/* Needed in GTK+2 for late changes */
	gtk_spin_button_update(GTK_SPIN_BUTTON(spin));
	return (gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(spin)));
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
	return (spin_new_x(gtk_adjustment_new(value, min, max, 1, 10, 10), 2));
}

/* void handler(GtkAdjustment *adjustment, gpointer user_data); */
void spin_connect(GtkWidget *spin, GtkSignalFunc handler, gpointer user_data)
{
	GtkAdjustment *adj;
	
	adj = gtk_spin_button_get_adjustment(GTK_SPIN_BUTTON(spin));
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed", handler, user_data);
}

// Wrapper for utf8->C translation

char *gtkncpy(char *dest, const char *src, int cnt)
{
#if GTK_MAJOR_VERSION == 2
	char *c = g_locale_from_utf8((gchar *)src, -1, NULL, NULL, NULL);
	if (c)
	{
		strncpy(dest, c, cnt);
		g_free(c);
	}
	else
#endif
	strncpy(dest, src, cnt);
	return (dest);
}

// Wrapper for C->utf8 translation

char *gtkuncpy(char *dest, const char *src, int cnt)
{
#if GTK_MAJOR_VERSION == 2
	char *c = g_locale_to_utf8((gchar *)src, -1, NULL, NULL, NULL);
	if (c)
	{
		strncpy(dest, c, cnt);
		g_free(c);
	}
	else
#endif
	strncpy(dest, src, cnt);
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
	gtk_box_pack_start(GTK_BOX(box), frame, !!expand, !!expand, 0);
	gtk_container_set_border_width(GTK_CONTAINER(frame), border);
	gtk_container_add(GTK_CONTAINER(frame), widget);
	return (frame);
}

GtkWidget *add_with_frame(GtkWidget *box, char *text, GtkWidget *widget, int border)
{
	return (add_with_frame_x(box, text, widget, border, FALSE));
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
	int i;
	GtkWidget *opt, *menu, *item;

	if (!handler && var)
	{
		*(int *)var = idx < cnt ? idx : 0;
		handler = GTK_SIGNAL_FUNC(wj_option);
	}
	opt = gtk_option_menu_new();
	menu = gtk_menu_new();
	for (i = 0; i < cnt; i++)
	{
		item = gtk_menu_item_new_with_label(names[i]);
		gtk_object_set_user_data(GTK_OBJECT(item), (gpointer)i);
		if (handler) gtk_signal_connect(GTK_OBJECT(item), "activate",
			handler, var);
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
  	}
	gtk_widget_show_all(menu);
	gtk_widget_show(opt); /* !!! Show now - or size won't be set properly */
	gtk_option_menu_set_menu(GTK_OPTION_MENU(opt), menu);
	gtk_option_menu_set_history(GTK_OPTION_MENU(opt), idx);

	FIX_OPTION_MENU_SIZE(opt);

	return (opt);
}

int wj_option_menu_get_history(GtkWidget *optmenu)
{
	optmenu = gtk_option_menu_get_menu(GTK_OPTION_MENU(optmenu));
	optmenu = gtk_menu_get_active(GTK_MENU(optmenu));
	return ((int)gtk_object_get_user_data(GTK_OBJECT(optmenu)));
}

// Pixmap-backed drawing area

static gboolean wj_pixmap_configure(GtkWidget *widget, GdkEventConfigure *event,
	gpointer user_data)
{
	GdkPixmap *pix, *oldpix = gtk_object_get_user_data(GTK_OBJECT(widget));
	GdkGC *gc;
	gint oldw, oldh;

	if (oldpix)
	{
		gdk_window_get_size(oldpix, &oldw, &oldh);
		if ((oldw == widget->allocation.width) &&
			(oldh == widget->allocation.height)) return (TRUE);
	}

	pix = gdk_pixmap_new(widget->window, widget->allocation.width,
		widget->allocation.height, -1);
	if (oldpix)
	{
		gc = gdk_gc_new(pix);
		gdk_draw_pixmap(pix, gc, oldpix, 0, 0, 0, 0, -1, -1);
		gdk_gc_destroy(gc);
		gdk_pixmap_unref(oldpix);
	}
	gtk_object_set_user_data(GTK_OBJECT(widget), (gpointer)pix);

	if (oldpix && (oldw >= widget->allocation.width) &&
		(oldh >= widget->allocation.height)) return (TRUE);

	return (FALSE); /* NOW call user configure handler to [re]draw things */
}

static gboolean wj_pixmap_expose(GtkWidget *widget, GdkEventExpose *event,
	gpointer user_data)
{
	GdkPixmap *pix = gtk_object_get_user_data(GTK_OBJECT(widget));

	if (!pix) return (TRUE);
	gdk_draw_pixmap(widget->window, widget->style->black_gc, pix,
		event->area.x, event->area.y, event->area.x, event->area.y,
		event->area.width, event->area.height);
	return (TRUE);
}

static void wj_pixmap_destroy(GtkObject *object, gpointer user_data)
{
	GdkPixmap *pix = gtk_object_get_user_data(object);
	if (pix) gdk_pixmap_unref(pix);
}

GtkWidget *wj_pixmap(int width, int height)
{
	GtkWidget *area = gtk_drawing_area_new();
	gtk_widget_set_events(area, GDK_ALL_EVENTS_MASK);
	gtk_widget_set_usize(area, width, height);
	gtk_widget_show(area);
	gtk_signal_connect(GTK_OBJECT(area), "configure_event",
		GTK_SIGNAL_FUNC(wj_pixmap_configure), NULL);
	gtk_signal_connect(GTK_OBJECT(area), "expose_event",
		GTK_SIGNAL_FUNC(wj_pixmap_expose), NULL);
	gtk_signal_connect(GTK_OBJECT(area), "destroy",
		GTK_SIGNAL_FUNC(wj_pixmap_destroy), NULL);
	return (area);
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

// Signalled toggle

static void sig_toggle_toggled(GtkToggleButton *togglebutton, gpointer user_data)
{
	*(int *)user_data = gtk_toggle_button_get_active(togglebutton);
}

GtkWidget *sig_toggle(char *label, int value, int *var, GtkSignalFunc handler)
{
	GtkWidget *tog;

	if (!handler && var)
	{
		*(int *)var = value;
		handler = GTK_SIGNAL_FUNC(sig_toggle_toggled);
	}

	tog = gtk_check_button_new_with_label(label);
	gtk_widget_show(tog);
	gtk_container_set_border_width(GTK_CONTAINER(tog), 5);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(tog), value);
	if (handler) gtk_signal_connect(GTK_OBJECT(tog), "toggled", handler,
		(gpointer)var);

	return (tog);
}


// Whatever is needed to move mouse pointer 

#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11 /* Call X */

#include <X11/Xlib.h>
#include <gdk/gdkx.h>

int move_mouse_relative(int dx, int dy)
{
	XWarpPointer(GDK_WINDOW_XDISPLAY(drawing_canvas->window),
		None, None, 0, 0, 0, 0, dx, dy);
	return (TRUE);
}

#elif defined GDK_WINDOWING_WIN32 /* Call GDI */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

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

guint keyval_key(guint keyval)
{
	GdkDisplay *display = gtk_widget_get_display(drawing_canvas);
	GdkKeymap *keymap = gdk_keymap_get_for_display(display);
	GdkKeymapKey *key;
	gint nkeys;

	if (!gdk_keymap_get_entries_for_keyval(keymap, keyval, &key, &nkeys))
	{
#ifdef GDK_WINDOWING_WIN32
		/* Keypad numbers need specialcasing on Windows */
		if ((keyval >= GDK_KP_0) && (keyval <= GDK_KP_9))
			return(keyval - GDK_KP_0 + VK_NUMPAD0);
#endif
		return (0);
	}
	if (!nkeys) return (0);
	keyval = key[0].keycode;
	g_free(key);
	return (keyval);
}

#endif

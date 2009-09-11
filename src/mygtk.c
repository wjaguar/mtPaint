/*	mygtk.c
	Copyright (C) 2004-2007 Mark Tyler and Dmitry Groshev

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

#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
#include <X11/Xlib.h>
#include <gdk/gdkx.h>
#elif defined GDK_WINDOWING_WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
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
	return (spin_new_x(gtk_adjustment_new(value, min, max, 1, 10, 10), 0));
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
	GtkWidget *sep = pack(widget, gtk_hseparator_new());
	gtk_widget_show(sep);
	gtk_widget_set_usize(sep, xs, ys);
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

static int alert_result;

static gboolean alert_reply(GtkWidget *widget, gpointer data)
{
	if ( alert_result == 0 ) alert_result = (int)data;
	if ( alert_result == 10 ) gtk_widget_destroy(widget);
	
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
		if (!butxt[i]) continue;
		buttons[i] = add_a_button(butxt[i], 2, GTK_DIALOG(alert)->action_area, TRUE);
		gtk_signal_connect(GTK_OBJECT(buttons[i]), "clicked",
			GTK_SIGNAL_FUNC(alert_reply), (gpointer)(i + 1));
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
	
	adj = gtk_range_get_adjustment(GTK_RANGE(BOX_CHILD_0(spinslide)));
	adj->lower = minv;
	adj->upper = maxv;
	gtk_adjustment_changed(adj);
}

gint mt_spinslide_get_value(GtkWidget *spinslide)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON(BOX_CHILD_1(spinslide));
	gtk_spin_button_update(spin);
	return (gtk_spin_button_get_value_as_int(spin));
}

/* Different in that this doesn't force slider to integer-value position */
gint mt_spinslide_read_value(GtkWidget *spinslide)
{
	GtkSpinButton *spin;

	spin = GTK_SPIN_BUTTON(BOX_CHILD_1(spinslide));
	return (gtk_spin_button_get_value_as_int(spin));
}

void mt_spinslide_set_value(GtkWidget *spinslide, gint value)
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
			wbox = xpack(box, gtk_vbox_new(FALSE, 0));
			gtk_object_set_user_data(GTK_OBJECT(wbox), var);
		}
		gtk_container_set_border_width(GTK_CONTAINER(button), 5);
		gtk_box_pack_start(GTK_BOX(wbox), button, FALSE, FALSE, 0);
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
	button = xpack(hbox, gtk_button_new_with_label(nCancel));
	gtk_container_set_border_width(GTK_CONTAINER(button), 5);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
		Cancel, GTK_OBJECT(window));
	gtk_widget_add_accelerator(button, "clicked", ag, GDK_Escape, 0,
		(GtkAccelFlags)0);

	button = xpack(hbox, gtk_button_new_with_label(nOK));
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

	button = xpack(box, gtk_button_new_with_label(name));
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
	GtkWidget *fs;

	fs = gtk_file_selection_new((char *)gtk_object_get_user_data(
		GTK_OBJECT(widget)));
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

	add_with_frame(box, name, hbox, 5);
	entry = gtk_entry_new();
	gtk_widget_show(entry);
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 5);
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

// Properly destroy transient window

void destroy_dialog(GtkWidget *window)
{
	/* Needed in Windows to stop GTK+ lowering the main window */
	gtk_window_set_transient_for(GTK_WINDOW(window), NULL);
	gtk_widget_destroy(window);
}

// Settings notebook

static void toggle_book(GtkToggleButton *button, GtkNotebook *book)
{
	int i = gtk_toggle_button_get_active(button);
	gtk_notebook_set_page(book, i ? 1 : 0);
}

GtkWidget *buttoned_book(GtkWidget **page0, GtkWidget **page1,
	GtkWidget **button, char *button_label)
{
	GtkWidget *notebook = gtk_notebook_new();
	gtk_notebook_set_show_tabs(GTK_NOTEBOOK(notebook), FALSE);
	gtk_notebook_set_show_border(GTK_NOTEBOOK(notebook), FALSE);
	*page0 = gtk_vbox_new(FALSE, 0);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), *page0, NULL);
	*page1 = gtk_vbox_new(FALSE, 0);
	gtk_notebook_append_page(GTK_NOTEBOOK(notebook), *page1, NULL);
	gtk_widget_show_all(notebook);
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

// Eliminate flicker when scrolling

/* This code serves a very important role - it disables background clear,
 * completely eliminating the annoying flicker when scrolling the canvas
 * in GTK+1 and GTK+2/Windows, and lessening CPU load in GTK+2/Linux.
 * The trick was discovered by Mark Tyler while developing MTK.
 * It also disables double buffering in GTK+2, because when rendering is
 * done properly, it is useless and just wastes considerable time. - WJ */

static void realize_trick(GtkWidget *widget, gpointer user_data)
{
	gdk_window_set_back_pixmap(widget->window, NULL, FALSE);
}

void fix_scroll(GtkWidget *scroll)
{
	scroll = GTK_BIN(scroll)->child;
	gtk_signal_connect_after(GTK_OBJECT(scroll), "realize",
		GTK_SIGNAL_FUNC(realize_trick), NULL);
#if GTK_MAJOR_VERSION == 2
	gtk_widget_set_double_buffered(scroll, FALSE);
#endif
	scroll = GTK_BIN(scroll)->child;
	gtk_signal_connect_after(GTK_OBJECT(scroll), "realize",
		GTK_SIGNAL_FUNC(realize_trick), NULL);
#if GTK_MAJOR_VERSION == 2
	gtk_widget_set_double_buffered(scroll, FALSE);
#endif
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
 * Engine in GTK+1; mixing up keys in GTK+2/Windows */

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
			*dx = -1; break;
		case GDK_KP_Right: case GDK_Right:
			*dx = 1; break;
		case GDK_KP_Up: case GDK_Up:
			*dy = -1; break;
		case GDK_KP_Down: case GDK_Down:
			*dy = 1; break;
	}
	return (*dx || *dy);
}

// Focusable pixmap widget

#define FPIXMAP_KEY "mtPaint.fpixmap"

typedef struct {
	int xp, yp, width, height, xc, yc;
	int focused_cursor;
	GdkRectangle pm, cr;
	GdkPixmap *pixmap, *cursor;
	GdkBitmap *cmask;
} fpixmap_data;

static guint fpixmap_key;

fpixmap_data *wj_fpixmap_data(GtkWidget *widget)
{
	return (gtk_object_get_data_by_id(GTK_OBJECT(widget), fpixmap_key));
}

static void wj_fpixmap_paint(GtkWidget *widget, GdkRectangle *area)
{
	GdkRectangle pdest, cdest;
	fpixmap_data *d;

	if (!GTK_WIDGET_DRAWABLE(widget)) return;
	if (!(d = wj_fpixmap_data(widget))) return;

#if GTK_MAJOR_VERSION == 1
	/* Preparation */
	gdk_window_set_back_pixmap(widget->window, NULL, TRUE);
	gdk_window_clear_area(widget->window, area->x, area->y,
		area->width, area->height);
	gdk_gc_set_clip_rectangle(widget->style->black_gc, area);
#endif

	/* Frame */
	if (d->pixmap) gdk_draw_rectangle(widget->window, widget->style->black_gc,
		FALSE, d->pm.x - 1, d->pm.y - 1, d->pm.width + 1, d->pm.height + 1);

	while (d->pixmap && gdk_rectangle_intersect(&d->pm, area, &pdest))
	{
		/* Contents pixmap */
		gdk_draw_pixmap(widget->window, widget->style->black_gc,
			d->pixmap, pdest.x - d->pm.x, pdest.y - d->pm.y,
			pdest.x, pdest.y, pdest.width, pdest.height);

		/* Cursor pixmap */
		if (d->focused_cursor && !GTK_WIDGET_HAS_FOCUS(widget)) break;
		if (!d->cursor || !gdk_rectangle_intersect(&d->cr, &pdest, &cdest))
			break;
		if (d->cmask)
		{
			gdk_gc_set_clip_mask(widget->style->black_gc, d->cmask);
			gdk_gc_set_clip_origin(widget->style->black_gc, d->cr.x, d->cr.y);
		}
		gdk_draw_pixmap(widget->window, widget->style->black_gc,
			d->cursor, cdest.x - d->cr.x, cdest.y - d->cr.y,
			cdest.x, cdest.y, cdest.width, cdest.height);
		if (d->cmask)
		{
			gdk_gc_set_clip_mask(widget->style->black_gc, NULL);
			gdk_gc_set_clip_origin(widget->style->black_gc, 0, 0);
		}
		break;
	}

	/* Focus rectangle */
	if (GTK_WIDGET_HAS_FOCUS(widget)) gtk_paint_focus(widget->style, widget->window,
#if GTK_MAJOR_VERSION == 2
		GTK_WIDGET_STATE(widget),
#endif
		area, widget, NULL, 0, 0,
		widget->allocation.width - 1, widget->allocation.height - 1);

#if GTK_MAJOR_VERSION == 1
	/* Cleanup */
	gdk_gc_set_clip_rectangle(widget->style->black_gc, NULL);
#endif
}

#if GTK_MAJOR_VERSION == 1

static void wj_fpixmap_draw_focus(GtkWidget *widget)
{
	gtk_widget_draw(widget, NULL);
}

static gboolean wj_fpixmap_focus(GtkWidget *widget, GdkEventFocus *event)
{
	if (event->in) GTK_WIDGET_SET_FLAGS(widget, GTK_HAS_FOCUS);
	else GTK_WIDGET_UNSET_FLAGS(widget, GTK_HAS_FOCUS);
	gtk_widget_draw_focus(widget);
	return (FALSE);
}

#endif

static gboolean wj_fpixmap_expose(GtkWidget *widget, GdkEventExpose *event)
{
	wj_fpixmap_paint(widget, &event->area);
	return (FALSE);
}

static void wj_fpixmap_size_req(GtkWidget *widget, GtkRequisition *requisition,
	gpointer user_data)
{
	fpixmap_data *d = user_data;
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
	requisition->width = d->width + (d->xp = xp);
	requisition->height = d->height + (d->yp = yp);
}

static void wj_fpixmap_size_alloc(GtkWidget *widget, GtkAllocation *allocation,
	gpointer user_data)
{
	fpixmap_data *d = user_data;
	GdkRectangle opm = d->pm;
	int w, h, x = d->xp, y = d->yp;

	w = allocation->width - d->xp;
	h = allocation->height - d->yp;
	if (w > d->width) x = allocation->width - d->width , w = d->width;
	if (y > d->height) y = allocation->height - d->height , h = d->height;
	x /= 2; y /= 2;
	w = w < 0 ? 0 : w; h = h < 0 ? 0 : h;
	d->pm.x = x; d->pm.y = y;
	d->pm.width = w; d->pm.height = h; 
	d->cr.x += x - opm.x; d->cr.y += y - opm.y;

// !!! Is this needed, or not?
//	if ((opm.x != x) || (opm.y != y) || (opm.width != w) || (opm.height != h))
//		gtk_widget_queue_draw(widget);
}

static void wj_fpixmap_destroy(GtkObject *object, gpointer user_data)
{
	fpixmap_data *d = user_data;
	if (d->pixmap) gdk_pixmap_unref(d->pixmap);
	if (d->cursor) gdk_pixmap_unref(d->cursor);
	if (d->cmask) gdk_bitmap_unref(d->cmask);
	free(d);
}

GtkWidget *wj_fpixmap(int width, int height)
{
	GtkWidget *w;
	fpixmap_data *d;

	if (!fpixmap_key) fpixmap_key = g_quark_from_static_string(FPIXMAP_KEY);
	d = calloc(1, sizeof(fpixmap_data));
	if (!d) return (NULL);
	d->width = width; d->height = height;
	w = gtk_drawing_area_new();
	GTK_WIDGET_SET_FLAGS(w, GTK_CAN_FOCUS);
	gtk_widget_set_events(w, GDK_ALL_EVENTS_MASK);
	gtk_widget_show(w);
	gtk_object_set_data_by_id(GTK_OBJECT(w), fpixmap_key, d);
	gtk_signal_connect(GTK_OBJECT(w), "destroy",
		GTK_SIGNAL_FUNC(wj_fpixmap_destroy), d);
#if GTK_MAJOR_VERSION == 1
	gtk_signal_connect(GTK_OBJECT(w), "draw_focus",
		GTK_SIGNAL_FUNC(wj_fpixmap_draw_focus), NULL);
	gtk_signal_connect(GTK_OBJECT(w), "focus_in_event",
		GTK_SIGNAL_FUNC(wj_fpixmap_focus), NULL);
	gtk_signal_connect(GTK_OBJECT(w), "focus_out_event",
		GTK_SIGNAL_FUNC(wj_fpixmap_focus), NULL);
#endif
	gtk_signal_connect(GTK_OBJECT(w), "expose_event",
		GTK_SIGNAL_FUNC(wj_fpixmap_expose), NULL);
	gtk_signal_connect_after(GTK_OBJECT(w), "size_request",
		GTK_SIGNAL_FUNC(wj_fpixmap_size_req), d);
	gtk_signal_connect(GTK_OBJECT(w), "size_allocate",
		GTK_SIGNAL_FUNC(wj_fpixmap_size_alloc), d);

	return (w);
}

/* Must be called after realize to init, and afterwards to access pixmap */
GdkPixmap *wj_fpixmap_pixmap(GtkWidget *widget)
{
	fpixmap_data *d;

	if (!(d = wj_fpixmap_data(widget))) return (NULL);
	if (!d->pixmap && GTK_WIDGET_REALIZED(widget))
		d->pixmap = gdk_pixmap_new(widget->window, d->width, d->height, -1);
	return (d->pixmap);
}

void wj_fpixmap_draw_rgb(GtkWidget *widget, int x, int y, int w, int h,
	unsigned char *rgb, int step)
{
	fpixmap_data *d;

	if (!(d = wj_fpixmap_data(widget))) return;
	if (!d->pixmap) return;
	gdk_draw_rgb_image(d->pixmap, widget->style->black_gc,
		x, y, w, h, GDK_RGB_DITHER_NONE, rgb, step);
	gtk_widget_queue_draw_area(widget, x + d->pm.x, y + d->pm.y, w, h);
}

void wj_fpixmap_fill_rgb(GtkWidget *widget, int x, int y, int w, int h, int rgb)
{
	GdkGCValues sv;
	fpixmap_data *d;

	if (!(d = wj_fpixmap_data(widget))) return;
	if (!d->pixmap) return;
	gdk_gc_get_values(widget->style->black_gc, &sv);
	gdk_rgb_gc_set_foreground(widget->style->black_gc, rgb);
	gdk_draw_rectangle(widget->window, widget->style->black_gc,
		TRUE, x + d->pm.x, y + d->pm.y, w, h);
	gdk_gc_set_foreground(widget->style->black_gc, &sv.foreground);
	gtk_widget_queue_draw_area(widget, x + d->pm.x, y + d->pm.y, w, h);
}

void wj_fpixmap_move_cursor(GtkWidget *widget, int x, int y)
{
	GdkRectangle ocr, tcr1, tcr2, *rcr = NULL;
	fpixmap_data *d;

	if (!(d = wj_fpixmap_data(widget))) return;
	if ((x == d->xc) && (y == d->yc)) return;
	ocr = d->cr;
	d->cr.x += x - d->xc;
	d->cr.y += y - d->yc;
	d->xc = x; d->yc = y;

	if (!d->pixmap || !d->cursor) return;
	if (d->focused_cursor && !GTK_WIDGET_HAS_FOCUS(widget)) return;

	/* Anything visible? */
	if (!GTK_WIDGET_VISIBLE(widget)) return;
	if (gdk_rectangle_intersect(&ocr, &d->pm, &tcr1)) rcr = &tcr1;
	if (gdk_rectangle_intersect(&d->cr, &d->pm, &tcr2))
	{
		if (rcr) gdk_rectangle_union(&tcr1, &tcr2, rcr = &ocr);
		else rcr = &tcr2;
	}
	if (!rcr) return; /* Both positions invisible */
	gtk_widget_queue_draw_area(widget, rcr->x, rcr->y, rcr->width, rcr->height);
}

/* Must be called after realize */
int wj_fpixmap_set_cursor(GtkWidget *widget, char *image, char *mask,
	int width, int height, int hot_x, int hot_y, int focused)
{
	fpixmap_data *d;

	if (!GTK_WIDGET_REALIZED(widget)) return (FALSE);
	if (!(d = wj_fpixmap_data(widget))) return (FALSE);
	if (d->cursor) gdk_pixmap_unref(d->cursor);
	if (d->cmask) gdk_bitmap_unref(d->cmask);
	d->cursor = NULL; d->cmask = NULL;
	d->focused_cursor = focused;
	while (image)
	{
		d->cursor = gdk_pixmap_create_from_data(widget->window,
			image, width, height, -1,
			&widget->style->white, &widget->style->black);
		d->cr.x = d->pm.x + d->xc - hot_x;
		d->cr.y = d->pm.y + d->yc - hot_y;
		d->cr.width = width;
		d->cr.height = height;
		if (!mask) break;
		d->cmask = gdk_bitmap_create_from_data(widget->window,
			mask, width, height);
		break;
	}
	if (!d->pixmap) return (TRUE);
	/* Optimizing redraw in a rare operation is useless */
	gtk_widget_queue_draw(widget);
	return (TRUE);
}

int wj_fpixmap_xy(GtkWidget *widget, int x, int y, int *xr, int *yr)
{
	fpixmap_data *d;
	if (!(d = wj_fpixmap_data(widget))) return (FALSE);
	if (!d->pixmap) return (FALSE);
	x -= d->pm.x; y -= d->pm.y;
	if ((x < 0) || (x >= d->pm.width) || (y < 0) || (y >= d->pm.height))
		return (FALSE);
	*xr = x; *yr = y;
	return (TRUE);
}

void wj_fpixmap_cursor(GtkWidget *widget, int *x, int *y)
{
	fpixmap_data *d = wj_fpixmap_data(widget);
	if (d && d->cursor) *x = d->xc , *y = d->yc;
	else *x = *y = 0;
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

/*	mygtk.h
	Copyright (C) 2004-2006 Mark Tyler and Dmitry Groshev

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

///	Generic Widget Primitives

GtkWidget *add_a_window( GtkWindowType type, char *title, GtkWindowPosition pos, gboolean modal );
GtkWidget *add_a_button( char *text, int bord, GtkWidget *box, gboolean filler );
GtkWidget *add_a_spin( int value, int min, int max );
GtkWidget *add_a_table( int rows, int columns, int bord, GtkWidget *box );
GtkWidget *add_a_toggle( char *label, GtkWidget *box, gboolean value );
GtkWidget *add_to_table( char *text, GtkWidget *table, int row, int column, int spacing);
GtkWidget *spin_to_table( GtkWidget *table, int row, int column, int spacing,
	int value, int min, int max );
void add_hseparator( GtkWidget *widget, int xs, int ys );

void progress_init(char *text, int canc);		// Initialise progress window
int progress_update(float val);				// Update progress window
void progress_end();					// Close progress window

int alert_box( char *title, char *message, char *text1, char *text2, char *text3 );

// Slider-spin combo (practically a new widget class)

GtkWidget *mt_spinslide_new(gint swidth, gint sheight);
void mt_spinslide_set_range(GtkWidget *spinslide, gint minv, gint maxv);
gint mt_spinslide_get_value(GtkWidget *spinslide);
gint mt_spinslide_read_value(GtkWidget *spinslide);
void mt_spinslide_set_value(GtkWidget *spinslide, gint value);
/* void handler(GtkAdjustment *adjustment, gpointer user_data); */
void mt_spinslide_connect(GtkWidget *spinslide, GtkSignalFunc handler,
	gpointer user_data);
#define ADJ2INT(a) ((int)((a)->value + 0.5))

// Self-contained package of radio buttons

GtkWidget *wj_radio_pack(char **names, int cnt, int vnum, int idx, int *var,
	GtkSignalFunc handler);

// Buttons for standard dialogs

GtkWidget *OK_box(int border, GtkWidget *window, char *nOK, GtkSignalFunc OK,
	char *nCancel, GtkSignalFunc Cancel);
GtkWidget *OK_box_add(GtkWidget *box, char *name, GtkSignalFunc Handler, int idx);

// Easier way with spinbuttons

int read_spin(GtkWidget *spin);
GtkWidget *add_float_spin(double value, double min, double max);
void spin_connect(GtkWidget *spin, GtkSignalFunc handler, gpointer user_data);

// Box unpacking macro
#define BOX_CHILD(box, n) \
	(((GtkBoxChild *)g_list_nth_data(GTK_BOX(box)->children, (n)))->widget)

// Wrapper for utf8->C translation

char *gtkncpy(char *dest, const char *src, int cnt);

// Extracting widget from GtkTable

GtkWidget *table_slot(GtkWidget *table, int row, int col);

// Packing framed widget

GtkWidget *add_with_frame_x(GtkWidget *box, char *text, GtkWidget *widget,
	int border, int expand);
GtkWidget *add_with_frame(GtkWidget *box, char *text, GtkWidget *widget, int border);

// Option menu

GtkWidget *wj_option_menu(char **names, int cnt, int idx, gpointer var,
	GtkSignalFunc handler);
int wj_option_menu_get_history(GtkWidget *optmenu);

// Pixmap-backed drawing area

GtkWidget *wj_pixmap(int width, int height);

// Set minimum size for a widget

void widget_set_minsize(GtkWidget *widget, int width, int height);
GtkWidget *widget_align_minsize(GtkWidget *widget, int width, int height);

// Signalled toggle

GtkWidget *sig_toggle(char *label, int value, int *var, GtkSignalFunc handler);

// Moving mouse cursor

int move_mouse_relative(int dx, int dy);

// Mapping keyval to key

guint real_key(GdkEventKey *event);
guint keyval_key(guint keyval);

// Filtering bogus xine-ui "keypresses" (Linux only)
#ifdef WIN32
#define XINE_FAKERY(key) 0
#else
#define XINE_FAKERY(key) (((key) == GDK_Shift_L) || ((key) == GDK_Control_L) \
	|| ((key) == GDK_Scroll_Lock) || ((key) == GDK_Num_Lock))
#endif

// Workaround for stupid GTK1 typecasts
#if GTK_MAJOR_VERSION == 1
#define GTK_RADIO_BUTTON_0(X) (GtkRadioButton *)(X)
#else
#define GTK_RADIO_BUTTON_0(X) GTK_RADIO_BUTTON(X)
#endif

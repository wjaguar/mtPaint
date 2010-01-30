/*	mygtk.h
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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

///	Icon descriptor type

typedef void *xpm_icon_desc[2];

#if GTK_MAJOR_VERSION == 1
#define XPM_TYPE char**
#else /* if GTK_MAJOR_VERSION == 2 */
#define XPM_TYPE void**
#endif

///	Generic RGB buffer

typedef struct {
	int xy[4];
	unsigned char *rgb;
} rgbcontext;

///	Generic Widget Primitives

GtkWidget *add_a_window( GtkWindowType type, char *title, GtkWindowPosition pos, gboolean modal );
GtkWidget *add_a_button( char *text, int bord, GtkWidget *box, gboolean filler );
GtkWidget *add_a_spin( int value, int min, int max );
GtkWidget *add_a_table( int rows, int columns, int bord, GtkWidget *box );
GtkWidget *add_a_toggle( char *label, GtkWidget *box, gboolean value );
GtkWidget *add_to_table( char *text, GtkWidget *table, int row, int column, int spacing);
GtkWidget *add_to_table_l(char *text, GtkWidget *table, int row, int column,
	int l, int spacing);
GtkWidget *to_table(GtkWidget *widget, GtkWidget *table, int row, int column, int spacing);
GtkWidget *to_table_l(GtkWidget *widget, GtkWidget *table, int row, int column,
	int l, int spacing);
GtkWidget *spin_to_table( GtkWidget *table, int row, int column, int spacing,
	int value, int min, int max );
GtkWidget *float_spin_to_table(GtkWidget *table, int row, int column, int spacing,
	double value, double min, double max);
void add_hseparator( GtkWidget *widget, int xs, int ys );

void progress_init(char *text, int canc);		// Initialise progress window
int progress_update(float val);				// Update progress window
void progress_end();					// Close progress window

int alert_box(char *title, char *message, char *text1, ...);

// Add page to notebook

GtkWidget *add_new_page(GtkWidget *notebook, char *name);

// Slider-spin combo (practically a new widget class)

GtkWidget *mt_spinslide_new(int swidth, int sheight);
void mt_spinslide_set_range(GtkWidget *spinslide, int minv, int maxv);
int mt_spinslide_get_value(GtkWidget *spinslide);
int mt_spinslide_read_value(GtkWidget *spinslide);
void mt_spinslide_set_value(GtkWidget *spinslide, int value);
/* void handler(GtkAdjustment *adjustment, gpointer user_data); */
void mt_spinslide_connect(GtkWidget *spinslide, GtkSignalFunc handler,
	gpointer user_data);
#define SPINSLIDE_ADJUSTMENT(s) \
	(GTK_SPIN_BUTTON(BOX_CHILD_1(s))->adjustment)
#define ADJ2INT(a) ((int)rint((a)->value))

// Self-contained package of radio buttons

GtkWidget *wj_radio_pack(char **names, int cnt, int vnum, int idx, gpointer var,
	GtkSignalFunc handler);

// Convert window close into a button click ("Cancel" or whatever)

void delete_to_click(GtkWidget *window, GtkWidget *button);

// Buttons for standard dialogs

GtkWidget *OK_box(int border, GtkWidget *window, char *nOK, GtkSignalFunc OK,
	char *nCancel, GtkSignalFunc Cancel);
GtkWidget *OK_box_add(GtkWidget *box, char *name, GtkSignalFunc Handler);
GtkWidget *OK_box_add_toggle(GtkWidget *box, char *name, GtkSignalFunc Handler);

// Easier way with spinbuttons

int read_spin(GtkWidget *spin);
double read_float_spin(GtkWidget *spin);
GtkWidget *add_float_spin(double value, double min, double max);
void spin_connect(GtkWidget *spin, GtkSignalFunc handler, gpointer user_data);
#if GTK_MAJOR_VERSION == 1
void spin_set_range(GtkWidget *spin, int min, int max);
#else
#define spin_set_range(spin, min, max) \
	gtk_spin_button_set_range(GTK_SPIN_BUTTON(spin), (min), (max))
#endif

// Box unpacking macros
#define BOX_CHILD_0(box) \
	(((GtkBoxChild*)GTK_BOX(box)->children->data)->widget)
#define BOX_CHILD_1(box) \
	(((GtkBoxChild*)GTK_BOX(box)->children->next->data)->widget)
#define BOX_CHILD_2(box) \
	(((GtkBoxChild*)GTK_BOX(box)->children->next->next->data)->widget)
#define BOX_CHILD(box, n) \
	(((GtkBoxChild *)g_list_nth_data(GTK_BOX(box)->children, (n)))->widget)

// Wrapper for utf8->C translation

char *gtkncpy(char *dest, const char *src, int cnt);

// Wrapper for utf8->C and C->utf8 translation

char *gtkxncpy(char *dest, const char *src, int cnt, int u);
#define gtkncpy(dest, src, cnt) gtkxncpy(dest, src, cnt, FALSE)
#define gtkuncpy(dest, src, cnt) gtkxncpy(dest, src, cnt, TRUE)

// Generic wrapper for strncpy(), ensuring NUL termination

#define strncpy0(A,B,C) (strncpy((A), (B), (C))[(C) - 1] = 0)

// A more sane replacement for strncat()

char *strnncat(char *dest, const char *src, int max);

// Extracting widget from GtkTable

GtkWidget *table_slot(GtkWidget *table, int row, int col);

// Packing framed widget

GtkWidget *add_with_frame_x(GtkWidget *box, char *text, GtkWidget *widget,
	int border, int expand);
GtkWidget *add_with_frame(GtkWidget *box, char *text, GtkWidget *widget);

// Entry + Browse

GtkWidget *mt_path_box(char *name, GtkWidget *box, char *title, int fsmode);

// Option menu

GtkWidget *wj_option_menu(char **names, int cnt, int idx, gpointer var,
	GtkSignalFunc handler);
int wj_option_menu_get_history(GtkWidget *optmenu);

// Workaround for broken option menu sizing in GTK2
#if GTK_MAJOR_VERSION == 2
void wj_option_realize(GtkWidget *widget, gpointer user_data);
#define FIX_OPTION_MENU_SIZE(opt) \
	gtk_signal_connect_after(GTK_OBJECT(opt), "realize", \
		GTK_SIGNAL_FUNC(wj_option_realize), NULL)
#else
#define FIX_OPTION_MENU_SIZE(opt)
#endif

// Set minimum size for a widget

void widget_set_minsize(GtkWidget *widget, int width, int height);
GtkWidget *widget_align_minsize(GtkWidget *widget, int width, int height);

// Make widget request no less size than before (in one direction)

void widget_set_keepsize(GtkWidget *widget, int keep_height);

// Signalled toggles

GtkWidget *sig_toggle(char *label, int value, gpointer var, GtkSignalFunc handler);
GtkWidget *sig_toggle_button(char *label, int value, gpointer var, GtkSignalFunc handler);

// Workaround for GtkCList reordering bug in GTK2

void clist_enable_drag(GtkWidget *clist);

// Move browse-mode selection in GtkCList without invoking callbacks

void clist_reselect_row(GtkCList *clist, int n);

// Move browse-mode selection in GtkList

void list_select_item(GtkWidget *list, GtkWidget *item);

// Properly destroy transient window

void destroy_dialog(GtkWidget *window);

// Settings notebook

GtkWidget *plain_book(GtkWidget **pages, int npages);
GtkWidget *buttoned_book(GtkWidget **page0, GtkWidget **page1,
	GtkWidget **button, char *button_label);

// Most common use of boxes

GtkWidget *pack(GtkWidget *box, GtkWidget *widget);
GtkWidget *xpack(GtkWidget *box, GtkWidget *widget);
GtkWidget *pack_end(GtkWidget *box, GtkWidget *widget);
GtkWidget *pack5(GtkWidget *box, GtkWidget *widget);
GtkWidget *xpack5(GtkWidget *box, GtkWidget *widget);
GtkWidget *pack_end5(GtkWidget *box, GtkWidget *widget);

// Put vbox into container

GtkWidget *add_vbox(GtkWidget *cont);

// Save/restore window positions

void win_store_pos(GtkWidget *window, char *inikey);
void win_restore_pos(GtkWidget *window, char *inikey, int defx, int defy,
	int defw, int defh);

// Fix for paned widgets losing focus in GTK+1

#if GTK_MAJOR_VERSION == 1
void paned_mouse_fix(GtkWidget *widget);
#else
#define paned_mouse_fix(X)
#endif

// Init-time bugfixes

void gtk_init_bugfixes();

// Moving mouse cursor

int move_mouse_relative(int dx, int dy);

// Mapping keyval to key

guint real_key(GdkEventKey *event);
guint low_key(GdkEventKey *event);
guint keyval_key(guint keyval);

// Interpreting arrow keys

int arrow_key(GdkEventKey *event, int *dx, int *dy, int mult);

// Create pixmap cursor

GdkCursor *make_cursor(const char *icon, const char *mask, int w, int h,
	int tip_x, int tip_y);

// Menu-like combo box

GtkWidget *wj_combo_box(char **names, int cnt, int idx, gpointer var,
	GtkSignalFunc handler);
int wj_combo_box_get_history(GtkWidget *combobox);

// Bin widget with customizable size handling

GtkWidget *wj_size_bin();

// Disable visual updates while tweaking container's contents

gpointer toggle_updates(GtkWidget *widget, gpointer unlock);

// Drawable to RGB

unsigned char *wj_get_rgb_image(GdkWindow *window, GdkPixmap *pixmap,
	unsigned char *buf, int x, int y, int width, int height);

// Clipboard

int internal_clipboard(int which);
int process_clipboard(int which, char *what, GtkSignalFunc handler, gpointer data);
int offer_clipboard(int which, GtkTargetEntry *targets, int ntargets,
	GtkSignalFunc handler);

// Allocate a memory chunk which is freed along with a given widget

void *bound_malloc(GtkWidget *widget, int size);

// Gamma correction toggle

GtkWidget *gamma_toggle();

// Image widget

GtkWidget *xpm_image(XPM_TYPE xpm);

// Render stock icons to pixmaps

/* !!! Mask needs be zeroed before the call - especially with GTK+1 :-) */
#if GTK_MAJOR_VERSION == 1
#define render_stock_pixmap(X,Y,Z) NULL
#else
GdkPixmap *render_stock_pixmap(GtkWidget *widget, const gchar *stock_id,
	GdkBitmap **mask);
#endif

// Release outstanding pointer grabs

int release_grab();

// Frame widget with passthrough scrolling

GtkWidget *wjframe_new();
void add_with_wjframe(GtkWidget *bin, GtkWidget *widget);

// Scrollable canvas widget

GtkWidget *wjcanvas_new();
void wjcanvas_size(GtkWidget *widget, int width, int height);
void wjcanvas_get_vport(GtkWidget *widget, int *vport);
int wjcanvas_scroll_in(GtkWidget *widget, int x, int y);

// Focusable pixmap widget

GtkWidget *wjpixmap_new(int width, int height);
GdkPixmap *wjpixmap_pixmap(GtkWidget *widget);
void wjpixmap_draw_rgb(GtkWidget *widget, int x, int y, int w, int h,
	unsigned char *rgb, int step);
void wjpixmap_fill_rgb(GtkWidget *widget, int x, int y, int w, int h, int rgb);
void wjpixmap_move_cursor(GtkWidget *widget, int x, int y);
void wjpixmap_set_cursor(GtkWidget *widget, char *image, char *mask,
	int width, int height, int hot_x, int hot_y, int focused);
void wjpixmap_cursor(GtkWidget *widget, int *x, int *y);
int wjpixmap_rxy(GtkWidget *widget, int x, int y, int *xr, int *yr);

// Repaint expose region

// !!! For now, repaint_func() is expected to know widget & window to repaint
typedef void (*repaint_func)(int x, int y, int w, int h);

#if GTK_MAJOR_VERSION == 1 /* No regions there */
#define repaint_expose(event, vport, repaint, cost) \
	(repaint)((event)->area.x + (vport)[0], (event)->area.y + (vport)[1], \
		(event)->area.width, (event)->area.height)
#else 
void repaint_expose(GdkEventExpose *event, int *vport, repaint_func repaint, int cost);
#endif

// Track updates of multiple widgets (by whatever means necessary)

void track_updates(GtkSignalFunc handler, GtkWidget *widget, ...);

// Convert pathname to absolute

char *resolve_path(char *buf, int buflen, char *path);

// A (better) substitute for fnmatch(), in case one is needed

#if defined(WIN32) || ((GTK_MAJOR_VERSION == 2) && (GTK_MINOR_VERSION < 4))
int wjfnmatch(const char *mask, const char *str, int utf);
#endif

// Prod the focused spinbutton, if any, to finally update its value

void update_window_spin(GtkWidget *window);

// Process event queue

void handle_events();

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

// Path string sizes
/* If path is longer than this, it is user's own problem */
#define SANE_PATH_LEN 2048

#ifdef WIN32
#define PATHBUF 260 /* MinGW defines PATH_MAX to not include terminating NUL */
#elif defined MAXPATHLEN
#define PATHBUF MAXPATHLEN /* MAXPATHLEN includes the NUL */
#elif defined PATH_MAX
#define PATHBUF PATH_MAX /* POSIXly correct PATH_MAX does too */
#else
#define PATHBUF SANE_PATH_LEN /* Arbitrary limit for GNU Hurd and the like */
#endif
#if PATHBUF > SANE_PATH_LEN /* Force a sane limit */
#undef PATHBUF
#define PATHBUF SANE_PATH_LEN
#endif

#if GTK_VERSION_MAJOR == 1 /* Same encoding in GTK+1 */
#define PATHTXT PATHBUF
#else /* Allow for expansion when converting from codepage to UTF8 */
#define PATHTXT (PATHBUF * 2)
#endif

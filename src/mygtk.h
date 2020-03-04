/*	mygtk.h
	Copyright (C) 2004-2020 Mark Tyler and Dmitry Groshev

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
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <dirent.h>
#include <sys/stat.h>

///	GTK+2 version to use

#if GTK_MAJOR_VERSION == 2
#ifndef GTK2VERSION
#define GTK2VERSION GTK_MINOR_VERSION
#endif
#endif

///	List widgets to use

#if GTK_MAJOR_VERSION == 1
#define U_LISTS_GTK1

#else /* GTK_MAJOR_VERSION == 2 */
#if GTK2VERSION < 18
#define U_LISTS_GTK1
#endif
#endif

#define gtk_widget_get_parent(A) ((A)->parent)
#define KEY(A) GDK_##A

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

///	Main toplevel, for anchoring dialogs and rendering pixmaps

GtkWidget *main_window;

///	Generic Widget Primitives

GtkWidget *add_a_window(GtkWindowType type, char *title, GtkWindowPosition pos);
GtkWidget *add_a_spin( int value, int min, int max );

int user_break;

void progress_init(char *text, int canc);		// Initialise progress window
int progress_update(float val);				// Update progress window
void progress_end();					// Close progress window

int alert_box(char *title, char *message, char *text1, ...);

// Slider-spin combo (a decorated spinbutton)

GtkWidget *mt_spinslide_new(int swidth, int sheight);
#define ADJ2INT(a) ((int)rint((a)->value))

// Self-contained package of radio buttons

GtkWidget *wj_radio_pack(char **names, int cnt, int vnum, int idx, void **r,
	GtkSignalFunc handler);
int wj_radio_pack_get_active(GtkWidget *widget);

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

// Wrapper for utf8->C and C->utf8 translation

char *gtkxncpy(char *dest, const char *src, int cnt, int u);
#define gtkncpy(dest, src, cnt) gtkxncpy(dest, src, cnt, FALSE)
#define gtkuncpy(dest, src, cnt) gtkxncpy(dest, src, cnt, TRUE)

// Generic wrapper for strncpy(), ensuring NUL termination

#define strncpy0(A,B,C) (strncpy((A), (B), (C))[(C) - 1] = 0)

// A more sane replacement for strncat()

char *strnncat(char *dest, const char *src, int max);

// Add C strings to a string with explicit length

char *wjstrcat(char *dest, int max, const char *s0, int l, ...);

// Add directory to filename

char *file_in_dir(char *dest, const char *dir, const char *file, int cnt);
char *file_in_homedir(char *dest, const char *file, int cnt);

// Set minimum size for a widget

void widget_set_minsize(GtkWidget *widget, int width, int height);
GtkWidget *widget_align_minsize(GtkWidget *widget, int width, int height);

// Make widget request no less size than before (in one direction)

void widget_set_keepsize(GtkWidget *widget, int keep_height);

// Workaround for GtkCList reordering bug in GTK2

void clist_enable_drag(GtkWidget *clist);

// Most common use of boxes

GtkWidget *pack(GtkWidget *box, GtkWidget *widget);
GtkWidget *xpack(GtkWidget *box, GtkWidget *widget);
GtkWidget *pack_end(GtkWidget *box, GtkWidget *widget);

// Put vbox into container

GtkWidget *add_vbox(GtkWidget *cont);

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

int arrow_key_(unsigned key, unsigned state, int *dx, int *dy, int mult);
#define arrow_key(E,X,Y,M) arrow_key_((E)->keyval, (E)->state, (X), (Y), (M))

// Create pixmap cursor

GdkCursor *make_cursor(const char *icon, const char *mask, int w, int h,
	int tip_x, int tip_y);

// Menu-like combo box

GtkWidget *wj_combo_box(char **names, int cnt, int u, int idx, void **r,
	GtkSignalFunc handler);
int wj_combo_box_get_history(GtkWidget *combobox);

// Box widget with customizable size handling

GtkWidget *wj_size_box();

// Disable visual updates while tweaking container's contents

gpointer toggle_updates(GtkWidget *widget, gpointer unlock);

// Maximized state

#if GTK_MAJOR_VERSION == 1
int is_maximized(GtkWidget *window);
void set_maximized(GtkWidget *window);
#else /* if GTK_MAJOR_VERSION == 2 */
#define is_maximized(W) \
	(!!(gdk_window_get_state((W)->window) & GDK_WINDOW_STATE_MAXIMIZED))
#define set_maximized(W) gtk_window_maximize(W)
#endif

// Drawable to RGB

unsigned char *wj_get_rgb_image(GdkWindow *window, GdkPixmap *pixmap,
	unsigned char *buf, int x, int y, int width, int height);

// Clipboard

int internal_clipboard(int which);

// Clipboard pixmaps

typedef unsigned long XID_type;

typedef struct {
	int w, h, depth;
	XID_type xid;
	GdkPixmap *pm;
} pixmap_info;

#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
#define HAVE_PIXMAPS
int export_pixmap(pixmap_info *p, int w, int h);
void pixmap_put_rows(pixmap_info *p, unsigned char *src, int y, int cnt);
#endif
int import_pixmap(pixmap_info *p, XID_type *xid); // xid = NULL for a screenshot
void drop_pixmap(pixmap_info *p);
int pixmap_get_rows(pixmap_info *p, unsigned char *dest, int y, int cnt);

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

// Scrollable canvas widget

GtkWidget *wjcanvas_new();
void wjcanvas_size(GtkWidget *widget, int width, int height);
void wjcanvas_get_vport(GtkWidget *widget, int *vport);
int wjcanvas_scroll_in(GtkWidget *widget, int x, int y);
int wjcanvas_bind_mouse(GtkWidget *widget, GdkEventMotion *event, int x, int y);

// Focusable pixmap widget

GtkWidget *wjpixmap_new(int width, int height);
GdkPixmap *wjpixmap_pixmap(GtkWidget *widget);
void wjpixmap_draw_rgb(GtkWidget *widget, int x, int y, int w, int h,
	unsigned char *rgb, int step);
void wjpixmap_move_cursor(GtkWidget *widget, int x, int y);
void wjpixmap_set_cursor(GtkWidget *widget, char *image, char *mask,
	int width, int height, int hot_x, int hot_y, int focused);
int wjpixmap_rxy(GtkWidget *widget, int x, int y, int *xr, int *yr);

// Type of pathname

#define PT_ABS 0	/* Absolute */
#define PT_REL 1	/* Relative */
#define PT_DRIVE_ABS 2	/* On Windows: absolute w/o drive (\DIR\FILE) */
#define PT_DRIVE_REL 3	/* On Windows: relative with drive (C:FILE) */

int path_type(char *path);

// Convert pathname to absolute

char *resolve_path(char *buf, int buflen, char *path);

// A (better) substitute for fnmatch(), in case one is needed

#if defined(WIN32) || ((GTK_MAJOR_VERSION == 2) && (GTK2VERSION < 4))
int wjfnmatch(const char *mask, const char *str, int utf);
#endif

// Replace '/' path separators

#ifdef WIN32
void reseparate(char *str);
#endif

// Process event queue

void handle_events();

// Make GtkEntry accept Ctrl+Enter as a character

void accept_ctrl_enter(GtkWidget *entry);

// Grab/ungrab input

#define GRAB_FULL    0 /* Redirect everything */
#define GRAB_WIDGET  1 /* Redirect everything outside widget */
#define GRAB_PROGRAM 2 /* Redirect everything outside program's windows */

int do_grab(int mode, GtkWidget *widget, GdkCursor *cursor);
void undo_grab(GtkWidget *widget);

// Workaround for crazy GTK+1 resize handling

#if GTK_MAJOR_VERSION == 1
void force_resize(GtkWidget *widget);
#endif

// Workaround for broken GTK_SHADOW_NONE viewports in GTK+1

#if GTK_MAJOR_VERSION == 1
void vport_noshadow_fix(GtkWidget *widget);
#else
#define vport_noshadow_fix(X)
#endif

// Helper for accessing scrollbars

void get_scroll_adjustments(GtkWidget *win, GtkAdjustment **h, GtkAdjustment **v);

// Helper for widget show/hide

void widget_showhide(GtkWidget *widget, int what);

// Color name to value

int parse_color(char *what);

//	DPI value

double window_dpi(GtkWidget *win);

//	Memory size (Mb)

unsigned sys_mem_size();

// Filtering bogus xine-ui "keypresses" (Linux only)
#ifdef WIN32
#define XINE_FAKERY(key) 0
#else
#define XINE_FAKERY(key) (((key) == KEY(Shift_L)) || ((key) == KEY(Control_L)) \
	|| ((key) == KEY(Scroll_Lock)) || ((key) == KEY(Num_Lock)))
#endif

// Workaround for stupid GTK1 typecasts
#if GTK_MAJOR_VERSION == 1
#define GTK_RADIO_BUTTON_0(X) (GtkRadioButton *)(X)
#else
#define GTK_RADIO_BUTTON_0(X) GTK_RADIO_BUTTON(X)
#endif

// Path separator char
#ifdef WIN32
#define DIR_SEP '\\'
#define DIR_SEP_STR "\\"
#else
#define DIR_SEP '/'
#define DIR_SEP_STR "/"
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

#if GTK_MAJOR_VERSION == 1 /* Same encoding in GTK+1 */
#define PATHTXT PATHBUF
#else /* Allow for expansion when converting from codepage to UTF8 */
#define PATHTXT (PATHBUF * 2)
#endif

// Filename string size

#ifdef WIN32
#define NAMEBUF 256 /* MinGW doesn't define MAXNAMLEN nor NAME_MAX */
#elif defined MAXNAMLEN
#define NAMEBUF (MAXNAMLEN + 1)
#elif defined NAME_MAX
#define NAMEBUF (NAME_MAX + 1)
#else
#define NAMEBUF 256 /* Most filesystems limit filenames to 255 bytes or less */
#endif

// Threading helpers

#if 0 /* Not needed for now - GTK+/Win32 still isn't thread-safe anyway */
//#ifdef U_THREADS
guint threads_idle_add_priority(gint priority, GtkFunction function, gpointer data);
guint threads_timeout_add(guint32 interval, GSourceFunc function, gpointer data);
#define THREADS_ENTER() gdk_threads_enter()
#define THREADS_LEAVE() gdk_threads_leave()
#else
#define threads_idle_add_priority(X,Y,Z) gtk_idle_add_priority(X,Y,Z)
#define threads_timeout_add(X,Y,Z) g_timeout_add(X,Y,Z)
#define THREADS_ENTER()
#define THREADS_LEAVE()
#endif

/*	vcode.c
	Copyright (C) 2013-2021 Dmitry Groshev

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
#include "vcode.h"
#include "inifile.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "canvas.h"
#include "cpick.h"
#include "icons.h"
#include "fpick.h"
#include "prefs.h"

/// More of meaningless differences to hide

#if GTK_MAJOR_VERSION == 3
#define gdk_rgb_init() /* Not needed anymore */
#define gtk_object_get_data(A,B) g_object_get_data(A,B)
#define gtk_object_set_data(A,B,C) g_object_set_data(A,B,C)
#define gtk_object_get_data_by_id(A,B) g_object_get_qdata(A,B)
#define gtk_object_set_data_by_id(A,B,C) g_object_set_qdata(A,B,C)
#define GtkObject GObject
#define gtk_object_weakref(A,B,C) g_object_weak_ref(A,B,C)
#define GtkDestroyNotify GWeakNotify
#define GTK_WIDGET_REALIZED(A) gtk_widget_get_realized(A)
#define GTK_WIDGET_MAPPED(A) gtk_widget_get_mapped(A)
#define	GTK_WIDGET_SENSITIVE(A) gtk_widget_get_sensitive(A)
#define	GTK_WIDGET_IS_SENSITIVE(A) gtk_widget_is_sensitive(A)
#define GTK_WIDGET_VISIBLE(A) gtk_widget_get_visible(A)
#define gtk_notebook_set_page gtk_notebook_set_current_page
#define gtk_accel_group_unref g_object_unref
#define gtk_hpaned_new() gtk_paned_new(GTK_ORIENTATION_HORIZONTAL)
#define gtk_vpaned_new() gtk_paned_new(GTK_ORIENTATION_VERTICAL)
#define gtk_hseparator_new() gtk_separator_new(GTK_ORIENTATION_HORIZONTAL)
#define gtk_vseparator_new() gtk_separator_new(GTK_ORIENTATION_VERTICAL)
#define hbox_new(A) gtk_box_new(GTK_ORIENTATION_HORIZONTAL, (A))
#define vbox_new(A) gtk_box_new(GTK_ORIENTATION_VERTICAL, (A))
#define gtk_menu_item_right_justify(A) gtk_menu_item_set_right_justified((A), TRUE)
#define gtk_radio_menu_item_group gtk_radio_menu_item_get_group
#define gtk_widget_ref(A) g_object_ref(A)
#define gtk_widget_unref(A) g_object_unref(A)

static GQuark tool_key;
#define TOOL_KEY "mtPaint.tool"

/* Running in a wheel is for hamsters, and pointless churn is pointless */
G_GNUC_BEGIN_IGNORE_DEPRECATIONS

#else
#define gtk_selection_data_get_data(A) ((A)->data)
#define gtk_selection_data_get_length(A) ((A)->length)
#define gtk_selection_data_get_format(A) ((A)->format)
#define gtk_selection_data_get_target(A) ((A)->target)
#define gtk_selection_data_get_data_type(A) ((A)->type)
#define gtk_bin_get_child(A) ((A)->child)
#define gtk_text_view_get_buffer(A) ((A)->buffer)
#define gtk_dialog_get_action_area(A) ((A)->action_area)
#define gtk_dialog_get_content_area(A) ((A)->vbox)
#define gtk_font_selection_get_preview_entry(A) ((A)->preview_entry)
#define gtk_check_menu_item_get_active(A) ((A)->active)
#define gtk_adjustment_get_value(A) ((A)->value)
#define gtk_adjustment_get_upper(A) ((A)->upper)
#define gtk_adjustment_get_page_size(A) ((A)->page_size)
#define gtk_window_get_focus(A) ((A)->focus_widget)
#define gtk_paned_get_child1(A) ((A)->child1)
#define gtk_paned_get_child2(A) ((A)->child2)
#define gtk_paned_get_position(A) ((A)->child1_size)
#define gtk_menu_item_get_submenu(A) ((A)->submenu)
#define hbox_new(A) gtk_hbox_new(FALSE, (A))
#define vbox_new(A) gtk_vbox_new(FALSE, (A))

#endif

/* Make code not compile if it cannot work */
typedef char Opcodes_Too_Long[2 * (op_LAST <= WB_OPMASK) - 1];

/// V-CODE ENGINE

/* Max V-code subroutine nesting */
#define CALL_DEPTH 16
/* Max container widget nesting */
#define CONT_DEPTH 128
/* Max columns in a list */
#define MAX_COLS 16
/* Max keys in keymap */
#define MAX_KEYS 512

#define VVS(N) (((N) + sizeof(void *) - 1) / sizeof(void *))

#define GET_OPF(S) ((int)*(void **)(S)[1])
#define GET_OP(S) (GET_OPF(S) & WB_OPMASK)

#define GET_DESCV(S,N) (((void **)(S)[1])[(N)])
#define GET_HANDLER(S) GET_DESCV(S, 1)

#if VSLOT_SIZE != 3
#error "Mismatched slot size"
#endif
// !!! V should never be NULL - IS_UNREAL() relies on that
#define EVSLOT(P,S,V) { (V), &(P), NULL, (S) }
#define EV_SIZE (VSLOT_SIZE + 1)
#define EV_PARENT(S) S[VSLOT_SIZE]

#define VCODE_KEY "mtPaint.Vcode"

/* Internal datastore */

#define GET_VDATA(V) ((V)[1])

typedef struct {
	void *code;	// Noop tag, must be first field
	void ***dv;	// Pointer to dialog response
	void **destroy;	// Pre-destruction event slot
	void **wantkey;	// Slot of prioritized keyboard handler
	void **keymap;	// KEYMAP slot
	void **smmenu;	// SMARTMENU slot
	void **fupslot;	// Slot which needs defocusing to update (only 1 for now)
	void **actmap;	// Actmap array
	void *now_evt;	// Keyboard event being handled (check against recursion)
	void *tparent;	// Transient parent window
	char *ininame;	// Prefix for inifile vars
	char **script;	// Commands if simulated
	int xywh[5];	// Stored window position/size/state
	int actn;	// Actmap slots count
	unsigned actmask;	// Last actmap mask
	unsigned vismask;	// Visibility mask
	char modal;	// Set modal flag when displaying
	char raise;	// Raise after displaying
	char unfocus;	// Focus to NULL after displaying
	char done;	// Set when destroyed
	char run;	// Set when script is running
} v_dd;

/* Actmap array */

#define ACT_SIZE 2 /* Pointers per actmap slot */
#define ADD_ACT(W,S,V) ((W)[0] = (S) , (W)[1] = (V))

static void act_state(v_dd *vdata, unsigned int mask)
{
	void **map = vdata->actmap;
	unsigned int n, m, vm = vdata->vismask;
	int i;

	vdata->actmask = mask;
	i = vdata->actn;
	while (i-- > 0)
	{
		n = (unsigned)map[1];
		if ((m = n & vm)) cmd_showhide(map[0], !!(m & mask));
		if ((m = n & ~vm)) cmd_sensitive(map[0], !!(m & mask));
		map += ACT_SIZE;
	}
}

/* Simulated widget - one struct for all kinds */

typedef struct {
	char insens;	// Insensitivity flag
	short op;	// Internal opcode
	short cnt;	// Options count
	int value;	// Integer value
	int range[2];	// Value range
	char *id;	// Identifying string
	void *strs;	// Option labels / string value / anything else
} swdata;

#define IS_UNREAL(S) ((S)[0] == (S)[2])
#define GET_UOP(S) (((swdata *)(S)[0])->op)

static char *set_uentry(swdata *sd, char *s)
{
	return (sd->strs = !s ? NULL :
		sd->value < 0 ? g_strdup(s) : g_strndup(s, sd->value));
}

/* Command table */

typedef struct {
	short op, size, uop;
} cmdef;

static cmdef *cmds[op_LAST];

/* From widget to its wdata */
void **get_wdata(GtkWidget *widget, char *id)
{
	return (gtk_object_get_data(GTK_OBJECT(widget), id ? id : VCODE_KEY));
}

/* From slot to its wdata */
void **wdata_slot(void **slot)
{
	while (TRUE)
	{
		int op = GET_OP(slot);
		// WDONE anchors wdata
		if (op == op_WDONE) return (slot);
		// EVTs link to it
		if ((op >= op_EVT_0) && (op <= op_EVT_LAST)) return (*slot);
		// EVs link to parent slot
		if ((op >= op_EV_0) && (op <= op_EV_LAST))
			slot = EV_PARENT(slot);
		// Other slots just repose in sequence
		else slot = PREV_SLOT(slot);
		/* And if not valid wdata, die by SIGSEGV in the end */
	}
}

/* From event to its originator */
void **origin_slot(void **slot)
{
	while (TRUE)
	{
		int op = GET_OP(slot);
		if (op < op_EVT_0) return (slot);
		// EVs link to parent slot
		else if ((op >= op_EV_0) && (op <= op_EV_LAST))
			slot = EV_PARENT(slot);
		else slot = PREV_SLOT(slot);
		/* And if not valid wdata, die by SIGSEGV in the end */
	}
}

/* From slot to its storage location */
void *slot_data(void **slot, void *ddata)
{
	void **desc = slot[1], **v = desc[1];
	int opf = (int)desc[0];

	if (opf & WB_FFLAG) v = (void *)(ddata + (int)v);
	if (opf & WB_NFLAG) v = *v; // dereference
	return (v);
}

/* Find specific V-code _after_ this slot */
static void **op_slot(void **slot, int op)
{
	while ((slot = NEXT_SLOT(slot))[1])
	{
		int n = GET_OP(slot);
		// Found
		if (n == op) return (slot);
		// Till another origin slot
		if (n < op_EVT_0) break;
	}
	// Not found
	return (NULL);
}

/* Find unreal slot before this one */
static void **prev_uslot(void **slot)
{
	slot = origin_slot(PREV_SLOT(slot));
	if (!IS_UNREAL(slot)) slot = op_slot(slot, op_uALTNAME);
	return (slot);
}

void dialog_event(void *ddata, void **wdata, int what, void **where)
{
	v_dd *vdata = GET_VDATA(wdata);

	if (((int)vdata->code & WB_OPMASK) != op_WDONE) return; // Paranoia
	if (vdata->dv) *vdata->dv = where;
}

/* !!! Warning: handlers should not access datastore after window destruction!
 * GTK+ refs objects for signal duration, but no guarantee every other toolkit
 * will behave alike - WJ */

static void get_evt_1(GtkObject *widget, gpointer user_data)
{
	void **slot = user_data;
	void **base = slot[0], **desc = slot[1];

	((evt_fn)desc[1])(GET_DDATA(base), base, (int)desc[0] & WB_OPMASK, slot);
}

static void get_evt_1_t(GtkObject *widget, gpointer user_data)
{
	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		get_evt_1(widget, user_data);
}

void do_evt_1_d(void **slot)
{
	void **base = slot[0], **desc = slot[1];

	if (!desc[1]) run_destroy(base);
	else ((evt_fn)desc[1])(GET_DDATA(base), base, (int)desc[0] & WB_OPMASK, slot);
}

static gboolean get_evt_del(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	do_evt_1_d(user_data);
	return (TRUE); // it is for handler to decide, destroy it or not
}

static gboolean get_evt_conf(GtkWidget *widget, GdkEventConfigure *event,
	gpointer user_data)
{
	get_evt_1(NULL, user_data);
	return (TRUE); // no use of its propagating
}

static gboolean get_evt_key(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	void **slot = user_data;
	void **base = slot[0], **desc = slot[1];
	key_ext key = {
		event->keyval, low_key(event), real_key(event), event->state };
	int res = ((evtxr_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot, &key);
#if GTK_MAJOR_VERSION == 1
	/* Return value alone doesn't stop GTK1 from running other handlers */
	if (res) gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
#endif
	return (!!res);
}

static int check_smart_menu_keys(void *sdata, GdkEventKey *event);

static gboolean window_evt_key(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
	void **slot = user_data;
	v_dd *vdata = GET_VDATA((void **)slot[0]);
	gint res, res2, stop;

	/* Do nothing if called recursively */
	if ((void *)event == vdata->now_evt) return (FALSE);

	res = res2 = stop = 0;
	/* First, ask smart menu */
	if (vdata->smmenu) res = check_smart_menu_keys(vdata->smmenu, event);

	/* Now, ask prioritized widget */
	while (!res && vdata->wantkey)
	{
		void *was_evt, **wslot = origin_slot(vdata->wantkey);

		if (!GTK_WIDGET_MAPPED(wslot[0])) break; // not displayed
// !!! Maybe this check is enough by itself?
		if (!cmd_checkv(wslot, SLOT_FOCUSED)) break;

		slot = NEXT_SLOT(vdata->wantkey);
		if (GET_HANDLER(slot) &&
			(res = stop = get_evt_key(widget, event, slot))) break;

#if GTK_MAJOR_VERSION >= 2
		/* We let widgets in the focused part process the keys first */
		res = gtk_window_propagate_key_event(GTK_WINDOW(widget), event);
		if (res) break;
#endif
		/* Let default handlers have priority */
		// !!! Be ready to handle nested events
		was_evt = vdata->now_evt;
		vdata->now_evt = event;

		gtk_signal_emit_by_name(GTK_OBJECT(widget), "key_press_event",
			event, &res);
		res2 = TRUE; // Default events checked already

		vdata->now_evt = was_evt;

		break;
	}

	/* And only now, run own handler */
	slot = user_data;
	if (!res && GET_HANDLER(slot))
		res = stop = get_evt_key(widget, event, slot);

	res |= res2;
#if GTK_MAJOR_VERSION == 1
	/* Return value alone doesn't stop GTK1 from running other handlers */
	if (res && !stop)
		gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
#endif
	return (res);
}

//	Widget datastores

typedef struct {
	GtkWidget *pane, *vbox;
} dock_data;

#define HVSPLIT_MAX 4

typedef struct {
	GtkWidget *box, *panes[2], *inbox[HVSPLIT_MAX];
	int cnt;
} hvsplit_data;

//	Text renderer

#if GTK_MAJOR_VERSION >= 2

int texteng_aa = TRUE;
#if (GTK_MAJOR_VERSION == 3) || (GTK2VERSION >= 6)
#define FULLPANGO 1
int texteng_rot = TRUE;
int texteng_spc = TRUE;
#else
#define FULLPANGO 0
#define PangoMatrix int /* For rotate_box() */
#define PANGO_MATRIX_INIT 0
int texteng_rot = FALSE;
int texteng_spc = FALSE;
#endif
int texteng_lf = TRUE;
int texteng_dpi = TRUE;

#else /* GTK+1 */

#ifdef U_MTK
int texteng_aa = TRUE;
#else
int texteng_aa = FALSE;
#endif
int texteng_rot = FALSE;
int texteng_spc = FALSE;
int texteng_lf = FALSE;
int texteng_dpi = FALSE;

#endif

int texteng_con = FALSE;

typedef struct {
	double sysdpi;
	int dpi;
#if GTK_MAJOR_VERSION == 3
	int lastsize, lastdpi, lastset;
#endif
} fontsel_data;

#if GTK_MAJOR_VERSION >= 2

/* Pango coords to pixels, inclusive */
static void rotate_box(PangoRectangle *lr, PangoMatrix *m)
{
	double xx[4] = { lr->x, lr->x + lr->width, lr->x, lr->x + lr->width };
	double yy[4] = { lr->y, lr->y, lr->y + lr->height, lr->y + lr->height };
	double x_min, x_max, y_min, y_max;
	int i;


#if FULLPANGO
	if (m) for (i = 0; i < 4; i++)
	{
		double xt, yt;
		xt = xx[i] * m->xx + yy[i] * m->xy + m->x0;
		yt = xx[i] * m->yx + yy[i] * m->yy + m->y0;
		xx[i] = xt;
		yy[i] = yt;
	}
#endif
	x_min = x_max = xx[0];
	y_min = y_max = yy[0];
	for (i = 1; i < 4; i++)
	{
		if (x_min > xx[i]) x_min = xx[i];
		if (x_max < xx[i]) x_max = xx[i];
		if (y_min > yy[i]) y_min = yy[i];
		if (y_max < yy[i]) y_max = yy[i];
	}

	lr->x = floor(x_min / PANGO_SCALE);
	lr->width = ceil(x_max / PANGO_SCALE) - lr->x;
	lr->y = floor(y_min / PANGO_SCALE);
	lr->height = ceil(y_max / PANGO_SCALE) - lr->y;
}

#if GTK_MAJOR_VERSION == 3

#define FONT_SIGNAL "style_updated"

static void fontsel_style(GtkWidget *widget, gpointer user_data)
{
	GtkStyleContext *ctx;
	PangoFontDescription *d;
	void **r = user_data;
	fontsel_data *fd = r[2];
	int sz, fontsize;

	if (!fd->dpi || !fd->sysdpi)
	{
		fd->lastdpi = 0; // To force an update when DPI is set again
		return; // Do nothing else
	}
	ctx = gtk_widget_get_style_context(widget);
	gtk_style_context_get(ctx, gtk_style_context_get_state(ctx), "font", &d, NULL);
	sz = pango_font_description_get_size(d);
	fontsize = gtk_font_selection_get_size(GTK_FONT_SELECTION(r[0]));

	if ((sz != fd->lastset) || (fd->dpi != fd->lastdpi) || (fontsize != fd->lastsize))
	{
		fd->lastdpi = fd->dpi;
		fd->lastsize = fontsize;
		sz = (fontsize * fd->dpi) / fd->sysdpi;
		pango_font_description_set_size(d, fd->lastset = sz);
		gtk_widget_override_font(widget, d); // To make widget show it
	}
	pango_font_description_free(d);
}

#else

#define FONT_SIGNAL "style_set"

/* In principle, similar approach can be used with GTK+1 too - but it would be
 * much less clean and less precise, and I am unsure about possibly wasting
 * a lot of X font resources; therefore, no DPI for GTK+1 - WJ */
static void fontsel_style(GtkWidget *widget, GtkStyle *previous_style,
	gpointer user_data)
{
	PangoContext *c;
	PangoFontDescription *d;
	void **r = user_data;
	fontsel_data *fd = r[2];
	int sz;

	if (!fd->dpi || !fd->sysdpi) return; // Leave alone
	c = gtk_widget_get_pango_context(widget);
	if (!c) return;
	d = pango_context_get_font_description(c);
	sz = (pango_font_description_get_size(d) * fd->dpi) / fd->sysdpi;
	/* !!! 1st is used for font render, 2nd for GtkEntry size calculation;
	 * need to modify both the same way */
	pango_font_description_set_size(d, sz);
	pango_font_description_set_size(widget->style->font_desc, sz);
}

#endif
#endif /* GTK+2&3 */

static void fontsel_prepare(GtkWidget *widget, gpointer user_data)
{
	char **ss = user_data;
	gtk_font_selection_set_font_name(GTK_FONT_SELECTION(widget), ss[0]);
	gtk_font_selection_set_preview_text(GTK_FONT_SELECTION(widget), ss[1]);
	ss[0] = ss[1] = NULL;
}

static GtkWidget *fontsel(void **r, void *v)
{
	GtkWidget *widget = gtk_font_selection_new();
	GtkWidget *entry = gtk_font_selection_get_preview_entry(GTK_FONT_SELECTION(widget));

	accept_ctrl_enter(entry);
	/* !!! Setting initial values fails if no toplevel */
	gtk_signal_connect(GTK_OBJECT(widget), "realize",
		GTK_SIGNAL_FUNC(fontsel_prepare), v);
#if GTK_MAJOR_VERSION == 3
	/* !!! Kill off animating size changes */
	css_restyle(entry, ".mtPaint_fontentry { transition-duration: 0; "
		"transition-delay: 0; }", "mtPaint_fontentry", NULL);
#endif
#if GTK_MAJOR_VERSION >= 2
	gtk_signal_connect(GTK_OBJECT(entry), FONT_SIGNAL, 
		GTK_SIGNAL_FUNC(fontsel_style), r);
#endif
	return (widget);
}

#define PAD_SIZE 2

static void do_render_text(texteng_dd *td)
{
	GtkWidget *widget = main_window;
#if GTK_MAJOR_VERSION == 3
	cairo_surface_t *text_pixmap;
	cairo_t *cr;
	cairo_matrix_t cm;
#else
	GdkPixmap *text_pixmap;
#endif
	unsigned char *buf;
	int width, height, have_rgb = 0;

#if GTK_MAJOR_VERSION >= 2

	static const PangoAlignment align[3] = {
		PANGO_ALIGN_LEFT, PANGO_ALIGN_CENTER, PANGO_ALIGN_RIGHT };
	PangoMatrix matrix = PANGO_MATRIX_INIT, *mp = NULL;
	PangoRectangle ink, rect;
	PangoContext *context;
	PangoLayout *layout;
	PangoFontDescription *font_desc;
	int tx, ty;


	context = gtk_widget_create_pango_context(widget);
	layout = pango_layout_new(context);
	font_desc = pango_font_description_from_string(td->font);
	if (td->dpi) pango_font_description_set_size(font_desc,
		(int)(pango_font_description_get_size(font_desc) *
		(td->dpi / window_dpi(widget))));
	pango_layout_set_font_description(layout, font_desc);
	pango_font_description_free(font_desc);

	pango_layout_set_text(layout, td->text, -1);
	pango_layout_set_alignment(layout, align[td->align]);

#if FULLPANGO
	if (td->spacing)
	{
		PangoAttrList *al = pango_attr_list_new();
		PangoAttribute *a = pango_attr_letter_spacing_new(
			(td->spacing * PANGO_SCALE) / 100);
		pango_attr_list_insert(al, a);
		pango_layout_set_attributes(layout, al);
		pango_attr_list_unref(al);
	}
	if (td->angle)
	{
		pango_matrix_rotate(&matrix, td->angle * 0.01);
		pango_context_set_matrix(context, &matrix);
		pango_layout_context_changed(layout);
		mp = &matrix;
	}
#endif
	pango_layout_get_extents(layout, &ink, &rect);

	// Adjust height of ink rectangle to logical
	tx = ink.y + ink.height;
	ty = rect.y + rect.height;
	if (tx < ty) tx = ty;
	if (ink.y > rect.y) ink.y = rect.y;
	ink.height = tx - ink.y;

	rotate_box(&rect, mp); // What gdk_draw_layout() uses
	rotate_box(&ink, mp); // What one should use
	tx = PAD_SIZE - ink.x + rect.x;
	ty = PAD_SIZE - ink.y + rect.y;
	width = ink.width + PAD_SIZE * 2;
	height = ink.height + PAD_SIZE * 2;

#if GTK_MAJOR_VERSION == 3
	text_pixmap = cairo_image_surface_create(CAIRO_FORMAT_RGB24, width, height);

	cr = cairo_create(text_pixmap);
	cairo_translate(cr, tx - rect.x, ty - rect.y);
	if (mp)
	{
		cm.xx = mp->xx;
		cm.yx = mp->yx;
		cm.xy = mp->xy;
		cm.yy = mp->yy;
		cm.x0 = mp->x0;
		cm.y0 = mp->y0;
		cairo_transform(cr, &cm);
	}
	cairo_set_source_rgb(cr, 1, 1, 1);
	pango_cairo_show_layout(cr, layout);
	cairo_destroy(cr);
#else
	text_pixmap = gdk_pixmap_new(widget->window, width, height, -1);

	gdk_draw_rectangle(text_pixmap, widget->style->black_gc, TRUE, 0, 0, width, height);
	gdk_draw_layout(text_pixmap, widget->style->white_gc, tx, ty, layout);
#endif

	g_object_unref(layout);
	g_object_unref(context);

#else /* #if GTK_MAJOR_VERSION == 1 */

	GdkFont *t_font = gdk_font_load(td->font);
	int lbearing, rbearing, f_width, ascent, descent;


	gdk_string_extents(t_font, td->text,
		&lbearing, &rbearing, &f_width, &ascent, &descent);

	width = rbearing - lbearing + PAD_SIZE * 2;
	height = ascent + descent + PAD_SIZE * 2;

	text_pixmap = gdk_pixmap_new(widget->window, width, height, -1);
	gdk_draw_rectangle(text_pixmap, widget->style->black_gc, TRUE, 0, 0, width, height);
	gdk_draw_string(text_pixmap, t_font, widget->style->white_gc,
			PAD_SIZE - lbearing, ascent + PAD_SIZE, td->text);

	gdk_font_unref(t_font);

#endif

	buf = malloc(width * height * 3);
	if (buf) have_rgb = !!wj_get_rgb_image(gtk_widget_get_window(widget),
		text_pixmap, buf, 0, 0, width, height);
	// REMOVE PIXMAP
#if GTK_MAJOR_VERSION == 3
	cairo_surface_destroy(text_pixmap);
#else
	gdk_pixmap_unref(text_pixmap);
#endif

	memset(&td->ctx, 0, sizeof(td->ctx));
	if (!have_rgb) free(buf);
	else
	{
		td->ctx.xy[2] = width;
		td->ctx.xy[3] = height;
		td->ctx.rgb = buf;
	}
}

//	Mouse handling

typedef struct {
	void *slot[EV_SIZE];
	mouse_ext *mouse;
	int vport[4];
} mouse_den;

static int do_evt_mouse(void **slot, void *event, mouse_ext *mouse)
{
	static void *ev_MOUSE = WBh(EV_MOUSE, 0);
	void **orig = origin_slot(slot);
	void **base = slot[0], **desc = slot[1];
	mouse_den den = { EVSLOT(ev_MOUSE, orig, event), mouse, { 0, 0, 0, 0 } };
	int op = GET_OP(orig);

#if GTK_MAJOR_VERSION >= 2
	if ((((int)desc[0] & WB_OPMASK) >= op_EVT_XMOUSE0) && tablet_device)
	{
		gdouble pressure = 1.0;
		gdk_event_get_axis(event, GDK_AXIS_PRESSURE, &pressure);
		mouse->pressure = (int)(pressure * MAX_PRESSURE + 0.5);
	}
#endif
	if ((op == op_CANVASIMG) || (op == op_CANVASIMGB) || (op == op_CANVAS))
	{
		wjcanvas_get_vport(orig[0], den.vport);
		den.mouse->x += den.vport[0];
		den.mouse->y += den.vport[1];
	}

	return (((evtxr_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, den.slot, mouse));
}

/* !!! After a drop, gtk_drag_end() sends to drag source a fake release event
 * with coordinates (0, 0); remember to ignore them where necessary - WJ */
static gboolean get_evt_mouse(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	mouse_ext mouse = {
		event->x, event->y, event->button,
		event->type == GDK_BUTTON_PRESS ? 1 :
		event->type == GDK_2BUTTON_PRESS ? 2 :
		event->type == GDK_3BUTTON_PRESS ? 3 :
		event->type == GDK_BUTTON_RELEASE ? -1 : 0,
		event->state, MAX_PRESSURE };
#if GTK_MAJOR_VERSION == 1
	if (GET_OP((void **)user_data) >= op_EVT_XMOUSE0)
		mouse.pressure = (int)(event->pressure * MAX_PRESSURE + 0.5);
#endif
	return (do_evt_mouse(user_data, event, &mouse));
}

// This peculiar encoding is historically used throughout mtPaint
static inline int state_to_button(unsigned int state)
{
	return ((state & _B13mask) == _B13mask ? 13 :
		state & _B1mask ? 1 :
		state & _B3mask ? 3 :
		state & _B2mask ? 2 : 0);
}

static gboolean get_evt_mmouse(GtkWidget *widget, GdkEventMotion *event,
	gpointer user_data)
{
	mouse_ext mouse;

	mouse.pressure = MAX_PRESSURE;
#if GTK_MAJOR_VERSION == 1
	if (GET_OP((void **)user_data) >= op_EVT_XMOUSE0)
	{
		gdouble pressure = event->pressure;
		if (event->is_hint) gdk_input_window_get_pointer(event->window,
			event->deviceid, NULL, NULL, &pressure, NULL, NULL, NULL);
		mouse.pressure = (int)(pressure * MAX_PRESSURE + 0.5);
	}
#endif
	while (TRUE)
	{
#if GTK_MAJOR_VERSION == 3
		if (event->is_hint)
		{
			gdk_event_request_motions(event);
			if (GET_OP((void **)user_data) < op_EVT_XMOUSE0)
			{
				gdk_window_get_device_position(event->window,
					event->device, &mouse.x, &mouse.y, &mouse.state);
				break;
			}
		}
#else /* #if GTK_MAJOR_VERSION <= 2 */
		if (!event->is_hint);
#if GTK_MAJOR_VERSION == 2
		else if (GET_OP((void **)user_data) >= op_EVT_XMOUSE0)
		{
			gdk_device_get_state(event->device, event->window,
				NULL, &mouse.state);
		}
#endif
		else
		{
			gdk_window_get_pointer(event->window,
				&mouse.x, &mouse.y, &mouse.state);
			break;
		}
#endif /* GTK+1&2 */
		mouse.x = event->x;
		mouse.y = event->y;
		mouse.state = event->state;
		break;
	}

	mouse.button = state_to_button(mouse.state);

	mouse.count = 0; // motion

	return (do_evt_mouse(user_data, event, &mouse));
}

static void enable_events(void **slot, int op)
{
#if GTK_MAJOR_VERSION == 3
	/* Let's be granular, here */
	GdkEventMask m;
	switch (op)
	{
	case op_EVT_KEY: m = GDK_KEY_PRESS_MASK | GDK_KEY_RELEASE_MASK; break;
	/* Button release goes where button press went, b/c automatic grab */
	case op_EVT_MOUSE: case op_EVT_RMOUSE:
	case op_EVT_XMOUSE: case op_EVT_RXMOUSE:
		 m = GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK; break;
	case op_EVT_MMOUSE: case op_EVT_MXMOUSE: m = GDK_BUTTON_PRESS_MASK |
		GDK_BUTTON_RELEASE_MASK | GDK_POINTER_MOTION_MASK; break;
	case op_EVT_CROSS: m = GDK_ENTER_NOTIFY_MASK | GDK_LEAVE_NOTIFY_MASK; break;
	case op_EVT_SCROLL: m = GDK_SCROLL_MASK; break;
	default: return;
	}
	gtk_widget_add_events(*slot, m);
#else /* #if GTK_MAJOR_VERSION <= 2 */
	if ((op >= op_EVT_MOUSE) && (op <= op_EVT_RXMOUSE))
	{
		if (op >= op_EVT_XMOUSE0) gtk_widget_set_extension_events(*slot,
			GDK_EXTENSION_EVENTS_CURSOR);
#if GTK_MAJOR_VERSION == 1
		if (GTK_WIDGET_NO_WINDOW(*slot)) return;
#endif
	}
	else if (op != op_EVT_CROSS) return; // Ignore op_EVT_KEY & op_EVT_SCROLL
	/* No granularity at all, but it was always this way, and it worked */
	gtk_widget_set_events(*slot, GDK_ALL_EVENTS_MASK);
#endif /* GTK+1&2 */
}

// !!! With GCC inlining this, weird size fluctuations can happen. Or not.
static void add_mouse(void **r, int op)
{
	static const char *sn[3] = { "button_press_event",
		"motion_notify_event", "button_release_event" };
	void **slot = origin_slot(PREV_SLOT(r));
	int cw = op - op_EVT_MOUSE + (op >= op_EVT_XMOUSE0);

	gtk_signal_connect(GTK_OBJECT(*slot), sn[cw & 3],
		cw & 1 ? GTK_SIGNAL_FUNC(get_evt_mmouse) :
		GTK_SIGNAL_FUNC(get_evt_mouse), r);
	enable_events(slot, op);
}

static gboolean get_evt_cross(GtkWidget *widget, GdkEventCrossing *event,
	gpointer user_data)
{
	void **slot, **base, **desc;

	/* Skip grab/ungrab related events */
	if (event->mode != GDK_CROSSING_NORMAL) return (FALSE);
	
	slot = user_data; base = slot[0]; desc = slot[1];
	((evtx_fn)desc[1])(GET_DDATA(base), base, (int)desc[0] & WB_OPMASK, slot,
		(void *)(event->type == GDK_ENTER_NOTIFY));

	return (FALSE); // let it propagate
}

#if GTK_MAJOR_VERSION >= 2

static gboolean get_evt_scroll(GtkWidget *widget, GdkEventScroll *event,
	gpointer user_data)
{
	void **slot = user_data, **base = slot[0], **desc = slot[1];
	scroll_ext scroll = { event->direction == GDK_SCROLL_LEFT ? -1 :
		event->direction == GDK_SCROLL_RIGHT ? 1 : 0,
		event->direction == GDK_SCROLL_UP ? -1 :
		event->direction == GDK_SCROLL_DOWN ? 1 : 0,
		event->state };

	((evtx_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot, &scroll);

	if (!(scroll.xscroll | scroll.yscroll)) return (TRUE);
	event->direction = scroll.xscroll < 0 ? GDK_SCROLL_LEFT :
		scroll.xscroll > 0 ? GDK_SCROLL_RIGHT :
		scroll.yscroll < 0 ? GDK_SCROLL_UP :
		/* scroll.yscroll > 0 ? */ GDK_SCROLL_DOWN;
	return (FALSE);
}
#endif

//	Drag handling

typedef struct {
	int n;
	clipform_dd *src;
	GtkTargetList *targets;
	GtkTargetEntry ent[1];
} clipform_data;

typedef struct {
	int x, y, may_drag;
	int color;
	void **r;
	clipform_data *cd;
} drag_ctx;

typedef struct {
	GdkDragContext *drag_context;
	GtkSelectionData *data;
	guint info;
	guint time;
} drag_sel;

typedef struct {
	void *slot[EV_SIZE];
	drag_sel *ds;
	drag_ctx *dc;
	drag_ext d;
} drag_den;

static int drag_event(drag_sel *ds, drag_ctx *dc)
{
	static void *ev_DRAGFROM = WBh(EV_DRAGFROM, 0);
	void **slot = dc->r, **orig = origin_slot(slot);
	void **base = slot[0], **desc = slot[1];
	drag_den den = { EVSLOT(ev_DRAGFROM, orig, den.slot), ds, dc };

	/* While technically, drag starts at current position, I use the saved
	 * one - where user had clicked to begin drag - WJ */
	// !!! Struct not stable yet, so init fields one by one
	memset(&den.d, 0, sizeof(den.d));
	den.d.x = dc->x;
	den.d.y = dc->y;
	den.d.format = ds ? dc->cd->src + ds->info : NULL;

	return (((evtxr_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, den.slot, &den.d));
}

#define RGB_DND_W 48
#define RGB_DND_H 32

static void set_drag_icon(GdkDragContext *context, GtkWidget *src, int rgb)
{
#if GTK_MAJOR_VERSION == 3
	GdkPixbuf *p = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, RGB_DND_W, RGB_DND_H);
	gdk_pixbuf_fill(p, ((unsigned)rgb << 8) + 0xFF);
	gtk_drag_set_icon_pixbuf(context, p, -2, -2);
	g_object_unref(p);
#else /* #if GTK_MAJOR_VERSION <= 2 */
	GdkGCValues sv;
	GdkPixmap *swatch;

	swatch = gdk_pixmap_new(src->window, RGB_DND_W, RGB_DND_H, -1);
	gdk_gc_get_values(src->style->black_gc, &sv);
	gdk_rgb_gc_set_foreground(src->style->black_gc, rgb);
	gdk_draw_rectangle(swatch, src->style->black_gc, TRUE, 0, 0,
		RGB_DND_W, RGB_DND_H);
	gdk_gc_set_foreground(src->style->black_gc, &sv.foreground);
	gtk_drag_set_icon_pixmap(context, gtk_widget_get_colormap(src),
		swatch, NULL, -2, -2);
	gdk_pixmap_unref(swatch);
#endif /* GTK+1&2 */
}

/* !!! In GTK+3 icon must be set in "drag_begin", setting it in try_start_drag()
 * somehow only sets it for not-valid-target case - WJ */
static void begin_drag(GtkWidget *widget, GdkDragContext *context, gpointer user_data)
{
	drag_ctx *dc = user_data;
	if (dc->color >= 0) set_drag_icon(context, widget, dc->color);
}

static int try_start_drag(GtkWidget *widget, GdkEvent *event, gpointer user_data)
{
	drag_ctx *dc = user_data;
	int rx, ry;
	GdkModifierType state;

	if (event->type == GDK_BUTTON_PRESS)
	{
		if (event->button.button == 1) // Only left button inits drag
		{
			dc->x = event->button.x;
			dc->y = event->button.y;
			dc->may_drag = TRUE;
		}
	}
	else if (event->type == GDK_BUTTON_RELEASE)
	{
		if (event->button.button == 1) dc->may_drag = FALSE;
	}
	else if (event->type == GDK_MOTION_NOTIFY)
	{
		if (event->motion.is_hint)
#if GTK_MAJOR_VERSION == 3
			gdk_window_get_device_position(event->motion.window,
				event->motion.device, &rx, &ry, &state);
#else /* if GTK_MAJOR_VERSION <= 2 */
			gdk_window_get_pointer(event->motion.window, &rx, &ry, &state);
#endif
		else
		{
			rx = event->motion.x;
			ry = event->motion.y;
			state = event->motion.state;
		}
		/* May init drag */
		if (state & GDK_BUTTON1_MASK)
		{
			/* No dragging without clicking on the widget first */
			if (dc->may_drag &&
#if GTK_MAJOR_VERSION == 1
				((abs(rx - dc->x) > 3) ||
				(abs(ry - dc->y) > 3))
#else /* if GTK_MAJOR_VERSION >= 2 */
				gtk_drag_check_threshold(widget,
					dc->x, dc->y, rx, ry)
#endif
			) /* Initiate drag */
			{
				dc->may_drag = FALSE;
				/* Call handler so it can decide if it wants
				 * this drag, and maybe set icon color */
				dc->color = -1;
				if (!drag_event(NULL, dc)) return (TRUE); // no drag
#if GTK_MAJOR_VERSION == 3
				gtk_drag_begin_with_coordinates(widget,
					dc->cd->targets, GDK_ACTION_COPY |
					GDK_ACTION_MOVE, 1, event, -1, -1);
#else /* if GTK_MAJOR_VERSION <= 2 */
				gtk_drag_begin(widget, dc->cd->targets,
					GDK_ACTION_COPY | GDK_ACTION_MOVE, 1, event);
#endif
				return (TRUE);
			}
		}
		else dc->may_drag = FALSE; // Release events can be lost
	}
	return (FALSE);
}

static void get_evt_drag(GtkWidget *widget, GdkDragContext *drag_context,
	GtkSelectionData *data, guint info, guint time, gpointer user_data)
{
	drag_sel ds = { drag_context, data, info, time };
	drag_event(&ds, user_data);
}

static void fcimage_rxy(void **slot, int *xy);

static void get_evt_drop(GtkWidget *widget, GdkDragContext *drag_context,
	gint x,	gint y,	GtkSelectionData *data,	guint info, guint time,
	gpointer user_data)
{
	void **dd = user_data, **orig = origin_slot(dd);
	void **slot, **base, **desc;
	clipform_data *cd = *dd; // from DRAGDROP slot
	int res, op = GET_OP(orig), xy[2] = { x, y };
	drag_ext dx;

#if GTK_MAJOR_VERSION == 1
	/* GTK+1 provides window-relative coordinates, not widget-relative */
	if (GTK_WIDGET_NO_WINDOW(*orig))
	{
		xy[0] -= GTK_WIDGET(*orig)->allocation.x;
		xy[1] -= GTK_WIDGET(*orig)->allocation.y;
	}
#endif
	/* Map widget-relative coordinates to inner window */
	if (op == op_FCIMAGEP) fcimage_rxy(orig, xy);

	// !!! Struct not stable yet, so init fields one by one
	memset(&dx, 0, sizeof(dx));
	dx.x = xy[0];
	dx.y = xy[1];
	dx.format = cd->src + info;
	dx.data = (void *)gtk_selection_data_get_data(data);
	dx.len = gtk_selection_data_get_length(data);

	/* Selection data format isn't checked because it's how GTK+2 does it,
	 * reportedly to ignore a bug in (some versions of) KDE - WJ */
	res = dx.format->size ? dx.len == dx.format->size : dx.len >= 0;

	slot = SLOT_N(dd, 2); // from DRAGDROP to its EVT_DROP
	base = slot[0]; desc = slot[1];
	((evtx_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot, &dx);

	if (GET_DESCV(dd, 2)) // Accept move as a copy (disallow deleting source)
		gtk_drag_finish(drag_context, res, FALSE, time);
}

static gboolean tried_drop(GtkWidget *widget, GdkDragContext *context,
	gint x, gint y, guint time, gpointer user_data)
{
	GdkAtom target;
	/* Check if drop could provide a supported format */
#if GTK_MAJOR_VERSION == 1
	clipform_data *cd = user_data;
	GList *dest, *src;
	gpointer tp;

	for (dest = cd->targets->list; dest; dest = dest->next)
	{
		target = ((GtkTargetPair *)dest->data)->target;
		tp = GUINT_TO_POINTER(target);
		for (src = context->targets; src && (src->data != tp); src = src->next);
		if (src) break;
	}
	if (!dest) return (FALSE);
#else /* if GTK_MAJOR_VERSION >= 2 */
	target = gtk_drag_dest_find_target(widget, context, NULL);
	if (target == GDK_NONE) return (FALSE);
#endif

	/* Trigger "drag_data_received" event */
	gtk_drag_get_data(widget, context, target, time);
	return (TRUE);
}

void *dragdrop(void **r)
{
	void *v = r[0], **pp = r[1], **slot = origin_slot(PREV_SLOT(r));
	clipform_data *cd = **(void ***)v;

	if (pp[4]) // Have drag handler
	{
		drag_ctx *dc = r[2];
		dc->r = NEXT_SLOT(r);
		dc->cd = cd;
		gtk_signal_connect(GTK_OBJECT(*slot), "button_press_event",
			GTK_SIGNAL_FUNC(try_start_drag), dc);
		gtk_signal_connect(GTK_OBJECT(*slot), "motion_notify_event",
			GTK_SIGNAL_FUNC(try_start_drag), dc);
		gtk_signal_connect(GTK_OBJECT(*slot), "button_release_event",
			GTK_SIGNAL_FUNC(try_start_drag), dc);
		gtk_signal_connect(GTK_OBJECT(*slot), "drag_begin",
			GTK_SIGNAL_FUNC(begin_drag), dc);
		gtk_signal_connect(GTK_OBJECT(*slot), "drag_data_get",
			GTK_SIGNAL_FUNC(get_evt_drag), dc);
	}
	if (pp[6]) // Have drop handler
	{
		int dmode = GTK_DEST_DEFAULT_HIGHLIGHT |
			GTK_DEST_DEFAULT_MOTION;
		if (!pp[2]) dmode |= GTK_DEST_DEFAULT_DROP;
		else gtk_signal_connect(GTK_OBJECT(*slot), "drag_drop",
			GTK_SIGNAL_FUNC(tried_drop), cd);

		gtk_drag_dest_set(*slot, dmode, cd->ent, cd->n, pp[2] ?
			GDK_ACTION_COPY | GDK_ACTION_MOVE : GDK_ACTION_COPY);
		gtk_signal_connect(GTK_OBJECT(*slot), "drag_data_received",
			GTK_SIGNAL_FUNC(get_evt_drop), r);
	}
	return (cd); // For drop handler
}

//	Clipboard handling

typedef struct {
	void *slot[EV_SIZE];
	GtkSelectionData *data;
	guint info;
	clipform_data *cd;
	copy_ext c;
} copy_den;

static void clip_evt(GtkSelectionData *sel, guint info, void **slot)
{
	static void *ev_COPY = WBh(EV_COPY, 0);
	void **base, **desc;
	clipform_data *cd = slot[0];
	copy_den den = { EVSLOT(ev_COPY, slot, den.slot), sel, info, cd,
		{ sel ? cd->src + info : NULL, NULL, 0 } };

	slot = NEXT_SLOT(slot); // from CLIPBOARD to EVT_COPY
	base = slot[0]; desc = slot[1];
	((evtx_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, den.slot, &den.c);
}

static int paste_evt(GtkSelectionData *sel, clipform_dd *format, void **slot)
{
	void **base = slot[0], **desc = slot[1];
	copy_ext c = { format, (void *)gtk_selection_data_get_data(sel),
		gtk_selection_data_get_length(sel) };

	return (((evtxr_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot, &c));
}

static clipform_dd *clip_format(GtkSelectionData *sel, clipform_data *cd)
{
	GdkAtom *targets;
	int i, n = gtk_selection_data_get_length(sel) / sizeof(GdkAtom);

	if ((n > 0) && (gtk_selection_data_get_format(sel) == 32) &&
		(gtk_selection_data_get_data_type(sel) == GDK_SELECTION_TYPE_ATOM))
	{
#if GTK_MAJOR_VERSION == 3
		guint res, res0 = G_MAXUINT;

		targets = (GdkAtom *)gtk_selection_data_get_data(sel);
#if 0 /* #ifdef GDK_WINDOWING_QUARTZ */
		/* !!! For debugging */
		g_print("%d targets:\n", n);
		for (i = 0; i < n; i++) g_print("%s\n", gdk_atom_name(targets[i]));
#endif
		/* Need to scan for each target, to return the earliest slot in
		 * the array in case more than one match */
		for (i = 0; i < n; i++)
			if (gtk_target_list_find(cd->targets, targets[i], &res) &&
				(res < res0)) res0 = res;
		// Return the matching format
		if (res0 < G_MAXUINT) return (cd->src + res0);
#else /* if GTK_MAJOR_VERSION <= 2 */
		GList *dest;
		GdkAtom target;

		targets = (GdkAtom *)gtk_selection_data_get_data(sel);
		for (dest = cd->targets->list; dest; dest = dest->next)
		{
			target = ((GtkTargetPair *)dest->data)->target;
			for (i = 0; (i < n) && (targets[i] != target); i++);
			if (i < n) break;
		}
		// Return the matching format
		if (dest) return (cd->src + ((GtkTargetPair *)dest->data)->info);
#endif
	}
	return (NULL);
}

#if GTK_MAJOR_VERSION == 1

#define CLIPMASK 3 /* 2 clipboards */

enum {
	TEXT_STRING = 0,
	TEXT_TEXT,
	TEXT_COMPOUND,
	TEXT_CNT
};

static GtkTargetEntry text_targets[] = {
	{ "STRING", 0, TEXT_STRING },
	{ "TEXT", 0, TEXT_TEXT }, 
	{ "COMPOUND_TEXT", 0, TEXT_COMPOUND }
};
static GtkTargetList *text_tlist;

typedef struct {
	char *what;
	int where;
} text_offer;
static text_offer text_on_offer[2];

static void clear_text_offer(int nw)
{
	int i;

	for (i = 0; i < 2; i++)
	{
		if (text_on_offer[i].where &= ~nw) continue;
		free(text_on_offer[i].what);
		text_on_offer[i].what = NULL;
	}
}

static void selection_callback(GtkWidget *widget, GtkSelectionData *sel,
	guint time, gpointer user_data)
{
	void **res = user_data;
	if (!res[1]) res[1] = clip_format(sel, res[0]); // check
	else if ((sel->length < 0) || !paste_evt(sel, res[1], res[0])) // paste
		res[1] = NULL; // fail
	res[0] = NULL; // done
}

static int process_clipboard(void **slot)
{
	clipform_data *cd = slot[0];
	void **desc = slot[1];
	void *res[2] = { NULL, NULL };
	clipform_dd *format;
	GdkAtom sel;
	guint sig = 0;
	int which, nw = (int)desc[2] & CLIPMASK;

	for (which = 0; (1 << which) <= nw; which++)
	{
		if (!((1 << which) & nw)) continue;
		// If we're who put data there
// !!! No check if same program but another clipboard - no need for now
		if (internal_clipboard(which)) continue;
		sel = gdk_atom_intern(which ? "PRIMARY" : "CLIPBOARD", FALSE);

		res[0] = cd; res[1] = NULL;
		if (!sig) sig = gtk_signal_connect(GTK_OBJECT(main_window),
			"selection_received",
			GTK_SIGNAL_FUNC(selection_callback), &res);
		/* First, deciding on format... */
		gtk_selection_convert(main_window, sel,
			gdk_atom_intern("TARGETS", FALSE), GDK_CURRENT_TIME);
		while (res[0]) gtk_main_iteration();
		if (!(format = res[1])) continue; // no luck
		/* ...then, requesting the format */
		res[0] = SLOT_N(slot, 2); // from CLIPBOARD to EVT_PASTE
		gtk_selection_convert(main_window, sel,
			gdk_atom_intern(format->target, FALSE), GDK_CURRENT_TIME);
		while (res[0]) gtk_main_iteration();
		if (res[1]) break; // success
	}
	if (sig) gtk_signal_disconnect(GTK_OBJECT(main_window), sig);

	return (!!res[1]);
}

static void selection_get_callback(GtkWidget *widget, GtkSelectionData *sel,
	guint info, guint time, gpointer user_data)
{
	void **slot = g_dataset_get_data(main_window,
		gdk_atom_name(sel->selection));
	/* Text clipboard is handled right here */
	if (slot == (void *)text_on_offer)
	{
		char *s;
		int i, which;

		which = sel->selection == gdk_atom_intern("PRIMARY", FALSE) ? 2 : 1;
		for (i = 0; i < 2; i++) if (text_on_offer[i].where & which) break;
		if (i > 1) return; // No text for this clipboard
		s = text_on_offer[i].what;
		if (!s) return; // Paranoia
		if (info == TEXT_STRING)
			gtk_selection_data_set(sel, sel->target, 8, s, strlen(s));
		else if ((info == TEXT_TEXT) || (info == TEXT_COMPOUND))
		{
			guchar *ns;
			GdkAtom target;
			gint format, len;
			gdk_string_to_compound_text(s, &target, &format, &ns, &len);
			gtk_selection_data_set(sel, target, format, ns, len);
			gdk_free_compound_text(ns);
		}
	}
	/* Other kinds get handled outside */
	else if (slot) clip_evt(sel, info, slot);
}

static void selection_clear_callback(GtkWidget *widget, GdkEventSelection *event,
	gpointer user_data)
{
	void **slot = g_dataset_get_data(main_window,
		gdk_atom_name(event->selection));
	if (slot == (void *)text_on_offer) clear_text_offer(
		event->selection == gdk_atom_intern("PRIMARY", FALSE) ? 2 : 1);
	else if (slot) clip_evt(NULL, 0, slot);
}

// !!! GTK+ 1.2 internal type (gtk/gtkselection.c)
typedef struct {
	GdkAtom selection;
	GtkTargetList *list;
} GtkSelectionTargetList;

static int offer_clipboard(void **slot, int text)
{
	static int connected;
	void **desc = slot[1];
	clipform_data *cd = slot[0];
	GtkTargetEntry *targets = cd->ent;
	gpointer info = slot;
	GtkSelectionTargetList *slist;
	GList *list, *tmp;
	GdkAtom sel;
	int which, res = FALSE, nw = (int)desc[2] & CLIPMASK, ntargets = cd->n;


	if (text) targets = text_targets , ntargets = TEXT_CNT , info = text_on_offer;
	for (which = 0; (1 << which) <= nw; which++)
	{
		if (!((1 << which) & nw)) continue;
		sel = gdk_atom_intern(which ? "PRIMARY" : "CLIPBOARD", FALSE);
		if (!gtk_selection_owner_set(main_window, sel, GDK_CURRENT_TIME))
			continue;

		/* Don't have gtk_selection_clear_targets() in GTK+1 - have to
		 * reimplement */
		list = gtk_object_get_data(GTK_OBJECT(main_window),
			"gtk-selection-handlers");
		for (tmp = list; tmp; tmp = tmp->next)
		{
			if ((slist = tmp->data)->selection != sel) continue;
			list = g_list_remove_link(list, tmp);
			gtk_target_list_unref(slist->list);
			g_free(slist);
			break;
		}
		gtk_object_set_data(GTK_OBJECT(main_window),
			"gtk-selection-handlers", list);

		// !!! Have to resort to this to allow for multiple X clipboards
		g_dataset_set_data(main_window, which ? "PRIMARY" : "CLIPBOARD", info);
		if (!connected)
		{
			gtk_signal_connect(GTK_OBJECT(main_window), "selection_get",
				GTK_SIGNAL_FUNC(selection_get_callback), NULL);
			gtk_signal_connect(GTK_OBJECT(main_window), "selection_clear_event",
				GTK_SIGNAL_FUNC(selection_clear_callback), NULL);
			connected = TRUE;
		}

		gtk_selection_add_targets(main_window, sel, targets, ntargets);
		res = TRUE;
	}

	return (res);
}

static void offer_text(void **slot, char *s)
{
	void **desc = slot[1];
	int i, nw = (int)desc[2] & CLIPMASK;

	clear_text_offer(nw);
	i = !!text_on_offer[0].what;
	text_on_offer[i].what = strdup(s);
	text_on_offer[i].where = nw;

	if (!text_tlist) text_tlist = gtk_target_list_new(text_targets, TEXT_CNT);

	offer_clipboard(slot, TRUE);
}

#else /* if GTK_MAJOR_VERSION >= 2 */

#ifdef GDK_WINDOWING_X11
#define CLIPMASK 3 /* 2 clipboards */
#else
#define CLIPMASK 1 /* 1 clipboard */
#endif

/* While GTK+2 allows for synchronous clipboard handling, it's implemented
 * through copying the entire clipboard data - and when the data might be
 * a huge image which mtPaint is bound to allocate *yet again*, this would be
 * asking for trouble - WJ */

static void clip_paste(GtkClipboard *clipboard, GtkSelectionData *sel,
	gpointer user_data)
{
	void **res = user_data;
	if ((gtk_selection_data_get_length(sel) < 0) ||
		!paste_evt(sel, res[1], res[0])) res[1] = NULL; // fail
	res[0] = NULL; // done
}

static void clip_check(GtkClipboard *clipboard, GtkSelectionData *sel,
	gpointer user_data)
{
	void **res = user_data;
	res[1] = clip_format(sel, res[0]);
	res[0] = NULL; // done
}

static int process_clipboard(void **slot)
{
	GtkClipboard *clip;
	clipform_data *cd = slot[0];
	void **desc = slot[1];
	int which, nw = (int)desc[2] & CLIPMASK;

	for (which = 0; (1 << which) <= nw; which++)
	{
		if (!((1 << which) & nw)) continue;
		// If we're who put data there
// !!! No check if same program but another clipboard - no need for now
		if (internal_clipboard(which)) continue;
		clip = gtk_clipboard_get(which ? GDK_SELECTION_PRIMARY :
			GDK_SELECTION_CLIPBOARD);
		{
			void *res[] = { cd, NULL };
			clipform_dd *format;
			/* First, deciding on format... */
			gtk_clipboard_request_contents(clip,
				gdk_atom_intern("TARGETS", FALSE),
				clip_check, res);
			while (res[0]) gtk_main_iteration();
			if (!(format = res[1])) continue; // no luck
			/* ...then, requesting the format */
			res[0] = SLOT_N(slot, 2); // from CLIPBOARD to EVT_PASTE
			gtk_clipboard_request_contents(clip,
				gdk_atom_intern(format->target, FALSE),
				clip_paste, res);
			while (res[0]) gtk_main_iteration();
			if (res[1]) return (TRUE); // success
		}
	}
	return (FALSE);
}

#ifdef GDK_WINDOWING_QUARTZ

/* GTK+/Quartz has a dummy stub for gdk_selection_owner_get(), so needs a
 * workaround to have a working internal_clipboard() */

static void* slot_on_offer;

static void clip_copy(GtkClipboard *clipboard, GtkSelectionData *sel, guint info,
	gpointer user_data)
{
	clip_evt(sel, info, slot_on_offer);
}

static void clip_clear(GtkClipboard *clipboard, gpointer user_data)
{
	clip_evt(NULL, 0, slot_on_offer);
}

static int offer_clipboard(void **slot, int unused)
{
	void **desc = slot[1];
	clipform_data *cd = slot[0];
	int i, res = FALSE;

	if ((int)desc[2] & CLIPMASK) // Paranoia
	// Two attempts, for GTK+ function can fail for strange reasons
	for (i = 0; i < 2; i++)
	{
		if (!gtk_clipboard_set_with_owner(gtk_clipboard_get(
			GDK_SELECTION_CLIPBOARD), cd->ent, cd->n,
			clip_copy, clip_clear, G_OBJECT(main_window))) continue;
		slot_on_offer = slot;
		res = TRUE;
		break;
	}
	return (res);
}

#else /* Anything but Quartz */

static void clip_copy(GtkClipboard *clipboard, GtkSelectionData *sel, guint info,
	gpointer user_data)
{
	clip_evt(sel, info, user_data);
}

static void clip_clear(GtkClipboard *clipboard, gpointer user_data)
{
	clip_evt(NULL, 0, user_data);
}

static int offer_clipboard(void **slot, int unused)
{
	void **desc = slot[1];
	clipform_data *cd = slot[0];
	int i, which, res = FALSE, nw = (int)desc[2] & CLIPMASK;

	for (which = 0; (1 << which) <= nw; which++)
	{
		if (!((1 << which) & nw)) continue;
		// Two attempts, for GTK+2 function can fail for strange reasons
		for (i = 0; i < 2; i++)
		{
			if (!gtk_clipboard_set_with_data(gtk_clipboard_get(
				which ? GDK_SELECTION_PRIMARY :
				GDK_SELECTION_CLIPBOARD), cd->ent, cd->n,
				clip_copy, clip_clear, slot)) continue;
			res = TRUE;
			break;
		}
	}
	return (res);
}

#endif

static void offer_text(void **slot, char *s)
{
	void **desc = slot[1];
	int which, nw = (int)desc[2] & CLIPMASK;

	for (which = 0; (1 << which) <= nw; which++)
	{
		if (!((1 << which) & nw)) continue;
		gtk_clipboard_set_text(gtk_clipboard_get(which ?
			GDK_SELECTION_PRIMARY : GDK_SELECTION_CLIPBOARD), s, -1);
	}
}

#endif

#undef CLIPMASK

//	Keynames handling

#if GTK_MAJOR_VERSION == 1
#include <ctype.h>
#endif

static void get_keyname(char *buf, int buflen, guint key, guint state, int ui)
{
	char tbuf[128], *name, *m1, *m2, *m3, *sp;
	guint u;

	if (!ui) /* For inifile */
	{
		m1 = state & _Cmask ? "C" : "";
		m2 = state & _Smask ? "S" : "";
		m3 = state & _Amask ? "A" : "";
		sp = "_";
		name = gdk_keyval_name(key);
	}
	else /* For display */
	{
		m1 = state & _Smask ? "Shift+" : "";
		m2 = state & _Cmask ? "Ctrl+" : "";
		m3 = state & _Amask ? "Alt+" : "";
		sp = "";
		name = tbuf;
#if GTK_MAJOR_VERSION == 1
		u = key;
#else /* #if GTK_MAJOR_VERSION >= 2 */
		u = gdk_keyval_to_unicode(key);
		if ((u < 0x80) && (u != key)) u = 0; // Alternative key
#endif
		if (u == ' ') name = "Space";
		else if (u == '\\') name = "Backslash";
#if GTK_MAJOR_VERSION == 1
		else if (u < 0x100)
		{
			tbuf[0] = toupper(u);
			tbuf[1] = 0;
		}
#else /* #if GTK_MAJOR_VERSION >= 2 */
		else if (u && g_unichar_isgraph(u))
			tbuf[g_unichar_to_utf8(g_unichar_toupper(u), tbuf)] = 0;
#endif
		else
		{
			char *s = gdk_keyval_name(gdk_keyval_to_lower(key));
			int c, i = 0;

			if (s) while (i < sizeof(tbuf) - 1)
			{
				if (!(c = s[i])) break;
				if (c == '_') c = ' ';
				tbuf[i++] = c;
			}
			tbuf[i] = 0;
			if ((i == 1) && (tbuf[0] >= 'a') && (tbuf[0] <= 'z'))
				tbuf[0] -= 'a' - 'A';
		}
	}
	wjstrcat(buf, buflen, "", 0, m1, m2, m3, sp, name, NULL);
}

static guint parse_keyname(char *name, guint *mod)
{
	guint m = 0;
	char c;

	while (TRUE) switch (c = *name++)
	{
	case 'C': m |= _Cmask; continue;
	case 'S': m |= _Smask; continue;
	case 'A': m |= _Amask; continue;
	case '_':
		*mod = m;
		return (gdk_keyval_from_name(name));
	default: return (KEY(VoidSymbol));
	}
}

#define SEC_KEYNAMES ">keys<" /* Impossible name */

static char *key_name(guint key, guint mod, int section)
{
	char buf[256];
	int nk, ns;

	/* Refer from unique ID to keyval */
	get_keyname(buf, sizeof(buf), key, mod, FALSE);
// !!! Maybe pack state into upper bits, like Qt does?
	nk = ini_setint(&main_ini, section, buf, key);
	/* Refer from display name to ID slot (the names cannot clash) */
	get_keyname(buf, sizeof(buf), key, mod, TRUE);
	ns = ini_setref(&main_ini, section, buf, nk);

	return (INI_KEY(&main_ini, ns));
}

//	Keymap handling

typedef struct {
	guint key, mod, keycode;
	int idx;
} keyinfo;

typedef struct {
	void **slot;
	int section;
	int idx;	// Used for various mapping indices
	guint key, mod;	// Last one labeled on menuitem
} keyslot;

typedef struct {
	GtkAccelGroup *ag;
	void ***res;			// Pointer to response var
	int nkeys, nslots;		// Counters
	int slotsec, keysec;		// Sections
	int updated;			// Already matches INI
	keyinfo map[MAX_KEYS];		// Keys
	keyslot slots[1];		// Widgets (slot #0 stays empty)
} keymap_data;

#define KEYMAP_SIZE(N) (sizeof(keymap_data) + (N) * sizeof(keyslot))

// !!! And with inlining this, problem also
int add_sec_name(memx2 *mem, int base, int sec)
{
	char *s, *stack[CONT_DEPTH];
	int n = 0, h = mem->here;

	while (sec != base)
	{
		s = _(INI_KEY(&main_ini, sec));
		stack[n++] = s + strspn(s, "/");
		sec = INI_PARENT(&main_ini, sec);
	}
	while (n-- > 0)
	{
		addstr(mem, stack[n], 1);
		addchars(mem, '/', 1);
	}
	mem->buf[mem->here - 1] = '\0';
	return (h);
}

static keymap_dd *keymap_export(keymap_data *keymap)
{
	memx2 mem;
	keymap_dd *km;
	keyinfo *kf;
	int lp = sizeof(keymap_dd) + sizeof(char *) * (keymap->nslots + 1);
	int i, j, k, sec;

	memset(&mem, 0, sizeof(mem));
	getmemx2(&mem, lp + sizeof(key_dd) * MAX_KEYS);

	/* First, stuff the names into memory block */
	mem.here = lp;
	for (i = j = 1; i <= keymap->nslots; i++)
	{
		sec = keymap->slots[i].section;
		k = -1;
		if (sec > 0)
		{
			k = add_sec_name(&mem, keymap->slotsec, sec);
			((char **)(mem.buf + sizeof(keymap_dd)))[j] = (char *)k;
			k = j++;
		}
		keymap->slots[i].idx = k;
	}

	lp = mem.here;
	getmemx2(&mem, sizeof(key_dd) * (MAX_KEYS + 1));

	/* Now the block is at its final size, so set the pointers */
	km = (void *)mem.buf;
	km->nslots = j - 1;
	km->nkeys = keymap->nkeys;
	km->maxkeys = MAX_KEYS;
	km->keys = ALIGNED(mem.buf + lp, ALIGNOF(key_dd));
	km->slotnames = (char **)(mem.buf + sizeof(keymap_dd));
	km->slotnames[0] = NULL;
	for (i = 1; i < j; i++)
		km->slotnames[i] = mem.buf + (int)km->slotnames[i];

	/* Now fill the keys table */
	sec = ini_setsection(&main_ini, 0, SEC_KEYNAMES);
	ini_transient(&main_ini, sec, NULL);
	for (kf = keymap->map , i = 0; i < keymap->nkeys; i++ , kf++)
	{
		km->keys[i].slot = keymap->slots[kf->idx].idx;
		km->keys[i].name = key_name(kf->key, kf->mod, sec);
	}

	return (km);
}

static int keymap_add(keymap_data *keymap, void **slot, char *name, int section)
{
	keyslot *ks = keymap->slots + ++keymap->nslots;
	ks->section = !name ? 0 :
		ini_setint(&main_ini, section, name, keymap->nslots);
	ks->slot = slot;
	ks->idx = -1; // No mapping
	return (TRUE);
}

static int keymap_map(keymap_data *keymap, int idx, guint key, guint mod)
{
	keyinfo *kf = keymap->map + keymap->nkeys;

	if (!key || (keymap->nkeys >= MAX_KEYS)) return (FALSE);
	kf->key = key;
	kf->mod = mod;
	kf->idx = idx;
	keymap->nkeys++;
	return (TRUE);
}

static void keymap_to_ini(keymap_data *keymap)
{
	char buf[256];
	keyinfo *ki, *kd;
	int i, sec, w = keymap->keysec, f = keymap->updated;

	kd = ki = keymap->map;
	for (i = keymap->nkeys; i-- > 0; ki++)
	{
		sec = keymap->slots[ki->idx].section;
		if (f && sec) continue;
		get_keyname(buf, sizeof(buf), ki->key, ki->mod, FALSE);
		/* Fixed key - clear the ref, preserve the key */
		if (!sec)
		{
			ini_setref(&main_ini, w, buf, 0);
			*kd++ = *ki;
		}
		ini_getref(&main_ini, w, buf, sec);
	}
	keymap->nkeys = kd - keymap->map;
	keymap->updated = TRUE;
}

static void keymap_update(keymap_data *keymap, keymap_dd *keys)
{
	char *nm;
	guint key, mod;
	int i, j, n, sec, w = keymap->keysec;

	/* Make default keys into INI defaults */
	if (!keymap->updated) keymap_to_ini(keymap);

	/* Import keys into INI */
	if (keys)
	{
		/* Clear everything existing */
		for (i = INI_FIRST(&main_ini, w); i; i = INI_NEXT(&main_ini, i))
			ini_setref(&main_ini, w, INI_KEY(&main_ini, i), 0);

		/* Prepare reverse map */
		for (i = j = 1; i <= keymap->nslots; i++)
			if (keymap->slots[i].section > 0) keymap->slots[j++].idx = i;

		/* Stuff things into inifile */
		sec = ini_setsection(&main_ini, 0, SEC_KEYNAMES);
		for (i = 0; i < keys->nkeys; i++)
		{
			j = keys->keys[i].slot;
			if (j < 0) continue; // Fixed key
			n = ini_getref(&main_ini, sec, keys->keys[i].name, 0);
			if (n <= 0) continue; // Invalid key
			ini_setref(&main_ini, w, INI_KEY(&main_ini, n),
				keymap->slots[keymap->slots[j].idx].section);
		}

		/* Overwrite fixed keys again */
		keymap_to_ini(keymap);
	}

	/* Step through the section reading it in */
	for (i = INI_FIRST(&main_ini, w); i; i = INI_NEXT(&main_ini, i))
	{
		nm = INI_KEY(&main_ini, i);
		key = parse_keyname(nm, &mod);
		if (key == KEY(VoidSymbol)) continue;
		sec = ini_getref(&main_ini, w, nm, 0);
		if (sec <= 0) continue;
		sec = (int)INI_VALUE(&main_ini, sec);
		if ((sec > 0) && (sec <= keymap->nslots))
			keymap_map(keymap, sec, key, mod);
	}
}

#define FAKE_ACCEL "<f>/a/k/e"

// !!! And with inlining this, problem also
void keymap_init(keymap_data *keymap, keymap_dd *keys)
{
	char buf[256], *nm;
	GtkWidget *w;
	keyslot *ks;
	guint key, mod, key0, mod0;
	int i, j, op;

	/* Ensure keys section */
	nm = INI_KEY(&main_ini, keymap->slotsec);
	j = strlen(nm);
	wjstrcat(buf, sizeof(buf), nm, j, " keys" + !j, NULL);
	j = ini_getsection(&main_ini, 0, buf);
	keymap->keysec = ini_setsection(&main_ini, 0, buf);

	/* If nondefault keys are there */
	if (keys || ((j > 0) && !keymap->updated))
		keymap_update(keymap, keys);

	/* Label new key indices on slots - back to forth */
	i = keymap->nslots + 1;
	while (--i > 0) keymap->slots[i].idx = -1;
	i = keymap->nkeys;
	while (i-- > 0) keymap->slots[keymap->map[i].idx].idx = i;

	/* Update keys on menuitems */
	for (ks = keymap->slots + 1 , i = keymap->nslots; i > 0; i-- , ks++)
	{
		key = mod = 0;
		j = ks->idx;
		if (j >= 0) key = keymap->map[j].key , mod = keymap->map[j].mod;
		key0 = ks->key; mod0 = ks->mod;
		ks->key = key; ks->mod = mod; // Update
		if (!((key ^ key0) | (mod ^ mod0))) continue; // Unchanged
		// No matter, if not a menuitem
		op = GET_OP(ks->slot);
		if ((op < op_MENU_0) || (op >= op_MENU_LAST)) continue;
		// Tell widget about update
		w = ks->slot[0];
		if (key0 | mod0) gtk_widget_remove_accelerator(w, keymap->ag,
			key0, mod0);
#if GTK_MAJOR_VERSION >= 2
		/* !!! It has to be there in place of key, for menu spacing */
		gtk_widget_set_accel_path(w, j < 0 ? FAKE_ACCEL : NULL, keymap->ag);
#endif
		if (j >= 0) gtk_widget_add_accelerator(w, "activate", keymap->ag,
			key, mod, GTK_ACCEL_VISIBLE);
	}
}

static void keymap_reset(keymap_data *keymap)
{
	keyinfo *kf = keymap->map;
	int i = keymap->nkeys;

	while (i-- > 0) kf->keycode = keyval_key(kf->key) , kf++;
}

static void keymap_find(keymap_data *keymap, key_ext *key)
{
	keyinfo *kf, *match = NULL;
	int i;

	for (kf = keymap->map , i = keymap->nkeys; i > 0; i-- , kf++)
	{
		/* Modifiers should match first */
		if ((key->state & _CSAmask) != kf->mod) continue;
		/* Let keyval have priority; this is also a workaround for
		 * GTK2 bug #136280 */
		if (key->lowkey == kf->key) break;
		/* Let keycodes match when keyvals don't */
		if (key->realkey == kf->keycode) match = kf;
	}
	/* Keyval match */
	if (i > 0) match = kf;
/* !!! If the starting layout has the keyval+mods combo mapped to one key, and
 * the current layout to another, both will work till "rebind keys" is done.
 * I like this better than shortcuts moving with every layout switch - WJ */
	/* If we have at least a keycode match */
	*(keymap->res) = match ? keymap->slots[match->idx].slot : NULL;
}

//	KEYBUTTON widget

typedef struct {
	int section;
	guint key, mod;
} keybutton_data;

static gboolean convert_key(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	keybutton_data *dt = user_data;
	char buf[256];

	/* Do nothing until button is pressed */
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget)))
		return (FALSE);

	/* Store keypress, display its name */
	if (event->type == GDK_KEY_PRESS)
	{
		get_keyname(buf, sizeof(buf), dt->key = low_key(event),
			dt->mod = event->state, TRUE);
		gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(widget))), buf);
	}
	/* Depress button when key gets released */
	else /* if (event->type == GDK_KEY_RELEASE) */
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);

	return (TRUE);
}

static void add_del(void **r, GtkWidget *window)
{
	gtk_signal_connect(GTK_OBJECT(window), "delete_event",
		GTK_SIGNAL_FUNC(get_evt_del), r);
}

static void **skip_if(void **pp)
{
	int lp, mk;
	void **ifcode;

	ifcode = pp + 1 + (lp = WB_GETLEN((int)*pp));
	if (lp > 1) // skip till corresponding ENDIF
	{
		mk = (int)pp[2];
		while ((((int)*ifcode & WB_OPMASK) != op_ENDIF) ||
			((int)ifcode[1] != mk))
			ifcode += 1 + WB_GETLEN((int)*ifcode);
	}
	return (ifcode + 1 + (WB_GETLEN((int)*ifcode)));
}

/* Trigger events which need triggering */
static void trigger_things(void **wdata)
{
	char *data = GET_DDATA(wdata);
	void **slot, **base, **desc;

	for (wdata = GET_WINDOW(wdata); wdata[1]; wdata = NEXT_SLOT(wdata))
	{
		int opf = GET_OPF(wdata), op = opf & WB_OPMASK;
		if (IS_UNREAL(wdata)) op = GET_UOP(wdata);

		/* Trigger events for nested widget */
		if (op == op_MOUNT)
		{
			if ((slot = wdata[2])) trigger_things(slot);
			continue;
		}
		if (op == op_uMOUNT)
		{
			if ((slot = ((swdata *)*wdata)->strs))
				trigger_things(slot);
			continue;
		}
		/* Prepare preset split widget */
		if ((op == op_HVSPLIT) && WB_GETLEN(opf))
		{
			cmd_set(wdata, (int)GET_DESCV(wdata, 1));
			continue;
		}

		if (op != op_TRIGGER) continue;
		if (!WB_GETLEN(opf)) // Regular version
		{
			slot = PREV_SLOT(wdata);
			while (GET_OP(slot) > op_EVT_LAST) // Find EVT slot
				slot = PREV_SLOT(slot);
			base = slot;
		}
		else // Version for menu/toolbar items
		{
			/* Here, event is put into next slot, and widget is
			 * in nearest widgetlike slot before */
			base = NEXT_SLOT(wdata);
			slot = origin_slot(wdata);
		}
		desc = base[1];
		((evt_fn)desc[1])(data, base[0], (int)desc[0] & WB_OPMASK, slot);
	}
}

#if GTK_MAJOR_VERSION == 3

/* Wait till window is getting drawn to make it user-resizable */
static gboolean make_resizable(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
	gtk_window_set_resizable(GTK_WINDOW(widget), TRUE);
	g_signal_handlers_disconnect_by_func(widget, make_resizable, user_data);
	return (FALSE);
}

/* Reintroduce sanity to sizing wrapped labels */
static void do_rewrap(GtkWidget *widget, gint vert, gint *min, gint *nat,
	gint for_width, gpointer user_data)
{
	PangoLayout *layout;
	PangoRectangle r;
	GtkBorder pb;
	gint xpad;
	int wmax, w, h, ww, sw2, lines, w2l;

//g_print("vert %d for_width %d min %d nat %d\n", vert, for_width, *min, *nat);
	if (vert) return; // Do nothing for vertical size

	/* Max wrap width */
	layout = gtk_widget_create_pango_layout(widget,
		/* Let wrap width be 65 chars */
		"88888888888888888888888888888888"
		"888888888888888888888888888888888");
	pango_layout_get_size(layout, &ww, NULL);
	sw2 = PANGO_SCALE * ((gdk_screen_get_width(gtk_widget_get_screen(widget)) + 1) / 2);
	if (ww > sw2) ww = sw2;

	/* Full text width */
	pango_layout_set_text(layout, gtk_label_get_text(GTK_LABEL(widget)), -1);
	pango_layout_set_alignment(layout, gtk_widget_get_direction(widget) ==
		GTK_TEXT_DIR_RTL ? PANGO_ALIGN_RIGHT : PANGO_ALIGN_LEFT);
	pango_layout_set_wrap(layout, PANGO_WRAP_WORD);
	pango_layout_set_width(layout, -1);
	pango_layout_get_extents(layout, NULL, &r);
	wmax = r.width;

	if (wmax > ww) /* Wrap need happen */
	{
		pango_layout_set_width(layout, ww);
		pango_layout_get_extents(layout, NULL, &r);
		w = r.width;
		h = r.height;

		/* Maybe make narrower, to rebalance lines */
		lines = pango_layout_get_line_count(layout);
		w2l = (wmax + lines - 1) / lines;
		if (w2l < w)
		{
			pango_layout_set_width(layout, w2l);
			pango_layout_get_extents(layout, NULL, &r);
			if (r.height > h) // Extra line(s) got added - make wider
			{
				pango_layout_set_width(layout, (w2l + w) / 2);
				pango_layout_get_extents(layout, NULL, &r);
			}
			if (r.height <= h) w = r.width; // No extra lines happened
		}

		/* Full width in pixels */
		w = w / PANGO_SCALE + 1;
		get_padding_and_border(gtk_widget_get_style_context(widget),
			NULL, NULL, &pb);
		gtk_misc_get_padding(GTK_MISC(widget), &xpad, NULL);
		w += pb.left + pb.right + 2 * xpad;
	}
	else w = *nat; /* Take what label wants */

//g_print("Want nat %d\n", w);
	if (w > *nat) w = *nat; // If widget wants even less
	if (w < *min) w = *min; // Respect the minsize

	/* Reduce natural width to this */
	/* !!! Make minimum and natural width the same: otherwise, bug in GTK+3
	 * size negotiation causes labels be too tall; the longer the text, the
	 * taller the label - WJ */
	*min = *nat = w;
//g_print("Now nat %d\n", *nat);

	g_object_unref(layout);
}

/* Apply minimum width from the given widget to this one */
static void do_shorten(GtkWidget *widget, gint vert, gint *min, gint *nat,
	gint for_width, gpointer user_data)
{
	if (vert) return; // Do nothing for vertical size
	gtk_widget_get_preferred_width(user_data, min, NULL);
}

#if GTK3VERSION < 22 /* No gtk_scrolled_window_set_propagate_natural_*() */
/* Try to avoid scrolling - request natural size of contents */
static void do_wantmax(GtkWidget *widget, gint vert, gint *min, gint *nat,
	gint for_width, gpointer user_data)
{
	GtkWidget *inside = gtk_bin_get_child(GTK_BIN(widget));
	gint cmin = 0, cnat = 0;

	if (!inside) return; // Nothing to do
	if (((int)user_data & 1) && !vert) return; // Leave width be
	if (((int)user_data & 2) && vert) return; // Leave height be
	if (for_width >= 0)
		gtk_widget_get_preferred_height_for_width(inside, for_width, &cmin, &cnat);
	else (vert ? gtk_widget_get_preferred_height :
		gtk_widget_get_preferred_width)(inside, &cmin, &cnat);
	if (cnat > *nat) *nat = cnat;
}
#endif

#else /* #if GTK_MAJOR_VERSION <= 2 */

/* Try to avoid scrolling - request full size of contents */
static void scroll_max_size_req(GtkWidget *widget, GtkRequisition *requisition,
	gpointer user_data)
{
	GtkWidget *child = GTK_BIN(widget)->child;

	if (child && GTK_WIDGET_VISIBLE(child))
	{
		GtkRequisition wreq;
		int n, border = GTK_CONTAINER(widget)->border_width * 2;

		gtk_widget_get_child_requisition(child, &wreq);
		n = wreq.width + border;
		if ((requisition->width < n) && !((int)user_data & 1))
			requisition->width = n;
		n = wreq.height + border;
		if ((requisition->height < n) && !((int)user_data & 2))
			requisition->height = n;
	}
}

#endif

// !!! And with inlining this, problem also
GtkWidget *scrollw(int vh)
{
	static const int scrollp[3] = { GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC,
		GTK_POLICY_ALWAYS };
	GtkWidget *widget = gtk_scrolled_window_new(NULL, NULL);
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),
		scrollp[vh & 255], scrollp[vh >> 8]);
#if GTK3VERSION >= 22
	/* To fix newly-broken sizing */
	if (!(vh & 255)) gtk_scrolled_window_set_propagate_natural_width(
			GTK_SCROLLED_WINDOW(widget), TRUE);
	if (!(vh >> 8)) gtk_scrolled_window_set_propagate_natural_height(
			GTK_SCROLLED_WINDOW(widget), TRUE);
#endif
	return (widget);
}

/* Toggle notebook pages */
static void toggle_vbook(GtkToggleButton *button, gpointer user_data)
{
	gtk_notebook_set_page(**(void ***)user_data,
		!!gtk_toggle_button_get_active(button));
}

#if GTK_MAJOR_VERSION == 3

//	RGBIMAGE widget

typedef struct {
	cairo_surface_t *s;
	unsigned char *rgb;
	int w, h, bkg;
} rgbimage_data;

static gboolean redraw_rgb(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
	rgbimage_data *rd = user_data;
	GdkRectangle r;
	int x2, y2, rxy[4] = { 0, 0, rd->w, rd->h };


	if (!gdk_cairo_get_clip_rectangle(cr, &r)) return (TRUE); // Nothing to do

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);

	if (!clip(rxy, r.x, r.y, x2 = r.x + r.width, y2 = r.y + r.height, rxy) ||
		!rd->rgb) rxy[2] = r.x , rxy[3] = y2;
	else
	{
		/* RGB image buffer */
		if (!rd->s) rd->s = cairo_upload_rgb(NULL, gtk_widget_get_window(widget),
			rd->rgb, rd->w, rd->h, rd->w * 3);
		cairo_set_source_surface(cr, rd->s, 0, 0);
		cairo_unfilter(cr);
		cairo_rectangle(cr, 0, 0, rd->w, rd->h); // Let Cairo clip it
		cairo_fill(cr);
	}

	/* Opaque background outside image proper */
	cairo_set_rgb(cr, rd->bkg);
	if (rxy[2] < x2)
	{
		cairo_rectangle(cr, rxy[2], r.y, x2 - rxy[2], rxy[3] - rxy[1]);
		cairo_fill(cr);
	}
	if (rxy[3] < y2)
	{
		cairo_rectangle(cr, r.x, rxy[3], r.width, y2 - rxy[3]);
		cairo_fill(cr);
	}

	cairo_restore(cr);

	return (TRUE);
}

static void reset_rgb(GtkWidget *widget, gpointer user_data)
{
	rgbimage_data *rd = user_data;
	if (rd->s) cairo_surface_fdestroy(rd->s);
	rd->s = NULL;
}

GtkWidget *rgbimage(void **r, int *wh)
{
	GtkWidget *widget;
	rgbimage_data *rd = r[2];

	widget = gtk_drawing_area_new();
	rd->rgb = r[0];
	rd->w = wh[0];
	rd->h = wh[1];
	gtk_widget_set_size_request(widget, wh[0], wh[1]);
	g_signal_connect(G_OBJECT(widget), "unrealize", G_CALLBACK(reset_rgb), rd);
	g_signal_connect(G_OBJECT(widget), "draw", G_CALLBACK(redraw_rgb), rd);

	return (widget);
}

//	RGBIMAGEP widget

static gboolean redraw_rgbp(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
	rgbimage_data *rd = user_data;
	GtkAllocation alloc;
	int x, y;

	if (!rd->s) return (TRUE);
	gtk_widget_get_allocation(widget, &alloc);
	x = (alloc.width - rd->w) / 2;
	y = (alloc.height - rd->h) / 2;

	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, rd->s, x, y);
	cairo_rectangle(cr, x, y, rd->w, rd->h);
	cairo_fill(cr);
	cairo_restore(cr);

	return (TRUE);
}

static void reset_rgbp(GtkWidget *widget, gpointer user_data)
{
	rgbimage_data *rd = user_data;
	cairo_surface_t *s;
	cairo_t *cr;

	if (!rd->s)
	{
		GdkWindow *win = gtk_widget_get_window(widget);
		if (!win) win = gdk_screen_get_root_window(gtk_widget_get_screen(widget));
		rd->s = gdk_window_create_similar_surface(win, CAIRO_CONTENT_COLOR,
			rd->w, rd->h);
	}

	s = cairo_upload_rgb(rd->s, NULL, rd->rgb, rd->w, rd->h, rd->w * 3);
	cr = cairo_create(rd->s);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_source_surface(cr, s, 0, 0);
	cairo_unfilter(cr);
	cairo_paint(cr);
	cairo_destroy(cr);
	cairo_surface_fdestroy(s);

	gtk_widget_queue_draw(widget);
}

// !!! And with inlining this, problem also
GtkWidget *rgbimagep(void **r, int w, int h)
{
	rgbimage_data *rd = r[2];
	GtkWidget *widget;

	/* With GtkImage unable to properly hold surfaces till GTK+ 3.20, have
	 * to use a window-less container as substitute */
	widget = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
	gtk_widget_set_size_request(widget, w, h);

	rd->rgb = r[0];
	rd->w = w;
	rd->h = h;
	g_signal_connect(G_OBJECT(widget), "realize", G_CALLBACK(reset_rgbp), rd);
	g_signal_connect(G_OBJECT(widget), "unrealize", G_CALLBACK(reset_rgb), rd);
	g_signal_connect(G_OBJECT(widget), "draw", G_CALLBACK(redraw_rgbp), rd);

	return (widget);
}

#else /* if GTK_MAJOR_VERSION <= 2 */

//	RGBIMAGE widget

typedef struct {
	unsigned char *rgb;
	int w, h, bkg;
} rgbimage_data;

static void expose_ext(GtkWidget *widget, GdkEventExpose *event,
	rgbimage_data *rd, int dx, int dy)
{
	GdkGCValues sv;
	GdkGC *gc = widget->style->black_gc;
	unsigned char *src = rd->rgb;
	int w = rd->w, h = rd->h, bkg = 0;
	int x1, y1, x2, y2, rxy[4] = { 0, 0, w, h };

	x2 = (x1 = event->area.x + dx) + event->area.width;
	y2 = (y1 = event->area.y + dy) + event->area.height;

	if (!clip(rxy, x1, y1, x2, y2, rxy) || !src) rxy[2] = x1 , rxy[3] = y2;
	else gdk_draw_rgb_image(widget->window, gc,
		event->area.x, event->area.y, rxy[2] - rxy[0], rxy[3] - rxy[1],
		GDK_RGB_DITHER_NONE, src + (y1 * w + x1) * 3, w * 3);

	/* With theme engines lurking out there, weirdest things can happen */
	if (((rxy[2] < x2) || (rxy[3] < y2)) && (bkg = rd->bkg))
	{
		gdk_gc_get_values(gc, &sv);
		gdk_rgb_gc_set_foreground(gc, bkg);
	}
	if (rxy[2] < x2) gdk_draw_rectangle(widget->window, gc,
		TRUE, rxy[2] - dx, event->area.y,
		x2 - rxy[2], rxy[3] - rxy[1]);
	if (rxy[3] < y2) gdk_draw_rectangle(widget->window, gc,
		TRUE, event->area.x, rxy[3] - dy,
		event->area.width, y2 - rxy[3]);
	if (bkg) gdk_gc_set_foreground(gc, &sv.foreground);
}

static gboolean expose_rgb(GtkWidget *widget, GdkEventExpose *event, gpointer user_data)
{
	expose_ext(widget, event, user_data, 0, 0);
	return (TRUE);
}

// !!! And with inlining this, problem also
GtkWidget *rgbimage(void **r, int *wh)
{
	GtkWidget *widget;
	rgbimage_data *rd = r[2];

	widget = gtk_drawing_area_new();
	rd->rgb = r[0];
	rd->w = wh[0];
	rd->h = wh[1];
	gtk_widget_set_usize(widget, wh[0], wh[1]);
	gtk_signal_connect(GTK_OBJECT(widget), "expose_event",
		GTK_SIGNAL_FUNC(expose_rgb), rd);

	return (widget);
}

//	RGBIMAGEP widget

static void reset_rgbp(GtkWidget *widget, gpointer user_data)
{
	rgbimage_data *rd = user_data;
	GdkPixmap *pmap;

	gtk_pixmap_get(GTK_PIXMAP(widget), &pmap, NULL);
	gdk_draw_rgb_image(pmap, widget->style->black_gc,
		0, 0, rd->w, rd->h, GDK_RGB_DITHER_NONE,
		rd->rgb, rd->w * 3);
	gtk_widget_queue_draw(widget);
}

// !!! And with inlining this, problem also
GtkWidget *rgbimagep(void **r, int w, int h)
{
	rgbimage_data *rd = r[2];
	GdkPixmap *pmap;
	GtkWidget *widget;

	pmap = gdk_pixmap_new(main_window->window, w, h, -1);
	widget = gtk_pixmap_new(pmap, NULL);
	gdk_pixmap_unref(pmap);
	gtk_pixmap_set_build_insensitive(GTK_PIXMAP(widget), FALSE);

	rd->rgb = r[0];
	rd->w = w;
	rd->h = h;
	gtk_signal_connect(GTK_OBJECT(widget), "realize",
		GTK_SIGNAL_FUNC(reset_rgbp), rd);

	return (widget);
}

#endif /* GTK+1&2 */

//	CANVASIMG widget

#if GTK_MAJOR_VERSION == 3

static void expose_canvasimg(GtkWidget *widget, cairo_region_t *clip_r,
	gpointer user_data)
{
	rgbimage_data *rd = user_data;
	cairo_rectangle_int_t r;
	int x2, y2, rxy[4] = { 0, 0, rd->w, rd->h };


	cairo_region_get_extents(clip_r, &r);
	if (!clip(rxy, r.x, r.y, x2 = r.x + r.width, y2 = r.y + r.height, rxy) ||
		!rd->rgb) rxy[2] = r.x , rxy[3] = y2;
	/* RGB image buffer */
	else wjcanvas_draw_rgb(widget, rxy[0], rxy[1], rxy[2] - rxy[0], rxy[3] - rxy[1],
		rd->rgb + (rxy[1] * rd->w + rxy[0]) * 3, rd->w * 3, 0, FALSE);

	/* Opaque background outside image proper */
	if (rxy[2] < x2) wjcanvas_draw_rgb(widget, rxy[2], r.y,
		x2 - rxy[2], rxy[3] - rxy[1], NULL, 0, rd->bkg, FALSE);
	if (rxy[3] < y2) wjcanvas_draw_rgb(widget, r.x, rxy[3],
		r.width, y2 - rxy[3], NULL, 0, rd->bkg, FALSE);
}

#else /* if GTK_MAJOR_VERSION <= 2 */

static gboolean expose_canvasimg(GtkWidget *widget, GdkEventExpose *event,
	gpointer user_data)
{
	int vport[4];

	wjcanvas_get_vport(widget, vport);
	expose_ext(widget, event, user_data, vport[0], vport[1]);
	return (TRUE);
}

#endif /* GTK+1&2 */

GtkWidget *canvasimg(void **r, int w, int h, int bkg)
{
	rgbimage_data *rd = r[2];
	GtkWidget *widget, *frame;

	widget = wjcanvas_new();
	rd->rgb = r[0];
	rd->w = w;
	rd->h = h;
	rd->bkg = bkg;
	wjcanvas_size(widget, w, h);
	wjcanvas_set_expose(widget, GTK_SIGNAL_FUNC(expose_canvasimg), rd);

	frame = wjframe_new();
	gtk_widget_show(frame);
	gtk_container_add(GTK_CONTAINER(frame), widget);

	return (widget);
}

//	CANVAS widget

#if GTK_MAJOR_VERSION == 3

static void expose_canvas_(GtkWidget *widget, cairo_region_t *clip_r,
	gpointer user_data)
{
	void **slot = user_data;
	void **base = slot[0], **desc = slot[1];
	cairo_rectangle_int_t re, rex;
	rgbcontext ctx;
	int m, s, sz, cost = (int)GET_DESCV(PREV_SLOT(slot), 2);
	int i, n, wh, r1 = 0;

	/* Analyze what we got */
	cairo_region_get_extents(clip_r, &rex);
	wh = rex.width * rex.height;
	n = cairo_region_num_rectangles(clip_r);
	for (i = m = sz = 0; i < n; i++)
	{
		cairo_region_get_rectangle(clip_r, i, &re);
		sz += (s = re.width * re.height);
		if (m < s) m = s;
	}
	/* Only bother with regions if worth it */
	if (wh - sz <= cost * (n - 1)) r1 = n = 1 , m = wh , re = rex;

	ctx.rgb = malloc(m * 3);
	for (i = 0; i < n; i++)
	{
		if (!r1) cairo_region_get_rectangle(clip_r, i, &re);
		ctx.xy[2] = (ctx.xy[0] = re.x) + re.width;
		ctx.xy[3] = (ctx.xy[1] = re.y) + re.height;

		if (((evtxr_fn)desc[1])(GET_DDATA(base), base,
			(int)desc[0] & WB_OPMASK, slot, &ctx))
// !!! Allow drawing area to be reduced, or ignored altogether
			wjcanvas_draw_rgb(widget, ctx.xy[0], ctx.xy[1],
				ctx.xy[2] - ctx.xy[0], ctx.xy[3] - ctx.xy[1],
				ctx.rgb, (ctx.xy[2] - ctx.xy[0]) * 3, 0, FALSE);
	}
	free(ctx.rgb);
}

#else /* if GTK_MAJOR_VERSION <= 2 */

static gboolean expose_canvas_(GtkWidget *widget, GdkEventExpose *event,
	gpointer user_data)
{
	void **slot = user_data;
	void **base = slot[0], **desc = slot[1];
	GdkRectangle *r = &event->area;
	rgbcontext ctx;
	int vport[4];
	int i, cnt = 1, wh = r->width * r->height;
#if GTK_MAJOR_VERSION == 2 /* Maybe use regions */
	GdkRectangle *rects;
	gint nrects;
	int m, s, sz, cost = (int)GET_DESCV(PREV_SLOT(slot), 2);
 
	gdk_region_get_rectangles(event->region, &rects, &nrects);
	for (i = m = sz = 0; i < nrects; i++)
	{
		sz += (s = rects[i].width * rects[i].height);
		if (m < s) m = s;
	}

	/* Only bother with regions if worth it */
	if (wh - sz > cost * (nrects - 1))
		r = rects , cnt = nrects , wh = m;
#endif
	wjcanvas_get_vport(widget, vport);

	ctx.rgb = malloc(wh * 3);
	for (i = 0; i < cnt; i++)
	{
		ctx.xy[2] = (ctx.xy[0] = vport[0] + r[i].x) + r[i].width;
		ctx.xy[3] = (ctx.xy[1] = vport[1] + r[i].y) + r[i].height;

		if (((evtxr_fn)desc[1])(GET_DDATA(base), base,
			(int)desc[0] & WB_OPMASK, slot, &ctx))
// !!! Allow drawing area to be reduced, or ignored altogether
			gdk_draw_rgb_image(widget->window, widget->style->black_gc,
				ctx.xy[0] - vport[0], ctx.xy[1] - vport[1],
				ctx.xy[2] - ctx.xy[0], ctx.xy[3] - ctx.xy[1],
				GDK_RGB_DITHER_NONE, ctx.rgb,
				(ctx.xy[2] - ctx.xy[0]) * 3);
	}
	free(ctx.rgb);

#if GTK_MAJOR_VERSION == 2
	g_free(rects);
#endif
	return (FALSE);
}

#endif /* GTK+1&2 */

//	FCIMAGEP widget

typedef struct {
	void **mslot;
	unsigned char *rgb;
	int *xy;
	int w, h, cursor;
} fcimage_data;

static void reset_fcimage(GtkWidget *widget, gpointer user_data)
{
	fcimage_data *fd = user_data;

	if (!wjpixmap_pixmap(widget)) return;
	wjpixmap_draw_rgb(widget, 0, 0, fd->w, fd->h, fd->rgb, fd->w * 3);
	if (!fd->xy) return;
	if (!fd->cursor) wjpixmap_set_cursor(widget,
		xbm_ring4_bits, xbm_ring4_mask_bits,
		xbm_ring4_width, xbm_ring4_height,
		xbm_ring4_x_hot, xbm_ring4_y_hot, FALSE);
	fd->cursor = TRUE;
	wjpixmap_move_cursor(widget, fd->xy[0], fd->xy[1]);
}

static gboolean click_fcimage(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	gtk_widget_grab_focus(widget);
	return (FALSE);
}

// !!! And with inlining this, problem also
GtkWidget *fcimagep(void **r, char *ddata)
{
	fcimage_data *fd = r[2];
	void **pp = r[1];
	GtkWidget *widget;
	int w, h, *xy;

	xy = (void *)(ddata + (int)pp[3]);
	w = xy[0]; h = xy[1];
	xy = pp[2] == (void *)(-1) ? NULL : (void *)(ddata + (int)pp[2]);

	widget = wjpixmap_new(w, h);
	fd->xy = xy;
	fd->rgb = r[0];
	fd->w = w;
	fd->h = h;
	gtk_signal_connect(GTK_OBJECT(widget), "realize",
		GTK_SIGNAL_FUNC(reset_fcimage), fd);
	gtk_signal_connect(GTK_OBJECT(widget), "button_press_event",
		GTK_SIGNAL_FUNC(click_fcimage), NULL);
	return (widget);
}

static void fcimage_rxy(void **slot, int *xy)
{
	fcimage_data *fd = slot[2];

	wjpixmap_rxy(slot[0], xy[0], xy[1], xy + 0, xy + 1);
	xy[0] = xy[0] < 0 ? 0 : xy[0] >= fd->w ? fd->w - 1 : xy[0];
	xy[1] = xy[1] < 0 ? 0 : xy[1] >= fd->h ? fd->h - 1 : xy[1];
}

// OPT* widgets

#if GTK_MAJOR_VERSION == 3

/* !!! Limited to 256 choices max, by using id[0] for index */
static void opt_reset(void **slot, char *ddata, int idx)
{
	void **pp = slot[1];
	char **names, id[2] = { 0, 0 };
	int i, j, k, cnt, opf = GET_OPF(slot), op = opf & WB_OPMASK;

	if (op == op_OPTD) cnt = -1 , names = *(char ***)(ddata + (int)pp[2]);
	else cnt = (int)pp[3] , names = pp[2];
	if (!cnt) cnt = -1;

	g_signal_handlers_disconnect_by_func(slot[0], get_evt_1, NEXT_SLOT(slot));
	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(slot[0]));
	for (i = j = k = 0; (i != cnt) && names[i]; i++)
	{
		if (!names[i][0]) continue;
		id[0] = i;
		gtk_combo_box_text_append(GTK_COMBO_BOX_TEXT(slot[0]), id, _(names[i]));
		if (i == idx) j = k;
		k++;
  	}
	gtk_combo_box_set_active(GTK_COMBO_BOX(slot[0]), j);
	if (WB_GETREF(opf) > 1) g_signal_connect(slot[0], "changed",
		G_CALLBACK(get_evt_1), NEXT_SLOT(slot));
}	

static int wj_option_menu_get_history(GtkWidget *cbox)
{
	const char *id = gtk_combo_box_get_active_id(GTK_COMBO_BOX(cbox));
	return (id ? id[0] : 0);
}

#define wj_option_menu_set_history(W,N) gtk_combo_box_set_active(W,N)

#else /* if GTK_MAJOR_VERSION <= 2 */

#if GTK_MAJOR_VERSION == 2

/* Cause the size to be properly reevaluated */
static void opt_size_fix(GtkWidget *widget)
{
	gtk_signal_emit_by_name(GTK_OBJECT(gtk_option_menu_get_menu(
		GTK_OPTION_MENU(widget))), "selection_done");
}

#endif

static void opt_reset(void **slot, char *ddata, int idx)
{
	GtkWidget *menu, *item;
	void **pp = slot[1];
	char **names;
	int i, j, k, cnt, opf = GET_OPF(slot), op = opf & WB_OPMASK;

	if (op == op_OPTD) cnt = -1 , names = *(char ***)(ddata + (int)pp[2]);
	else cnt = (int)pp[3] , names = pp[2];
	if (!cnt) cnt = -1;

	menu = gtk_menu_new();
	for (i = j = k = 0; (i != cnt) && names[i]; i++)
	{
		if (!names[i][0]) continue;
		item = gtk_menu_item_new_with_label(_(names[i]));
		gtk_object_set_user_data(GTK_OBJECT(item), (gpointer)i);
		if (WB_GETREF(opf) > 1) gtk_signal_connect(GTK_OBJECT(item),
			"activate", GTK_SIGNAL_FUNC(get_evt_1), NEXT_SLOT(slot));
		gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
		if (i == idx) j = k;
		k++;
  	}
	gtk_widget_show_all(menu);
	gtk_menu_set_active(GTK_MENU(menu), j);
	gtk_option_menu_set_menu(GTK_OPTION_MENU(slot[0]), menu);
#if GTK_MAJOR_VERSION == 2
	opt_size_fix(slot[0]);
#endif
}	

static int wj_option_menu_get_history(GtkWidget *optmenu)
{
	optmenu = gtk_option_menu_get_menu(GTK_OPTION_MENU(optmenu));
	optmenu = gtk_menu_get_active(GTK_MENU(optmenu));
	return (optmenu ? (int)gtk_object_get_user_data(GTK_OBJECT(optmenu)) : 0);
}

#define wj_option_menu_set_history(W,N) gtk_option_menu_set_history(W,N)

#endif /* GTK+1&2 */

//	RPACK* and COMBO widgets

static GtkWidget *mkpack(int mode, int d, int ref, char *ddata, void **r)
{
	void **pp = r[1];
	char **src = pp[2];
	int nh, n, v = *(int *)r[0];

	nh = (n = (int)pp[3]) & 255;
	if (mode) n >>= 8;
	if (d) n = -1 , src = *(char ***)(ddata + (int)pp[2]);
	if (!n) n = -1;
	return ((mode ? wj_radio_pack : wj_combo_box)(src, n, nh, v,
		NEXT_SLOT(r), ref < 2 ? NULL : 
		mode ? GTK_SIGNAL_FUNC(get_evt_1_t) : GTK_SIGNAL_FUNC(get_evt_1)));
}

#if GTK_MAJOR_VERSION == 3

static gboolean col_expose(GtkWidget *widget, cairo_t *cr, unsigned char *col)
{
	cairo_save(cr);
	cairo_set_operator(cr, CAIRO_OPERATOR_SOURCE);
	cairo_set_rgb(cr, MEM_2_INT(col, 0));
	cairo_paint(cr);
	cairo_restore(cr);

	return (TRUE);
}

#else /* if GTK_MAJOR_VERSION <= 2 */

// !!! ref to RGB[3]
static gboolean col_expose(GtkWidget *widget, GdkEventExpose *event,
	unsigned char *col)
{
	GdkGCValues sv;

	gdk_gc_get_values(widget->style->black_gc, &sv);
	gdk_rgb_gc_set_foreground(widget->style->black_gc, MEM_2_INT(col, 0));
	gdk_draw_rectangle(widget->window, widget->style->black_gc, TRUE,
		event->area.x, event->area.y, event->area.width, event->area.height);
	gdk_gc_set_foreground(widget->style->black_gc, &sv.foreground);

	return (TRUE);
}

#endif /* GTK+1&2 */

//	COLORLIST widget

typedef struct {
	unsigned char *col;
	int cnt, *idx;
} colorlist_data;

#ifdef U_LISTS_GTK1
static gboolean colorlist_click(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	void **slot = user_data;
	void **base = slot[0], **desc = slot[1];
	colorlist_ext xdata;

	if (event->type == GDK_BUTTON_PRESS)
	{
		xdata.idx = (int)gtk_object_get_user_data(GTK_OBJECT(widget));
		xdata.button = event->button;
		((evtx_fn)desc[1])(GET_DDATA(base), base,
			(int)desc[0] & WB_OPMASK, slot, &xdata);
	}

	/* Let click processing continue */
	return (FALSE);
}

static void colorlist_select(GtkList *list, GtkWidget *widget, gpointer user_data)
{
	void **orig = user_data, **slot = SLOT_N(orig, 2);
	void **base = slot[0], **desc = slot[1];
	colorlist_data *dt = orig[2];

	/* Update the value */
	*dt->idx = (int)gtk_object_get_user_data(GTK_OBJECT(widget));
	/* Call the handler */
	if (desc[1]) ((evt_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot);
}

// !!! And with inlining this, problem also
GtkWidget *colorlist(void **r, char *ddata)
{
	GtkWidget *list, *item, *col, *label, *box;
	colorlist_data *dt = r[2];
	void *v, **pp = r[1];
	char txt[64], *t, **sp = NULL;
	int i, cnt = 0, *idx = r[0];

	list = gtk_list_new();

	// Fill datablock
	v = ddata + (int)pp[2];
	if (((int)pp[0] & WB_OPMASK) == op_COLORLIST) // array of names
	{
		sp = *(char ***)v;
		while (sp[cnt]) cnt++;
	}
	else cnt = *(int *)v; // op_COLORLISTN - number
	dt->cnt = cnt;
	dt->col = (void *)(ddata + (int)pp[3]); // palette
	dt->idx = idx;

	for (i = 0; i < cnt; i++)
	{
		item = gtk_list_item_new();
		gtk_object_set_user_data(GTK_OBJECT(item), (gpointer)i);
		if (pp[5]) gtk_signal_connect(GTK_OBJECT(item), "button_press_event",
			GTK_SIGNAL_FUNC(colorlist_click), NEXT_SLOT(r));
		gtk_container_add(GTK_CONTAINER(list), item);

		box = gtk_hbox_new(FALSE, 3);
		gtk_widget_show(box);
		gtk_container_set_border_width(GTK_CONTAINER(box), 3);
		gtk_container_add(GTK_CONTAINER(item), box);

		col = pack(box, gtk_drawing_area_new());
		gtk_drawing_area_size(GTK_DRAWING_AREA(col), 20, 20);
		gtk_signal_connect(GTK_OBJECT(col), "expose_event",
			GTK_SIGNAL_FUNC(col_expose), dt->col + i * 3);

		/* Name or index */
		if (sp) t = _(sp[i]);
		else sprintf(t = txt, "%d", i);
		label = xpack(box, gtk_label_new(t));
		gtk_misc_set_alignment(GTK_MISC(label), 0.0, 1.0);

		gtk_widget_show_all(item);
	}
	gtk_list_set_selection_mode(GTK_LIST(list), GTK_SELECTION_BROWSE);
	/* gtk_list_select_*() don't work in GTK_SELECTION_BROWSE mode */
	gtk_container_set_focus_child(GTK_CONTAINER(list),
		GTK_WIDGET(g_list_nth(GTK_LIST(list)->children, *idx)->data));
	gtk_signal_connect(GTK_OBJECT(list), "select_child",
		GTK_SIGNAL_FUNC(colorlist_select), r);

	return (list);
}

static void colorlist_reset_color(void **slot, int idx)
{
	colorlist_data *dt = slot[2];
	unsigned char *rgb = dt->col + idx * 3;
	GdkColor c;

	c.pixel = 0;
	c.red   = rgb[0] * 257;
	c.green = rgb[1] * 257;
	c.blue  = rgb[2] * 257;
	// In case of some really ancient system with indexed display mode
	gdk_colormap_alloc_color(gdk_colormap_get_system(), &c, FALSE, TRUE);
	/* Redraw the item displaying the color */
	gtk_widget_queue_draw(
		GTK_WIDGET(g_list_nth(GTK_LIST(slot[0])->children, idx)->data));
}

static void list_scroll_in(GtkWidget *widget, gpointer user_data)
{	
	GtkContainer *list = GTK_CONTAINER(widget);
	GtkAdjustment *adj = user_data;
	int y;

	if (!list->focus_child) return; // Paranoia
	if (adj->upper <= adj->page_size) return; // Nothing to scroll
	y = list->focus_child->allocation.y +
		list->focus_child->allocation.height / 2 -
		adj->page_size / 2;
	adj->value = y < 0 ? 0 : y > adj->upper - adj->page_size ?
		adj->upper - adj->page_size : y;
	gtk_adjustment_value_changed(adj);
}
#endif

#ifndef U_LISTS_GTK1
static gboolean colorlist_click(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	void **slot = user_data;
	void **base = slot[0], **desc = slot[1];
	colorlist_ext xdata;
	GtkTreePath *tp;

	if ((event->type == GDK_BUTTON_PRESS) &&
		(event->window == gtk_tree_view_get_bin_window(GTK_TREE_VIEW(widget))) &&
		gtk_tree_view_get_path_at_pos(GTK_TREE_VIEW(widget), event->x, event->y,
			&tp, NULL, NULL, NULL))
	{
		xdata.idx = gtk_tree_path_get_indices(tp)[0];
		gtk_tree_path_free(tp);
		xdata.button = event->button;
		((evtx_fn)desc[1])(GET_DDATA(base), base,
			(int)desc[0] & WB_OPMASK, slot, &xdata);
	}

	/* Let click processing continue */
	return (FALSE);
}

static void colorlist_select(GtkTreeView *tree, gpointer user_data)
{
	void **orig = user_data, **slot = SLOT_N(orig, 2);
	void **base = slot[0], **desc = slot[1];
	colorlist_data *dt = orig[2];
	GtkTreePath *tp;

	/* Update the value */
	gtk_tree_view_get_cursor(tree, &tp, NULL);
	*dt->idx = gtk_tree_path_get_indices(tp)[0];
	gtk_tree_path_free(tp);
	/* Call the handler */
	if (desc[1]) ((evt_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot);
}

// !!! And with inlining this, problem also
GtkWidget *colorlist(void **r, char *ddata)
{
	GtkListStore *ls;
	GtkCellRenderer *ren;
	GtkTreePath *tp;
	GtkWidget *w;
	colorlist_data *dt = r[2];
	void *v, **pp = r[1];
	char txt[64], *t, **sp = NULL;
	int i, cnt = 0, *idx = r[0];

	// Fill datablock
	v = ddata + (int)pp[2];
	if (((int)pp[0] & WB_OPMASK) == op_COLORLIST) // array of names
	{
		sp = *(char ***)v;
		while (sp[cnt]) cnt++;
	}
	else cnt = *(int *)v; // op_COLORLISTN - number
	dt->cnt = cnt;
	dt->col = (void *)(ddata + (int)pp[3]); // palette
	dt->idx = idx;

	ls = gtk_list_store_new(2, GDK_TYPE_PIXBUF, G_TYPE_STRING);
	for (i = 0; i < cnt; i++)
	{
		GtkTreeIter it;
		GdkPixbuf *p = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, 20, 20);
		gdk_pixbuf_fill(p, ((unsigned)MEM_2_INT(dt->col, i * 3) << 8) + 0xFF);
		gtk_list_store_append(ls, &it);
		/* Name or index */
		if (sp) t = _(sp[i]);
		else sprintf(t = txt, "%d", i);
		gtk_list_store_set(ls, &it, 0, p, 1, t, -1);
		g_object_unref(p);
	}
	w = gtk_tree_view_new_with_model(GTK_TREE_MODEL(ls));
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(w), FALSE);
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(w)),
		GTK_SELECTION_BROWSE);
	ren = gtk_cell_renderer_pixbuf_new();
	g_object_set(ren, "width", 20 + 3, "xalign", 1.0, "ypad", 3, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(w), 
		gtk_tree_view_column_new_with_attributes("Color", ren,
			"pixbuf", 0, NULL));
	ren = gtk_cell_renderer_text_new();
	g_object_set(ren, "xalign", 0.0, "yalign", 1.0, "xpad", 3, "ypad", 3, NULL);
	gtk_tree_view_append_column(GTK_TREE_VIEW(w),
		gtk_tree_view_column_new_with_attributes("Index", ren,
			"text", 1, NULL));
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(w), FALSE);

	tp = gtk_tree_path_new_from_indices(*idx, -1);
	gtk_tree_view_set_cursor_on_cell(GTK_TREE_VIEW(w), tp, NULL, NULL, FALSE);
	/* Seems safe to do w/o scrolledwindow too */
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(w), tp, NULL, TRUE, 0.5, 0.0);
	gtk_tree_path_free(tp);

	if (pp[5]) g_signal_connect(w, "button_press_event", G_CALLBACK(colorlist_click), NEXT_SLOT(r));
	g_signal_connect(w, "cursor_changed", G_CALLBACK(colorlist_select), r);

	return (w);
}

static void colorlist_reset_color(void **slot, int idx)
{
	colorlist_data *dt = slot[2];
	unsigned char *rgb = dt->col + idx * 3;
	GtkTreeModel *tm = gtk_tree_view_get_model(GTK_TREE_VIEW(slot[0]));
	GtkTreePath *tp = gtk_tree_path_new_from_indices(idx, -1);
	GtkTreeIter it;

	if (gtk_tree_model_get_iter(tm, &it, tp))
	{
		GdkPixbuf *p;
		gtk_tree_model_get(tm, &it, 0, &p, -1);
		gdk_pixbuf_fill(p, ((unsigned)MEM_2_INT(rgb, 0) << 8) + 0xFF);
		g_object_unref(p); // !!! After get() ref'd it
		/* Redraw the row displaying the color */
		gtk_tree_model_row_changed(tm, tp, &it);
	}
	gtk_tree_path_free(tp);
}
#endif

//	GRADBAR widget

#define GRADBAR_LEN 16
#define SLOT_SIZE 15

typedef struct {
	unsigned char *map, *rgb;
	GtkWidget *lr[2];
	void **r;
	int ofs, lim, *idx, *len;
	unsigned char idxs[GRADBAR_LEN];
} gradbar_data;

static void gradbar_scroll(GtkWidget *btn, gpointer user_data)
{
	unsigned char *idx = user_data;
	gradbar_data *dt = (void *)(idx - offsetof(gradbar_data, idxs) - *idx);
	int dir = *idx * 2 - 1;

	dt->ofs += dir;
	*dt->idx += dir; // self-reading
	gtk_widget_set_sensitive(dt->lr[0], !!dt->ofs);
	gtk_widget_set_sensitive(dt->lr[1], dt->ofs < dt->lim - GRADBAR_LEN);
	gtk_widget_queue_draw(gtk_widget_get_parent(btn));
	get_evt_1(NULL, (gpointer)dt->r);
}

static void gradbar_slot(GtkWidget *btn, gpointer user_data)
{
	unsigned char *idx = user_data;
	gradbar_data *dt;

	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) return;
	dt = (void *)(idx - offsetof(gradbar_data, idxs) - *idx);
	*dt->idx = *idx + dt->ofs; // self-reading
	get_evt_1(NULL, (gpointer)dt->r);
}

#if GTK_MAJOR_VERSION == 3

static gboolean gradbar_draw(GtkWidget *widget, cairo_t *cr, gpointer user_data)
{
	unsigned char *rp, *idx = user_data;
	gradbar_data *dt = (void *)(idx - offsetof(gradbar_data, idxs) - *idx);
	int n = *idx + dt->ofs;

	cairo_save(cr);
	cairo_rectangle(cr, 0, 0, SLOT_SIZE, SLOT_SIZE);
	if (n < *dt->len) // Filled slot
	{
		rp = dt->rgb ? dt->rgb + dt->map[n] * 3 : dt->map + n * 3;
		cairo_set_rgb(cr, MEM_2_INT(rp, 0));

	}
	else // Empty slot - show that
	{
		cairo_set_rgb(cr, RGB_2_INT(178, 178, 178));
		cairo_fill(cr);
		cairo_set_rgb(cr, RGB_2_INT(128, 128, 128));
		cairo_move_to(cr, 0, 0);
		cairo_line_to(cr, 0, SLOT_SIZE);
		cairo_line_to(cr, SLOT_SIZE, SLOT_SIZE);
		cairo_close_path(cr);
	}
	cairo_fill(cr);
	cairo_restore(cr);

	return (TRUE);
}

#else /* if GTK_MAJOR_VERSION <= 2 */

static gboolean gradbar_draw(GtkWidget *widget, GdkEventExpose *event,
	gpointer user_data)
{
	unsigned char rgb[SLOT_SIZE * 2 * 3], *idx = user_data;
	gradbar_data *dt = (void *)(idx - offsetof(gradbar_data, idxs) - *idx);
	int i, n = *idx + dt->ofs;

	if (n < *dt->len) // Filled slot
	{
		memcpy(rgb, dt->rgb ? dt->rgb + dt->map[n] * 3 :
			dt->map + n * 3, 3);
		for (i = 3; i < SLOT_SIZE * 2 * 3; i++) rgb[i] = rgb[i - 3];
	}
	else // Empty slot - show that
	{
		memset(rgb, 178, sizeof(rgb));
		memset(rgb, 128, SLOT_SIZE * 3);
	}

	gdk_draw_rgb_image(widget->window, widget->style->black_gc,
		0, 0, SLOT_SIZE, SLOT_SIZE, GDK_RGB_DITHER_NONE,
		rgb + SLOT_SIZE * 3, -3);

	return (TRUE);
}

#endif /* GTK+1&2 */

// !!! With inlining this, problem also
GtkWidget *gradbar(void **r, char *ddata)
{
	GtkWidget *hbox, *btn, *sw;
	gradbar_data *dt = r[2];
	void **pp = r[1];
	int i;

	hbox = gtk_hbox_new(TRUE, 0);

	dt->r = NEXT_SLOT(r);
	dt->idx = r[0];
	dt->len = (void *)(ddata + (int)pp[3]); // length
	dt->map = (void *)(ddata + (int)pp[4]); // gradient map
	if (*(int *)(ddata + (int)pp[2])) // mode not-RGB
		dt->rgb = (void *)(ddata + (int)pp[5]); // colormap
	dt->lim = (int)pp[6];

	dt->lr[0] = btn = xpack(hbox, gtk_button_new());
	add_css_class(btn, "mtPaint_gradbar_button");
	gtk_container_add(GTK_CONTAINER(btn), gtk_arrow_new(GTK_ARROW_LEFT,
#if GTK_MAJOR_VERSION == 1
        // !!! Arrow w/o shadow is invisible in plain GTK+1
		GTK_SHADOW_OUT));
#else /* #if GTK_MAJOR_VERSION >= 2 */
		GTK_SHADOW_NONE));
#endif
	gtk_widget_set_sensitive(btn, FALSE);
	gtk_signal_connect(GTK_OBJECT(btn), "clicked",
		GTK_SIGNAL_FUNC(gradbar_scroll), dt->idxs + 0);
	btn = NULL;
	for (i = 0; i < GRADBAR_LEN; i++)
	{
		dt->idxs[i] = i;
		btn = xpack(hbox, gtk_radio_button_new_from_widget(
			GTK_RADIO_BUTTON_0(btn)));
		add_css_class(btn, "mtPaint_gradbar_button");
		gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(btn), FALSE);
		gtk_signal_connect(GTK_OBJECT(btn), "toggled",
			GTK_SIGNAL_FUNC(gradbar_slot), dt->idxs + i);
		sw = gtk_drawing_area_new();
		gtk_container_add(GTK_CONTAINER(btn), sw);
#if GTK_MAJOR_VERSION == 3
		gtk_widget_set_valign(sw, GTK_ALIGN_CENTER);
		gtk_widget_set_halign(sw, GTK_ALIGN_CENTER);
		gtk_widget_set_size_request(sw, SLOT_SIZE, SLOT_SIZE);
		g_signal_connect(G_OBJECT(sw), "draw",
			G_CALLBACK(gradbar_draw), dt->idxs + i);
#else /* if GTK_MAJOR_VERSION <= 2 */
		gtk_widget_set_usize(sw, SLOT_SIZE, SLOT_SIZE);
		gtk_signal_connect(GTK_OBJECT(sw), "expose_event",
			GTK_SIGNAL_FUNC(gradbar_draw), dt->idxs + i);
#endif /* GTK+1&2 */
	}
	dt->lr[1] = btn = xpack(hbox, gtk_button_new());
	add_css_class(btn, "mtPaint_gradbar_button");
	gtk_container_add(GTK_CONTAINER(btn), gtk_arrow_new(GTK_ARROW_RIGHT,
#if GTK_MAJOR_VERSION == 1
        // !!! Arrow w/o shadow is invisible in plain GTK+1
		GTK_SHADOW_OUT));
#else /* #if GTK_MAJOR_VERSION >= 2 */
		GTK_SHADOW_NONE));
#endif
	gtk_signal_connect(GTK_OBJECT(btn), "clicked",
		GTK_SIGNAL_FUNC(gradbar_scroll), dt->idxs + 1);

	gtk_widget_show_all(hbox);
	return (hbox);
}

//	COMBOENTRY widget

#if GTK_MAJOR_VERSION == 3

static void comboentry_reset(GtkWidget *cbox, char **v, char **src)
{
	GtkWidget *entry = gtk_bin_get_child(GTK_BIN(cbox));

	gtk_entry_set_text(GTK_ENTRY(entry), *(char **)v);
	// Replace transient buffer
	*(const char **)v = gtk_entry_get_text(GTK_ENTRY(entry));

	gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(cbox));
	while (*src) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cbox), *src++);
}

static void comboentry_chg(GtkComboBox *cbox, gpointer user_data)
{
	/* Only react to selecting from list */
	if (gtk_combo_box_get_active(cbox) >= 0) get_evt_1(G_OBJECT(cbox), user_data);
}

// !!! With inlining this, problem also
GtkWidget *comboentry(char *ddata, void **r)
{
	void **pp = r[1];
	GtkWidget *cbox = gtk_combo_box_text_new_with_entry();
	GtkWidget *entry = gtk_bin_get_child(GTK_BIN(cbox));

	comboentry_reset(cbox, r[0], *(char ***)(ddata + (int)pp[2]));

	g_signal_connect(G_OBJECT(cbox), "changed",
		G_CALLBACK(comboentry_chg), NEXT_SLOT(r));
	g_signal_connect(G_OBJECT(entry), "activate",
		G_CALLBACK(get_evt_1), NEXT_SLOT(r));

	return (cbox);
}

#define comboentry_get_text(A) \
	gtk_entry_get_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(A))))
#define comboentry_set_text(A,B) \
	gtk_entry_set_text(GTK_ENTRY(gtk_bin_get_child(GTK_BIN(A))), B)

#else /* if GTK_MAJOR_VERSION <= 2 */

static void comboentry_reset(GtkCombo *combo, char **v, char **src)
{
	GList *list = NULL;

	gtk_entry_set_text(GTK_ENTRY(combo->entry), *(char **)v);
	// Replace transient buffer
	*(const char **)v = gtk_entry_get_text(GTK_ENTRY(combo->entry));
	// NULL-terminated array of pointers
	while (*src) list = g_list_append(list, *src++);
#if GTK_MAJOR_VERSION == 1
	if (!list) gtk_list_clear_items(GTK_LIST(combo->list), 0, -1);
	else /* Fails if list is empty in GTK+1 */
#endif
	gtk_combo_set_popdown_strings(combo, list);
	g_list_free(list);
}

// !!! With inlining this, problem also
GtkWidget *comboentry(char *ddata, void **r)
{
	void **pp = r[1];
	GtkWidget *w = gtk_combo_new();
	GtkCombo *combo = GTK_COMBO(w);

	gtk_combo_disable_activate(combo);
	comboentry_reset(combo, r[0], *(char ***)(ddata + (int)pp[2]));

	gtk_signal_connect(GTK_OBJECT(combo->popwin), "hide",
		GTK_SIGNAL_FUNC(get_evt_1), NEXT_SLOT(r));
	gtk_signal_connect(GTK_OBJECT(combo->entry), "activate",
		GTK_SIGNAL_FUNC(get_evt_1), NEXT_SLOT(r));

	return (w);
}

#define comboentry_get_text(A) gtk_entry_get_text(GTK_ENTRY(GTK_COMBO(A)->entry))
#define comboentry_set_text(A,B) gtk_entry_set_text(GTK_ENTRY(GTK_COMBO(A)->entry), B)

#endif /* GTK+1&2 */

//	PCTCOMBO widget

#if GTK_MAJOR_VERSION == 3

// !!! Even with inlining this, some space gets wasted
GtkWidget *pctcombo(void **r)
{
	GtkWidget *cbox, *entry, *button;
	char buf[32];
	int i, n = 0, v = *(int *)r[0], *ns = GET_DESCV(r, 2);

	/* Uses 0-terminated array of ints */
	while (ns[n] > 0) n++;

	cbox = gtk_combo_box_text_new_with_entry();
	/* Find the button */
	button = combobox_button(cbox);
	gtk_widget_set_can_focus(button, FALSE);
	/* Make it small enough */
	css_restyle(button, ".mtPaint_pctbutton { padding: 0; }",
		"mtPaint_pctbutton", NULL);
	gtk_widget_set_size_request(button, 18, -1);

	entry = gtk_bin_get_child(GTK_BIN(cbox));
	gtk_widget_set_can_focus(entry, FALSE);
	gtk_editable_set_editable(GTK_EDITABLE(entry), FALSE);
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 6);
	gtk_entry_set_max_width_chars(GTK_ENTRY(entry), 6);

	for (i = 0; i < n; i++)
	{
		sprintf(buf, "%d%%", ns[i]);
		gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(cbox), buf);
	}
	sprintf(buf, "%d%%", v);
	gtk_entry_set_text(GTK_ENTRY(entry), buf);

	g_signal_connect(G_OBJECT(entry), "changed",
		G_CALLBACK(get_evt_1), NEXT_SLOT(r));

	return (cbox);
}

#define pctcombo_entry(A) gtk_bin_get_child(GTK_BIN(A))

#else /* if GTK_MAJOR_VERSION <= 2 */

#if (GTK_MAJOR_VERSION == 2) && (GTK2VERSION < 12) /* GTK+ 2.10 or earlier */

static void pctcombo_chg(GtkWidget *entry, gpointer user_data)
{
	/* Filter out spurious deletions */
	if (strlen(gtk_entry_get_text(GTK_ENTRY(entry))))
		get_evt_1(NULL, user_data);
}

#endif

// !!! Even with inlining this, some space gets wasted
GtkWidget *pctcombo(void **r)
{
	GtkWidget *w, *entry;
	GList *list = NULL;
	char *ts, *s;
	int i, n = 0, v = *(int *)r[0], *ns = GET_DESCV(r, 2);

	/* Uses 0-terminated array of ints */
	while (ns[n] > 0) n++;

	w = gtk_combo_new();
	gtk_combo_set_value_in_list(GTK_COMBO(w), FALSE, FALSE);
	entry = GTK_COMBO(w)->entry;
	GTK_WIDGET_UNSET_FLAGS(entry, GTK_CAN_FOCUS);
	gtk_widget_set_usize(GTK_COMBO(w)->button, 18, -1);
#if GTK_MAJOR_VERSION == 1
	gtk_widget_set_usize(w, 75, -1);
#else /* #if GTK_MAJOR_VERSION == 2 */
	gtk_entry_set_width_chars(GTK_ENTRY(entry), 6);
#endif
	gtk_entry_set_editable(GTK_ENTRY(entry), FALSE);

	ts = s = calloc(n, 32); // 32 chars per int
	for (i = 0; i < n; i++)
	{
		list = g_list_append(list, s);
		s += sprintf(s, "%d%%", ns[i]) + 1;
	}
	gtk_combo_set_popdown_strings(GTK_COMBO(w), list);
	g_list_free(list);
	sprintf(ts, "%d%%", v);
	gtk_entry_set_text(GTK_ENTRY(entry), ts);
	free(ts);

	/* In GTK1, combo box entry is updated continuously */
#if GTK_MAJOR_VERSION == 1
	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(w)->popwin), "hide",
		GTK_SIGNAL_FUNC(get_evt_1), NEXT_SLOT(r));
#elif GTK2VERSION < 12 /* GTK+ 2.10 or earlier */
	gtk_signal_connect(GTK_OBJECT(entry), "changed",
		GTK_SIGNAL_FUNC(pctcombo_chg), NEXT_SLOT(r));
#else /* GTK+ 2.12+ */
	gtk_signal_connect(GTK_OBJECT(entry), "changed",
		GTK_SIGNAL_FUNC(get_evt_1), NEXT_SLOT(r));
#endif

	return (w);
}

#define pctcombo_entry(A) (GTK_COMBO(A)->entry)

#endif /* GTK+1&2 */

//	TEXT widget

static GtkWidget *textarea(char *init)
{
	GtkWidget *scroll, *text;

#if GTK_MAJOR_VERSION == 1
	text = gtk_text_new(NULL, NULL);
	gtk_text_set_editable(GTK_TEXT(text), TRUE);
	if (init) gtk_text_insert(GTK_TEXT(text), NULL, NULL, NULL, init, -1);

	scroll = gtk_scrolled_window_new(NULL, GTK_TEXT(text)->vadj);
#else /* #if GTK_MAJOR_VERSION >= 2 */
	GtkTextBuffer *texbuf = gtk_text_buffer_new(NULL);
	if (init) gtk_text_buffer_set_text(texbuf, init, -1);

	text = gtk_text_view_new_with_buffer(texbuf);

#if GTK_MAJOR_VERSION == 3
	scroll = gtk_scrolled_window_new(
		gtk_scrollable_get_hadjustment(GTK_SCROLLABLE(text)),
		gtk_scrollable_get_vadjustment(GTK_SCROLLABLE(text)));
#else
	scroll = gtk_scrolled_window_new(GTK_TEXT_VIEW(text)->hadjustment,
		GTK_TEXT_VIEW(text)->vadjustment);
#endif
#endif
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_container_add(GTK_CONTAINER(scroll), text);
	gtk_widget_show_all(scroll);

	return (text);
}

static char *read_textarea(GtkWidget *text)
{
#if GTK_MAJOR_VERSION == 1
	return (gtk_editable_get_chars(GTK_EDITABLE(text), 0, -1));
#else /* #if GTK_MAJOR_VERSION >= 2 */
	GtkTextIter begin, end;
	GtkTextBuffer *buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(text));

	gtk_text_buffer_get_start_iter(buffer, &begin);
	gtk_text_buffer_get_end_iter(buffer, &end);
	return (gtk_text_buffer_get_text(buffer, &begin, &end, TRUE));
#endif
}

static void set_textarea(GtkWidget *text, char *init)
{
#if GTK_MAJOR_VERSION == 1
	gtk_editable_delete_text(GTK_EDITABLE(text), 0, -1);
	if (init) gtk_text_insert(GTK_TEXT(text), NULL, NULL, NULL, init, -1);
#else /* #if GTK_MAJOR_VERSION >= 2 */
	gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(text)),
		init ? init : "", -1);
#endif
}

//	Columns for LIST* widgets

typedef struct {
	char *ddata;			// datastruct
	void **dcolumn;			// datablock "column" if any
	void **columns[MAX_COLS];	// column slots
	void **r;			// slot
	int ncol;			// columns
} col_data;

static void *get_cell(col_data *c, int row, int col)
{
	void **cp = c->columns[col][1];
	char *v;
	int op, kind, ofs = 0;

	kind = (int)cp[3] >> COL_LSHIFT;
	if (!cp[2]) // relative
	{
		ofs = (int)cp[1];
		cp = c->dcolumn;
	}
	op = (int)cp[0];
	v = cp[1];
	if (op & WB_FFLAG) v = c->ddata + (int)v;
	if (op & WB_NFLAG) v = *(void **)v; // array dereference
	if (!v) return (NULL); // Paranoia
	v += ofs + row * (int)cp[2];
	if (kind == col_PTR) v = *(char **)v; // cell dereference
	else if (kind == col_REL) v += *(int *)v;

	return (v);
}

static void set_columns(col_data *c, col_data *src, char *ddata, void **r)
{
	int i;

	/* Copy & clear the source */
	*c = *src;
	memset(src, 0, sizeof(*src));

	c->ddata = ddata;
	c->r = r;
	/* Link columns to group */
	for (i = 0; i < c->ncol; i++)
		((swdata *)c->columns[i][0])->strs = (void *)c;
}

//	LISTCC widget

typedef struct {
	int lock;		// against in-reset signals
	int wantfoc;		// delayed focus
	int h;			// max height
	int *idx;		// result field
	int *cnt;		// length field
	col_data c;
} listcc_data;

#ifdef U_LISTS_GTK1
static void listcc_select(GtkList *list, GtkWidget *widget, gpointer user_data)
{
	listcc_data *dt = user_data;
	void **base, **desc, **slot = NEXT_SLOT(dt->c.r);

	if (dt->lock) return;
	/* Update the value */
	*dt->idx = (int)gtk_object_get_user_data(GTK_OBJECT(widget));
	// !!! No touching slot outside lock: uninitialized the first time
	base = slot[0]; desc = slot[1];
	/* Call the handler */
	if (desc[1]) ((evt_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot);
}

static void listcc_select_item(void **slot)
{
	GtkWidget *item, *win, *list = slot[0], *fw = NULL;
	listcc_data *dt = slot[2];
	int idx = *dt->idx, n = dt->h - idx - 1; // backward

	if (!(item = g_list_nth_data(GTK_LIST(list)->children, n))) return;
	/* Move focus if sensitive, flag to be moved later if not */
	if (!(dt->wantfoc = !GTK_WIDGET_IS_SENSITIVE(list)))
	{
		dt->lock++; // this is strictly a visual update

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
		/* Clear stuck selections when possible */
		gtk_list_item_select(GTK_LIST_ITEM(item));

		dt->lock--;
	}
	/* Signal must be reliable even with focus delayed */
	listcc_select(NULL, item, dt);
}

static void listcc_update(GtkWidget *widget, GtkStateType state, gpointer user_data)
{
	if (state == GTK_STATE_INSENSITIVE)
	{
		void **slot = user_data;
		listcc_data *dt = slot[2];

		if (!dt->wantfoc) return;
		dt->lock++; // strictly a visual update
		listcc_select_item(slot);
		dt->lock--;
	}
}

static void listcc_toggled(GtkWidget *widget, gpointer user_data)
{
	listcc_data *dt = user_data;
	void **slot, **base, **desc;
	char *v;
	int col, row;

	if (dt->lock) return;
	/* Find out what happened to what, and where */
	col = (int)gtk_object_get_user_data(GTK_OBJECT(widget));
	row = (int)gtk_object_get_user_data(GTK_OBJECT(widget->parent->parent));

	/* Self-updating */
	v = get_cell(&dt->c, row, col);
	*(int *)v = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));
	/* Now call the handler */
	slot = dt->c.columns[col];
	slot = NEXT_SLOT(slot);
	base = slot[0]; desc = slot[1];
	if (desc[1]) ((evtx_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot, (void *)row);
}

static void listcc_reset(void **slot, int row)
{
	GtkWidget *list = slot[0];
	GList *col, *curr = GTK_LIST(list)->children;
	listcc_data *ld = slot[2];
	int cnt = *ld->cnt;


	if (row >= 0) // specific row, not whole list
		curr = g_list_nth(curr, ld->h - row - 1); // backward
	ld->lock = TRUE;
	for (; curr; curr = curr->next)
	{
		GtkWidget *item = curr->data;
		int j, n = (int)gtk_object_get_user_data(GTK_OBJECT(item));

		if (n >= cnt)
		{
			gtk_widget_hide(item);
			/* !!! To stop arrow keys handler from selecting this */
			gtk_widget_set_sensitive(item, FALSE);
			continue;
		}
		
		col = GTK_BOX(GTK_BIN(item)->child)->children;
		for (j = 0; j < ld->c.ncol; col = col->next , j++)
		{
			GtkWidget *widget = ((GtkBoxChild *)(col->data))->widget;
			char *v = get_cell(&ld->c, n, j);
			void **cp = ld->c.columns[j][1];
			int op = (int)cp[0] & WB_OPMASK;

			if ((op == op_TXTCOLUMN) || (op == op_XTXTCOLUMN))
				gtk_label_set_text(GTK_LABEL(widget), v);
			else if (op == op_CHKCOLUMN)
				gtk_toggle_button_set_active(
					GTK_TOGGLE_BUTTON(widget), *(int *)v);
		}
		gtk_widget_set_sensitive(item, TRUE);
		gtk_widget_show(item);
		if (row >= 0) break; // one row only
	}

	if (row < 0) listcc_select_item(slot); // only when full reset
	ld->lock = FALSE;
}

// !!! With inlining this, problem also
GtkWidget *listcc(void **r, char *ddata, col_data *c)
{
	char txt[128];
	GtkWidget *widget, *list, *item, *uninit_(hbox);
	listcc_data *ld = r[2];
	void **pp = r[1];
	int j, n, h, *cnt, *idx = r[0];


	cnt = (void *)(ddata + (int)pp[2]); // length pointer
	h = (int)pp[3]; // max for variable length
	if (h < *cnt) h = *cnt;

	list = gtk_list_new();
	gtk_list_set_selection_mode(GTK_LIST(list), GTK_SELECTION_BROWSE);

	/* Fill datastruct */
	ld->idx = idx;
	ld->cnt = cnt;
	ld->h = h;
	set_columns(&ld->c, c, ddata, r);

	for (n = h - 1; n >= 0; n--) // Reverse numbering
	{
		item = gtk_list_item_new();
		gtk_object_set_user_data(GTK_OBJECT(item), (gpointer)n);
		gtk_container_add(GTK_CONTAINER(list), item);
// !!! Spacing = 3
		hbox = gtk_hbox_new(FALSE, 3);
		gtk_container_add(GTK_CONTAINER(item), hbox);

		for (j = 0; j < ld->c.ncol; j++)
		{
			void **cp = ld->c.columns[j][1];
			int op = (int)cp[0] & WB_OPMASK, jw = (int)cp[3];

			if (op == op_CHKCOLUMN)
			{
#if GTK_MAJOR_VERSION == 1
			/* !!! Vertical spacing is too small without the label */
				widget = gtk_check_button_new_with_label("");
#else /* if GTK_MAJOR_VERSION == 2 */
			/* !!! Focus frame is placed wrong with the empty label */
				widget = gtk_check_button_new();
#endif
				gtk_object_set_user_data(GTK_OBJECT(widget),
					(gpointer)j);
				gtk_signal_connect(GTK_OBJECT(widget), "toggled",
					GTK_SIGNAL_FUNC(listcc_toggled), ld);
			}
			else
			{
				if ((op == op_TXTCOLUMN) || (op == op_XTXTCOLUMN))
					txt[0] = '\0'; // Init to empty string
				else /* if (op == op_IDXCOLUMN) */ // Constant
					sprintf(txt, "%d", (int)cp[1] + (int)cp[2] * n);
				widget = gtk_label_new(txt);
				gtk_misc_set_alignment(GTK_MISC(widget),
					((jw >> 16) & 3) * 0.5, 0.5);
			}
			if (jw & 0xFFFF)
				gtk_widget_set_usize(widget, jw & 0xFFFF, -2);
			(op == op_XTXTCOLUMN ? xpack : pack)(hbox, widget);
		}
		gtk_widget_show_all(hbox);
	}

	r[0] = list; // Fix up slot
	if (*cnt) listcc_reset(r, -1);

	gtk_signal_connect(GTK_OBJECT(list), "select_child",
		GTK_SIGNAL_FUNC(listcc_select), ld);
	/* To move focus when delayed by insensitivity */
	widget = pack(hbox, gtk_label_new("")); // canary: updated last
	gtk_signal_connect(GTK_OBJECT(widget), "state_changed",
		GTK_SIGNAL_FUNC(listcc_update), r);

	return (list);
}
#endif

#ifndef U_LISTS_GTK1

#define LISTCC_KEY "mtPaint.listcc"

static void listcc_select(GtkTreeView *tree, gpointer user_data)
{
	listcc_data *dt = user_data;
	void **base, **desc, **slot = NEXT_SLOT(dt->c.r);
	GtkTreePath *tp;
	int l;

	if (dt->lock) return;
	/* Update the value */
	l = gtk_tree_model_iter_n_children(gtk_tree_view_get_model(tree), NULL);
	gtk_tree_view_get_cursor(tree, &tp, NULL);
	*dt->idx = l - 1 - gtk_tree_path_get_indices(tp)[0]; // Backward
	gtk_tree_path_free(tp);

	base = slot[0]; desc = slot[1];
	/* Call the handler */
	if (desc[1]) ((evt_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot);
}

static void listcc_select_item(void **slot)
{
	GtkTreeView *tr = slot[0];
	GtkTreeModel *tm = gtk_tree_view_get_model(tr);
	GtkTreePath *tp;
	listcc_data *dt = slot[2];
	int l, idx = *dt->idx; 

	l = gtk_tree_model_iter_n_children(tm, NULL);
	if ((idx < 0) || (idx >= l)) return;
	dt->lock++; // this is strictly a visual update
	tp = gtk_tree_path_new_from_indices(l - idx - 1, -1); // backward
	gtk_tree_view_set_cursor_on_cell(tr, tp, NULL, NULL, FALSE);
	gtk_tree_path_free(tp);
	dt->lock--;

	/* Signal must be reliable whatever happens */
	listcc_select(tr, dt);
}

void listcc_toggled(GtkCellRendererToggle *ren, gchar *path, gpointer user_data)
{
	void **slot = g_object_get_data(G_OBJECT(ren), LISTCC_KEY);
	void **base, **desc;
	listcc_data *dt = slot[2];
	GtkTreeView *tr = slot[0];
	GtkTreeModel *tm;
	GtkTreeIter it;
	gint yf;
	char *v;
	int col, row;

	if (dt->lock) return;
	/* Find out what happened to what, and where */
	tm = gtk_tree_view_get_model(tr);
	if (!gtk_tree_model_get_iter_from_string(tm, &it, path)) return; // Paranoia
	col = (int)user_data;
	gtk_tree_model_get(tm, &it, col, &yf, -1);
	gtk_list_store_set(GTK_LIST_STORE(tm), &it, col, yf ^= 1, -1); // Update list
	row = yf >> 1;

	/* Self-updating */
	v = get_cell(&dt->c, row, col);
	*(int *)v = yf & 1;
	/* Now call the handler */
	slot = dt->c.columns[col];
	slot = NEXT_SLOT(slot);
	base = slot[0]; desc = slot[1];
	if (desc[1]) ((evtx_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot, (void *)row);
}

static void listcc_reset(void **slot, int row)
{
	GtkWidget *w = slot[0];
	listcc_data *ld = slot[2];
	GtkListStore *ls;
	GtkTreeIter it;
	char txt[64];
	int j, n = 0, ncol = ld->c.ncol, cnt = *ld->cnt;

	ld->lock = TRUE;
	if (row >= 0) // specific row, not whole list
	{
		GtkTreePath *tp;
		GtkTreeModel *tm = gtk_tree_view_get_model(GTK_TREE_VIEW(w));
		int l = gtk_tree_model_iter_n_children(tm, NULL);

		ls = GTK_LIST_STORE(tm);
#if 0 /* LISTCC_RESET_ROW currently is NOT used on added/removed ones */
	/* Add/remove existing rows; expecting one row only so let updates be */
		if ((row >= cnt) && (row < l))
		{
			gtk_tree_model_get_iter_first(tm, &it);
			while (row < l--) gtk_list_store_remove(ls, &it);
		}
		else if ((row < cnt) && (row >= l))
		{
			while (row >= l++) gtk_list_store_prepend(ls, &it);
		}
#endif
		tp = gtk_tree_path_new_from_indices(l - 1 - row, -1);
		if (!gtk_tree_model_get_iter(tm, &it, tp))
			cnt = 0; // !!! Row out of range, do nothing
		gtk_tree_path_free(tp);
		n = row;
	}
	else // prepare new model
	{
		GType ctypes[MAX_COLS];

		for (j = 0; j < ncol; j++)
		{
			void **cp = ld->c.columns[j][1];
			int op = (int)cp[0] & WB_OPMASK;
			ctypes[j] = op == op_CHKCOLUMN ? G_TYPE_INT : G_TYPE_STRING;
		}
		ls = gtk_list_store_newv(ncol, ctypes);
	}

	for (; n < cnt; n++)
	{
		if (row < 0) gtk_list_store_prepend(ls, &it);
		for (j = 0; j < ncol; j++)
		{
			char *v = get_cell(&ld->c, n, j);
			void **cp = ld->c.columns[j][1];
			int op = (int)cp[0] & WB_OPMASK;

			if (op == op_CHKCOLUMN) // index * 2 + flag
				gtk_list_store_set(ls, &it, j, n * 2 + !!*(int *)v, -1);
			else
			{
				if (op == op_IDXCOLUMN) // Constant
					sprintf(v = txt, "%d", (int)cp[1] + (int)cp[2] * n);
				// op_TXTCOLUMN/op_XTXTCOLUMN otherwise
				gtk_list_store_set(ls, &it, j, v, -1);
			}
		}
		if (row >= 0) break; // one row only
	}

	if (row < 0)
	{
		gtk_tree_view_set_model(GTK_TREE_VIEW(w), GTK_TREE_MODEL(ls));
		listcc_select_item(slot); // only when full reset
	}
	ld->lock = FALSE;
}

static void listcc_chk(GtkTreeViewColumn *col, GtkCellRenderer *ren,
	GtkTreeModel *tm, GtkTreeIter *it, gpointer data)
{
	gint yf;

	gtk_tree_model_get(tm, it, (int)data, &yf, -1);
	g_object_set(ren, "active", yf & 1, NULL);
}

static void listcc_scroll_in(GtkWidget *widget, gpointer user_data)
{
	GtkTreePath *tp;

	gtk_tree_view_get_cursor(GTK_TREE_VIEW(widget), &tp, NULL);
	if (!tp) return; // Paranoia
	gtk_tree_view_scroll_to_cell(GTK_TREE_VIEW(widget), tp, NULL, FALSE, 0, 0);
	gtk_tree_path_free(tp);
}

// !!! With inlining this, problem also
GtkWidget *listcc(void **r, char *ddata, col_data *c)
{
	GtkWidget *w;
	listcc_data *ld = r[2];
	void **pp = r[1];
	int j, h, *cnt, *idx = r[0];


	cnt = (void *)(ddata + (int)pp[2]); // length pointer
	h = (int)pp[3]; // max for variable length
	if (h < *cnt) h = *cnt;

	/* Fill datastruct */
	ld->idx = idx;
	ld->cnt = cnt;
	ld->h = h;
	set_columns(&ld->c, c, ddata, r);

	w = gtk_tree_view_new();
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(w), FALSE);
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(w)),
		GTK_SELECTION_BROWSE);
	for (j = 0; j < ld->c.ncol; j++)
	{
		GtkCellRenderer *ren;
		GtkTreeViewColumn *col;
		void **cp = ld->c.columns[j][1];
		int op = (int)cp[0] & WB_OPMASK, jw = (int)cp[3];

		if (op == op_CHKCOLUMN)
		{
			ren = gtk_cell_renderer_toggle_new();
			/* To preserve spacing, need a height of a GtkLabel here;
			 * but waiting for realize to get & set it is a hassle,
			 * and the layers box looks perfectly OK as is - WJ */
//			g_object_set(ren, "height", 10, NULL);
			col = gtk_tree_view_column_new_with_attributes("", ren, NULL);
			gtk_tree_view_column_set_cell_data_func(col, ren,
				listcc_chk, (gpointer)j, NULL);
			g_object_set_data(G_OBJECT(ren), LISTCC_KEY, r);
			g_signal_connect(ren, "toggled",
				G_CALLBACK(listcc_toggled), (gpointer)j);
		}
		else /* op_TXTCOLUMN/op_XTXTCOLUMN/op_IDXCOLUMN */
		{
			ren = gtk_cell_renderer_text_new();
			gtk_cell_renderer_set_alignment(ren, ((jw >> 16) & 3) * 0.5, 0.5);
			col = gtk_tree_view_column_new_with_attributes("", ren,
				"text", j, NULL);
			if (op == op_XTXTCOLUMN)
				gtk_tree_view_column_set_expand(col, TRUE);
		}
		if (jw & 0xFFFF) g_object_set(ren, "width", jw & 0xFFFF, NULL);
// !!! Maybe gtk_tree_view_column_set_fixed_width(col, jw & 0xFFFF) instead?
		g_object_set(ren, "xpad", 2, NULL); // Looks good enough
		gtk_tree_view_append_column(GTK_TREE_VIEW(w), col);
	}
	gtk_tree_view_set_headers_visible(GTK_TREE_VIEW(w), FALSE);

	/* This will scroll selected row in on redisplay */
	g_signal_connect(w, "map", G_CALLBACK(listcc_scroll_in), NULL);

	r[0] = w; // Fix up slot
	if (*cnt) listcc_reset(r, -1);

	g_signal_connect(w, "cursor_changed", G_CALLBACK(listcc_select), ld);

	return (w);
}
#endif

//	LISTC widget

typedef struct {
	int kind;		// type of list
	int update;		// delayed update flags
	int lock;		// against in-reset signals
	int cntmax;		// maximum value from row map
	int *idx;		// result field
	int *cnt;		// length field
	int *sort;		// sort column & direction
	int **map;		// row map vector field
	void **change;		// slot for EVT_CHANGE
	void **ok;		// slot for EVT_OK
#ifdef U_LISTS_GTK1
	GtkWidget *sort_arrows[MAX_COLS];
	GdkPixmap *icons[2];
	GdkBitmap *masks[2];
#endif
	col_data c;
} listc_data;

#ifdef U_LISTS_GTK1
static gboolean listcx_key(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	listc_data *dt = user_data;
	GtkCList *clist = GTK_CLIST(widget);
	int row = 0;

	if (dt->lock) return (FALSE); // Paranoia
	switch (event->keyval)
	{
	case GDK_End: case GDK_KP_End:
		row = clist->rows - 1;
		// Fallthrough
	case GDK_Home: case GDK_KP_Home:
		clist->focus_row = row;
		gtk_clist_select_row(clist, row, 0);
		gtk_clist_moveto(clist, row, 0, 0.5, 0);
		return (TRUE);
	case GDK_Return: case GDK_KP_Enter:
		if (!dt->ok) break;
		get_evt_1(NULL, dt->ok);
		return (TRUE);
	}
	return (FALSE);
}

static gboolean listcx_click(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	listc_data *dt = user_data;
	GtkCList *clist = GTK_CLIST(widget);
	void **slot, **base, **desc;
	gint row, col;

	if (dt->lock) return (FALSE); // Paranoia
	if ((event->button != 3) || (event->type != GDK_BUTTON_PRESS))
		return (FALSE);
	if (!gtk_clist_get_selection_info(clist, event->x, event->y, &row, &col))
		return (FALSE);

	if (clist->focus_row != row)
	{
		clist->focus_row = row;
		gtk_clist_select_row(clist, row, 0);
	}

	slot = SLOT_N(dt->c.r, 2);
	base = slot[0]; desc = slot[1];
	if (desc[1]) ((evtx_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot, (void *)row);

	return (TRUE);
}

static void listcx_done(GtkCList *clist, listc_data *ld)
{
	int j;

	/* Free pixmaps */
	gdk_pixmap_unref(ld->icons[0]);
	gdk_pixmap_unref(ld->icons[1]);
	if (ld->masks[0]) gdk_pixmap_unref(ld->masks[0]);
	if (ld->masks[1]) gdk_pixmap_unref(ld->masks[1]);

	/* Remember column widths */
	for (j = 0; j < ld->c.ncol; j++)
	{
		void **cp = ld->c.columns[j][1];
		int op = (int)cp[0];
		int l = WB_GETLEN(op); // !!! -2 for each extra ref

		if (l > 4) inifile_set_gint32(cp[5], clist->column[j].width);
	}
}

static void listc_select_row(GtkCList *clist, gint row, gint column,
	GdkEventButton *event, gpointer user_data)
{
	listc_data *dt = user_data;
	void **slot = NEXT_SLOT(dt->c.r), **base = slot[0], **desc = slot[1];
	int dclick;

	if (dt->lock) return;
	dclick = dt->ok && event && (event->type == GDK_2BUTTON_PRESS);
	/* Update the value */
	*dt->idx = (int)gtk_clist_get_row_data(clist, row);
	/* Call the handler */
	if (desc[1]) ((evt_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot);
	/* Call the other handler */
	if (dclick) get_evt_1(NULL, dt->ok);
}

static void listc_update(GtkWidget *w, gpointer user_data)
{
	GtkCList *clist = GTK_CLIST(w);
	listc_data *ld = user_data;
	int what = ld->update;


	if (!GTK_WIDGET_MAPPED(w)) /* Is frozen anyway */
	{
		// Flag a waiting refresh
		if (what & 1) ld->update = (what & 2) | 4;
		return;
	}

	ld->update = 0;
	if (what & 4) /* Do a delayed refresh */
	{
		gtk_clist_freeze(clist);
		gtk_clist_thaw(clist);
	}
	if ((what & 2) && clist->selection) /* Do a scroll */
		gtk_clist_moveto(clist, (int)(clist->selection->data), 0, 0.5, 0);
}

static int listc_collect(gchar **row_text, gchar **row_pix, col_data *c, int row)
{
	int j, res = FALSE, ncol = c->ncol;

	if (row_pix) memset(row_pix, 0, ncol * sizeof(*row_pix));
	for (j = 0; j < ncol; j++)
	{
		char *v = get_cell(c, row, j);
		void **cp = c->columns[j][1];
		int op = (int)cp[0] & WB_OPMASK;

// !!! IDXCOLUMN not supported
		if (op == op_FILECOLUMN)
		{
			if (!row_pix || (v[0] == ' ')) v++;
			else row_pix[j] = v , v = "" , res = TRUE;
		}
		row_text[j] = v;
	}
	return (res);
}

static void listc_get_order(GtkCList *clist, int *res, int l)
{
	int i;

	for (i = 0; i < l; i++) res[i] = -1; // nowhere by default
	if (l > clist->rows) l = clist->rows;
	for (i = 0; i < l; i++)
		res[(int)gtk_clist_get_row_data(clist, i)] = i;
}

static int listc_sort(GtkCList *clist, listc_data *ld, int reselect)
{
	void **slot;

	/* Do nothing if empty */
	if (!*ld->cnt) return (0);

	/* Call & apply external sort */
	if ((slot = ld->change))
	{
		GList *tmp, *cur, **ll, *pos = NULL;
		int i, cnt, *map;

		/* Call the EVT_CHANGE handler, to sort map vector */
		get_evt_1(NULL, slot);

		/* !!! Directly rearrange widget's internals; it's deprecated
		 * so nothing inside will change anymore */
		gtk_clist_freeze(clist);

		if (clist->selection) pos = g_list_nth(clist->row_list,
			GPOINTER_TO_INT(clist->selection->data));

		ll = calloc(ld->cntmax + 1, sizeof(*ll));
		for (cur = clist->row_list; cur; cur = cur->next)
		{
			GtkCListRow *row = cur->data;
			ll[(int)row->data] = cur;
		}

		/* Rearrange rows */
		cnt = *ld->cnt;
		map = *ld->map;
		clist->row_list = cur = ll[map[0]];
		cur->prev = NULL;
		for (i = 1; i < cnt; i++)
		{
			cur->next = tmp = ll[map[i]];
			tmp->prev = cur;
			cur = tmp;
		}
		clist->row_list_end = cur;
		cur->next = NULL;

		free(ll);

		if (pos) /* Relocate existing selection */
		{
			int n = g_list_position(clist->row_list, pos);
			clist->selection->data = GINT_TO_POINTER(n);
			clist->focus_row = n;
		}

	}
	/* Do builtin sort */	
	else gtk_clist_sort(clist);

	if (!reselect || !clist->selection)
	{
		int n = gtk_clist_find_row_from_data(clist, (gpointer)*ld->idx);
		if (n < 0) *ld->idx = (int)gtk_clist_get_row_data(clist, n = 0);
		clist->focus_row = n;
		gtk_clist_select_row(clist, n, 0);
	}

	/* Done rearranging rows */
	if (slot)
	{
		gtk_clist_thaw(clist);
		return (1); // Refresh later if invisible now
	}

	/* !!! Builtin sort doesn't move focus along with selection, do it here */
	if (clist->selection)
	{
		int n = GPOINTER_TO_INT(clist->selection->data);
		if (clist->focus_row != n)
		{
			clist->focus_row = n;
			if (GTK_WIDGET_HAS_FOCUS((GtkWidget *)clist))
				return (4); // Refresh
		}
	}
	return (0);
}

static void listc_select_index(GtkWidget *widget, int v)
{
	GtkCList *clist = GTK_CLIST(widget);
	int row = gtk_clist_find_row_from_data(clist, (gpointer)v);

	if (row < 0) return; // Paranoia
	gtk_clist_select_row(clist, row, 0);
	/* !!! Focus fails to follow selection in browse mode - have to
	 * move it here, but a full redraw is necessary afterwards */
	if (clist->focus_row == row) return;
	clist->focus_row = row;
	if (GTK_WIDGET_HAS_FOCUS(widget) && !clist->freeze_count)
		gtk_widget_queue_draw(widget);
}

static void listc_reset_row(GtkCList *clist, listc_data *ld, int n)
{
	gchar *row_text[MAX_COLS];
	int i, row, ncol = ld->c.ncol;

	// !!! No support for anything but text columns
	listc_collect(row_text, NULL, &ld->c, n);
	row = gtk_clist_find_row_from_data(clist, (gpointer)n);
	for (i = 0; i < ncol; i++) gtk_clist_set_text(clist, row, i, row_text[i]);
}

/* !!! Should not redraw old things while resetting - or at least, not refer
 * outside of new data if doing it */
static void listc_reset(GtkCList *clist, listc_data *ld)
{
	int i, j, m, n, ncol = ld->c.ncol, cnt = *ld->cnt, *map = NULL;

	ld->lock = TRUE;
	gtk_clist_freeze(clist);
	gtk_clist_clear(clist);

	if (ld->map) map = *ld->map;
	for (m = i = 0; i < cnt; i++)
	{
		gchar *row_text[MAX_COLS], *row_pix[MAX_COLS];
		int row, pix;

		n = map ? map[i] : i;
		if (m < n) m = n;
		pix = listc_collect(row_text, row_pix, &ld->c, n);
		row = gtk_clist_append(clist, row_text);
		gtk_clist_set_row_data(clist, row, (gpointer)n);
		if (!pix) continue;
		for (j = 0; j < ncol; j++)
		{
			char *s = row_pix[j];
			if (!s) continue;
			pix = s[0] == 'D';
// !!! Spacing = 4
			gtk_clist_set_pixtext(clist, row, j, s + 1, 4,
				ld->icons[pix], ld->masks[pix]);
		}
	}
	ld->cntmax = m;

	/* Adjust column widths (not for draggable list) */
	if ((ld->kind != op_LISTCd) && (ld->kind != op_LISTCX))
	{
		for (j = 0; j < ncol; j++)
		{
// !!! Spacing = 5
			gtk_clist_set_column_width(clist, j,
				5 + gtk_clist_optimal_column_width(clist, j));
		}
	}

	i = *ld->idx;
	if (i >= cnt) i = cnt - 1;
	if (!cnt) *ld->idx = 0;	/* Safer than -1 for empty list */
	/* Draggable and unordered lists aren't sorted */
	else if ((ld->kind == op_LISTCd) || (ld->kind == op_LISTCu))
	{
		if (i < 0) i = 0;
		gtk_clist_select_row(clist, i, 0);
		*ld->idx = i;
	}
	else
	{
		*ld->idx = i;
		listc_sort(clist, ld, FALSE);
	}

	gtk_clist_thaw(clist);

	/* !!! Otherwise the newly empty rows are not cleared on Windows */
	gtk_widget_queue_draw((GtkWidget *)clist);

	ld->update |= 3;
	listc_update((GtkWidget *)clist, ld);
	ld->lock = FALSE;
}

static void listc_column_button(GtkCList *clist, gint col, gpointer user_data)
{
	listc_data *dt = user_data;
	int sort = *dt->sort;

	if (col < 0) col = abs(sort) - 1; /* Sort as is */
	else if (abs(sort) == col + 1) sort = -sort; /* Reverse same column */
	else /* Select another column */
	{
		gtk_widget_hide(dt->sort_arrows[abs(sort) - 1]);
		gtk_widget_show(dt->sort_arrows[col]);
		sort = col + 1;
	}
	*dt->sort = sort;

	gtk_clist_set_sort_column(clist, col);
	gtk_clist_set_sort_type(clist, sort > 0 ? GTK_SORT_ASCENDING :
		GTK_SORT_DESCENDING);
	gtk_arrow_set(GTK_ARROW(dt->sort_arrows[col]), sort > 0 ?
		GTK_ARROW_DOWN : GTK_ARROW_UP, GTK_SHADOW_IN);

	/* Sort and maybe redraw */
	dt->update |= listc_sort(clist, dt, TRUE);
	/* Scroll to selected row */
	dt->update |= 2;
	listc_update((GtkWidget *)clist, dt);
}

static void listc_sort_by(GtkCList *clist, listc_data *ld, int n)
{
	if (!*ld->sort) return;
	*ld->sort = n;
	listc_column_button(clist, -1, ld);
}

static void listc_prepare(GtkWidget *w, gpointer user_data)
{
	listc_data *ld = user_data;
	GtkCList *clist = GTK_CLIST(w);
	int j;

	if (ld->kind == op_LISTCX)
	{
		/* Ensure enough space for pixmaps */
		gtk_clist_set_row_height(clist, 0);
		if (clist->row_height < 16) gtk_clist_set_row_height(clist, 16);
	}

	/* Adjust width for columns which use sample text */
	for (j = 0; j < ld->c.ncol; j++)
	{
		void **cp = ld->c.columns[j][1];
		int op = (int)cp[0];
		int l = WB_GETLEN(op); // !!! -2 for each extra ref

		if (l < 6) continue;
		l = gdk_string_width(
#if GTK_MAJOR_VERSION == 1
			w->style->font,
#else /* if GTK_MAJOR_VERSION == 2 */
			gtk_style_get_font(w->style),
#endif
			cp[6]);
		if (clist->column[j].width < l)
			gtk_clist_set_column_width(clist, j, l);
	}

	/* To avoid repeating after unrealize (likely won't happen anyway) */
//	gtk_signal_disconnect_by_func(GTK_OBJECT(w),
//		GTK_SIGNAL_FUNC(listc_prepare), user_data);
}

// !!! With inlining this, problem also likely
GtkWidget *listc(void **r, char *ddata, col_data *c)
{
	static int zero = 0;
	GtkWidget *list, *hbox;
	GtkCList *clist;
	listc_data *ld = r[2];
	void **pp = r[1];
	int *cntv, *sort = &zero, **map = NULL;
	int j, w, sm, kind, heads = 0;


	cntv = (void *)(ddata + (int)pp[2]); // length var
	kind = (int)pp[0] & WB_OPMASK; // kind of list
	if ((kind == op_LISTCS) || (kind == op_LISTCX))
	{
		sort = (void *)(ddata + (int)pp[3]); // sort mode
		if (kind == op_LISTCX)
			map = (void *)(ddata + (int)pp[4]); // row map
	}

	list = gtk_clist_new(c->ncol);

	/* Fill datastruct */
	ld->kind = kind;
	ld->idx = r[0];
	ld->cnt = cntv;
	ld->sort = sort;
	ld->map = map;
	set_columns(&ld->c, c, ddata, r);

	sm = *sort;
	clist = GTK_CLIST(list);
	for (j = 0; j < ld->c.ncol; j++)
	{
		void **cp = ld->c.columns[j][1];
		int op = (int)cp[0], jw = (int)cp[3];
		int l = WB_GETLEN(op); // !!! -2 for each extra ref

		gtk_clist_set_column_resizeable(clist, j, kind == op_LISTCX);
		if ((w = jw & 0xFFFF))
		{
			if (l > 4) w = inifile_get_gint32(cp[5], w);
			gtk_clist_set_column_width(clist, j, w);
		}
		/* Left justification is default */
		jw = (jw >> 16) & 3;
		if (jw) gtk_clist_set_column_justification(clist, j,
			jw == 1 ? GTK_JUSTIFY_CENTER : GTK_JUSTIFY_RIGHT);

		hbox = gtk_hbox_new(FALSE, 0);
		(!jw ? pack : jw == 1 ? xpack : pack_end)(hbox,
			gtk_label_new((l > 3) && *(char *)cp[4] ? _(cp[4]) : ""));
		heads += l > 3;
		gtk_widget_show_all(hbox);
		// !!! Must be before gtk_clist_column_title_passive()
		gtk_clist_set_column_widget(clist, j, hbox);

		if (sm) ld->sort_arrows[j] = pack_end(hbox,
			gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_IN));
		else gtk_clist_column_title_passive(clist, j);
		if (kind == op_LISTCX) GTK_WIDGET_UNSET_FLAGS(
			GTK_CLIST(clist)->column[j].button, GTK_CAN_FOCUS);
	}

	if (sm)
	{
		int c = abs(sm) - 1;

		gtk_widget_show(ld->sort_arrows[c]);	// Show sort arrow
		gtk_clist_set_sort_column(clist, c);
		gtk_clist_set_sort_type(clist, sm > 0 ? GTK_SORT_ASCENDING :
			GTK_SORT_DESCENDING);
		gtk_arrow_set(GTK_ARROW(ld->sort_arrows[c]), sm > 0 ?
			GTK_ARROW_DOWN : GTK_ARROW_UP, GTK_SHADOW_IN);
		gtk_signal_connect(GTK_OBJECT(clist), "click_column",
			GTK_SIGNAL_FUNC(listc_column_button), ld);
	}

	if (sm || heads) gtk_clist_column_titles_show(clist); // Hide if useless
	gtk_clist_set_selection_mode(clist, GTK_SELECTION_BROWSE);

	if (kind == op_LISTCX)
	{
#ifdef GTK_STOCK_DIRECTORY
		ld->icons[1] = render_stock_pixmap(main_window,
			GTK_STOCK_DIRECTORY, &ld->masks[1]);
#endif
#ifdef GTK_STOCK_FILE
		ld->icons[0] = render_stock_pixmap(main_window,
			GTK_STOCK_FILE, &ld->masks[0]);
#endif
		if (!ld->icons[1]) ld->icons[1] = gdk_pixmap_create_from_xpm_d(
			main_window->window, &ld->masks[1], NULL, xpm_open_xpm);
		if (!ld->icons[0]) ld->icons[0] = gdk_pixmap_create_from_xpm_d(
			main_window->window, &ld->masks[0], NULL, xpm_new_xpm);

		gtk_signal_connect(GTK_OBJECT(clist), "key_press_event",
			GTK_SIGNAL_FUNC(listcx_key), ld);
		gtk_signal_connect(GTK_OBJECT(clist), "button_press_event",
			GTK_SIGNAL_FUNC(listcx_click), ld);
	}

	/* For some finishing touches */
	gtk_signal_connect(GTK_OBJECT(clist), "realize",
		GTK_SIGNAL_FUNC(listc_prepare), ld);

	/* This will apply delayed updates when they can take effect */
	gtk_signal_connect(GTK_OBJECT(clist), "map",
		GTK_SIGNAL_FUNC(listc_update), ld);

	if (*cntv) listc_reset(clist, ld);

	gtk_signal_connect(GTK_OBJECT(clist), "select_row",
		GTK_SIGNAL_FUNC(listc_select_row), ld);

	if (kind == op_LISTCd) clist_enable_drag(list); // draggable rows

	return (list);
}
#endif

#ifndef U_LISTS_GTK1
static GQuark listc_key;

/* Low 8 bits is column index, others denote type */
#define CELL_TEXT  0x000
#define CELL_FTEXT 0x100
#define CELL_ICON  0x200
#define CELL_TMASK 0xF00
#define CELL_XMASK 0x0FF

static gboolean listcx_click(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	listc_data *dt = user_data;
	GtkTreeView *tree = GTK_TREE_VIEW(widget);
	GtkTreePath *tp, *tp0;
	void **slot, **base, **desc;
	int row;

	if (dt->lock) return (FALSE); // Paranoia
	if ((event->button != 3) || (event->type != GDK_BUTTON_PRESS) ||
		(event->window != gtk_tree_view_get_bin_window(tree)) ||
		!gtk_tree_view_get_path_at_pos(tree, event->x, event->y, &tp,
			NULL, NULL, NULL)) return (FALSE);

	row = gtk_tree_path_get_indices(tp)[0];
	gtk_tree_view_get_cursor(tree, &tp0, NULL);
	/* Move cursor to where the click was */
	if (!tp0 || (row != gtk_tree_path_get_indices(tp0)[0]))
		gtk_tree_view_set_cursor_on_cell(tree, tp, NULL, NULL, FALSE);
	gtk_tree_path_free(tp0);
	gtk_tree_path_free(tp);

	slot = SLOT_N(dt->c.r, 2);
	base = slot[0]; desc = slot[1];
	if (desc[1]) ((evtx_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot, (void *)row);

	return (TRUE);
}

static void listcx_done(GtkTreeView *tree, listc_data *ld)
{
	int j;

	/* Remember column widths */
	for (j = 0; j < ld->c.ncol; j++)
	{
		void **cp = ld->c.columns[j][1];
		int op = (int)cp[0];
		int l = WB_GETLEN(op); // !!! -2 for each extra ref

		if (l > 4) inifile_set_gint32(cp[5], gtk_tree_view_column_get_width(
			gtk_tree_view_get_column(tree, j)));
	}
}

static void listcx_act(GtkTreeView *tree, GtkTreePath *tp,
	GtkTreeViewColumn *col, gpointer user_data)
{
	listc_data *dt = user_data;

	if (dt->lock) return; // Paranoia
	if (dt->ok) get_evt_1(NULL, dt->ok);
}

static void listc_select_row(GtkTreeView *tree, gpointer user_data)
{
	listc_data *dt = user_data;
	void **slot = NEXT_SLOT(dt->c.r), **base = slot[0], **desc = slot[1];
	GtkTreeModel *tm;
	GtkTreePath *tp;
	GtkTreeIter it;
	gint row;

	if (dt->lock) return;
	/* Update the value */
	gtk_tree_view_get_cursor(tree, &tp, NULL);
	tm = gtk_tree_view_get_model(tree);
	row = tp && gtk_tree_model_get_iter(tm, &it, tp);
	gtk_tree_path_free(tp);
	if (!row) return; // Paranoia
	gtk_tree_model_get(tm, &it, 0, &row, -1);
	*dt->idx = row;

	/* Call the handler */
	if (desc[1]) ((evt_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot);
}

static void listc_select_item(GtkTreeView *tree, int idx)
{
	GtkTreePath *tp = gtk_tree_path_new_from_indices(idx, -1);
	gtk_tree_view_set_cursor_on_cell(tree, tp, NULL, NULL, FALSE);
	gtk_tree_view_scroll_to_cell(tree, tp, NULL, TRUE, 0.5, 0.0);
	gtk_tree_path_free(tp);
}

/* If no such index, return row 0 with its index */
static void listc_find_index(GtkTreeModel *tm, int idx, int *whatwhere)
{
	GtkTreeIter it;
	int n = 0;
	gint pos;

	whatwhere[0] = whatwhere[1] = 0;
	if (!gtk_tree_model_get_iter_first(tm, &it)) return;
	gtk_tree_model_get(tm, &it, 0, &pos, -1);
	whatwhere[0] = pos;
	if (idx < 0) return;
	while (pos != idx)
	{
		if (!gtk_tree_model_iter_next(tm, &it)) return;
		gtk_tree_model_get(tm, &it, 0, &pos, -1);
		n++;
	}
	/* Found! */
	whatwhere[0] = idx;
	whatwhere[1] = n;
}

static void listc_select_index(GtkTreeView *tree, int idx)
{
	GtkTreeModel *tm = gtk_tree_view_get_model(tree);
	int whatwhere[2];

	listc_find_index(tm, idx, whatwhere);
	listc_select_item(tree, whatwhere[1]);
}

static void listc_get_order(GtkTreeView *tree, int *res, int l)
{
	GtkTreeModel *tm = gtk_tree_view_get_model(tree);
	GtkTreeIter it;
	int i, n = gtk_tree_model_iter_n_children(tm, NULL);
	gint pos;

	for (i = 0; i < l; i++) res[i] = -1; // nowhere by default
	if (l > n) l = n;
	if (!gtk_tree_model_get_iter_first(tm, &it)) return;
	for (i = 0; i < l; i++)
	{
		gtk_tree_model_get(tm, &it, 0, &pos, -1);
		res[pos] = i;
		if (!gtk_tree_model_iter_next(tm, &it)) return;
	}
}

static gint listc_sort_func(GtkTreeModel *tm, GtkTreeIter *a, GtkTreeIter *b,
	gpointer user_data)
{
	listc_data *ld = user_data;
	char *v0, *v1;
	int n, dir, cell, sort = *ld->sort;
	gint row0, row1;

	sort += !sort; // Default is column 0 in ascending order
	dir = sort > 0 ? 1 : -1;
	cell = sort * dir - 1;
	gtk_tree_model_get(tm, a, 0, &row0, -1);
	gtk_tree_model_get(tm, b, 0, &row1, -1);
	v0 = get_cell(&ld->c, row0, cell);
	v1 = get_cell(&ld->c, row1, cell);
	n = strcmp(v0 ? v0 : "", v1 ? v1 : ""); // Better safe than sorry
	if (!n) n = row0 - row1;
	return (n * dir);
}

static int listc_sort(GtkTreeView *tree, listc_data *ld, int reselect)
{
	GtkTreeModel *tm = gtk_tree_view_get_model(tree);
	GtkListStore *ls;
	GtkTreePath *tp;
	GtkTreeIter it;
	void **slot;
	int whatwhere[2];
	int i, have_pos, cnt = *ld->cnt, *map = NULL;
	gint pos = 0;

	/* Do nothing if empty */
	if (!cnt) return (0);

	ls = GTK_LIST_STORE(tm);
	/* Get current position if any */
	gtk_tree_view_get_cursor(tree, &tp, NULL);
	if ((have_pos = tp && gtk_tree_model_get_iter(tm, &it, tp)))
		gtk_tree_model_get(tm, &it, 0, &pos, -1);
	gtk_tree_path_free(tp);

	/* Call & apply external sort */
	if ((slot = ld->change))
	{
		/* Call the EVT_CHANGE handler, to sort map vector */
		get_evt_1(NULL, slot);

		/* Rearrange rows */ // !!! On unfreezed widget
		cnt = *ld->cnt;
		map = *ld->map;
		gtk_tree_model_get_iter_first(tm, &it);
		for (i = 0; i < cnt; i++)
		{
			gtk_list_store_set(ls, &it, 0, map[i], -1);
			gtk_tree_model_iter_next(tm, &it);
		}
	}
	/* Do builtin sort */
	else
	{
		// Tell it it's unsorted, first
		gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(tm),
			GTK_TREE_SORTABLE_UNSORTED_SORT_COLUMN_ID, GTK_SORT_ASCENDING);
		// Then set to sorted again; real sort order is in *ld->sort
		gtk_tree_sortable_set_sort_column_id(GTK_TREE_SORTABLE(tm),
			1, GTK_SORT_ASCENDING);
	}

	/* Relocate existing position (visual update) */
	if (reselect && have_pos)
	{
		listc_find_index(tm, pos, whatwhere);
		ld->lock++;
		listc_select_item(tree, whatwhere[1]);
		ld->lock--;
	}
	/* Set position anew */
	else
	{
		listc_find_index(tm, *ld->idx, whatwhere);
		*ld->idx = whatwhere[0];
		listc_select_item(tree, whatwhere[1]);
	}
	/* Done rearranging rows */
	return (!!slot);
}

/* !!! This will brutally recalculate the entire list; may be slow if large */
static void listc_optimal_width(GtkTreeView *tree, listc_data *ld)
{
	GtkTreeModel *tm = gtk_tree_view_get_model(tree);
	int i, j;
#if GTK_MAJOR_VERSION == 3
	/* !!! GTK+2 accounts for vertical separators, and resizes columns to fit
	 * header buttons; GTK+3 does neither */
	int hvis = gtk_tree_view_get_headers_visible(tree);
	gint vsep;
	gtk_widget_style_get(GTK_WIDGET(tree), "vertical-separator", &vsep, NULL);
#endif

	for (j = 0; j < ld->c.ncol; j++)
	{
		GtkTreeViewColumn *col = gtk_tree_view_get_column(tree, j);
		GtkTreeIter it;
		gint width;

		gtk_tree_view_column_queue_resize(col); // Reset width
		if (!gtk_tree_model_get_iter_first(tm, &it)) continue;
		for (i = 0; i < 500; i++) // Stop early if overlarge
		{
			gtk_tree_view_column_cell_set_cell_data(col, tm, &it,
				FALSE, FALSE);
			gtk_tree_view_column_cell_get_size(col, NULL, NULL, NULL,
				&width, NULL); // It returns max of all
			if (!gtk_tree_model_iter_next(tm, &it)) break;
		}
#if GTK_MAJOR_VERSION == 3
		if (hvis)
		{
			GtkWidget *button = gtk_tree_view_column_get_button(col);
			if (button)
			{
				gint bw;
				gtk_widget_get_preferred_width(button, &bw, NULL);
				if (width < bw) width = bw;
			}
		}
		width += vsep;
#endif
		gtk_tree_view_column_set_fixed_width(col, width);
	}
}

/* GtkTreeView here displays everything by reference, refill not needed */
#define listc_reset_row(A,B,C) gtk_widget_queue_draw(A)

/* !!! Should not redraw old things while resetting - or at least, not refer
 * outside of new data if doing it */
static void listc_reset(GtkTreeView *tree, listc_data *ld)
{
	GtkTreeModel *tm;
	int i, cnt = *ld->cnt, sort = *ld->sort;


	ld->lock = TRUE;
	tm = gtk_tree_view_get_model(tree);

	/* Rebuild the index vector, unless it surely stays the same */
	if (!tm || (gtk_tree_model_iter_n_children(tm, NULL) != cnt) ||
		(ld->kind == op_LISTCd) || ld->map)
	{
		GtkListStore *ls = gtk_list_store_new(1, G_TYPE_INT);
		GtkTreeIter it;
		int m, n, *map = ld->map ? *ld->map : NULL;

		for (m = i = 0; i < cnt; i++)
		{
			n = map ? map[i] : i;
			if (m < n) m = n;
			gtk_list_store_append(ls, &it);
			gtk_list_store_set(ls, &it, 0, n, -1);
		}
		tm = GTK_TREE_MODEL(ls);
		ld->cntmax = m;

		/* Let it sit there in case it's needed */
		gtk_tree_sortable_set_sort_func(GTK_TREE_SORTABLE(ls), 1,
			listc_sort_func, ld, NULL);
	}
	gtk_tree_view_set_model(tree, tm);

	/* !!! Sort arrow gets lost after resetting model */
	if (sort)
	{
		GtkTreeViewColumn *col = gtk_tree_view_get_column(tree, abs(sort) - 1);
		gtk_tree_view_column_set_sort_indicator(col, TRUE); // Show sort arrow
	}

	/* Adjust column widths (not for draggable list) */
	if ((ld->kind != op_LISTCd) && (ld->kind != op_LISTCX))
	{
		if (gtk_widget_get_mapped(GTK_WIDGET(tree)))
			listc_optimal_width(tree, ld);
		else ld->update |= 1; // Do it later
	}

	i = *ld->idx;
	if (i >= cnt) i = cnt - 1;
	if (!cnt) *ld->idx = 0;	/* Safer than -1 for empty list */
	/* Draggable and unordered lists aren't sorted */
	else if ((ld->kind == op_LISTCd) || (ld->kind == op_LISTCu))
	{
		if (i < 0) i = 0;
		listc_select_item(tree, i);
		*ld->idx = i;
	}
	else
	{
		*ld->idx = i;
		listc_sort(tree, ld, FALSE);
	}

	/* !!! Sometimes it shows the wrong part and redraw doesn't help */
	gtk_adjustment_value_changed(gtk_tree_view_get_vadjustment(tree));

	ld->lock = FALSE;
}

void listc_getcell(GtkTreeViewColumn *col, GtkCellRenderer *ren, GtkTreeModel *tm,
	GtkTreeIter *it, gpointer data)
{
	listc_data *dt = g_object_get_qdata(G_OBJECT(ren), listc_key);
	char *s;
	int cell = (int)data;
	gint row;

	gtk_tree_model_get(tm, it, 0, &row, -1);
	s = get_cell(&dt->c, row, cell & CELL_XMASK);
	if (!s) s = "\0"; // NULL is used for empty cells in some places
	cell &= CELL_TMASK;
	/* TEXT/FTEXT */
	if (cell != CELL_ICON)
		g_object_set(ren, "text", s + (cell == CELL_FTEXT), NULL);
	/* ICON */
	else g_object_set(ren, "visible", s[0] != ' ',
#if GTK_MAJOR_VERSION == 2
		"stock-id", (s[0] == 'D' ? GTK_STOCK_DIRECTORY : GTK_STOCK_FILE), NULL);
#else
		"icon-name", (s[0] == 'D' ? "folder" : "text-x-generic"), NULL);
#endif
}

#if GTK_MAJOR_VERSION == 2
#define gtk_tree_view_column_get_button(A) ((A)->button)
#endif

/* Use of sort arrows should NOT cause sudden redirect of keyboard input */
static void listc_defocus(GObject *obj, GParamSpec *pspec, gpointer user_data)
{
	GtkWidget *button = gtk_tree_view_column_get_button(GTK_TREE_VIEW_COLUMN(obj));
	if (!button || !gtk_widget_get_can_focus(button)) return;
	gtk_widget_set_can_focus(button, FALSE);
}

static void listc_column_button(GtkTreeViewColumn *col, gpointer user_data)
{
	listc_data *dt = g_object_get_qdata(G_OBJECT(col), listc_key);
	GtkTreeView *tree = GTK_TREE_VIEW(gtk_tree_view_column_get_tree_view(col));
	int sort = *dt->sort, idx = (int)user_data;

	if (idx > CELL_XMASK); /* Sort as is */
	else if (abs(sort) == idx + 1) sort = -sort; /* Reverse same column */
	else /* Select another column */
	{
		GtkTreeViewColumn *col0 = gtk_tree_view_get_column(tree, abs(sort) - 1);
		gtk_tree_view_column_set_sort_indicator(col0, FALSE);
		gtk_tree_view_column_set_sort_indicator(col, TRUE);
		sort = idx + 1;
	}
	*dt->sort = sort;

	gtk_tree_view_column_set_sort_order(col, sort > 0 ?
			GTK_SORT_ASCENDING : GTK_SORT_DESCENDING);

	/* Sort and maybe redraw */
	listc_sort(tree, dt, TRUE);
}

static void listc_sort_by(GtkTreeView *tree, listc_data *ld, int n)
{
	GtkTreeViewColumn *col;

	if (!*ld->sort) return;
	col = gtk_tree_view_get_column(tree, abs(*ld->sort) - 1);
	*ld->sort = n;
	listc_column_button(col, (gpointer)CELL_XMASK + 1); // Impossible index as flag
}

static void listc_update(GtkWidget *widget, gpointer user_data)
{
	listc_data *ld = user_data;
	int what = ld->update;

	ld->update = 0;
	if (what & 1) listc_optimal_width(GTK_TREE_VIEW(widget), ld);
}

static void listc_prepare(GtkWidget *widget, gpointer user_data)
{
	listc_data *ld = user_data;
	GtkTreeView *tree = GTK_TREE_VIEW(widget);
	PangoContext *context = gtk_widget_create_pango_context(widget);
	PangoLayout *layout = pango_layout_new(context);
	int j;

	for (j = 0; j < ld->c.ncol; j++)
	{
		void **cp = ld->c.columns[j][1];
		int op = (int)cp[0];
		int l = WB_GETLEN(op); // !!! -2 for each extra ref
		GtkTreeViewColumn *col = gtk_tree_view_get_column(tree, j);
		GtkWidget *button = gtk_tree_view_column_get_button(col);
		gint width, lw;

		/* Prevent sort buttons' narrowing down */
		if (ld->kind == op_LISTCS) widget_set_keepsize(button, FALSE);
		/* Adjust width for columns which use sample text */
		if (l < 6) continue;
		pango_layout_set_text(layout, cp[6], -1);
		pango_layout_get_pixel_size(layout, &lw, NULL);
		gtk_tree_view_column_cell_get_size(col, NULL, NULL, NULL, &width, NULL);
		if (width < lw) gtk_tree_view_column_set_fixed_width(col, lw);
	}
	g_object_unref(layout);
	g_object_unref(context);

	/* To avoid repeating after unrealize (likely won't happen anyway) */
//	g_signal_handlers_disconnect_by_func(widget, listc_prepare, user_data);
}

#define LISTC_XPAD 2

GtkWidget *listc(void **r, char *ddata, col_data *c)
{
	static int zero = 0;
	GtkWidget *w;
	GtkTreeView *tree;
	listc_data *ld = r[2];
	void **pp = r[1];
	int *cntv, *sort = &zero, **map = NULL;
	int j, sm, kind, heads = 0;


	listc_key = g_quark_from_static_string(LISTCC_KEY);

	cntv = (void *)(ddata + (int)pp[2]); // length var
	kind = (int)pp[0] & WB_OPMASK; // kind of list
	if ((kind == op_LISTCS) || (kind == op_LISTCX))
	{
		sort = (void *)(ddata + (int)pp[3]); // sort mode
		if (kind == op_LISTCX)
			map = (void *)(ddata + (int)pp[4]); // row map
	}

	w = gtk_tree_view_new();
	gtk_tree_view_set_enable_search(GTK_TREE_VIEW(w), FALSE);
	gtk_tree_selection_set_mode(gtk_tree_view_get_selection(GTK_TREE_VIEW(w)),
		GTK_SELECTION_BROWSE);

	/* Fill datastruct */
	ld->kind = kind;
	ld->idx = r[0];
	ld->cnt = cntv;
	ld->sort = sort;
	ld->map = map;
	set_columns(&ld->c, c, ddata, r);

	sm = *sort;
	tree = GTK_TREE_VIEW(w);
	for (j = 0; j < ld->c.ncol; j++)
	{
		GtkCellRenderer *ren;
		GtkTreeViewColumn *col = gtk_tree_view_column_new();
		void **cp = ld->c.columns[j][1];
		int op = (int)cp[0], jw = (int)cp[3];
		int width, l = WB_GETLEN(op); // !!! -2 for each extra ref

		op &= WB_OPMASK;
		gtk_tree_view_column_set_resizable(col, kind == op_LISTCX);
		if (kind == op_LISTCX) gtk_tree_view_column_set_sizing(col,
			GTK_TREE_VIEW_COLUMN_FIXED);
		if ((width = jw & 0xFFFF))
		{
			if (l > 4) width = inifile_get_gint32(cp[5], width);
			gtk_tree_view_column_set_fixed_width(col, width);
		}
		/* Left/center/right justification */
		jw = (jw >> 16) & 3;
		gtk_tree_view_column_set_alignment(col, jw * 0.5);
		gtk_tree_view_column_set_title(col, (l > 3) && *(char *)cp[4] ?
			_(cp[4]) : "");
		heads += l > 3;
		gtk_tree_view_column_set_expand(col, op == op_XTXTCOLUMN);
		if (sm)
		{
			g_signal_connect(col, "notify", G_CALLBACK(listc_defocus), NULL);
			g_signal_connect(col, "clicked",
				G_CALLBACK(listc_column_button), (gpointer)j);
			g_object_set_qdata(G_OBJECT(col), listc_key, ld);
		}
		if (op == op_FILECOLUMN)
		{
			ren = gtk_cell_renderer_pixbuf_new();
			g_object_set(ren, "stock-size", GTK_ICON_SIZE_SMALL_TOOLBAR,
				"xpad", LISTC_XPAD, NULL);
//			g_object_set(col, "spacing", 2); // !!! If ever needed
			gtk_tree_view_column_pack_start(col, ren, FALSE);
			gtk_tree_view_column_set_cell_data_func(col, ren, listc_getcell,
				(gpointer)(CELL_ICON + j), NULL);
			g_object_set_qdata(G_OBJECT(ren), listc_key, ld);
		}
		ren = gtk_cell_renderer_text_new();
		gtk_cell_renderer_set_alignment(ren, jw * 0.5, 0.5);
		g_object_set(ren, "xpad", LISTC_XPAD, NULL);
		gtk_tree_view_column_pack_start(col, ren, TRUE);
		gtk_tree_view_column_set_cell_data_func(col, ren, listc_getcell,
			(gpointer)((op == op_FILECOLUMN ? CELL_FTEXT : CELL_TEXT) + j), NULL);
		g_object_set_qdata(G_OBJECT(ren), listc_key, ld);

		gtk_tree_view_append_column(GTK_TREE_VIEW(w), col);
	}

	if (sm)
	{
		GtkTreeViewColumn *col = gtk_tree_view_get_column(tree, abs(sm) - 1);
		gtk_tree_view_column_set_sort_indicator(col, TRUE); // Show sort arrow
		gtk_tree_view_column_set_sort_order(col, sm > 0 ?
			GTK_SORT_ASCENDING : GTK_SORT_DESCENDING);
		gtk_tree_view_set_headers_clickable(tree, TRUE);
	}

	gtk_tree_view_set_headers_visible(tree, sm || heads); // Hide if useless

	if (kind == op_LISTCX)
	{
		g_signal_connect(w, "button_press_event", G_CALLBACK(listcx_click), ld);
		g_signal_connect(w, "row-activated", G_CALLBACK(listcx_act), ld);
	}

	/* For some finishing touches */
	g_signal_connect(w, "realize", G_CALLBACK(listc_prepare), ld);
	/* This will apply delayed updates when they can take effect */
	g_signal_connect(w, "map", G_CALLBACK(listc_update), ld);

	if (*cntv) listc_reset(tree, ld);

	g_signal_connect(w, "cursor_changed", G_CALLBACK(listc_select_row), ld);

	gtk_tree_view_set_reorderable(tree, kind == op_LISTCd); // draggable rows

	gtk_widget_show(w); /* !!! Need this for sizing-on-realize */

	return (w);
}
#endif

//	uLISTC widget

typedef struct {
	swdata s;
	col_data c;
} lswdata;

static int ulistc_reset(void **wdata)
{
	lswdata *ld = wdata[2];
	int l = *(int *)(ld->c.ddata + (int)GET_DESCV(wdata, 2));
	int idx = *(int *)ld->s.strs;

	/* Constrain the index */
	if (idx >= l) idx = l - 1;
	if (idx < 0) idx = 0;
	*(int *)ld->s.strs = idx;

	return (idx);
}

//	PATH widget

static void pathbox_button(GtkWidget *widget, gpointer user_data)
{
	void **slot = user_data, **desc = slot[1];
	void *xdata[2] = { _(desc[2]), slot }; // title and slot

	file_selector_x((int)desc[3], xdata);
}

// !!! With inlining this, problem also
GtkWidget *pathbox(void **r, int border)
{
	GtkWidget *hbox, *entry, *button;

	hbox = hbox_new(5 - 2);
	gtk_widget_show(hbox);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), border);

	entry = xpack(hbox, gtk_entry_new());
	button = pack(hbox, gtk_button_new_with_label(_("Browse")));
	gtk_container_set_border_width(GTK_CONTAINER(button), 2);
	gtk_widget_show(button);

	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(pathbox_button), r);

	return (entry);
}

/* Set value of path widget */
static void set_path(GtkWidget *widget, char *s, int mode)
{
	char path[PATHTXT];
	if (mode == PATH_VALUE) s = gtkuncpy(path, s, PATHTXT);
	gtk_entry_set_text(GTK_ENTRY(widget), s);
}

//	SPINPACK widget

#define TLSPINPACK_SIZE(P) (((int)(P)[2] * 2 + 1) * sizeof(void **))

static void spinpack_evt(GtkAdjustment *adj, gpointer user_data)
{
	void **tp = user_data, **vp = *tp, **r = *vp;
	void **slot, **base, **desc;
	char *ddata;
	int *v, idx = (tp - vp) / 2 - 1;

	if (!r) return; // Lock
	/* Locate the data */
	slot = NEXT_SLOT(r);
	base = slot[0];
	ddata = GET_DDATA(base);
	/* Locate the cell in array */
	desc = r[1];
	v = desc[1];
	if ((int)desc[0] & WB_FFLAG) v = (void *)(ddata + (int)v);
	v += idx * 3;
	/* Read the value */
	*v = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(*(tp - 1)));
	/* Call the handler */
	desc = slot[1];
	if (desc[1]) ((evtx_fn)desc[1])(ddata, base, (int)desc[0] & WB_OPMASK,
		slot, (void *)idx);
}

// !!! With inlining this, problem also
GtkWidget *tlspinpack(void **r, void **vp, GtkWidget *table, int wh)
{
	GtkWidget *widget = NULL;
	void **tp = vp, **pp = r[1];
	int row, column, i, l, n, *np = r[0];

	n = (int)pp[2];
	row = wh & 255;
	column = (wh >> 8) & 255;
	l = (wh >> 16) + 1;

	*tp++ = r;

	for (i = 0; i < n; i++ , np += 3)
	{
		int x = i % l, y = i / l;
		*tp++ = widget = add_a_spin(np[0], np[1], np[2]);
		/* Value might get clamped, and slot is self-reading so should
		 * reflect that */
		np[0] = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(widget));
		spin_connect(widget, GTK_SIGNAL_FUNC(spinpack_evt), tp);
// !!! Spacing = 2
		gtk_table_attach(GTK_TABLE(table), widget,
			column + x, column + x + 1, row + y, row + y + 1,
			GTK_EXPAND | GTK_FILL, 0, 0, 2);
		*tp++ = vp;
	}
	return (widget);
}

//	TLTEXT widget

// !!! Even with inlining this, some space gets wasted
void tltext(char *v, void **pp, GtkWidget *table, int pad)
{
	GtkWidget *label;
	char *tmp, *s;
	int x, wh, row, column;

	tmp = s = strdup(v);

	wh = (int)pp[2];
	row = wh & 255;
	x = column = (wh >> 8) & 255;

	while (TRUE)
	{
		int i = strcspn(tmp, "\t\n");
		int c = tmp[i];
		tmp[i] = '\0';
		label = gtk_label_new(tmp);
		gtk_label_set_justify(GTK_LABEL(label), GTK_JUSTIFY_LEFT);
		gtk_misc_set_alignment(GTK_MISC(label), 0.0, 0.5);
		gtk_widget_show(label);
		gtk_table_attach(GTK_TABLE(table), label, x, x + 1, row, row + 1,
			GTK_FILL, 0, pad, pad);
		x++;
		if (!c) break;
		if (c == '\n') x = column , row++;
		tmp += i + 1;
	}
	free(s);
}

//	TOOLBAR widget

/* !!! These pass the button slot, not event slot, as event source */

static void toolbar_lclick(GtkWidget *widget, gpointer user_data)
{
	void **slot = user_data;
	void **base = slot[0], **desc = slot[1];

	/* Ignore radio buttons getting depressed */
#if GTK_MAJOR_VERSION == 3
	if (GTK_IS_RADIO_TOOL_BUTTON(widget) && !gtk_toggle_tool_button_get_active(
		GTK_TOGGLE_TOOL_BUTTON(widget))) return;
	slot = g_object_get_qdata(G_OBJECT(widget), tool_key); // if initialized
#else /* #if GTK_MAJOR_VERSION <= 2 */
	if (GTK_IS_RADIO_BUTTON(widget) && !GTK_TOGGLE_BUTTON(widget)->active)
		return;
	slot = gtk_object_get_user_data(GTK_OBJECT(widget)); // if initialized
#endif
	if (slot) ((evt_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot);
}

static gboolean toolbar_rclick(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	void **slot = user_data;
	void **base = slot[0], **desc = slot[1];

	/* Handle only right clicks */
	if ((event->type != GDK_BUTTON_PRESS) || (event->button != 3))
		return (FALSE);
#if GTK_MAJOR_VERSION == 3
	slot = g_object_get_qdata(G_OBJECT(widget), tool_key); // if initialized
#else /* #if GTK_MAJOR_VERSION <= 2 */
	slot = gtk_object_get_user_data(GTK_OBJECT(widget)); // if initialized
#endif
	if (slot) ((evt_fn)desc[1])(GET_DDATA(base), base,
		(int)desc[0] & WB_OPMASK, slot);
	return (TRUE);
}

//	SMARTTBAR widget

#if GTK_MAJOR_VERSION == 3

/* This one handler, for keeping overflow menu untouchable */
static gboolean leave_be(GtkToolItem *tool_item, gpointer user_data)
{
	return (TRUE);
}

#else /* #if GTK_MAJOR_VERSION <= 2 */

/* The following is main toolbars auto-sizing code. If toolbar is too long for
 * the window, some of its items get hidden, but remain accessible through an
 * "overflow box" - a popup with 5xN grid of buttons inside. This way, we can
 * support small-screen devices without penalizing large-screen ones. - WJ */

typedef struct {
	void **r, **r2;	// slot and end-of-items slot
	GtkWidget *button, *tbar, *vport, *popup, *bbox;
} smarttbar_data;

#define WRAPBOX_W 5

static void wrapbox_size_req(GtkWidget *widget, GtkRequisition *req,
	gpointer user_data)
{
	GtkBox *box = GTK_BOX(widget);
	GtkBoxChild *child;
	GList *chain;
	GtkRequisition wreq;
	int cnt, nr, w, h, l, spacing;

	cnt = w = h = spacing = 0;
	for (chain = box->children; chain; chain = chain->next)
	{
		child = chain->data;
		if (!GTK_WIDGET_VISIBLE(child->widget)) continue;
		gtk_widget_size_request(child->widget, &wreq);
		if (w < wreq.width) w = wreq.width;
		if (h < wreq.height) h = wreq.height;
		cnt++;
	}
	if (cnt) spacing = box->spacing;
	nr = (cnt + WRAPBOX_W - 1) / WRAPBOX_W;
	cnt = nr > 1 ? WRAPBOX_W : cnt; 

	l = GTK_CONTAINER(widget)->border_width * 2 - spacing;
	req->width = (w + spacing) * cnt + l;
	req->height = (h + spacing) * nr + l;
}

static void wrapbox_size_alloc(GtkWidget *widget, GtkAllocation *alloc,
	gpointer user_data)
{
	GtkBox *box = GTK_BOX(widget);
	GtkBoxChild *child;
	GList *chain;
	GtkRequisition wreq;
	GtkAllocation wall;
	int idx, cnt, nr, l, w, h, ww, wh, spacing;

	widget->allocation = *alloc;

	/* Count widgets */
	cnt = w = h = 0;
	for (chain = box->children; chain; chain = chain->next)
	{
		child = chain->data;
		if (!GTK_WIDGET_VISIBLE(child->widget)) continue;
		gtk_widget_get_child_requisition(child->widget, &wreq);
		if (w < wreq.width) w = wreq.width;
		if (h < wreq.height) h = wreq.height;
		cnt++;
	}
	if (!cnt) return; // Nothing needs positioning in here
	nr = (cnt + WRAPBOX_W - 1) / WRAPBOX_W;
	cnt = nr > 1 ? WRAPBOX_W : cnt; 

	/* Adjust sizes (homogeneous, shrinkable, no expand, no fill) */
	l = GTK_CONTAINER(widget)->border_width;
	spacing = box->spacing;
	ww = alloc->width - l * 2 + spacing;
	wh = alloc->height - l * 2 + spacing;
	if ((w + spacing) * cnt > ww) w = ww / cnt - spacing;
	if (w < 1) w = 1;
	if ((h + spacing) * nr > wh) h = wh / nr - spacing;
	if (h < 1) h = 1;

	/* Now position the widgets */
	wall.height = h;
	wall.width = w;
	idx = 0;
	for (chain = box->children; chain; chain = chain->next)
	{
		child = chain->data;
		if (!GTK_WIDGET_VISIBLE(child->widget)) continue;
		wall.x = alloc->x + l + (w + spacing) * (idx % WRAPBOX_W);
		wall.y = alloc->y + l + (h + spacing) * (idx / WRAPBOX_W);
		gtk_widget_size_allocate(child->widget, &wall);
		idx++;
	}
}

static int split_toolbar_at(GtkWidget *tbar, int w)
{
	GList *chain;
	GtkToolbarChild *child;
	GtkAllocation *alloc;
	int border, x = 0;

	if (w < 1) w = 1;
	if (!tbar) return (w);
	border = GTK_CONTAINER(tbar)->border_width;
	for (chain = GTK_TOOLBAR(tbar)->children; chain; chain = chain->next)
	{
		child = chain->data;
		if (child->type == GTK_TOOLBAR_CHILD_SPACE) continue;
		if (!GTK_WIDGET_VISIBLE(child->widget)) continue;
		alloc = &child->widget->allocation;
		if (alloc->x < w)
		{
			if (alloc->x + alloc->width <= w)
			{
				x = alloc->x + alloc->width;
				continue;
			}
			w = alloc->x;
		}
		if (!x) return (1); // Nothing to see here
		return (x + border > w ? x : x + border);
	}
	return (w); // Toolbar is empty
}

static void htoolbox_size_req(GtkWidget *widget, GtkRequisition *req,
	gpointer user_data)
{
	smarttbar_data *sd = user_data;
	GtkBox *box = GTK_BOX(widget);
	GtkBoxChild *child;
	GList *chain;
	GtkRequisition wreq;
	int cnt, w, h, l;

	cnt = w = h = 0;
	for (chain = box->children; chain; chain = chain->next)
	{
		child = chain->data;
		if (!GTK_WIDGET_VISIBLE(child->widget)) continue;
		gtk_widget_size_request(child->widget, &wreq);
		if (h < wreq.height) h = wreq.height;
		/* Button adds no extra width */
		if (child->widget == sd->button) continue;
		w += wreq.width + child->padding * 2;
		cnt++;
	}
	if (cnt > 1) w += (cnt - 1) * box->spacing;
	l = GTK_CONTAINER(widget)->border_width * 2;
	req->width = w + l;
	req->height = h + l;
}

static void htoolbox_size_alloc(GtkWidget *widget, GtkAllocation *alloc,
	gpointer user_data)
{
	smarttbar_data *sd = user_data;
	GtkBox *box = GTK_BOX(widget);
	GtkBoxChild *child;
	GList *chain;
	GtkRequisition wreq;
	GtkAllocation wall;
	int vw, bw, xw, dw, pad, spacing;
	int cnt, l, x, wrkw;

	widget->allocation = *alloc;

	/* Calculate required size */
	cnt = 0;
	vw = bw = xw = 0;
	spacing = box->spacing;
	for (chain = box->children; chain; chain = chain->next)
	{
		GtkWidget *w;

		child = chain->data;
		pad = child->padding * 2;
		w = child->widget;
		if (w == sd->button)
		{
			gtk_widget_size_request(w, &wreq);
			bw = wreq.width + pad + spacing; // Button
		}
		else if (GTK_WIDGET_VISIBLE(w))
		{
			gtk_widget_get_child_requisition(w, &wreq);
			if (w == sd->vport) vw = wreq.width; // Viewport
			else xw += wreq.width; // Extra widgets
			xw += pad;
			cnt++;
		}
	}
	if (cnt > 1) xw += (cnt - 1) * spacing;
	cnt -= !!vw; // Now this counts visible extra widgets
	l = GTK_CONTAINER(widget)->border_width;
	xw += l * 2;
	if (vw && (xw + vw > alloc->width)) /* If viewport doesn't fit */
		vw = split_toolbar_at(sd->tbar, alloc->width - xw - bw);
	else bw = 0;

	/* Calculate how much to reduce extra widgets' sizes */
	dw = 0;
	if (cnt) dw = (xw + bw + vw - alloc->width + cnt - 1) / cnt;
	if (dw < 0) dw = 0;

	/* Now position the widgets */
	x = alloc->x + l;
	wall.y = alloc->y + l;
	wall.height = alloc->height - l * 2;
	if (wall.height < 1) wall.height = 1;
	for (chain = box->children; chain; chain = chain->next)
	{
		GtkWidget *w;

		child = chain->data;
		pad = child->padding;
		w = child->widget;
		/* Button uses size, the others, visibility */
		if (w == sd->button ? !bw : !GTK_WIDGET_VISIBLE(w)) continue;
		gtk_widget_get_child_requisition(w, &wreq);
		wrkw = w == sd->vport ? vw : w == sd->button ? wreq.width :
			wreq.width - dw;
		if (wrkw < 1) wrkw = 1;
		wall.width = wrkw;
		x = (wall.x = x + pad) + wrkw + pad + spacing;
		gtk_widget_size_allocate(w, &wall);
	}

	if (sd->button) widget_showhide(sd->button, bw);
}

static void htoolbox_popup(GtkWidget *button, gpointer user_data)
{
	smarttbar_data *sd = user_data;
	GtkWidget *popup = sd->popup;
	GtkAllocation *alloc = &button->allocation;
	GtkRequisition req;
	GtkBox *box;
	GtkBoxChild *child;
	GList *chain;
	gint x, y, w, h, vl;

	/* Pre-grab; use an already visible widget */
	if (!do_grab(GRAB_PROGRAM, button, NULL)) return;

	/* Position the popup */
#if GTK2VERSION >= 2 /* GTK+ 2.2+ */
	{
		GdkScreen *screen = gtk_widget_get_screen(button);
		w = gdk_screen_get_width(screen);
		h = gdk_screen_get_height(screen);
		/* !!! To have styles while unrealized, need at least this */
		gtk_window_set_screen(GTK_WINDOW(popup), screen);
	}
#else
	w = gdk_screen_width();
	h = gdk_screen_height();
#endif
	vl = sd->vport->allocation.width;
	box = GTK_BOX(sd->bbox);
	for (chain = box->children; chain; chain = chain->next)
	{
		GtkWidget *btn, *tool;
		void **slot;

		child = chain->data;
		btn = child->widget;
		slot = gtk_object_get_user_data(GTK_OBJECT(btn));
		if (!slot) continue; // Paranoia
		tool = slot[0];
		/* Copy button relief setting of toolbar buttons */
		gtk_button_set_relief(GTK_BUTTON(btn),
			gtk_button_get_relief(GTK_BUTTON(tool)));
		/* Copy their sensitivity */
		gtk_widget_set_sensitive(GTK_WIDGET(btn),
			GTK_WIDGET_SENSITIVE(GTK_WIDGET(tool)));
		/* Copy their state (feedback is disabled while invisible) */
		if (GTK_IS_TOGGLE_BUTTON(btn)) gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(btn), GTK_TOGGLE_BUTTON(tool)->active);
//		gtk_widget_set_style(btn, gtk_rc_get_style(tool));
		/* Set visibility */
		widget_showhide(btn, GTK_WIDGET_VISIBLE(tool) &&
			(tool->allocation.x >= vl));
	}
	gtk_widget_size_request(popup, &req);
	gdk_window_get_origin(GTK_WIDGET(sd->r[0])->window, &x, &y);
	x += alloc->x + (alloc->width - req.width) / 2;
	y += alloc->y + alloc->height;
	if (x + req.width > w) x = w - req.width;
	if (x < 0) x = 0;
	if (y + req.height > h) y -= alloc->height + req.height;
	if (y + req.height > h) y = h - req.height;
	if (y < 0) y = 0;
#if GTK_MAJOR_VERSION == 1
	gtk_widget_realize(popup);
	gtk_window_reposition(GTK_WINDOW(popup), x, y);
#else /* #if GTK_MAJOR_VERSION == 2 */
	gtk_window_move(GTK_WINDOW(popup), x, y);
#endif

	/* Actually popup it */
	gtk_widget_show(popup);
	gtk_window_set_focus(GTK_WINDOW(popup), NULL); // Nothing is focused
	gdk_flush(); // !!! To accept grabs, window must be actually mapped

	/* Transfer grab to it */
	do_grab(GRAB_WIDGET, popup, NULL);
}

static void htoolbox_popdown(GtkWidget *widget)
{
	undo_grab(widget);
	gtk_widget_hide(widget);
}

static void htoolbox_unrealize(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *popup = user_data;

	if (GTK_WIDGET_VISIBLE(popup)) htoolbox_popdown(popup);
	gtk_widget_unrealize(popup);
}

static gboolean htoolbox_popup_key(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	if ((event->keyval != GDK_Escape) || (event->state & (GDK_CONTROL_MASK |
		GDK_SHIFT_MASK | GDK_MOD1_MASK))) return (FALSE);
	htoolbox_popdown(widget);
	return (TRUE);
}

static gboolean htoolbox_popup_click(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	GtkWidget *ev = gtk_get_event_widget((GdkEvent *)event);

	/* Clicks on popup's descendants are OK; otherwise, remove the popup */
	if (ev != widget)
	{
		while (ev)
		{
			ev = ev->parent;
			if (ev == widget) return (FALSE);
		}
	}
	htoolbox_popdown(widget);
	return (TRUE);
}

static void htoolbox_tool_clicked(GtkWidget *button, gpointer user_data)
{
	smarttbar_data *sd = user_data;
	void **slot;

	/* Invisible buttons don't send (virtual) clicks to toolbar */
	if (!GTK_WIDGET_VISIBLE(sd->popup)) return;
	/* Ignore radio buttons getting depressed */
	if (GTK_IS_RADIO_BUTTON(button) && !GTK_TOGGLE_BUTTON(button)->active)
		return;
	htoolbox_popdown(sd->popup);
	slot = gtk_object_get_user_data(GTK_OBJECT(button));
	gtk_button_clicked(GTK_BUTTON(slot[0]));
}

static gboolean htoolbox_tool_rclick(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	smarttbar_data *sd = user_data;
	void **slot;

	/* Handle only right clicks */
	if ((event->type != GDK_BUTTON_PRESS) || (event->button != 3))
		return (FALSE);
	htoolbox_popdown(sd->popup);
	slot = gtk_object_get_user_data(GTK_OBJECT(widget));
	return (toolbar_rclick(slot[0], event, SLOT_N(sd->r, 2)));
}

// !!! With inlining this, problem also
GtkWidget *smarttbar_button(smarttbar_data *sd, char *v)
{
	GtkWidget *box = sd->r[0], *ritem = NULL;
	GtkWidget *button, *arrow, *popup, *ebox, *frame, *bbox, *item;
	void **slot, *rvar = MEM_NONE;

	sd->button = button = pack(box, gtk_button_new());
#if GTK_MAJOR_VERSION == 1
	// !!! Arrow w/o shadow is invisible in plain GTK+1
	arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_OUT);
#else /* #if GTK_MAJOR_VERSION == 2 */
	arrow = gtk_arrow_new(GTK_ARROW_DOWN, GTK_SHADOW_NONE);
#endif
	gtk_widget_show(arrow);
	gtk_container_add(GTK_CONTAINER(button), arrow);
#if GTK2VERSION >= 4 /* GTK+ 2.4+ */
	gtk_button_set_focus_on_click(GTK_BUTTON(button), FALSE);
#endif
	if (v) gtk_tooltips_set_tip(GTK_TOOLBAR(sd->tbar)->tooltips, button,
		_(v), "Private");

	sd->popup = popup = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_policy(GTK_WINDOW(popup), FALSE, FALSE, TRUE);
#if GTK2VERSION >= 10 /* GTK+ 2.10+ */
	gtk_window_set_type_hint(GTK_WINDOW(popup), GDK_WINDOW_TYPE_HINT_COMBO);
#endif
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(htoolbox_popup), sd);
	gtk_signal_connect(GTK_OBJECT(box), "unrealize",
		GTK_SIGNAL_FUNC(htoolbox_unrealize), popup);
	gtk_signal_connect_object(GTK_OBJECT(box), "destroy",
		GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(popup));
	/* Eventbox covers the popup, and popup has a grab; then, all clicks
	 * inside the popup get its descendant as event widget; anything else,
	 * including popup window itself, means click was outside, and triggers
	 * popdown (solution from GtkCombo) - WJ */
	ebox = gtk_event_box_new();
	gtk_container_add(GTK_CONTAINER(popup), ebox);
	frame = gtk_frame_new(NULL);
	gtk_frame_set_shadow_type(GTK_FRAME(frame), GTK_SHADOW_OUT);
	gtk_container_add(GTK_CONTAINER(ebox), frame);

	sd->bbox = bbox = wj_size_box();
	gtk_signal_connect(GTK_OBJECT(bbox), "size_request",
		GTK_SIGNAL_FUNC(wrapbox_size_req), NULL);
	gtk_signal_connect(GTK_OBJECT(bbox), "size_allocate",
		GTK_SIGNAL_FUNC(wrapbox_size_alloc), NULL);
	gtk_container_add(GTK_CONTAINER(frame), bbox);

	gtk_widget_show_all(ebox);
	gtk_signal_connect(GTK_OBJECT(popup), "key_press_event",
		GTK_SIGNAL_FUNC(htoolbox_popup_key), NULL);
	gtk_signal_connect(GTK_OBJECT(popup), "button_press_event",
		GTK_SIGNAL_FUNC(htoolbox_popup_click), NULL);

	for (slot = sd->r; slot - sd->r2 < 0; slot = NEXT_SLOT(slot))
	{
		void **desc = slot[1];
		int l, op = (int)desc[0];

		l = WB_GETLEN(op);
		op &= WB_OPMASK;
		if (op == op_TBBUTTON) item = gtk_button_new();
		else if (op == op_TBTOGGLE) item = gtk_toggle_button_new();
		else if (op == op_TBRBUTTON)
		{
			ritem = item = gtk_radio_button_new_from_widget(
				rvar != desc[1] ? NULL : GTK_RADIO_BUTTON(ritem));
			rvar = desc[1];
			/* !!! Flags are ignored; can XOR desc[0]'s to compare
			 * them too, but false match improbable anyway - WJ */
			gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(item), FALSE);
		}
		else continue; // Not a regular toolbar button
#if GTK2VERSION >= 4 /* GTK+ 2.4+ */
		gtk_button_set_focus_on_click(GTK_BUTTON(item), FALSE);
#endif
		gtk_container_add(GTK_CONTAINER(item), xpm_image(desc[4]));
		pack(bbox, item);
		gtk_tooltips_set_tip(GTK_TOOLBAR(sd->tbar)->tooltips, item,
			_(desc[3]), "Private");
		gtk_object_set_user_data(GTK_OBJECT(item), slot);
		gtk_signal_connect(GTK_OBJECT(item), "clicked",
			GTK_SIGNAL_FUNC(htoolbox_tool_clicked), sd);
		if (l > 4) gtk_signal_connect(GTK_OBJECT(item), "button_press_event",
			GTK_SIGNAL_FUNC(htoolbox_tool_rclick), sd);
	}

	return (button);
}

//	TWOBOX widget

#define TWOBAR_KEY "mtPaint.twobar"

static void twobar_size_req(GtkWidget *widget, GtkRequisition *req,
	gpointer user_data)
{
	GtkBox *box = GTK_BOX(widget);
	GtkBoxChild *child;
	GtkRequisition wreq1, wreq2;
	int l;


	wreq1.width = wreq1.height = 0;
	wreq2 = wreq1;
	if (box->children)
	{
		child = box->children->data;
		if (GTK_WIDGET_VISIBLE(child->widget))
			gtk_widget_size_request(child->widget, &wreq1);
		if (box->children->next)
		{
			child = box->children->next->data;
			if (GTK_WIDGET_VISIBLE(child->widget))
				gtk_widget_size_request(child->widget, &wreq2);
		}
	}

	l = box->spacing;
	/* One or none */
	if (!wreq2.width);
	else if (!wreq1.width) wreq1 = wreq2;
	/* Two in one row */
	else if (gtk_object_get_data(GTK_OBJECT(widget), TWOBAR_KEY))
	{
		wreq1.width += wreq2.width + l;
		if (wreq1.height < wreq2.height) wreq1.height = wreq2.height;
	}
	/* Two rows (default) */
	else
	{	
		wreq1.height += wreq2.height + l;
		if (wreq1.width < wreq2.width) wreq1.width = wreq2.width;
	}
	/* !!! Children' padding is ignored (it isn't used anyway) */

	l = GTK_CONTAINER(widget)->border_width * 2;

#if GTK_MAJOR_VERSION == 1
	/* !!! GTK+1 doesn't want to reallocate upper-level containers when
	 * something on lower level gets downsized */
	if (widget->requisition.height > wreq1.height + l) force_resize(widget);
#endif

	req->width = wreq1.width + l;
	req->height = wreq1.height + l;
}

static void twobar_size_alloc(GtkWidget *widget, GtkAllocation *alloc,
	gpointer user_data)
{
	GtkBox *box = GTK_BOX(widget);
	GtkBoxChild *child, *child2 = NULL;
	GtkRequisition wreq1, wreq2;
	GtkAllocation wall;
	int l, h, w2, ww, wh, bar, oldbar;


	widget->allocation = *alloc;

	if (!box->children) return; // Empty
	child = box->children->data;
	if (box->children->next)
	{
		child2 = box->children->next->data;
		if (!GTK_WIDGET_VISIBLE(child2->widget)) child2 = NULL;
	}
	if (!GTK_WIDGET_VISIBLE(child->widget)) child = child2 , child2 = NULL;
	if (!child) return;

	l = GTK_CONTAINER(widget)->border_width;
	wall.x = alloc->x + l;
	wall.y = alloc->y + l;
	l *= 2;
	ww = alloc->width - l;
	if (ww < 1) ww = 1;
	wall.width = ww;
	wh = alloc->height - l;
	if (wh < 1) wh = 1;
	wall.height = wh; 

	if (!child2) /* Place one, and be done */
	{
		gtk_widget_size_allocate(child->widget, &wall);
		return;
	}

	/* Need to arrange two */
	gtk_widget_get_child_requisition(child->widget, &wreq1);
	gtk_widget_get_child_requisition(child2->widget, &wreq2);
	l = box->spacing;
	w2 = wreq1.width + wreq2.width + l;
	h = wreq1.height;
	if (h < wreq2.height) h = wreq2.height;

	bar = w2 <= ww; /* Can do one row */
	if (bar)
	{
		if (wall.height > h) wall.height = h;
		l += (wall.width = wreq1.width);
		gtk_widget_size_allocate(child->widget, &wall);
		wall.x += l;
		wall.width = ww - l;
	}
	else /* Two rows */
	{
		l += (wall.height = wreq1.height);
		gtk_widget_size_allocate(child->widget, &wall);
		wall.y += l;
		wall.height = wh - l;
		if (wall.height < 1) wall.height = 1;
	}
	gtk_widget_size_allocate(child2->widget, &wall);

	oldbar = (int)gtk_object_get_data(GTK_OBJECT(widget), TWOBAR_KEY);
	if (bar != oldbar) /* Shape change */
	{
		gtk_object_set_data(GTK_OBJECT(widget), TWOBAR_KEY, (gpointer)bar);
		/* !!! GTK+1 doesn't handle requeued resizes properly */
#if GTK_MAJOR_VERSION == 1
		force_resize(widget);
#else
		gtk_widget_queue_resize(widget);
#endif
	}
}

#endif /* GTK+1&2 */

//	MENUBAR widget

/* !!! This passes the item slot, not event slot, as event source */

static void menu_evt(GtkWidget *widget, gpointer user_data)
{
	void **slot = user_data;
	void **base = slot[0], **desc = slot[1];

	/* Ignore radio buttons getting depressed */
#if GTK_MAJOR_VERSION == 3
	if (GTK_IS_RADIO_MENU_ITEM(widget) && !gtk_check_menu_item_get_active(
		GTK_CHECK_MENU_ITEM(widget))) return;
	slot = g_object_get_qdata(G_OBJECT(widget), tool_key); // if initialized
#else /* #if GTK_MAJOR_VERSION <= 2 */
	if (GTK_IS_RADIO_MENU_ITEM(widget) && !GTK_CHECK_MENU_ITEM(widget)->active)
		return;
	slot = gtk_object_get_user_data(GTK_OBJECT(widget));
#endif
	((evt_fn)desc[1])(GET_DDATA(base), base, (int)desc[0] & WB_OPMASK, slot);
}

#if (GTK_MAJOR_VERSION == 3) || (GTK2VERSION >= 4) /* Not needed before GTK+ 2.4 */

/* Ignore shortcut key only when item itself is insensitive or hidden */
static gboolean menu_allow_key(GtkWidget *widget, guint signal_id, gpointer user_data)
{
	return (GTK_WIDGET_IS_SENSITIVE(widget) && GTK_WIDGET_VISIBLE(widget));
}

#endif

//	SMARTMENU widget

/* The following is main menu auto-rearrange code. If the menu is too long for
 * the window, some of its items are moved into "overflow" submenu - and moved
 * back to menubar when the window is made wider. This way, we can support
 * small-screen devices without penalizing large-screen ones. - WJ */

#define MENU_RESIZE_MAX 16

typedef struct {
	void **slot;
	GtkWidget *fallback;
	guint key;
	int width;
} r_menu_slot;

typedef struct {
	void **r; // own slot
	GtkWidget *mbar;
	int r_menu_state;
	int in_alloc;
	r_menu_slot r_menu[MENU_RESIZE_MAX];
} smartmenu_data;

/* Handle keyboard accels for overflow menu */
static int check_smart_menu_keys(void *sdata, GdkEventKey *event)
{
	smartmenu_data *sd = sdata;
	r_menu_slot *slot;
	guint lowkey;
	int l = sd->r_menu_state;

	/* No overflow - nothing to do */
	if (!l) return (FALSE);
	/* Menu hidden - do nothing */
	if (!GTK_WIDGET_VISIBLE(sd->r[0])) return (FALSE);
	/* Alt+key only */
	if ((event->state & _CSAmask) != _Amask) return (FALSE);

	lowkey = low_key(event);
	for (slot = sd->r_menu + 1; slot->key != lowkey; slot++)
		if (--l <= 0) return (FALSE); // No such key in overflow

	/* Just popup - if we're here, overflow menu is offscreen anyway */
	gtk_menu_popup(GTK_MENU(gtk_menu_item_get_submenu(GTK_MENU_ITEM(slot->fallback))),
		NULL, NULL, NULL, NULL, 0, 0);
	return (TRUE);
}

#if GTK_MAJOR_VERSION <= 2 /* !!! FOR NOW */

/* Invalidate width cache after width-affecting change */
static void check_width_cache(smartmenu_data *sd, int width)
{
	r_menu_slot *slot, *sm = sd->r_menu + sd->r_menu_state;

	if (sm->width == width) return;
	if (sm->width) for (slot = sd->r_menu; slot->slot; slot++)
		slot->width = 0;
	sm->width = width;
}

/* Show/hide widgets according to new state */
static void change_to_state(smartmenu_data *sd, int state)
{
	GtkWidget *w;
	r_menu_slot *r_menu = sd->r_menu;
	int i, oldst = sd->r_menu_state;

	if (oldst < state)
	{
		for (i = oldst + 1; i <= state; i++)
			gtk_widget_hide(r_menu[i].slot[0]);
		if (oldst == 0)
		{
			w = r_menu[0].slot[0];
			gtk_widget_set_state(w, GTK_STATE_NORMAL);
			gtk_widget_show(w);
		}
	}
	else
	{
		for (i = oldst; i > state; i--)
		{
			w = r_menu[i].slot[0];
			gtk_widget_set_state(w, GTK_STATE_NORMAL);
			gtk_widget_show(w);
		}
		if (state == 0) gtk_widget_hide(r_menu[0].slot[0]);
	}
	sd->r_menu_state = state;
}

/* Move submenus between menubar and overflow submenu */
static void switch_states(smartmenu_data *sd, int newstate, int oldstate)
{
	r_menu_slot *r_menu = sd->r_menu;
	GtkWidget *submenu;
	GtkMenuItem *item;
	int i;

	if (newstate < oldstate) /* To main menu */
	{
		for (i = oldstate; i > newstate; i--)
		{
			gtk_widget_hide(r_menu[i].fallback);
			item = GTK_MENU_ITEM(r_menu[i].fallback);
			gtk_widget_ref(submenu = item->submenu);
			gtk_menu_item_remove_submenu(item);
			item = GTK_MENU_ITEM(r_menu[i].slot[0]);
			gtk_menu_item_set_submenu(item, submenu);
			gtk_widget_unref(submenu);
		}
	}
	else /* To overflow submenu */
	{
		for (i = oldstate + 1; i <= newstate; i++)
		{
			item = GTK_MENU_ITEM(r_menu[i].slot[0]);
			gtk_widget_ref(submenu = item->submenu);
			gtk_menu_item_remove_submenu(item);
			item = GTK_MENU_ITEM(r_menu[i].fallback);
			gtk_menu_item_set_submenu(item, submenu);
			gtk_widget_unref(submenu);
			gtk_widget_show(r_menu[i].fallback);
		}
	}
}

/* Get width request for default state */
static int smart_menu_full_width(smartmenu_data *sd, GtkWidget *widget, int width)
{
	check_width_cache(sd, width);
	if (!sd->r_menu[0].width)
	{
		GtkRequisition req;
		GtkWidget *child = sd->mbar; /* aka BOX_CHILD_0(widget) */
		int oldst = sd->r_menu_state;
		gpointer lock = toggle_updates(widget, NULL);
		change_to_state(sd, 0);
		gtk_widget_size_request(child, &req);
		sd->r_menu[0].width = req.width;
		change_to_state(sd, oldst);
		child->requisition.width = width;
		toggle_updates(widget, lock);
	}
	return (sd->r_menu[0].width);
}

/* Switch to the state which best fits the allocated width */
static void smart_menu_state_to_width(smartmenu_data *sd, GtkWidget *widget,
	int rwidth, int awidth)
{
	r_menu_slot *slot;
	GtkWidget *child = BOX_CHILD_0(widget);
	gpointer lock = NULL;
	int state, oldst, newst;

	check_width_cache(sd, rwidth);
	state = oldst = sd->r_menu_state;
	while (TRUE)
	{
		newst = rwidth < awidth ? state - 1 : state + 1;
		slot = sd->r_menu + newst;
		if ((newst < 0) || !slot->slot) break;
		if (!slot->width)
		{
			GtkRequisition req;
			if (!lock) lock = toggle_updates(widget, NULL);
			change_to_state(sd, newst);
			gtk_widget_size_request(child, &req);
			slot->width = req.width;
		}
		state = newst;
		if ((rwidth < awidth) ^ (slot->width <= awidth)) break;
	}
	while ((sd->r_menu[state].width > awidth) && sd->r_menu[state + 1].slot)
		state++;
	if (state != sd->r_menu_state)
	{
		if (!lock) lock = toggle_updates(widget, NULL);
		change_to_state(sd, state);
		child->requisition.width = sd->r_menu[state].width;
	}
	if (state != oldst) switch_states(sd, state, oldst);
	if (lock) toggle_updates(widget, lock);
}

static void smart_menu_size_req(GtkWidget *widget, GtkRequisition *req,
	gpointer user_data)
{
	smartmenu_data *sd = user_data;
	GtkRequisition child_req;
	GtkWidget *child;
	int fullw;

	req->width = req->height = GTK_CONTAINER(widget)->border_width * 2;
	if (!GTK_BOX(widget)->children) return;
	child = BOX_CHILD_0(widget);
	if (!GTK_WIDGET_VISIBLE(child)) return;

	gtk_widget_size_request(child, &child_req);
	fullw = smart_menu_full_width(sd, widget, child_req.width);

	req->width += fullw;
	req->height += child_req.height;
}

static void smart_menu_size_alloc(GtkWidget *widget, GtkAllocation *alloc,
	gpointer user_data)
{
	smartmenu_data *sd = user_data;
	GtkRequisition child_req;
	GtkAllocation child_alloc;
	GtkWidget *child;
	int border = GTK_CONTAINER(widget)->border_width, border2 = border * 2;

	widget->allocation = *alloc;
	if (!GTK_BOX(widget)->children) return;
	child = BOX_CHILD_0(widget);
	if (!GTK_WIDGET_VISIBLE(child)) return;

	/* Maybe recursive calls to this cannot happen, but if they can,
	 * crash would be quite spectacular - so, better safe than sorry */
	if (sd->in_alloc) /* Postpone reaction */
	{
		sd->in_alloc |= 2;
		return;
	}

	/* !!! Always keep child widget requisition set according to its
	 * !!! mode, or this code will break down in interesting ways */
	gtk_widget_get_child_requisition(child, &child_req);
/* !!! Alternative approach - reliable but slow */
//	gtk_widget_size_request(child, &child_req);
	while (TRUE)
	{
		sd->in_alloc = 1;
		child_alloc.x = alloc->x + border;
		child_alloc.y = alloc->y + border;
		child_alloc.width = alloc->width > border2 ?
			alloc->width - border2 : 0;
		child_alloc.height = alloc->height > border2 ?
			alloc->height - border2 : 0;
		if ((child_alloc.width != child->allocation.width) &&
			(sd->r_menu_state > 0 ?
			child_alloc.width != child_req.width :
			child_alloc.width < child_req.width))
			smart_menu_state_to_width(sd, widget, child_req.width,
				child_alloc.width);
		if (sd->in_alloc < 2) break;
		alloc = &widget->allocation;
	}
	sd->in_alloc = 0;

	gtk_widget_size_allocate(child, &child_alloc);
}

#endif

/* Fill smart menu structure */
// !!! With inlining this, problem also
void *smartmenu_done(void **tbar, void **r)
{
#if GTK_MAJOR_VERSION == 3
	GtkWidget *tl = gtk_label_new("");
	char c, *ts, *src, *dest;
#endif
	smartmenu_data *sd = tbar[2];
	GtkWidget *parent, *item;
	void **rr;
	char *s;
	int i, l, n = 0;

	/* Find items */
	for (rr = tbar; rr - r < 0; rr = NEXT_SLOT(rr))
	{
		if (GET_OP(rr) != op_SSUBMENU) continue;
		sd->r_menu[n++].slot = rr;
	}

	/* Setup overflow submenu */
	parent = gtk_menu_item_get_submenu(GTK_MENU_ITEM(sd->r_menu[--n].slot[0]));
	for (i = 0; i < n; i++)
	{
		sd->r_menu[i].fallback = item = gtk_menu_item_new_with_label("");
		gtk_container_add(GTK_CONTAINER(parent), item);
		rr = sd->r_menu[i].slot[1];

		l = strspn(s = rr[1], "/");
		if (s[l]) s = _(s); // Translate
		s += l;
#if GTK_MAJOR_VERSION == 3
		/* Due to crippled API, cannot set & display a mnemonic without it
		 * attaching in the regular way; need to strip them from items */
		gtk_label_set_text_with_mnemonic(GTK_LABEL(tl), s);
		sd->r_menu[i].key = gtk_label_get_mnemonic_keyval(GTK_LABEL(tl));
		ts = strdup(s);
		src = dest = ts;
		while (TRUE)
		{
			c = *src++;
			if (c != '_') *dest++ = c;
			if (!c) break;
		}
		gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(item))), ts);
		free(ts);
#else /* #if GTK_MAJOR_VERSION <= 2 */
		sd->r_menu[i].key = gtk_label_parse_uline(
			GTK_LABEL(GTK_BIN(item)->child), s);
#endif
	}
	for (i = 0; i <= n / 2; i++) // Swap ends
	{
		r_menu_slot tmp = sd->r_menu[i];
		sd->r_menu[i] = sd->r_menu[n - i];
		sd->r_menu[n - i] = tmp;
	}
	gtk_widget_hide(sd->r_menu[0].slot[0]);

#if GTK_MAJOR_VERSION == 3
	g_object_ref_sink(tl);
	g_object_unref(tl);
#endif
	return (sd);
}

#if GTK_MAJOR_VERSION <= 2

/* Heightbar: increase own height to max height of invisible neighbors */

typedef struct {
	GtkWidget *self;
	GtkRequisition *req;
} heightbar_data;

static void heightbar_req(GtkWidget *widget, gpointer data)
{
	heightbar_data *hd = data;
	GtkRequisition req;

	if (widget == hd->self) return; // Avoid recursion
	if (GTK_WIDGET_VISIBLE(widget)) return; // Let GTK+ handle it, for now
	gtk_widget_size_request(widget, &req); // Ask what invisible ones want
	if (req.height > hd->req->height) hd->req->height = req.height;
}

static void heightbar_size_req(GtkWidget *widget, GtkRequisition *req,
	gpointer user_data)
{
	heightbar_data hd = { widget, req };
	if (widget->parent) gtk_container_foreach(GTK_CONTAINER(widget->parent),
		(GtkCallback)heightbar_req, &hd);
}

#endif

/* Get/set window position & size from/to inifile */
void rw_pos(v_dd *vdata, int set)
{
	char name[128];
	int i, l = strlen(vdata->ininame);

	memcpy(name, vdata->ininame, l);
	name[l++] = '_'; name[l + 1] = '\0';
	for (i = 0; i < 5; i++)
	{
		name[l] = "xywhm"[i];
		if (set) inifile_set_gint32(name, vdata->xywh[i]);
		else if (vdata->xywh[i] || (i < 2) || (i > 3)) // 0 means auto-size
			vdata->xywh[i] = inifile_get_gint32(name, vdata->xywh[i]);
	}
}

static GtkWidget *get_wrap(void **slot)
{
	GtkWidget *w = slot[0];
	int op = GET_OP(slot);
	if ((op == op_SPINSLIDE) || (op == op_SPINSLIDEa) ||
//		(op == op_CANVASIMG) || (op == op_CANVASIMGB) || // Leave frame be
		(op == op_PATHs) || (op == op_PATH) || (op == op_TEXT))
		w = gtk_widget_get_parent(w);
	return (w);
}

/* More specialized packing modes */

enum {
	pk_PACKEND1 = pk_LAST,
	pk_TABLE0p,
	pk_TABLEp,
	pk_SCROLLVP,
	pk_SCROLLVPv,
	pk_SCROLLVPm,
	pk_SCROLLVPn,
	pk_CONT,
	pk_BIN,
	pk_SHOW,	/* No packing needed, just show the widget */
	pk_UNREAL,	/* Pseudo widget - no packing, just finish creation */
	pk_UNREALV,	/* Pseudo widget with int value */
};

#define pk_MASK     0xFF
#define pkf_PARENT 0x100
#define pkf_CANVAS 0x200

/* Make code not compile where it cannot run */
typedef char Too_Many_Packing_Modes[2 * (pk_LAST <= WB_PKMASK + 1) - 1];

/* Packing modifiers */

typedef struct {
	int cw;
	int minw, minh;
	int maxw, maxh;
	int wantmax;
} pkmods;

/* Prepare widget for packing according to settings */
GtkWidget *do_prepare(GtkWidget *widget, int pk, pkmods *mods)
{
	/* Show this */
	if (pk) gtk_widget_show(widget);
	/* Unwrap this */
	if (pk & pkf_PARENT)
		while (gtk_widget_get_parent(widget))
			widget = gtk_widget_get_parent(widget);
	/* Border this */
	if (mods->cw) gtk_container_set_border_width(GTK_CONTAINER(widget), mods->cw);
#if GTK_MAJOR_VERSION == 3
	/* Set fixed width/height for this */
/* !!! The call below does MINIMUM size; later, separate out the cases where UPPER
 * limit is desired, make a wrapper for that, and use maxw/maxh for them */
	if ((mods->minw > 0) || (mods->minh > 0))
		gtk_widget_set_size_request(widget,
			mods->minw > 0 ? mods->minw : -1,
			mods->minh > 0 ? mods->minh : -1);
	/* And/or min ones; this, GTK+3 can do naturally */
	if ((mods->minw < 0) || (mods->minh < 0))
		gtk_widget_set_size_request(widget,
			mods->minw < 0 ? -mods->minw : -1,
			mods->minh < 0 ? -mods->minh : -1);
	/* Make this scrolled window request max size */
	if (mods->wantmax)
	{
#if GTK3VERSION >= 22
		/* Use the new functions */
		if (!((mods->wantmax - 1) & 1))
			gtk_scrolled_window_set_propagate_natural_width(
				GTK_SCROLLED_WINDOW(widget), TRUE);
		if (!((mods->wantmax - 1) & 2))
			gtk_scrolled_window_set_propagate_natural_height(
				GTK_SCROLLED_WINDOW(widget), TRUE);
#else
		/* Add a wrapper doing it */
		GtkWidget *wrap = wjsizebin_new(G_CALLBACK(do_wantmax), NULL,
			(gpointer)(mods->wantmax - 1));
		gtk_widget_show(wrap);
		gtk_container_add(GTK_CONTAINER(wrap), widget);
		widget = wrap;
#endif
	}
#else /* #if GTK_MAJOR_VERSION <= 2 */
	/* Set fixed width/height for this */
	if ((mods->minw > 0) || (mods->minh > 0))
		gtk_widget_set_usize(widget,
			mods->minw > 0 ? mods->minw : -2,
			mods->minh > 0 ? mods->minh : -2);
	/* And/or min ones */
// !!! For now, always use wrapper
	if ((mods->minw < 0) || (mods->minh < 0))
		widget = widget_align_minsize(widget,
			mods->minw < 0 ? -mods->minw : -2,
			mods->minh < 0 ? -mods->minh : -2);
	/* Make this scrolled window request max size */
	if (mods->wantmax) gtk_signal_connect(GTK_OBJECT(widget), "size_request",
		GTK_SIGNAL_FUNC(scroll_max_size_req), (gpointer)(mods->wantmax - 1));
#endif

	return (widget);
}

/* Container stack */

typedef struct {
	void *widget;
	int group; // in inifile
	int type; // how to stuff things into it
} ctslot;

/* Types of containers */

enum {
	ct_NONE = 0,
	ct_BOX,
	ct_TABLE,
	ct_CONT, /* Generic container, like menubar and menu */
	ct_BIN,
	ct_SCROLL,
	ct_TBAR,
	ct_NBOOK,
	ct_HVSPLIT,
	ct_SGROUP,
	ct_SMARTMENU,
};

/* !!! Limited to rows & columns 0-255 per wh composition l16:col8:row8 */
static void table_it(ctslot *ct, GtkWidget *it, int wh, int pad, int pack)
{
	int row = wh & 255, column = (wh >> 8) & 255, l = (wh >> 16) + 1;
	int r0 = (ct->type >> 8) & 255, r1 = ct->type >> 16;

	/* Track height of columns 0 & 1 in bytes 1 & 2 of type field */
	if (!column && (r0 <= row)) r0 = row + 1;
	if ((column <= 1) && (column + l > 1) && (r1 <= row)) r1 = row + 1;
	ct->type = (r1 << 16) + (r0 << 8) + (ct->type & 255);

	gtk_table_attach(GTK_TABLE(ct->widget), it, column, column + l, row, row + 1,
		pack == pk_TABLEx ? GTK_EXPAND | GTK_FILL : GTK_FILL, 0,
		pack == pk_TABLEp ? pad : 0, pad);
}

/* Pack widget into container according to settings */
int do_pack(GtkWidget *widget, ctslot *ct, void **pp, int n, int tpad)
{
	GtkScrolledWindow *sw;
	GtkAdjustment *adj = adj;
	GtkWidget *box = ct->widget;
	int what = ct->type & 255;
	int l = WB_GETLEN((int)pp[0]);

#if GTK_MAJOR_VERSION == 3
	/* Apply size group */
	if (what == ct_SGROUP)
	{
		gtk_size_group_add_widget((GtkSizeGroup *)box, widget);
		/* Real container is above it */
		ct++;
		box = ct->widget;
		what = ct->type & 255;
	}
#endif

	/* Remember what & when goes into HVSPLIT */
	if (what == ct_HVSPLIT)
	{
		hvsplit_data *hd = (void *)ct->widget;
		box = hd->box;
		if (hd->cnt < HVSPLIT_MAX) hd->inbox[hd->cnt++] = widget;
	}

#if GTK_MAJOR_VERSION == 3
	/* Protect canvas widgets from external painting-over, with CSS nodes
	 * or without */
	if ((n & pkf_CANVAS) && GTK_IS_SCROLLED_WINDOW(box))
		css_restyle(box, (gtk3version >= 20 ?
			".mtPaint_cscroll overshoot,.mtPaint_cscroll undershoot"
			" { background:none; }" :
			".mtPaint_cscroll .overshoot,.mtPaint_cscroll .undershoot"
			" { background:none; }"),
			"mtPaint_cscroll", NULL);
#endif

	n &= pk_MASK; // Strip flags

	/* Adapt packing mode to container type */
	if (n <= pk_DEF) switch (what)
	{
	case ct_SMARTMENU:
	case ct_CONT: n = pk_CONT; break;
	case ct_BIN: n = pk_BIN; break;
	case ct_SCROLL: n = pk_SCROLLVP; break;
	}

	switch (n)
	{
	case pk_PACK:
		gtk_box_pack_start(GTK_BOX(box), widget, FALSE, FALSE, tpad);
		break;
	case pk_XPACK: case pk_EPACK:
		gtk_box_pack_start(GTK_BOX(box), widget, TRUE, n != pk_EPACK, tpad);
		break;
	case pk_PACKEND: case pk_PACKEND1:
		gtk_box_pack_end(GTK_BOX(box), widget, FALSE, FALSE, tpad);
		break;
	case pk_TABLE: case pk_TABLEx: case pk_TABLEp:
		table_it(ct, widget, (int)pp[l], tpad, n);
		break;
	case pk_TABLE0p:
		table_it(ct, widget, (ct->type >> 8) & 255, tpad, pk_TABLEp);
		break;
	case pk_TABLE1x:
		table_it(ct, widget, 0x100 + (ct->type >> 16), tpad, pk_TABLEx);
		break;
	case pk_SCROLLVP: case pk_SCROLLVPv: case pk_SCROLLVPm: case pk_SCROLLVPn:
		sw = GTK_SCROLLED_WINDOW(box);

		gtk_scrolled_window_add_with_viewport(sw, widget);
#ifdef U_LISTS_GTK1
		adj = gtk_scrolled_window_get_vadjustment(sw);
		if ((n == pk_SCROLLVPv) || (n == pk_SCROLLVPm))
			gtk_container_set_focus_vadjustment(GTK_CONTAINER(widget), adj);
		if (n == pk_SCROLLVPm)
		{
			gtk_signal_connect(GTK_OBJECT(widget), "map",
				GTK_SIGNAL_FUNC(list_scroll_in), adj);
		}
#endif
		if (n == pk_SCROLLVPn)
		{
			/* Set viewport to shadowless */
			box = gtk_bin_get_child(GTK_BIN(box));
			gtk_viewport_set_shadow_type(GTK_VIEWPORT(box), GTK_SHADOW_NONE);
			vport_noshadow_fix(box);
		}
		return (1); // unstack
	case pk_CONT: case pk_BIN:
		gtk_container_add(GTK_CONTAINER(box), widget);
		if (n == pk_BIN) return (1); // unstack
		break;
	}
	if (n == pk_PACKEND1) gtk_box_reorder_child(GTK_BOX(box), widget, 1);
	return (0);
}

/* Remap opcodes for script mode */
static int in_script(int op, char **script)
{
	int r = WB_GETREF(op);
	op &= WB_OPMASK;
	/* No script - leave as is */
	if (!script);
	/* Remap backend-dependent opcodes */
	else if ((op < op_CTL_0) || (op >= op_CTL_LAST))
	{
		int uop = cmds[op] ? cmds[op]->uop : 0;
		op = uop > 0 ? uop : uop < 0 ? op : r ? op_uOP : op_TRIGGER;
	}
	/* No need to connect event handlers in script mode - except DESTROY */
	else if ((op != op_EVT_DESTROY) &&
		(op >= op_EVT_0) && (op <= op_EVT_LAST)) op = op_TRIGGER;
	return (op);
}

typedef struct {
	int slots;	// total slots
	int top;	// total on-top allocation, in *void's
	int keys;	// keymap slots
	int act;	// ACTMAP slots
} sizedata;

/* Predict how many _void pointers_ a V-code sequence could need */
// !!! And with inlining this, same problem
void predict_size(sizedata *sz, void **ifcode, char *ddata, char **script)
{
	sizedata s;
	void **v, **pp, *rstack[CALL_DEPTH], **rp = rstack;
	int scripted = FALSE;
	int op, opf, ref, uop;

	memset(&s, 0, sizeof(s));
	while (TRUE)
	{
		opf = op = (int)*ifcode++;
		ifcode = (pp = ifcode) + WB_GETLEN(op);
		s.slots += ref = WB_GETREF(op);
		op = in_script(op, script);
		if (op < op_END_LAST) break; // End
		// Livescript start/stop
		if (!script && (op == op_uOPNAME) && (opf & WB_SFLAG))
			scripted = !ref;
		// Subroutine return
		if (op == op_RET) ifcode = *--rp;
		// Jump or subroutine call
		else if ((op == op_GOTO) || (op == op_CALL))
		{
			if (op == op_CALL) *rp++ = ifcode;
			v = *pp;
			if (opf & WB_FFLAG)
				v = (void *)(ddata + (int)v);
			if (opf & WB_NFLAG) v = *v; // dereference
			ifcode = v;
		}
		// Keymap
		else if (op == op_KEYMAP) s.keys = 1;
		// Allocation
		else if (op == op_ACTMAP) s.act++;
		else 
		{
			s.top += VVS((op == op_TALLOC) || (op == op_TCOPY) ?
				*(int *)(ddata + (int)pp[1]) :
				op == op_TLSPINPACK ? TLSPINPACK_SIZE(pp - 1) :
				cmds[op] ? cmds[op]->size : 0);
			/* Nothing else happens to unreferrable things, neither
			 * to those that simulation ignores */
			if (!ref || !cmds[op]) continue;
			uop = cmds[op]->uop;
			// Name for scripting
			if (scripted && (uop > 0))
			{
				s.slots++;
				s.top += VVS(sizeof(swdata));
			}
			// Slot in keymap
			if (s.keys &&
				(((op >= op_uMENU_0) && (op < op_uMENU_LAST)) ||
				((uop >= op_uMENU_0) && (uop < op_uMENU_LAST))))
				s.keys++;
		}
	}

	if (s.keys) s.top += VVS(KEYMAP_SIZE(s.keys));
	s.top += s.act * ACT_SIZE;
	s.slots += 2; // safety margin
	*sz = s;
}

static cmdef cmddefs[] = {
	{ op_RGBIMAGE,	sizeof(rgbimage_data) },
	{ op_RGBIMAGEP,	sizeof(rgbimage_data) },
	{ op_CANVASIMG,	sizeof(rgbimage_data) },
	{ op_CANVASIMGB, sizeof(rgbimage_data) },
	{ op_FCIMAGEP,	sizeof(fcimage_data) },
	{ op_KEYBUTTON,	sizeof(keybutton_data) },
	{ op_FONTSEL,	sizeof(fontsel_data), op_uENTRY },
// !!! Beware - COLORLIST is self-reading and uOPTD is not
	{ op_COLORLIST,	sizeof(colorlist_data), op_uOPTD },
	{ op_COLORLISTN, sizeof(colorlist_data), op_uLISTCC },
	{ op_GRADBAR,	sizeof(gradbar_data) },
	{ op_LISTCCr,	sizeof(listcc_data), op_uLISTCC },
	{ op_LISTC,	sizeof(listc_data), op_uLISTC },
	{ op_LISTCd,	sizeof(listc_data), op_uLISTC },
	{ op_LISTCu,	sizeof(listc_data) },
	{ op_LISTCS,	sizeof(listc_data), op_uLISTC },
	{ op_LISTCX,	sizeof(listc_data) },
	{ op_DOCK,	sizeof(dock_data) },
	{ op_HVSPLIT,	sizeof(hvsplit_data) },
#if GTK_MAJOR_VERSION <= 2
	{ op_SMARTTBAR,	sizeof(smarttbar_data), op_uMENUBAR },
#endif
	{ op_SMARTMENU,	sizeof(smartmenu_data), op_uMENUBAR },
	{ op_DRAGDROP,	sizeof(drag_ctx) },
	/* In this, data slot points to dependent widget's wdata */
	{ op_MOUNT,	0, op_uMOUNT },
	/* In these, data slot points to menu/toolbar slot */
	{ op_TBBUTTON,	0, op_uMENUITEM },
	{ op_TBTOGGLE,	0, op_uMENUCHECK },
	{ op_TBRBUTTON,	0, op_uMENURITEM },
	{ op_TBBOXTOG,	0, op_uMENUCHECK },
	{ op_MENUITEM,	0, op_uMENUITEM },
	{ op_MENUCHECK,	0, op_uMENUCHECK },
	{ op_MENURITEM,	0, op_uMENURITEM },

	{ op_WEND,	0, op_uWEND },
	{ op_WSHOW,	0, op_uWSHOW },
	{ op_MAINWINDOW, 0, op_uWINDOW },
	{ op_WINDOW,	0, op_uWINDOW },
	{ op_WINDOWm,	0, op_uWINDOW },
	{ op_FPICKpm,	0, op_uFPICK },
	{ op_TOPVBOX,	0, op_uTOPBOX },
	{ op_TOPVBOXV,	0, op_uTOPBOX },
	{ op_PAGE,	0, op_uFRAME },
	{ op_FRAME,	0, op_uFRAME },
	{ op_LABEL,	0, op_uLABEL },
	{ op_SPIN,	0, op_uSPIN },
	{ op_SPINc,	0, op_uSPIN },
	{ op_FSPIN,	0, op_uFSPIN },
	{ op_SPINa,	0, op_uSPINa },
	{ op_SPINSLIDE,	0, op_uSPIN },
	{ op_SPINSLIDEa, 0, op_uSPINa },
	{ op_CHECK,	0, op_uCHECK },
	{ op_CHECKb,	0, op_uCHECKb },
	{ op_RPACK,	0, op_uRPACK },
	{ op_RPACKD,	0, op_uRPACKD },
	{ op_OPT,	0, op_uOPT },
	{ op_OPTD,	0, op_uOPTD },
	{ op_COMBO,	0, op_uOPT },
	{ op_ENTRY,	0, op_uENTRY },
	{ op_MLENTRY,	0, op_uENTRY },
	{ op_COLOR,	0, op_uCOLOR },
//	and various others between op_OPTD and op_OKBTN
	{ op_OKBTN,	0, op_uOKBTN },
	{ op_BUTTON,	0, op_uBUTTON },
	{ op_TOOLBAR,	0, op_uMENUBAR },
	{ op_ACTMAP,	0, -1 },
	{ op_INSENS,	0, -1 },

	{ op_uWINDOW,	sizeof(swdata), -1 },
	{ op_uFPICK,	sizeof(swdata), -1 },
	{ op_uTOPBOX,	sizeof(swdata), -1 },
	{ op_uOP,	sizeof(swdata), -1 },
	{ op_uFRAME,	sizeof(swdata), -1 },
	{ op_uLABEL,	sizeof(swdata), -1 },
	{ op_uCHECK,	sizeof(swdata), -1 },
	{ op_uCHECKb,	sizeof(swdata), -1 },
	{ op_uSPIN,	sizeof(swdata), -1 },
	{ op_uFSPIN,	sizeof(swdata), -1 },
	{ op_uSPINa,	sizeof(swdata), -1 },
	{ op_uSCALE,	sizeof(swdata), -1 },
	{ op_uOPT,	sizeof(swdata), -1 },
	{ op_uOPTD,	sizeof(swdata), -1 },
	{ op_uRPACK,	sizeof(swdata), -1 },
	{ op_uRPACKD,	sizeof(swdata), -1 },
	{ op_uENTRY,	sizeof(swdata), -1 },
	{ op_uPATHSTR,	sizeof(swdata), -1 },
	{ op_uCOLOR,	sizeof(swdata), -1 },
	{ op_uLISTCC,	sizeof(swdata), -1 },
	{ op_uLISTC,	sizeof(lswdata), -1 },
	{ op_uOKBTN,	sizeof(swdata), -1 },
	{ op_uBUTTON,	sizeof(swdata), -1 },
	{ op_uMENUBAR,	sizeof(swdata), -1 },
	{ op_uMENUITEM,	sizeof(swdata), -1 },
	{ op_uMENUCHECK, sizeof(swdata), -1 },
	{ op_uMENURITEM, sizeof(swdata), -1 },
	{ op_uMOUNT,	sizeof(swdata), -1 },
	{ op_IDXCOLUMN,	sizeof(swdata), -1 },
	{ op_TXTCOLUMN,	sizeof(swdata), -1 },
	{ op_XTXTCOLUMN, sizeof(swdata), -1 },
	{ op_FILECOLUMN, sizeof(swdata), -1 },
	{ op_CHKCOLUMN,	sizeof(swdata), -1 },
	{ op_uALTNAME,	sizeof(swdata), -1 },
};

static void do_destroy(void **wdata);

/* V-code is really simple-minded; it can do 0-tests but no arithmetics, and
 * naturally, can inline only constants. Everything else must be prepared either
 * in global variables, or in fields of "ddata" structure.
 * Parameters of codes should be arrayed in fixed order:
 * result location first; table location last; builtin event(s) before that */

#define DEF_BORDER 5
#define GET_BORDER(T) (borders[op_BOR_##T - op_BOR_0] + DEF_BORDER)

/* Create a new slot */
#define PREP_SLOT(R,W,D,T) (R)[0] = (W) , (R)[1] = (D) , (R)[2] = (T)
#define ADD_SLOT(R,W,D,T) PREP_SLOT(R, W, D, T) , (R) += VSLOT_SIZE
/* Finalize a prepared slot */
#define FIX_SLOT(R,W) (R)[0] = (W) , (R) += VSLOT_SIZE

/* Accessors for container stack */

#define CT_PUSH(SP,W,T)	((SP)-- , (SP)->widget = (W) , (SP)->type = (T) , \
	(SP)->group = keygroup)
#define CT_POP(SP)	((SP)++)
#define CT_DROP(SP)	(keygroup = ((SP)++)->group)
#define CT_TOP(SP)	((SP)->widget)
#define CT_N(SP,N)	((SP)[(N)].widget)
#define CT_WHAT(SP)	((SP)->type & 255)

void **run_create_(void **ifcode, void *ddata, int ddsize, char **script)
{
	char *ident = VCODE_KEY;
	/* Avoid complex typecast - not initing GType in cmdline mode */
	GtkWindow *tparent = (GtkWindow *)main_window;
#if GTK_MAJOR_VERSION == 1
	int have_sliders = FALSE;
#endif
	int scripted = FALSE, part = FALSE, accel = 0;
	int borders[op_BOR_LAST - op_BOR_0], wpos = GTK_WIN_POS_CENTER;
	ctslot wstack[CONT_DEPTH], *wp = wstack + CONT_DEPTH;
	int keygroup = 0;
	keymap_data *keymap = NULL;
	GtkWidget *window = NULL, *widget = NULL;
	GtkAccelGroup* ag = NULL;
	v_dd *vdata;
	sizedata sz;
	col_data c;
	pkmods mods;
	void *rstack[CALL_DEPTH], **rp = rstack;
	void *v, **pp, **dtail, **r = NULL, **res = NULL, *sw = NULL;
	void **tbar = NULL, **rslot = NULL, *rvar = NULL;
	char *wid = NULL, *gid = NULL;
	int ld, dsize;
	int i, n, op, lp, ref, pk, cw, tpad, ct = 0;

        // Per-command allocations
        memset(cmds, 0, sizeof(cmds));
        for (i = 0; i < sizeof(cmddefs) / sizeof(cmddefs[0]); i++)
                cmds[cmddefs[i].op] = cmddefs + i;

	// Allocation size
	predict_size(&sz, ifcode, ddata, script);
	ld = VVS(ddsize);
	n = VVS(sizeof(v_dd));
	dsize = ld + n + ++sz.slots * VSLOT_SIZE + sz.top;
	if (!(res = calloc(dsize, sizeof(void *)))) return (NULL); // failed
	dtail = res + dsize; // Locate tail of block
	memcpy(res, ddata, ddsize); // Copy datastruct
	ddata = res; // And switch to using it
	vdata = (void *)(res += ld); // Locate where internal data go
	r = res += n; // Anchor after it
	vdata->code = WDONE; // Make internal datastruct a noop
	// Allocate actmap
	vdata->actmap = dtail -= sz.act * ACT_SIZE;
	// Store struct ref at anchor, use datastruct as tag for it
	ADD_SLOT(r, ddata, vdata, dtail);

	// Border sizes are DEF_BORDER-based
	memset(borders, 0, sizeof(borders));

	// Column data
	memset(&c, 0, sizeof(c));

	// Packing modifiers
	memset(&mods, 0, sizeof(mods));

	if (!script) ag = gtk_accel_group_new();

	while (TRUE)
	{
		op = (int)*ifcode;
		ifcode = (pp = ifcode) + 1 + (lp = WB_GETLEN(op));
		pk = WB_GETPK(op);
		/* Table loc is outside the token proper */
		{
			int p = pk & pk_MASK; // !!! To prevent GCC misoptimizing
			lp -= (p >= pk_TABLE) && (p <= pk_TABLEx);
		}
		v = lp > 0 ? pp[1] : NULL;
		if (op & WB_FFLAG) v = (void *)((char *)ddata + (int)v);
		if (op & WB_NFLAG) v = *(char **)v; // dereference a string
		ref = WB_GETREF(op);
		op = in_script(op, script);
		if (cmds[op]) dtail -= VVS(cmds[op]->size);
		/* Prepare slot, with data pointer in widget's place */
		PREP_SLOT(r, v, pp, dtail);
		tpad = cw = 0;
		gid = NULL;
		switch (op)
		{
		/* Terminate */
		case op_WEND: case op_WSHOW: case op_WDIALOG:
			/* Terminate the list */
			ADD_SLOT(r, NULL, NULL, NULL);

			/* Apply keymap */
			if (keymap)
			{
				keymap->ag = ag;
				keymap_init(keymap, NULL);
				accel |= 1;
			}

			/* !!! In GTK+1, doing it earlier makes any following
			 * gtk_widget_add_accelerator() calls to silently fail */
			if (accel > 1) gtk_accel_group_lock(ag);

			/* Add accel group, or drop it if unused */
			if (!accel) gtk_accel_group_unref(ag);
			else gtk_window_add_accel_group(GTK_WINDOW(window), ag);

			gtk_object_set_data(GTK_OBJECT(window), ident,
				(gpointer)res);
			gtk_signal_connect_object(GTK_OBJECT(window), "destroy",
				GTK_SIGNAL_FUNC(do_destroy), (gpointer)res);
			/* !!! Freeing the datastruct is best to happen only when
			 * all refs to underlying object are safely dropped */
			gtk_object_weakref(GTK_OBJECT(window),
				(GtkDestroyNotify)free, (gpointer)ddata);
#if GTK_MAJOR_VERSION == 1
			/* To make Smooth theme engine render sliders properly */
			if (have_sliders) gtk_signal_connect(
				GTK_OBJECT(window), "show",
				GTK_SIGNAL_FUNC(gtk_widget_queue_resize), NULL);
#endif
			/* Init actmap to insensitive */
			act_state(vdata, 0);
			/* Add finishing touches to a toplevel */
			if (!part)
			{
				vdata->tparent = tparent;
				/* Trigger remembered events */
				trigger_things(res);
			}
			/* Display */
			if (op != op_WEND) cmd_showhide(GET_WINDOW(res), TRUE);
			/* Dialogs must be immune to stuck pointer grabs */
			if (op == op_WDIALOG)
			{
				// Paranoia
				GtkWidget *grab = gtk_grab_get_current();
				if (grab && (grab != GET_REAL_WINDOW(res)))
					gtk_grab_add(GET_REAL_WINDOW(res));
				// Real concern - a server grab
				release_grab();
			}
			/* Wait for input */
			if (op == op_WDIALOG)
			{
				*(void ***)v = NULL; // clear result slot
				vdata->dv = v; // announce it
				while (!*(void ***)v) gtk_main_iteration();
			}
			/* Return anchor position */
			return (res);
		/* Terminate in script mode */
		case op_uWEND: case op_uWSHOW:
			/* Terminate the list */
			ADD_SLOT(r, NULL, NULL, NULL);
			/* Trigger remembered events */
			if (!part) trigger_things(res);
			/* Init actmap to insensitive */
			act_state(vdata, 0);
			/* Activate */
			if (op != op_uWEND)
				cmd_showhide(GET_WINDOW(res), TRUE);
			/* Return anchor position - maybe already freed */
			return (res);
		/* Script mode fileselector */
		case op_uFPICK:
			((swdata *)dtail)->strs = resolve_path(NULL, PATHBUF, v);
			// Fallthrough
		/* Script mode pseudo window */
		case op_uWINDOW: case op_uTOPBOX:
			part = op == op_uTOPBOX; // not toplevel
			/* Store script ref, to run it when done */
			vdata->script = script;
			wid = ""; // First unnamed field gets to be default
			pk = pk_UNREAL;
			if (op == op_uFPICK) pk = pk_UNREALV;
			break;
		/* Script mode alternate identifier */
		case op_uALTNAME:
			if (!script && !scripted) continue;
			wid = v;
			pk = pk_UNREALV;
			break;
		/* Script mode identifier (forced) */
		case op_uOPNAME:
			/* If flagged as livescript start/stop marker */
			if ((int)pp[0] & WB_SFLAG)
			{
				widget = NULL;
				scripted = FALSE;
				if (ref) break; // stop if ref
				wid = ""; // default
				scripted = !script;
				continue; // start if no ref
			}
			/* If inside script and having something to set */
			if ((script || scripted) && v)
			{
				void **slot = prev_uslot(r);
				if (slot) ((swdata *)slot[0])->id = v;
			}
			wid = NULL; // Clear current identifier
			continue;
		/* Script mode frame, as identifier to next control */
		case op_uFRAME:
			wid = v;
			// Fallthrough
		/* Script mode OK button, as placeholder */
		case op_uOKBTN:
		/* Script mode generic placeholder / group marker */
		case op_uOP:
			pk = pk_UNREAL;
			/* If a group ID and inside script */
			if ((((int)pp[0] & WB_OPMASK) == op_uOP) && lp &&
				(script || scripted))
			{
				void **slot;
				if (v) wid = v;
				if (!wid && (slot = prev_uslot(r)))
					wid = ((swdata *)slot[0])->id;
				pk = pk_UNREALV;
			}
			break;
		/* Script mode button */
		case op_uBUTTON:
			wid = v;
			pk = pk_UNREALV;
			// Not scriptable by default
			if ((int)pp[0] & WB_SFLAG) break;
			op = op_uOP;
			pk = pk_UNREAL; // Leave identifier for next widget
			break;
		/* Script mode checkbox */
		case op_uCHECKb:
			*(int *)v = inifile_get_gboolean(pp[3], *(int *)v);
			// Fallthrough
		case op_uCHECK:
			wid = pp[2];
			tpad = !!*(int *)v;
			pk = pk_UNREALV;
			break;
		/* Script mode spinbutton */
		case op_uSPIN: case op_uFSPIN: case op_uSPINa: case op_uSCALE:
		{
			int a, b;
			if (op != op_uSPINa) a = (int)pp[2] , b = (int)pp[3];
			else a = ((int *)v)[1] , b = ((int *)v)[2];
			((swdata *)dtail)->range[0] = a;
			((swdata *)dtail)->range[1] = b;
			tpad = *(int *)v;
			tpad = tpad > b ? b : tpad < a ? a : tpad;
			if (op == op_uSCALE) // Backup original value
				((swdata *)dtail)->strs = (void *)tpad;
			pk = pk_UNREALV;
			break;
		}
		/* Script mode option pack */
		case op_uOPT: case op_uOPTD: case op_uRPACK: case op_uRPACKD:
		{
			char **strs = pp[2];
			int n = (int)pp[3];

			if (op == op_uRPACK) n >>= 8;
			else if (op == op_uOPT);
			else /* OPTD/RPACKD */
			{
				strs = *(char ***)((char *)ddata + (int)strs);
				n = 0;
			}
			if (n <= 0) for (n = 0; strs[n]; n++); // Count strings
			((swdata *)dtail)->cnt = n;
			((swdata *)dtail)->strs = strs;
			tpad = *(int *)v;
			if ((tpad >= n) || (tpad < 0) || !strs[tpad][0]) tpad = 0;
			pk = pk_UNREALV;
			break;
		}
		/* Script mode entry - fill from drop-away buffer */
		case op_uENTRY: case op_uPATHSTR:
			// Length limit (not including 0)
			tpad = ((swdata *)dtail)->value = lp > 1 ? (int)pp[2] : -1;
			// Replace transient buffer - it may get freed on return
			*(char **)v = set_uentry((swdata *)dtail, *(char **)v);
			pk = pk_UNREALV;
			break;
		/* Script mode color picker - leave unfilled (?) */
		case op_uCOLOR:
			pk = pk_UNREALV;
			break;
		/* Script mode list with columns */
		case op_uLISTC:
			set_columns(&((lswdata *)dtail)->c, &c, ddata, r);
			((swdata *)dtail)->strs = v; // Pointer to index
			tpad = ulistc_reset(r);
			pk = pk_UNREALV;
			break;
		/* Script mode list */
		case op_uLISTCC:
			((swdata *)dtail)->strs = v; // Pointer to index
			tpad = *(int *)v;
			pk = pk_UNREALV;
			break;
		/* Script mode menubar */
		case op_uMENUBAR:
			tbar = r;
			rvar = rslot = NULL;
			pk = pk_UNREAL;
			break;
		/* Script mode menu item/toggle */
		case op_uMENURITEM:
			/* Chain to previous */
			if (rvar == v) ((swdata *)rslot[0])->range[1] =
				((swdata *)dtail)->range[0] = r - rslot;
			/* Now this represents group */
			rslot = r; rvar = v;
			// Fallthrough
		case op_uMENUCHECK:
			tpad = *(int *)v;
			tpad = op == op_uMENURITEM ? tpad == (int)pp[2] : !!tpad;
			// Fallthrough
		case op_uMENUITEM:
			wid = pp[3];
			((swdata *)dtail)->strs = tbar;
			pk = pk_UNREALV;
			break;
		/* Script mode mount socket */
		case op_uMOUNT:
		{
			void **what = ((mnt_fn)pp[2])(res);
			*(int *)v = TRUE;
			((swdata *)dtail)->strs = what;
			pk = pk_UNREAL;
			break;
		}
		/* Done with a container */
		case op_WDONE:
#if GTK_MAJOR_VERSION == 3
			if (CT_WHAT(wp) == ct_SGROUP) CT_POP(wp);
#endif
			/* Prepare smart menubar when done */
			if (CT_WHAT(wp) == ct_SMARTMENU)
				vdata->smmenu = smartmenu_done(tbar, r);
			CT_DROP(wp);
			continue;
		/* Create the main window */
		case op_MAINWINDOW:
		{
			int wh = (int)pp[3];

			gdk_rgb_init();

			init_tablet();	// Set up the tablet

			widget = window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
// !!! Better to use WIDTH() and HEIGHT() as elsewhere
			// Set minimum width/height
#if GTK_MAJOR_VERSION == 3
			gtk_widget_set_size_request(window, wh >> 16, wh & 0xFFFF);
			/* Global initialization */
			tool_key = g_quark_from_static_string(TOOL_KEY);
#else
			gtk_widget_set_usize(window, wh >> 16, wh & 0xFFFF);
#endif
			// Set name _without_ translating
			gtk_window_set_title(GTK_WINDOW(window), v);

	/* !!! If main window receives these events, GTK+ will be able to
	 * direct them to current modal window. Which makes it possible to
	 * close popups by clicking on the main window outside popup - WJ */
			gtk_widget_add_events(window,
				GDK_BUTTON_PRESS_MASK | GDK_BUTTON_RELEASE_MASK);

			// we need to realize the window because we use pixmaps
			// for items on the toolbar & menus in the context of it
			gtk_widget_realize(window);

#if GTK_MAJOR_VERSION == 1
			{
				GdkPixmap *icon_pix = gdk_pixmap_create_from_xpm_d(
					window->window, NULL, NULL, pp[2]);
				gdk_window_set_icon(window->window, NULL, icon_pix, NULL);
//				gdk_pixmap_unref(icon_pix);
			}
#else /* #if GTK_MAJOR_VERSION >= 2 */
			{
				GdkPixbuf *p = gdk_pixbuf_new_from_xpm_data(pp[2]);
				gtk_window_set_icon(GTK_WINDOW(window), p);
				g_object_unref(p);
			}
#endif

			// For use as anchor and context
			main_window = window;

			ct = ct_BIN;
			break;
		}
		/* Create a toplevel window, and put a vertical box inside it */
		case op_WINDOW: case op_WINDOWm:
			vdata->modal = op != op_WINDOW;
			widget = window = add_a_window(GTK_WINDOW_TOPLEVEL,
				*(char *)v ? _(v) : v, wpos);
			sw = add_vbox(window);
			ct = ct_BOX;
			break;
		/* Create a dialog window, with vertical & horizontal box */
		case op_DIALOGm:
			vdata->modal = TRUE;
			widget = window = gtk_dialog_new();
			gtk_window_set_title(GTK_WINDOW(window), _(v));
			gtk_window_set_position(GTK_WINDOW(window), wpos);
// !!! Border = 6
			gtk_container_set_border_width(GTK_CONTAINER(window), 6);
			/* Both boxes go onto stack, with vbox on top */
			CT_PUSH(wp, gtk_dialog_get_action_area(GTK_DIALOG(window)), ct_BOX);
			CT_PUSH(wp, gtk_dialog_get_content_area(GTK_DIALOG(window)), ct_BOX);
			break;
		/* Create a fileselector window (with horizontal box inside) */
		case op_FPICKpm:
		{
			GtkWidget *box;

			vdata->modal = TRUE;
			widget = window = fpick(&box,
				*(char **)((char *)ddata + (int)pp[2]),
				*(int *)((char *)ddata + (int)pp[3]), r);
#ifdef U_FPICK_GTKFILESEL
			add_del(SLOT_N(r, 2), window);
#endif
			sw = box;
			ct = ct_BOX;
			/* Initialize */
			fpick_set_filename(window, v, FALSE);
			break;
		}
		/* Create a popup window */
		case op_POPUP:
			vdata->modal = TRUE;
			widget = window = add_a_window(GTK_WINDOW_POPUP,
				*(char *)v ? _(v) : v, wpos);
			cw = GET_BORDER(POPUP);
			ct = ct_BIN;
			break;
		/* Create a vbox which will serve as separate widget */
		case op_TOPVBOX:
			part = TRUE; // not toplevel
			widget = window = vbox_new(0);
			cw = GET_BORDER(TOPVBOX);
			ct = ct_BOX;
			pk = pk_SHOW;
			break;
		/* Create a widget vbox with special sizing behaviour */
		case op_TOPVBOXV:
			part = TRUE; // not toplevel
			// Fill space vertically but not horizontally
			widget = window = gtk_alignment_new(0.0, 0.5, 0.0, 1.0);
			// Keep max vertical size
			widget_set_keepsize(window, TRUE);
			sw = add_vbox(window);
			ct = ct_BOX;
			gtk_container_set_border_width(GTK_CONTAINER(sw),
				GET_BORDER(TOPVBOX));
			pk = pk_SHOW;
			break;
		/* Add a dock widget */
		case op_DOCK:
		{
			GtkWidget *p0, *p1, *pane;
			dock_data *dd = (void *)dtail;

			widget = hbox_new(0);
			gtk_widget_show(widget);

			/* First, create the dock pane - hidden for now */
			dd->pane = pane = gtk_hpaned_new();
			paned_mouse_fix(pane);
			gtk_box_pack_end(GTK_BOX(widget), pane, TRUE, TRUE, 0);

			/* Create the right pane */
			p1 = vbox_new(0);
			gtk_widget_show(p1);
			gtk_paned_pack2(GTK_PANED(pane), p1, FALSE, TRUE);
#if GTK_MAJOR_VERSION == 1
	/* !!! Hack - but nothing else seems to prevent a sorry mess when
	 * a widget gets REMOUNT'ed from a never-yet displayed pane */
			gtk_container_set_resize_mode(GTK_CONTAINER(p1),
				GTK_RESIZE_QUEUE);
#endif

			/* Now, create the left pane - for now, separate */
			dd->vbox = p0 = xpack(widget, vbox_new(0));
			gtk_widget_show(p0);

			/* Pack everything */
			if (do_pack(widget, wp, pp, pk, tpad)) CT_POP(wp);
			CT_PUSH(wp, p1, ct_BOX); // right page second
			CT_PUSH(wp, p0, ct_BOX); // left page first
// !!! Maybe pk_SHOW ?
			pk = 0;
			break;
		}
		/* Add a horizontal/vertical split widget */
		case op_HVSPLIT:
		{
			GtkWidget *p;
			hvsplit_data *hd = (void *)dtail;

			hd->box = widget = vbox_new(0);

			/* Create the two panes - hidden for now */
			hd->panes[0] = p = gtk_hpaned_new();
			paned_mouse_fix(p);
			gtk_box_pack_end(GTK_BOX(widget), p, TRUE, TRUE, 0);
			hd->panes[1] = p = gtk_vpaned_new();
			paned_mouse_fix(p);
			gtk_box_pack_end(GTK_BOX(widget), p, TRUE, TRUE, 0);

			sw = hd; // Datastruct in place of widget
			ct = ct_HVSPLIT;
			break;
		}
		/* Add a notebook page */
		case op_PAGE: case op_PAGEi:
		{
			GtkWidget *label = op == op_PAGE ?
				gtk_label_new(_(v)) : xpm_image(v);
			gtk_widget_show(label);
			widget = vbox_new(op == op_PAGE ? 0 : (int)pp[2]);
			gtk_notebook_append_page(GTK_NOTEBOOK(CT_TOP(wp)),
				widget, label);
			ct = ct_BOX;
			pk = pk_SHOW;
			break;
		}
		/* Add a table */
		case op_TABLE:
			widget = gtk_table_new((int)v & 0xFFFF, (int)v >> 16, FALSE);
			if (lp > 1)
			{
				int s = (int)pp[2];
				gtk_table_set_row_spacings(GTK_TABLE(widget), s);
				gtk_table_set_col_spacings(GTK_TABLE(widget), s);
			}
// !!! Padding = 0
			cw = GET_BORDER(TABLE);
			ct = ct_TABLE;
			break;
		/* Add an equal-spaced horizontal box */
		case op_EQBOX:
		/* Add a box */
		case op_VBOX: case op_HBOX:
#if GTK_MAJOR_VERSION == 3
			widget = gtk_box_new((op == op_VBOX ? GTK_ORIENTATION_VERTICAL :
				GTK_ORIENTATION_HORIZONTAL), (int)v & 255);
			gtk_box_set_homogeneous(GTK_BOX(widget), op == op_EQBOX);
#else
			widget = (op == op_VBOX ? gtk_vbox_new :
				gtk_hbox_new)(op == op_EQBOX, (int)v & 255);
#endif
			if (lp)
			{
				cw = ((int)v >> 8) & 255;
				tpad = ((int)v >> 16) & 255;
			}
			ct = ct_BOX;
			break;
		/* Add a frame */
		case op_FRAME:
			wid = v;
			// Fallthrough
		case op_EFRAME:
			widget = gtk_frame_new(v && *(char *)v ? _(v) : v);
			if (op == op_EFRAME) gtk_frame_set_shadow_type(
				GTK_FRAME(widget), GTK_SHADOW_ETCHED_OUT);
// !!! Padding = 0
			cw = GET_BORDER(FRAME);
			ct = ct_BIN;
			break;
		/* Add a scrolled window */
		case op_SCROLL:
			widget = scrollw((int)v);
			tpad = GET_BORDER(SCROLL);
			ct = ct_SCROLL;
			break;
		/* Add a control-like scrolled window */
		case op_CSCROLL:
		{
			int *xp = v;
			xp[0] = xp[1] = 0; // initial position
			widget = scrollw(0x101); // auto/auto
// !!! Padding = 0 Border = 0
			ct = ct_SCROLL;
			break;
		}
		/* Add a normal notebook */
		case op_NBOOKl: case op_NBOOK:
			widget = gtk_notebook_new();
			if (op == op_NBOOKl) gtk_notebook_set_tab_pos(
				GTK_NOTEBOOK(widget), GTK_POS_LEFT);
			if ((CT_WHAT(wp) == ct_SCROLL) && (pk <= pk_DEF))
				pk = pk_SCROLLVPn; // no border
// !!! Padding = 0
			cw = GET_BORDER(NBOOK);
			ct = ct_NBOOK;
			break;
		/* Add a plain notebook */
		case op_PLAINBOOK:
		{
			int n = v ? (int)v : 2; // 2 pages by default

			widget = gtk_notebook_new();
			gtk_notebook_set_show_tabs(GTK_NOTEBOOK(widget), FALSE);
			gtk_notebook_set_show_border(GTK_NOTEBOOK(widget), FALSE);

// !!! Border = 0
			if (do_pack(widget, wp, pp, pk, tpad)) CT_POP(wp);
			while (n-- > 0)
			{
				GtkWidget *page = vbox_new(0);
				gtk_notebook_prepend_page(GTK_NOTEBOOK(widget),
					page, NULL); // stack pages back to front
				CT_PUSH(wp, page, ct_BOX);
			}
			gtk_widget_show_all(widget);
			pk = 0;
			break;
		}
		/* Add a toggle button for controlling 2-paged notebook */
		case op_BOOKBTN:
			widget = gtk_toggle_button_new_with_label(_(pp[2]));
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget), FALSE);
			gtk_signal_connect(GTK_OBJECT(widget), "toggled",
				GTK_SIGNAL_FUNC(toggle_vbook), v);
// !!! Padding = 0
			cw = GET_BORDER(BUTTON);
			break;
		/* Add a statusbar box */
		case op_STATUSBAR:
		{
			GtkWidget *w, *label = gtk_label_new("");
			GtkRequisition req;

			w = widget = hbox_new(0);
#if GTK_MAJOR_VERSION == 3
			/* Do not let statusbar crowd anything else (unnecessary
			 * on GTK+1&2 due to different sizing method there) */
			w = wjsizebin_new(G_CALLBACK(do_shorten), NULL, label);
			gtk_widget_show(w);
			gtk_container_add(GTK_CONTAINER(w), widget);
#endif
		/* !!! The following is intended to give enough height to the bar
		 * even in case no items are shown. It depends on GTK+ setting
		 * proper height (and zero width) for a label containing an empty
		 * string. And size fixing isn't sure to set the right value if
		 * the toplevel isn't yet realized (unlike MAINWINDOW) - WJ */
			if (do_pack(w, wp, pp, pk, tpad)) CT_POP(wp);
			pack(widget, label);
			gtk_widget_show(label);
			/* To prevent statusbar wobbling */
#if GTK_MAJOR_VERSION == 3 /* !!! May need keepsize or maxsize */
			gtk_widget_get_preferred_size(widget, &req, NULL);
			gtk_widget_set_size_request(widget, -1, req.height);
#else /* if GTK_MAJOR_VERSION <= 2 */
			gtk_widget_size_request(widget, &req);
			gtk_widget_set_usize(widget, -1, req.height);
#endif

			ct = ct_BOX;
			pk = pk_SHOW;
			break;
		}
		/* Add a statusbar label */
		case op_STLABEL:
		{
			int paw = (int)v;
			widget = gtk_label_new("");
			gtk_misc_set_alignment(GTK_MISC(widget),
				((paw >> 16) & 255) / 2.0, 0.0);
			if (paw & 0xFFFF) mods.minw = paw & 0xFFFF; // usize
			// Label-specific packing
			if (pk == pk_PACKEND) pk = pk_PACKEND1;
// !!! Padding = 0 Border = 0
			break;
		}
		/* Add a horizontal line */
		case op_HSEP:
			widget = gtk_hseparator_new();
			if ((int)v >= 0) // usize
			{
				if (lp) mods.minw = (int)v;
// !!! Height = 10
				mods.minh = 10;
			}
// !!! Padding = 0
			break;
		/* Add a label */
		case op_LABEL: case op_uLABEL:
		{
			char *wi0 = wid;
			wid = v;
			// Maybe the preceding slot needs a label
			if (!wi0 && (script || scripted))
			{
				void **slot = prev_uslot(r);
				if (slot)
				{
					swdata *sd = slot[0];
					if (!sd->id && (sd->op != op_uOP))
						sd->id = wid , wid = NULL;
				}
			}
			if (op == op_uLABEL) // Script mode label
			{
				pk = pk_UNREAL;
				break;
			}
			// Fallthrough
		}
		case op_WLABEL:
		{
			int z = lp > 1 ? (int)pp[2] : 0;
			widget = gtk_label_new(*(char *)v ? _(v) : v);
			if (z & 0xFFFF) gtk_misc_set_padding(GTK_MISC(widget),
				(z >> 8) & 255, z & 255);
			gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
			gtk_misc_set_alignment(GTK_MISC(widget),
				(z >> 16) / 10.0, 0.5);
			tpad = GET_BORDER(LABEL);
			// Label-specific packing
			if (pk == pk_TABLE1x) pk = pk_TABLE0p;
			if (pk == pk_TABLE) pk = pk_TABLEp;
			if (op == op_WLABEL)
			{
#if GTK_MAJOR_VERSION == 3
				/* Code to keep wrapping width sane is absent in
				 * GTK+3, need be re-added as a wrapper */
				GtkWidget *wrap = wjsizebin_new(G_CALLBACK(do_rewrap), NULL, NULL);
				gtk_widget_show(wrap);
				gtk_container_add(GTK_CONTAINER(wrap), widget);
				pk |= pkf_PARENT;
#endif
				gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
			}
			break;
		}
		/* Add a helptext label */
		case op_HLABEL: case op_HLABELm:
			widget = gtk_label_new(v);
#if GTK_MAJOR_VERSION == 3
			gtk_widget_set_can_focus(widget, TRUE);
#else
			GTK_WIDGET_SET_FLAGS(widget, GTK_CAN_FOCUS);
#endif
#if GTK_MAJOR_VERSION >= 2
			gtk_label_set_selectable(GTK_LABEL(widget), TRUE);
#endif
#if GTK_MAJOR_VERSION == 3
			/* "font-size" stopped being broken only in GTK+ 3.22, and
			 * nagging about "Pango syntax" started - WJ */
			if (op == op_HLABELm) css_restyle(widget, (gtk3version >= 22 ?
				".mtPaint_hlabel { font-family: Monospace; font-size:9pt; }" :
				".mtPaint_hlabel { font: Monospace 9; }" ),
				"mtPaint_hlabel", NULL);
#elif GTK_MAJOR_VERSION == 2
			if (op == op_HLABELm)
			{
				PangoFontDescription *pfd =
					pango_font_description_from_string("Monospace 9");
					// Courier also works
				gtk_widget_modify_font(widget, pfd);
				pango_font_description_free(pfd);
			}
#endif
			gtk_label_set_justify(GTK_LABEL(widget), GTK_JUSTIFY_LEFT);
			gtk_label_set_line_wrap(GTK_LABEL(widget), TRUE);
			gtk_misc_set_alignment(GTK_MISC(widget), 0, 0);
// !!! Padding = 5/5
			gtk_misc_set_padding(GTK_MISC(widget), 5, 5);
			break;
		/* Add to table a batch of labels generated from text string */
		case op_TLTEXT:
			tltext(v, pp, CT_TOP(wp), GET_BORDER(LABEL));
			pk = 0;
			break;
		/* Add a progressbar */
		case op_PROGRESS:
			widget = gtk_progress_bar_new();
#if GTK_MAJOR_VERSION == 3
			gtk_progress_bar_set_text(GTK_PROGRESS_BAR(widget), _(v));
#else /* if GTK_MAJOR_VERSION <= 2 */
			gtk_progress_set_format_string(GTK_PROGRESS(widget), _(v));
			gtk_progress_set_show_text(GTK_PROGRESS(widget), TRUE);
#endif
// !!! Padding = 0
			break;
		/* Add a color patch renderer */
		case op_COLORPATCH:
			widget = gtk_drawing_area_new();
#if GTK_MAJOR_VERSION == 3
			gtk_widget_set_size_request(widget,
				(int)pp[2] >> 16, (int)pp[2] & 0xFFFF);
			g_signal_connect(G_OBJECT(widget), "draw",
				G_CALLBACK(col_expose), v);
#else /* if GTK_MAJOR_VERSION <= 2 */
			gtk_drawing_area_size(GTK_DRAWING_AREA(widget),
				(int)pp[2] >> 16, (int)pp[2] & 0xFFFF);
			gtk_signal_connect(GTK_OBJECT(widget), "expose_event",
				GTK_SIGNAL_FUNC(col_expose), v);
#endif
// !!! Padding = 0
			break;
		/* Add an RGB renderer */
		case op_RGBIMAGE:
			widget = rgbimage(r, (int *)((char *)ddata + (int)pp[2]));
// !!! Padding = 0
			break;
		/* Add a buffered (by pixmap) RGB renderer */
		case op_RGBIMAGEP:
			widget = rgbimagep(r, (int)pp[2] >> 16, (int)pp[2] & 0xFFFF);
// !!! Padding = 0
			break;
		/* Add a framed canvas-based renderer */
		case op_CANVASIMG:
			widget = canvasimg(r, (int)pp[2] >> 16, (int)pp[2] & 0xFFFF, 0);
			if ((CT_WHAT(wp) == ct_SCROLL) && (pk <= pk_DEF))
				pk = pk_BIN;
// !!! Padding = 0
			pk |= pkf_PARENT | pkf_CANVAS;
			break;
		/* Add a framed canvas-based renderer with background */
		case op_CANVASIMGB:
		{
			int *xp = (int *)((char *)ddata + (int)pp[2]);
			widget = canvasimg(r, xp[0], xp[1], xp[2]);
			if ((CT_WHAT(wp) == ct_SCROLL) && (pk <= pk_DEF))
				pk = pk_BIN;
// !!! Padding = 0
			pk |= pkf_PARENT | pkf_CANVAS;
			break;
		}
		/* Add a canvas widget */
		case op_CANVAS:
		{
			GtkWidget *frame;

			widget = wjcanvas_new();
			wjcanvas_size(widget, (int)v >> 16, (int)v & 0xFFFF);
			wjcanvas_set_expose(widget, GTK_SIGNAL_FUNC(expose_canvas_),
				NEXT_SLOT(r));

			frame = wjframe_new();
			gtk_widget_show(frame);
			gtk_container_add(GTK_CONTAINER(frame), widget);

// !!! For now, connection to scrollbars is automatic
			if ((CT_WHAT(wp) == ct_SCROLL) && (pk <= pk_DEF))
				pk = pk_BIN;
			pk |= pkf_PARENT | pkf_CANVAS;
			break;
		}
		/* Add a focusable buffered RGB renderer with cursor */
		case op_FCIMAGEP:
			widget = fcimagep(r, ddata);
// !!! Padding = 0
			break;
		/* Add a non-spinning spin to table slot */
		case op_NOSPIN:
		{
			int n = *(int *)v;
			widget = add_a_spin(n, n, n);
#if GTK_MAJOR_VERSION == 3
			gtk_widget_set_can_focus(widget, FALSE);
#else
			GTK_WIDGET_UNSET_FLAGS(widget, GTK_CAN_FOCUS);
#endif
			tpad = GET_BORDER(SPIN);
			break;
		}
		/* Add a spin, fill from field/var */
		case op_SPIN: case op_SPINc:
			widget = add_a_spin(*(int *)v, (int)pp[2], (int)pp[3]);
#if (GTK_MAJOR_VERSION == 3) || (GTK2VERSION >= 4) /* GTK+ 2.4+ */
			if (op == op_SPINc) gtk_entry_set_alignment(
				GTK_ENTRY(&(GTK_SPIN_BUTTON(widget)->entry)), 0.5);
#endif
			tpad = GET_BORDER(SPIN);
			break;
		/* Add float spin, fill from field/var */
		case op_FSPIN:
			widget = add_float_spin(*(int *)v / 100.0,
				(int)pp[2] / 100.0, (int)pp[3] / 100.0);
			tpad = GET_BORDER(SPIN);
			break;
		/* Add a spin, fill from array */
		case op_SPINa:
		{
			int *xp = v;
			widget = add_a_spin(xp[0], xp[1], xp[2]);
			tpad = GET_BORDER(SPIN);
			break;
		}
		/* Add a grid of spins, fill from array of arrays */
		// !!! Presents one widget out of all grid (the last one)
		case op_TLSPINPACK:
			r[2] = dtail -= VVS(TLSPINPACK_SIZE(pp));
			widget = tlspinpack(r, dtail, CT_TOP(wp), (int)pp[lp + 1]);
			pk = 0;
			break;
		/* Add a spinslider */
		case op_SPINSLIDE: case op_SPINSLIDEa:
		{
			int z = lp > 3 ? (int)pp[4] : 0;
			widget = mt_spinslide_new(z > 0xFFFF ? z >> 16 : -1,
				z & 0xFFFF ? z & 0xFFFF : -1);
			if (op == op_SPINSLIDEa) spin_set_range(widget,
				((int *)v)[1], ((int *)v)[2]);
			else spin_set_range(widget, (int)pp[2], (int)pp[3]);
			gtk_spin_button_set_value(GTK_SPIN_BUTTON(widget), *(int *)v);
			tpad = GET_BORDER(SPINSLIDE);
#if GTK_MAJOR_VERSION == 1
			have_sliders = TRUE;
#endif
			pk |= pkf_PARENT;
			break;
		}
		/* Add a named checkbox, fill from field/var/inifile */
		case op_CHECKb:
			*(int *)v = inifile_get_gboolean(pp[3], *(int *)v);
			// Fallthrough
		case op_CHECK:
			widget = gtk_check_button_new_with_label(_(wid = pp[2]));
			gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(widget),
				*(int *)v);
// !!! Padding = 0
			cw = GET_BORDER(CHECK);
			break;
		/* Add a pack of radio buttons for field/var */
		case op_RPACK: case op_RPACKD:
		/* Add a combobox for field/var */
		case op_COMBO:
			widget = mkpack(op != op_COMBO, op == op_RPACKD,
				ref, ddata, r);
// !!! Padding = 0
			cw = GET_BORDER(RPACK);
			if (op != op_COMBO) break;
			cw = GET_BORDER(OPT);
#if GTK2VERSION >= 4 /* GTK+ 2.4+ */
			/* !!! GtkComboBox ignores its border setting, and is
			 * easier to wrap than fix */
			if (cw)
			{
				GtkWidget *w = gtk_alignment_new(0.5, 0.5, 1.0, 1.0);
				gtk_container_add(GTK_CONTAINER(w), widget);
				gtk_widget_show(w);
				pk |= pkf_PARENT;
			}
#endif
			break;
		/* Add an option menu for field/var */
		case op_OPT: case op_OPTD:
#if GTK_MAJOR_VERSION == 3
			widget = r[0] = gtk_combo_box_text_new(); // Fix up slot
#else /* if GTK_MAJOR_VERSION <= 2 */
			widget = r[0] = gtk_option_menu_new(); // Fix up slot
			 /* !!! Show now - or size won't be set properly */
			gtk_widget_show(widget);
#endif
#if GTK_MAJOR_VERSION == 2
			gtk_signal_connect(GTK_OBJECT(widget), "realize",
				GTK_SIGNAL_FUNC(opt_size_fix), NULL);
#endif
			opt_reset(r, ddata, *(int *)v);
			/* !!! Border is better left alone - to avoid a drawing
			 * glitch in GTK+1 */
			tpad = GET_BORDER(OPT);
			break;
		/* Add an entry widget, fill from drop-away buffer */
		case op_ENTRY: case op_MLENTRY:
			widget = gtk_entry_new();
			if (lp > 1) gtk_entry_set_max_length(GTK_ENTRY(widget),
				(int)pp[2]);
			if (*(char **)v)
				gtk_entry_set_text(GTK_ENTRY(widget), *(char **)v);
			if (op == op_MLENTRY) accept_ctrl_enter(widget);
			// Replace transient buffer - it may get freed on return
			*(const char **)v = gtk_entry_get_text(GTK_ENTRY(widget));
			tpad = GET_BORDER(ENTRY);
			break;
		/* Add a path entry or box to table */
		case op_PENTRY:
#if GTK_MAJOR_VERSION == 3
			widget = gtk_entry_new();
			gtk_entry_set_max_length(GTK_ENTRY(widget), (int)pp[2]);
#else
			widget = gtk_entry_new_with_max_length((int)pp[2]);
#endif
			set_path(widget, v, PATH_VALUE);
			tpad = GET_BORDER(ENTRY);
			break;
		/* Add a path box to table */
		case op_PATHs:
			v = inifile_get(v, ""); // read and fallthrough
		case op_PATH:
			widget = pathbox(r, GET_BORDER(PATH));
			set_path(widget, v, PATH_VALUE);
// !!! Padding = 0 Border = 0
			pk |= pkf_PARENT;
			break;
		/* Add a text widget, fill from drop-away buffer at pointer */
		case op_TEXT:
			widget = textarea(*(char **)v);
			*(char **)v = NULL;
// !!! Padding = 0 Border = 0
			pk |= pkf_PARENT; // wrapped
			break;
		/* Add a font selector, fill from array: font name/test string */
		case op_FONTSEL:
			widget = fontsel(r, v);
// !!! Border = 4
			cw = 4;
			break;
#ifdef U_CPICK_MTPAINT
		/* Add a hex color entry */
		case op_HEXENTRY:
			widget = hexentry(*(int *)v, r);
// !!! Padding = 0 Border = 0
			break;
		/* Add an eyedropper button */
		case op_EYEDROPPER:
			widget = eyedropper(r);
// !!! Padding = 2 Border = 0
			tpad = 2;
			if (pk == pk_TABLE) pk = pk_TABLEp;
			break;
#endif
		/* Add a togglebutton for selecting shortcut keys */
		case op_KEYBUTTON:
		{
			keybutton_data *dt = (void *)dtail;
			dt->section = ini_setsection(&main_ini, 0, SEC_KEYNAMES);
			ini_transient(&main_ini, dt->section, NULL);

			widget = gtk_toggle_button_new_with_label(_("New key ..."));
#if GTK_MAJOR_VERSION == 1
			gtk_widget_add_events(widget, GDK_KEY_RELEASE_MASK);
#endif
			gtk_signal_connect(GTK_OBJECT(widget), "key_press_event",
				GTK_SIGNAL_FUNC(convert_key), dt);
			gtk_signal_connect(GTK_OBJECT(widget), "key_release_event",
				GTK_SIGNAL_FUNC(convert_key), dt);
			cw = GET_BORDER(BUTTON);
			break;
		}
		/* Add a button for tablet config dialog */
		case op_TABLETBTN:
			widget = gtk_button_new_with_label(_(v));
			gtk_signal_connect_object(GTK_OBJECT(widget), "clicked",
				GTK_SIGNAL_FUNC(conf_tablet), (gpointer)r);
			cw = GET_BORDER(BUTTON);
			break;
		/* Add a combo-entry for text strings */
		case op_COMBOENTRY:
			widget = comboentry(ddata, r);
// !!! Padding = 5
			tpad = 5;
			break;
		/* Add a color picker box, w/field array, & leave unfilled (?) */
		case op_COLOR: case op_TCOLOR:
			widget = cpick_create(op == op_TCOLOR);
			vdata->fupslot = r; // "Hex" needs defocus to update
// !!! Padding = 0
			break;
		/* Add a colorlist box, fill from fields */
		case op_COLORLIST: case op_COLORLISTN:
			widget = colorlist(r, ddata);
			if ((CT_WHAT(wp) == ct_SCROLL) && (pk <= pk_DEF))
#ifdef U_LISTS_GTK1
				pk = pk_SCROLLVPm;
#else
				pk = pk_BIN; // auto-connects to scrollbars
#endif
			break;
		/* Add a buttonbar for gradient */
		case op_GRADBAR:
			widget = gradbar(r, ddata);
// !!! Padding = 0
			break;
		/* Add a combo for percent values */
		case op_PCTCOMBO:
			widget = pctcombo(r);
// !!! Padding = 0
			break;
		/* Add a list with pre-defined columns */
		case op_LISTCCr:
			widget = listcc(r, ddata, &c);
			/* !!! For GtkTreeView this does not do anything in GTK+2
			 * and produces background-colored border in GTK+3;
			 * need to fix the coloring OR ignore the border: list in
			 * "Configure Animation" looks good without it too - WJ */
			cw = GET_BORDER(LISTCC);
			if ((CT_WHAT(wp) == ct_SCROLL) && (pk <= pk_DEF))
#ifdef U_LISTS_GTK1
				pk = pk_SCROLLVPv;
#else
				pk = pk_BIN; // auto-connects to scrollbars
#endif
			break;
		/* Add a clist with pre-defined columns */
		case op_LISTC: case op_LISTCd: case op_LISTCu:
		case op_LISTCS: case op_LISTCX:
			widget = listc(r, ddata, &c);
// !!! Border = 0
			if ((CT_WHAT(wp) == ct_SCROLL) && (pk <= pk_DEF))
				pk = pk_BIN; // auto-connects to scrollbars
			break;
		/* Add a clickable button */
		case op_BUTTON:
			wid = (int)pp[0] & WB_SFLAG ? v : "="; // Hide by default
			// Fallthrough
		case op_OKBTN: case op_CANCELBTN: case op_DONEBTN:
		{
			widget = gtk_button_new_with_label(_(v));
			if ((op == op_OKBTN) || (op == op_DONEBTN))
			{
				gtk_widget_add_accelerator(widget, "clicked", ag,
					KEY(Return), 0, (GtkAccelFlags)0);
				gtk_widget_add_accelerator(widget, "clicked", ag,
					KEY(KP_Enter), 0, (GtkAccelFlags)0);
				accel |= 1;
			}
			if ((op == op_CANCELBTN) || (op == op_DONEBTN))
			{
				gtk_widget_add_accelerator(widget, "clicked", ag,
					KEY(Escape), 0, (GtkAccelFlags)0);
				add_del(NEXT_SLOT(r), window);
				accel |= 1;
			}
			/* Click-event */
			if ((op != op_BUTTON) || pp[3])
				gtk_signal_connect_object(GTK_OBJECT(widget),
					"clicked", GTK_SIGNAL_FUNC(do_evt_1_d),
					(gpointer)NEXT_SLOT(r));
			cw = GET_BORDER(BUTTON);
			break;
		}
		/* Add a toggle button to OK-box */
		case op_TOGGLE:
			widget = gtk_toggle_button_new_with_label(_(pp[2]));
			if (pp[4]) gtk_signal_connect(GTK_OBJECT(widget),
				"toggled", GTK_SIGNAL_FUNC(get_evt_1), NEXT_SLOT(r));
			cw = GET_BORDER(BUTTON);
			break;
		/* Add a toolbar */
		case op_TOOLBAR: case op_SMARTTBAR:
		{
			GtkWidget *bar;
#if GTK_MAJOR_VERSION <= 2
			GtkWidget *vport;
			smarttbar_data *sd;
#endif

			tbar = r;
			rvar = rslot = NULL;
#if GTK_MAJOR_VERSION == 1
			widget = gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL,
				GTK_TOOLBAR_ICONS);
#else /* #if GTK_MAJOR_VERSION >= 2 */
			widget = gtk_toolbar_new();
			gtk_toolbar_set_style(GTK_TOOLBAR(widget), GTK_TOOLBAR_ICONS);
#endif
			ct = ct_TBAR;
			if (op != op_SMARTTBAR)
			{
				tpad = GET_BORDER(TOOLBAR);
				break;
			}

			// !!! Toolbar is what sits on stack till SMARTTBMORE
			sw = bar = widget;
			gtk_widget_show(bar);

#if GTK_MAJOR_VERSION == 3
			/* Just add a box, to hold extra things at end */
			widget = hbox_new(0);
			pack(widget, bar);
#else /* #if GTK_MAJOR_VERSION <= 2 */
			widget = wj_size_box();

			/* Make datastruct */
			sd = (void *)dtail;
			sd->tbar = bar;
			sd->r = r;

			gtk_signal_connect(GTK_OBJECT(widget), "size_request",
				GTK_SIGNAL_FUNC(htoolbox_size_req), sd);
			gtk_signal_connect(GTK_OBJECT(widget), "size_allocate",
				GTK_SIGNAL_FUNC(htoolbox_size_alloc), sd);

			vport = pack(widget, gtk_viewport_new(NULL, NULL));
			gtk_viewport_set_shadow_type(GTK_VIEWPORT(vport),
				GTK_SHADOW_NONE);
			gtk_widget_show(vport);
			vport_noshadow_fix(vport);
			sd->vport = vport;
			gtk_container_add(GTK_CONTAINER(vport), bar);
#endif /* GTK+1&2 */
// !!! Padding = 0
			break;
		}
		/* Add the arrow button to smart toolbar */
		case op_SMARTTBMORE:
		{
			GtkWidget *box = tbar[0];

			// !!! Box replaces toolbar on stack
			CT_POP(wp);
			CT_PUSH(wp, box, ct_BOX);
#if GTK_MAJOR_VERSION == 3
			/* Button is builtin */
			widget = NULL;
			pk = pk_NONE;
#else /* #if GTK_MAJOR_VERSION <= 2 */
			{
				smarttbar_data *sd = tbar[2];
				sd->r2 = r; // remember where the slots end

				widget = smarttbar_button(sd, v);
				pk = pk_SHOW;
			}
#endif /* GTK+1&2 */
			break;
		}
		/* Add a container-toggle beside toolbar */
		case op_TBBOXTOG:
			widget = pack(CT_N(wp, 1), gtk_toggle_button_new());
#if GTK_MAJOR_VERSION == 3
			gtk_widget_set_tooltip_text(widget, _(wid = pp[3]));
#else
			/* Parasite tooltip on toolbar */
			gtk_tooltips_set_tip(GTK_TOOLBAR(CT_TOP(wp))->tooltips,
				widget, _(wid = pp[3]), "Private");
#endif
#if (GTK_MAJOR_VERSION == 3) || (GTK2VERSION >= 4) /* GTK+ 2.4+ */
			gtk_button_set_focus_on_click(GTK_BUTTON(widget), FALSE);
#endif
			gtk_signal_connect(GTK_OBJECT(widget), "clicked",
				GTK_SIGNAL_FUNC(toolbar_lclick), NEXT_SLOT(tbar));
			ct = ct_BIN;
			pk = pk_SHOW;
			// Fallthrough
		/* Add a toolbar button/toggle */
		case op_TBBUTTON: case op_TBTOGGLE: case op_TBRBUTTON:
		{
			GtkWidget *rb = NULL;
#if GTK_MAJOR_VERSION == 3
			GtkToolItem *it = NULL;
			GtkWidget *m;
#endif

			if (keygroup) keymap_add(keymap, r, pp[3], keygroup);

			r[2] = tbar; // link to toolbar slot
			if (op == op_TBRBUTTON)
			{
				static int set = TRUE;

				if (rvar == v) rb = rslot[0];
				/* Now this represents group */
				rslot = r; rvar = v;
				/* Activate the one button whose ID matches var */
				v = *(int *)v == (int)pp[2] ? &set : NULL;
			}

#if GTK_MAJOR_VERSION == 3
			if (op == op_TBRBUTTON)
				it = gtk_radio_tool_button_new_from_widget(
					GTK_RADIO_TOOL_BUTTON(rb));
			else if (op == op_TBTOGGLE)
				it = gtk_toggle_tool_button_new();
			else if (op == op_TBBUTTON)
				it = gtk_tool_button_new(NULL, NULL);
			if (it)
			{
				gtk_tool_item_set_tooltip_text(it, _(wid = pp[3]));
				gtk_tool_button_set_icon_widget(GTK_TOOL_BUTTON(it),
					xpm_image(pp[4]));
				g_signal_connect(G_OBJECT(it),
					op == op_TBBUTTON ? "clicked" : "toggled",
					G_CALLBACK(toolbar_lclick), NEXT_SLOT(tbar));
				gtk_toolbar_insert(GTK_TOOLBAR(CT_TOP(wp)), it, -1);
				widget = GTK_WIDGET(it);
				pk = pk_SHOW;
				/* Set overflow menu to usable state */
				if (GET_OP(tbar) == op_SMARTTBAR)
				{
					m = gtk_tool_item_retrieve_proxy_menu_item(it);
					gtk_container_remove(GTK_CONTAINER(m),
						gtk_bin_get_child(GTK_BIN(m)));
					gtk_container_add(GTK_CONTAINER(m),
						xpm_image(pp[4]));
					gtk_widget_set_tooltip_text(m, _(pp[3]));
					/* Prevent item's re-creation */
					g_signal_connect(G_OBJECT(it),
						"create_menu_proxy",
						G_CALLBACK(leave_be), NULL);
				}
				if (v) gtk_toggle_tool_button_set_active(
					GTK_TOGGLE_TOOL_BUTTON(widget), *(int *)v);
			}
			else if (v) gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(widget), *(int *)v);
			g_object_set_qdata(G_OBJECT(widget), tool_key, r);
#else /* #if GTK_MAJOR_VERSION <= 2 */
			if (op != op_TBBOXTOG) widget = gtk_toolbar_append_element(
				GTK_TOOLBAR(CT_TOP(wp)),
				(op == op_TBBUTTON ? GTK_TOOLBAR_CHILD_BUTTON :
				op == op_TBRBUTTON ? GTK_TOOLBAR_CHILD_RADIOBUTTON :
				GTK_TOOLBAR_CHILD_TOGGLEBUTTON), rb,
				NULL, _(wid = pp[3]), "Private", xpm_image(pp[4]),
				GTK_SIGNAL_FUNC(toolbar_lclick), NEXT_SLOT(tbar));
			if (v) gtk_toggle_button_set_active(
				GTK_TOGGLE_BUTTON(widget), *(int *)v);
			gtk_object_set_user_data(GTK_OBJECT(widget), r);
#endif /* GTK+1&2 */
			if (lp > 4) gtk_signal_connect(GTK_OBJECT(widget),
				"button_press_event",
				GTK_SIGNAL_FUNC(toolbar_rclick), SLOT_N(tbar, 2));
			break;
		}
		/* Add a toolbar separator */
		case op_TBSPACE:
		{
#if GTK_MAJOR_VERSION == 3
			GtkToolItem *it = gtk_separator_tool_item_new();
			gtk_widget_show(GTK_WIDGET(it));
			gtk_toolbar_insert(GTK_TOOLBAR(CT_TOP(wp)), it, -1);
#else /* #if GTK_MAJOR_VERSION <= 2 */
			gtk_toolbar_append_space(GTK_TOOLBAR(CT_TOP(wp)));
#endif
			break;
		}
		/* Add a two/one row container for 2 toolbars */
		case op_TWOBOX:
#if GTK_MAJOR_VERSION == 3
			widget = gtk_flow_box_new();
			gtk_flow_box_set_selection_mode(GTK_FLOW_BOX(widget),
				GTK_SELECTION_NONE);
			ct = ct_CONT;
#else /* #if GTK_MAJOR_VERSION <= 2 */
			widget = wj_size_box();
			gtk_signal_connect(GTK_OBJECT(widget), "size_request",
				GTK_SIGNAL_FUNC(twobar_size_req), NULL);
			gtk_signal_connect(GTK_OBJECT(widget), "size_allocate",
				GTK_SIGNAL_FUNC(twobar_size_alloc), NULL);
			ct = ct_BOX;
#endif
// !!! Padding = 0
			break;
		/* Add a menubar */
		case op_MENUBAR: case op_SMARTMENU:
		{
			GtkWidget *bar;
			smartmenu_data *sd;

			// Stop dynamic allocation of accelerators during runtime
			accel |= 3;

			tbar = r;
			rvar = rslot = NULL;
			widget = gtk_menu_bar_new();

			ct = ct_CONT;
// !!! Padding = 0
			if (op != op_SMARTMENU) break;

			ct = ct_SMARTMENU;
			// !!! Menubar is what sits on stack
			sw = bar = widget;
			gtk_widget_show(bar);

			/* Make datastruct */
			sd = (void *)dtail;
			sd->mbar = bar;
			sd->r = r;

#if GTK_MAJOR_VERSION <= 2
			widget = wj_size_box();

			gtk_signal_connect(GTK_OBJECT(widget), "size_request",
				GTK_SIGNAL_FUNC(smart_menu_size_req), sd);
			gtk_signal_connect(GTK_OBJECT(widget), "size_allocate",
				GTK_SIGNAL_FUNC(smart_menu_size_alloc), sd);

			pack(widget, bar);
#endif
			break;
		}
		/* Add a dropdown submenu */
		case op_SUBMENU: case op_ESUBMENU: case op_SSUBMENU:
		{
			GtkWidget *label, *menu;
			char *s;
#if GTK_MAJOR_VERSION <= 2
			guint keyval;
#endif
			int l;

			gid = v; /* For keymap */

			widget = gtk_menu_item_new_with_label("");
			if (op == op_ESUBMENU)
				gtk_menu_item_right_justify(GTK_MENU_ITEM(widget));

			l = strspn(s = v, "/");
			if (s[l]) s = _(s); // Translate
			s += l;

			label = gtk_bin_get_child(GTK_BIN(widget));
#if GTK_MAJOR_VERSION == 3
			gtk_label_set_text_with_mnemonic(GTK_LABEL(label), s);
			/* !!! In case any non-toplevel submenu has an underline,
			 * in GTK+3 it'll have to be cut out from string beforehand */
#else /* #if GTK_MAJOR_VERSION <= 2 */
			keyval = gtk_label_parse_uline(GTK_LABEL(label), s);
			/* Toplevel submenus can have Alt+letter shortcuts */
			if ((l < 2) && (keyval != GDK_VoidSymbol))
#if GTK_MAJOR_VERSION == 1
				gtk_widget_add_accelerator(widget, "activate_item",
					ag, keyval, GDK_MOD1_MASK, GTK_ACCEL_LOCKED);
#else
				gtk_label_set_text_with_mnemonic(GTK_LABEL(label), s);
#endif
#endif /* GTK+1&2 */

			sw = menu = gtk_menu_new();
			ct = ct_CONT;
			gtk_menu_set_accel_group(GTK_MENU(menu), ag);
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(widget), menu);

			break;
		}
		/* Add a menu item/toggle */
		case op_MENUITEM: case op_MENUCHECK: case op_MENURITEM:
		{
			char *s;
			int l;

			if (keygroup) keymap_add(keymap, r, pp[3], keygroup);

			r[2] = tbar; // link to menu slot
			if (op == op_MENUCHECK)
				widget = gtk_check_menu_item_new_with_label("");
			else if (op == op_MENURITEM)
			{
				GSList *group = NULL;

				if (rvar == v) group = gtk_radio_menu_item_group(
					rslot[0]);
				/* Now this represents group */
				rslot = r; rvar = v;
				widget = gtk_radio_menu_item_new_with_label(group, "");
			}
#if GTK_MAJOR_VERSION >= 2
			else if ((lp > 3) && show_menu_icons)
			{
				widget = gtk_image_menu_item_new_with_label("");
				gtk_image_menu_item_set_image(
					GTK_IMAGE_MENU_ITEM(widget), xpm_image(pp[4]));
			}
#endif
			else widget = gtk_menu_item_new_with_label("");

			if (v) /* Initialize a check/radio item */
			{
				int f = *(int *)v;
				/* Activate the one button whose ID matches var */
				if ((op != op_MENURITEM) || (f = f == (int)pp[2]))
					gtk_check_menu_item_set_active(
						GTK_CHECK_MENU_ITEM(widget), f);
#if GTK_MAJOR_VERSION <= 2
				gtk_check_menu_item_set_show_toggle(
					GTK_CHECK_MENU_ITEM(widget), TRUE);
#endif
			}

			l = strspn(s = pp[3], "/");
			if (s[l]) s = _(s); // Translate
			s += l;

#if GTK_MAJOR_VERSION == 3
			/* !!! In case any regular menuitem has an underline,
			 * in GTK+3 it'll have to be cut out from string beforehand */
			gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(widget))), s);

			g_object_set_qdata(G_OBJECT(widget), tool_key, r);
#else /* #if GTK_MAJOR_VERSION <= 2 */
			gtk_label_parse_uline(GTK_LABEL(GTK_BIN(widget)->child), s);

			gtk_object_set_user_data(GTK_OBJECT(widget), r);
#endif
			gtk_signal_connect(GTK_OBJECT(widget), "activate",
				GTK_SIGNAL_FUNC(menu_evt), NEXT_SLOT(tbar));

#if GTK_MAJOR_VERSION >= 2
		/* !!! Otherwise GTK+ won't add spacing to an empty accel field */
			gtk_widget_set_accel_path(widget, FAKE_ACCEL, ag);
#endif
#if (GTK_MAJOR_VERSION == 3) || (GTK2VERSION >= 4)
		/* !!! GTK+ 2.4+ ignores invisible menu items' keys by default */
			gtk_signal_connect(GTK_OBJECT(widget), "can_activate_accel",
				GTK_SIGNAL_FUNC(menu_allow_key), NULL);
#endif
			break;
		}
		/* Add a tearoff menu item */
		case op_MENUTEAR:
			widget = gtk_tearoff_menu_item_new();
			break;
		/* Add a separator menu item */
		case op_MENUSEP:
			widget = gtk_menu_item_new();
			gtk_widget_set_sensitive(widget, FALSE);
			break;
		/* Add a mount socket with custom-built separable widget */
		case op_MOUNT:
		{
			void **what = r[2] = ((mnt_fn)pp[2])(res);
			*(int *)v = TRUE;
			widget = gtk_alignment_new(0.5, 0.5, 1.0, 1.0);
			gtk_container_add(GTK_CONTAINER(widget),
				GET_REAL_WINDOW(what));
// !!! Padding = 0
			if (lp > 2 + 2) // put socket in pane w/preset size
			{
				GtkWidget *pane = gtk_vpaned_new();
				paned_mouse_fix(pane);
				gtk_paned_set_position(GTK_PANED(pane),
					inifile_get_gint32(pp[3], (int)pp[4]));
				gtk_paned_pack2(GTK_PANED(pane), vbox_new(0), TRUE, TRUE);
				gtk_widget_show_all(pane);
				gtk_paned_pack1(GTK_PANED(pane),
					widget, FALSE, TRUE);
				pk |= pkf_PARENT;
			}
			break;
		}
		/* Steal a widget from its mount socket by slot ref */
		case op_REMOUNT:
		{
			void **where = *(void ***)v;
			GtkWidget *what = gtk_bin_get_child(GTK_BIN(where[0]));

			widget = gtk_alignment_new(0.5, 0.5, 1.0, 1.0);
			gtk_widget_hide(where[0]);
			gtk_widget_reparent(what, widget);
			get_evt_1(where[0], NEXT_SLOT(where));
			break;
		}
		/* Add a height-limiter item */
		case op_HEIGHTBAR:
		{
			widget = gtk_label_new(""); // Gives useful lower limit
#if GTK_MAJOR_VERSION == 3
			GtkSizeGroup *sg = gtk_size_group_new(GTK_SIZE_GROUP_VERTICAL);
			gtk_size_group_set_ignore_hidden(sg, FALSE);
			CT_PUSH(wp, sg, ct_SGROUP);
#else /* #if GTK_MAJOR_VERSION <= 2 */
			gtk_signal_connect(GTK_OBJECT(widget), "size_request",
				GTK_SIGNAL_FUNC(heightbar_size_req), NULL);
#endif
			break;
		}
#if 0
		/* Call a function */
		case op_EXEC:
			r = ((ext_fn)v)(r, &wp, res);
			continue;
#endif
		/* Call a V-code subroutine */
		case op_CALL:
			*rp++ = ifcode;
			// Fallthrough
		/* Do a V-code jump */
		case op_GOTO:
			ifcode = v;
			continue;
		/* Return from V-code subroutine */
		case op_RET:
			ifcode = *--rp;
			continue;
		/* Skip next token(s) if/unless field/var is unset */
		case op_IF: case op_UNLESS:
			if (!*(int *)v ^ (op != op_IF))
				ifcode = skip_if(pp);
			continue;
		/* Skip next token(s) unless inifile var, set by default, is unset */
		case op_UNLESSbt:
			if (inifile_get_gboolean(v, TRUE))
				ifcode = skip_if(pp);
			continue;
		/* Put last referrable widget into activation map */
		case op_ACTMAP:
			if (lp > 1) vdata->vismask = (unsigned)v; // VISMASK
			else
			{
				void **where = vdata->actmap + vdata->actn++ * ACT_SIZE;
				ADD_ACT(where, origin_slot(PREV_SLOT(r)), v);
			}
			continue;
		/* Set up a configurable keymap for widgets */
		case op_KEYMAP:
			vdata->keymap = r;
			r[2] = dtail -= VVS(KEYMAP_SIZE(sz.keys));
			keymap = (void *)dtail;
			keymap->res = v;
			keymap->slotsec = keygroup =
				ini_setsection(&main_ini, 0, pp[2]);
			ini_transient(&main_ini, keygroup, NULL); // Not written out
			widget = NULL;
			break;
		/* Add a shortcut, from text desc, to last referrable widget */
		case op_SHORTCUT:
		{
			void **slot = origin_slot(PREV_SLOT(r));
			int op = GET_OP(slot);
			guint keyval = 0, mods = 0;

			if (!lp); // Do nothing
			else if (lp < 2) gtk_accelerator_parse(v, &keyval, &mods);
			else keyval = (guint)v , mods = (guint)pp[2];

			if (keygroup &&
				// Already mapped
				((keymap->slots[keymap->nslots].slot == slot) ||
				// On-demand mapping for script mode menuitems
				((op >= op_uMENU_0) && (op < op_uMENU_LAST) &&
				 keymap_add(keymap, slot, GET_DESCV(slot, 3), keygroup))))
			{
				if (lp) keymap_map(keymap, keymap->nslots, keyval, mods);
				continue;
			}
#if GTK_MAJOR_VERSION >= 2
			/* !!! In case one had been set (for menu spacing) */
			gtk_widget_set_accel_path(*slot, NULL, ag);
#endif
			gtk_widget_add_accelerator(*slot, "activate",
				ag, keyval, mods, GTK_ACCEL_VISIBLE);
			accel |= 1;
			continue;
		}
		/* Install priority key handler for when widget is focused */
		case op_WANTKEYS:
			vdata->wantkey = r;
			widget = NULL;
			break;
		/* Store a reference to whatever is next into field */
		case op_REF:
			*(void **)v = r;
			continue;
		/* Make toplevel window shrinkable */
		case op_MKSHRINK:
#if GTK_MAJOR_VERSION == 3
			/* Just do smarter initial sizing, here it is enough */
			gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
			g_signal_connect(window, "draw", G_CALLBACK(make_resizable), NULL);
#else /* #if GTK_MAJOR_VERSION <= 2 */
			gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
#endif
			continue;
		/* Make toplevel window non-resizable */
		case op_NORESIZE:
#if GTK_MAJOR_VERSION == 3
			gtk_window_set_resizable(GTK_WINDOW(window), FALSE);
#else
			gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, TRUE);
#endif
			continue;
		/* Make scrolled window request max size */
		case op_WANTMAX:
			mods.wantmax = (int)v + 1;
			continue;
		/* Make widget keep max requested width/height */
		case op_KEEPSIZE:
			widget_set_keepsize(widget, lp);
			continue;
		/* Use saved size & position for window */
		case op_WXYWH:
		{
			unsigned int n = (unsigned)pp[2];
			vdata->ininame = v;
			vdata->xywh[2] = n >> 16;
			vdata->xywh[3] = n & 0xFFFF;
			if (v) rw_pos(vdata, FALSE);
			continue;
		}
		/* Make toplevel window be positioned at mouse */
		case op_WPMOUSE: wpos = GTK_WIN_POS_MOUSE; continue;
		/* Make toplevel window be positioned anywhere WM pleases */
		case op_WPWHEREVER: wpos = GTK_WIN_POS_NONE; continue;
		/* Make last referrable widget hidden */
		case op_HIDDEN:
			gtk_widget_hide(get_wrap(origin_slot(PREV_SLOT(r))));
			continue;
		/* Make last referrable widget insensitive */
		case op_INSENS:
			cmd_sensitive(origin_slot(PREV_SLOT(r)), FALSE);
			continue;
		/* Make last referrable widget focused */
		case op_FOCUS:
		{
			void **orig = origin_slot(PREV_SLOT(r));
			/* !!! For GtkFontSelection, focusing needs be done after
			 * window is shown - in GTK+ 2.24.10, at least */
			if (GET_OP(orig) == op_FONTSEL) gtk_signal_connect_object(
				GTK_OBJECT(window), "show",
				GTK_SIGNAL_FUNC(gtk_widget_grab_focus),
				(gpointer)gtk_font_selection_get_preview_entry(
				GTK_FONT_SELECTION(*orig)));
			else gtk_widget_grab_focus(*orig);
			continue;
		}
		/* Set fixed/minimum width for next widget */
		case op_WIDTH:
			mods.minw = (int)v;
			continue;
		/* Set fixed/minimum height for next widget */
		case op_HEIGHT:
			mods.minh = (int)v;
			continue;
		/* Make window transient to given widget-map */
		case op_ONTOP:
			tparent = !v ? NULL :
				GTK_WINDOW(GET_REAL_WINDOW(*(void ***)v));
			continue;
		/* Change identifier, for reusable toplevels */
		case op_IDENT:
			ident = v;
			continue;
		/* Raise window after displaying */
		case op_RAISED:
			vdata->raise = TRUE;
			continue;
		/* Start group of list columns */
		case op_WLIST:
			memset(&c, 0, sizeof(c));
			continue;
		/* Add a datablock pseudo-column */
		case op_COLUMNDATA:
			c.dcolumn = pp;
			continue;
		/* Add a regular list column */
		case op_IDXCOLUMN: case op_TXTCOLUMN: case op_XTXTCOLUMN:
		case op_FILECOLUMN: case op_CHKCOLUMN:
			((swdata *)dtail)->cnt = c.ncol;
			c.columns[c.ncol++] = r;
			pk = pk_UNREAL;
			if ((script || scripted) && (lp - ref * 2 > 1))
			{
				wid = pp[4];
				tpad = 0;
				pk = pk_UNREALV;
			}
			break;
		/* Create an XBM cursor */
		case op_XBMCURSOR:
		{
			int xyl = (int)pp[3], l = xyl >> 16;
			widget = (void *)make_cursor(v, pp[2], l, l,
				(xyl >> 8) & 255, xyl & 255);
			break;
		}
		/* Create a system cursor */
		case op_SYSCURSOR:
			widget = (void *)gdk_cursor_new((int)v);
			break;
		/* Add a group of clipboard formats */
		case op_CLIPFORM:
		{
			int i, n = (int)pp[2], l = sizeof(clipform_data) + 
				sizeof(GtkTargetEntry) * (n - 1);
			clipform_data *cd = calloc(1, l);

			cd->n = n;
			cd->src = v;
			for (i = 0; i < n; i++)
			{
				cd->ent[i].target = cd->src[i].target;
				cd->ent[i].info = i;
				/* cd->ent[i].flags = 0; */
			}
			cd->targets = gtk_target_list_new(cd->ent, n);

			widget = (void *)cd;
			break;
		}
		/* Install drag/drop handlers */
// !!! For drag, this must be done before mouse event handlers
		case op_DRAGDROP:
			widget = dragdrop(r);
			break;
		/* Add a clipboard control slot */
		case op_CLIPBOARD:
			widget = **(void ***)v; // from CLIPFORM
			break;
		/* Install activate event handler */
		case op_EVT_OK:
		{
			void **slot = origin_slot(PREV_SLOT(r));
			int what = GET_OP(slot);
// !!! Support only what actually used on, and their brethren
			switch (what)
			{
			case op_ENTRY: case op_MLENTRY: case op_PENTRY:
			case op_PATH: case op_PATHs:
				gtk_signal_connect(GTK_OBJECT(*slot), "activate",
					GTK_SIGNAL_FUNC(get_evt_1), r);
				break;
			case op_LISTC: case op_LISTCd: case op_LISTCu:
			case op_LISTCS: case op_LISTCX:
			{
				listc_data *ld = slot[2];
				ld->ok = r;
				break;
			}
			}
			widget = NULL;
			break;
		}
		/* Install destroy event handler (onto window) */
		case op_EVT_CANCEL:
			add_del(r, window);
			widget = NULL;
			break;
		/* Install deallocation event handler */
		case op_EVT_DESTROY:
			vdata->destroy = r;
			widget = NULL;
			break;
		/* Install key event handler */
		case op_EVT_KEY:
		{
			void **slot = origin_slot(PREV_SLOT(r));
			int toplevel = (slot == GET_WINDOW(res)) &&
				GTK_IS_WINDOW(slot[0]);

			gtk_signal_connect(GTK_OBJECT(*slot), "key_press_event",
				// Special handler for toplevels
				toplevel ? GTK_SIGNAL_FUNC(window_evt_key) :
				GTK_SIGNAL_FUNC(get_evt_key), r);
			enable_events(slot, op);
			widget = NULL;
			break;
		}
		/* Install mouse event handler */
		case op_EVT_MOUSE: case op_EVT_MMOUSE: case op_EVT_RMOUSE:
		case op_EVT_XMOUSE: case op_EVT_MXMOUSE: case op_EVT_RXMOUSE:
			add_mouse(r, op);
			widget = NULL;
			break;
		/* Install crossing event handler */
		case op_EVT_CROSS:
		{
			void **slot = origin_slot(PREV_SLOT(r));
			gtk_signal_connect(GTK_OBJECT(*slot), "enter_notify_event",
				GTK_SIGNAL_FUNC(get_evt_cross), r);
			gtk_signal_connect(GTK_OBJECT(*slot), "leave_notify_event",
				GTK_SIGNAL_FUNC(get_evt_cross), r);
			enable_events(slot, op);
			widget = NULL;
			break;
		}
#if GTK_MAJOR_VERSION >= 2
		/* Install scroll event handler */
		case op_EVT_SCROLL:
		{
			void **slot = origin_slot(PREV_SLOT(r));
			gtk_signal_connect(GTK_OBJECT(*slot), "scroll_event",
				GTK_SIGNAL_FUNC(get_evt_scroll), r);
	/* !!! The classical scroll events cannot be reliably received on any
	 * GdkWindow with GDK_SMOOTH_SCROLL_MASK set (such as the ones of GTK+3
	 * builtin scrollable widgets); if that is ever needed, enable_events()
	 * will need install a realize handler which forcibly removes the flag
	 * from all widget's GdkWindows - WJ */
			enable_events(slot, op);
			widget = NULL;
			break;
		}
#endif
		/* Install Change-event handler */
		case op_EVT_CHANGE:
		{
			void **slot = origin_slot(PREV_SLOT(r));
			int what = GET_OP(slot);
// !!! Support only what actually used on, and their brethren
			switch (what)
			{
			case op_SPINSLIDE: case op_SPINSLIDEa:
			case op_SPIN: case op_SPINc: case op_SPINa:
			case op_FSPIN:
				spin_connect(*slot,
					GTK_SIGNAL_FUNC(get_evt_1), r);
				break;
			case op_CHECK: case op_CHECKb:
				gtk_signal_connect(*slot, "toggled",
					GTK_SIGNAL_FUNC(get_evt_1), r);
				break;
			case op_COLOR: case op_TCOLOR:
				cpick_set_evt(*slot, r);
				break;
			case op_CSCROLL:
			{
				GtkAdjustment *xa, *ya;
				get_scroll_adjustments(*slot, &xa, &ya);
				gtk_signal_connect(GTK_OBJECT(xa), "value_changed",
					GTK_SIGNAL_FUNC(get_evt_1), r);
				gtk_signal_connect(GTK_OBJECT(ya), "value_changed",
					GTK_SIGNAL_FUNC(get_evt_1), r);
				break;
			}
			case op_CANVAS:
		/* !!! This is sent after realize, or resize while realized */
				gtk_signal_connect(*slot, "configure_event",
					GTK_SIGNAL_FUNC(get_evt_conf), r);
				break;
			case op_TEXT:
#if GTK_MAJOR_VERSION >= 2 /* In GTK+1, same handler as for GtkEntry */
				g_signal_connect(gtk_text_view_get_buffer(
					GTK_TEXT_VIEW(*slot)), "changed",
					GTK_SIGNAL_FUNC(get_evt_1), r);
				break;
#endif
			case op_ENTRY: case op_MLENTRY: case op_PENTRY:
			case op_PATH: case op_PATHs:
				gtk_signal_connect(*slot, "changed",
					GTK_SIGNAL_FUNC(get_evt_1), r);
				break;
			case op_LISTCX:
			{
				listc_data *ld = slot[2];
				ld->change = r;
				break;
			}
			}
		} // fallthrough
		/* Remember that event needs triggering here */
		/* Or remember a cleanup location */
		case op_EVT_SCRIPT: case op_EVT_MULTI:
		case op_TRIGGER: case op_CLEANUP:
			widget = NULL;
			break;
		/* Allocate/copy memory */
		case op_TALLOC: case op_TCOPY:
		{
			int l = *(int *)((char *)ddata + (int)pp[2]);
			if (!l) continue;
			dtail -= VVS(l);
			if (op == op_TCOPY) memcpy(dtail, *(void **)v, l);
			*(void **)v = dtail;
			continue;
		}
		default:
			/* Set nondefault border size */
			if ((op >= op_BOR_0) && (op < op_BOR_LAST))
				borders[op - op_BOR_0] = lp ? (int)v - DEF_BORDER : 0;
			continue;
		}
		if (ref)
		{
			/* Finish pseudo widget */
			if (pk == pk_UNREALV)
			{
				((swdata *)dtail)->value = tpad;
				((swdata *)dtail)->id = wid;
				wid = NULL;
				pk = pk_UNREAL;
			}
			if (pk == pk_UNREAL)
			{
				widget = (void *)dtail;
				((swdata *)dtail)->op = op;
				pk = 0;
			}
			/* Remember this */
			FIX_SLOT(r, widget ? (void *)widget : res);
			/* Remember events */
			if (ref > 2) ADD_SLOT(r, res, pp + lp - 3, NULL);
			if (ref > 1) ADD_SLOT(r, res, pp + lp - 1, NULL);
			/* Remember name */
			if (scripted && cmds[op] && (cmds[op]->uop > 0) &&
				(cmds[op]->uop != op_uLABEL))
			{
				static void *id_ALTNAME = WBrh(uALTNAME, 0);
				dtail -= VVS(sizeof(swdata));
				ADD_SLOT(r, dtail, &id_ALTNAME, dtail);
				((swdata *)dtail)->op = op_uALTNAME;
				((swdata *)dtail)->id = wid;
				wid = NULL;
			}
		}
		/* Pack this according to mode flags */
		if (script) continue; // no packing in script mode
		mods.cw = cw;
		{
			GtkWidget *w = do_prepare(widget, pk, &mods);
			memset(&mods, 0, sizeof(mods));
			if (((pk & pk_MASK) != pk_NONE) && do_pack(w, wp, pp, pk, tpad))
				CT_POP(wp); // unstack
		}
		/* Stack this */
		if (ct) CT_PUSH(wp, sw ? sw : widget, ct);
		ct = 0;
		sw = NULL;
		if (gid && keygroup) // Nested group
			keygroup = ini_setsection(&main_ini, keygroup, gid);
	}
}

static void do_destroy(void **wdata)
{
	void **pp, *v = NULL;
	char *data = GET_DDATA(wdata);
	v_dd *vdata = GET_VDATA(wdata);
	int op;

	if (vdata->done) return; // Paranoia
	vdata->done = TRUE;

	if (vdata->destroy)
	{
		void **base = vdata->destroy[0], **desc = vdata->destroy[1];
		((evt_fn)desc[1])(GET_DDATA(base), base,
			(int)desc[0] & WB_OPMASK, vdata->destroy);
	}

	for (wdata = GET_WINDOW(wdata); (pp = wdata[1]); wdata = NEXT_SLOT(wdata))
	{
		op = (int)*pp++;
		v = pp[0];
		if (op & WB_FFLAG) v = data + (int)v;
		if (IS_UNREAL(wdata)) op = GET_UOP(wdata);
		op &= WB_OPMASK;
		switch (op)
		{
		case op_uFPICK: v = &((swdata *)*wdata)->strs; // Fallthrough
		case op_CLEANUP: free(*(void **)v); break;
		case op_CLIPFORM:
		{
			clipform_data *cd = *wdata;
			gtk_target_list_unref(cd->targets);
			free(cd);
			break;
		}
		case op_uENTRY: case op_uPATHSTR:
			v = &((swdata *)*wdata)->strs;
			// Fallthrough
		case op_TEXT: case op_FONTSEL: g_free(*(char **)v); break;
		case op_TABLETBTN: conf_done(NULL); break;
		case op_uMOUNT:
			// !!! REMOUNT not expected in unreal state
			run_destroy(((swdata *)*wdata)->strs);
			break;
		case op_REMOUNT:
		{
			void **where = *(void ***)v;
			GtkWidget *what = gtk_bin_get_child(GTK_BIN(*wdata));

			gtk_widget_reparent(what, where[0]);
			gtk_widget_show(where[0]);
			get_evt_1(where[0], NEXT_SLOT(where));
			break;
		}
		case op_LISTCX: listcx_done(*wdata, wdata[2]); break;
		case op_MAINWINDOW: gtk_main_quit(); break;
		}
	}
}

static void *do_query(char *data, void **wdata, int mode)
{
	void **pp, *v = NULL;
	int op;

	for (; (pp = wdata[1]); wdata = NEXT_SLOT(wdata))
	{
		op = (int)*pp++;
		v = op & (~0U << WB_LSHIFT) ? pp[0] : NULL;
		if (op & WB_FFLAG) v = data + (int)v;
		if (op & WB_NFLAG) v = *(void **)v; // dereference
		if (IS_UNREAL(wdata)) op = GET_UOP(wdata);
		op &= WB_OPMASK;
		switch (op)
		{
		case op_FPICKpm:
			fpick_get_filename(*wdata, v, PATHBUF, FALSE);
			break;
		case op_uFPICK:
			strncpy0(v, ((swdata *)*wdata)->strs, PATHBUF);
			break;
		case op_SPINSLIDE: case op_SPINSLIDEa:
		case op_SPIN: case op_SPINc: case op_SPINa:
			*(int *)v = mode & 1 ? gtk_spin_button_get_value_as_int(
				GTK_SPIN_BUTTON(*wdata)) : read_spin(*wdata);
			break;
		case op_uSPIN: case op_uFSPIN: case op_uSPINa: case op_uSCALE:
		case op_uCHECK: case op_uCHECKb:
		case op_uOPT: case op_uOPTD: case op_uRPACK: case op_uRPACKD:
		case op_uCOLOR: case op_uMENUCHECK:
			*(int *)v = ((swdata *)*wdata)->value;
			if (op == op_uCHECKb) inifile_set_gboolean(pp[2], *(int *)v);
			break;
		case op_FSPIN:
			*(int *)v = rint((mode & 1 ?
#if GTK_MAJOR_VERSION == 3
				gtk_spin_button_get_value(GTK_SPIN_BUTTON(*wdata)) :
#else
				GTK_SPIN_BUTTON(*wdata)->adjustment->value :
#endif
				read_float_spin(*wdata)) * 100);
			break;
		case op_TBTOGGLE:
#if GTK_MAJOR_VERSION == 3 /* In GTK+1&2, same handler as for GtkToggleButton */
			*(int *)v = gtk_toggle_tool_button_get_active(*wdata);
			break;
#endif
		case op_CHECK: case op_CHECKb: case op_TOGGLE: case op_TBBOXTOG:
			*(int *)v = gtk_toggle_button_get_active(*wdata);
			if (op == op_CHECKb) inifile_set_gboolean(pp[2], *(int *)v);
			break;
		case op_TBRBUTTON:
		{
			GSList *group;
			void **slot = wdata;

			/* If reading radio group through an inactive slot */
#if GTK_MAJOR_VERSION == 3
			if (!gtk_toggle_tool_button_get_active(*wdata))
			{
				/* Let outer loop find active item */
				if (mode <= 1) break;
				/* Otherwise, find active item here */
				group = gtk_radio_tool_button_get_group(*wdata);
				/* !!! The ugly thing returns a group of _regular_
				 * radiobuttons which sit inside toolbuttons */
				while (group && !gtk_toggle_button_get_active(
					GTK_TOGGLE_BUTTON(group->data)))
					group = group->next;
				if (!group) break; // impossible happened
				slot = g_object_get_qdata(G_OBJECT(
					gtk_widget_get_parent(group->data)), tool_key);
			}
#else /* #if GTK_MAJOR_VERSION <= 2 */
			if (!gtk_toggle_button_get_active(*wdata))
			{
				/* Let outer loop find active item */
				if (mode <= 1) break;
				/* Otherwise, find active item here */
				group = gtk_radio_button_group(*wdata);
				while (group && !GTK_TOGGLE_BUTTON(group->data)->active)
					group = group->next;
				if (!group) break; // impossible happened
				slot = gtk_object_get_user_data(
					GTK_OBJECT(group->data));
			}
#endif
			*(int *)v = TOOL_ID(slot);
			break;
		}
		case op_MENUCHECK:
			*(int *)v = gtk_check_menu_item_get_active(
				GTK_CHECK_MENU_ITEM(*wdata));
			break;
		case op_MENURITEM:
		{
			GSList *group;
			void **slot = wdata;

			/* If reading radio group through an inactive slot */
			if (!gtk_check_menu_item_get_active(GTK_CHECK_MENU_ITEM(*wdata)))
			{
				/* Let outer loop find active item */
				if (mode <= 1) break;
				/* Otherwise, find active item here */
				group = gtk_radio_menu_item_group(*wdata);
				while (group && !gtk_check_menu_item_get_active(
					GTK_CHECK_MENU_ITEM(group->data)))
					group = group->next;
				if (!group) break; // impossible happened
#if GTK_MAJOR_VERSION == 3
				slot = g_object_get_qdata(G_OBJECT(group->data),
					tool_key);
#else
				slot = gtk_object_get_user_data(
					GTK_OBJECT(group->data));
#endif
			}
			*(int *)v = TOOL_ID(slot);
			break;
		}
		case op_uMENURITEM:
		{
			void **slot = wdata;
			int n;

			/* If reading radio group through an inactive slot */
			if (!((swdata *)*wdata)->value)
			{
				/* Let outer loop find active item */
				if (mode <= 1) break;
				/* Otherwise, find active item here */
				while ((n = ((swdata *)slot[0])->range[0]))
					slot -= n;
				while (TRUE)
				{
					if (((swdata *)slot[0])->value) break;
					if (!(n = ((swdata *)slot[0])->range[1]))
						break;
					slot += n;
				}
				if (!n) break; // impossible happened
			}
			*(int *)v = TOOL_ID(slot);
			break;
		}
		case op_TLSPINPACK: case op_HEXENTRY: case op_EYEDROPPER:
		case op_COLORLIST: case op_COLORLISTN: case op_GRADBAR:
		case op_LISTCCr: case op_uLISTCC:
		case op_LISTC: case op_LISTCd: case op_LISTCu:
		case op_LISTCS: case op_LISTCX: case op_uLISTC:
			break; // self-reading
		case op_KEYBUTTON:
		{
			keybutton_data *dt = wdata[2];
			*(char **)v = !dt->key ? NULL :
				key_name(dt->key, dt->mod, dt->section);
			break;
		}
		case op_COLOR:
			*(int *)v = cpick_get_colour(*wdata, NULL);
			break;
		case op_TCOLOR:
			*(int *)v = cpick_get_colour(*wdata, (int *)v + 1);
			break;
		case op_CSCROLL:
		{
			GtkAdjustment *xa, *ya;
			int *xp = v;
			get_scroll_adjustments(*wdata, &xa, &ya);
			xp[0] = gtk_adjustment_get_value(xa);
			xp[1] = gtk_adjustment_get_value(ya);
			break;
		}
		case op_RPACK: case op_RPACKD:
			*(int *)v = wj_radio_pack_get_active(*wdata);
			break;
		case op_OPT: case op_OPTD:
			*(int *)v = wj_option_menu_get_history(*wdata);
			break;
		case op_COMBO:
			*(int *)v = wj_combo_box_get_history(*wdata);
			break;
		case op_PCTCOMBO:
			*(int *)v = 0; // default for error
			sscanf(gtk_entry_get_text(
				GTK_ENTRY(pctcombo_entry(*wdata))), "%d%%",
				(int *)v);
			break;
		case op_COMBOENTRY:
			*(const char **)v = comboentry_get_text(*wdata);
			break;
		case op_ENTRY: case op_MLENTRY:
			*(const char **)v = gtk_entry_get_text(GTK_ENTRY(*wdata));
			break;
		case op_uENTRY: case op_uPATHSTR:
			*(char **)v = ((swdata *)*wdata)->strs;
			break;
		case op_PENTRY: case op_PATH:
			gtkncpy(v, gtk_entry_get_text(GTK_ENTRY(*wdata)),
				op != op_PATH ? (int)pp[1] : PATHBUF);
			break;
		case op_PATHs:
		{
			char path[PATHBUF];
			gtkncpy(path, gtk_entry_get_text(GTK_ENTRY(*wdata)), PATHBUF);
			inifile_set(v, path);
			break;
		}
		case op_TEXT:
			g_free(*(char **)v);
			*(char **)v = read_textarea(*wdata);
			break;
		case op_FONTSEL:
		{
			char **ss = v;
			g_free(ss[0]);
#if GTK_MAJOR_VERSION == 1
			ss[0] = NULL;
			if (gtk_font_selection_get_font(*wdata))
#endif
			ss[0] = gtk_font_selection_get_font_name(*wdata);
			*(const char **)(ss + 1) =
				gtk_font_selection_get_preview_text(*wdata);
			break;
		}
		case op_MOUNT:
			*(int *)v = !!gtk_bin_get_child(GTK_BIN(*wdata));
			break;
		case op_uMOUNT:
			*(int *)v = !!((swdata *)*wdata)->strs;
			break;
		default: v = NULL; break;
		}
		if (mode > 1) return (v);
	}
	return (NULL);
}

void run_query(void **wdata)
{
	v_dd *vdata = GET_VDATA(wdata);

	/* Prod a focused widget which needs defocusing to update */
	if (!vdata->script)
	{
		GtkWidget *w = GET_REAL_WINDOW(wdata);
		GtkWidget *f = gtk_window_get_focus(GTK_WINDOW(w));
		/* !!! No use to check if "fupslot" has focus - all dialogs with
		 * such widget get destroyed after query anyway */
		if (f && (GTK_IS_SPIN_BUTTON(f) || vdata->fupslot))
			gtk_window_set_focus(GTK_WINDOW(w), NULL);
	}

	do_query(GET_DDATA(wdata), GET_WINDOW(wdata), 0);
}

void run_destroy(void **wdata)
{
	v_dd *vdata = GET_VDATA(wdata);
	if (vdata->done) return; // already destroyed
	if (vdata->script) // simulated
	{
		do_destroy(wdata);
		free(GET_DDATA(wdata));
		return;
	}
	/* Work around WM misbehaviour, and save position & size if needed */
	cmd_showhide(GET_WINDOW(wdata), FALSE);
	gtk_widget_destroy(GET_REAL_WINDOW(wdata));
}

void cmd_reset(void **slot, void *ddata)
{
// !!! Support only what actually used on, and their brethren
	void *v, **pp, **wdata = slot;
	int op, opf, group = FALSE;

	for (; (pp = wdata[1]); wdata = NEXT_SLOT(wdata))
	{
		opf = op = (int)*pp++;
		v = WB_GETLEN(op) ? pp[0] : NULL;
		if (op & WB_FFLAG) v = (char *)ddata + (int)v;
		if (op & WB_NFLAG) v = *(void **)v; // dereference
		if (IS_UNREAL(wdata)) op = GET_UOP(wdata);
		op &= WB_OPMASK;
		switch (op)
		{
		case op_uOP:
			/* If GROUP, reset up to one w/o script flag */
			if ((opf & WB_OPMASK) == op_uOP)
				group = !group || (opf & WB_SFLAG);
			break;
		case op_SPINSLIDE: case op_SPINSLIDEa:
		case op_SPIN: case op_SPINc: case op_SPINa:
			gtk_spin_button_set_value(*wdata, *(int *)v);
			break;
		case op_TLSPINPACK:
		{
			void **vp = wdata[2];
			int i, n = (int)pp[1];

			*vp = NULL; // Lock
			for (i = 0; i < n; i++)
			{
				GtkSpinButton *spin = vp[i * 2 + 1];
				int *p = (int *)v + i * 3;

				gtk_spin_button_set_value(spin, *p);
				/* Value might get clamped, and slot is
				 * self-reading so should reflect that */
				*p = gtk_spin_button_get_value_as_int(spin);
			}
			*vp = wdata; // Unlock
			break;
		}
		case op_uSPIN: case op_uSPINa:
		{
			swdata *sd = *wdata;
			int n = *(int *)v;
			sd->value = n > sd->range[1] ? sd->range[1] :
				n < sd->range[0] ? sd->range[0] : n;
			break;
		}
		case op_TBTOGGLE: 
#if GTK_MAJOR_VERSION == 3 /* In GTK+1&2, same handler as for GtkToggleButton */
			gtk_toggle_tool_button_set_active(*wdata, *(int *)v);
			break;
#endif
		case op_CHECK: case op_CHECKb: case op_TOGGLE:
		case op_TBBOXTOG:
			gtk_toggle_button_set_active(*wdata, *(int *)v);
			break;
		case op_uCHECK: case op_uCHECKb:
			((swdata *)*wdata)->value = !!*(int *)v;
			break;
		case op_OPT:
			/* !!! No support for discontinuous lists, for now */
			wj_option_menu_set_history(*wdata, *(int *)v);
			break;
		case op_OPTD:
			opt_reset(wdata, ddata, *(int *)v);
			break;
		case op_uOPT: case op_uOPTD:
		{
			swdata *sd = *wdata;
			int n;

			if (op == op_uOPTD)
			{
				char **strs = sd->strs =
					*(char ***)((char *)ddata + (int)pp[1]);
				for (n = 0; strs[n]; n++); // Count strings
				sd->cnt = n;
			}

			n = *(int *)v;
			sd->value = (n < 0) || (n >= sd->cnt) ||
				!((char **)sd->strs)[n][0] ? 0 : n;
			break;
		}
		case op_LISTCCr:
			listcc_reset(wdata, -1);
#ifdef U_LISTS_GTK1
			/* !!! Or the changes will be ignored if the list wasn't
			 * yet displayed (as in inactive dock tab) - WJ */
			gtk_widget_queue_resize(*wdata);
#endif
			break;
		/* op_uLISTCC needs no resetting */
		case op_LISTC: case op_LISTCd: case op_LISTCu:
		case op_LISTCS: case op_LISTCX:
			listc_reset(*wdata, wdata[2]);
			break;
		case op_uLISTC:
			ulistc_reset(wdata);
			break;
		case op_CSCROLL:
		{
			GtkAdjustment *xa, *ya;
			int *xp = v;
			get_scroll_adjustments(*wdata, &xa, &ya);
#if GTK_MAJOR_VERSION == 3
			{
				/* This is to change both before triggering any */
				guint id = g_signal_lookup("value_changed",
					G_TYPE_FROM_INSTANCE(xa));
				g_signal_handlers_block_matched(G_OBJECT(xa),
					G_SIGNAL_MATCH_ID, id, 0, NULL, NULL, NULL);
				g_signal_handlers_block_matched(G_OBJECT(ya),
					G_SIGNAL_MATCH_ID, id, 0, NULL, NULL, NULL);
				gtk_adjustment_set_value(xa, xp[0]);
				gtk_adjustment_set_value(ya, xp[1]);
				g_signal_handlers_unblock_matched(G_OBJECT(xa),
					G_SIGNAL_MATCH_ID, id, 0, NULL, NULL, NULL);
				g_signal_handlers_unblock_matched(G_OBJECT(ya),
					G_SIGNAL_MATCH_ID, id, 0, NULL, NULL, NULL);
			}
#else /* #if GTK_MAJOR_VERSION <= 2 */
			xa->value = xp[0];
			ya->value = xp[1];
#endif
			gtk_adjustment_value_changed(xa);
			gtk_adjustment_value_changed(ya);
			break;
		}
		case op_COMBOENTRY:
			comboentry_reset(*wdata, v, *(char ***)(ddata + (int)pp[1]));
			break;
		case op_ENTRY: case op_MLENTRY:
			gtk_entry_set_text(*wdata, *(char **)v);
			// Replace transient buffer - it may get freed on return
			*(const char **)v = gtk_entry_get_text(*wdata);
			break;
		case op_PATHs:
			v = inifile_get(v, ""); // read and fallthrough
		case op_PENTRY: case op_PATH:
			set_path(*wdata, v, PATH_VALUE);
			break;
		case op_RGBIMAGEP:
		{
			rgbimage_data *rd = wdata[2];
			rd->rgb = v; // Size is fixed, but update source
			if (GTK_WIDGET_REALIZED(*wdata)) reset_rgbp(*wdata, rd);
			break;
		}
		case op_CANVASIMGB:
		{
			rgbimage_data *rd = wdata[2];
			int nw, *xp = (int *)(ddata + (int)pp[1]);

			nw = (rd->w ^ xp[0]) | (rd->h ^ xp[1]);
			rd->rgb = v;
			rd->w = xp[0];
			rd->h = xp[1];
			rd->bkg = xp[2];
			if (nw) wjcanvas_size(*wdata, xp[0], xp[1]);
			wjcanvas_uncache(*wdata, NULL);
			gtk_widget_queue_draw(*wdata);
			break;
		}
		case op_FCIMAGEP:
		{
			fcimage_data *fd = wdata[2];
			fd->rgb = v; // Update source, leave other parts be
			if (GTK_WIDGET_REALIZED(*wdata)) reset_fcimage(*wdata, fd);
			break;
		}
		case op_KEYMAP:
			keymap_reset(wdata[2]);
#if GTK_MAJOR_VERSION >= 2
			gtk_signal_emit_by_name(GTK_OBJECT(gtk_widget_get_toplevel(
				GET_REAL_WINDOW(wdata_slot(wdata)))),
				"keys_changed", NULL);
#endif
			break;
#if 0 /* Not needed for now */
		case op_FPICKpm:
			fpick_set_filename(*wdata, v, FALSE);
			break;
		case op_FSPIN:
			gtk_spin_button_set_value(*wdata, *(int *)v * 0.01);
			break;
		case op_uENTRY: case op_uPATHSTR:
			// Replace transient buffer - it may get freed on return
			*(char **)v = set_uentry(*wdata, *(char **)v);
			cmd_event(wdata, op_EVT_CHANGE);
			break;
		case op_TBRBUTTON:
			if (*(int *)v == TOOL_ID(wdata))
#if GTK_MAJOR_VERSION == 3
				gtk_toggle_tool_button_set_active(*wdata, TRUE);
#else
				gtk_toggle_button_set_active(*wdata, TRUE);
#endif
			break;
		case op_MENURITEM:
			if (*(int *)v != TOOL_ID(wdata)) break;
			// Fallthrough
		case op_MENUCHECK:
			gtk_check_menu_item_set_active(*wdata,
				op == op_MENURITEM ? TRUE : *(int *)v);
			break;
		case op_PCTCOMBO:
			/* Same as in cmd_set() */
			break;
		case op_RPACK: case op_RPACKD:
		case op_COLORLIST: case op_COLORLISTN:
// !!! No ready setter functions for these (and no need of them yet)
			break;
		case op_COLOR:
			cpick_set_colour(*wdata, *(int *)v, 255);
			break;
		case op_TCOLOR:
			cpick_set_colour(*wdata, ((int *)v)[0], ((int *)v)[1]);
			break;
		case op_RGBIMAGE:
		{
			rgbimage_data *rd = wdata[2];
			int *wh = (int *)(ddata + (int)pp[1]);
			rd->rgb = v;
			rd->w = wh[0];
			rd->h = wh[1];
			cmd_repaint(wdata);
			break;
		}
#endif
		}
		if (!group) return;
	}
}

void cmd_sensitive(void **slot, int state)
{
	int op;

	if (IS_UNREAL(slot))
	{
// !!! COLUMNs should redirect to their list slot instead
		((swdata *)slot[0])->insens = !state;
		return;
	}
	op = GET_OP(slot);
	if (op >= op_EVT_0) return; // only widgets
	gtk_widget_set_sensitive(get_wrap(slot), state);
}

static int midmatch(const char *s, const char *v, int l)
{
	while ((s = strchr(s, ' ')))
	{
		s += strspn(s, " (");
		if (!strncasecmp(s, v, l)) break;
	}
	return (!!s);
}

static int find_string(swdata *sd, char *s, int l, int column);

void **find_slot(void **slot, char *id, int l, int mlevel)
{
	void **start = slot, **where = NULL;
	char buf[64], *nm, *ts;
	int op, n, p = INT_MAX;

	for (; slot[1]; slot = NEXT_SLOT(slot))
	{
		op = GET_OP(slot);
		if (op == op_uOPNAME) break; // ENDSCRIPT marker
		if (mlevel >= 0) // Searching menu items
		{
			/* Reading the descriptors, so ignore unreal state */
// !!! Or maybe switch/case?
			if ((op == op_uALTNAME) || (op == op_uMENUITEM))
				nm = ((swdata *)slot[0])->id;
			else if ((op == op_SUBMENU) || (op == op_ESUBMENU) ||
				(op == op_SSUBMENU))
			{
				nm = GET_DESCV(slot, 1);
				// Remove shortcut marker from toplevel items
				if ((ts = strchr(nm, '_'))) nm = wjstrcat(buf,
					sizeof(buf), nm, ts - nm, ts + 1, NULL);
			}
			else if ((op == op_MENUITEM) || (op == op_MENUCHECK) ||
				(op == op_MENURITEM)) nm = GET_DESCV(slot, 3);
			else continue;
			if (mlevel) // Searching a sublevel
			{
				n = strspn(nm, "/");
				if ((n < mlevel) && (op != op_uALTNAME))
					break; // level end
				else if (n != mlevel)
					continue; // submenu or direct altname
				nm += n;
			}
		}
		else // Searching pseudo widgets
		{
			if (!IS_UNREAL(slot)) continue;
			if (GET_UOP(slot) == op_uOP)
			{
				// Ignore dummy slots
				if (op != op_uOP) continue;
				// Skip groups in flat search
				if (mlevel == MLEVEL_FLAT) continue;
				// Stop on new group in block search
				if (mlevel == MLEVEL_BLOCK) break;
			}
			// Ignore anything but groups in group search
			else if (mlevel == MLEVEL_GROUP) continue;
			nm = ((swdata *)slot[0])->id;
			if (!nm) continue; // No name
		}
		/* Match empty option name to an empty ID (default) */
		if (!l)
		{
			if (nm[0]) continue;
			where = slot;
			break;
		}
		/* In a flattened widget, match contents */
		if ((nm[0] == ':') && !nm[1] && (op == op_uALTNAME))
		{
			void **w = origin_slot(slot);
			if (!IS_UNREAL(w)) continue;
			n = GET_UOP(w);
			// Allow only static lists for now
			if ((n != op_uOPT) && (n != op_uRPACK)) continue;
			n = find_string(w[0], id, l, FALSE);
			if (n < 0) continue;
			// Use matched string as widget name
			nm = ((char **)((swdata *)w[0])->strs)[n];
		}
		/* Match at beginning, preferring shortest word */
		if (!strncasecmp(nm, id, l))
		{
			int d = l + strcspn(nm + l, " .,()");
			// Prefer if no other words
			d = d + d + !!nm[d + strcspn(nm + d, " .,()")];
			if (d >= p) continue;
			p = d;
			where = slot;
		}
		else if (where);
		/* Match at word beginning */
		else if (mlevel && midmatch(nm, id, l)) where = slot;
	}
	/* Resolve alternative name */
	if (where && (GET_OP(where) == op_uALTNAME))
	{
		nm = ((swdata *)where[0])->id;
		if ((nm[0] != ':') || nm[1]) // Leave flattening markers be
			where = origin_slot(where);
	}
	/* Try in-group searching */
	if (!where && (mlevel == MLEVEL_FLAT) && (ts = memchr(id, '/', l)) &&
		(ts != id) && (id + l - ts > 1))
	{
		where = find_slot(start, id, ts - id, MLEVEL_GROUP);
		if (where) where = find_slot(NEXT_SLOT(where), ts + 1,
			id + l - ts - 1, MLEVEL_BLOCK);
	}
	return (where);
}

/* Match string to list column of strings, or to widget's string list */
static int find_string(swdata *sd, char *s, int l, int column)
{
	char *tmp;
	col_data *c = NULL;
	int i, ll, p = INT_MAX, n = sd->cnt;

	if (!s || !l) return (-1); // Error
	if (column)
	{
		c = sd->strs;
		if (!c) return (-1); // Not linked
		column = n;
		/* List length */
		n = *(int *)(c->ddata + (int)GET_DESCV(c->r, 2));
	}

	for (ll = -1 , i = 0; i < n; i++)
	{
		tmp = c ? get_cell(c, i, column) : ((char **)sd->strs)[i];
		if (!tmp[0]) continue;
		/* Match at beginning */
		if (!strncasecmp(tmp, s, l))
		{
			int k = strlen(tmp);
			if (k >= p) continue;
			p = k;
		}
		else if (ll >= 0) continue;
		/* Match at word beginning */
		else if (!midmatch(tmp, s, l)) continue;
		ll = i;
	}
	return (ll);
}

/* Resolve parameter chaining */
static char **unchain_p(char **strs)
{
	while (*strs == (void *)strs) strs = (void *)strs[1];
	return (strs);
}

/* Parse a parenthesized list of tuples into an array of ints
 * List may continue in next script positions */
static multi_ext *multi_parse(char *s0)
{
	multi_ext *mx;
	char c, *ss, *tmp, **strs;
	int w = 0, mw = INT_MAX, fp = -1;
	int n, l, cnt, sc, err, *ix, **rows;

	/* Crudely count the parts */
	if (s0[0] != '(') return (NULL); // Wrong
	ss = s0 + 1;
	cnt = sc = 0;
	strs = script_cmds;
	while (TRUE)
	{
		c = *ss++;
		if (c == ')') break;
		if (c == ',') cnt++;
		if (!c)
		{
			strs = unchain_p(strs);
			ss = *strs++;
			if (!ss) return (NULL); // Unterminated
			sc++;
		}
	}

	/* Allocate */
	mx = calloc(1, sizeof(multi_ext) + sizeof(int *) * (sc + 2 - 1) +
		sizeof(int) * (cnt + sc * 2 + 2 + 1));
	if (!mx) return (NULL);
	ix = (void *)(mx->rows + sc + 2);
	// Extremely unlikely, but why not make sure
	if (ALIGNOF(int) > ALIGNOF(int *)) ix = ALIGNED(ix, ALIGNOF(int));

	/* Parse & fill */
	sc = l = err = 0;
	rows = mx->rows;
	strs = script_cmds;
	for (ss = s0 + 1; ss; strs = unchain_p(strs) , ss = *strs++)
	{
		if (!ss[0]) continue; // Empty
		if ((ss[0] == ')') && !ss[1]) break; // Lone ')'
		rows[l++] = ix++;
		n = 0; err = 1;
		while (TRUE)
		{
			ix[n] = strtol(ss, &tmp, 10);
			if (tmp == ss) break; // Error
			if (*tmp == '.') // Maybe floating - use fixedpoint
			{
				// !!! Only one fixedpoint column allowed for now
				if ((fp >= 0) && (fp != n)) break;

				ix[fp = n] = (int)(g_strtod(ss, &tmp) *
					MAX_PRESSURE + 0.5);
			}
			n++;
			ss = tmp + 1;
			if (*tmp == ',') continue;
			if (*tmp && ((*tmp != ')') || tmp[1])) break; // Error
			err = 0; // Valid tuple
			*(ix - 1) = n; // Store count
			ix += n; // Skip over
			if (w < n) w = n; // Update max
			if (mw > n) mw = n; // Update min
			break;
		}
		if (err || *tmp) break;
	}
	if (err || !ss || (l <= 0))
	{
		free(mx);
		return (NULL);
	}

	/* Finalize */
	script_cmds = strs;

	mx->nrows = l;
	mx->ncols = w;
	mx->mincols = mw;
	mx->fractcol = fp;
	return (mx);
}

int cmd_setstr(void **slot, char *s)
{
	void *v;
	char *tmp, *st = NULL;
	int op, ll = 0, res = 1;

	/* If see a list and prepared to handle it, do so */
	if (s && (s[0] == '(') && (v = op_slot(slot, op_EVT_MULTI))) slot = v;

	op = GET_OP(slot);
	if (IS_UNREAL(slot)) op = GET_UOP(slot);
	switch (op)
	{
	case op_uFPICK: case op_uPATHSTR:
		if (!s) s = "";
		// Commandline is already in system encoding
		if (!cmd_mode) s = st = gtkncpy(NULL, s, PATHBUF);
		cmd_setv(slot, s, op == op_uFPICK ? FPICK_VALUE : ENTRY_VALUE);
		g_free(st);
		goto done;
	case op_ENTRY: case op_uENTRY:
		if (!s) s = "";
		// Commandline is in system encoding
		if (cmd_mode) s = st = gtkuncpy(NULL, s, 0);
		cmd_setv(slot, s, ENTRY_VALUE);
		g_free(st);
		goto done;
	case op_CHECK: case op_CHECKb: case op_TOGGLE:
	case op_TBTOGGLE: case op_TBBOXTOG: case op_TBRBUTTON:
	case op_MENUCHECK: case op_MENURITEM:
	case op_uCHECK: case op_uCHECKb:
	case op_uMENUCHECK: case op_uMENURITEM:
		ll = !s ? TRUE : !s[0] ? FALSE : str2bool(s);
		if (ll < 0) return (-1); // Error
		break;
	case op_TXTCOLUMN: case op_XTXTCOLUMN:
		ll = TRUE;
		// Fallthrough
	case op_uOPT: case op_uOPTD: case op_uRPACK: case op_uRPACKD:
		if (!s) return (-1); // Error
		ll = find_string(slot[0], s, strlen(s), ll);
		if (ll < 0) return (-1); // Error
		break;
	case op_BUTTON: case op_TBBUTTON: case op_uBUTTON:
	case op_MENUITEM: case op_uMENUITEM:
		ll = res = 0; // No use for parameter
		break;
	case op_FSPIN: case op_uFSPIN: 
		if (s && s[0])
		{ 
			double a = g_strtod(s, &tmp);
			if (*tmp) return (-1); // Error
			ll = rint(a * 100);
			break;
		}
		// Fallthrough
	case op_SPIN: case op_SPINc: case op_SPINa:
	case op_SPINSLIDE: case op_SPINSLIDEa:
	case op_uSPIN: case op_uSPINa:
	case op_LISTCCr: case op_uLISTCC: case op_uLISTC: case op_IDXCOLUMN:
		if (!s) return (-1); // Error
		ll = 0; // Default
		if (s[0])
		{ 
			ll = strtol(s, &tmp, 10);
			if (*tmp) return (-1); // Error
		}
		break;
	case op_uCOLOR:
		ll = parse_color(s);
		if (ll < 0) return (-1); // Error
		break;
	case op_uSCALE:
	{
		swdata *sd = slot[0];

		if (!s || !s[0]) return (-1); // Error
		if (strchr(s, '%')) /* "w=125%" */
		{
			ll = strtol(s, &tmp, 10);
			if (*tmp != '%') return (-1); // Error
			ll = (ll * (int)sd->strs) / 100;
		}
		else if ((s[0] == 'x') || (s[0] == 'X')) /* "w=x1.25" */
		{
			double a = g_strtod(s + 1, &tmp);
			if (*tmp) return (-1); // Error
			ll = rint((int)sd->strs * a);
		}
		else /* "w=200" */
		{ 
			ll = strtol(s, &tmp, 10);
			if (*tmp) return (-1); // Error
		}
		sd->cnt = 3; // Value is being set directly
		break;
	}
	case op_EVT_MULTI:
		if ((v = multi_parse(s)))
		{
			void **base = slot[0], **desc = slot[1];
			res = ((evtxr_fn)desc[1])(GET_DDATA(base), base,
				(int)desc[0] & WB_OPMASK, slot, v);
			if (res >= 0) free(v); // Handler can ask to keep it
			if (res) return (1);
		}
		// !!! Fallthrough to fail
	default: return (-1); // Error: cannot handle
	}
	/* From column to list */
	if ((op >= op_COLUMN_0) && (op < op_COLUMN_LAST))
		slot = ((col_data *)((swdata *)slot[0])->strs)->r;
	/* Set value to widget */
	cmd_set(slot, ll);
done:	cmd_event(slot, op_EVT_SCRIPT); // Notify
	return (res);
}

static int tbar_event(void **slot, int what)
{
	void **base, **desc, **tbar = NULL;
	int op = GET_OP(slot);
	if (IS_UNREAL(slot)) op = GET_UOP(slot);
	switch (op)
	{
	case op_TBBUTTON: case op_TBTOGGLE: case op_TBRBUTTON: case op_TBBOXTOG:
	case op_MENUITEM: case op_MENUCHECK: case op_MENURITEM:
		tbar = slot[2];
		break;
	case op_uMENUITEM: case op_uMENUCHECK: case op_uMENURITEM:
		tbar = ((swdata *)slot[0])->strs;
		break;
	}
	if (tbar) tbar = op_slot(tbar, what);
	if (!tbar || ((what == op_EVT_CLICK) && (WB_GETLEN(GET_OPF(slot)) < 5)))
		return (-1); // Fail
	/* Call event at saved slot, for this slot */
	base = tbar[0]; desc = tbar[1];
	((evt_fn)desc[1])(GET_DDATA(base), base, (int)desc[0] & WB_OPMASK, slot);
	return (0);
}

int cmd_run_script(void **slot, char **strs)
{
	char *opt, *tmp, **err = NULL;
	void **wdata;
	int op, ll, maybe;

	/* Resolve slot */
	op = GET_OP(slot);
	if (IS_UNREAL(slot)) op = GET_UOP(slot);
	if (op == op_uMOUNT) slot = ((swdata *)*slot)->strs;
	else if (op == op_MOUNT) slot = slot[2];
	if (!slot) return (1); // Paranoia

	/* Step through options */
	while (strs = unchain_p(strs) , opt = *strs)
	{
		/* Stop on commands and empty strings */
		if (!opt[0] || (opt[0] == '-')) break;
		strs++;
		/* Stop on dialog close */
		if ((opt[0] == ':') && !opt[1]) break;
		/* Have option: first, parse it */
		opt += (maybe = opt[0] == '.'); // Optional if preceded by "."
		// Expect "(list)", "name=value", or "name:"
		ll = opt[0] == '(' ? 0 : strcspn(opt, "=:");
		/* Now, find target for the option */
		wdata = find_slot(slot, opt, ll, MLEVEL_FLAT);
		/* Raise an error if no match */
		if (!wdata)
		{
			if (maybe) continue; // Ignore optional options
			err = strs;
			break;
		}
		/* For flattened lists, uALTNAME gets returned */
		op = GET_OP(wdata);
		wdata = origin_slot(wdata);
		/* Leave insensitive slots alone */
		if (!cmd_checkv(wdata, SLOT_SENSITIVE)) continue;
// !!! Or maybe raise an error, too?
		script_cmds = strs; // For nested dialog
		/* Set value to flattened list */
		if (op == op_uALTNAME)
		{
			ll = find_string(wdata[0], opt, ll, FALSE); // Cannot fail
			cmd_set(wdata, ll);
			cmd_event(wdata, op_EVT_SCRIPT); // Notify
		}
		/* Activate right-click handler */
		else if (opt[ll] == ':') ll = tbar_event(wdata, op_EVT_CLICK);
		/* Set value to slot */
		else ll = cmd_setstr(wdata, !opt[ll] ? NULL :
			opt + ll + (opt[ll] == '='));
		/* Raise an error if invalid value */
		if (ll < 0)
		{
			err = strs;
			break;
		}
		strs = script_cmds; // Nested dialogs can consume options
		if (user_break) break;
	}
	script_cmds = strs; // Stopped at here

	/* An error happened - report it */
	if (err)
	{
		err--; // Previous option caused the error
		/* Find name-bearing slot */
		slot = wdata;
		if (slot && !IS_UNREAL(slot)) slot = op_slot(wdata, op_uALTNAME);
		tmp = g_strdup_printf(!wdata ? _("'%s' does not match any widget") :
			_("'%s' value does not fit '%s' widget"), *err,
			slot ? ((swdata *)slot[0])->id : NULL);
		alert_box(_("Error"), tmp, NULL);
		g_free(tmp);
		return (-1);
	}

	/* Now activate the OK handler if any */
	for (wdata = slot; wdata[1]; wdata = NEXT_SLOT(wdata))
		if (IS_UNREAL(wdata) && ((GET_UOP(wdata) == op_uOKBTN) ||
			(GET_UOP(wdata) == op_uFPICK))) break;
	if (wdata[1])
	{
		cmd_event(wdata, op_EVT_OK);
		/* !!! The memory block is likely to be freed at this point */
		return (0);
	}

	return (1);
}

void cmd_showhide(void **slot, int state)
{
	GtkWidget *wrap, *ws = NULL;
	void **keymap = NULL;
	int raise = FALSE, unfocus = FALSE, mx = FALSE;

	if (GET_OP(slot) == op_WDONE) slot = NEXT_SLOT(slot); // skip head noop
	if (IS_UNREAL(slot))
	{
		if ((GET_OP(PREV_SLOT(slot)) == op_WDONE) && state)
		{
			v_dd *vdata = GET_VDATA(PREV_SLOT(slot));
			/* Script is run when the window is first displayed */
			if (vdata->run) return; // Redisplay
			vdata->run = TRUE;
			if ((cmd_run_script(slot, vdata->script) < 0) &&
				(GET_UOP(slot) == op_uWINDOW))
				run_destroy(PREV_SLOT(slot)); // On error
		}
		return;
	}
	if (GET_OP(slot) >= op_EVT_0) return; // only widgets
	wrap = get_wrap(slot); // For *some* pkf_PARENT widgets, hide/show their parent
	if (!GTK_WIDGET_VISIBLE(wrap) ^ !!state) return; // no use
	if (GET_OP(PREV_SLOT(slot)) == op_WDONE) // toplevels are special
	{
		v_dd *vdata = GET_VDATA(PREV_SLOT(slot));
		GtkWidget *w = GTK_WIDGET(slot[0]);

		if (state) // show - apply stored size, position, raise, unfocus
		{
			gtk_window_set_transient_for(GTK_WINDOW(w), vdata->tparent);
#if GTK_MAJOR_VERSION == 3
			if (vdata->ininame) gtk_window_move(GTK_WINDOW(w),
#else
			if (vdata->ininame) gtk_widget_set_uposition(w,
#endif
				vdata->xywh[0], vdata->xywh[1]);
			else vdata->ininame = ""; // first time
			gtk_window_set_default_size(GTK_WINDOW(w),
				vdata->xywh[2] ? vdata->xywh[2] : -1,
				vdata->xywh[3] ? vdata->xywh[3] : -1);
			if (vdata->modal) gtk_window_set_modal(GTK_WINDOW(w), TRUE);
			/* Prepare to do postponed actions */
			mx = vdata->xywh[4];
#if GTK_MAJOR_VERSION >= 2
			/* !!! When doing maximize, we have to display window
			 * contents after window itself, or some widgets may get
			 * locked into wrong size by premature first-time size
			 * request & allocation within unmaximized window.
			 * On GTK+1 the resize mechanism is quite different and
			 * such reordering only makes the result even worse; what
			 * can cause proper resizing there, isn't found yet - WJ */
			if (mx)
			{
				ws = gtk_bin_get_child(GTK_BIN(w));
				if (ws && GTK_WIDGET_VISIBLE(ws))
					widget_showhide(ws, FALSE);
				else ws = NULL;
			}
#endif
			keymap = vdata->keymap;
			raise = vdata->raise;
			unfocus = vdata->unfocus;
			vdata->raise = vdata->unfocus = FALSE;
		}
		else // hide - remember size & position
		{
			/* !!! These reads also do gdk_flush() which, followed by
			 * set_transient(NULL), KDE somehow needs for restoring
			 * focus from fileselector back to pref window - WJ */
			if (!(vdata->xywh[4] = is_maximized(w)))
			{
#if GTK_MAJOR_VERSION == 3
				gtk_window_get_size(GTK_WINDOW(w),
#else
				gdk_window_get_size(w->window,
#endif
					vdata->xywh + 2, vdata->xywh + 3);
				gdk_window_get_root_origin(gtk_widget_get_window(w),
					vdata->xywh + 0, vdata->xywh + 1);
			}
			if (vdata->ininame && vdata->ininame[0])
				rw_pos(vdata, TRUE);
			/* Needed for "dialogparent" effect in KDE4 to not misbehave */
			if (vdata->modal) gtk_window_set_modal(GTK_WINDOW(w), FALSE);
			/* Needed in Windows to stop GTK+ lowering the main window,
			 * and everywhere to avoid forcing focus to main window */
			gtk_window_set_transient_for(GTK_WINDOW(w), NULL);
		}
	}
	widget_showhide(wrap, state);
	/* !!! Window must be visible, or maximize fails if either dimension is
	 * already at max, with KDE3 & 4 at least - WJ */
	if (mx) set_maximized(slot[0]);
	if (ws) widget_showhide(ws, TRUE);
	if (raise) gdk_window_raise(gtk_widget_get_window(GTK_WIDGET(slot[0])));
	if (unfocus) gtk_window_set_focus(slot[0], NULL);
	/* !!! Have to wait till canvas is displayed, to init keyboard */
	if (keymap) keymap_reset(keymap[2]);
}

void cmd_set(void **slot, int v)
{
	swdata *sd;
	int op;

	slot = origin_slot(slot);
	op = GET_OP(slot);
	if (IS_UNREAL(slot)) op = GET_UOP(slot);
	sd = slot[0];
// !!! Support only what actually used on, and their brethren
	switch (op)
	{
	case op_DOCK:
	{
		dock_data *dd = slot[2];
		GtkWidget *window, *vbox = dd->vbox, *pane = dd->pane;
		char *ini = ((void **)slot[1])[1];
		int w, w2;

		if (!v ^ !!gtk_paned_get_child1(GTK_PANED(pane))) return; // nothing to do

		window = gtk_widget_get_toplevel(slot[0]);
#if GTK_MAJOR_VERSION == 3
		if (gtk_widget_get_visible(window))
			gtk_window_get_size(GTK_WINDOW(window), &w2, NULL);
		/* Window size isn't yet valid */
		else g_object_get(G_OBJECT(window), "default_width", &w2, NULL);
#else
		if (GTK_WIDGET_VISIBLE(window))
			gdk_window_get_size(window->window, &w2, NULL);
		/* Window size isn't yet valid */
		else gtk_object_get(GTK_OBJECT(window), "default_width", &w2, NULL);
#endif

		if (v)
		{
			/* Restore dock size if set, autodetect otherwise */
			w = inifile_get_gint32(ini, -1);
			if (w >= 0) gtk_paned_set_position(GTK_PANED(pane), w2 - w);
			/* Now, let's juggle the widgets */
			gtk_widget_ref(vbox);
			gtk_container_remove(GTK_CONTAINER(slot[0]), vbox);
			gtk_paned_pack1(GTK_PANED(pane), vbox, TRUE, TRUE);
			gtk_widget_show(pane);
		}
		else
		{
			inifile_set_gint32(ini, w2 - gtk_paned_get_position(
				GTK_PANED(pane)));
			gtk_widget_hide(pane);
//			vbox = gtk_paned_get_child1(GTK_PANED(pane));
			gtk_widget_ref(vbox);
			gtk_container_remove(GTK_CONTAINER(pane), vbox);
			xpack(slot[0], vbox);
		}
		gtk_widget_unref(vbox);
		break;
	}
	case op_HVSPLIT:
	{
		hvsplit_data *hd = slot[2];
		GtkWidget *pane, **p = hd->panes, *w = NULL, *box = slot[0];
		int v0, v1;

		v0 = GTK_WIDGET_VISIBLE(p[0]) ? 1 :
			GTK_WIDGET_VISIBLE(p[1]) ? 2 : 0;
		v1 = (int)v < 1 ? 0 : (int)v > 1 ? 2 : 1;
		if (v1 == v0) break; // nothing to do
		if (!v1) // hide 2nd part
		{
			pane = p[v0 - 1];
			gtk_widget_hide(pane);
			w = gtk_paned_get_child1(GTK_PANED(pane));
			gtk_widget_ref(w);
			gtk_container_remove(GTK_CONTAINER(pane), w);
			xpack(box, w);
		}
		else if (!v0) // show 2nd part
		{
			pane = p[v1 - 1];
			if (!gtk_paned_get_child2(GTK_PANED(pane))) // move
			{
				w = hd->inbox[1];
				gtk_widget_ref(w);
				gtk_container_remove(GTK_CONTAINER(
					gtk_widget_get_parent(w)), w);
				gtk_paned_pack2(GTK_PANED(pane), w, TRUE, TRUE);
				gtk_widget_unref(w);
				gtk_widget_show(w);
			}
			w = hd->inbox[0];
			gtk_widget_ref(w);
			gtk_container_remove(GTK_CONTAINER(box), w);
			gtk_paned_pack1(GTK_PANED(pane), w, TRUE, TRUE);
			gtk_widget_show(pane);
		}
		else // swap direction
		{
			pane = p[v0 - 1];
			gtk_widget_hide(pane);
			w = gtk_paned_get_child1(GTK_PANED(pane));
			gtk_widget_ref(w);
			gtk_container_remove(GTK_CONTAINER(pane), w);
			gtk_paned_pack1(GTK_PANED(p[2 - v0]), w, TRUE, TRUE);
			gtk_widget_unref(w);
			w = gtk_paned_get_child2(GTK_PANED(pane));
			gtk_widget_ref(w);
			gtk_container_remove(GTK_CONTAINER(pane), w);
			gtk_paned_pack2(GTK_PANED(p[2 - v0]), w, TRUE, TRUE);
			gtk_widget_show(p[2 - v0]);
		}
		gtk_widget_unref(w);
		break;
	}
	case op_NOSPIN:
		spin_set_range(slot[0], v, v);
		break;
	case op_SPINSLIDE: case op_SPINSLIDEa:
	case op_SPIN: case op_SPINc: case op_SPINa:
		gtk_spin_button_set_value(slot[0], v);
		break;
	case op_uSCALE:
		/* uSCALE ignores indirect writes after a direct one */
		if (sd->cnt == 2) break;
		sd->cnt &= 2;
		// Fallthrough
	case op_uSPIN: case op_uFSPIN: case op_uSPINa:
	case op_uCHECK: case op_uCHECKb:
		v = (op == op_uCHECK) || (op == op_uCHECKb) ? !!v :
			v > sd->range[1] ? sd->range[1] :
			v < sd->range[0] ? sd->range[0] : v;
		if (v == sd->value) break;
		// Fallthrough
	case op_uCOLOR:
		sd->value = v;
		cmd_event(slot, op_EVT_CHANGE);
		break;
	case op_uOPT: case op_uOPTD: case op_uRPACK: case op_uRPACKD:
		if ((v < 0) || (v >= sd->cnt) || !((char **)sd->strs)[v][0]) v = 0;
		if (v == sd->value) break;
		sd->value = v;
		cmd_event(slot, op_EVT_SELECT);
		break;
	case op_BUTTON: case op_uBUTTON:
		cmd_event(slot, op_EVT_CLICK); // for any value
		break;
	case op_TBBUTTON:
#if GTK_MAJOR_VERSION == 3
		g_signal_emit_by_name(slot[0], "clicked"); // for any value
#else
		gtk_button_clicked(slot[0]); // for any value
#endif
		break;
	case op_uMENURITEM:
		// Cannot unset itself, and no use to set twice
		if (!v || sd->value) break;
		/* Unset the whole group */
		{
			void **r = slot;
			int n;

			while ((n = ((swdata *)r[0])->range[0])) r -= n;
			while (TRUE)
			{
				((swdata *)r[0])->value = FALSE;
				if (!(n = ((swdata *)r[0])->range[1])) break;
				r += n;
			}
		}
		// Fallthrough
	case op_uMENUCHECK:
		v = !!v;
		if (sd->value == v) break;
		sd->value = v;
		// Fallthrough
	case op_uMENUITEM:
		tbar_event(slot, op_EVT_CHANGE);
		break;
	case op_FSPIN:
		gtk_spin_button_set_value(slot[0], v / 100.0);
		break;
	case op_TBTOGGLE: case op_TBRBUTTON:
#if GTK_MAJOR_VERSION == 3 /* In GTK+1&2, same handler as for GtkToggleButton */
		gtk_toggle_tool_button_set_active(slot[0], v);
		break;
#endif
	case op_CHECK: case op_CHECKb: case op_TOGGLE:
	case op_TBBOXTOG:
		gtk_toggle_button_set_active(slot[0], v);
		break;
	case op_MENUITEM:
		gtk_menu_item_activate(slot[0]); // for any value
		break;
	case op_MENUCHECK: case op_MENURITEM:
		gtk_check_menu_item_set_active(slot[0], v);
		break;
	case op_OPT: case op_OPTD:
		/* !!! No support for discontinuous lists, for now */
		wj_option_menu_set_history(slot[0], v);
		break;
	case op_PCTCOMBO:
	{
		char buf[32];

		sprintf(buf, "%d%%", v);
		gtk_entry_set_text(GTK_ENTRY(pctcombo_entry(slot[0])), buf);
#if GTK_MAJOR_VERSION == 1
		/* Call the handler, for consistency */
		get_evt_1(NULL, NEXT_SLOT(slot));
#endif
		break;
	}
#ifdef U_CPICK_MTPAINT
	case op_HEXENTRY:
		*(int *)slot_data(slot, GET_DDATA(wdata_slot(NEXT_SLOT(slot)))) = v;
		set_hexentry(slot[0], v);
		break;
#endif
	case op_PLAINBOOK:
		gtk_notebook_set_page(slot[0], v);
		break;
	case op_LISTCCr:
	{
		listcc_data *dt = slot[2];
		if ((v < 0) || (v >= *dt->cnt)) break; // Ensure sanity
		*dt->idx = v;
		listcc_select_item(slot);
		break;
	}
	case op_uLISTCC: case op_uLISTC:
	{
		char *ddata = GET_DDATA(wdata_slot(slot));
		int *cnt = (void *)(ddata + (int)GET_DESCV(slot, 2));

		if ((v < 0) || (v >= *cnt)) break; // Ensure sanity
		*(int *)(sd->strs) = v;
		cmd_event(slot, op_EVT_SELECT);
		break;
	}
	case op_LISTC: case op_LISTCd: case op_LISTCu:
	case op_LISTCS: case op_LISTCX:
		listc_select_index(slot[0], v);
		break;
	}
}

/* Passively query one slot, show where the result went */
void *cmd_read(void **slot, void *ddata)
{
	return (do_query(ddata, origin_slot(slot), 3));
}

void cmd_peekv(void **slot, void *res, int size, int idx)
{
// !!! Support only what actually used on
	int op = GET_OP(slot);
	if (size <= 0) return;
	if (op == op_WDONE)
	{
		if (idx == WDATA_TABLET)
		{
			if (size >= sizeof(char *)) *(char **)res = tablet_device ?
				(char *)gdk_device_get_name(tablet_device) : NULL;
			return;
		}
		// skip to toplevel slot
		slot = NEXT_SLOT(slot) , op = GET_OP(slot);
	}
	if (IS_UNREAL(slot)) op = GET_UOP(slot);
	switch (op)
	{
	case op_uWINDOW:
	case op_MAINWINDOW: case op_WINDOW: case op_WINDOWm: case op_DIALOGm:
		if ((idx == WINDOW_DPI) && (size >= sizeof(int)))
		{
			GtkWidget *w = op == op_uWINDOW ? main_window : slot[0];
			*(int *)res = cmd_mode ? 72 : // Use FreeType's default DPI
				(int)(window_dpi(w) / window_scale(w) + 0.5);
		}
		break;
	case op_FPICKpm: fpick_get_filename(slot[0], res, size, idx); break;
	case op_uFPICK:
	{
		char *s = ((swdata *)slot[0])->strs;
		/* !!! Pseudo widget uses system encoding in all modes */
		if (idx == FPICK_RAW) s = strrchr(s, DIR_SEP) + 1; // Guaranteed
		strncpy0(res, s, size);
		break;
	}
	case op_PENTRY: case op_PATH: case op_PATHs:
	{
		char *s = (char *)gtk_entry_get_text(slot[0]);
		if (idx == PATH_VALUE) gtkncpy(res, s, size);
		else strncpy0(res, s, size); // PATH_RAW
		break;
	}
	case op_CSCROLL:
	{
		GtkAdjustment *xa, *ya;
		int *v = res;
		get_scroll_adjustments(slot[0], &xa, &ya);
		if (idx == CSCROLL_XYSIZE)
		{
			switch (size / sizeof(int))
			{
			default:
			case 4: v[3] = gtk_adjustment_get_page_size(ya);
			case 3: v[2] = gtk_adjustment_get_page_size(xa);
			case 2: v[1] = gtk_adjustment_get_value(ya);
			case 1: v[0] = gtk_adjustment_get_value(xa);
			case 0: break;
			}
		}
		else if (idx == CSCROLL_LIMITS)
		{
			if (size >= sizeof(int))
				v[0] = gtk_adjustment_get_upper(xa) -
					gtk_adjustment_get_page_size(xa);
			if (size >= sizeof(int) * 2)
				v[1] = gtk_adjustment_get_upper(ya) -
					gtk_adjustment_get_page_size(ya);
		}
		break;
	}
	case op_CANVAS:
	{
		GtkWidget *w = slot[0];
		if (idx == CANVAS_SIZE)
		{
			int *v = res;
#if GTK_MAJOR_VERSION == 3
			GtkAllocation alloc;
			gtk_widget_get_allocation(w, &alloc);
			if (size >= sizeof(int)) v[0] = alloc.width;
			if (size >= sizeof(int) * 2) v[1] = alloc.height;
#else
			if (size >= sizeof(int)) v[0] = w->allocation.width;
			if (size >= sizeof(int) * 2) v[1] = w->allocation.height;
#endif
		}
		else if (idx == CANVAS_VPORT)
		{
			if (size < sizeof(int) * 4) break;
			wjcanvas_get_vport(w, res);
		}
		else if (idx == CANVAS_FIND_MOUSE)
		{
			mouse_ext *m = res;
			gint x, y;
			int vport[4];

			// No reason to parcel the data
			if (size < sizeof(mouse_ext)) break;
			gdk_window_get_pointer(gtk_widget_get_window(w), &x, &y,
				&m->state);
			wjcanvas_get_vport(w, vport);
			m->x = x + vport[0];
			m->y = y + vport[1];

			m->button = state_to_button(m->state);
			m->count = 0;
			m->pressure = MAX_PRESSURE;
		}
		break;
	}
	case op_LISTC: case op_LISTCd: case op_LISTCu:
	case op_LISTCS: case op_LISTCX:
		/* if (idx == LISTC_ORDER) */
		listc_get_order(slot[0], res, size / sizeof(int));
		break;
#if 0 /* Getting raw selection - not needed for now */
		*(int *)res = (clist->selection ? (int)clist->selection->data : 0);
#endif
	case op_KEYMAP:
		if (size >= sizeof(keymap_dd *))
			*(keymap_dd **)res = keymap_export(slot[2]);
		break;
	}
}

/* These cannot be peeked just by calling do_query() with a preset int* var:
	op_TBRBUTTON
	op_MENURITEM
	op_TCOLOR
	op_COMBOENTRY
	op_ENTRY, op_MLENTRY
	op_TEXT
 * Others can, if need be */

void cmd_setv(void **slot, void *res, int idx)
{
// !!! Support only what actually used on
	int op = GET_OP(slot);
	if (op == op_WDONE)
	{
		if (idx == WDATA_ACTMAP)
		{
			v_dd *vdata = GET_VDATA(slot);
			if (vdata->actmask != (unsigned)res) // If anything changed
				act_state(vdata, (unsigned)res);
			return;
		}
		// skip to toplevel slot
		slot = NEXT_SLOT(slot) , op = GET_OP(slot);
	}
	if (IS_UNREAL(slot)) op = GET_UOP(slot);
	switch (op)
	{
	case op_uWINDOW:
		if (cmd_mode || (idx != WINDOW_DISAPPEAR)) break;
		// Fallthrough
	case op_MAINWINDOW: case op_WINDOW: case op_WINDOWm: case op_DIALOGm:
	{
		v_dd *vdata = GET_VDATA(PREV_SLOT(slot));

		if (idx == WINDOW_TITLE)
			gtk_window_set_title(slot[0], res);
		else if (idx == WINDOW_ESC_BTN)
		{
			GtkAccelGroup *ag = gtk_accel_group_new();
			gtk_widget_add_accelerator(*(void **)res, "clicked", ag,
				KEY(Escape), 0, (GtkAccelFlags)0);
			gtk_window_add_accel_group(slot[0], ag);
		}
		else if (idx == WINDOW_FOCUS)
		{
			/* Cannot move focus to nowhere while window is hidden */
			if (!res && !GTK_WIDGET_VISIBLE(slot[0]))
				vdata->unfocus = TRUE;
			gtk_window_set_focus(slot[0], res ? *(void **)res : NULL);
		}
		else if (idx == WINDOW_RAISE)
		{
			if (GTK_WIDGET_VISIBLE(slot[0]))
				gdk_window_raise(gtk_widget_get_window(
					GTK_WIDGET(slot[0])));
			/* Cannot raise hidden window, will do it later */
			else vdata->raise = TRUE;
		}
		else if (idx == WINDOW_DISAPPEAR)
		{
			GtkWidget *w = slot[0];

			if (IS_UNREAL(slot)) w = NULL;
			if (!res) /* Show again */
			{
				if (!w) break; // Paranoia
#if GTK_MAJOR_VERSION >= 2
				gdk_window_deiconify(gtk_widget_get_window(w));
#endif
				gdk_window_raise(gtk_widget_get_window(w));
				break;
			}
			if (w == main_window) w = NULL;
			/* Hide from view, to allow a screenshot */
#if GTK_MAJOR_VERSION == 1
			gdk_window_lower(main_window->window);
			if (w) gdk_window_lower(w->window);

			gdk_flush();
			handle_events();	// Wait for minimize

			sleep(1);	// Wait a second for screen to redraw
#else /* #if GTK_MAJOR_VERSION >= 2 */
			if (w)
			{
				gtk_window_set_transient_for(slot[0], NULL);
				gdk_window_iconify(gtk_widget_get_window(w));
			}
			gdk_window_iconify(gtk_widget_get_window(main_window));

			gdk_flush();
			handle_events(); 	// Wait for minimize

			g_usleep(400000);	// Wait 0.4 s for screen to redraw
#endif
		}
		else if (idx == WINDOW_TEXTENG) do_render_text(res);
		break;
	}
	case op_FPICKpm: fpick_set_filename(slot[0], res, idx); break;
	case op_uFPICK:
	{
		char *s, *ts, *s0 = ((swdata *)slot[0])->strs;

		/* !!! Pseudo widget uses system encoding in all modes */
		if (idx == FPICK_RAW)
		{
			ts = strrchr(s0, DIR_SEP); // Guaranteed to be there
			s = wjstrcat(NULL, 0, s0, ts - s0 + 1, res, NULL);
		}
		else s = resolve_path(NULL, PATHBUF, res);

		free(s0);
		((swdata *)slot[0])->strs = s;
		break;
	}
	case op_NBOOK: case op_NBOOKl:
		gtk_notebook_set_show_tabs(slot[0], (int)res);
		break;
	case op_SPINSLIDE: case op_SPINSLIDEa:
	case op_SPIN: case op_SPINc: case op_SPINa:
	{
		int *v = res, n = v[0];
		spin_set_range(slot[0], v[1], v[2]);
		gtk_spin_button_set_value(slot[0], n);
		break;
	}
	case op_uSPIN: case op_uSPINa:
	{
		swdata *sd = slot[0];
		int n, a, b, *v = res;

		sd->range[0] = a = v[1];
		sd->range[1] = b = v[2];
		n = v[0] > b ? b : v[0] < a ? a : v[0];
		if (n != sd->value)
		{
			sd->value = n;
			cmd_event(slot, op_EVT_CHANGE);
		}
		break;
	}
	case op_MENUITEM: case op_MENUCHECK: case op_MENURITEM:
		gtk_label_set_text(GTK_LABEL(gtk_bin_get_child(GTK_BIN(slot[0]))), res);
		break;
	case op_LABEL: case op_WLABEL: case op_STLABEL:
		gtk_label_set_text(slot[0], res);
		break;
	case op_TEXT:
		set_textarea(slot[0], res);
		break;
	case op_ENTRY: case op_MLENTRY:
		gtk_entry_set_text(slot[0], res);
		break;
	case op_uENTRY: case op_uPATHSTR:
		set_uentry(slot[0], res);
		cmd_event(slot, op_EVT_CHANGE);
		break;
	case op_PENTRY: case op_PATH: case op_PATHs:
		set_path(slot[0], res, idx);
		break;
	case op_COMBOENTRY:
		comboentry_set_text(slot[0], res);
		break;
	case op_COLOR: case op_TCOLOR:
	{
		int *v = res;
		if (idx == COLOR_ALL)
			cpick_set_colour_previous(slot[0], v[2], v[3]);
		cpick_set_colour(slot[0], v[0], v[1]);
		break;
	}
	case op_uCOLOR:
		((swdata *)slot[0])->value = *(int *)res;
		break;
	case op_COLORLIST: case op_COLORLISTN:
		colorlist_reset_color(slot, (int)res);
		break;
	case op_PROGRESS:
#if GTK_MAJOR_VERSION == 3
		gtk_progress_bar_set_fraction(slot[0], (int)res / 100.0);
#else
		gtk_progress_set_percentage(slot[0], (int)res / 100.0);
#endif
		break;
	case op_CSCROLL:
	{
		/* if (idx == CSCROLL_XYRANGE) */
		/* Used as a way to preset position for a delayed resize */
		GtkAdjustment *xa, *ya;
		int *v = res;
		get_scroll_adjustments(slot[0], &xa, &ya);
#if GTK_MAJOR_VERSION == 3
		{
			guint idv = g_signal_lookup("value_changed",
				G_TYPE_FROM_INSTANCE(xa));
			guint idc = g_signal_lookup("changed",
				G_TYPE_FROM_INSTANCE(xa));
			g_signal_handlers_block_matched(G_OBJECT(xa),
				G_SIGNAL_MATCH_ID, idv, 0, NULL, NULL, NULL);
			g_signal_handlers_block_matched(G_OBJECT(ya),
				G_SIGNAL_MATCH_ID, idv, 0, NULL, NULL, NULL);
			g_signal_handlers_block_matched(G_OBJECT(xa),
				G_SIGNAL_MATCH_ID, idc, 0, NULL, NULL, NULL);
			g_signal_handlers_block_matched(G_OBJECT(ya),
				G_SIGNAL_MATCH_ID, idc, 0, NULL, NULL, NULL);
			/* Set limit first and value after */
			gtk_adjustment_set_upper(xa, v[2]);
			gtk_adjustment_set_upper(ya, v[3]);
			gtk_adjustment_set_value(xa, v[0]);
			gtk_adjustment_set_value(ya, v[1]);
			g_signal_handlers_unblock_matched(G_OBJECT(xa),
				G_SIGNAL_MATCH_ID, idv, 0, NULL, NULL, NULL);
			g_signal_handlers_unblock_matched(G_OBJECT(ya),
				G_SIGNAL_MATCH_ID, idv, 0, NULL, NULL, NULL);
			g_signal_handlers_unblock_matched(G_OBJECT(xa),
				G_SIGNAL_MATCH_ID, idc, 0, NULL, NULL, NULL);
			g_signal_handlers_unblock_matched(G_OBJECT(ya),
				G_SIGNAL_MATCH_ID, idc, 0, NULL, NULL, NULL);
		}
#else
		xa->value = v[0];
		ya->value = v[1];
		xa->upper = v[2];
		ya->upper = v[3];
#endif
		break;
	}
	case op_CANVASIMG: case op_CANVASIMGB:
	{
		rgbimage_data *rd = slot[2];
		int *v = res;
		rd->w = v[0];
		rd->h = v[1];
		wjcanvas_size(slot[0], v[0], v[1]);
		break;
	}
	case op_CANVAS:
	{
		GtkWidget *w = slot[0];
		int vport[4], rxy[4], *v = res;

		if (idx == CANVAS_SIZE)
		{
			wjcanvas_size(w, v[0], v[1]);
			break;
		}
		wjcanvas_get_vport(w, vport);
		if (idx == CANVAS_REPAINT)
		{
			wjcanvas_uncache(w, v);
			if (clip(rxy, v[0], v[1], v[2], v[3], vport))
				gtk_widget_queue_draw_area(w,
					rxy[0] - vport[0], rxy[1] - vport[1],
					rxy[2] - rxy[0], rxy[3] - rxy[1]);
		}
		else if (idx == CANVAS_PAINT)
		{
			rgbcontext *ctx = res;
			/* Paint */
			if (ctx->rgb)
			{
#if GTK_MAJOR_VERSION == 3
				wjcanvas_draw_rgb(w, ctx->xy[0], ctx->xy[1],
					ctx->xy[2] - ctx->xy[0],
					ctx->xy[3] - ctx->xy[1],
					ctx->rgb, (ctx->xy[2] - ctx->xy[0]) * 3,
					0, TRUE);
// !!! Remains to be seen if the below is needed - might be, in some cases
//				gdk_window_process_updates(gtk_widget_get_window(w), FALSE);
#else /* if GTK_MAJOR_VERSION <= 2 */
				gdk_draw_rgb_image(w->window, w->style->black_gc,
					ctx->xy[0] - vport[0],
					ctx->xy[1] - vport[1],
					ctx->xy[2] - ctx->xy[0],
					ctx->xy[3] - ctx->xy[1],
					GDK_RGB_DITHER_NONE, ctx->rgb,
					(ctx->xy[2] - ctx->xy[0]) * 3);
#endif
				free(ctx->rgb);
			}
			/* Prepare */
			else if (clip(ctx->xy, vport[0], vport[1],
				vport[2], vport[3], ctx->xy))
				ctx->rgb = malloc((ctx->xy[2] - ctx->xy[0]) *
					(ctx->xy[3] - ctx->xy[1]) * 3);
		}
		else if (idx == CANVAS_BMOVE_MOUSE)
		{
			gint x, y;
			gdk_window_get_pointer(gtk_widget_get_window(w), &x, &y, NULL);
			if ((x >= 0) && (y >= 0) &&
				((x += vport[0]) < vport[2]) &&
				((y += vport[1]) < vport[3]) &&
				/* Autoscroll canvas if required */
				wjcanvas_scroll_in(w, x + v[0], y + v[1]))
			{
				wjcanvas_get_vport(w, rxy);
				v[0] += vport[0] - rxy[0];
				v[1] += vport[1] - rxy[1];
			}
			if (move_mouse_relative(v[0], v[1])) v[0] = v[1] = 0;
		}
		break;
	}
	case op_FCIMAGEP:
	{
		fcimage_data *fd = slot[2];
		int *v = res;
		if (!fd->xy) break;
		memcpy(fd->xy, v, sizeof(int) * 2);
		wjpixmap_move_cursor(slot[0], v[0], v[1]);
		break;
	}
	case op_LISTCCr:
		listcc_reset(slot, (int)res);
// !!! May be needed if LISTCC_RESET_ROW w/GtkList gets used to display an added row
//		gtk_widget_queue_resize(slot[0]);
		break;
	case op_LISTC: case op_LISTCd: case op_LISTCu:
	case op_LISTCS: case op_LISTCX:
		if (idx == LISTC_RESET_ROW)
			listc_reset_row(slot[0], slot[2], (int)res);
		else if (idx == LISTC_SORT)
			listc_sort_by(slot[0], slot[2], (int)res);
		break;
#if 0 /* Moving raw selection - not needed for now */
		gtk_clist_select_row(slot[0], (int)res, 0);
#endif
	case op_KEYMAP:
		if (idx == KEYMAP_KEY) keymap_find(slot[2], res);
		else /* if (idx == KEYMAP_MAP) */
		{
			keymap_init(slot[2], res);
			keymap_reset(slot[2]);
		}
		break;
	case op_EV_DRAGFROM:
	{
		drag_den *dp = (void *)slot;
		if (idx == DRAG_ICON_RGB) dp->dc->color = (int)res;
		else /* if (idx == DRAG_DATA) */
		{
			char **pp = res;
			gtk_selection_data_set(dp->ds->data,
				/* Could use "dp->ds->data->target" instead */
				gdk_atom_intern(dp->d.format->target, FALSE),
				dp->d.format->format ? dp->d.format->format : 8,
				pp[0], pp[1] - pp[0]);
		}
		break;
	}
	case op_EV_COPY:
	{
		copy_den *cp = (void *)slot;
		char **pp = res;
		gtk_selection_data_set(cp->data, gtk_selection_data_get_target(cp->data),
			cp->c.format->format ? cp->c.format->format : 8,
			pp[0], pp[1] - pp[0]);
		break;
	}
	case op_CLIPBOARD:
		offer_text(slot, res);
		break;
#if GTK_MAJOR_VERSION >= 2
	case op_FONTSEL:
	{
		GtkFontSelection *fs = slot[0];
		fontsel_data *fd = slot[2];

		fd->dpi = (int)res;
		if (!fd->sysdpi) fd->sysdpi = window_dpi(main_window); // Init
		/* To cause full preview reset */
#if GTK_MAJOR_VERSION == 3
		{
			GtkWidget *e = gtk_font_selection_get_size_entry(fs);
			char *s = strdup(gtk_entry_get_text(GTK_ENTRY(e)));

			gtk_entry_set_text(GTK_ENTRY(e), "0");
			gtk_widget_activate(e);
			gtk_entry_set_text(GTK_ENTRY(e), s);
			gtk_widget_activate(e);
			free(s);
			/* Force style update if not triggered by the above */
			if (fd->dpi != fd->lastdpi) fontsel_style(
				gtk_font_selection_get_preview_entry(fs), slot);
		}
#else /* #if GTK_MAJOR_VERSION <= 2 */
		{
			int size = fs->size;

			fs->size = 0;
			gtk_widget_activate(fs->size_entry);
			fs->size = size;
		}
#endif
		break;
	}
#endif
	}
}

void cmd_repaint(void **slot)
{
	int op = op;
	if (IS_UNREAL(slot)) return;
#if GTK_MAJOR_VERSION == 3
	op = GET_OP(slot);
	/* Tell cached widgets to drop cache */
	if (op == op_RGBIMAGE) reset_rgb(slot[0], slot[2]);
	else if ((op == op_CANVASIMG) || (op == op_CANVASIMGB) || (op == op_CANVAS))
		wjcanvas_uncache(slot[0], NULL);
#endif
#ifdef U_LISTS_GTK1
	op = GET_OP(slot);
	if ((op == op_COLORLIST) || (op == op_COLORLISTN))
	/* Stupid GTK+ does nothing for gtk_widget_queue_draw(allcol_list) */
		gtk_container_foreach(GTK_CONTAINER(slot[0]),
			(GtkCallback)gtk_widget_queue_draw, NULL);
	else
#endif
	gtk_widget_queue_draw(slot[0]);
}

#define SETCUR_KEY "mtPaint.cursor"

static void reset_cursor(GtkWidget *widget, gpointer user_data)
{
	gpointer c = gtk_object_get_data_by_id(GTK_OBJECT(widget), (GQuark)user_data);
	if (c != (gpointer)(-1)) gdk_window_set_cursor(gtk_widget_get_window(widget), c);
}

void cmd_cursor(void **slot, void **cursor)
{
	static GQuark setcur_key;
	GtkWidget *w = slot[0];

	if (IS_UNREAL(slot)) return;
	/* Remember cursor for restoring it after realize */
	if (!setcur_key) setcur_key = g_quark_from_static_string(SETCUR_KEY);
	if (!gtk_object_get_data_by_id(slot[0], setcur_key))
		gtk_signal_connect(slot[0], "realize",
			GTK_SIGNAL_FUNC(reset_cursor), (gpointer)setcur_key);
	gtk_object_set_data_by_id(slot[0], setcur_key, cursor ? cursor[0] :
		(gpointer)(-1));

	if (gtk_widget_get_window(w)) gdk_window_set_cursor(gtk_widget_get_window(w),
		cursor ? cursor[0] : NULL);
}

int cmd_checkv(void **slot, int idx)
{
	int op = GET_OP(slot);
	if (op == op_WDONE) // skip head noop
		slot = NEXT_SLOT(slot) , op = GET_OP(slot);
	if (IS_UNREAL(slot))
	{
		if (idx == SLOT_SENSITIVE)
		{
			/* Columns redirect to their list */
			if ((op >= op_COLUMN_0) && (op < op_COLUMN_LAST))
			{
				col_data *c = ((swdata *)slot[0])->strs;
				if (c) return (cmd_checkv(c->r, idx));
			}
			return (!((swdata *)slot[0])->insens);
		}
		if (idx == SLOT_UNREAL) return (TRUE);
		op = GET_UOP(slot);
	}
	if (idx == SLOT_RADIO) return ((op == op_TBRBUTTON) ||
		(op == op_MENURITEM) || (op == op_uMENURITEM));
	if (op == op_CLIPBOARD)
	{
		if (idx == CLIP_OFFER) return (offer_clipboard(slot, FALSE));
		if (idx == CLIP_PROCESS) return (process_clipboard(slot));
	}
	else if (op == op_EV_MOUSE)
	{
		mouse_den *dp = (void *)slot;
		void **canvas = EV_PARENT(slot);
		if (dp->mouse->count) return (FALSE); // Not a move
		return (wjcanvas_bind_mouse(canvas[0], slot[0],
			dp->mouse->x - dp->vport[0],
			dp->mouse->y - dp->vport[1]));
	}
	else if (op < op_EVT_0) // Regular widget
	{
		if (idx == SLOT_SENSITIVE)
			return (GTK_WIDGET_SENSITIVE(get_wrap(slot)));
		if (idx == SLOT_FOCUSED)
		{
			GtkWidget *w = gtk_widget_get_toplevel(slot[0]);
			if (!GTK_IS_WINDOW(w)) return (FALSE);
			w = gtk_window_get_focus(GTK_WINDOW(w));
			return (w && ((w == slot[0]) ||
				gtk_widget_is_ancestor(w, slot[0])));
		}
		if (idx == SLOT_SCRIPTABLE)
			return (!!(GET_OPF(slot) & WB_SFLAG));
		/* if (idx == SLOT_UNREAL) return (FALSE); */
	}
	return (FALSE);
}

void cmd_event(void **slot, int op)
{
	slot = op_slot(slot, op);
	if (slot) get_evt_1(NULL, slot); // Found
}

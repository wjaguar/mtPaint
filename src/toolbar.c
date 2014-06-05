/*	toolbar.c
	Copyright (C) 2006-2014 Mark Tyler and Dmitry Groshev

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
#include "inifile.h"
#include "ani.h"
#include "png.h"
#include "mainwindow.h"
#include "otherwindow.h"
#include "canvas.h"
#include "toolbar.h"
#include "layer.h"
#include "viewer.h"
#include "channels.h"
#include "csel.h"
#include "font.h"
#include "icons.h"
#include "vcode.h"



GtkWidget *icon_buttons[TOTAL_ICONS_TOOLS];

int toolbar_status[TOOLBAR_MAX];			// True=show
GtkWidget *toolbar_boxes[TOOLBAR_MAX],			// Used for showing/hiding
	*toolbar_zoom_view, *drawing_col_prev;
void **toolbar_boxes_[TOOLBAR_MAX];		// Used for showing/hiding

GdkCursor *m_cursor[TOTAL_CURSORS];			// My mouse cursors
GdkCursor *move_cursor, *busy_cursor, *corner_cursor[4]; // System cursors



static GtkWidget *toolbar_zoom_main;
static void **toolbar_labels[2],	// Colour A & B
	*ts_spinslides[4],		// Size, flow, opacity, value
	*ts_label_channel;		// Channel name

static unsigned char mem_prev[PREVIEW_WIDTH * PREVIEW_HEIGHT * 3];
					// RGB colours, tool, pattern preview

static gint expose_preview( GtkWidget *widget, GdkEventExpose *event )
{
	int rx, ry, rw, rh;

	rx = event->area.x;
	ry = event->area.y;
	rw = event->area.width;
	rh = event->area.height;

	if ( ry < PREVIEW_HEIGHT )
	{
		if ( (ry+rh) >= PREVIEW_HEIGHT )
		{
			rh = PREVIEW_HEIGHT - ry;
		}
		gdk_draw_rgb_image( widget->window, widget->style->black_gc,
				rx, ry, rw, rh,
				GDK_RGB_DITHER_NONE,
				mem_prev + 3*( rx + PREVIEW_WIDTH*ry ),
				PREVIEW_WIDTH*3
				);
	}

	return FALSE;
}


static gint click_colours( GtkWidget *widget, GdkEventButton *event )
{
	if (mem_img[CHN_IMAGE])
	{
		if ( event->y > 31 ) choose_pattern(0);
		else
		{
			if ( event->x < 48 ) colour_selector(COLSEL_EDIT_AB);
			else choose_pattern(1);
		}
	}

	return FALSE;
}

static GtkWidget *toolbar_add_zoom(GtkWidget *box)	// Add zoom combo box
{
	int i;
	static char *txt[] = { "10%", "20%", "25%", "33%", "50%", "100%",
		"200%", "300%", "400%", "800%", "1200%", "1600%", "2000%", NULL };
	GtkWidget *combo, *combo_entry;
	GList *combo_list = NULL;


	combo = gtk_combo_new();
	gtk_combo_set_value_in_list (GTK_COMBO (combo), FALSE, FALSE);
	gtk_widget_show (combo);
	combo_entry = GTK_COMBO (combo)->entry;
	GTK_WIDGET_UNSET_FLAGS (combo_entry, GTK_CAN_FOCUS);
	gtk_widget_set_usize(GTK_COMBO(combo)->button, 18, -1);


#if GTK_MAJOR_VERSION == 1
	gtk_widget_set_usize(combo, 75, -1);
#else /* #if GTK_MAJOR_VERSION == 2 */
	gtk_entry_set_width_chars(GTK_ENTRY(combo_entry), 6);
#endif

	gtk_entry_set_editable( GTK_ENTRY(combo_entry), FALSE );

	for ( i=0; txt[i]; i++ ) combo_list = g_list_append( combo_list, txt[i] );

	gtk_combo_set_popdown_strings( GTK_COMBO(combo), combo_list );
	g_list_free( combo_list );
	gtk_entry_set_text( GTK_ENTRY(combo_entry), "100%" );

	pack(box, combo);

	return (combo);
}

static float toolbar_get_zoom( GtkWidget *combo )
{
	int i = 0;
	float res = 0;
	char *txt = (char *) gtk_entry_get_text( GTK_ENTRY(GTK_COMBO (combo)->entry) );

	if ( strlen(txt) > 2)		// Weed out bogus calls
	{
		sscanf(txt, "%i%%", &i);
		res = (float)i / 100;
		if (res < 1.0) res = 1.0 / rint(1.0 / res);
	}

	return res;
}

void toolbar_zoom_update()			// Update the zoom combos to reflect current zoom
{
	char txt[32];

	if ( toolbar_zoom_main == NULL ) return;
	if ( toolbar_zoom_view == NULL ) return;

	snprintf( txt, 30, "%i%%", (int)(can_zoom*100) );
	gtk_entry_set_text( GTK_ENTRY(GTK_COMBO(toolbar_zoom_main)->entry), txt );

	snprintf( txt, 30, "%i%%", (int)(vw_zoom*100) );
	gtk_entry_set_text( GTK_ENTRY(GTK_COMBO(toolbar_zoom_view)->entry), txt );
}

static void toolbar_zoom_main_change()
{
	float new = toolbar_get_zoom( toolbar_zoom_main );
	int z = cursor_zoom;
	cursor_zoom = FALSE; // !!! Cursor in dropdown - don't zoom on it
	if (new > 0) align_size(new);
	cursor_zoom = z;
}

static void toolbar_zoom_view_change()
{
	float new = toolbar_get_zoom( toolbar_zoom_view );

	if ( new > 0 ) vw_align_size( new );
}

static void **settings_buttons[TOTAL_SETTINGS];
static int v_settings[TOTAL_SETTINGS];
static int *vars_settings[TOTAL_SETTINGS] = {
	&mem_continuous, &mem_undo_opacity,
	&tint_mode[0], &tint_mode[1],
	&mem_cselect, &mem_blend,
	&mem_unmask, &mem_gradient
};

void mode_change(int setting, int state)
{
	if (!*(int *)cmd_read(settings_buttons[setting], NULL) != !state)
		cmd_set(settings_buttons[setting], state); // Toggle the button

	if (!state == !*vars_settings[setting]) return; // No change, or changed already
	*(vars_settings[setting]) = state;
	if ((setting == SETB_CSEL) && mem_cselect && !csel_data)
	{
		csel_init();
		mem_cselect = !!csel_data;
	}
	update_stuff(setting == SETB_GRAD ? UPD_GMODE : UPD_MODE);
}

typedef struct {
	filterwindow_dd fw;
	int fstep;
} flood_dd;

static int set_flood(flood_dd *dt, void **wdata)
{
	run_query(wdata);
	flood_step = dt->fstep / 100.0;
	return TRUE;
}

static void *toolwindow_code[] = {
	UNLESSbt("centerSettings"), WPMOUSE, GOTO(filterwindow_code) };

#define WBbase flood_dd
static void *flood_code[] = {
	VBOXPS,
	BORDER(SPIN, 0),
	FSPIN(fstep, 0, 20000),
	CHECKv(_("RGB Cube"), flood_cube),
	CHECKv(_("By image channel"), flood_img),
	CHECKv(_("Gradient-driven"), flood_slide),
	WDONE, RET
};
#undef WBbase

void flood_settings() /* Flood fill step */
{
	flood_dd tdata = {
		{ _("Fill settings"), flood_code, FW_FN(set_flood) },
		rint(flood_step * 100) };
	run_create(toolwindow_code, &tdata, sizeof(tdata));
}

static int set_settings(filterwindow_dd *dt, void **wdata)
{
	run_query(wdata);
	return TRUE;
}

#define WBbase filterwindow_dd
void *smudge_code[] = { CHECKv(_("Respect opacity mode"), smudge_mode), RET };
#undef WBbase

void smudge_settings() /* Smudge opacity mode */
{
	static filterwindow_dd tdata = {
		_("Smudge settings"), smudge_code, FW_FN(set_settings) };
	run_create(toolwindow_code, &tdata, sizeof(tdata));
}

#define WBbase filterwindow_dd
void *step_code[] = {
	VBOXPS,
	BORDER(SPIN, 0),
	SPINv(brush_spacing, 0, MAX_WIDTH),
// !!! Not implemented yet
//	CHECKv(_("Flat gradient strokes"), ???),
	WDONE, RET
};
#undef WBbase

void step_settings() /* Brush spacing */
{
	static filterwindow_dd tdata = {
		_("Brush spacing"), step_code, FW_FN(set_settings) };
	run_create(toolwindow_code, &tdata, sizeof(tdata));
}

typedef struct {
	filterwindow_dd fw;
	int mode, reverse;
	int red, green, blue;
} blend_dd;

static int set_blend(blend_dd *dt, void **wdata)
{
	int i, j;

	run_query(wdata);
	i = dt->mode < 0 ? BLEND_NORMAL : dt->mode; // Paranoia
	j = !dt->red + (!dt->green << 1) + (!dt->blue << 2);

	/* Don't accept stop-all or do-nothing */
	if ((j == 7) || !(i | j)) return (FALSE);

	blend_mode = i | (dt->reverse ? BLEND_REVERSE : 0) | (j << BLEND_RGBSHIFT);

	return (TRUE);
}

static char *blends[BLEND_NMODES] = {
	_("Normal"), _("Hue"), _("Saturation"), _("Value"),
	_("Colour"), _("Saturate More"),
	_("Multiply"), _("Divide"), _("Screen"), _("Dodge"),
	_("Burn"), _("Hard Light"), _("Soft Light"), _("Difference"),
	_("Darken"), _("Lighten"), _("Grain Extract"),
	_("Grain Merge") };

#define WBbase blend_dd
static void *blend_code[] = {
	VBOXbp(5, 5, 5),
	COMBO(blends, BLEND_NMODES, mode),
	CHECK(_("Reverse"), reverse),
	HSEP,
	EQBOXs(5),
	CHECK(_("Red"), red),
	CHECK(_("Green"), green),
	CHECK(_("Blue"), blue),
	WDONE, WDONE, RET
};
#undef WBbase

void blend_settings() /* Blend mode */
{
	blend_dd tdata = {
		{ _("Blend mode"), blend_code, FW_FN(set_blend) },
		blend_mode & BLEND_MMASK, blend_mode & BLEND_REVERSE,
		~blend_mode & (1 << BLEND_RGBSHIFT),
		~blend_mode & (2 << BLEND_RGBSHIFT),
		~blend_mode & (4 << BLEND_RGBSHIFT) };
	run_create(toolwindow_code, &tdata, sizeof(tdata));
}


typedef struct {
	int size, flow, opac, chan;
} settings_dd;

static void ts_spinslide_moved(settings_dd *dt, void **wdata, int what, void **where)
{
	void *cause = cmd_read(where, dt);

	if (cause == &dt->size) tool_size = dt->size;
	else if (cause == &dt->flow) tool_flow = dt->flow;
	else if (cause == &dt->opac)
	{
		if (dt->opac != tool_opacity) pressed_opacity(dt->opac);
	}
	else /* if (cause == &dt->chan) */
	{
		if (dt->chan != channel_col_A[mem_channel])
			pressed_value(dt->chan);
	}
}


void toolbar_settings_exit(void *dt, void **wdata)
{
	if (!wdata) wdata = toolbar_boxes_[TOOLBAR_SETTINGS];
	if (!wdata) return;
	toolbar_boxes_[TOOLBAR_SETTINGS] = NULL;
	gtk_check_menu_item_set_active(
		GTK_CHECK_MENU_ITEM(menu_widgets[MENU_TBSET]), FALSE);
	run_destroy(wdata);
}

static void toolbar_click(GtkWidget *widget, gpointer user_data)
{
	toolbar_item *item = user_data;

	action_dispatch(item->action, item->mode, item->radio < 0 ? TRUE :
		GTK_TOGGLE_BUTTON(widget)->active, FALSE);
}

static gboolean toolbar_rclick(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	toolbar_item *item = user_data;

	/* Handle only right clicks */
	if ((event->type != GDK_BUTTON_PRESS) || (event->button != 3))
		return (FALSE);
	action_dispatch(item->action2, item->mode2, TRUE, FALSE);
	return (TRUE);
}

static void fill_toolbar(GtkToolbar *bar, toolbar_item *items, GtkWidget **wlist,
	GtkSignalFunc lclick, GtkSignalFunc rclick)
{
	GtkWidget *item, *radio[32];

	if (!lclick) lclick = GTK_SIGNAL_FUNC(toolbar_click);
	if (!rclick) rclick = GTK_SIGNAL_FUNC(toolbar_rclick);

	memset(radio, 0, sizeof(radio));
	for (; items->tooltip; items++)
	{
		if (!items->xpm) // This is a separator
		{
			gtk_toolbar_append_space(bar);
			continue;
		}
		item = gtk_toolbar_append_element(bar,
			items->radio < 0 ? GTK_TOOLBAR_CHILD_BUTTON :
			items->radio ? GTK_TOOLBAR_CHILD_RADIOBUTTON :
			GTK_TOOLBAR_CHILD_TOGGLEBUTTON,
			items->radio > 0 ? radio[items->radio] : NULL,
			NULL, __(items->tooltip), "Private",
			xpm_image(items->xpm), lclick, (gpointer)items);
		if (items->radio > 0) radio[items->radio] = item;
		if (items->action2) gtk_signal_connect(GTK_OBJECT(item),
			"button_press_event", rclick, (gpointer)items);
		mapped_dis_add(item, items->actmap);
		if (wlist) wlist[items->ID] = item;
	}
}

/* The following is main toolbars auto-sizing code. If toolbar is too long for
 * the window, some of its items get hidden, but remain accessible through an
 * "overflow box" - a popup with 5xN grid of buttons inside. This way, we can
 * support small-screen devices without penalizing large-screen ones. - WJ */

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

static int split_toolbar_at(GtkWidget *vport, int w)
{
	GtkWidget *tbar;
	GList *chain;
	GtkToolbarChild *child;
	GtkAllocation *alloc;
	int border, x = 0;

	if (w < 1) w = 1;
	if (!GTK_IS_VIEWPORT(vport)) return (w);
	tbar = GTK_BIN(vport)->child;
	if (!tbar || !GTK_IS_TOOLBAR(tbar)) return (w);
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
	GtkBox *box = GTK_BOX(widget);
	GtkBoxChild *child;
	GList *chain;
	GtkRequisition wreq;
	int idx, cnt, w, h, l;

	idx = cnt = w = h = 0;
	for (chain = box->children; chain; chain = chain->next , idx++)
	{
		child = chain->data;
		if (!GTK_WIDGET_VISIBLE(child->widget)) continue;
		gtk_widget_size_request(child->widget, &wreq);
		if (h < wreq.height) h = wreq.height;
		/* Button in slot 1 adds no extra width */
		if (idx == 1) continue;
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
	GtkWidget *button = NULL;
	GtkBox *box = GTK_BOX(widget);
	GtkBoxChild *child;
	GList *chain;
	GtkRequisition wreq;
	GtkAllocation wall;
	int vw, bw, xw, dw, pad, spacing;
	int idx, cnt, l, x, wrkw;

	widget->allocation = *alloc;

	/* Calculate required size */
	idx = cnt = 0;
	vw = bw = xw = 0;
	spacing = box->spacing;
	for (chain = box->children; chain; chain = chain->next , idx++)
	{
		child = chain->data;
		pad = child->padding * 2;
		if (idx == 1)
		{
			gtk_widget_size_request(button = child->widget, &wreq);
			bw = wreq.width + pad + spacing; // Button
		}
		else if (GTK_WIDGET_VISIBLE(child->widget))
		{
			gtk_widget_get_child_requisition(child->widget, &wreq);
			if (!idx) vw = wreq.width; // Viewport
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
		vw = split_toolbar_at(BOX_CHILD_0(widget), alloc->width - xw - bw);
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
	idx = 0;
	for (chain = box->children; chain; chain = chain->next , idx++)
	{
		child = chain->data;
		pad = child->padding;
		/* Button uses size, the others, visibility */
		if (idx == 1 ? !bw : !GTK_WIDGET_VISIBLE(child->widget)) continue;
		gtk_widget_get_child_requisition(child->widget, &wreq);
		wrkw = idx ? wreq.width : vw;
		if (idx > 1) wrkw -= dw;
		if (wrkw < 1) wrkw = 1;
		wall.width = wrkw;
		x = (wall.x = x + pad) + wrkw + pad + spacing;
		gtk_widget_size_allocate(child->widget, &wall);
	}

	if (button) widget_showhide(button, bw);
}

static void htoolbox_popup(GtkWidget *button, gpointer user_data)
{
	GtkWidget *tool, *vport, *btn, *popup = user_data;
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
	vport = gtk_object_get_user_data(GTK_OBJECT(button));
	vl = vport->allocation.width;
	box = gtk_object_get_user_data(GTK_OBJECT(popup));
	for (chain = box->children; chain; chain = chain->next)
	{
		child = chain->data;
		btn = child->widget;
		tool = gtk_object_get_user_data(GTK_OBJECT(btn));
		if (!tool) continue; // Paranoia
		/* Copy button relief setting of toolbar buttons */
		gtk_button_set_relief(GTK_BUTTON(btn),
			gtk_button_get_relief(GTK_BUTTON(tool)));
		/* Copy their state (feedback is disabled while invisible) */
		if (GTK_IS_TOGGLE_BUTTON(btn)) gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(btn), GTK_TOGGLE_BUTTON(tool)->active);
//		gtk_widget_set_style(btn, gtk_rc_get_style(tool));
		/* Set visibility */
		widget_showhide(btn, GTK_WIDGET_VISIBLE(tool) &&
			(tool->allocation.x >= vl));
	}
	gtk_widget_size_request(popup, &req);
	gdk_window_get_origin(button->parent->window, &x, &y);
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
	GtkWidget *popup = user_data;

	/* Invisible buttons don't send (virtual) clicks to toolbar */
	if (!GTK_WIDGET_VISIBLE(popup)) return;
	/* Ignore radio buttons getting depressed */
	if (GTK_IS_RADIO_BUTTON(button) && !GTK_TOGGLE_BUTTON(button)->active)
		return;
	htoolbox_popdown(popup);
	gtk_button_clicked(GTK_BUTTON(gtk_object_get_user_data(GTK_OBJECT(button))));
}

static gboolean htoolbox_tool_rclick(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	toolbar_item *item = user_data;

	/* Handle only right clicks */
	if ((event->type != GDK_BUTTON_PRESS) || (event->button != 3))
		return (FALSE);
	htoolbox_popdown(gtk_widget_get_toplevel(widget));
	action_dispatch(item->action2, item->mode2, TRUE, FALSE);
	return (TRUE);
}

static GtkWidget *smart_toolbar(toolbar_item *items, GtkWidget **wlist)
{
	GtkWidget *box, *vport, *button, *arrow, *tbar, *popup, *ebox, *frame;
	GtkWidget *bbox, *item, *radio[32];

	box = wj_size_box();
	gtk_signal_connect(GTK_OBJECT(box), "size_request",
		GTK_SIGNAL_FUNC(htoolbox_size_req), NULL);
	gtk_signal_connect(GTK_OBJECT(box), "size_allocate",
		GTK_SIGNAL_FUNC(htoolbox_size_alloc), NULL);

	vport = pack(box, gtk_viewport_new(NULL, NULL));
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(vport), GTK_SHADOW_NONE);
	gtk_widget_show(vport);
	vport_noshadow_fix(vport);
	button = pack(box, gtk_button_new());
	gtk_object_set_user_data(GTK_OBJECT(button), vport);
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

	popup = gtk_window_new(GTK_WINDOW_POPUP);
	gtk_window_set_policy(GTK_WINDOW(popup), FALSE, FALSE, TRUE);
#if GTK2VERSION >= 10 /* GTK+ 2.10+ */
	gtk_window_set_type_hint(GTK_WINDOW(popup), GDK_WINDOW_TYPE_HINT_COMBO);
#endif
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(htoolbox_popup), popup);
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

	bbox = wj_size_box();
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
	gtk_object_set_user_data(GTK_OBJECT(popup), GTK_BOX(bbox));

#if GTK_MAJOR_VERSION == 1
	tbar = gtk_toolbar_new(GTK_ORIENTATION_HORIZONTAL, GTK_TOOLBAR_ICONS);
#else /* #if GTK_MAJOR_VERSION == 2 */
	tbar = gtk_toolbar_new();
	gtk_toolbar_set_style(GTK_TOOLBAR(tbar), GTK_TOOLBAR_ICONS);
#endif
	gtk_widget_show(tbar);
	gtk_container_add(GTK_CONTAINER(vport), tbar);

	fill_toolbar(GTK_TOOLBAR(tbar), items, wlist, NULL, NULL);
	gtk_tooltips_set_tip(GTK_TOOLBAR(tbar)->tooltips, button,
		__("More..."), "Private");
	memset(radio, 0, sizeof(radio));
	for (; items->tooltip; items++)
	{
		if (!items->xpm) continue; // This is a separator
		if (items->radio < 0) item = gtk_button_new();
		else
		{
			if (!items->radio) item = gtk_toggle_button_new();
			else radio[items->radio] = item = gtk_radio_button_new_from_widget(
				GTK_RADIO_BUTTON_0(radio[items->radio]));
			gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(item), FALSE);
		}
#if GTK2VERSION >= 4 /* GTK+ 2.4+ */
		gtk_button_set_focus_on_click(GTK_BUTTON(item), FALSE);
#endif
		gtk_container_add(GTK_CONTAINER(item), xpm_image(items->xpm));
		pack(bbox, item);
		gtk_tooltips_set_tip(GTK_TOOLBAR(tbar)->tooltips, item,
			__(items->tooltip), "Private");
		mapped_dis_add(item, items->actmap);
		gtk_object_set_user_data(GTK_OBJECT(item), wlist[items->ID]);
		gtk_signal_connect(GTK_OBJECT(item), "clicked",
			GTK_SIGNAL_FUNC(htoolbox_tool_clicked), popup);
		if (items->action2) gtk_signal_connect(GTK_OBJECT(item),
			"button_press_event", GTK_SIGNAL_FUNC(htoolbox_tool_rclick),
			(gpointer)items);
	}

	return (box);
}

#define GP_WIDTH 256
#define GP_HEIGHT 16
static GtkWidget *grad_view;

static void **create_grad_view(void **r, GtkWidget ***wpp, void **wdata)
{
	GdkPixmap *pmap;

	/* Gradient mode preview */
	pmap = gdk_pixmap_new(main_window->window, GP_WIDTH, GP_HEIGHT, -1);
	grad_view = gtk_pixmap_new(pmap, NULL);
	gdk_pixmap_unref(pmap);
	gtk_pixmap_set_build_insensitive(GTK_PIXMAP(grad_view), FALSE);
	gtk_container_add(GTK_CONTAINER(**wpp), grad_view);
	gtk_widget_show(grad_view);

	return (r);
}

static void settings_bar_click(settings_dd *dt, void **wdata, int what, void **where)
{
	int act_m, res;

	if (what == op_EVT_CHANGE)
	{
		act_m = TOOL_ID(where);
		res = *(int *)cmd_read(where, dt);
	}
	else /* if (what == op_EVT_CLICK) */
	{
		act_m = TOOL_IR(where);
		res = TRUE;
	}
	action_dispatch(act_m >> 16, (act_m & 0xFFFF) - 0x8000, res, FALSE);
}

#define WBbase settings_dd
static void *settings_code[] = {
	TOPVBOXV, // Keep height at max requested, to let dock contents stay put
	BORDER(TOOLBAR, 0),
	TOOLBARx(settings_bar_click, settings_bar_click),
	REFv(settings_buttons[SETB_CONT]),
	TBTOGGLExv(_("Continuous Mode"), XPM_ICON(mode_cont),
		ACTMOD(ACT_MODE, SETB_CONT), ACTMOD(DLG_STEP, 0),
		v_settings[SETB_CONT]),
	REFv(settings_buttons[SETB_OPAC]),
	TBTOGGLEv(_("Opacity Mode"), XPM_ICON(mode_opac),
		ACTMOD(ACT_MODE, SETB_OPAC), v_settings[SETB_OPAC]),
	REFv(settings_buttons[SETB_TINT]),
	TBTOGGLEv(_("Tint Mode"), XPM_ICON(mode_tint),
		ACTMOD(ACT_MODE, SETB_TINT), v_settings[SETB_TINT]),
	REFv(settings_buttons[SETB_TSUB]),
	TBTOGGLEv(_("Tint +-"), XPM_ICON(mode_tint2),
		ACTMOD(ACT_MODE, SETB_TSUB), v_settings[SETB_TSUB]),
	REFv(settings_buttons[SETB_CSEL]),
	TBTOGGLExv(_("Colour-Selective Mode"), XPM_ICON(mode_csel),
		ACTMOD(ACT_MODE, SETB_CSEL), ACTMOD(DLG_COLORS, COLSEL_EDIT_CSEL),
		v_settings[SETB_CSEL]),
	REFv(settings_buttons[SETB_FILT]),
	TBTOGGLExv(_("Blend Mode"), XPM_ICON(mode_blend),
		ACTMOD(ACT_MODE, SETB_FILT), ACTMOD(DLG_FILT, 0),
		v_settings[SETB_FILT]),
	REFv(settings_buttons[SETB_MASK]),
	TBTOGGLEv(_("Disable All Masks"), XPM_ICON(mode_mask),
		ACTMOD(ACT_MODE, SETB_MASK), v_settings[SETB_MASK]),
	REFv(settings_buttons[SETB_GRAD]),
	TBBOXTOGxv(_("Gradient Mode"), NULL,
		ACTMOD(ACT_MODE, SETB_GRAD), ACTMOD(DLG_GRAD, 1),
		v_settings[SETB_GRAD]),
	EXEC(create_grad_view),
	WDONE, WDONE,
	/* Colors A & B */
	BORDER(LABEL, 0),
	REFv(toolbar_labels[0]), MLABELxr("", 5, 2, 0),
	REFv(toolbar_labels[1]), MLABELxr("", 5, 2, 0),
	ETABLE(2, 4), BORDER(TLABEL, 0), BORDER(SPINSLIDE, 0),
	TLLABEL(_("Size"), 0, 0),
	REFv(ts_spinslides[0]), TLSPINSLIDEx(size, 1, 255, 1, 0),
	EVENT(CHANGE, ts_spinslide_moved),
	TLLABEL(_("Flow"), 0, 1),
	REFv(ts_spinslides[1]), TLSPINSLIDEx(flow, 1, 255, 1, 1),
	EVENT(CHANGE, ts_spinslide_moved),
	TLLABEL(_("Opacity"), 0, 2),
	REFv(ts_spinslides[2]), TLSPINSLIDEx(opac, 0, 255, 1, 2),
	EVENT(CHANGE, ts_spinslide_moved),
	REFv(ts_label_channel), TLLABELr("", 0, 3), HIDDEN,
	REFv(ts_spinslides[3]), TLSPINSLIDEx(chan, 0, 255, 1, 3), HIDDEN,
	EVENT(CHANGE, ts_spinslide_moved),
	WEND
};
#undef WBbase

void **create_settings_box()
{
	static settings_dd tdata; // zeroed out, get updated later
	int i;

	for (i = 0; i < TOTAL_SETTINGS; i++) // Initialize buttons' state
		v_settings[i] = *vars_settings[i];
	return (run_create(settings_code, &tdata, sizeof(tdata)));
}

static void *sbar_code[] = {
	WPWHEREVER, WINDOW(_("Settings Toolbar")),
		EVENT(CANCEL, toolbar_settings_exit),
	WXYWH("toolbar_settings", 0, 0),
	REMOUNTv(settings_dock),
	WSHOW
};

static void toolbar_settings_init()
{
	if (toolbar_boxes_[TOOLBAR_SETTINGS]) // Used when Home key is pressed
	{
		cmd_showhide(toolbar_boxes_[TOOLBAR_SETTINGS], TRUE);
		return;
	}

///	SETTINGS TOOLBAR

	toolbar_boxes_[TOOLBAR_SETTINGS] = run_create(sbar_code, sbar_code, 0);
}

static toolbar_item main_bar[] = {
	{ _("New Image"), -1, MTB_NEW, 0, XPM_ICON(new), DLG_NEW, 0 },
	{ _("Load Image File"), -1, MTB_OPEN, 0, XPM_ICON(open), DLG_FSEL, FS_PNG_LOAD },
	{ _("Save Image File"), -1, MTB_SAVE, 0, XPM_ICON(save), ACT_SAVE, 0 },
	{""},
	{ _("Cut"), -1, MTB_CUT, NEED_SEL2, XPM_ICON(cut), ACT_COPY, 1 },
	{ _("Copy"), -1, MTB_COPY, NEED_SEL2, XPM_ICON(copy), ACT_COPY, 0 },
	{ _("Paste"), -1, MTB_PASTE, NEED_CLIP, XPM_ICON(paste), ACT_PASTE, 0 },
	{""},
	{ _("Undo"), -1, MTB_UNDO, NEED_UNDO, XPM_ICON(undo), ACT_DO_UNDO, 0 },
	{ _("Redo"), -1, MTB_REDO, NEED_REDO, XPM_ICON(redo), ACT_DO_UNDO, 1 },
	{""},
	{ _("Transform Colour"), -1, MTB_BRCOSA, 0, XPM_ICON(brcosa), DLG_BRCOSA, 0 },
	{ _("Pan Window"), -1, MTB_PAN, 0, XPM_ICON(pan), ACT_PAN, 0 },
	{ NULL }};

static toolbar_item tools_bar[] = {
	{ _("Paint"), 1, TTB_PAINT, 0, XPM_ICON(paint), ACT_TOOL, TTB_PAINT },
	{ _("Shuffle"), 1, TTB_SHUFFLE, 0, XPM_ICON(shuffle), ACT_TOOL, TTB_SHUFFLE },
	{ _("Flood Fill"), 1, TTB_FLOOD, 0, XPM_ICON(flood), ACT_TOOL, TTB_FLOOD, DLG_FLOOD, 0 },
	{ _("Straight Line"), 1, TTB_LINE, 0, XPM_ICON(line), ACT_TOOL, TTB_LINE },
	{ _("Smudge"), 1, TTB_SMUDGE, NEED_24, XPM_ICON(smudge), ACT_TOOL, TTB_SMUDGE, DLG_SMUDGE, 0 },
	{ _("Clone"), 1, TTB_CLONE, 0, XPM_ICON(clone), ACT_TOOL, TTB_CLONE },
	{ _("Make Selection"), 1, TTB_SELECT, 0, XPM_ICON(select), ACT_TOOL, TTB_SELECT },
	{ _("Polygon Selection"), 1, TTB_POLY, 0, XPM_ICON(polygon), ACT_TOOL, TTB_POLY },
	{ _("Place Gradient"), 1, TTB_GRAD, 0, XPM_ICON(grad_place), ACT_TOOL, TTB_GRAD, DLG_GRAD, 0 },
	{""},
	{ _("Lasso Selection"), -1, TTB_LASSO, NEED_LAS2, XPM_ICON(lasso), ACT_LASSO, 0 },
	{ _("Paste Text"), -1, TTB_TEXT, 0, XPM_ICON(text), DLG_TEXT, 0, DLG_TEXT_FT, 0 },
	{""},
	{ _("Ellipse Outline"), -1, TTB_ELLIPSE, NEED_SEL, XPM_ICON(ellipse2), ACT_ELLIPSE, 0 },
	{ _("Filled Ellipse"), -1, TTB_FELLIPSE, NEED_SEL, XPM_ICON(ellipse), ACT_ELLIPSE, 1 },
	{ _("Outline Selection"), -1, TTB_OUTLINE, NEED_SEL2, XPM_ICON(rect1), ACT_OUTLINE, 0 },
	{ _("Fill Selection"), -1, TTB_FILL, NEED_SEL2, XPM_ICON(rect2), ACT_OUTLINE, 1 },
	{""},
	{ _("Flip Selection Vertically"), -1, TTB_SELFV, NEED_CLIP, XPM_ICON(flip_vs), ACT_SEL_FLIP_V, 0 },
	{ _("Flip Selection Horizontally"), -1, TTB_SELFH, NEED_CLIP, XPM_ICON(flip_hs), ACT_SEL_FLIP_H, 0 },
	{ _("Rotate Selection Clockwise"), -1, TTB_SELRCW, NEED_CLIP, XPM_ICON(rotate_cs), ACT_SEL_ROT, 0 },
	{ _("Rotate Selection Anti-Clockwise"), -1, TTB_SELRCCW, NEED_CLIP, XPM_ICON(rotate_as), ACT_SEL_ROT, 1 },
	{ NULL }};

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

void toolbar_init(GtkWidget *vbox_main)
{
	static char *xbm_list[TOTAL_CURSORS] = { xbm_square_bits, xbm_circle_bits,
		xbm_horizontal_bits, xbm_vertical_bits, xbm_slash_bits, xbm_backslash_bits,
		xbm_spray_bits, xbm_shuffle_bits, xbm_flood_bits, xbm_select_bits, xbm_line_bits,
		xbm_smudge_bits, xbm_polygon_bits, xbm_clone_bits, xbm_grad_bits
		},
	*xbm_mask_list[TOTAL_CURSORS] = { xbm_square_mask_bits, xbm_circle_mask_bits,
		xbm_horizontal_mask_bits, xbm_vertical_mask_bits, xbm_slash_mask_bits,
		xbm_backslash_mask_bits, xbm_spray_mask_bits, xbm_shuffle_mask_bits,
		xbm_flood_mask_bits, xbm_select_mask_bits, xbm_line_mask_bits,
		xbm_smudge_mask_bits, xbm_polygon_mask_bits, xbm_clone_mask_bits,
		xbm_grad_mask_bits
		};
	static unsigned char cursor_tip[TOTAL_CURSORS][2] = { {0, 0}, {0, 0},
		{0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {0, 0}, {2, 19},
		{10, 10}, {0, 0}, {0, 0}, {10, 10}, {0, 0}, {0, 0} };
	static GdkCursorType corners[4] = {
		GDK_TOP_LEFT_CORNER, GDK_TOP_RIGHT_CORNER,
		GDK_BOTTOM_LEFT_CORNER, GDK_BOTTOM_RIGHT_CORNER };
	GtkWidget *toolbox, *box, *mbuttons[TOTAL_ICONS_MAIN];
	int i;


	for (i=0; i<TOTAL_CURSORS; i++)
		m_cursor[i] = make_cursor(xbm_list[i], xbm_mask_list[i],
			20, 20, cursor_tip[i][0], cursor_tip[i][1]);
	move_cursor = gdk_cursor_new(GDK_FLEUR);
	busy_cursor = gdk_cursor_new(GDK_WATCH);
	for (i = 0; i < 4; i++) corner_cursor[i] = gdk_cursor_new(corners[i]);

	for ( i=1; i<TOOLBAR_MAX; i++ )
	{
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
			menu_widgets[MENU_TBMAIN - TOOLBAR_MAIN + i]),
			toolbar_status[i]);	// Menu toggles = status
	}

///	TOOLBOX WIDGET

	toolbox = pack(vbox_main, wj_size_box());
	gtk_signal_connect(GTK_OBJECT(toolbox), "size_request",
		GTK_SIGNAL_FUNC(twobar_size_req), NULL);
	gtk_signal_connect(GTK_OBJECT(toolbox), "size_allocate",
		GTK_SIGNAL_FUNC(twobar_size_alloc), NULL);

///	MAIN TOOLBAR

	toolbar_boxes[TOOLBAR_MAIN] = box =
		pack(toolbox, smart_toolbar(main_bar, mbuttons));

	toolbar_zoom_main = toolbar_add_zoom(box);
	toolbar_zoom_view = toolbar_add_zoom(box);
	/* In GTK1, combo box entry is updated continuously */
#if GTK_MAJOR_VERSION == 1
	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(toolbar_zoom_main)->popwin),
		"hide", GTK_SIGNAL_FUNC(toolbar_zoom_main_change), NULL);
	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(toolbar_zoom_view)->popwin),
		"hide", GTK_SIGNAL_FUNC(toolbar_zoom_view_change), NULL);
#else /* #if GTK_MAJOR_VERSION == 2 */
	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(toolbar_zoom_main)->entry),
		"changed", GTK_SIGNAL_FUNC(toolbar_zoom_main_change), NULL);
	gtk_signal_connect(GTK_OBJECT(GTK_COMBO(toolbar_zoom_view)->entry),
		"changed", GTK_SIGNAL_FUNC(toolbar_zoom_view_change), NULL);
#endif
	toolbar_viewzoom(FALSE);

	if (toolbar_status[TOOLBAR_MAIN])
		gtk_widget_show(box); // Only show if user wants

///	TOOLS TOOLBAR

	toolbar_boxes[TOOLBAR_TOOLS] =
		pack(toolbox, smart_toolbar(tools_bar, icon_buttons));
	if (toolbar_status[TOOLBAR_TOOLS])
		gtk_widget_show(toolbar_boxes[TOOLBAR_TOOLS]); // Only show if user wants

	/* !!! If hardcoded default tool isn't the default brush, this will crash */
	change_to_tool(TTB_PAINT);
}

void ts_update_gradient()
{
	unsigned char rgb[GP_WIDTH * GP_HEIGHT * 3], cset[3];
	unsigned char pal[256 * 3], *tmp = NULL, *dest;
	int i, j, k, op, op2, frac, slot, idx = 255, wrk[NUM_CHANNELS + 3];
	GdkPixmap *pmap;

	if (!grad_view) return;
	gtk_widget_realize(grad_view); // Ensure widget has a GC
	if (!GTK_WIDGET_REALIZED(grad_view)) return;

	slot = mem_channel + 1;
	if (mem_channel != CHN_IMAGE) /* Create pseudo palette */
	{
		for (j = 0; j < 3; j++)
		for (i = 0; i < 256; i++)
		{
			k = mem_background * 255 + (channel_rgb[mem_channel][j] -
				mem_background) * i + 127;
			pal[i * 3 + j] = (k + (k >> 8) + 1) >> 8;
		}
	}
	else if (mem_img_bpp == 1) /* Read in current palette */
	{
		memset(pal, 0, sizeof(pal));
		for (i = 0; i < 256; i++)
		{
			pal[i * 3 + 0] = mem_pal[i].red;
			pal[i * 3 + 1] = mem_pal[i].green;
			pal[i * 3 + 2] = mem_pal[i].blue;
		}
	}
	else tmp = cset , --slot; /* Use gradient colors */
	if (!IS_INDEXED) idx = 0; /* Allow intermediate opacities */

	/* Draw the preview, ignoring RGBA coupling */
	memset(rgb, mem_background, sizeof(rgb));
	for (i = 0; i < GP_WIDTH; i++)
	{
		dest = rgb + i * 3;
		wrk[CHN_IMAGE + 3] = 0;
		op = grad_value(wrk, slot, i * (1.0 / (double)(GP_WIDTH - 1)));
		if (!op) continue;
		for (j = 0; j < GP_HEIGHT; j++ , dest += GP_WIDTH * 3)
		{
			frac = BAYER(i, j);
			op2 = (op + frac) >> 8;
			if (!op2) continue;
			op2 |= idx;
			if (tmp == cset)
			{
				cset[0] = (wrk[0] + frac) >> 8;
				cset[1] = (wrk[1] + frac) >> 8;
				cset[2] = (wrk[2] + frac) >> 8;
			}
			else
			{
				k = (wrk[mem_channel + 3] + frac) >> 8;
				if (mem_channel == CHN_IMAGE) k = wrk[k];
				tmp = pal + 3 * k;
			}
			k = dest[0] * 255 + (tmp[0] - dest[0]) * op2;
			dest[0] = (k + (k >> 8) + 1) >> 8;
			k = dest[1] * 255 + (tmp[1] - dest[1]) * op2;
			dest[1] = (k + (k >> 8) + 1) >> 8;
			k = dest[2] * 255 + (tmp[2] - dest[2]) * op2;
			dest[2] = (k + (k >> 8) + 1) >> 8;
		}
	}

	/* Show it */
	gtk_pixmap_get(GTK_PIXMAP(grad_view), &pmap, NULL);
	gdk_draw_rgb_image(pmap, grad_view->style->black_gc,
		0, 0, GP_WIDTH, GP_HEIGHT,
		GDK_RGB_DITHER_NONE, rgb, GP_WIDTH * 3);
	gtk_widget_queue_draw(grad_view);
}

void toolbar_update_settings()
{
	char txt[64];
	int i, j, c, l;

	for (i = 0; i < 2; i++)
	{
		c = "AB"[i]; j = mem_col_[i];
		if (mem_img_bpp == 1) l = sprintf(txt, "%c [%d] = {%d,%d,%d}",
			c, j, mem_pal[j].red, mem_pal[j].green, mem_pal[j].blue);
		else l = sprintf(txt, "%c = {%i,%i,%i}", c, mem_col_24[i].red,
			mem_col_24[i].green, mem_col_24[i].blue);
		if (RGBA_mode && mem_img[CHN_ALPHA])
			sprintf(txt + l, " + {%d}", channel_col_[i][CHN_ALPHA]);
		cmd_setv(toolbar_labels[i], txt, LABEL_VALUE);
	}

	i = mem_channel == CHN_IMAGE;
	cmd_showhide(toolbar_labels[0], i);
	cmd_showhide(toolbar_labels[1], i);
	cmd_showhide(ts_spinslides[3], !i);
	cmd_showhide(ts_label_channel, !i);
	if (!i) cmd_setv(ts_label_channel, channames[mem_channel], LABEL_VALUE);
	// Disable opacity for indexed image
	cmd_sensitive(ts_spinslides[2], !IS_INDEXED);

	// Update tool settings
	cmd_set(ts_spinslides[0], tool_size);
	cmd_set(ts_spinslides[1], tool_flow);
	cmd_set(ts_spinslides[2], tool_opacity);
	if (!i) cmd_set(ts_spinslides[3], channel_col_A[mem_channel]);

	ts_update_gradient();
}

static png_color brcosa_palette[256];

static gboolean expose_palette(GtkWidget *widget, GdkEventExpose *event,
	gpointer user_data)
{
	int x1, y1, x2, y2, vport[4];

	wjcanvas_get_vport(widget, vport);
	x2 = (x1 = event->area.x + vport[0]) + event->area.width;
	y2 = (y1 = event->area.y + vport[1]) + event->area.height;

	/* With theme engines lurking out there, weirdest things can happen */
	if (y2 > PALETTE_HEIGHT)
	{
		gdk_draw_rectangle(widget->window, widget->style->black_gc,
			TRUE, event->area.x, PALETTE_HEIGHT - vport[1],
			event->area.width, y2 - PALETTE_HEIGHT);
		if (y1 >= PALETTE_HEIGHT) return (TRUE);
		y2 = PALETTE_HEIGHT;
	}
	if (x2 > PALETTE_WIDTH)
	{
		gdk_draw_rectangle(widget->window, widget->style->black_gc,
			TRUE, PALETTE_WIDTH - vport[0], event->area.y,
			x2 - PALETTE_WIDTH, event->area.height);
		if (x1 >= PALETTE_WIDTH) return (TRUE);
		x2 = PALETTE_WIDTH;
	}

	gdk_draw_rgb_image(widget->window, widget->style->black_gc,
		event->area.x, event->area.y, x2 - x1, y2 - y1,
		GDK_RGB_DITHER_NONE, mem_pals + y1 * PALETTE_W3 + x1 * 3, PALETTE_W3);

	return (TRUE);
}

static gboolean motion_palette(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
	GdkModifierType state;
	int x, y, pindex, vport[4];

	if (event->is_hint) gdk_window_get_pointer (event->window, &x, &y, &state);
	else
	{
		x = event->x;
		y = event->y;
		state = event->state;
	}

	/* If cursor got warped, will have another movement event to handle */
	if (drag_index && wjcanvas_bind_mouse(widget, event, x, y)) return (TRUE);

	wjcanvas_get_vport(widget, vport);

	pindex = (y + vport[1] - PALETTE_SWATCH_Y) / PALETTE_SWATCH_H;
	pindex = pindex < 0 ? 0 : pindex >= mem_cols ? mem_cols - 1 : pindex;

	if (drag_index && (drag_index_vals[1] != pindex))
	{
		mem_pal_index_move(drag_index_vals[1], pindex);
		update_stuff(UPD_MVPAL);
		drag_index_vals[1] = pindex;
	}

	return (TRUE);
}

static gboolean release_palette( GtkWidget *widget, GdkEventButton *event )
{
	if (drag_index)
	{
		drag_index = FALSE;
		gdk_window_set_cursor( drawing_palette->window, NULL );
		if ( drag_index_vals[0] != drag_index_vals[1] )
		{
			mem_pal_copy(mem_pal, brcosa_palette);	// Get old values back
			mem_undo_next(UNDO_XPAL);		// Do undo stuff
			mem_pal_index_move( drag_index_vals[0], drag_index_vals[1] );

			mem_canvas_index_move( drag_index_vals[0], drag_index_vals[1] );

			mem_undo_prepare();
			update_stuff(UPD_TPAL | CF_MENU);
		}
	}

	return FALSE;
}

static gboolean click_palette( GtkWidget *widget, GdkEventButton *event )
{
	int px, py, pindex, vport[4];


	/* Filter out multiple clicks */
	if (event->type != GDK_BUTTON_PRESS) return (TRUE);

	wjcanvas_get_vport(widget, vport);
	px = event->x + vport[0];
	py = event->y + vport[1];
	if (py < PALETTE_SWATCH_Y) return (TRUE);
	pindex = (py - PALETTE_SWATCH_Y) / PALETTE_SWATCH_H;
	if (pindex >= mem_cols) return (TRUE);

	if (px < PALETTE_SWATCH_X) colour_selector(COLSEL_EDIT_ALL + pindex);
	else if (px < PALETTE_CROSS_X)		// Colour A or B changed
	{
		if ((event->button == 1) && (event->state & GDK_SHIFT_MASK))
		{
			mem_pal_copy(brcosa_palette, mem_pal);
			drag_index = TRUE;
			drag_index_vals[0] = drag_index_vals[1] = pindex;
			gdk_window_set_cursor(drawing_palette->window, move_cursor);
		}
		else if ((event->button == 1) || (event->button == 3))
		{
			int ab = (event->button == 3) || (event->state & GDK_CONTROL_MASK);

			mem_col_[ab] = pindex;
			mem_col_24[ab] = mem_pal[pindex];
			update_stuff(UPD_AB);
		}
	}
	else /* if (px >= PALETTE_CROSS_X) */		// Mask changed
	{
		mem_prot_mask[pindex] ^= 255;
		mem_mask_init();		// Prepare RGB masks
		update_stuff(UPD_CMASK);
	}

	return (TRUE);
}

void toolbar_palette_init(GtkWidget *box)		// Set up the palette area
{
	GtkWidget *vbox, *hbox, *scrolledwindow_palette, *viewport_palette;


	toolbar_boxes[TOOLBAR_PALETTE] = vbox = pack(box, gtk_vbox_new(FALSE, 0));
	if (toolbar_status[TOOLBAR_PALETTE]) gtk_widget_show(vbox);

	hbox = pack5(vbox, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox);

	drawing_col_prev = wjcanvas_new();
	gtk_widget_show(drawing_col_prev);
	wjcanvas_size(drawing_col_prev, PREVIEW_WIDTH, PREVIEW_HEIGHT);

	gtk_signal_connect_object( GTK_OBJECT(drawing_col_prev), "button_release_event",
		GTK_SIGNAL_FUNC (click_colours), GTK_OBJECT(drawing_col_prev) );
	gtk_signal_connect_object( GTK_OBJECT(drawing_col_prev), "expose_event",
		GTK_SIGNAL_FUNC (expose_preview), GTK_OBJECT(drawing_col_prev) );
	gtk_widget_set_events (drawing_col_prev, GDK_ALL_EVENTS_MASK);

	viewport_palette = wjframe_new();
	gtk_widget_show(viewport_palette);
	gtk_container_add(GTK_CONTAINER(viewport_palette), drawing_col_prev);
	gtk_box_pack_start(GTK_BOX(hbox), viewport_palette, TRUE, FALSE, 0);

	scrolledwindow_palette = xpack(vbox, gtk_scrolled_window_new(NULL, NULL));
	gtk_widget_show (scrolledwindow_palette);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow_palette),
		GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

	drawing_palette = wjcanvas_new();
	gtk_widget_show(drawing_palette);
	wjcanvas_size(drawing_palette, PALETTE_WIDTH, 64);
	add_with_wjframe(scrolledwindow_palette, drawing_palette);

	gtk_signal_connect_object( GTK_OBJECT(drawing_palette), "expose_event",
		GTK_SIGNAL_FUNC (expose_palette), GTK_OBJECT(drawing_palette) );
	gtk_signal_connect_object( GTK_OBJECT(drawing_palette), "button_press_event",
		GTK_SIGNAL_FUNC (click_palette), GTK_OBJECT(drawing_palette) );
	gtk_signal_connect_object( GTK_OBJECT(drawing_palette), "motion_notify_event",
		GTK_SIGNAL_FUNC (motion_palette), GTK_OBJECT(drawing_palette) );
	gtk_signal_connect_object( GTK_OBJECT(drawing_palette), "button_release_event",
		GTK_SIGNAL_FUNC (release_palette), GTK_OBJECT(drawing_palette) );

	gtk_widget_set_events (drawing_palette, GDK_ALL_EVENTS_MASK);
}

void toolbar_showhide()				// Show/Hide all 4 toolbars
{
	static const unsigned char bar[4] =
		{ TOOLBAR_MAIN, TOOLBAR_TOOLS, TOOLBAR_PALETTE, TOOLBAR_STATUS };
	int i;

	if (!toolbar_boxes[TOOLBAR_MAIN]) return;	// Grubby hack to avoid segfault

	// Don't touch regular toolbars in view mode
	if (!view_image_only) for (i = 0; i < 4; i++)
		widget_showhide(toolbar_boxes[bar[i]], toolbar_status[bar[i]]);

	if (!toolbar_status[TOOLBAR_SETTINGS])
		toolbar_settings_exit(NULL, NULL);
	else
	{
		toolbar_settings_init();
		gdk_window_raise(main_window->window);
	}
}


void pressed_toolbar_toggle(int state, int which)
{						// Menu toggle for toolbars
	toolbar_status[which] = state;
	toolbar_showhide();
}



///	PATTERNS/TOOL PREVIEW AREA


void mem_set_brush(int val)			// Set brush, update size/flow/preview
{
	int offset, j, o, o2;

	brush_type = mem_brush_list[val][0];
	tool_size = mem_brush_list[val][1];
	if ( mem_brush_list[val][2]>0 ) tool_flow = mem_brush_list[val][2];

	offset = 3*( 2 + 36*(val % 9) + 36*PATCH_WIDTH*(val / 9) + 2*PATCH_WIDTH );
			// Offset in brush RGB
	for ( j=0; j<32; j++ )
	{
		o = 3*(40 + PREVIEW_WIDTH*j);		// Preview offset
		o2 = offset + 3*PATCH_WIDTH*j;		// Offset in brush RGB
		memcpy(mem_prev + o, mem_brushes + o2, 32 * 3);
	}
}

#include "graphics/xbm_patterns.xbm"
#if (PATTERN_GRID_W * 8 != xbm_patterns_width) || (PATTERN_GRID_H * 8 != xbm_patterns_height)
#error "Mismatched patterns bitmap"
#endif

/* Create RGB dump of patterns to display, with each pattern repeated 4x4 */
unsigned char *render_patterns()
{
	png_color *p;
	unsigned char *buf, *dest;
	int i = 0, j, x, y, h, b;

#define PAT_ROW_L (PATTERN_GRID_W * (8 * 4 + 4) * 3)
#define PAT_8ROW_L (8 * PAT_ROW_L)
	buf = calloc(1, PATTERN_GRID_H * (8 * 4 + 4) * PAT_ROW_L);
	dest = buf + 2 * PAT_ROW_L + 2 * 3;
	for (y = 0; y < PATTERN_GRID_H; y++ , dest += (8 * 3 + 4) * PAT_ROW_L)
	{
		for (h = 0; h < 8; h++)
		for (x = 0; x < PATTERN_GRID_W; x++ , dest += (8 * 3 + 4) * 3)
		{
			b = xbm_patterns_bits[i++];
			for (j = 0; j < 8; j++ , b >>= 1)
			{
				p = mem_col_24 + (b & 1);
				*dest++ = p->red;
				*dest++ = p->green;
				*dest++ = p->blue;
			}
			memcpy(dest, dest - 8 * 3, 8 * 3);
			memcpy(dest + 8 * 3, dest - 8 * 3, 2 * 8 * 3);
		}
		memcpy(dest, dest - PAT_8ROW_L, PAT_8ROW_L);
		memcpy(dest + PAT_8ROW_L, dest - PAT_8ROW_L, 2 * PAT_8ROW_L);
	}
#undef PAT_8ROW_L
#undef PAT_ROW_L
	return (buf);
}

/* Set 0-1 indexed image as new patterns */
void set_patterns(unsigned char *src)
{
	int i, j, b, l = PATTERN_GRID_W * PATTERN_GRID_H * 8;

	for (i = 0; i < l; i++)
	{
		for (b = j = 0; j < 8; j++) b += *src++ << j;
		xbm_patterns_bits[i] = b;
	}
}

void mem_pat_update()			// Update indexed and then RGB pattern preview
{
	int i, j, k, l, ii, b;

	if ( mem_img_bpp == 1 )
	{
		mem_col_A24 = mem_pal[mem_col_A];
		mem_col_B24 = mem_pal[mem_col_B];
	}

	/* Pattern bitmap starts here */
	l = mem_tool_pat * 8 - (mem_tool_pat % PATTERN_GRID_W) * 7;

	/* Set up pattern maps from XBM */
	for (i = ii = 0; i < 8; i++)
	{
		b = xbm_patterns_bits[l + i * PATTERN_GRID_W];
		for (k = 0; k < 8; k++ , ii++ , b >>= 1)
		{
			mem_pattern[ii] = j = b & 1;
			mem_col_pat[ii] = mem_col_[j];
			mem_col_pat24[ii * 3 + 0] = mem_col_24[j].red;
			mem_col_pat24[ii * 3 + 1] = mem_col_24[j].green;
			mem_col_pat24[ii * 3 + 2] = mem_col_24[j].blue;
		}
	}

	k = PREVIEW_WIDTH * 32 * 3;
	for (i = 0; i < 16; i++)
	{
		for (j = 0; j < PREVIEW_WIDTH; j++ , k += 3)
		{
			l = ((i & 7) * 8 + (j & 7)) * 3;
			mem_prev[k + 0] = mem_col_pat24[l + 0];
			mem_prev[k + 1] = mem_col_pat24[l + 1];
			mem_prev[k + 2] = mem_col_pat24[l + 2];
		}
	}
}

void update_top_swatch()			// Update selected colours A & B
{
	unsigned char AA[3], BB[3], *tmp;
	int i, j;

	AA[0] = mem_col_A24.red;
	AA[1] = mem_col_A24.green;
	AA[2] = mem_col_A24.blue;
	BB[0] = mem_col_B24.red;
	BB[1] = mem_col_B24.green;
	BB[2] = mem_col_B24.blue;

#define dAB (10 * PREVIEW_WIDTH * 3 + 10 * 3)
	for (j = 1; j <= 20; j++)
	{
		tmp = (mem_prev + 1 * 3) + j * (PREVIEW_WIDTH * 3);
		for (i = 0; i < 20; i++ , tmp += 3)
		{
			tmp[0] = AA[0];
			tmp[1] = AA[1];
			tmp[2] = AA[2];

			tmp[dAB + 0] = BB[0];
			tmp[dAB + 1] = BB[1];
			tmp[dAB + 2] = BB[2];
		}
	}
#undef dAB
}

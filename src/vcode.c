/*	vcode.c
	Copyright (C) 2013 Dmitry Groshev

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
#include "vcode.h"

/// BYTECODE ENGINE

/* !!! Warning: handlers should not access datastore after window destruction!
 * GTK+ refs objects for signal duration, but no guarantee every other toolkit
 * will behave alike - WJ */

static void get_evt_1(GtkObject *widget, gpointer user_data)
{
	void **slot = user_data;
	void **base = slot[0], **desc = slot[1];

	((evt_fn)desc[1])(GET_DDATA(base), base, (int)desc[0] & 0xFFFF, slot);
}

static void **add_click(void **r, void **res, void **pp, GtkWidget *widget,
	GtkWidget *window)
{
	// default to destructor
	if (!pp[1]) gtk_signal_connect_object(GTK_OBJECT(widget), "clicked",
		GTK_SIGNAL_FUNC(gtk_widget_destroy), GTK_OBJECT(window));
	else
	{
		r[0] = res;
		r[1] = pp;
		gtk_signal_connect(GTK_OBJECT(widget), "clicked",
			GTK_SIGNAL_FUNC(get_evt_1), r);
		r += 2;
	}
	return (r);
}

static void **skip_if(void **pp)
{
	int lp, mk;
	void **ifcode;

	ifcode = pp + 1 + (lp = (int)*pp >> 16);
	if (lp > 1) // skip till corresponding ENDIF
	{
		mk = (int)pp[1];
		while ((((int)*ifcode & 0xFFFF) != op_ENDIF) ||
			((int)ifcode[1] != mk))
			ifcode += 1 + ((int)*ifcode >> 16);
	}
	return (ifcode + 1 + ((int)*ifcode >> 16));
}

#define GET_OP(S) ((int)*(void **)(S)[1] & 0xFFFF)

/* From event to its originator */
static void **origin_slot(void **slot)
{
	while (((int)*(void **)slot[1] & 0xFFFF) >= op_EVT_0) slot -= 2;
	return (slot);
}

/* Trigger events which need triggering */
void trigger_things(void **wdata)
{
	char *data = GET_DDATA(wdata);
	void **slot, **desc;

	for (wdata = GET_WINDOW(wdata); wdata[1]; wdata += 2)
	{
		if (GET_OP(wdata) != op_TRIGGER) continue;
		slot = wdata - 2;
		desc = slot[1];
		((evt_fn)desc[1])(data, slot[0], (int)desc[0] & 0xFFFF, slot);
	}
}

static void table_it(GtkWidget *table, GtkWidget *it, int wh)
{
	to_table_l(it, table, wh & 255, (wh >> 8) & 255, (wh >> 16) + 1, 0);
}

/* Find where unused rows start */
static int next_table_level(GtkWidget *table)
{
	GList *item;
	int y, n = 0;
	for (item = GTK_TABLE(table)->children; item; item = item->next)
	{
		y = ((GtkTableChild *)item->data)->bottom_attach;
		if (n < y) n = y;
	}
	return (n);
}

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
		if (requisition->width < n) requisition->width = n;
		n = wreq.height + border;
		if (requisition->height < n) requisition->height = n;
	}
}

#if U_NLS

/* Translate array of strings */
static int n_trans(char **dest, char **src, int n)
{
	int i;
	for (i = 0; (i != n) && src[i]; i++) dest[i] = _(src[i]);
	return (i);
}

#endif

enum {
	pk_NONE = 0,
	pk_PACK,
	pk_PACK5,
	pk_XPACK,
	pk_TABLE,
	pk_TABLE2,
	pk_TABLE2x
};
#define pk_MASK    0xFF
#define pkf_FRAME 0x100
#define pkf_STACK 0x200
#define pkf_SLOT  0x400

/* Bytecode is really simple-minded; it can do 0-tests but no arithmetics, and
 * naturally, can inline only constants. Everything else must be prepared either
 * in global variables, or in fields of "ddata" structure.
 * Parameters of codes should be arrayed in fixed order:
 * result location first; frame name last; table location, or name in table,
 * directly before it (last if no frame name) */

#define DEF_BORDER 5
#define GET_BORDER(T) (borders[op_BOR_##T - op_BOR_0] + DEF_BORDER)

void **run_create(void **ifcode, int ifsize, void *ddata, int ddsize)
{
	static const int scrollp[3] = { GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC,
		GTK_POLICY_ALWAYS };
#if U_NLS
	char *tc[256];
#endif
#if GTK_MAJOR_VERSION == 1
	int have_sliders = FALSE;
#endif
	char txt[PATHTXT];
	int borders[op_BOR_LAST - op_BOR_0], wpos = GTK_WIN_POS_CENTER;
	GtkWidget *wstack[128], **wp = wstack + 128;
	GtkWidget *tw, *window = NULL, *widget = NULL;
	GtkAccelGroup* ag = NULL;
	unsigned char *dstore = NULL;
	void **r = NULL, **res = NULL;
	void **pp, **ifend = (void **)((char *)ifcode + ifsize);
	int ld = (ddsize + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
	int n, op, lp, dsize, pk, tpad = 0;


	// Array of data widgets cannot outgrow original bytecode
	dsize = ld + sizeof(void *) * 2 + ifsize;

	// Border sizes are DEF_BORDER-based
	memset(borders, 0, sizeof(borders));

	while (ifend - ifcode > 0)
	{
		op = (int)*ifcode++;
		ifcode = (pp = ifcode) + (lp = op >> 16);
		pk = pkf_SLOT;
		switch (op &= 0xFFFF)
		{
		/* Terminate */
		case op_WEND: case op_WSHOW:
			/* !!! For now, done unconditionally */
			gtk_window_set_transient_for(GTK_WINDOW(window),
				GTK_WINDOW(main_window));
#if GTK_MAJOR_VERSION == 1
			/* To make Smooth theme engine render sliders properly */
			if (have_sliders) gtk_signal_connect_after(
				GTK_OBJECT(window), "show",
				GTK_SIGNAL_FUNC(gtk_widget_queue_resize), NULL);
#endif
			/* Trigger remembered events */
			trigger_things(res);
			/* Display */
			if (op == op_WSHOW) gtk_widget_show(window);
			ifcode = ifend;
			continue;
		/* Done with a container */
		case op_WDONE: ++wp; continue;
		/* Create a toplevel window, bind datastore to it, and
		 * put a vertical box inside it */
		case op_WINDOW: case op_WINDOWm:
			window = add_a_window(GTK_WINDOW_TOPLEVEL, _(pp[0]),
				wpos, op == op_WINDOWm);
			dstore = bound_malloc(window, dsize);
			memcpy(dstore, ddata, ddsize); // Copy datastruct
			res = r = (void **)(dstore + ld); // Anchor after it
			*r++ = dstore; // Store struct ref at anchor
			*r++ = window; // Store window ref right next to it
			*r++ = pp - 1; // And slot ref after it
			*--wp = add_vbox(window);
			continue;
		/* Add a notebook page */
		case op_PAGE:
			--wp; wp[0] = add_new_page(wp[1], _(pp[0]));
			continue;
		/* Add a table */
		case op_TABLE: case op_TABLEr:
			--wp; wp[0] = widget = add_a_table((int)pp[0] & 0xFFFF,
				(int)pp[0] >> 16, GET_BORDER(TABLE), wp[1]);
			if (op != op_TABLEr) pk = 0; // !referrable widget
			break;
		/* Add a horizontal box */
		case op_HBOX: case op_TLHBOX:
			widget = gtk_hbox_new(FALSE, 0);
			gtk_widget_show(widget);
// !!! Padding = 0
			pk = pk_PACK | pkf_STACK;
			if (op == op_TLHBOX) pk = pk_TABLE | pkf_STACK;
			break;
		/* Add a framed vertical box */
		case op_FVBOX:
			widget = gtk_vbox_new(FALSE, lp > 1 ? (int)pp[0] : 0);
			gtk_widget_show(widget);
// !!! Border = 5
			gtk_container_set_border_width(GTK_CONTAINER(widget), 5);
			pk = pk_PACK | pkf_FRAME | pkf_STACK;
			break;
		/* Add a scrolled window */
		case op_SCROLL:
			widget = gtk_scrolled_window_new(NULL, NULL);
			gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),
				scrollp[(int)pp[0] & 255], scrollp[(int)pp[0] >> 8]);
			gtk_widget_show(widget);
			pk = pk_XPACK | pkf_STACK;
			break;
		/* Put a notebook into scrolled window */
		case op_SNBOOK:
			tw = *wp++; // unstack the box
			--wp; wp[0] = gtk_notebook_new();
			gtk_notebook_set_tab_pos(GTK_NOTEBOOK(wp[0]), GTK_POS_LEFT);
//			gtk_notebook_set_scrollable(GTK_NOTEBOOK(wp[0]), TRUE);
			gtk_scrolled_window_add_with_viewport(
				GTK_SCROLLED_WINDOW(tw), wp[0]);
			tw = GTK_BIN(tw)->child;
			gtk_viewport_set_shadow_type(GTK_VIEWPORT(tw), GTK_SHADOW_NONE);
			gtk_widget_show_all(tw);
			vport_noshadow_fix(tw);
			continue;
		/* Add a horizontal line */
		case op_HSEP:
// !!! Height = 10
			add_hseparator(wp[0], lp ? (int)pp[0] : -2, 10);
			continue;
		/* Add a (multiline) label */
		case op_MLABEL:
			widget = gtk_label_new(_(pp[0]));
			gtk_widget_show(widget);
			pk = pk_PACK;
			break;
		/* Add a label to table slot */
		case op_TLLABEL:
		{
			int wh = (int)pp[1];
// !!! Padding = 5 Cells = 1
			add_to_table_l(_(pp[0]), wp[0],
				wh & 255, (wh >> 8) & 255, 1, 5);
			continue;
		}
		/* Add a named spin to table, fill from field/var */
		case op_TSPIN: case op_TSPINv: case op_TSPINa:
		{
			int *xp = op == op_TSPINv ? pp[0] :
				(int *)((char *)ddata + (int)pp[0]);
			tpad = GET_BORDER(TSPIN);
			widget = add_a_spin(*xp,
				op == op_TSPINa ? xp[1] : (int)pp[1],
				op == op_TSPINa ? xp[2] : (int)pp[2]);
			pk = pk_TABLE2 | pkf_SLOT;
			break;
		}
		/* Add a spin to box, fill from field */
		case op_SPIN:
			widget = add_a_spin(*(int *)((char *)ddata + (int)pp[0]),
				(int)pp[1], (int)pp[2]);
// !!! Padding = 5
			pk = pk_PACK5 | pkf_SLOT;
			break;
		/* Add an expandable spin to box, fill from field array */
		case op_XSPINa:
		{
			int *xp = (int *)((char *)ddata + (int)pp[0]);
			widget = add_a_spin(xp[0], xp[1], xp[2]);
			pk = pk_XPACK | pkf_SLOT;
			break;
		}
		/* Add a named spinslider to table, fill from field */
		case op_TSPINSLIDE:
// !!! Padding = 0
			tpad = 0;
// !!! Width = 255 Height = 20
			widget = mt_spinslide_new(255, 20);
			mt_spinslide_set_range(widget, (int)pp[1], (int)pp[2]);
			mt_spinslide_set_value(widget,
				*(int *)((char *)ddata + (int)pp[0]));
#if GTK_MAJOR_VERSION == 1
			have_sliders = TRUE;
#endif
			pk = pk_TABLE2x | pkf_SLOT;
			break;
		/* Add a named checkbox, fill from field/var */
		case op_CHECK: case op_CHECKv: case op_TLCHECK: case op_TLCHECKv:
		{
			int *xp = (op == op_CHECKv) || (op == op_TLCHECKv) ?
				pp[0] : (int *)((char *)ddata + (int)pp[0]);
			widget = sig_toggle(_(pp[1]), *xp, NULL, NULL);
// !!! Padding = 0
			pk = pk_PACK | pkf_SLOT;
			if (op >= op_TLCHECK) pk = pk_TABLE | pkf_SLOT;
			break;
		}
		/* Add a named checkbox, fill from inifile */
		case op_CHECKb:
			widget = sig_toggle(_(pp[2]), inifile_get_gboolean(pp[0],
				(int)pp[1]), NULL, NULL);
			pk = pk_PACK | pkf_SLOT;
			break;
		/* Add a (self-reading) pack of radio buttons for field/var */
		case op_RPACK: case op_RPACKv: case op_RPACKd: case op_FRPACK:
		{
			char **src = pp[1];
			int nh = (int)pp[2], *v = op == op_RPACKv ? pp[0] :
				(int *)(dstore + (int)pp[0]);
			int n = nh >> 8;
			if (op == op_RPACKd)
				src = *(char ***)(dstore + (int)pp[1]) , n = -1;
#if U_NLS
			n = n_trans(tc, src, n);
			src = tc;
#endif
			widget = wj_radio_pack(src, n, nh & 255, *v, v, NULL);
			pk = pk_XPACK | pkf_SLOT;
			if (op == op_FRPACK)
			{
// !!! Border = 5
				gtk_container_set_border_width(
					GTK_CONTAINER(widget), 5);
				pk = pk_PACK | pkf_FRAME | pkf_SLOT;
			}
			break;
		}
		/* Add an option menu for field/var */
		case op_OPT: case op_TLOPTv:
		{
			void *hs = pp[4]; // event handler
			char **src = pp[1];
			int v = op == op_TLOPTv ? (int)pp[0] :
				*(int *)((char *)ddata + (int)pp[0]);
#if U_NLS
			n_trans(tc, src, (int)pp[2]);
			src = tc;
#endif
			widget = wj_option_menu(src, (int)pp[2], v,
				hs ? r + 2 : NULL,
				hs ? GTK_SIGNAL_FUNC(get_evt_1) : NULL);
			*r++ = widget;
			*r++ = pp - 1;
			if (hs) *r++ = res , *r++ = pp + 3; // event
// !!! Padding = 0
			pk = op == op_TLOPTv ? pk_TABLE : pk_PACK;
			break;
		}
		/* Add a path box, fill from var/inifile */
		case op_PATHv: case op_PATHs:
			widget = mt_path_box(_(pp[1]), wp[0], _(pp[2]),
				(int)pp[3]);
			gtkuncpy(txt, op == op_PATHs ? inifile_get(pp[0], "") :
				pp[0], PATHTXT);
			gtk_entry_set_text(GTK_ENTRY(widget), txt);
//			pk = pkf_SLOT;
			break;
		/* Add a box with "OK"/"Cancel", or something like */
		case op_OKBOX:
		{
			GtkWidget *hbox, *ok_bt, *cancel_bt;

			ag = gtk_accel_group_new();
 			gtk_window_add_accel_group(GTK_WINDOW(window), ag);

			--wp; wp[0] = hbox = pack(wp[1], gtk_hbox_new(TRUE, 0));
			gtk_container_set_border_width(GTK_CONTAINER(hbox),
				GET_BORDER(OKBOX));
			gtk_widget_show(hbox);
			if (!lp) continue; // empty box for separate buttons
			*r++ = hbox;
			*r++ = pp - 1;

			ok_bt = cancel_bt = gtk_button_new_with_label(_(pp[0]));
			gtk_container_set_border_width(GTK_CONTAINER(ok_bt),
				GET_BORDER(OKBTN));
			gtk_widget_show(ok_bt);
			/* OK-event */
			r = add_click(r, res, pp + 2, ok_bt, window);
			if (pp[1])
			{
				cancel_bt = xpack(hbox,
					gtk_button_new_with_label(_(pp[1])));
				gtk_container_set_border_width(
					GTK_CONTAINER(cancel_bt), GET_BORDER(OKBTN));
				gtk_widget_show(cancel_bt);
				/* Cancel-event */
				r = add_click(r, res, pp + 4, cancel_bt, window);
			}
			xpack(hbox, ok_bt);

			gtk_widget_add_accelerator(cancel_bt, "clicked", ag,
				GDK_Escape, 0, (GtkAccelFlags)0);
			gtk_widget_add_accelerator(ok_bt, "clicked", ag,
				GDK_Return, 0, (GtkAccelFlags)0);
			gtk_widget_add_accelerator(ok_bt, "clicked", ag,
				GDK_KP_Enter, 0, (GtkAccelFlags)0);
			delete_to_click(window, cancel_bt);
			continue;
		}
		/* Add a clickable button to OK-box */
		case op_OKBTN: case op_CANCELBTN: case op_OKADD: case op_OKNEXT:
		{
			*r++ = widget = xpack(wp[0],
				gtk_button_new_with_label(_(pp[0])));
			*r++ = pp - 1;
			gtk_container_set_border_width(GTK_CONTAINER(widget),
				GET_BORDER(OKBTN));
			if (op == op_OKADD) gtk_box_reorder_child(GTK_BOX(wp[0]),
				widget, 1);
			gtk_widget_show(widget);
			if (op == op_OKBTN)
			{
				gtk_widget_add_accelerator(widget, "clicked", ag,
					GDK_Return, 0, (GtkAccelFlags)0);
				gtk_widget_add_accelerator(widget, "clicked", ag,
					GDK_KP_Enter, 0, (GtkAccelFlags)0);
			}
			else if (op == op_CANCELBTN)
			{
				gtk_widget_add_accelerator(widget, "clicked", ag,
					GDK_Escape, 0, (GtkAccelFlags)0);
				delete_to_click(window, widget);
			}
			/* Click-event */
			r = add_click(r, res, pp + 1, widget, window);
			continue;
		}
		/* Call a function */
		case op_EXEC:
			r = ((ext_fn)pp[0])(r, &wp);
			continue;
		/* Skip next token(s) if/unless field is unset */
		case op_IF: case op_UNLESS:
			if (!*(int *)((char *)ddata + (int)pp[0]) ^ (op != op_IF))
				ifcode = skip_if(pp - 1);
			continue;
		/* Skip next token(s) if/unless var is unset */
		case op_IFv: case op_UNLESSv:
			if (!*(int *)pp[0] ^ (op != op_IFv))
				ifcode = skip_if(pp - 1);
			continue;
		/* Store a reference to whatever is next into field */
		case op_REF:
			*(void **)(dstore + (int)pp[0]) = r;
			continue;
		/* Make toplevel window shrinkable */
		case op_MKSHRINK:
			gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
			continue;
		/* Make toplevel window non-resizable */
		case op_NORESIZE:
			gtk_window_set_policy(GTK_WINDOW(window), FALSE, FALSE, TRUE);
			continue;
		/* Make scrolled window request max size */
		case op_WANTMAX:
			gtk_signal_connect(GTK_OBJECT(widget), "size_request",
				GTK_SIGNAL_FUNC(scroll_max_size_req), NULL);
			continue;
		/* Set default width for window */
		case op_DEFW:
			gtk_window_set_default_size(GTK_WINDOW(window),
				(int)pp[0], -1);
			continue;
		/* Make toplevel window be positioned at mouse */
		case op_WPMOUSE: wpos = GTK_WIN_POS_MOUSE; continue;
		/* Make last referrable widget insensitive */
		case op_INSENS:
			gtk_widget_set_sensitive(*origin_slot(r - 2), FALSE);
			continue;
#if 0
		/* Set border on container widget */
		case op_SETBORDER:
			gtk_container_set_border_width(GTK_CONTAINER(wp[0])),
				(int)pp[0]);
			continue;
#endif
		/* Install Change-event handler */
		case op_EVT_CHANGE:
		{
			void **slot = origin_slot(r - 2);
			int what = GET_OP(slot);
// !!! Support only what actually used on, and their brethren
			switch (what)
			{
			case op_SPIN: case op_XSPINa: case op_TSPIN:
			case op_TSPINa: case op_TSPINv:
				spin_connect(*slot,
					GTK_SIGNAL_FUNC(get_evt_1), r);
				break;
			case op_TSPINSLIDE:
				mt_spinslide_connect(*slot,
					GTK_SIGNAL_FUNC(get_evt_1), r);
				break;
			case op_CHECK: case op_CHECKv: case op_TLCHECK:
			case op_TLCHECKv: case op_CHECKb:
				gtk_signal_connect(GTK_OBJECT(*slot), "toggled",
					GTK_SIGNAL_FUNC(get_evt_1), r);
				break;
			}
		} // fallthrough
		/* Remember that event needs triggering here */
		case op_TRIGGER:
			*r++ = res;
			*r++ = pp - 1;
			continue;
		/* Set nondefault border size */
		case op_BOR_TABLE: case op_BOR_TSPIN: case op_BOR_OKBOX:
		case op_BOR_OKBTN:
			borders[op - op_BOR_0] = (int)pp[0] - DEF_BORDER;
			continue;
		default: continue;
		}
		/* Remember this */
		if (pk & pkf_SLOT)
		{
			*r++ = widget;
			*r++ = pp - 1;
		}
		*(wp - 1) = widget; // pre-stack
		/* Frame this */
		if (pk & pkf_FRAME)
			widget = add_with_frame(NULL, _(pp[--lp]), widget);
		/* Pack this */
		switch (n = pk & pk_MASK)
		{
		case pk_PACK: pack(wp[0], widget); break;
		case pk_PACK5: pack5(wp[0], widget); break;
		case pk_XPACK: xpack(wp[0], widget); break;
		case pk_TABLE: table_it(wp[0], widget, (int)pp[--lp]); break;
		case pk_TABLE2: case pk_TABLE2x:
		{
			int y = next_table_level(wp[0]);
			add_to_table(_(pp[--lp]), wp[0], y, 0, tpad);
			gtk_table_attach(GTK_TABLE(wp[0]), widget, 1, 2,
				y, y + 1, GTK_EXPAND | GTK_FILL,
				n == pk_TABLE2x ? GTK_FILL : 0, 0, tpad);
			break;
		}
		}
		/* Stack this */
		if (pk & pkf_STACK) --wp;
	}

	/* Return anchor position */
	return (res);
}

static void *do_query(char *data, void **wdata, int mode)
{
	void **pp, *v = NULL;
	int op;

	for (; (pp = wdata[1]); wdata += 2)
	{
		op = (int)*pp++;
		switch (op &= 0xFFFF)
		{
		case op_SPIN: case op_XSPINa:
		case op_TSPIN: case op_TSPINa: case op_TSPINv:
			v = op == op_TSPINv ? pp[0] : data + (int)pp[0];
			*(int *)v = mode & 1 ? gtk_spin_button_get_value_as_int(
				GTK_SPIN_BUTTON(*wdata)) : read_spin(*wdata);
			break;
		case op_TSPINSLIDE:
			v = data + (int)pp[0];
			*(int *)v = (mode & 1 ? mt_spinslide_read_value :
				mt_spinslide_get_value)(*wdata);
			break;
		case op_CHECK: case op_TLCHECK:
			v = data + (int)pp[0];
			*(int *)v = gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(*wdata));
			break;
		case op_CHECKv: case op_TLCHECKv:
			v = pp[0];
			*(int *)v = gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(*wdata));
			break;
		case op_CHECKb:
			v = pp[0];
			inifile_set_gboolean(v, gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(*wdata)));
			break;
		case op_RPACK: case op_RPACKd: case op_FRPACK:
			v = data + (int)pp[0];
			break; // self-reading
		case op_RPACKv:
			v = pp[0];
			break; // self-reading
		case op_OPT: case op_TLOPTv:
			v = op == op_TLOPTv ? pp[0] : data + (int)pp[0];
			*(int *)v = wj_option_menu_get_history(*wdata);
			break;
		case op_PATHv:
			v = pp[0];
			gtkncpy(v, gtk_entry_get_text(GTK_ENTRY(*wdata)), PATHBUF);
			break;
		case op_PATHs:
		{
			char path[PATHBUF];
			gtkncpy(path, gtk_entry_get_text(GTK_ENTRY(*wdata)), PATHBUF);
			v = pp[0];
			inifile_set(v, path);
			break;
		}
		}
		if (mode > 1) return (v);
	}
	return (NULL);
}

void run_query(void **wdata)
{
	do_query(GET_DDATA(wdata), GET_WINDOW(wdata), 0);
}

void cmd_sensitive(void **slot, int state)
{
	if (GET_OP(slot) < op_EVT_0) // any widget
		gtk_widget_set_sensitive(slot[0], state);
}

void cmd_showhide(void **slot, int state)
{
	if (GET_OP(slot) < op_EVT_0) // any widget
		widget_showhide(slot[0], state);
}

void cmd_set(void **slot, int v)
{
	if (GET_OP(slot) == op_TSPINSLIDE) // only spinsliders for now
		mt_spinslide_set_value(slot[0], v);
}

void cmd_set3(void **slot, int *v)
{
	if (GET_OP(slot) == op_TSPINSLIDE) // only spinsliders for now
	{
		mt_spinslide_set_range(slot[0], v[1], v[2]);
		mt_spinslide_set_value(slot[0], v[0]);
	}
}

/* Passively query one slot, show where the result went */
void *cmd_read(void **slot, void *ddata)
{
	return (do_query(ddata, origin_slot(slot), 3));
}

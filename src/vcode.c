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

static void get_evt_1(GtkWidget *widget, gpointer user_data)
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

/* Bytecode is really simple-minded; it can do 0-tests but no arithmetics, and
 * naturally, can inline only constants. Everything else must be prepared either
 * in global variables, or in fields of "ddata" structure */

#define DEF_BORDER 5
#define GET_BORDER(T) (borders[op_BOR_##T - op_BOR_0] + DEF_BORDER)

void **run_create(void **ifcode, int ifsize, void *ddata, int ddsize)
{
	static const int scrollp[3] = { GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC,
		GTK_POLICY_ALWAYS };
	char txt[PATHTXT];
	int borders[op_BOR_LAST - op_BOR_0];
	GtkWidget *wstack[128], **wp = wstack + 128;
	GtkWidget *window = NULL, *widget = NULL;
	unsigned char *dstore = NULL;
	void **r = NULL, **res = NULL;
	void **pp, **ifend = (void **)((char *)ifcode + ifsize);
	int ld = (ddsize + sizeof(void *) - 1) & ~(sizeof(void *) - 1);
	int op, lp, dsize;


	// Array of data widgets cannot outgrow original bytecode
	dsize = ld + sizeof(void *) * 2 + ifsize;

	// Border sizes are DEF_BORDER-based
	memset(borders, 0, sizeof(borders));

	while (ifend - ifcode > 0)
	{
		op = (int)*ifcode++;
		ifcode = (pp = ifcode) + (lp = op >> 16);
		switch (op &= 0xFFFF)
		{
		/* Terminate */
		case op_WEND: ifcode = ifend; break;
		/* Done with a container */
		case op_WDONE: ++wp; break;
		/* Create a toplevel window, bind datastore to it, and
		 * put a vertical box inside it */
		case op_WINDOW:
			window = add_a_window(GTK_WINDOW_TOPLEVEL, _(pp[0]),
				GTK_WIN_POS_CENTER, !!pp[1]);
			dstore = bound_malloc(window, dsize);
			memcpy(dstore, ddata, ddsize); // Copy datastruct
			res = r = (void **)(dstore + ld); // Anchor after it
			*r++ = dstore; // Store struct ref at anchor
			*r++ = window; // Store window ref right next to it
			*r++ = pp - 1; // And slot ref after it
			*--wp = add_vbox(window);
			break;
		/* Add a notebook page */
		case op_PAGE:
			--wp; wp[0] = add_new_page(wp[1], _(pp[0]));
			break;
		/* Add a 2-col table */
		case op_TABLE2:
			--wp; wp[0] = add_a_table((int)pp[0], 2,
				GET_BORDER(TABLE), wp[1]);
			break;
		/* Add a scrolled window */
		case op_SCROLL:
			widget = xpack(wp[0], gtk_scrolled_window_new(NULL, NULL));
			gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(widget),
				scrollp[(int)pp[0] & 255], scrollp[(int)pp[0] >> 8]);
			gtk_widget_show(widget);
			break;
		/* Put a notebook into scrolled window */
		case op_SNBOOK:
			--wp; wp[0] = gtk_notebook_new();
			gtk_notebook_set_tab_pos(GTK_NOTEBOOK(wp[0]), GTK_POS_LEFT);
//			gtk_notebook_set_scrollable(GTK_NOTEBOOK(wp[0]), TRUE);
			gtk_scrolled_window_add_with_viewport(
				GTK_SCROLLED_WINDOW(widget), wp[0]);
			gtk_viewport_set_shadow_type(GTK_VIEWPORT(
				GTK_BIN(widget)->child), GTK_SHADOW_NONE);
			gtk_widget_show_all(widget);
			vport_noshadow_fix(GTK_BIN(widget)->child);
			break;
		/* Add a horizontal line */
		case op_HSEP:
// !!! Length = 200, height = 10
			add_hseparator(wp[0], 200, 10);
			break;
		/* Add a named spin to table, fill from field/var */
		case op_TSPIN: case op_TSPINv: case op_TSPINa:
		{
			int *xp = op == op_TSPINv ? pp[1] :
				(int *)((char *)ddata + (int)pp[1]);
			int b = GET_BORDER(TSPIN), n = next_table_level(wp[0]);

			add_to_table(_(pp[0]), wp[0], n, 0, b);
			*r++ = widget = spin_to_table(wp[0], n, 1, b, *xp,
				op == op_TSPINa ? xp[1] : (int)pp[2],
				op == op_TSPINa ? xp[2] : (int)pp[3]);
			*r++ = pp - 1;
			break;
		}
		/* Add a named checkbox, fill from field/var */
		case op_CHECK: case op_CHECKv:
		{
			int *xp = op == op_CHECKv ? pp[1] :
				(int *)((char *)ddata + (int)pp[1]);
			*r++ = widget = add_a_toggle(_(pp[0]), wp[0], *xp);
			*r++ = pp - 1;
			break;
		}
		/* Add a named checkbox, fill from inifile */
		case op_CHECKb:
			*r++ = widget = add_a_toggle(_(pp[0]), wp[0], 
				inifile_get_gboolean(pp[1], (int)pp[2]));
			*r++ = pp - 1;
			break;
		/* Add a (self-reading) pack of radio buttons */
		case op_RPACK:
		{
			char **src = pp[0];
			int nh = (int)pp[1], *v = (int *)(dstore + (int)pp[2]);
#if U_NLS
			char *tc[256];
			int i, n = nh & 255;
			for (i = 0; i < n; i++) tc[i] = _(src[i]);
			src = tc;
#endif
			xpack(wp[0], widget = wj_radio_pack(src, nh & 255,
				nh >> 8, *v, v, NULL));
			break;
		}
		/* Add a path box, fill from var/inifile */
		case op_PATHv: case op_PATHs:
			*r++ = widget = mt_path_box(_(pp[0]), wp[0], _(pp[1]),
				(int)pp[2]);
			*r++ = pp - 1;
			gtkuncpy(txt, op == op_PATHs ? inifile_get(pp[3], "") :
				pp[3], PATHTXT);
			gtk_entry_set_text(GTK_ENTRY(widget), txt);
			break;
		/* Add a box with "OK"/"Cancel", or something like */
		case op_OKBOX:
		{
			GtkWidget *hbox, *ok_bt, *cancel_bt;
			GtkAccelGroup* ag;

			--wp; wp[0] = hbox = pack(wp[1], gtk_hbox_new(TRUE, 0));
			gtk_container_set_border_width(GTK_CONTAINER(hbox),
				GET_BORDER(OKBOX));
			*r++ = hbox;
			*r++ = pp - 1;

			ok_bt = cancel_bt = gtk_button_new_with_label(_(pp[0]));
			gtk_container_set_border_width(GTK_CONTAINER(ok_bt), 5);
			/* OK-event */
			r = add_click(r, res, pp + 2, ok_bt, window);
			if (pp[1])
			{
				cancel_bt = xpack(hbox,
					gtk_button_new_with_label(_(pp[1])));
				gtk_container_set_border_width(
					GTK_CONTAINER(cancel_bt), 5);
				/* Cancel-event */
				r = add_click(r, res, pp + 4, cancel_bt, window);
			}
			xpack(hbox, ok_bt);

			ag = gtk_accel_group_new();
			gtk_widget_add_accelerator(cancel_bt, "clicked", ag,
				GDK_Escape, 0, (GtkAccelFlags)0);
			gtk_widget_add_accelerator(ok_bt, "clicked", ag,
				GDK_Return, 0, (GtkAccelFlags)0);
			gtk_widget_add_accelerator(ok_bt, "clicked", ag,
				GDK_KP_Enter, 0, (GtkAccelFlags)0);
 			gtk_window_add_accel_group(GTK_WINDOW(window), ag);
			delete_to_click(window, cancel_bt);
			gtk_widget_show_all(hbox);
			break;
		}
		/* Add a clickable button to OK-box */
		case op_OKADD:
		{
			*r++ = widget = xpack(wp[0],
				gtk_button_new_with_label(_(pp[0])));
			*r++ = pp - 1;
			gtk_box_reorder_child(GTK_BOX(wp[0]), widget, 1);
			gtk_container_set_border_width(GTK_CONTAINER(widget), 5);
			gtk_widget_show(widget);
			/* Click-event */
			r = add_click(r, res, pp + 1, widget, window);
			break;
		}
		/* Call a function */
		case op_EXEC:
			r = ((ext_fn)pp[0])(r, &wp);
			break;
		/* Skip next token if/unless field is unset */
		case op_IF: case op_UNLESS:
			if (!*(int *)((char *)ddata + (int)pp[0]) ^ (op != op_IF))
				ifcode += 1 + ((int)*ifcode >> 16);
			break;
		/* Make toplevel window shrinkable */
		case op_MKSHRINK:
			gtk_window_set_policy(GTK_WINDOW(window), TRUE, TRUE, FALSE);
			break;
		/* Make scrolled window request max size */
		case op_WANTMAX:
			gtk_signal_connect(GTK_OBJECT(widget), "size_request",
				GTK_SIGNAL_FUNC(scroll_max_size_req), NULL);
			break;
		/* Set nondefault border size */
		case op_BOR_TABLE: case op_BOR_TSPIN: case op_BOR_OKBOX:
			borders[op - op_BOR_0] = (int)pp[0] - DEF_BORDER;
			break;
		}
	}

	/* !!! For now, done unconditionally */
	gtk_window_set_transient_for(GTK_WINDOW(window), GTK_WINDOW(main_window));

	/* Return anchor position */
	return (res);
}

void run_query(void **wdata)
{
	char *data = GET_DDATA(wdata);
	void **pp;
	int op;

	for (++wdata; (pp = wdata[1]); wdata += 2)
	{
		op = (int)*pp++;
		switch (op &= 0xFFFF)
		{
		case op_TSPIN: case op_TSPINa:
			*(int *)(data + (int)pp[1]) = read_spin(*wdata);
			break;
		case op_TSPINv:
			*(int *)pp[1] = read_spin(*wdata);
			break;
		case op_CHECK: case op_CHECKv:
		{
			int *xp = op == op_CHECKv ? pp[1] : (int *)(data + (int)pp[1]);
			*xp = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(*wdata));
			break;
		}
		case op_CHECKb:
			inifile_set_gboolean(pp[1], gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(*wdata)));
			break;
//		case op_RPACK: // self-reading
		case op_PATHv:
			gtkncpy(pp[3], gtk_entry_get_text(GTK_ENTRY(*wdata)), PATHBUF);
			break;
		case op_PATHs:
		{
			char path[PATHBUF];
			gtkncpy(path, gtk_entry_get_text(GTK_ENTRY(*wdata)), PATHBUF);
			inifile_set(pp[3], path);
			break;
		}
		}
	}
}

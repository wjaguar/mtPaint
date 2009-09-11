/*	viewer.c
	Copyright (C) 2004-2009 Mark Tyler and Dmitry Groshev

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
#include "viewer.h"
#include "canvas.h"
#include "inifile.h"
#include "layer.h"
#include "channels.h"
#include "toolbar.h"
#include "font.h"

int view_showing, view_update_pending;
float vw_zoom = 1;

int opaque_view;


static gint viewer_keypress( GtkWidget *widget, GdkEventKey *event )
{					// Used by command line window too
	if (check_zoom_keys(wtf_pressed(event))) return TRUE;	// Check HOME/zoom keys

	return FALSE;
}


////	COMMAND LINE WINDOW


void cline_select(GtkWidget *clist, gint row, gint col, GdkEvent *event, gpointer *pointer)
{
	static int last_row; // 0 initially
	int change;

	if (row != last_row)
	{
		if (!layers_total) change = check_for_changes();
		else change = check_layers_for_changes();
		if ((change == 2) || (change == -10))	// Load requested file
			do_a_load(global_argv[(last_row = row) + file_arg_start]);
		else clist_reselect_row(GTK_CLIST(clist), last_row); // Go back
	}
}

void create_cline_area( GtkWidget *vbox1 )
{
	int i;
	char txt2[PATHTXT];
	GtkWidget *scrolledwindow, *col_list;
	gchar *item[1];

	scrolledwindow = xpack(vbox1, gtk_scrolled_window_new(NULL, NULL));
	gtk_widget_show(scrolledwindow);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(scrolledwindow),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	col_list = gtk_clist_new(1);
	gtk_clist_set_column_auto_resize( GTK_CLIST(col_list), 0, TRUE );
	gtk_clist_set_selection_mode(GTK_CLIST(col_list), GTK_SELECTION_BROWSE);

	item[0] = txt2;
	for ( i=file_arg_start; i<(file_arg_start + files_passed); i++ )
	{
		gtkuncpy(txt2, global_argv[i], PATHTXT);
		gtk_clist_set_selectable ( GTK_CLIST(col_list),
			gtk_clist_append(GTK_CLIST (col_list), item), TRUE );
	}
	gtk_container_add ( GTK_CONTAINER(scrolledwindow), col_list );
	gtk_widget_show(col_list);
	gtk_signal_connect(GTK_OBJECT(col_list), "select_row",
		GTK_SIGNAL_FUNC(cline_select), NULL);

	gtk_widget_grab_focus(col_list);

	gtk_signal_connect(GTK_OBJECT(col_list), "key_press_event",
		GTK_SIGNAL_FUNC(viewer_keypress), NULL);
}



///	HELP WINDOW

#include "help.c"

gboolean click_help_end( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	// Make sure the user can only open 1 help window
	gtk_widget_set_sensitive(menu_widgets[MENU_HELP], TRUE);
	gtk_widget_destroy(widget);

	return FALSE;
}

void pressed_help()
{
	GtkAccelGroup* ag;
	GtkWidget *help_window, *table, *notebook, *frame, *label, *button,
		*box1, *box2, *scrolled_window1, *viewport1, *label2;
	int i, j;
	char txt[128], **tmp, *res, *strs[HELP_PAGE_MAX + 1];


	ag = gtk_accel_group_new();

	// Make sure the user can only open 1 help help_window
	gtk_widget_set_sensitive(menu_widgets[MENU_HELP], FALSE);

	help_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);

	gtk_container_set_border_width (GTK_CONTAINER (help_window), 4);
	gtk_window_set_position (GTK_WINDOW (help_window), GTK_WIN_POS_CENTER);
	snprintf(txt, 120, "%s - %s", VERSION, _("About"));
	gtk_window_set_title (GTK_WINDOW (help_window), txt);
	gtk_widget_set_usize (help_window, -2, 400);

	box1 = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (help_window), box1);
	gtk_widget_show (box1);

	box2 = xpack(box1, gtk_vbox_new(FALSE, 10));
	gtk_container_set_border_width (GTK_CONTAINER (box2), 1);
	gtk_widget_show (box2);

	table = xpack(box2, gtk_table_new(3, 6, FALSE));

	notebook = gtk_notebook_new ();
	gtk_table_attach_defaults (GTK_TABLE (table), notebook, 0, 6, 0, 1);
	gtk_widget_show (notebook);

	strs[0] = "";
	for (i = 0; i < HELP_PAGE_COUNT; i++)
	{
		tmp = help_pages[i];
		for (j = 0; tmp[j]; j++) strs[j + 1] = _(tmp[j]);
		strs[j + 1] = NULL;
		res = g_strjoinv("\n", strs);

		frame = gtk_frame_new (NULL);
		gtk_container_set_border_width (GTK_CONTAINER (frame), 10);
		gtk_widget_show (frame);

		scrolled_window1 = gtk_scrolled_window_new (NULL, NULL);
		gtk_widget_show (scrolled_window1);
		gtk_container_add (GTK_CONTAINER (frame), scrolled_window1);
		gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolled_window1), 
			GTK_POLICY_NEVER, GTK_POLICY_ALWAYS);

		viewport1 = gtk_viewport_new (NULL, NULL);
		gtk_widget_show (viewport1);
		gtk_container_add (GTK_CONTAINER (scrolled_window1), viewport1);

		label2 = gtk_label_new(res);
		g_free(res);
#if GTK_MAJOR_VERSION == 2
		if ((i == 1) || (i == 2))	// Keyboard/Mouse shortcuts tab only
		{
			PangoFontDescription *pfd =
				pango_font_description_from_string("Monospace 9");
				// Courier also works
			gtk_widget_modify_font(label2, pfd);
			pango_font_description_free(pfd);
		}
#endif
		gtk_widget_set_usize( label2, 380, -2 );		// Set minimum width/height
		gtk_widget_show (label2);

		gtk_container_add (GTK_CONTAINER (viewport1), label2);
		GTK_WIDGET_SET_FLAGS (label2, GTK_CAN_FOCUS);
#if GTK_MAJOR_VERSION == 2
		gtk_label_set_selectable(GTK_LABEL (label2), TRUE);
#endif
		gtk_label_set_justify (GTK_LABEL (label2), GTK_JUSTIFY_LEFT);
		gtk_label_set_line_wrap (GTK_LABEL (label2), TRUE);
		gtk_misc_set_alignment (GTK_MISC (label2), 0, 0);
		gtk_misc_set_padding (GTK_MISC (label2), 5, 5);

		label = gtk_label_new(_(help_titles[i]));
		gtk_notebook_append_page (GTK_NOTEBOOK (notebook), frame, label);
	}

	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook), GTK_POS_LEFT);

	box2 = pack(box1, gtk_vbox_new(FALSE, 10));
	gtk_container_set_border_width (GTK_CONTAINER (box2), 1);
	gtk_widget_show (box2);

	button = xpack(box2, gtk_button_new_with_label (_("Close")));
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);
	GTK_WIDGET_SET_FLAGS (button, GTK_CAN_DEFAULT);

	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_help_end), GTK_OBJECT(help_window));
	gtk_signal_connect(GTK_OBJECT(help_window), "delete_event",
		GTK_SIGNAL_FUNC(click_help_end), NULL);

	gtk_widget_show (button);

	gtk_widget_show (table);
	gtk_window_set_default_size( GTK_WINDOW(help_window), 600, 2 );
	gtk_widget_show (help_window);
	gtk_window_add_accel_group(GTK_WINDOW (help_window), ag);
}



///	PAN WINDOW

int max_pan;

static GtkWidget *pan_window, *draw_pan;
static int pan_w, pan_h;
static unsigned char *pan_rgb;

void draw_pan_thumb(int x1, int y1, int x2, int y2)
{
	int i, j, k, ix, iy, zoom = 1, scale = 1;
	unsigned char *dest, *src;

	if (!pan_rgb) return;		// Needed to stop segfault

	/* Create thumbnail */
	dest = pan_rgb;
	for (i = 0; i < pan_h; i++)
	{
		iy = (i * mem_height) / pan_h;
		src = mem_img[CHN_IMAGE] + iy * mem_width * mem_img_bpp;
		if (mem_img_bpp == 3) /* RGB */
		{
			for (j = 0; j < pan_w; j++ , dest += 3)
			{
				ix = ((j * mem_width) / pan_w) * 3;
				dest[0] = src[ix + 0];
				dest[1] = src[ix + 1];
				dest[2] = src[ix + 2];
			}
		}
		else /* Indexed */
		{
			for (j = 0; j < pan_w; j++ , dest += 3)
			{
				ix = src[(j * mem_width) / pan_w];
				dest[0] = mem_pal[ix].red;
				dest[1] = mem_pal[ix].green;
				dest[2] = mem_pal[ix].blue;
			}
		}
	}


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	/* Canvas coords to image coords */
	x2 = ((x1 + x2) / scale) * zoom;
	y2 = ((y1 + y2) / scale) * zoom;
	x1 = (x1 / scale) * zoom;
	y1 = (y1 / scale) * zoom;
	x1 = x1 < 0 ? 0 : x1 >= mem_width ? mem_width - 1 : x1;
	x2 = x2 < 0 ? 0 : x2 >= mem_width ? mem_width - 1 : x2;
	y1 = y1 < 0 ? 0 : y1 >= mem_height ? mem_height - 1 : y1;
	y2 = y2 < 0 ? 0 : y2 >= mem_height ? mem_height - 1 : y2;

	/* Image coords to thumbnail coords */
	x1 = (x1 * pan_w) / mem_width;
	x2 = (x2 * pan_w) / mem_width;
	y1 = (y1 * pan_h) / mem_height;
	y2 = (y2 * pan_h) / mem_height;

	/* Draw the border */
	dest = src = pan_rgb + (y1 * pan_w + x1) * 3;
	j = y2 - y1;
	k = (x2 - x1) * 3;
	for (i = 0; i <= j; i++)
	{
		dest[k + 0] = dest[k + 1] = dest[k + 2] =
			dest[0] = dest[1] = dest[2] = ((i >> 2) & 1) * 255;
		dest += pan_w * 3;
	}
	j = x2 - x1;
	k = (y2 - y1) * pan_w * 3;
	for (i = 0; i <= j; i++)
	{
		src[k + 0] = src[k + 1] = src[k + 2] =
			src[0] = src[1] = src[2] = ((i >> 2) & 1) * 255;
		src += 3;
	}

	if (draw_pan) gdk_draw_rgb_image(draw_pan->window, draw_pan->style->black_gc,
		0, 0, pan_w, pan_h, GDK_RGB_DITHER_NONE, pan_rgb, pan_w * 3);
}

void pan_thumbnail()		// Create thumbnail and selection box
{
	GtkAdjustment *hori, *vert;

	while (gtk_events_pending()) gtk_main_iteration();
		// Update main window first to get new scroll positions if necessary

	hori = gtk_scrolled_window_get_hadjustment( GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );
	vert = gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );

	draw_pan_thumb(hori->value, vert->value, hori->page_size, vert->page_size);
}

static void do_pan(GtkAdjustment *hori, GtkAdjustment *vert, int nv_h, int nv_v)
{
	static int wait_h, wait_v, in_pan;

	nv_h = nv_h < 0 ? 0 : nv_h > hori->upper - hori->page_size ?
		hori->upper - hori->page_size : nv_h;
	nv_v = nv_v < 0 ? 0 : nv_v > vert->upper - vert->page_size ?
		vert->upper - vert->page_size : nv_v;

	if (in_pan) /* Delay reaction */
	{
		wait_h = nv_h; wait_v = nv_v;
		in_pan |= 2;
		return;
	}

	while (TRUE)
	{
		in_pan = 1;

		/* Update selection box */
		draw_pan_thumb(nv_h, nv_v, hori->page_size, vert->page_size);

		/* Update position of main window scrollbars */
		hori->value = nv_h;
		vert->value = nv_v;
		gtk_adjustment_value_changed(hori);
		gtk_adjustment_value_changed(vert);

		/* Process events */
		while (gtk_events_pending()) gtk_main_iteration();
		if (in_pan < 2) break;

		/* Do delayed update */
		nv_h = wait_h;
		nv_v = wait_v;
	}
	in_pan = 0;
}

static void delete_pan()
{
	free(pan_rgb);
	pan_rgb = NULL;				// Needed to stop segfault
	gtk_widget_destroy(pan_window);
}

static gboolean key_pan(GtkWidget *widget, GdkEventKey *event)
{
	int nv_h, nv_v, hm, vm;
	GtkAdjustment *hori, *vert;

	if (!check_zoom_keys_real(wtf_pressed(event)))
	{
		/* xine-ui sends bogus keypresses so don't delete on this */
		if (!arrow_key(event, &hm, &vm, 4) &&
			!XINE_FAKERY(event->keyval)) delete_pan();
		else
		{
			hori = gtk_scrolled_window_get_hadjustment(
				GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );
			vert = gtk_scrolled_window_get_vadjustment(
				GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );

			nv_h = hori->value + hm * (hori->page_size / 4);
			nv_v = vert->value + vm * (hori->page_size / 4);

			do_pan(hori, vert, nv_h, nv_v);
		}
	}
	else pan_thumbnail();	// Update selection box as user may have zoomed in/out

	return (TRUE);
}

static void pan_button(int mx, int my, int button)
{
	int nv_h, nv_v;
	float cent_x, cent_y;
	GtkAdjustment *hori, *vert;

	if (button == 1)	// Left click = pan window
	{
		hori = gtk_scrolled_window_get_hadjustment( GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );
		vert = gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );

		cent_x = ((float) mx) / pan_w;
		cent_y = ((float) my) / pan_h;

		nv_h = mem_width*can_zoom*cent_x - hori->page_size/2;
		nv_v = mem_height*can_zoom*cent_y - vert->page_size/2;

		do_pan(hori, vert, nv_h, nv_v);
	}
	else if (button == 3) delete_pan();	// Right click = kill window
}

static gboolean click_pan(GtkWidget *widget, GdkEventButton *event)
{
	pan_button(event->x, event->y, event->button);
	return (TRUE);
}

static gboolean pan_motion(GtkWidget *widget, GdkEventMotion *event)
{
	int x, y, button = 0;
	GdkModifierType state;

	if (event->is_hint) gdk_window_get_pointer (event->window, &x, &y, &state);
	else
	{
		x = event->x;
		y = event->y;
		state = event->state;
	}

	if (state & GDK_BUTTON1_MASK) button = 1;
	if (state & GDK_BUTTON3_MASK) button = 3;

	pan_button(x, y, button);

	return (TRUE);
}

static gboolean expose_pan(GtkWidget *widget, GdkEventExpose *event)
{
	gdk_draw_rgb_image(widget->window, widget->style->black_gc,
		event->area.x, event->area.y, event->area.width, event->area.height,
		GDK_RGB_DITHER_NONE,
		pan_rgb + (event->area.y * pan_w + event->area.x) * 3, pan_w * 3);
	return (FALSE);
}

void pressed_pan()
{
	float rat_x, rat_y;

	draw_pan = NULL;	// Needed by draw_pan_thumb above

	rat_x = max_pan / ((float) mem_width);
	rat_y = max_pan / ((float) mem_height);

	if ( rat_x > rat_y )
	{
		pan_w = rat_y * mem_width;
		pan_h = max_pan;
	}
	else
	{
		pan_w = max_pan;
		pan_h = rat_x * mem_height;
	}
	mtMAX(pan_w, pan_w, 1)
	mtMAX(pan_h, pan_h, 1)

	pan_rgb = calloc(1, pan_w * pan_h * 3);

	pan_thumbnail();

	pan_window = add_a_window( GTK_WINDOW_POPUP, _("Pan Window"), GTK_WIN_POS_MOUSE, TRUE );
	gtk_container_set_border_width (GTK_CONTAINER (pan_window), 2);

	draw_pan = gtk_drawing_area_new();
	gtk_widget_set_usize( draw_pan, pan_w, pan_h );
	gtk_container_add (GTK_CONTAINER (pan_window), draw_pan);
	gtk_widget_show( draw_pan );
	gtk_signal_connect(GTK_OBJECT(draw_pan), "expose_event",
		GTK_SIGNAL_FUNC(expose_pan), NULL);
	gtk_signal_connect(GTK_OBJECT(draw_pan), "button_press_event",
		GTK_SIGNAL_FUNC(click_pan), NULL);
	gtk_signal_connect(GTK_OBJECT(draw_pan), "motion_notify_event",
		GTK_SIGNAL_FUNC(pan_motion), NULL);
	gtk_signal_connect(GTK_OBJECT(pan_window), "key_press_event",
		GTK_SIGNAL_FUNC(key_pan), NULL);
	gtk_widget_set_events(draw_pan, GDK_ALL_EVENTS_MASK);

	gtk_widget_show(pan_window);
}



////	VIEW WINDOW

static int vw_width, vw_height, vw_last_x, vw_last_y, vw_move_layer;
static int vw_mouse_status;

GtkWidget *vw_drawing;
int vw_focus_on;

void render_layers(unsigned char *rgb, int step, int px, int py, int pw, int ph,
	double czoom, int lr0, int lr1, int align)
{
	image_info *image;
	unsigned char *tmp, **img;
	int i, j, ii, jj, ll, wx0, wy0, wx1, wy1, xof, xpm, opac, thid, tdis;
	int ddx, ddy, mx, mw, my, mh;
	int pw2 = pw, ph2 = ph, dx = 0, dy = 0;
	int zoom = 1, scale = 1;

	if (czoom < 1.0) zoom = rint(1.0 / czoom);
	else scale = rint(czoom);

	/* Align on selected layer if needed */
	if (align && layers_total && layer_selected && (zoom > 1))
	{
		dx = layer_table[layer_selected].x % zoom;
		if (dx < 0) dx += zoom;
		dy = layer_table[layer_selected].y % zoom;
		if (dy < 0) dy += zoom;
	}

	/* Apply background bounds if needed */
	if (layers_pastry_cut)
	{
		if (px < 0)
		{
			rgb -= px * 3;
			pw2 += px;
			px = 0;
		}
		if (py < 0)
		{
			rgb -= py * step;
			ph2 += py;
			py = 0;
		}
		i = mem_width;
		j = mem_height;
		if (layers_total && layer_selected)
		{
			i = layer_table[0].image->image_.width;
			j = layer_table[0].image->image_.height;
		}
		i = ((i - dx + zoom - 1) / zoom) * scale;
		j = ((j - dy + zoom - 1) / zoom) * scale;
		if (pw2 > i) pw2 = i;
		if (ph2 > j) ph2 = j;
		if ((pw2 <= 0) || (ph2 <= 0)) return;
	}
	xof = px % scale;
	if (xof < 0) xof += scale;

	/* Get image-space bounds */
	i = px % scale < 0 ? 1 : 0;
	j = py % scale < 0 ? 1 : 0;
	wx0 = (px / scale) * zoom + dx - i;
	wy0 = (py / scale) * zoom + dy - j;
	wx1 = px + pw2 - 1;
	wy1 = py + ph2 - 1;
	i = wx1 % scale < 0 ? 1 : 0;
	j = wy1 % scale < 0 ? 1 : 0;
	wx1 = (wx1 / scale) * zoom + dx - i;
	wy1 = (wy1 / scale) * zoom + dy - j;

	/* No point in doing that here */
	thid = hide_image;
	hide_image = FALSE;
	tdis = channel_dis[CHN_ALPHA];
	channel_dis[CHN_ALPHA] = FALSE;

	for (ll = lr0; ll <= lr1; ll++)
	{
		if (ll && !layer_table[ll].visible) continue;
		i = layer_table[ll].x;
		j = layer_table[ll].y;
		if (!ll) i = j = 0;
		image = ll == layer_selected ? &mem_image :
			&layer_table[ll].image->image_;
		ii = i + image->width;
		jj = j + image->height;
		if ((i > wx1) || (j > wy1) || (ii <= wx0) || (jj <= wy0))
			continue;
		ddx = ddy = mx = my = 0;
		if (zoom > 1)
		{
			if (i > wx0) mx = (i - wx0 + zoom - 1) / zoom;
			if (j > wy0) my = (j - wy0 + zoom - 1) / zoom;
			ddx = wx0 + mx * zoom - i;
			ddy = wy0 + my * zoom - j;
			if (ii - 1 >= wx1) mw = pw2 - mx;
			else mw = (ii - i - ddx + zoom - 1) / zoom;
			if (jj - 1 >= wy1) mh = ph2 - my;
			else mh = (jj - j - ddy + zoom - 1) / zoom;
			if ((mw <= 0) || (mh <= 0)) continue;
		}
		else
		{
			if (i > wx0) mx = i * scale - px;
			else ddx = wx0 - i;
			if (j > wy0) my = j * scale - py;
			else ddy = wy0 - j;
			if (ii - 1 >= wx1) mw = pw2 - mx;
			else mw = ii * scale - px - mx;
			if (jj - 1 >= wy1) mh = ph2 - my;
			else mh = jj * scale - py - my;
		}
		tmp = rgb + my * step + mx * 3;
		xpm = -1;
		opac = 255;
		if (ll)
		{
			opac = (layer_table[ll].opacity * 255 + 50) / 100;
			xpm = ll == layer_selected ? mem_xpm_trans :
				layer_table[ll].image->state_.xpm_trans;
		}
		img = image->img;
		setup_row(xof + mx, mw, czoom, ii - i, xpm, opac,
			image->bpp, image->pal);
		i = (py + my) % scale;
		if (i < 0) i += scale;
		mh = mh * zoom + i;
		for (j = -1; i < mh; i += zoom , tmp += step)
		{
			if (i / scale == j)
			{
				memcpy(tmp, tmp - step, mw * 3);
				continue;
			}
			j = i / scale;
			render_row(tmp, img, ddx, ddy + j, NULL);
		}
	}
	hide_image = thid;
	channel_dis[CHN_ALPHA] = tdis;
}

void view_render_rgb( unsigned char *rgb, int px, int py, int pw, int ph, double czoom )
{
	int tmp = overlay_alpha;

	if (!rgb) return; /* Paranoia */
	/* Control transparency separately */
	overlay_alpha = opaque_view;
	/* Always align on background layer */
	render_layers(rgb, pw * 3, px, py, pw, ph, czoom, 0, layers_total, 0);
	overlay_alpha = tmp;
}

static guint idle_focus;

void vw_focus_view()						// Focus view window to main window
{
	int w, h, w0, h0;
	float main_h = 0.5, main_v = 0.5, px, py, nv_h, nv_v;
	GtkAdjustment *hori, *vert;

	if (idle_focus) gtk_idle_remove(idle_focus);
	idle_focus = 0;
	if (!view_showing) return;		// Bail out if not visible
	if (!vw_focus_on) return;		// Only focus if user wants to

	if (vw_mouse_status)	/* Dragging in progress - delay focus */
	{
		vw_mouse_status |= 2;
		return;
	}

	hori = gtk_scrolled_window_get_hadjustment( GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );
	vert = gtk_scrolled_window_get_vadjustment( GTK_SCROLLED_WINDOW(scrolledwindow_canvas) );

	canvas_size(&w, &h);
	if (hori->page_size < w)
		main_h = (hori->value + hori->page_size * 0.5) / w;
	if (vert->page_size < h)
		main_v = (vert->value + vert->page_size * 0.5) / h;

	/* If we are editing a layer above the background make adjustments */
	if (layers_total && layer_selected)
	{
		w0 = layer_table[0].image->image_.width;
		h0 = layer_table[0].image->image_.height;
		px = main_h * mem_width + layer_table[layer_selected].x;
		py = main_v * mem_height + layer_table[layer_selected].y;
		px = px < 0.0 ? 0.0 : px >= w0 ? w0 - 1 : px;
		py = py < 0.0 ? 0.0 : py >= h0 ? h0 - 1 : py;
		main_h = px / w0;
		main_v = py / h0;
	}

	hori = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(vw_scrolledwindow));
	vert = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(vw_scrolledwindow));

	if (hori->page_size < vw_width)
	{
		nv_h = vw_width * main_h - hori->page_size * 0.5;
		if (nv_h + hori->page_size > vw_width)
			nv_h = vw_width - hori->page_size;
		if (nv_h < 0.0) nv_h = 0.0;
	}
	else nv_h = 0.0;

	if ( vert->page_size < vw_height )
	{
		nv_v = vw_height * main_v - vert->page_size * 0.5;
		if (nv_v + vert->page_size > vw_height)
			nv_v = vw_height - vert->page_size;
		if (nv_v < 0.0) nv_v = 0.0;
	}
	else nv_v = 0.0;

	/* Do nothing if nothing changed */
	if ((hori->value == nv_h) && (vert->value == nv_v)) return;

	hori->value = nv_h;
	vert->value = nv_v;

	/* Update position of view window scrollbars */
	gtk_adjustment_value_changed(hori);
	gtk_adjustment_value_changed(vert);
}

void vw_focus_idle()
{
	if (idle_focus) return;
	if (!view_showing) return;
	if (!vw_focus_on) return;
	idle_focus = gtk_idle_add_priority(GTK_PRIORITY_REDRAW + 5,
		(GtkFunction)vw_focus_view, NULL);
}

gboolean vw_configure( GtkWidget *widget, GdkEventConfigure *event )
{
	int ww, wh, new_margin_x = 0, new_margin_y = 0;

	if (canvas_image_centre)
	{
		ww = vw_drawing->allocation.width - vw_width;
		wh = vw_drawing->allocation.height - vw_height;

		if (ww > 0) new_margin_x = ww >> 1;
		if (wh > 0) new_margin_y = wh >> 1;
	}

	if ((new_margin_x != margin_view_x) || (new_margin_y != margin_view_y))
	{
		margin_view_x = new_margin_x;
		margin_view_y = new_margin_y;
		/* Force redraw of whole canvas as the margin has shifted */
		gtk_widget_queue_draw(vw_drawing);
	}
	if (idle_focus) vw_focus_view(); // Time to refocus is NOW

	return TRUE;
}

void vw_align_size(float new_zoom)
{
	if (!view_showing) return;

	if (new_zoom < MIN_ZOOM) new_zoom = MIN_ZOOM;
	if (new_zoom > MAX_ZOOM) new_zoom = MAX_ZOOM;
	if (new_zoom == vw_zoom) return;

	vw_zoom = new_zoom;
	vw_realign();
	toolbar_zoom_update();	// View zoom doesn't get changed elsewhere
}

void vw_realign()
{
	int sw = mem_width, sh = mem_height, i;

	if (!view_showing) return;

	if (layers_total && layer_selected)
	{
		sw = layer_table[0].image->image_.width;
		sh = layer_table[0].image->image_.height;
	}

	if (vw_zoom < 1.0)
	{
		i = rint(1.0 / vw_zoom);
		sw = (sw + i - 1) / i;
		sh = (sh + i - 1) / i;
	}
	else
	{
		i = rint(vw_zoom);
		sw *= i; sh *= i;
	}

	if ((vw_width != sw) || (vw_height != sh))
	{
		vw_width = sw;
		vw_height = sh;
		wjcanvas_size(vw_drawing, vw_width, vw_height);
	}
	/* !!! Let refocus wait a bit - if window is being resized, view pane's
	 * allocation could be not yet updated (canvas is done first) - WJ */
	vw_focus_idle();
}

static gboolean vw_expose(GtkWidget *widget, GdkEventExpose *event)
{
	unsigned char *rgb;
	int px, py, pw, ph, vport[4];

	wjcanvas_get_vport(widget, vport);
	px = event->area.x + vport[0];
	py = event->area.y + vport[1];
	pw = event->area.width;
	ph = event->area.height;

	if ((pw <= 0) || (ph <= 0)) return (FALSE);

	rgb = calloc(1, pw * ph * 3);
	if (rgb)
	{
		memset(rgb, mem_background, pw * ph * 3);
		view_render_rgb(rgb, px - margin_view_x, py - margin_view_y,
			pw, ph, vw_zoom);
		gdk_draw_rgb_image(widget->window, widget->style->black_gc,
			event->area.x, event->area.y, pw, ph,
			GDK_RGB_DITHER_NONE, rgb, pw * 3);
		free(rgb);
	}

	return (FALSE);
}

void vw_update_area(int x, int y, int w, int h)	// Update x,y,w,h area of current image
{
	int zoom, scale, vport[4], rxy[4];

	if (!view_showing) return;
	
	if ( layer_selected > 0 )
	{
		x += layer_table[layer_selected].x;
		y += layer_table[layer_selected].y;
	}

	if (vw_zoom < 1.0)
	{
		zoom = rint(1.0 / vw_zoom);
		w += x;
		h += y;
		x = floor_div(x + zoom - 1, zoom);
		y = floor_div(y + zoom - 1, zoom);
		w = (w - x * zoom + zoom - 1) / zoom;
		h = (h - y * zoom + zoom - 1) / zoom;
		if ((w <= 0) || (h <= 0)) return;
	}
	else
	{
		scale = rint(vw_zoom);
		x *= scale;
		y *= scale;
		w *= scale;
		h *= scale;
	}

	x += margin_view_x; y += margin_view_y;
	wjcanvas_get_vport(vw_drawing, vport);
	if (clip(rxy, x, y, x + w, y + h, vport))
		gtk_widget_queue_draw_area(vw_drawing,
			rxy[0] - vport[0], rxy[1] - vport[1],
			rxy[2] - rxy[0], rxy[3] - rxy[1]);
}

static void vw_mouse_event(int event, int x, int y, guint state, guint button)
{
	image_info *image;
	unsigned char *rgb, **img;
	int dx, dy, i, lx, ly, lw, lh, bpp, tpix, ppix, ofs;
	int zoom = 1, scale = 1;
	png_color *pal;

	i = vw_mouse_status;
	if (!button || !layers_total || (event == GDK_BUTTON_RELEASE))
	{
		vw_mouse_status = 0;
		if (i & 2) vw_focus_view(); /* Delayed focus event */
		return;
	}

	if (vw_zoom < 1.0) zoom = rint(1.0 / vw_zoom);
	else scale = rint(vw_zoom);

	dx = vw_last_x;
	dy = vw_last_y;
	vw_last_x = x = ((x - margin_view_x) / scale) * zoom;
	vw_last_y = y = ((y - margin_view_y) / scale) * zoom;

	vw_mouse_status |= 1;
	if (i & 1)
	{
		if (vw_move_layer > 0)
			move_layer_relative(vw_move_layer, x - dx, y - dy);
	}
	else
	{
		vw_move_layer = -1;		// Which layer has the user clicked?
		for (i = layers_total; i > 0; i--)
		{
			lx = layer_table[i].x;
			ly = layer_table[i].y;
			image = i == layer_selected ? &mem_image :
				&layer_table[i].image->image_;
			lw = image->width;
			lh = image->height;
			bpp = image->bpp;
			img = image->img;
			pal = image->pal;
			rgb = img[CHN_IMAGE];

			/* Is click within layer box? */
			if ( x>=lx && x<(lx + lw) && y>=ly && y<(ly + lh) &&
				layer_table[i].visible )
			{
				ofs = (x-lx) + lw*(y-ly);
				/* Is transparency disabled? */
				if (opaque_view) break;
				/* Is click on a non transparent pixel? */
				if (img[CHN_ALPHA])
				{
					if (img[CHN_ALPHA][ofs] < (bpp == 1 ? 255 : 1))
						continue;
				}
				tpix = i == layer_selected ? mem_xpm_trans :
					layer_table[i].image->state_.xpm_trans;
				if (tpix >= 0)
				{
					if (bpp == 1) ppix = rgb[ofs];
					else
					{
						tpix = PNG_2_INT(pal[tpix]);
						ppix = MEM_2_INT(rgb, ofs * 3);
					}
					if (tpix == ppix) continue;
				}
				break;
			}
		}
		if (i > 0) vw_move_layer = i;
		layer_choose(i);
	}
}

static gboolean view_window_motion(GtkWidget *widget, GdkEventMotion *event)
{
	GdkModifierType state;
	guint button = 0;
	int x, y, vport[4];

	if (event->is_hint) gdk_window_get_pointer (event->window, &x, &y, &state);
	else
	{
		x = event->x;
		y = event->y;
		state = event->state;
	}

	if ((state & (GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)) ==
		(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)) button = 13;
	else if (state & GDK_BUTTON1_MASK) button = 1;
	else if (state & GDK_BUTTON3_MASK) button = 3;
	else if (state & GDK_BUTTON2_MASK) button = 2;

	wjcanvas_get_vport(widget, vport);
	vw_mouse_event(event->type, x + vport[0], y + vport[1], state, button);

	return (TRUE);
}

static gint view_window_button( GtkWidget *widget, GdkEventButton *event )
{
	int vport[4], pflag = event->type != GDK_BUTTON_RELEASE;

	/* Steal focus from dock window */
	if (pflag && dock_focused())
	{
		gtk_window_set_focus(GTK_WINDOW(main_window), NULL);
		return (TRUE);
	}

	wjcanvas_get_vport(widget, vport);
	vw_mouse_event(event->type, event->x + vport[0], event->y + vport[1],
		event->state, event->button);

	return (pflag);
}

void view_show()
{
	if (view_showing) return;
	gtk_widget_ref(scrolledwindow_canvas);
	gtk_container_remove(GTK_CONTAINER(vbox_right), scrolledwindow_canvas);
	gtk_paned_pack1 (GTK_PANED (main_split), scrolledwindow_canvas, FALSE, TRUE);
	gtk_paned_pack2 (GTK_PANED (main_split), vw_scrolledwindow, FALSE, TRUE);
	view_showing = TRUE;
	xpack(vbox_right, main_split);
	gtk_widget_unref(scrolledwindow_canvas);
	gtk_widget_unref(vw_scrolledwindow);
	gtk_widget_unref(main_split);
	toolbar_viewzoom(TRUE);
	set_cursor(); /* Because canvas window is now a new one */
	gtk_check_menu_item_set_active(
		GTK_CHECK_MENU_ITEM(menu_widgets[MENU_VIEW]), TRUE);
	vw_focus_view();
#if GTK_MAJOR_VERSION == 1 /* GTK+1 leaves adjustments in wrong state */
	gtk_adjustment_value_changed(
		gtk_scrolled_window_get_hadjustment(
		GTK_SCROLLED_WINDOW(scrolledwindow_canvas)));
	if (!vw_focus_on) gtk_adjustment_value_changed(
		gtk_scrolled_window_get_hadjustment(
		GTK_SCROLLED_WINDOW(vw_scrolledwindow)));
#endif
}

void view_hide()
{
	if (!view_showing) return;
	view_showing = FALSE;
	gtk_widget_ref(scrolledwindow_canvas);
	gtk_widget_ref(vw_scrolledwindow);
	gtk_widget_ref(main_split);
	gtk_container_remove(GTK_CONTAINER(vbox_right), main_split);
	gtk_container_remove(GTK_CONTAINER(main_split), scrolledwindow_canvas);
	gtk_container_remove(GTK_CONTAINER(main_split), vw_scrolledwindow);
	xpack(vbox_right, scrolledwindow_canvas);
	gtk_widget_unref(scrolledwindow_canvas);
	toolbar_viewzoom(FALSE);
	set_cursor(); /* Because canvas window is now a new one */
	gtk_check_menu_item_set_active(
		GTK_CHECK_MENU_ITEM(menu_widgets[MENU_VIEW]), FALSE);
#if GTK_MAJOR_VERSION == 1 /* GTK+1 leaves adjustments in wrong state */
	gtk_adjustment_value_changed(
		gtk_scrolled_window_get_hadjustment(
		GTK_SCROLLED_WINDOW(scrolledwindow_canvas)));
#endif
}


void pressed_centralize(int state)
{
	canvas_image_centre = state;
	force_main_configure();		// Force configure of main window - for centalizing code
}

void pressed_view_focus(int state)
{
	vw_focus_on = state;
	vw_focus_view();
}

void init_view()
{
	vw_width = 1;
	vw_height = 1;

	view_showing = FALSE;

	gtk_signal_connect( GTK_OBJECT(vw_drawing), "configure_event",
		GTK_SIGNAL_FUNC (vw_configure), NULL );
	gtk_signal_connect( GTK_OBJECT(vw_drawing), "expose_event",
		GTK_SIGNAL_FUNC (vw_expose), NULL );

	gtk_signal_connect( GTK_OBJECT(vw_drawing), "button_press_event",
		GTK_SIGNAL_FUNC (view_window_button), NULL );
	gtk_signal_connect( GTK_OBJECT(vw_drawing), "button_release_event",
		GTK_SIGNAL_FUNC (view_window_button), NULL );
	gtk_signal_connect( GTK_OBJECT(vw_drawing), "motion_notify_event",
		GTK_SIGNAL_FUNC (view_window_motion), NULL );
	gtk_widget_set_events (vw_drawing, GDK_ALL_EVENTS_MASK);
}



////	TEXT TOOL

GtkWidget *text_window, *text_font_window, *text_toggle[3], *text_spin[2];

#define PAD_SIZE 4

/* !!! This function invalidates "img" (may free or realloc it) */
int make_text_clipboard(unsigned char *img, int w, int h, int src_bpp)
{
	unsigned char bkg[3], *src, *dest, *tmp, *pix = img, *mask = NULL;
	int i, l = w *h;
	int idx, masked, aa, ab, back, dest_bpp = MEM_BPP;

	idx = (mem_channel == CHN_IMAGE) && (mem_img_bpp == 1);
	/* Indexed image can't be antialiased */
	aa = !idx && inifile_get_gboolean("fontAntialias0", FALSE);
	ab = inifile_get_gboolean("fontAntialias1", FALSE);

	back = inifile_get_gint32("fontBackground", 0);
// !!! Bug - who said palette is unchanged?
	bkg[0] = mem_pal[back].red;
	bkg[1] = mem_pal[back].green;
	bkg[2] = mem_pal[back].blue;

// !!! Inconsistency - why not use mask for utility channels, too?
	masked = !ab && (mem_channel == CHN_IMAGE);
	if (masked)
	{
		if ((src_bpp == 3) && (dest_bpp == 3)) mask = calloc(1, l);
			else mask = img , pix = NULL;
		if (!mask) goto fail;
	}
	else if (src_bpp < dest_bpp) pix = NULL;

	if (mask) /* Set up clipboard mask */
	{ 
		src = img; dest = mask;
		for (i = 0; i < l; i++ , src += src_bpp)
			*dest++ = *src; /* Image is white on black */
		if (!aa) mem_threshold(mask, l, 128);
	}

	if ((mask == img) && (src_bpp == 3)) /* Release excess memory */
		if ((tmp = realloc(mask, l))) mask = img = tmp;

	if (!pix) pix = malloc(l * dest_bpp);
	if (!pix)
	{
fail:		free(img);
		return (FALSE);
	}

	src = img; dest = pix;

	/* Utility channel - have inversion instead of masking */
	if (mem_channel != CHN_IMAGE)
	{ 
		int i, j = ab ? 0 : 255;

		for (i = 0; i < l; i++ , src += src_bpp)
			*dest++ = *src ^ j; /* Image is white on black */
		if (!aa) mem_threshold(pix, l, 128);
	}

	/* Image with mask */
	else if (mask)
	{
		int i, j, k = w * dest_bpp, l8 = 8 * dest_bpp, k8 = k * 8;
		int h8 = h < 8 ? h : 8,  w8 = w < 8 ? k : l8;
		unsigned char *tmp = dest_bpp == 1 ? mem_col_pat : mem_col_pat24;

		for (j = 0; j < h8; j++) /* First strip */
		{
			dest = pix + w * j * dest_bpp;
			memcpy(dest, tmp + l8 * j, w8);
			for (i = l8; i < k; i++ , dest++)
				dest[l8] = *dest;
		}
		src = pix;
		for (j = 8; j < h; j++ , src += k) /* Repeat strips */
			memcpy(src + k8, src, k);
	}

	/* Indexed image */
	else if (dest_bpp == 1)
	{
		int i, j;
		unsigned char *tmp;

		for (j = 0; j < h; j++)
		{
			tmp = mem_col_pat + (j & 7) * 8;
			for (i = 0; i < w; i++ , src += src_bpp)
				*dest++ = *src < 128 ? back : tmp[i & 7];
		}
	}

	/* Non-antialiased RGB */
	else if (!aa)
	{
		int i, j;
		unsigned char *tmp;

		for (j = 0; j < h; j++)
		{
			tmp = mem_col_pat24 + (j & 7) * (8 * 3);
			for (i = 0; i < w; i++ , src += src_bpp , dest += 3)
			{
				unsigned char *t2 = *src < 128 ? bkg :
					tmp + (i & 7) * 3;
				dest[0] = t2[0];
				dest[1] = t2[1];
				dest[2] = t2[2];
			}
		}
	}

	/* Background-merged RGB */
	else
	{
		int i, j;
		unsigned char *tmp;

		for (j = 0; j < h; j++)
		{
			tmp = mem_col_pat24 + (j & 7) * (8 * 3);
			for (i = 0; i < w; i++ , src += src_bpp , dest += 3)
			{
				unsigned char *t2 = tmp + (i & 7) * 3;
				int m = *src ^ 255, r = t2[0], g = t2[1], b = t2[2];
				int kk;

				kk = 255 * r + m * (bkg[0] - r);
				dest[0] = (kk + (kk >> 8) + 1) >> 8;
				kk = 255 * g + m * (bkg[1] - g);
				dest[1] = (kk + (kk >> 8) + 1) >> 8;
				kk = 255 * b + m * (bkg[2] - b);
				dest[2] = (kk + (kk >> 8) + 1) >> 8;
			}
		}
	}

	/* Release excess memory */
	if ((pix == img) && (dest_bpp < src_bpp))
		if ((tmp = realloc(pix, l * dest_bpp))) pix = img = tmp;
	if ((img != pix) && (img != mask)) free(img);

	mem_clip_new(w, h, dest_bpp, 0, FALSE);
	mem_clipboard = pix;
	mem_clip_mask = mask;

	return (TRUE);
}

void render_text( GtkWidget *widget )
{
	GdkPixmap *text_pixmap;
	unsigned char *buf;
	int width, height, have_rgb = 0;

#if GTK_MAJOR_VERSION == 2

	PangoContext *context;
	PangoLayout *layout;
	PangoFontDescription *font_desc;
	int tx = PAD_SIZE, ty = PAD_SIZE;
#if GTK_MINOR_VERSION >= 6
	PangoMatrix matrix = PANGO_MATRIX_INIT;
	double degs, angle;
	int w2, h2;
	int rotate = inifile_get_gboolean( "fontAntialias2", FALSE );
#endif


	context = gtk_widget_create_pango_context (widget);
	layout = pango_layout_new( context );
	font_desc = pango_font_description_from_string( inifile_get( "lastTextFont", "" ) );
	pango_layout_set_font_description( layout, font_desc );
	pango_font_description_free( font_desc );

	pango_layout_set_text( layout, inifile_get( "textString", "" ), -1 );

#if GTK_MINOR_VERSION >= 6
	if (rotate)		// Rotation Toggle
	{
		degs = inifile_get_gint32("fontAngle", 0) * 0.01;
		angle = G_PI*degs/180;
		pango_matrix_rotate (&matrix, degs);
		pango_context_set_matrix (context, &matrix);
		pango_layout_context_changed( layout );
		pango_layout_get_pixel_size( layout, &width, &height );
		w2 = abs(width * cos(angle)) + abs(height * sin(angle));
		h2 = abs(width * sin(angle)) + abs(height * cos(angle));
		width = w2;
		height = h2;
	}
	else
#endif
	pango_layout_get_pixel_size( layout, &width, &height );

	width += PAD_SIZE*2;
	height += PAD_SIZE*2;

	text_pixmap = gdk_pixmap_new(widget->window, width, height, -1);

	gdk_draw_rectangle(text_pixmap, widget->style->black_gc, TRUE, 0, 0, width, height);
	gdk_draw_layout(text_pixmap, widget->style->white_gc, tx, ty, layout);

	g_object_unref( layout );
	g_object_unref( context );

#else /* #if GTK_MAJOR_VERSION == 1 */

	GdkFont *t_font = gdk_font_load( inifile_get( "lastTextFont", "" ) );
	int lbearing, rbearing, f_width, ascent, descent;


	gdk_string_extents( t_font, inifile_get( "textString", "" ),
		&lbearing, &rbearing, &f_width, &ascent, &descent );

	width = rbearing - lbearing + PAD_SIZE*2;
	height = ascent + descent + PAD_SIZE*2;

	text_pixmap = gdk_pixmap_new(widget->window, width, height, -1);
	gdk_draw_rectangle(text_pixmap, widget->style->black_gc, TRUE, 0, 0, width, height);
	gdk_draw_string(text_pixmap, t_font, widget->style->white_gc,
			PAD_SIZE - lbearing, ascent + PAD_SIZE, inifile_get("textString", ""));

	gdk_font_unref( t_font );

#endif

	buf = malloc(width * height * 3);
	if (buf) have_rgb = !!wj_get_rgb_image(widget->window, text_pixmap,
		buf, 0, 0, width, height);
	gdk_pixmap_unref(text_pixmap);		// REMOVE PIXMAP

	text_paste = TEXT_PASTE_NONE;
	if (!have_rgb) free(buf);
	else have_rgb = make_text_clipboard(buf, width, height, 3);

	if (have_rgb) text_paste = TEXT_PASTE_GTK;
	else alert_box( _("Error"), _("Not enough memory to create clipboard"),
		_("OK"), NULL, NULL );
}

static gint delete_text( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gtk_widget_destroy( text_window );
	return FALSE;
}

static gint paste_text_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gboolean antialias[3] = { gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(text_toggle[0])),
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(text_toggle[1])),
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(text_toggle[2])) };

	char *t_string = (char *) gtk_font_selection_get_preview_text(
				GTK_FONT_SELECTION(text_font_window) ),
		*t_font_name = gtk_font_selection_get_font_name( GTK_FONT_SELECTION(text_font_window) );

#if GTK_MAJOR_VERSION == 1
	if ( gtk_font_selection_get_font( GTK_FONT_SELECTION(text_font_window) ) == NULL )
		return FALSE;
#endif

#if GTK_CHECK_VERSION(2,6,0)
	inifile_set_gint32("fontAngle", rint(read_float_spin(text_spin[1]) * 100.0));
#endif

	if (mem_channel == CHN_IMAGE)
	{
		inifile_set_gint32( "fontBackground", read_spin(text_spin[0]));
	}

	inifile_set( "lastTextFont", t_font_name );
	inifile_set( "textString", t_string );
	inifile_set_gboolean( "fontAntialias0", antialias[0] );
	inifile_set_gboolean( "fontAntialias1", antialias[1] );
	inifile_set_gboolean( "fontAntialias2", antialias[2] );

	render_text(widget);
	update_stuff(UPD_XCOPY);
	if (mem_clipboard) pressed_paste(TRUE);

	delete_text( widget, event, data );

	if (t_font_name) g_free(t_font_name);

	return FALSE;
}

void pressed_text()
{
	GtkWidget *vbox, *hbox;

	text_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Paste Text"), GTK_WIN_POS_CENTER, TRUE );
	gtk_window_set_default_size( GTK_WINDOW(text_window), 400, 400 );

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (text_window), vbox);

	text_font_window = xpack(vbox, gtk_font_selection_new());
	gtk_widget_show(text_font_window);
	gtk_container_set_border_width (GTK_CONTAINER (text_font_window), 4);

	add_hseparator( vbox, 200, 10 );

	hbox = pack5(vbox, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox);

	text_toggle[0] = add_a_toggle( _("Antialias"), hbox,
			inifile_get_gboolean( "fontAntialias0", FALSE ) );

#if defined(U_MTK) || GTK_MAJOR_VERSION == 2
	if (mem_img_bpp == 1)
#endif
		gtk_widget_hide(text_toggle[0]);

	if (mem_channel != CHN_IMAGE)
	{
		text_toggle[1] = add_a_toggle( _("Invert"), hbox,
			inifile_get_gboolean( "fontAntialias1", FALSE ) );
	}
	else
	{
		text_toggle[1] = add_a_toggle( _("Background colour ="), hbox,
			inifile_get_gboolean( "fontAntialias1", FALSE ) );

		text_spin[0] = pack5(hbox, add_a_spin(
			inifile_get_gint32("fontBackground", 0)	% mem_cols,
			0, mem_cols - 1));
	}

	text_toggle[2] = add_a_toggle( _("Angle of rotation ="), hbox, FALSE );

#if GTK_CHECK_VERSION(2,6,0)
	text_spin[1] = pack5(hbox, add_float_spin(
		inifile_get_gint32("fontAngle", 0) * 0.01, -360, 360));
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(text_toggle[2]), 
		inifile_get_gboolean( "fontAntialias2", FALSE ) );
#else
	gtk_widget_hide( text_toggle[2] );
#endif

	add_hseparator( vbox, 200, 10 );

	hbox = pack5(vbox, OK_box(0, text_window,
		_("Paste Text"), GTK_SIGNAL_FUNC(paste_text_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(delete_text)));

	gtk_widget_show(text_window);
	gtk_window_set_transient_for( GTK_WINDOW(text_window), GTK_WINDOW(main_window) );

	gtk_font_selection_set_font_name( GTK_FONT_SELECTION(text_font_window),
		inifile_get( "lastTextFont", "-misc-fixed-bold-r-normal-*-*-120-*-*-c-*-iso8859-1" ) );
	gtk_font_selection_set_preview_text( GTK_FONT_SELECTION(text_font_window),
		inifile_get( "textString", _("Enter Text Here") ) );

	gtk_widget_grab_focus( GTK_FONT_SELECTION(text_font_window)->preview_entry );
}

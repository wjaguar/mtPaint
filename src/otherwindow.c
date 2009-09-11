/*	otherwindow.c
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

#include "global.h"

#include "mygtk.h"
#include "memory.h"
#include "otherwindow.h"
#include "png.h"
#include "mainwindow.h"
#include "viewer.h"
#include "inifile.h"
#include "canvas.h"
#include "quantizer.h"
#include "layer.h"
#include "wu.h"
#include "ani.h"
#include "channels.h"
#include "toolbar.h"
#include "csel.h"

#if GTK_MAJOR_VERSION == 1
#include <unistd.h>
#endif

///	NEW IMAGE WINDOW

int im_type, new_window_type = 0;
GtkWidget *new_window;
GtkWidget *spinbutton_height, *spinbutton_width, *spinbutton_cols;


gint delete_new( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gtk_widget_destroy(new_window);

	return FALSE;
}

void reset_tools()
{
	float old_zoom = can_zoom;

	init_istate();
	init_pal();

	can_zoom = -1;
	if ( inifile_get_gboolean("zoomToggle", FALSE) )
		align_size(1);			// Always start at 100%
	else
		align_size(old_zoom);

	pressed_opacity( 255 );		// Set opacity to 100% to start with
	update_menus();
}

void do_new_chores()
{
	reset_tools();
	set_new_filename( _("Untitled") );
	update_all_views();
	gtk_widget_queue_draw(drawing_col_prev);
}

int do_new_one(int nw, int nh, int nc, int nt, int bpp)
{
	int res;

	mem_pal_copy(mem_pal, mem_pal_def);
#ifdef U_GUADALINEX
	if (nt == 1) mem_scale_pal(mem_pal, 0, 255,255,255, nc - 1, 0,0,0);
#else
	if (nt == 1) mem_scale_pal(mem_pal, 0, 0,0,0, nc - 1, 255,255,255);
#endif

	nw = nw < MIN_WIDTH ? MIN_WIDTH : nw > MAX_WIDTH ? MAX_WIDTH : nw;
	nh = nh < MIN_HEIGHT ? MIN_HEIGHT : nh > MAX_HEIGHT ? MAX_HEIGHT : nh;

	mem_cols = nc < 2 ? 2 : nc > 256 ? 256 : nc;
	res = mem_new( nw, nh, bpp, CMASK_IMAGE );
	if ( res!= 0 )			// Not enough memory!
	{
		memory_errors(1);
	}
	do_new_chores();

	return res;
}

static void create_new(GtkWidget *widget)
{
	int nw, nh, nc, bpp = 1, err=0;

	nw = read_spin(spinbutton_width);
	nh = read_spin(spinbutton_height);
	nc = read_spin(spinbutton_cols);

	if (im_type == 0) bpp = 3;

	if (im_type == 3)	// Grab Screenshot
	{
#if GTK_MAJOR_VERSION == 1
		gdk_window_lower( main_window->window );
		gdk_window_lower( new_window->window );

		gdk_flush();
		while (gtk_events_pending()) gtk_main_iteration();	// Wait for minimize

		sleep(1);			// Wait a second for screen to redraw

		grab_screen();
		do_new_chores();
		notify_changed();

		gdk_window_raise( main_window->window );
#endif
#if GTK_MAJOR_VERSION == 2
		gtk_window_set_transient_for( GTK_WINDOW(new_window), NULL );
		gdk_window_iconify( new_window->window );
		gdk_window_iconify( main_window->window );

		gdk_flush();
		while (gtk_events_pending()) gtk_main_iteration();	// Wait for minimize

		g_usleep(400000);		// Wait 0.4 of a second for screen to redraw

		grab_screen();
		do_new_chores();
		notify_changed();

		gdk_window_deiconify( main_window->window );
		gdk_window_raise( main_window->window );
#endif
	}

	if ((im_type < 3) && (new_window_type == 0))		// New image
	{
		err = do_new_one( nw, nh, nc, im_type, bpp );

		if ( err>0 )		// System was unable to allocate memory for image, using 8x8 instead
		{
			nw = mem_width;
			nh = mem_height;  
		}

		if ( layers_total>0 ) layers_notify_changed();

		inifile_set_gint32("lastnewWidth", nw );
		inifile_set_gint32("lastnewHeight", nh );
		inifile_set_gint32("lastnewCols", nc );
		inifile_set_gint32("lastnewType", im_type );

		/* Lose a selection marquee */
		pressed_select_none(NULL, NULL);
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
	}

	if (new_window_type == 1) layer_new(nw, nh, 3 - im_type, nc, CMASK_IMAGE);
	else
	{
		gtk_adjustment_value_changed( gtk_scrolled_window_get_hadjustment(
			GTK_SCROLLED_WINDOW(scrolledwindow_canvas) ) );
		gtk_adjustment_value_changed( gtk_scrolled_window_get_vadjustment(
			GTK_SCROLLED_WINDOW(scrolledwindow_canvas) ) );
		// These 2 are needed to synchronize the scrollbars & image view
	}

	gtk_widget_destroy(new_window);
}

void generic_new_window(int type)	// 0=New image, 1=New layer
{
	char *rad_txt[] = {_("24 bit RGB"), _("Greyscale"), _("Indexed Palette"), _("Grab Screenshot")},
		*title_txt[] = {_("New Image"), _("New Layer")};
	int w = mem_width, h = mem_height, c = mem_cols;

	GtkWidget *vbox1, *table1;

	new_window_type = type;

	if ( type == 0 && check_for_changes() == 1 ) return;

	new_window = add_a_window( GTK_WINDOW_TOPLEVEL, title_txt[type], GTK_WIN_POS_CENTER, TRUE );

	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox1);
	gtk_container_add (GTK_CONTAINER (new_window), vbox1);

	table1 = add_a_table( 3, 2, 5, vbox1 );

	if ( type == 0 )
	{
		w = inifile_get_gint32("lastnewWidth", DEFAULT_WIDTH);
		h = inifile_get_gint32("lastnewHeight", DEFAULT_HEIGHT);
		c = inifile_get_gint32("lastnewCols", 256);
		im_type = inifile_get_gint32("lastnewType", 2);
		if ( im_type<0 || im_type>2 ) im_type = 0;
	}
	else im_type = 3 - mem_img_bpp;

	spinbutton_width = spin_to_table(table1, 0, 1, 5, w, MIN_WIDTH, MAX_WIDTH);
	spinbutton_height = spin_to_table(table1, 1, 1, 5, h, MIN_WIDTH, MAX_HEIGHT);
	spinbutton_cols = spin_to_table(table1, 2, 1, 5, c, 2, 256);

	add_to_table( _("Width"), table1, 0, 0, 5 );
	add_to_table( _("Height"), table1, 1, 0, 5 );
	add_to_table( _("Colours"), table1, 2, 0, 5 );

	xpack(vbox1, wj_radio_pack(rad_txt, type ? 3 : 4, 0, im_type, &im_type, NULL));

	add_hseparator( vbox1, 200, 10 );

	pack(vbox1, OK_box(5, new_window, _("Create"), GTK_SIGNAL_FUNC(create_new),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));

	gtk_window_set_transient_for( GTK_WINDOW(new_window), GTK_WINDOW(main_window) );
	gtk_widget_show (new_window);
}

void pressed_new( GtkMenuItem *menu_item, gpointer user_data )
{
	generic_new_window(0);
}


///	PATTERN & BRUSH CHOOSER WINDOW

static GtkWidget *pat_window, *draw_pat;
static int pat_brush;
static unsigned char *mem_patch = NULL;


static gint delete_pat( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( pat_brush == 0 )
	{
		if ( mem_patch != NULL ) free(mem_patch);
		mem_patch = NULL;
	}
	gtk_widget_destroy(pat_window);

	return FALSE;
}

static gint key_pat( GtkWidget *widget, GdkEventKey *event )
{
	/* xine-ui sends bogus keypresses so don't delete on this */
	if (!XINE_FAKERY(event->keyval)) delete_pat( widget, NULL, NULL );

	return FALSE;
}

static gint click_pat( GtkWidget *widget, GdkEventButton *event )
{
	int pat_no = 0, mx, my;

	mx = event->x;
	my = event->y;

	pat_no = mx / (PATCH_WIDTH/9) + 9*( my / (PATCH_HEIGHT/9) );
	mtMAX(pat_no, pat_no, 0)
	mtMIN(pat_no, pat_no, 80)

	if ( event->button == 1 )
	{
		if ( pat_brush == 0 )
		{
			tool_pat = pat_no;
			mem_pat_update();				// Update memory
			gtk_widget_queue_draw( drawing_col_prev );	// Update widget
			if ( marq_status >= MARQUEE_PASTE && text_paste )
			{
				render_text( drawing_col_prev );
				check_marquee();
				gtk_widget_queue_draw( drawing_canvas );
			}
		}
		else
		{
			mem_set_brush(pat_no);
			brush_tool_type = tool_type;
			toolbar_update_settings();	// Update spin buttons
			gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[PAINT_TOOL_ICON]),
					TRUE );			// Update toolbar
			set_cursor();
		}
	}

	return FALSE;
}

static gint expose_pat( GtkWidget *widget, GdkEventExpose *event )
{
	gdk_draw_rgb_image( draw_pat->window, draw_pat->style->black_gc,
				event->area.x, event->area.y, event->area.width, event->area.height,
				GDK_RGB_DITHER_NONE,
				mem_patch + 3*( event->area.x + PATCH_WIDTH*event->area.y ),
				PATCH_WIDTH*3
				);
	return FALSE;
}

void choose_pattern(int typ)			// Bring up pattern chooser (0) or brush (1)
{
	int pattern, pixel, r, g, b, row, column, sx, sy, ex, ey;

	pat_brush = typ;

	if ( typ == 0 )
	{
		mem_patch = calloc(1, PATCH_WIDTH * PATCH_HEIGHT * 3);

		for ( pattern = 0; pattern < 81; pattern++ )
		{
			sy = 2 + (pattern / 9) * 36;		// Start y pixel on main image
			sx = 2 + (pattern % 9) * 36;		// Start x pixel on main image
			for ( column = 0; column < 8; column++ )
			{
				for ( row = 0; row < 8; row++ )
				{
					pixel = mem_patterns[pattern][column][row];
					if ( pixel == 1 )
					{
						r = mem_col_A24.red;
						g = mem_col_A24.green;
						b = mem_col_A24.blue;
					}
					else
					{
						r = mem_col_B24.red;
						g = mem_col_B24.green;
						b = mem_col_B24.blue;
					}
					for ( ey=0; ey<4; ey++ )
					{
					 for ( ex=0; ex<4; ex++ )
					 {
					  mem_patch[ 0+3*( sx+row+8*ex + PATCH_WIDTH*(sy+column+8*ey) ) ] = r;
					  mem_patch[ 1+3*( sx+row+8*ex + PATCH_WIDTH*(sy+column+8*ey) ) ] = g;
					  mem_patch[ 2+3*( sx+row+8*ex + PATCH_WIDTH*(sy+column+8*ey) ) ] = b;
					 }
					}
				}
			}
		}
	}
	else
	{
		mem_patch = mem_brushes;
	}

	pat_window = add_a_window( GTK_WINDOW_POPUP, _("Pattern Chooser"), GTK_WIN_POS_MOUSE, TRUE );
	gtk_container_set_border_width (GTK_CONTAINER (pat_window), 4);

	draw_pat = gtk_drawing_area_new ();
	gtk_widget_set_usize( draw_pat, PATCH_WIDTH, PATCH_HEIGHT );
	gtk_container_add (GTK_CONTAINER (pat_window), draw_pat);
	gtk_widget_show( draw_pat );
	gtk_signal_connect_object( GTK_OBJECT(draw_pat), "expose_event",
		GTK_SIGNAL_FUNC (expose_pat), GTK_OBJECT(draw_pat) );
	gtk_signal_connect_object( GTK_OBJECT(draw_pat), "button_press_event",
		GTK_SIGNAL_FUNC (click_pat), GTK_OBJECT(draw_pat) );
	gtk_signal_connect_object( GTK_OBJECT(draw_pat), "button_release_event",
		GTK_SIGNAL_FUNC (delete_pat), NULL );
	gtk_signal_connect_object (GTK_OBJECT (pat_window), "key_press_event",
		GTK_SIGNAL_FUNC (key_pat), NULL);
	gtk_widget_set_events (draw_pat, GDK_ALL_EVENTS_MASK);

	gtk_widget_show (pat_window);
}



///	ADD COLOURS TO PALETTE WINDOW


GtkWidget *add_col_window;
GtkWidget *spinbutton_col_add;


static void click_col_add_ok(GtkWidget *widget)
{
	int i, to_add;

	to_add = read_spin(spinbutton_col_add);

	if ( to_add != mem_cols )
	{
		spot_undo(UNDO_PAL);

		if ( to_add>mem_cols )
			for ( i=mem_cols; i<to_add; i++ )
			{
				mem_pal[i].red = 0;
				mem_pal[i].green = 0;
				mem_pal[i].blue = 0;
			}

		mem_cols = to_add;
		if ( mem_img_bpp == 1 )
		{
			if ( mem_col_A >= mem_cols ) mem_col_A = 0;
			if ( mem_col_B >= mem_cols ) mem_col_B = 0;
		}
		init_pal();
	}

	gtk_widget_destroy(add_col_window);
}


void pressed_add_cols( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *vbox5;

	add_col_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Set Palette Size"),
		GTK_WIN_POS_CENTER, TRUE );

	gtk_widget_set_usize (add_col_window, 320, -2);

	vbox5 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox5);
	gtk_container_add (GTK_CONTAINER (add_col_window), vbox5);

	add_hseparator( vbox5, -2, 10 );

	spinbutton_col_add = add_a_spin( 256, 2, 256 );
	gtk_box_pack_start (GTK_BOX (vbox5), spinbutton_col_add, FALSE, FALSE, 5);

	add_hseparator( vbox5, -2, 10 );

	pack(vbox5, OK_box(0, add_col_window, _("OK"), GTK_SIGNAL_FUNC(click_col_add_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));

	gtk_widget_show (add_col_window);
}

void pal_refresher()
{
	update_all_views();
	init_pal();
	gtk_widget_queue_draw(drawing_col_prev);
}


/* Generic code to handle UI needs of common image transform tasks */

GtkWidget *filter_win, *filter_cont;
filter_hook filter_func;
gpointer filter_data;

void run_filter(GtkButton *button, gpointer user_data)
{
	if (filter_func(filter_cont, filter_data))
		gtk_widget_destroy(filter_win);
	update_all_views();
}

void filter_window(gchar *title, GtkWidget *content, filter_hook filt, gpointer fdata, int istool)
{
	GtkWidget *vbox6;
	GtkWindowPosition pos = istool && !inifile_get_gboolean("centerSettings", TRUE) ?
		GTK_WIN_POS_MOUSE : GTK_WIN_POS_CENTER;

	filter_cont = content;
	filter_func = filt;
	filter_data = fdata;
	filter_win = add_a_window(GTK_WINDOW_TOPLEVEL, title, pos, TRUE);
	gtk_window_set_default_size(GTK_WINDOW(filter_win), 300, -1);

	vbox6 = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(vbox6);
	gtk_container_add(GTK_CONTAINER(filter_win), vbox6);

	add_hseparator(vbox6, -2, 10);

	gtk_box_pack_start(GTK_BOX(vbox6), content, FALSE, FALSE, 5);

	add_hseparator(vbox6, -2, 10);

	pack(vbox6, OK_box(0, filter_win, _("Apply"), GTK_SIGNAL_FUNC(run_filter),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));

	gtk_widget_show(filter_win);
}

///	BACTERIA EFFECT

int do_bacteria(GtkWidget *spin, gpointer fdata)
{
	int i;

	i = read_spin(spin);
	spot_undo(UNDO_FILT);
	mem_bacteria(i);

	return FALSE;
}

void pressed_bacteria(GtkMenuItem *menu_item, gpointer user_data)
{
	GtkWidget *spin = add_a_spin(10, 1, 100);
	filter_window(_("Bacteria Effect"), spin, do_bacteria, NULL, FALSE);
}


///	SORT PALETTE COLOURS

static GtkWidget *spal_window, *spal_spins[2], *spal_rev;
int spal_mode;

gint click_spal_apply( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int index1 = 0, index2 = 1;
	gboolean reverse;

	reverse = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(spal_rev));
	inifile_set_gboolean( "palrevSort", reverse );

	index1 = read_spin(spal_spins[0]);
	index2 = read_spin(spal_spins[1]);

	if ( index1 == index2 ) return FALSE;

	spot_undo(UNDO_XPAL);
	mem_pal_sort(spal_mode, index1, index2, reverse);
	init_pal();
	update_all_views();
	gtk_widget_queue_draw( drawing_col_prev );

	return FALSE;
}

gint click_spal_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	click_spal_apply( NULL, NULL, NULL );
	gtk_widget_destroy(spal_window);

	return FALSE;
}

void pressed_sort_pal( GtkMenuItem *menu_item, gpointer user_data )
{
	char *rad_txt[] = {
		_("Hue"), _("Saturation"), _("Luminance"), _("Distance to A"),
			_("Distance to A+B"),
		_("Red"), _("Green"), _("Blue"), _("Projection to A->B"),
			_("Frequency")};


	GtkWidget *vbox1, *hbox3, *table1;

	spal_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Sort Palette Colours"),
		GTK_WIN_POS_CENTER, TRUE );

	vbox1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox1);
	gtk_container_add (GTK_CONTAINER (spal_window), vbox1);

	table1 = add_a_table(2, 2, 5, vbox1);

	spal_spins[0] = spin_to_table(table1, 0, 1, 5, 0, 0, mem_cols - 1);
	spal_spins[1] = spin_to_table(table1, 1, 1, 5, mem_cols - 1, 0, mem_cols - 1);

	add_to_table( _("Start Index"), table1, 0, 0, 5 );
	add_to_table( _("End Index"), table1, 1, 0, 5 );

	table1 = pack(vbox1, wj_radio_pack(rad_txt, mem_img_bpp == 3 ? 9 : 10, 5,
		spal_mode, &spal_mode, NULL));
	gtk_container_set_border_width(GTK_CONTAINER(table1), 5);

	spal_rev = add_a_toggle(_("Reverse Order"), vbox1,
		inifile_get_gboolean("palrevSort", FALSE));

	add_hseparator( vbox1, 200, 10 );

	/* Cancel / Apply / OK */

	hbox3 = pack(vbox1, OK_box(5, spal_window, _("OK"), GTK_SIGNAL_FUNC(click_spal_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));
	OK_box_add(hbox3, _("Apply"), GTK_SIGNAL_FUNC(click_spal_apply), 1);

	gtk_widget_show (spal_window);
}


///	BRIGHTNESS-CONTRAST-SATURATION WINDOW

#define BRCOSA_ITEMS 6

static GtkWidget *brcosa_window,
		*brcosa_toggles[6],
		*brcosa_spins[BRCOSA_ITEMS+2],	// Extra 2 used for palette limits
		*brcosa_buttons[5];

static int brcosa_values[BRCOSA_ITEMS], brcosa_pal_lim[2],
	brcosa_values_default[BRCOSA_ITEMS] = {0, 0, 0, 8, 100, 0};
png_color brcosa_pal[256];

static void brcosa_buttons_sensitive() // Set 4 brcosa button as sensitive if the user has assigned changes
{
	int i;
	gboolean state = FALSE;

	if ( brcosa_buttons[0] == NULL ) return;

	for ( i=0; i<BRCOSA_ITEMS; i++ )
		if ( brcosa_values[i] != brcosa_values_default[i] ) state = TRUE;
	for ( i=2; i<5; i++ ) gtk_widget_set_sensitive( brcosa_buttons[i], state );
}

static gint click_brcosa_preview( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i, p1, p2;
	gboolean do_pal = FALSE;	// RGB palette processing

	mem_pal_copy( mem_pal, brcosa_pal );	// Get back normal palette
	if (mem_img_bpp == 3)
	{
		do_pal = gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(brcosa_toggles[4]) );
		if ( !do_pal && widget == brcosa_toggles[4] )
		{
			pal_refresher();			// User has just cleared toggle
		}
	}

	for ( i=0; i<BRCOSA_ITEMS; i++ )
	{
		mem_prev_bcsp[i] = brcosa_values[i];
		if ( i<3 ) mem_brcosa_allow[i] =
			gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(brcosa_toggles[i+1]) );
	}

	if ( mem_img_bpp == 1 || do_pal )
	{
		mtMIN( p1, brcosa_pal_lim[0], brcosa_pal_lim[1] )
		mtMAX( p2, brcosa_pal_lim[0], brcosa_pal_lim[1] )
		transform_pal(mem_pal, brcosa_pal, p1, p2);
		pal_refresher();
	}
	if (mem_img_bpp == 3) main_update_area(0, 0, mem_width, mem_height);

	return FALSE;
}

static void brcosa_pal_lim_change()
{
	int i;

	for ( i=0; i<2; i++ )
		brcosa_pal_lim[i] = gtk_spin_button_get_value_as_int(
			GTK_SPIN_BUTTON(brcosa_spins[BRCOSA_ITEMS+i]) );

	click_brcosa_preview( NULL, NULL, NULL );	// Update everything
}

static gint click_brcosa_preview_toggle( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( brcosa_buttons[1] == NULL ) return FALSE;		// Traps call during initialisation

	if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(brcosa_toggles[0]) ) )
	{
		click_brcosa_preview( widget, NULL, NULL );
		gtk_widget_hide(brcosa_buttons[1]);
	}
	else 	gtk_widget_show(brcosa_buttons[1]);

	return FALSE;
}

static gint click_brcosa_RGB_toggle( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(brcosa_toggles[5]) ) )
		mem_preview = 1;
	else	mem_preview = 0;

	click_brcosa_preview( widget, NULL, NULL );

	return FALSE;
}

static void brcosa_spinslide_moved(GtkAdjustment *adj, gpointer user_data)
{
	brcosa_values[(int)user_data] = ADJ2INT(adj);
	brcosa_buttons_sensitive();

	if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(brcosa_toggles[0])))
		click_brcosa_preview( NULL, NULL, NULL );
}

static gint delete_brcosa( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	inifile_set_gboolean( "autopreviewToggle",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(brcosa_toggles[0]) ) );
	gtk_widget_destroy(brcosa_window);

	mem_preview = 0;		// If in RGB mode this is required to disable live preview

	return FALSE;
}

static gint click_brcosa_cancel( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	mem_pal_copy( mem_pal, brcosa_pal );
	pal_refresher();
	delete_brcosa( NULL, NULL, NULL );

	return FALSE;
}

static gint click_brcosa_apply( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	unsigned char *mask, *mask0, *tmp;
	int i;

	mem_pal_copy( mem_pal, brcosa_pal );

	if ( brcosa_values[0] != 0 || brcosa_values[1] != 0 ||
		brcosa_values[2] != 0 || brcosa_values[3] != 8 || brcosa_values[4] != 100 ||
		brcosa_values[5] != 0 )
	{
		spot_undo(UNDO_COL);

		click_brcosa_preview( NULL, NULL, NULL );
		update_all_views();
		if ( mem_img_bpp == 3 && mem_preview == 1 )	// Only do if toggle set
		{
			mask = malloc(mem_width);
			if (mask)
			{
				mask0 = NULL;
				if (!channel_dis[CHN_MASK]) mask0 = mem_img[CHN_MASK];
				tmp = mem_img[CHN_IMAGE];
				for (i = 0; i < mem_height; i++)
				{
					prep_mask(0, 1, mem_width, mask, mask0, tmp);
					do_transform(0, 1, mem_width, mask, tmp, tmp);
					if (mask0) mask0 += mem_width;
					tmp += mem_width * 3;
				}
				free(mask);
			}
		}

		if ( mem_img_bpp == 1 ) mem_pal_copy( brcosa_pal, mem_pal );
		else
		{
			if ( gtk_toggle_button_get_active( GTK_TOGGLE_BUTTON(brcosa_toggles[4]))
				&& (widget != NULL)	// Don't do this when clicking OK
				)
			{
				mem_pal_copy( brcosa_pal, mem_pal );
				click_brcosa_preview(NULL, NULL, NULL);
			}
		}	// Update palette values in RGB/indexed mode as required
	}

	return FALSE;
}

static void click_brcosa_show_toggle( GtkWidget *widget, GtkWidget *data )
{
	gboolean toggle = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget));

	inifile_set_gboolean( "transcol_show", toggle );
	if ( toggle ) gtk_widget_show( GTK_WIDGET(data) );
	else
	{
		gtk_widget_hide( GTK_WIDGET(data) );
	}
}

/*
static void click_brcosa_store()
{
}
*/

static gint click_brcosa_ok()
{
	click_brcosa_apply( NULL, NULL, NULL );
	delete_brcosa( NULL, NULL, NULL );

	return FALSE;
}

static gint click_brcosa_reset()
{
	int i;

	mem_pal_copy( mem_pal, brcosa_pal );

	for (i = 0; i < BRCOSA_ITEMS; i++)
	{
		mt_spinslide_set_value(brcosa_spins[i], brcosa_values_default[i]);
	}
	pal_refresher();

	return FALSE;
}

void pressed_brcosa( GtkMenuItem *menu_item, gpointer user_data )
{
	GtkWidget *vbox, *vbox5, *table2, *hbox, *label, *button;

	static int mins[] = {-255, -100, -100, 1, 20, -1529},
		maxs[] = {255, 100, 100, 8, 500, 1529},
		order[] = {1, 2, 3, 5, 0, 4};
	char	*tog_txt[] = {	_("Auto-Preview"), _("Red"), _("Green"), _("Blue"), _("Palette"),
				_("Image") },
		*tab_txt[] = {	_("Brightness"), _("Contrast"), _("Saturation"), _("Posterize"),
				_("Gamma"), _("Hue") };
	int i;

	GtkAccelGroup* ag = gtk_accel_group_new();

	mem_pal_copy( brcosa_pal, mem_pal );		// Remember original palette

	for ( i=0; i<BRCOSA_ITEMS; i++ ) mem_prev_bcsp[i] = brcosa_values_default[i];

	for ( i=0; i<4; i++ ) brcosa_buttons[i] = NULL;
			// Enables preview_toggle code to detect an initialisation call

	mem_preview = 1;		// If in RGB mode this is required to enable live preview

	brcosa_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Transform Colour"),
		GTK_WIN_POS_MOUSE, TRUE );

	gtk_window_set_policy( GTK_WINDOW(brcosa_window), FALSE, FALSE, TRUE );
			// Automatically grow/shrink window

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (brcosa_window), vbox);

	table2 = add_a_table(6, 2, 10, vbox );
	for (i = 0; i < BRCOSA_ITEMS; i++)
	{
		add_to_table(tab_txt[i], table2, order[i], 0, 0);
		brcosa_values[i] = brcosa_values_default[i];
		brcosa_spins[i] = mt_spinslide_new(255, 20);
		gtk_table_attach(GTK_TABLE(table2), brcosa_spins[i], 1, 2,
			order[i], order[i] + 1, GTK_EXPAND | GTK_FILL,
			GTK_FILL, 0, 0);
		mt_spinslide_set_range(brcosa_spins[i], mins[i], maxs[i]);
		mt_spinslide_set_value(brcosa_spins[i], brcosa_values_default[i]);
		mt_spinslide_connect(brcosa_spins[i],
			GTK_SIGNAL_FUNC(brcosa_spinslide_moved), (gpointer)i);
	}



///	MIDDLE SECTION

	add_hseparator( vbox, -2, 10 );

	hbox = pack(vbox, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox);

	vbox5 = pack(vbox, gtk_vbox_new(FALSE, 0));
	if (inifile_get_gboolean("transcol_show", FALSE))
		gtk_widget_show (vbox5);

	button = add_a_toggle(_("Show Detail"), hbox,
		inifile_get_gboolean("transcol_show", FALSE));
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(click_brcosa_show_toggle), vbox5);
/*
	button = gtk_button_new_with_label(_("Store Values"));
	gtk_widget_show (button);
	gtk_box_pack_end (GTK_BOX (hbox), button, FALSE, FALSE, 4);

	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_store), NULL);
*/

///	OPTIONAL SECTION

	hbox = pack(vbox5, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox);

	if ( mem_img_bpp == 1 )
	{
		label = gtk_label_new( _("Palette") );
		gtk_box_pack_start( GTK_BOX(hbox), label, FALSE, FALSE, 5 );
		gtk_widget_show( label );
	}
	else
	{
		for ( i=5; i>3; i-- )
		{
			brcosa_toggles[i] = add_a_toggle( tog_txt[i], hbox, TRUE );
			gtk_signal_connect(GTK_OBJECT(brcosa_toggles[i]), "clicked",
				GTK_SIGNAL_FUNC(click_brcosa_RGB_toggle), NULL);
		}
	}
	brcosa_pal_lim[0] = 0;
	brcosa_pal_lim[1] = mem_cols - 1;
	for (i = 0; i < 2; i++)
	{
		brcosa_spins[BRCOSA_ITEMS + i] = pack(hbox,
			add_a_spin(brcosa_pal_lim[i], 0, mem_cols - 1));
		spin_connect(brcosa_spins[BRCOSA_ITEMS + i],
			GTK_SIGNAL_FUNC(brcosa_pal_lim_change), NULL);
	}

	hbox = pack(vbox5, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox);

	for ( i=0; i<4; i++ )
	{
		brcosa_toggles[i] = add_a_toggle( tog_txt[i], hbox, TRUE );
		if ( i == 0 ) gtk_signal_connect(GTK_OBJECT(brcosa_toggles[i]), "clicked",
			GTK_SIGNAL_FUNC(click_brcosa_preview_toggle), NULL);
		if ( i>0 ) gtk_signal_connect(GTK_OBJECT(brcosa_toggles[i]), "clicked",
			GTK_SIGNAL_FUNC(click_brcosa_preview), NULL);
	}

	if ( mem_img_bpp == 3 )
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( brcosa_toggles[4] ), FALSE);

	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON( brcosa_toggles[0] ),
		inifile_get_gboolean("autopreviewToggle", TRUE));



///	BOTTOM AREA

	add_hseparator( vbox, -2, 10 );

	hbox = pack(vbox, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox);
	gtk_container_set_border_width (GTK_CONTAINER (hbox), 5);

	button = add_a_button(_("Cancel"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_cancel), NULL);
	gtk_signal_connect_object (GTK_OBJECT (brcosa_window), "delete_event",
		GTK_SIGNAL_FUNC (click_brcosa_cancel), NULL);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);
	brcosa_buttons[0] = button;

	button = add_a_button(_("Preview"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_preview), NULL);
	brcosa_buttons[1] = button;

	button = add_a_button(_("Reset"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_reset), NULL);
	brcosa_buttons[2] = button;

	button = add_a_button(_("Apply"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_apply), NULL);
	brcosa_buttons[3] = button;

	button = add_a_button(_("OK"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(click_brcosa_ok), NULL);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	brcosa_buttons[4] = button;

	gtk_widget_show (brcosa_window);
	gtk_window_add_accel_group(GTK_WINDOW (brcosa_window), ag);

#if GTK_MAJOR_VERSION == 1
	gtk_widget_queue_resize(brcosa_window); /* Reset shortened sliders */
#endif

	click_brcosa_preview_toggle( NULL, NULL, NULL );		// Show/hide preview button
	brcosa_buttons_sensitive();					// Disable buttons
	gtk_window_set_transient_for( GTK_WINDOW(brcosa_window), GTK_WINDOW(main_window) );
}


///	RESIZE/RESCALE WINDOWS

GtkWidget *sisca_window, *sisca_table;
GtkWidget *sisca_spins[6], *sisca_toggles[2], *sisca_gc;
gboolean sisca_scale;


static void sisca_moved(GtkAdjustment *adjustment, gpointer user_data)
{
	int w, h, nw, idx = (int)user_data;


	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sisca_toggles[0])))
		return;
	w = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sisca_spins[0]));
	h = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(sisca_spins[1]));
	if (idx)
	{
		nw = rint(h * mem_width / (float)mem_height);
		nw = nw < 1 ? 1 : nw > MAX_WIDTH ? MAX_WIDTH : nw;
		if (nw == w) return;
	}
	else
	{
		nw = rint(w * mem_height / (float)mem_width);
		nw = nw < 1 ? 1 : nw > MAX_HEIGHT ? MAX_HEIGHT : nw;
		if (nw == h) return;
	}
	idx ^= 1; /* Other coordinate */
	gtk_spin_button_update(GTK_SPIN_BUTTON(sisca_spins[idx]));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(sisca_spins[idx]), nw);
}

static int scale_mode = 6;
static int resize_mode = 0;
int sharper_reduce;

static void click_sisca_ok(GtkWidget *widget, gpointer user_data)
{
	int nw, nh, ox = 0, oy = 0, res = 1, scale_type = 0, gcor = FALSE;

	nw = read_spin(sisca_spins[0]);
	nh = read_spin(sisca_spins[1]);
	if (!sisca_scale)
	{
		ox = read_spin(sisca_spins[2]);
		oy = read_spin(sisca_spins[3]);
	}

	if ((nw == mem_width) && (nh == mem_height) && !ox && !oy)
	{
		alert_box(_("Error"), _("New geometry is the same as now - nothing to do."),
			_("OK"), NULL, NULL);
		return;
	}

	if (sisca_scale)
	{
		if (mem_img_bpp == 3)
		{
			scale_type = scale_mode;
			gcor = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(sisca_gc));
		}
		res = mem_image_scale(nw, nh, scale_type, gcor, sharper_reduce);
	}
	else res = mem_image_resize(nw, nh, ox, oy, resize_mode);

	if (!res)
	{
		canvas_undo_chores();
		destroy_dialog(sisca_window);
	}
	else memory_errors(res);
}

void memory_errors(int type)
{
	if ( type == 1 )
		alert_box(_("Error"), _("The operating system cannot allocate the memory for this operation."), _("OK"), NULL, NULL);
	if ( type == 2 )
		alert_box(_("Error"), _("You have not allocated enough memory in the Preferences window for this operation."), _("OK"), NULL, NULL);
}

gint click_sisca_centre( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int nw = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[0]) );
	int nh = gtk_spin_button_get_value_as_int( GTK_SPIN_BUTTON(sisca_spins[1]) );

	nw = (nw - mem_width) / 2;
	nh = (nh - mem_height) / 2;

	gtk_spin_button_set_value( GTK_SPIN_BUTTON(sisca_spins[2]), nw );
	gtk_spin_button_set_value( GTK_SPIN_BUTTON(sisca_spins[3]), nh );

	return FALSE;
}

void sisca_init( char *title )
{
	gchar* scale_fnames[] = {
		_("Nearest Neighbour"),
		_("Bilinear / Area Mapping"),
		_("Bicubic"),
		_("Bicubic edged"),
		_("Bicubic better"),
		_("Bicubic sharper"),
		_("Blackman-Harris"),
		NULL
	};
	gchar* resize_modes[] = {
		_("Clear"),
		_("Tile"),
		_("Mirror tile"),
		NULL
	};

	GtkWidget *button_centre, *sisca_vbox, *sisca_hbox;
	GtkWidget *wvbox, *wvbox2, *notebook, *page0, *page1, *button;

	sisca_window = add_a_window( GTK_WINDOW_TOPLEVEL, title, GTK_WIN_POS_CENTER, TRUE );

	sisca_vbox = gtk_vbox_new(FALSE, 0);
	gtk_widget_show(sisca_vbox);
	gtk_container_add(GTK_CONTAINER (sisca_window), sisca_vbox);

	sisca_table = add_a_table(3, 3, 5, sisca_vbox);

	add_to_table( _("Width     "), sisca_table, 0, 1, 0 );
	add_to_table( _("Height    "), sisca_table, 0, 2, 0 );

	add_to_table( _("Original      "), sisca_table, 1, 0, 0);
	sisca_spins[0] = spin_to_table(sisca_table, 1, 1, 5, mem_width, mem_width, mem_width);
	sisca_spins[1] = spin_to_table(sisca_table, 1, 2, 5, mem_height, mem_height, mem_height);
	GTK_WIDGET_UNSET_FLAGS (sisca_spins[0], GTK_CAN_FOCUS);
	GTK_WIDGET_UNSET_FLAGS (sisca_spins[1], GTK_CAN_FOCUS);

	add_to_table( _("New"), sisca_table, 2, 0, 0 );
	sisca_spins[0] = spin_to_table(sisca_table, 2, 1, 5, mem_width, 1, MAX_WIDTH);
	sisca_spins[1] = spin_to_table(sisca_table, 2, 2, 5, mem_height, 1, MAX_HEIGHT);

	spin_connect(sisca_spins[0], GTK_SIGNAL_FUNC(sisca_moved), (gpointer)0);
	spin_connect(sisca_spins[1], GTK_SIGNAL_FUNC(sisca_moved), (gpointer)1);

	if ( !sisca_scale )
	{
		add_to_table( _("Offset"), sisca_table, 3, 0, 0 );
		sisca_spins[2] = spin_to_table(sisca_table, 3, 1, 5, 0,
			-MAX_WIDTH, MAX_WIDTH);
		sisca_spins[3] = spin_to_table(sisca_table, 3, 2, 5, 0,
			-MAX_HEIGHT, MAX_HEIGHT);

		button_centre = gtk_button_new_with_label(_("Centre"));

		gtk_widget_show(button_centre);
		gtk_table_attach (GTK_TABLE (sisca_table), button_centre, 0, 1, 4, 5,
			(GtkAttachOptions) (GTK_FILL),
			(GtkAttachOptions) (0), 5, 5);
		gtk_signal_connect(GTK_OBJECT(button_centre), "clicked",
			GTK_SIGNAL_FUNC(click_sisca_centre), NULL);
	}
	add_hseparator( sisca_vbox, -2, 10 );

	sisca_toggles[0] = sig_toggle(_("Fix Aspect Ratio"), TRUE, (gpointer)0,
		GTK_SIGNAL_FUNC(sisca_moved));

	sisca_hbox = sisca_gc = NULL;
	wvbox = page0 = sisca_vbox;

	/* Resize */
	if (!sisca_scale)
		sisca_hbox = wj_radio_pack(resize_modes, -1, 0, resize_mode, &resize_mode, NULL);

	/* RGB rescale */
	else if (mem_img_bpp == 3)
	{
		sisca_hbox = pack(sisca_vbox, gtk_hbox_new(FALSE, 0));
		wvbox = pack(sisca_hbox, gtk_vbox_new(FALSE, 0));

		sisca_gc = sig_toggle(_("Gamma corrected"),
			inifile_get_gboolean("defaultGamma", FALSE), NULL, NULL);
		gtk_box_pack_end(GTK_BOX(wvbox), sisca_gc, FALSE, FALSE, 0);

		notebook = pack(sisca_vbox, buttoned_book(&page0, &page1,
			&button, _("Settings")));
		wvbox2 = gtk_vbox_new(FALSE, 0);
		gtk_box_pack_end(GTK_BOX(sisca_hbox), wvbox2, FALSE, FALSE, 0);
		gtk_box_pack_start(GTK_BOX(wvbox2), button, FALSE, FALSE, 0);

		add_hseparator(page1, -2, 10);
		button = pack(page1, sig_toggle(_("Sharper image reduction"),
			sharper_reduce, &sharper_reduce, NULL));

		sisca_hbox = wj_radio_pack(scale_fnames, -1, 0, scale_mode, &scale_mode, NULL);
	}

	gtk_box_pack_start(GTK_BOX(wvbox), sisca_toggles[0], FALSE, FALSE, 0);

	if (sisca_hbox)
	{
		add_hseparator(page0, -2, 10);
		gtk_box_pack_start(GTK_BOX(page0), sisca_hbox, TRUE, TRUE, 0);
	}
	add_hseparator(page0, -2, 10);

	pack(sisca_vbox, OK_box(5, sisca_window, _("OK"), GTK_SIGNAL_FUNC(click_sisca_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));

	gtk_window_set_transient_for(GTK_WINDOW(sisca_window), GTK_WINDOW(main_window));
	gtk_widget_show_all(sisca_window);
}

void pressed_scale( GtkMenuItem *menu_item, gpointer user_data )
{
	sisca_scale = TRUE;
	sisca_init(_("Scale Canvas"));
}

void pressed_size( GtkMenuItem *menu_item, gpointer user_data )
{
	sisca_scale = FALSE;
	sisca_init(_("Resize Canvas"));
}


///	PALETTE EDITOR WINDOW

typedef struct {
	guint16 r, g, b, a;
} RGBA16;

static GtkWidget *allcol_window, *allcol_list;
static RGBA16 *ctable;
static int allcol_idx;
static colour_hook allcol_hook;


static void allcol_ok(colour_hook chook)
{
	chook(2);
	gtk_widget_destroy(allcol_window);
	free(ctable);
}

static void allcol_preview(colour_hook chook)
{
	chook(1);
}

static gboolean allcol_cancel(colour_hook chook)
{
	chook(0);
	gtk_widget_destroy(allcol_window);
	free(ctable);
	return (FALSE);
}

static void color_refresh()
{
	gtk_list_select_item(GTK_LIST(allcol_list), allcol_idx);

	/* Stupid GTK+ does nothing for gtk_widget_queue_draw(allcol_list) */
	gtk_container_foreach(GTK_CONTAINER(allcol_list),
		(GtkCallback)gtk_widget_queue_draw, NULL);
}

static gboolean color_expose( GtkWidget *widget, GdkEventExpose *event, gpointer user_data )
{
	RGBA16 *cc = user_data;
	unsigned char r = cc->r / 257, g = cc->g / 257, b = cc->b / 257, *rgb;
	int x = event->area.x, y = event->area.y, w = event->area.width, h = event->area.height;
	int i, j = w * h * 3;

	rgb = malloc(j);
	if (rgb)
	{
		for (i = 0; i < j; i += 3)
		{
			rgb[i] = r;
			rgb[i + 1] = g;
			rgb[i + 2] = b;
		}
		gdk_draw_rgb_image( widget->window, widget->style->black_gc, x, y, w, h, GDK_RGB_DITHER_NONE, rgb, w*3 );
		free(rgb);
	}

	return FALSE;
}


static void color_set( GtkColorSelection *selection, gpointer user_data )
{
	gdouble color[4];
	GtkWidget *widget;
	RGBA16 *cc;
	GdkColor c;

	gtk_color_selection_get_color( selection, color );
	widget = GTK_WIDGET(gtk_object_get_user_data(GTK_OBJECT(selection)));

	cc = gtk_object_get_user_data(GTK_OBJECT(widget));
	cc->r = rint(color[0] * 65535.0);
	cc->g = rint(color[1] * 65535.0);
	cc->b = rint(color[2] * 65535.0);
	cc->a = rint(color[3] * 65535.0);
	c.pixel = 0;
	c.red   = cc->r;
	c.green = cc->g;
	c.blue  = cc->b;
	gdk_colormap_alloc_color( gdk_colormap_get_system(), &c, FALSE, TRUE );
	gtk_widget_queue_draw(widget);
	allcol_hook(4);
}

static void color_select( GtkList *list, GtkWidget *widget, gpointer user_data )
{
	GtkColorSelection *cs = GTK_COLOR_SELECTION(user_data);
	RGBA16 *cc = gtk_object_get_user_data(GTK_OBJECT(widget));
	gdouble color[4];
#if GTK_MAJOR_VERSION == 2
	GdkColor c = {0, cc->r, cc->g, cc->b};
#endif

	gtk_object_set_user_data( GTK_OBJECT(cs), widget );
	color[0] = ((gdouble)(cc->r)) / 65535.0;
	color[1] = ((gdouble)(cc->g)) / 65535.0;
	color[2] = ((gdouble)(cc->b)) / 65535.0;
	color[3] = ((gdouble)(cc->a)) / 65535.0;

	gtk_signal_disconnect_by_func(GTK_OBJECT(cs), GTK_SIGNAL_FUNC(color_set), NULL);

	gtk_color_selection_set_color( cs, color );

#if GTK_MAJOR_VERSION == 1
	gtk_color_selection_set_color( cs, color);
#endif
#if GTK_MAJOR_VERSION == 2
	gtk_color_selection_set_previous_color(cs, &c);
	gtk_color_selection_set_previous_alpha(cs, cc->a);
#endif
	allcol_idx = cc - ctable;

	gtk_signal_connect( GTK_OBJECT(cs), "color_changed", GTK_SIGNAL_FUNC(color_set), NULL );
	allcol_hook(3);
}

static void colour_window(GtkWidget *win, GtkWidget *extbox, int cnt,
	char **cnames, int alpha, colour_hook chook, GtkSignalFunc lhook)
{
	GtkWidget *vbox, *hbox, *hbut, *button_ok, *button_preview, *button_cancel;
	GtkWidget *col_list, *l_item, *hbox2, *label, *drw, *swindow, *viewport;
	GtkWidget *cs;
	char txt[64], *tmp = txt;
	int i;

	GtkAccelGroup* ag = gtk_accel_group_new();

	cs = gtk_color_selection_new();

#if GTK_MAJOR_VERSION == 1
	gtk_color_selection_set_opacity(GTK_COLOR_SELECTION(cs), alpha);
#endif
#if GTK_MAJOR_VERSION == 2
	gtk_color_selection_set_has_opacity_control(GTK_COLOR_SELECTION(cs), alpha);

	gtk_color_selection_set_has_palette (GTK_COLOR_SELECTION(cs), TRUE);
#endif
	gtk_signal_connect( GTK_OBJECT(cs), "color_changed", GTK_SIGNAL_FUNC(color_set), NULL );

	allcol_window = win;
	allcol_hook = chook;

	gtk_signal_connect_object(GTK_OBJECT(allcol_window), "delete_event",
		GTK_SIGNAL_FUNC(allcol_cancel), (gpointer)chook);

	vbox = gtk_vbox_new( FALSE, 5 );
	gtk_widget_show( vbox );
	gtk_container_set_border_width( GTK_CONTAINER(vbox), 5 );
	gtk_container_add( GTK_CONTAINER(allcol_window), vbox );

	hbox = xpack(vbox, gtk_hbox_new(FALSE, 10));
	gtk_widget_show( hbox );

	swindow = pack(hbox, gtk_scrolled_window_new(NULL, NULL));
	gtk_widget_show(swindow);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (swindow),
		GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);

	viewport = gtk_viewport_new (NULL, NULL);
	gtk_widget_show(viewport);
	gtk_container_add (GTK_CONTAINER (swindow), viewport);

	allcol_idx = 0;
	allcol_list = col_list = gtk_list_new();
	gtk_signal_connect(GTK_OBJECT(col_list), "select_child",
		GTK_SIGNAL_FUNC(color_select), cs);
	gtk_list_set_selection_mode( GTK_LIST(col_list), GTK_SELECTION_BROWSE );
	gtk_container_add ( GTK_CONTAINER(viewport), col_list );
	gtk_widget_show( col_list );

	for (i = 0; i < cnt; i++)
	{
		l_item = gtk_list_item_new();
		gtk_object_set_user_data( GTK_OBJECT(l_item), (gpointer)(&ctable[i]));
		if (lhook) gtk_signal_connect(GTK_OBJECT(l_item),
			"button_press_event", lhook, (gpointer)i);
		gtk_container_add( GTK_CONTAINER(col_list), l_item );
		gtk_widget_show( l_item );

		hbox2 = gtk_hbox_new( FALSE, 3 );
		gtk_widget_show( hbox2 );
		gtk_container_set_border_width( GTK_CONTAINER(hbox2), 3 );
		gtk_container_add( GTK_CONTAINER(l_item), hbox2 );

		drw = pack(hbox2, gtk_drawing_area_new());
		gtk_drawing_area_size( GTK_DRAWING_AREA(drw), 20, 20 );
		gtk_signal_connect(GTK_OBJECT(drw), "expose_event",
			GTK_SIGNAL_FUNC(color_expose), (gpointer)(&ctable[i]));
		gtk_widget_show(drw);

		if (cnames) tmp = cnames[i];
		else sprintf(txt, "%i", i);
		label = xpack(hbox2, gtk_label_new(tmp));
		gtk_widget_show( label );
		gtk_misc_set_alignment( GTK_MISC(label), 0.0, 1.0 );
	}

	gtk_box_pack_start( GTK_BOX(hbox), cs, TRUE, TRUE, 0 );

	if (extbox) gtk_box_pack_start(GTK_BOX(vbox), extbox, FALSE, FALSE, 0);

	hbut = pack(vbox, gtk_hbox_new(FALSE, 3));
	gtk_widget_show(hbut);

	button_ok = gtk_button_new_with_label(_("OK"));
	gtk_widget_show( button_ok );
	gtk_widget_add_accelerator (button_ok, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button_ok, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_signal_connect_object(GTK_OBJECT(button_ok), "clicked",
		GTK_SIGNAL_FUNC(allcol_ok), (gpointer)chook);
	gtk_box_pack_end( GTK_BOX(hbut), button_ok, FALSE, FALSE, 5 );
	gtk_widget_set_usize(button_ok, 80, -2);

	button_preview = gtk_button_new_with_label(_("Preview"));
	gtk_widget_show( button_preview );
	gtk_signal_connect_object(GTK_OBJECT(button_preview), "clicked",
		GTK_SIGNAL_FUNC(allcol_preview), (gpointer)chook);
	gtk_box_pack_end( GTK_BOX(hbut), button_preview, FALSE, FALSE, 5 );
	gtk_widget_set_usize(button_preview, 80, -2);

	button_cancel = gtk_button_new_with_label(_("Cancel"));
	gtk_widget_show( button_cancel );
	gtk_widget_add_accelerator (button_cancel, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);
	gtk_signal_connect_object(GTK_OBJECT(button_cancel), "clicked",
		GTK_SIGNAL_FUNC(allcol_cancel), (gpointer)chook);
	gtk_box_pack_end( GTK_BOX(hbut), button_cancel, FALSE, FALSE, 5 );
	gtk_widget_set_usize(button_cancel, 80, -2);

	gtk_widget_show( cs );
	gtk_window_set_transient_for( GTK_WINDOW(allcol_window), GTK_WINDOW(main_window) );
	gtk_widget_show( allcol_window );
	gtk_window_add_accel_group( GTK_WINDOW(allcol_window), ag );

#if GTK_MAJOR_VERSION == 1
	while (gtk_events_pending()) gtk_main_iteration();
	gtk_list_select_item( GTK_LIST(col_list), 0 );
		// grubby hack needed to start with proper opacity in GTK+1
	gtk_widget_queue_resize(allcol_window); /* Reset shortened sliders */
#endif
}

static void do_allcol()
{
	int i;

	for (i = 0; i < mem_cols; i++)
	{
		mem_pal[i].red = (ctable[i].r + 128) / 257;
		mem_pal[i].green = (ctable[i].g + 128) / 257;
		mem_pal[i].blue = (ctable[i].b + 128) / 257;
	}

	init_pal();
	update_all_views();
	gtk_widget_queue_draw( drawing_col_prev );
}

static void do_allover()
{
	int i;

	for (i = 0; i < NUM_CHANNELS; i++)
	{
		channel_rgb[i][0] = (ctable[i].r + 128) / 257;
		channel_rgb[i][1] = (ctable[i].g + 128) / 257;
		channel_rgb[i][2] = (ctable[i].b + 128) / 257;
		channel_opacity[i] = (ctable[i].a + 128) / 257;
	}
	update_all_views();
}

static void do_AB(int idx)
{
	png_color *A0, *B0;
	A0 = mem_img_bpp == 1 ? &mem_pal[mem_col_A] : &mem_col_A24;
	B0 = mem_img_bpp == 1 ? &mem_pal[mem_col_B] : &mem_col_B24;

	A0->red = (ctable[idx].r + 128) / 257;
	A0->green = (ctable[idx].g + 128) / 257;
	A0->blue = (ctable[idx].b + 128) / 257;
	idx++;
	B0->red = (ctable[idx].r + 128) / 257;
	B0->green = (ctable[idx].g + 128) / 257;
	B0->blue = (ctable[idx].b + 128) / 257;
	pal_refresher();
}

static void set_csel()
{
	csel_data->center = RGB_2_INT((ctable[0].r + 128) / 257,
		(ctable[0].g + 128) / 257, (ctable[0].b + 128) / 257);
	csel_data->center_a = (ctable[0].a + 128) / 257;
	csel_data->limit = RGB_2_INT((ctable[1].r + 128) / 257,
		(ctable[1].g + 128) / 257, (ctable[1].b + 128) / 257);
	csel_data->limit_a = (ctable[1].a + 128) / 257;
	csel_preview = RGB_2_INT((ctable[2].r + 128) / 257,
		(ctable[2].g + 128) / 257, (ctable[2].b + 128) / 257);
/* !!! Alpha is disabled for now !!! */
//	csel_preview_a = (ctable[2].a + 128) / 257;
}

GtkWidget *range_spins[2];

static void select_colour(int what)
{
	switch (what)
	{
	case 0: /* Cancel */
		mem_pal_copy(mem_pal, brcosa_pal);
		pal_refresher();
		break;
	case 1: /* Preview */
		do_allcol();
		break;
	case 2: /* OK */
		mem_pal_copy(mem_pal, brcosa_pal);
		pal_refresher();
		spot_undo(UNDO_PAL);
		do_allcol();
		break;
	}
}

static gint click_colour(GtkWidget *widget, GdkEventButton *event, gpointer user_data)
{
	/* Middle click sets "from" */
	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 2))
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(range_spins[0]),
			(int)user_data);

	/* Right click sets "to" */
	if ((event->type == GDK_BUTTON_PRESS) && (event->button == 3))
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(range_spins[1]),
			(int)user_data);

	/* Let click processing continue */
	return (FALSE);
}

static void set_range_spin(GtkButton *button, GtkWidget *spin)
{
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), allcol_idx);
}

static void make_cscale(GtkButton *button, gpointer user_data)
{
	int i, start, stop, start0, mode;
	unsigned char rgb[6];
	double d;

	mode = wj_option_menu_get_history(GTK_WIDGET(user_data));
	start = start0 = read_spin(range_spins[0]);
	stop = read_spin(range_spins[1]);

	if (mode <= 1) /* RGB/HSV */
	{
		if (start > stop) { i = start; start = stop; stop = i; }
		if (stop < start + 2) return;
	}
	else if (stop == start) return; /* Gradient */

	rgb[0] = (ctable[start].r + 128) / 257;
	rgb[1] = (ctable[start].g + 128) / 257;
	rgb[2] = (ctable[start].b + 128) / 257;
	rgb[3] = (ctable[stop].r + 128) / 257;
	rgb[4] = (ctable[stop].g + 128) / 257;
	rgb[5] = (ctable[stop].b + 128) / 257;
	d = stop - start;

	if (mode == 0) /* RGB */
	{
		double r0, g0, b0, dr, dg, db;

		dr = ((int)rgb[3] - rgb[0]) / d;
		r0 = rgb[0] - dr * start;
		dg = ((int)rgb[4] - rgb[1]) / d;
		g0 = rgb[1] - dg * start;
		db = ((int)rgb[5] - rgb[2]) / d;
		b0 = rgb[2] - db * start;

		for (i = start + 1; i < stop; i++)
		{
			ctable[i].r = rint(r0 + dr * i) * 257;
			ctable[i].g = rint(g0 + dg * i) * 257;
			ctable[i].b = rint(b0 + db * i) * 257;
		}
	}
	else if (mode == 1) /* HSV */
	{
		int t;
		double h0, dh, s0, ds, v0, dv, hsv[6], hh, ss, vv;

		rgb2hsv(rgb + 0, hsv + 0);
		rgb2hsv(rgb + 3, hsv + 3);
		/* Grey has no hue */
		if (hsv[1] == 0.0) hsv[0] = hsv[3];
		if (hsv[4] == 0.0) hsv[3] = hsv[0];

		/* Always go from 1st to 2nd hue in ascending order */
		t = start == start0 ? 0 : 3;
		if (hsv[t] > hsv[t ^ 3]) hsv[t] -= 6.0;

		dh = (hsv[3] - hsv[0]) / d;
		h0 = hsv[0] - dh * start;
		ds = (hsv[4] - hsv[1]) / d;
		s0 = hsv[1] - ds * start;
		dv = (hsv[5] - hsv[2]) / d;
		v0 = hsv[2] - dv * start;

		for (i = start + 1; i < stop; i++)
		{
			vv = v0 + dv * i;
			ss = vv - vv * (s0 + ds * i);
			hh = h0 + dh * i;
			if (hh < 0.0) hh += 6.0;
			t = hh;
			hh = (hh - t) * (vv - ss);
			if (t & 1) { vv -= hh; hh += vv; }
			else hh += ss;
			t >>= 1;
			rgb[t] = rint(vv);
			rgb[(t + 1) % 3] = rint(hh);
			rgb[(t + 2) % 3] = rint(ss);
			ctable[i].r = rgb[0] * 257;
			ctable[i].g = rgb[1] * 257;
			ctable[i].b = rgb[2] * 257;
		}
	}
	else /* Gradient */
	{
		int j, op, c[NUM_CHANNELS + 3];

		j = start < stop ? 1 : -1;
		for (i = start; i != stop + j; i += j)
		{
			op = grad_value(c, 0, (i - start) / d);
			/* Zero opacity - empty slot */
			if (!op) ctable[i].r = ctable[i].g = ctable[i].b = 0;
			else
			{
				ctable[i].r = ((c[0] + 128) >> 8) * 257;
				ctable[i].g = ((c[1] + 128) >> 8) * 257;
				ctable[i].b = ((c[2] + 128) >> 8) * 257;
			}
		}
	}
	color_refresh();
}

static void select_overlay(int what)
{
	char txt[64];
	int i, j;

	switch (what)
	{
	case 0: /* Cancel */
		for (i = 0; i < NUM_CHANNELS; i++)	// Restore original values
		{
			ctable[i] = ctable[i + NUM_CHANNELS];
		}
	case 1: /* Preview */
		do_allover();
		break;
	case 2: /* OK */
		do_allover();
		for (i = 0; i < NUM_CHANNELS; i++)	// Save all settings to ini file
		{
			for (j = 0; j < 4; j++)
			{
				sprintf(txt, "overlay%i%i", i, j);
				inifile_set_gint32(txt, j < 3 ? channel_rgb[i][j] :
					channel_opacity[i]);
			}
		}
		break;
	}
}

static GtkWidget *AB_spins[NUM_CHANNELS];
static int temp_AB[NUM_CHANNELS][2];

static void select_AB(int what)
{
	int i;

	switch (what)
	{
	case 0: /* Cancel */
		do_AB(2);
		break;
	case 2: /* OK */
		do_AB(2);
		spot_undo(UNDO_PAL);
		for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
		{
			channel_col_A[i] = temp_AB[i][0];
			channel_col_B[i] = temp_AB[i][1];
		}
	case 1: /* Preview */
		do_AB(0);
		break;
	case 3: /* Select */
		for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
		{
			mt_spinslide_set_value(AB_spins[i], temp_AB[i][allcol_idx]);
		}
		break;
	}
}

static void AB_spin_moved(GtkAdjustment *adj, gpointer user_data)
{
	temp_AB[(int)user_data][allcol_idx] = ADJ2INT(adj);
}

static void posterize_AB(GtkButton *button, gpointer user_data)
{
	static int posm[8] = {0, 0xFF00, 0x5500, 0x2480, 0x1100,
				 0x0840, 0x0410, 0x0204};
	int i, pm, ps;

	ps = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(user_data));
	inifile_set_gint32("posterizeInt", ps);
	if (ps >= 8) return;
	pm = posm[ps]; ps = 8 - ps;

	i = (ctable[0].r + 128) / 257;
	ctable[0].r = (((i >> ps) * pm) >> 8) * 257;
	i = (ctable[0].g + 128) / 257;
	ctable[0].g = (((i >> ps) * pm) >> 8) * 257;
	i = (ctable[0].b + 128) / 257;
	ctable[0].b = (((i >> ps) * pm) >> 8) * 257;
	i = (ctable[1].r + 128) / 257;
	ctable[1].r = (((i >> ps) * pm) >> 8) * 257;
	i = (ctable[1].g + 128) / 257;
	ctable[1].g = (((i >> ps) * pm) >> 8) * 257;
	i = (ctable[1].b + 128) / 257;
	ctable[1].b = (((i >> ps) * pm) >> 8) * 257;
	color_refresh();
}

static GtkWidget *csel_spin, *csel_toggle;
static unsigned char csel_save[CSEL_SVSIZE];
static int csel_preview0, csel_preview_a0;

static void select_csel(int what)
{
	int old_over = csel_overlay;
	switch (what)
	{
	case 0: /* Cancel */
		csel_overlay = 0;
		memcpy(csel_data, csel_save, CSEL_SVSIZE);
		csel_preview = csel_preview0;
		csel_preview_a = csel_preview_a0;
		csel_reset(csel_data);
		break;
	case 1: /* Preview */
	case 2: /* OK */
		csel_overlay = 0;
		csel_data->range = read_float_spin(csel_spin);
		csel_data->invert = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(csel_toggle));
		set_csel();
		csel_reset(csel_data);
		if (what != 1) break;
		old_over = 0;
		csel_overlay = 1;
		break;
	case 4: /* Set */
		csel_overlay = 0;
		if (allcol_idx != 1) break; /* Only for limit */
		set_csel();
		csel_data->range = csel_eval(csel_data->mode, csel_data->center,
			csel_data->limit);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(csel_spin), csel_data->range);
		break;
	}
	if ((old_over != csel_overlay) && drawing_canvas)
		gtk_widget_queue_draw(drawing_canvas);
}

static void csel_mode_changed(GtkToggleButton *widget, gpointer user_data)
{
	int old_over;

	if (!gtk_toggle_button_get_active(widget)) return;
	if (csel_data->mode == (int)user_data) return;
	old_over = csel_overlay;
	csel_overlay = 0;
	csel_data->mode = (int)user_data;
	set_csel();
	csel_data->range = csel_eval(csel_data->mode, csel_data->center,
		csel_data->limit);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(csel_spin), csel_data->range);
	if (old_over && drawing_canvas) gtk_widget_queue_draw(drawing_canvas);
}

void colour_selector( int cs_type )		// Bring up GTK+ colour wheel
{
	GtkWidget *win, *extbox, *button, *spin, *opt;
	int i, j;

	if (cs_type == COLSEL_EDIT_ALL)
	{
		char *fromto[] = { _("From"), _("To") };
		char *scales[] = { _("RGB"), _("HSV"), _("Gradient") };

		mem_pal_copy( brcosa_pal, mem_pal );	// Remember old settings
		ctable = malloc(mem_cols * sizeof(RGBA16));
		if (!ctable) return;
		for (i = 0; i < mem_cols; i++)
		{
			ctable[i].r = mem_pal[i].red * 257;
			ctable[i].g = mem_pal[i].green * 257;
			ctable[i].b = mem_pal[i].blue * 257;
			ctable[i].a = 65535;
		}

		/* Prepare range controls */
		extbox = gtk_hbox_new(FALSE, 0);
		pack(extbox, gtk_label_new(_("Scale")));
		for (i = 0; i < 2; i++)
		{
			button = add_a_button(fromto[i], 4, extbox, TRUE);
			spin = range_spins[i] = pack(extbox, add_a_spin(0, 0, 255));
			gtk_signal_connect(GTK_OBJECT(button), "clicked",
				GTK_SIGNAL_FUNC(set_range_spin), spin);
		}
		opt = xpack(extbox, wj_option_menu(scales, 3, 0, NULL, NULL));
		gtk_container_set_border_width(GTK_CONTAINER(opt), 4);
		button = add_a_button(_("Create"), 4, extbox, TRUE);
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(make_cscale), (gpointer)opt);
		/* Protect sliders from option menu in GTK1 */
#if GTK_MAJOR_VERSION == 1
		extbox = widget_align_minsize(extbox, 0, 0);
#endif
		gtk_widget_show_all(extbox);

		win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Palette Editor"),
			GTK_WIN_POS_MOUSE, TRUE);
		colour_window(win, extbox, mem_cols, NULL, FALSE, select_colour,
			GTK_SIGNAL_FUNC(click_colour));

		/* Protect sliders from option menu in GTK1 */
#if GTK_MAJOR_VERSION == 1
		gtk_container_set_resize_mode(GTK_CONTAINER(extbox), GTK_RESIZE_QUEUE);
#endif
	}

	if (cs_type == COLSEL_OVERLAYS)
	{
		ctable = malloc(NUM_CHANNELS * 2 * sizeof(RGBA16));
		if (!ctable) return;
		for (i = 0; i < NUM_CHANNELS; i++)
		{
			ctable[i].r = channel_rgb[i][0] * 257;
			ctable[i].g = channel_rgb[i][1] * 257;
			ctable[i].b = channel_rgb[i][2] * 257;
			ctable[i].a = channel_opacity[i] * 257;
			/* Save old value in the same array */
			ctable[i + NUM_CHANNELS] = ctable[i];
		}

		win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Configure Overlays"),
			GTK_WIN_POS_CENTER, TRUE);
		colour_window(win, NULL, NUM_CHANNELS, allchannames,
			TRUE, select_overlay, NULL);
	}

	if (cs_type == COLSEL_EDIT_AB)
	{
		static char *AB_txt[] = { "A", "B" };
		png_color *A0, *B0;
		A0 = mem_img_bpp == 1 ? &mem_pal[mem_col_A] : &mem_col_A24;
		B0 = mem_img_bpp == 1 ? &mem_pal[mem_col_B] : &mem_col_B24;

		ctable = malloc(4 * sizeof(RGBA16));
		if (!ctable) return;
		ctable[0].r = A0->red * 257;
		ctable[0].g = A0->green * 257;
		ctable[0].b = A0->blue * 257;
		ctable[0].a = 65535;
		ctable[1].r = B0->red * 257;
		ctable[1].g = B0->green * 257;
		ctable[1].b = B0->blue * 257;
		ctable[1].a = 65535;
		/* Save previous values right here */
		ctable[2] = ctable[0];
		ctable[3] = ctable[1];

		/* Prepare posterize controls */
		
		extbox = gtk_table_new(3, 3, FALSE);
		win = gtk_hbox_new(FALSE, 0);
		gtk_table_attach(GTK_TABLE(extbox), win, 0, 3, 0, 1,
			GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
		button = add_a_button(_("Posterize"), 4, win, TRUE);
		spin = pack(win, add_a_spin(inifile_get_gint32("posterizeInt", 1), 1, 8));
		gtk_signal_connect(GTK_OBJECT(button), "clicked",
			GTK_SIGNAL_FUNC(posterize_AB), spin);

		/* Prepare channel A/B controls */

		for (i = CHN_ALPHA; i < NUM_CHANNELS; i++)
		{
			j = i - CHN_ALPHA;
			spin = gtk_label_new(allchannames[i]);
			gtk_table_attach(GTK_TABLE(extbox), spin, j, j + 1,
				1, 2, GTK_EXPAND | GTK_FILL, 0, 0, 0);
			gtk_misc_set_alignment(GTK_MISC(spin), 1.0 / 3.0, 0.5);
			spin = AB_spins[i] = mt_spinslide_new(-1, -1);
			gtk_table_attach(GTK_TABLE(extbox), spin, j, j + 1,
				2, 3, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
			mt_spinslide_set_range(spin, 0, 255);
			mt_spinslide_set_value(spin, channel_col_A[i]);
			mt_spinslide_connect(spin,
				GTK_SIGNAL_FUNC(AB_spin_moved), (gpointer)i);
			temp_AB[i][0] = channel_col_A[i];
			temp_AB[i][1] = channel_col_B[i];
		}
		gtk_widget_show_all(extbox);

		win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Colour Editor"),
			GTK_WIN_POS_MOUSE, TRUE);
		colour_window(win, extbox, 2, AB_txt, FALSE, select_AB, NULL);
	}
	if (cs_type == COLSEL_EDIT_CSEL)
	{
		char *csel_txt[] = { _("Centre"), _("Limit"), _("Preview") };
		char *csel_modes[] = { _("Sphere"), _("Angle"), _("Cube"), NULL };

		if (!csel_data)
		{
			csel_init();
			if (!csel_data) return;
		}
		/* Save previous values */
		memcpy(csel_save, csel_data, CSEL_SVSIZE);
		csel_preview0 = csel_preview;
		csel_preview_a0 = csel_preview_a;

		ctable = malloc(3 * sizeof(RGBA16));
		if (!ctable) return;
		ctable[0].r = INT_2_R(csel_data->center) * 257;
		ctable[0].g = INT_2_G(csel_data->center) * 257;
		ctable[0].b = INT_2_B(csel_data->center) * 257;
		ctable[0].a = csel_data->center_a * 257;
		ctable[1].r = INT_2_R(csel_data->limit) * 257;
		ctable[1].g = INT_2_G(csel_data->limit) * 257;
		ctable[1].b = INT_2_B(csel_data->limit) * 257;
		ctable[1].a = csel_data->limit_a * 257;
		ctable[2].r = INT_2_R(csel_preview) * 257;
		ctable[2].g = INT_2_G(csel_preview) * 257;
		ctable[2].b = INT_2_B(csel_preview) * 257;
		ctable[2].a = csel_preview_a * 257;

		/* Prepare extra controls */
		extbox = gtk_hbox_new(FALSE, 0);
		pack(extbox, gtk_label_new(_("Range")));
		csel_spin = pack(extbox, add_float_spin(csel_data->range, 0, 765));
		csel_toggle = add_a_toggle(_("Inverse"), extbox, csel_data->invert);
		pack(extbox, wj_radio_pack(csel_modes, -1, 1, csel_data->mode,
			NULL, GTK_SIGNAL_FUNC(csel_mode_changed)));
		gtk_widget_show_all(extbox);

		win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Colour-Selective Mode"),
			GTK_WIN_POS_CENTER, TRUE);
/* !!! Alpha ranges not implemented yet !!! */
//		colour_window(win, extbox, 3, csel_txt, TRUE, select_csel, NULL);
		colour_window(win, extbox, 3, csel_txt, FALSE, select_csel, NULL);
	}
}

void pressed_allcol( GtkMenuItem *menu_item, gpointer user_data )
{
	colour_selector( COLSEL_EDIT_ALL );
}

void choose_colours()
{
	colour_selector(COLSEL_EDIT_AB);
}

///	QUANTIZE WINDOW

#define DITH_NONE	0
#define DITH_FS		1
#define DITH_STUCKI	2
#define DITH_ORDERED	3
#define DITH_OLDFS	4
#define DITH_OLDDITHER	5
#define DITH_OLDSCATTER	6
#define DITH_MAX	7

static GtkWidget *quantize_window, *quantize_spin, *quantize_dither;
static GtkWidget *dither_serpent, *dither_spin, *dither_err;
static int quantize_cols, quantize_mode, dither_mode;

/* Dither settings - persistent */
static int dither_cspace = 1, dither_dist = 2, dither_limit;
static int dither_scan = TRUE, dither_8b, dither_sel;
static double dither_fract[2] = {1.0, 0.0};

static void click_quantize_radio(GtkWidget *widget, gpointer data)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(widget))) return;
	quantize_mode = (int)data;

	/* No dither for exact conversion */
	gtk_widget_set_sensitive(quantize_dither, quantize_mode != 0);
}

static void click_quantize_ok(GtkWidget *widget, gpointer data)
{
	int i, dither, new_cols, err = 0;
	unsigned char *old_image = mem_img[CHN_IMAGE], newpal[3][256];
	double efrac;

	/* Dithering filters */
	/* Floyd-Steinberg dither */
	static short fs_dither[16] =
		{ 16,  0, 0, 0, 7, 0,  0, 3, 5, 1, 0,  0, 0, 0, 0, 0 };
	/* Stucki dither */
	static short s_dither[16] =
		{ 42,  0, 0, 0, 8, 4,  2, 4, 8, 4, 2,  1, 2, 4, 2, 1 };

	dither = quantize_mode ? dither_mode : DITH_NONE;
	new_cols = read_spin(quantize_spin);
	new_cols = new_cols < 1 ? 1 : new_cols > 256 ? 256 : new_cols;
	dither_scan = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dither_serpent));
	dither_8b = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(dither_err));
	efrac = 0.01 * read_spin(dither_spin);
	dither_fract[dither_sel ? 1 : 0] = efrac;

	gtk_widget_destroy(quantize_window);

	/* Paranoia */
	if ((quantize_mode >= 6) || (dither >= DITH_MAX)) return;

	i = undo_next_core(UC_NOCOPY, mem_width, mem_height, 1, CMASK_IMAGE);
	if (i)
	{
		memory_errors(2);
		return;
	}

	switch (quantize_mode)
	{
	case 0:	/* Use image colours */
		new_cols = quantize_cols;
		err = mem_convert_indexed();
		dither = DITH_MAX;
		break;
	default:
	case 1: /* Use current palette */
		for (i = 0; i < new_cols; i++)
		{
			newpal[0][i] = mem_pal[i].red;
			newpal[1][i] = mem_pal[i].green;
			newpal[2][i] = mem_pal[i].blue;
		}
		break;
	case 2: /* DL1 quantizer */
		err = dl1quant(old_image, mem_width, mem_height, new_cols, newpal);
		break;
	case 3: /* DL3 quantizer */
		err = dl3quant(old_image, mem_width, mem_height, new_cols, newpal);
		break;
	case 4: /* Wu quantizer */
		err = wu_quant(old_image, mem_width, mem_height, new_cols, newpal);
		break;
	case 5: /* Max-Min quantizer */
		err = maxminquan(old_image, mem_width, mem_height, new_cols, newpal);
		break;
	}

	if (quantize_mode > 1)
	{
		for (i = 0; i < new_cols; i++)
		{
			mem_pal[i].red = newpal[0][i];
			mem_pal[i].green = newpal[1][i];
			mem_pal[i].blue = newpal[2][i];
		}
	}

	if (err) dither = DITH_MAX;
	switch (dither)
	{
	case DITH_NONE:
	case DITH_FS:
	case DITH_STUCKI:
		mem_dither(old_image, new_cols, dither == DITH_NONE ? NULL :
			dither == DITH_FS ? fs_dither : s_dither,
			dither_cspace, dither_dist, dither_limit, dither_sel,
			dither_scan, dither_8b, efrac);
		break;

// !!! No code yet - temporarily disabled !!!
//	case DITH_ORDERED:

	case DITH_OLDFS:
		err = dl3floste(old_image, mem_img[CHN_IMAGE],
			mem_width, mem_height, new_cols, 1, newpal);
		break;
	case DITH_OLDDITHER:
		err = mem_quantize(old_image, new_cols, 2);
		break;
	case DITH_OLDSCATTER:
		err = mem_quantize(old_image, new_cols, 3);
		break;
	default:
	case DITH_MAX: break;
	}

	// If the user is pasting or smudging, lose it!
	if ((tool_type == TOOL_SELECT) && (marq_status >= MARQUEE_PASTE))
		pressed_select_none(NULL, NULL);
	if (tool_type == TOOL_SMUDGE)
		gtk_toggle_button_set_active(
			GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE);

	mem_cols = new_cols;
	mem_col_A = mem_cols > 1 ? 1 : 0;
	mem_col_B = 0;
	update_menus();
	init_pal();
	update_all_views();
	gtk_widget_queue_draw(drawing_col_prev);
}

static void toggle_selective(GtkWidget *btn, gpointer user_data)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) return;

	/* Selectivity state toggled */
	if ((dither_sel == 0) ^ ((int)user_data == 0))
	{
		dither_fract[dither_sel ? 1 : 0] = 0.01 * read_spin(dither_spin);
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(dither_spin), 100.0 *
			((int)user_data ? dither_fract[1] : dither_fract[0]));
	}
	dither_sel = (int)user_data;
}

void pressed_quantize(GtkMenuItem *menu_item, gpointer user_data)
{
	GtkWidget *mainbox, *topbox, *notebook, *page0, *page1, *button;
	GtkWidget *vbox, *hbox;

	char *rad_txt[] = {_("Exact Conversion"), _("Use Current Palette"),
		_("DL1 Quantize (fastest)"), _("DL3 Quantize (very slow, better quality)"),
		_("Wu Quantize (best method for small palettes)"),
		_("Max-Min Quantize (best with dithering)"), NULL
		};

	char *rad_txt2[] = {_("None"), _("Floyd-Steinberg"), _("Stucki"),
// !!! "Ordered" not done yet !!!
		/* _("Ordered") */ "",
		_("Floyd-Steinberg (quick)"), _("Dithered (effect)"),
		_("Scattered (effect)"), NULL
		};

	char *dist_txt[] = {_("Largest (Linf)"), _("Sum (L1)"), _("Euclidean (L2)")};
	char *clamp_txt[] = {_("Gamut"), _("Weakly"), _("Strongly")};
	char *err_txt[] = {_("Off"), _("Separate/Sum"), _("Separate/Split"),
		_("Length/Sum"), _("Length/Split"), NULL};
	static char *csp_txt[] = {"RGB", "sRGB", "LXN"};


	quantize_cols = mem_cols_used(257);

	quantize_window = add_a_window(GTK_WINDOW_TOPLEVEL, _("Convert To Indexed"),
		GTK_WIN_POS_CENTER, TRUE);
	mainbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(quantize_window), mainbox);

	/* Colours spin */

	topbox = pack(mainbox, gtk_hbox_new(FALSE, 5));
	gtk_container_set_border_width(GTK_CONTAINER(topbox), 10);
	pack(topbox, gtk_label_new(_("Indexed Colours To Use")));
	quantize_spin = xpack(topbox, add_a_spin(quantize_cols > 256 ?
		mem_cols : quantize_cols, 1, 256));

	/* Notebook */

	notebook = xpack(mainbox, buttoned_book(&page0, &page1, &button,
		_("Settings")));
	gtk_box_pack_end(GTK_BOX(topbox), button, FALSE, FALSE, 0);

	/* Main page - Palette frame */

	/* No exact transfer if too many colours */
	if (quantize_cols > 256) rad_txt[0] = "";
	vbox = wj_radio_pack(rad_txt, -1, 0, quantize_cols > 256 ? 4 : 0,
		&quantize_mode, GTK_SIGNAL_FUNC(click_quantize_radio));
	add_with_frame(page0, _("Palette"), vbox, 5);

	/* Main page - Dither frame */

	hbox = wj_radio_pack(rad_txt2, -1, 6, quantize_cols > 256 ?
		DITH_OLDFS : DITH_NONE, &dither_mode, NULL);
	quantize_dither = add_with_frame(page0, _("Dither"), hbox, 5);
	if (quantize_cols <= 256)
		gtk_widget_set_sensitive(quantize_dither, FALSE);

	/* Settings page */

	hbox = wj_radio_pack(csp_txt, 3, 1, dither_cspace, &dither_cspace, NULL);
	add_with_frame(page1, _("Colour space"), hbox, 5);

	hbox = wj_radio_pack(dist_txt, 3, 1, dither_dist, &dither_dist, NULL);
	add_with_frame(page1, _("Difference measure"), hbox, 5);

	hbox = wj_radio_pack(clamp_txt, 3, 1, dither_limit, &dither_limit, NULL);
	add_with_frame(page1, _("Reduce colour bleed"), hbox, 5);

	dither_serpent = add_a_toggle(_("Serpentine scan"), page1, dither_scan);

	hbox = pack(page1, gtk_hbox_new(FALSE, 5));
	gtk_widget_show(hbox);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	pack(hbox, gtk_label_new(_("Error propagation, %")));
	dither_spin = xpack(hbox, add_a_spin(100, 0, 100));
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(dither_spin), (dither_sel ?
		dither_fract[1] : dither_fract[0]) * 100.0);

	hbox = wj_radio_pack(err_txt, -1, 3, dither_sel, &dither_sel,
		GTK_SIGNAL_FUNC(toggle_selective));
	add_with_frame(page1, _("Selective error propagation"), hbox, 5);

	dither_err = add_a_toggle(_("Full error precision"), page1, dither_8b);

	/* OK / Cancel */

	pack(mainbox, OK_box(0, quantize_window, _("OK"), GTK_SIGNAL_FUNC(click_quantize_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));

	gtk_window_set_transient_for(GTK_WINDOW(quantize_window),
		GTK_WINDOW(main_window));
	gtk_widget_show_all(quantize_window);
}

///	GRADIENT WINDOW

static int grad_channel;
static grad_info grad_temps[NUM_CHANNELS];
static grad_map grad_tmaps[NUM_CHANNELS + 1];
static grad_store grad_tbytes;

static GtkWidget *grad_window;
static GtkWidget *grad_spin_len, *grad_spin_rep, *grad_spin_ofs;
static GtkWidget *grad_opt_type, *grad_opt_bound;
static GtkWidget *grad_ss_pre;
static GtkWidget *grad_opt_gtype, *grad_opt_otype;
static GtkWidget *grad_check_grev, *grad_check_orev;

static unsigned char grad_pad[GRAD_POINTS * 3], grad_mpad[GRAD_POINTS];
static int grad_cnt, grad_ofs, grad_slot, grad_mode;

static GtkWidget *grad_ed_cs, *grad_ed_opt, *grad_ed_ss, *grad_ed_tog;
static GtkWidget *grad_ed_len, *grad_ed_bar[16], *grad_ed_left, *grad_ed_right;

#define SLOT_SIZE 15

#define PPAD_SLOT 11
#define PPAD_XSZ 32
#define PPAD_YSZ 8
#define PPAD_WIDTH(X) (PPAD_XSZ * (X) + 1)
#define PPAD_HEIGHT(X) (PPAD_YSZ * (X) + 1)

static gboolean palette_pad_draw(GtkWidget *widget, GdkEventConfigure *event,
	gpointer user_data)
{
	unsigned char *rgb, *tmp;
	int i, j, k, w, h, col, row, cellsize = PPAD_SLOT;
	int channel = (int)user_data;
	GdkPixmap *pmap = gtk_object_get_user_data(GTK_OBJECT(widget));

	if (!pmap) return (TRUE);
	w = PPAD_WIDTH(cellsize);
	h = PPAD_HEIGHT(cellsize);
	row = w * 3;
	rgb = malloc(h * row);
	if (!rgb) return (TRUE);

	memset(rgb, 0, h * row);
	for (col = 0 , i = 1; i < h - 1; i += cellsize)
	{
		tmp = rgb + i * row;
		for (j = 3; j < row - 3; j += cellsize * 3 , col++)
		{
			if (channel == CHN_IMAGE) /* Palette as such */
			{
				if (col < mem_cols) /* Draw only existing colors */
				{
					tmp[j + 0] = mem_pal[col].red;
					tmp[j + 1] = mem_pal[col].green;
					tmp[j + 2] = mem_pal[col].blue;
				}
			}
			else if (channel >= 0) /* Utility */
			{
				k = channel_rgb[channel][0] * col;
				tmp[j + 0] = (k + (k >> 8) + 1) >> 8;
				k = channel_rgb[channel][1] * col;
				tmp[j + 1] = (k + (k >> 8) + 1) >> 8;
				k = channel_rgb[channel][2] * col;
				tmp[j + 2] = (k + (k >> 8) + 1) >> 8;
			}
			else /* Opacity */
			{
				tmp[j + 0] = tmp[j + 1] = tmp[j + 2] = col;
			}
			for (k = j + 3; k < j + cellsize * 3 - 3; k++)
				tmp[k] = tmp[k - 3];
		}
		for (j = i + 1; j < i + cellsize - 1; j++)
			memcpy(rgb + j * row, tmp, row);
	}

	gdk_draw_rgb_image(pmap, widget->style->black_gc, 0, 0, w, h,
		GDK_RGB_DITHER_NONE, rgb, row);

	free(rgb);
	return (TRUE);
}

static void grad_edit_set_rgb(GtkColorSelection *selection, gpointer user_data);
static gboolean palette_pad_click(GtkWidget *widget, GdkEventButton *event,
	gpointer user_data)
{
	int x, y, i;

	/* Only single clicks */
	if (event->type != GDK_BUTTON_PRESS) return (TRUE);

	x = event->x / PPAD_SLOT;
	y = event->y / PPAD_SLOT;
	if ((x < 0) || (x >= PPAD_XSZ) || (y < 0) || (y >= PPAD_YSZ))
		return (TRUE);
	i = y * PPAD_XSZ + x;
	/* Indexed / utility / opacity */
	if (!(int)user_data) mt_spinslide_set_value(grad_ed_ss, i);
	else if (i < mem_cols) /* Valid RGB */
	{
		gdouble color[4] = { (gdouble)mem_pal[i].red / 255.0,
			(gdouble)mem_pal[i].green / 255.0,
			(gdouble)mem_pal[i].blue / 255.0, 1.0};

#if GTK_MAJOR_VERSION == 1
		GtkColorSelection *cs = GTK_COLOR_SELECTION(grad_ed_cs);
		gdouble oldcolor[4];

		/* Preserve old color, invoke "color_changed" handler */
		memcpy(oldcolor, cs->old_values + 3, sizeof(oldcolor));
		gtk_color_selection_set_color(cs, oldcolor);
		gtk_color_selection_set_color(cs, color);
		grad_edit_set_rgb(cs, NULL);
#else
		gtk_color_selection_set_color(GTK_COLOR_SELECTION(grad_ed_cs), color);
#endif
	}
	return (TRUE);
}

static void click_grad_edit_ok(GtkWidget *widget)
{
	int idx = (grad_channel == CHN_IMAGE) && (mem_img_bpp == 3) ? 0 :
		grad_channel + 1;

	if (grad_mode < 0) /* Opacity */
	{
		memcpy(grad_tbytes + GRAD_CUSTOM_OPAC(idx), grad_pad, GRAD_POINTS);
		memcpy(grad_tbytes + GRAD_CUSTOM_OMAP(idx), grad_mpad, GRAD_POINTS);
		grad_tmaps[idx].coplen = grad_cnt;
	}
	else /* Gradient */
	{
		memcpy(grad_tbytes + GRAD_CUSTOM_DATA(idx), grad_pad,
			idx ? GRAD_POINTS : GRAD_POINTS * 3);
		memcpy(grad_tbytes + GRAD_CUSTOM_DMAP(idx), grad_mpad, GRAD_POINTS);
		grad_tmaps[idx].cvslen = grad_cnt;
	}
	gtk_widget_destroy(widget);
}

static void grad_load_slot(int slot)
{
	gdouble color[4];
	int i;
#if GTK_MAJOR_VERSION == 2
	GdkColor c;
#endif

	if (slot >= grad_cnt) /* Empty slot */
	{
		grad_slot = slot;
		return;
	}
	grad_slot = -1; /* Block circular signal calls */

	if (!grad_mode) /* RGB */
	{
		GtkColorSelection *cs = GTK_COLOR_SELECTION(grad_ed_cs);

		for (i = 0; i < 3; i++)
			color[i] = (gdouble)grad_pad[slot * 3 + i] / 255.0;
		color[3] = 1.0;
		gtk_color_selection_set_color(cs, color);
#if GTK_MAJOR_VERSION == 1
		gtk_color_selection_set_color(cs, color);
#endif
#if GTK_MAJOR_VERSION == 2
		c.red = grad_pad[slot * 3 + 0] * 257;
		c.green = grad_pad[slot * 3 + 1] * 257;
		c.blue = grad_pad[slot * 3 + 2] * 257;
		gtk_color_selection_set_previous_color(cs, &c);
#endif
		gtk_option_menu_set_history(GTK_OPTION_MENU(grad_ed_opt),
			grad_mpad[slot]);
	}
	else /* Indexed / utility / opacity */
	{
		mt_spinslide_set_value(grad_ed_ss, grad_pad[slot]);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grad_ed_tog),
			grad_mpad[slot] == GRAD_TYPE_CONST);
	}
	grad_slot = slot;
}

static void grad_redraw_slots(int idx0, int idx1)
{
	if (idx0 < grad_ofs) idx0 = grad_ofs;
	if (++idx1 > grad_ofs + 16) idx1 = grad_ofs + 16;
	for (; idx0 < idx1; idx0++)
		gtk_widget_queue_draw(GTK_BIN(grad_ed_bar[idx0 - grad_ofs])->child);
}

static void grad_edit_set_mode()
{
	int mode, ix0 = grad_slot;

	if (grad_slot < 0) return; /* Blocked */
	if (!grad_mode) mode = wj_option_menu_get_history(grad_ed_opt);
	else mode = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grad_ed_tog)) ?
		GRAD_TYPE_CONST : GRAD_TYPE_RGB;
	grad_mpad[grad_slot] = mode;

	if (grad_cnt <= grad_slot)
	{
		ix0 = grad_cnt;
		grad_cnt = grad_slot + 1;
		gtk_spin_button_set_value(GTK_SPIN_BUTTON(grad_ed_len), grad_cnt);
	}
	grad_redraw_slots(ix0, grad_slot);
}

static void grad_edit_set_rgb(GtkColorSelection *selection, gpointer user_data)
{
	gdouble color[4];
	int i;

	if (grad_slot < 0) return; /* Blocked */
	gtk_color_selection_get_color(selection, color);
	for (i = 0; i < 3; i++)
		grad_pad[grad_slot * 3 + i] = rint(color[i] * 255.0);
	grad_edit_set_mode();
}

static void grad_edit_move_slide(GtkAdjustment *adj, gpointer user_data)
{
	if (grad_slot < 0) return; /* Blocked */
	grad_pad[grad_slot] = ADJ2INT(adj);
	grad_edit_set_mode();
}

static void grad_edit_length(GtkAdjustment *adj, gpointer user_data)
{
	int l0 = grad_cnt;
	grad_cnt = ADJ2INT(adj);
	if (l0 < grad_cnt) grad_redraw_slots(l0, grad_cnt - 1);
	else if (l0 > grad_cnt) grad_redraw_slots(grad_cnt, l0 - 1);
}

static void grad_edit_scroll(GtkButton *button, gpointer user_data)
{
	int dir = (int)user_data;
	
	grad_ofs += dir;
	gtk_widget_set_sensitive(grad_ed_left, !!grad_ofs);
	gtk_widget_set_sensitive(grad_ed_right, grad_ofs < GRAD_POINTS - 16);
	grad_redraw_slots(0, GRAD_POINTS);
}

static void grad_edit_slot(GtkWidget *btn, gpointer user_data)
{
	if (!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(btn))) return;
	grad_load_slot((int)user_data + grad_ofs);
}

static gboolean grad_draw_slot(GtkWidget *widget, GdkEventExpose *event,
	gpointer idx)
{
	unsigned char rgb[SLOT_SIZE * 2 * 3];
	int i, n = (int)idx + grad_ofs, mode = n >= grad_cnt ? -2 : grad_mode;

	switch (mode)
	{
	default: /* Error */
	case -2: /* Empty */
		rgb[0] = rgb[1] = rgb[2] = 178;
		break;
	case -1: /* Opacity */
		rgb[0] = rgb[1] = rgb[2] = grad_pad[n];
		break;
	case 0: /* RGB */
		rgb[0] = grad_pad[n * 3 + 0];
		rgb[1] = grad_pad[n * 3 + 1];
		rgb[2] = grad_pad[n * 3 + 2];
		break;
	case CHN_IMAGE + 1: /* Indexed */
		i = grad_pad[n];
		if (i >= mem_cols) rgb[0] = rgb[1] = rgb[2] = 0;
		else
		{
			rgb[0] = mem_pal[i].red;
			rgb[1] = mem_pal[i].green;
			rgb[2] = mem_pal[i].blue;
		}
		break;
	case CHN_ALPHA + 1: /* Alpha */
	case CHN_SEL + 1: /* Selection */
	case CHN_MASK + 1: /* Mask */
		i = channel_rgb[mode - 1][0] * grad_pad[n];
		rgb[0] = (i + (i >> 8) + 1) >> 8;
		i = channel_rgb[mode - 1][1] * grad_pad[n];
		rgb[1] = (i + (i >> 8) + 1) >> 8;
		i = channel_rgb[mode - 1][2] * grad_pad[n];
		rgb[2] = (i + (i >> 8) + 1) >> 8;
		break;
	}
	for (i = 3; i < SLOT_SIZE * 2 * 3; i++) rgb[i] = rgb[i - 3];
	if (mode == -2) /* Empty slot - show that */
		memset(rgb, 128, SLOT_SIZE * 3);

	gdk_draw_rgb_image(widget->window, widget->style->black_gc,
		0, 0, SLOT_SIZE, SLOT_SIZE, GDK_RGB_DITHER_NONE,
		rgb + SLOT_SIZE * 3, -3);

	return (TRUE);
}

static void grad_edit(GtkWidget *widget, gpointer user_data)
{
	GtkWidget *win, *mainbox, *hbox, *hbox2, *pix, *cs, *ss, *sw, *btn;
	int i, idx, opac = (int)user_data != 0;

	idx = (grad_channel == CHN_IMAGE) && (mem_img_bpp == 3) ? 0 :
		grad_channel + 1;

	/* Copy to temp */
	if (opac)
	{
		memcpy(grad_pad, grad_tbytes + GRAD_CUSTOM_OPAC(idx), GRAD_POINTS);
		memcpy(grad_mpad, grad_tbytes + GRAD_CUSTOM_OMAP(idx), GRAD_POINTS);
		grad_cnt = grad_tmaps[idx].coplen;
	}
	else
	{
		memcpy(grad_pad, grad_tbytes + GRAD_CUSTOM_DATA(idx),
			idx ? GRAD_POINTS : GRAD_POINTS * 3);
		memcpy(grad_mpad, grad_tbytes + GRAD_CUSTOM_DMAP(idx), GRAD_POINTS);
		grad_cnt = grad_tmaps[idx].cvslen;
	}
	if (grad_cnt < 2) grad_cnt = 2;
	grad_ofs = grad_slot = 0;
	grad_mode = opac ? -1 : idx;

	win = add_a_window(GTK_WINDOW_TOPLEVEL, _("Edit Gradient"),
		GTK_WIN_POS_CENTER, TRUE);
	mainbox = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(mainbox), 5);
	gtk_container_add(GTK_CONTAINER(win), mainbox);

	/* Palette pad */

	sw = pack(mainbox, gtk_alignment_new(0.5, 0.5, 0.0, 0.0));
	pix = wj_pixmap(PPAD_WIDTH(PPAD_SLOT), PPAD_HEIGHT(PPAD_SLOT));
	gtk_container_add(GTK_CONTAINER(sw), pix);
	gtk_signal_connect(GTK_OBJECT(pix), "configure_event",
		GTK_SIGNAL_FUNC(palette_pad_draw),
		(gpointer)(opac ? -1 : grad_channel));
	gtk_signal_connect(GTK_OBJECT(pix), "button_press_event",
		GTK_SIGNAL_FUNC(palette_pad_click), (gpointer)!grad_mode);
	add_hseparator(mainbox, -2, 10);

	/* Editor widgets */

	if (!grad_mode) /* RGB */
	{
		char *interp[] = {_("RGB"), _("HSV"), _("Backward HSV"),
			_("Constant")};

		grad_ed_cs = cs = pack(mainbox, gtk_color_selection_new());
#if GTK_MAJOR_VERSION == 1
		gtk_color_selection_set_opacity(GTK_COLOR_SELECTION(cs), FALSE);
#endif
#if GTK_MAJOR_VERSION == 2
		gtk_color_selection_set_has_opacity_control(GTK_COLOR_SELECTION(cs), FALSE);
		gtk_color_selection_set_has_palette(GTK_COLOR_SELECTION(cs), TRUE);
#endif
		gtk_signal_connect(GTK_OBJECT(cs), "color_changed",
			GTK_SIGNAL_FUNC(grad_edit_set_rgb), NULL);
		grad_ed_opt = sw = wj_option_menu(interp, 4, 0, NULL,
			GTK_SIGNAL_FUNC(grad_edit_set_mode));
		/* Protect sliders from option menu in GTK1 */
#if GTK_MAJOR_VERSION == 1
		sw = widget_align_minsize(sw, 0, 0);
#endif
	}
	else /* Indexed / utility / opacity */
	{
		grad_ed_ss = ss = pack(mainbox, mt_spinslide_new(-2, -2));
		mt_spinslide_set_range(ss, 0, grad_mode == CHN_IMAGE + 1 ?
			mem_cols - 1 : 255);
		mt_spinslide_connect(ss, GTK_SIGNAL_FUNC(grad_edit_move_slide), NULL);
		grad_ed_tog = sw = sig_toggle(_("Constant"), FALSE, NULL,
			GTK_SIGNAL_FUNC(grad_edit_set_mode));
	}
	hbox = pack(mainbox, gtk_hbox_new(TRUE, 5));
	gtk_box_pack_start(GTK_BOX(hbox), sw, TRUE, TRUE, 0);
	hbox2 = xpack(hbox, gtk_hbox_new(FALSE, 5));
	pack(hbox2, gtk_label_new(_("Points:")));
	grad_ed_len = sw = pack(hbox2, add_a_spin(grad_cnt, 2, GRAD_POINTS));
	spin_connect(sw, GTK_SIGNAL_FUNC(grad_edit_length), NULL);

	/* Gradient bar */

	hbox2 = pack(mainbox, gtk_hbox_new(TRUE, 0));
	grad_ed_left = btn = xpack(hbox2, gtk_button_new());
	gtk_container_add(GTK_CONTAINER(btn), gtk_arrow_new(GTK_ARROW_LEFT,
		GTK_SHADOW_NONE));
	gtk_widget_set_sensitive(btn, FALSE);
	gtk_signal_connect(GTK_OBJECT(btn), "clicked",
		GTK_SIGNAL_FUNC(grad_edit_scroll), (gpointer)(-1));
	btn = NULL;
	for (i = 0; i < 16; i++)
	{
		grad_ed_bar[i] = btn = xpack(hbox2, gtk_radio_button_new_from_widget(
			GTK_RADIO_BUTTON_0(btn)));
		gtk_toggle_button_set_mode(GTK_TOGGLE_BUTTON(btn), FALSE);
		gtk_signal_connect(GTK_OBJECT(btn), "toggled",
			GTK_SIGNAL_FUNC(grad_edit_slot), (gpointer)i);
		sw = gtk_drawing_area_new();
		gtk_container_add(GTK_CONTAINER(btn), sw);
		gtk_widget_set_usize(sw, SLOT_SIZE, SLOT_SIZE);
		gtk_signal_connect(GTK_OBJECT(sw), "expose_event",
			GTK_SIGNAL_FUNC(grad_draw_slot), (gpointer)i);
	}
	grad_ed_right = btn = xpack(hbox2, gtk_button_new());
	gtk_container_add(GTK_CONTAINER(btn), gtk_arrow_new(GTK_ARROW_RIGHT,
		GTK_SHADOW_NONE));
	gtk_signal_connect(GTK_OBJECT(btn), "clicked",
		GTK_SIGNAL_FUNC(grad_edit_scroll), (gpointer)1);

	pack(mainbox, OK_box(0, win, _("OK"), GTK_SIGNAL_FUNC(click_grad_edit_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));

	grad_load_slot(0);

	gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(grad_window));
	gtk_widget_show_all(win);

	/* Protect sliders from option menu in GTK1 */
#if GTK_MAJOR_VERSION == 1
	gtk_container_set_resize_mode(GTK_CONTAINER(hbox), GTK_RESIZE_QUEUE);
#endif
}

#define NUM_GTYPES 6
#define NUM_OTYPES 3
static const char gtmap[NUM_GTYPES * 2] = {GRAD_TYPE_RGB, 1, GRAD_TYPE_RGB, 2,
	GRAD_TYPE_HSV, 2, GRAD_TYPE_BK_HSV, 2, GRAD_TYPE_CONST, 3,
	GRAD_TYPE_CUSTOM, 3};
static const char opmap[NUM_OTYPES] = {GRAD_TYPE_RGB, GRAD_TYPE_CONST,
	GRAD_TYPE_CUSTOM};

static void grad_reset_menu(int mode, int bpp)
{
	GList *items = GTK_MENU_SHELL(gtk_option_menu_get_menu(
		GTK_OPTION_MENU(grad_opt_gtype)))->children;
	char f = bpp == 1 ? 1 : 2;
	int i, j;

	for (j = NUM_GTYPES - 1; j >= 0; j--)
	{
		if ((gtmap[j * 2] == mode) && (gtmap[j * 2 + 1] & f)) break;
	}

	for (; items; items = items->next)
	{
		i = (int)gtk_object_get_user_data(GTK_OBJECT(items->data));
		(gtmap[2 * i + 1] & f ? gtk_widget_show : gtk_widget_hide)
			(GTK_WIDGET(items->data));
	}
	gtk_option_menu_set_history(GTK_OPTION_MENU(grad_opt_gtype), j);
}

static void store_channel_gradient(int channel)
{
	grad_info *grad = grad_temps + channel;
	grad_map *gmap = grad_tmaps + channel + 1;

	if (channel < 0) return;
	if ((channel == CHN_IMAGE) && (mem_img_bpp == 3)) gmap = grad_tmaps;

	grad->len = read_spin(grad_spin_len);
	grad->gmode = wj_option_menu_get_history(grad_opt_type) + 1;
	grad->rep = read_spin(grad_spin_rep);
	grad->rmode = wj_option_menu_get_history(grad_opt_bound);
	grad->ofs = read_spin(grad_spin_ofs);

	gmap->gtype = gtmap[2 * wj_option_menu_get_history(grad_opt_gtype)];
	gmap->grev = !!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grad_check_grev));
	gmap->otype = opmap[wj_option_menu_get_history(grad_opt_otype)];
	gmap->orev = !!gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(grad_check_orev));
}

static void show_channel_gradient(int channel)
{
	grad_info *grad = grad_temps + channel;
	grad_map *gmap;
	int i, idx = channel + 1, bpp = BPP(channel);

	if (bpp == 3) --idx;
	gmap = grad_tmaps + idx;

	gtk_spin_button_set_value(GTK_SPIN_BUTTON(grad_spin_len), grad->len);
	gtk_option_menu_set_history(GTK_OPTION_MENU(grad_opt_type), grad->gmode - 1);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(grad_spin_rep), grad->rep);
	gtk_option_menu_set_history(GTK_OPTION_MENU(grad_opt_bound), grad->rmode);
	gtk_spin_button_set_value(GTK_SPIN_BUTTON(grad_spin_ofs), grad->ofs);

	grad_reset_menu(gmap->gtype, bpp);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grad_check_grev), gmap->grev);
	for (i = NUM_OTYPES - 1; (i >= 0) && (opmap[i] != gmap->otype); i--);
	gtk_option_menu_set_history(GTK_OPTION_MENU(grad_opt_otype), i);
	gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(grad_check_orev), gmap->orev);
}

static void click_grad_apply(GtkWidget *widget)
{
	int i;

	store_channel_gradient(grad_channel);
	memcpy(gradient, grad_temps, sizeof(grad_temps));
	memcpy(graddata, grad_tmaps, sizeof(grad_tmaps));
	memcpy(gradbytes, grad_tbytes, sizeof(grad_tbytes));

	grad_opacity = mt_spinslide_get_value(grad_ss_pre);

	for (i = 0; i < NUM_CHANNELS; i++) grad_update(gradient + i);
	for (i = 0; i <= NUM_CHANNELS; i++) gmap_setup(graddata + i, gradbytes, i);
	grad_def_update();
	toolbar_update_settings();
	if ((tool_type == TOOL_GRADIENT) && grad_opacity)
		gtk_widget_queue_draw(drawing_canvas);
}

static void click_grad_ok(GtkWidget *widget)
{
	click_grad_apply(widget);
	gtk_widget_destroy(widget);
}

static void grad_channel_changed(GtkToggleButton *widget, gpointer user_data)
{
	if ((int)user_data == grad_channel) return;
	if (!gtk_toggle_button_get_active(widget)) return;
	store_channel_gradient(grad_channel);
	grad_channel = -1;
	show_channel_gradient((int)user_data);
	grad_channel = (int)user_data;
}

static void grad_selector_box(GtkWidget *box, char **mtext, int op)
{
	GtkWidget *vbox, *hbox, *menu, *rev, *btn;

	vbox = gtk_vbox_new(FALSE, 0);
	add_with_frame_x(box, op ? _("Opacity") : _("Gradient"), vbox, 5, TRUE);
	menu = pack(vbox, wj_option_menu(mtext, op ? NUM_OTYPES : NUM_GTYPES,
		0, NULL, NULL));
	gtk_container_set_border_width(GTK_CONTAINER(menu), 5);
	hbox = pack(vbox, gtk_hbox_new(TRUE, 0));
	rev = add_a_toggle(_("Reverse"), hbox, FALSE);
	btn = add_a_button(_("Edit Custom"), 5, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(btn), "clicked",
		GTK_SIGNAL_FUNC(grad_edit), (gpointer)op);
	if (op)
	{
		grad_opt_otype = menu;
		grad_check_orev = rev;
	}
	else
	{
		grad_opt_gtype = menu;
		grad_check_grev = rev;
	}
}

void gradient_setup(int mode)
{
	char *gtypes[] = {_("Linear"), _("Bilinear"), _("Radial"), _("Square")};
	char *rtypes[] = {_("None"), _("Level"), _("Repeat"), _("Mirror")};
	char *gradtypes[] = {_("A to B"), _("A to B (RGB)"), _("A to B (HSV)"),
		_("A to B (backward HSV)"), _("A only"), _("Custom")};
	char *optypes[] = {_("Current to 0"), _("Current only"), _("Custom")};
	GtkWidget *win, *mainbox, *hbox, *table, *align;
	GtkWindowPosition pos = !mode && !inifile_get_gboolean("centerSettings", TRUE) ?
		GTK_WIN_POS_MOUSE : GTK_WIN_POS_CENTER;
#if GTK_MAJOR_VERSION == 1
	GtkWidget *hbox1, *hbox2;
#endif

	memcpy(grad_temps, gradient, sizeof(grad_temps));
	memcpy(grad_tmaps, graddata, sizeof(grad_tmaps));
	memcpy(grad_tbytes, gradbytes, sizeof(grad_tbytes));
	grad_channel = mem_channel;

	grad_window = win = add_a_window(GTK_WINDOW_TOPLEVEL,
		_("Configure Gradient"), pos, TRUE);
	mainbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(win), mainbox);

	/* Channel box */

	hbox = wj_radio_pack(allchannames, 4, 1, mem_channel, NULL,
		GTK_SIGNAL_FUNC(grad_channel_changed));
	add_with_frame(mainbox, _("Channel"), hbox, 5);

	/* Setup block */

	table = add_a_table(3, 4, 5, mainbox);
	add_to_table(_("Length"), table, 0, 0, 5);
	grad_spin_len = spin_to_table(table, 0, 1, 5, 0, 0, MAX_GRAD);
	add_to_table(_("Repeat length"), table, 1, 0, 5);
	grad_spin_rep = spin_to_table(table, 1, 1, 5, 0, 0, MAX_GRAD);
	add_to_table(_("Offset"), table, 2, 0, 5);
	grad_spin_ofs = spin_to_table(table, 2, 1, 5, 0, -MAX_GRAD, MAX_GRAD);
	add_to_table(_("Gradient type"), table, 0, 2, 5);
	grad_opt_type = wj_option_menu(gtypes, 4, 0, NULL, NULL);
	add_to_table(_("Extension type"), table, 1, 2, 5);
	grad_opt_bound = wj_option_menu(rtypes, 4, 0, NULL, NULL);
	/* Protect spinslider from option menus in GTK1 */
#if GTK_MAJOR_VERSION == 1

	hbox1 = gtk_hbox_new(FALSE, 0);
	xpack(hbox1, widget_align_minsize(grad_opt_type, 0, 0));
	gtk_table_attach(GTK_TABLE(table), hbox1, 3, 4, 0, 1,
		GTK_EXPAND | GTK_FILL, 0, 0, 5);
	hbox2 = gtk_hbox_new(FALSE, 0);
	xpack(hbox2, widget_align_minsize(grad_opt_bound, 0, 0));
	gtk_table_attach(GTK_TABLE(table), hbox2, 3, 4, 1, 2,
		GTK_EXPAND | GTK_FILL, 0, 0, 5);
#else
	gtk_table_attach(GTK_TABLE(table), grad_opt_type, 3, 4, 0, 1,
		GTK_EXPAND | GTK_FILL, 0, 0, 5);
	gtk_table_attach(GTK_TABLE(table), grad_opt_bound, 3, 4, 1, 2,
		GTK_EXPAND | GTK_FILL, 0, 0, 5);
#endif
	add_to_table(_("Preview opacity"), table, 2, 2, 5);
	grad_ss_pre = mt_spinslide_new(-1, -1);
	mt_spinslide_set_range(grad_ss_pre, 0, 255);
	mt_spinslide_set_value(grad_ss_pre, grad_opacity);
	/* !!! Box derivatives can't have their "natural" size set directly */
	align = widget_align_minsize(grad_ss_pre, 200, -2);
	gtk_table_attach(GTK_TABLE(table), align, 3, 4, 2, 3,
		GTK_EXPAND | GTK_FILL, 0, 0, 5);

	/* Select page */

	hbox = pack(mainbox, gtk_hbox_new(TRUE, 0));
	grad_selector_box(hbox, gradtypes, 0);
	grad_selector_box(hbox, optypes, 1);

	/* Cancel / Apply / OK */

	align = pack(mainbox, OK_box(0, win, _("OK"), GTK_SIGNAL_FUNC(click_grad_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy)));
	OK_box_add(align, _("Apply"), GTK_SIGNAL_FUNC(click_grad_apply), 1);

	/* Fill in values */

	gtk_widget_show_all(mainbox);
	show_channel_gradient(mem_channel);

	gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(main_window));
	gtk_widget_show(win);

	/* Properly set up slider & option menus in GTK1 */
#if GTK_MAJOR_VERSION == 1
	gtk_widget_queue_resize(win);
	gtk_container_set_resize_mode(GTK_CONTAINER(hbox), GTK_RESIZE_QUEUE);
	gtk_container_set_resize_mode(GTK_CONTAINER(hbox1), GTK_RESIZE_QUEUE);
	gtk_container_set_resize_mode(GTK_CONTAINER(hbox2), GTK_RESIZE_QUEUE);
#endif
}

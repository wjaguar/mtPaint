/*	mainwindow.c
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

#include <stdlib.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>
#include <string.h>
#include <math.h>

#include "global.h"

#include "memory.h"
#include "png.h"
#include "mainwindow.h"
#include "viewer.h"
#include "mygtk.h"
#include "otherwindow.h"
#include "inifile.h"
#include "canvas.h"
#include "polygon.h"
#include "layer.h"
#include "info.h"
#include "prefs.h"
#include "ani.h"
#include "channels.h"
#include "toolbar.h"
#include "csel.h"


#include "graphics/icon.xpm"



GtkWidget
	*main_window, *main_vsplit, *main_hsplit, *main_split,
	*drawing_palette, *drawing_canvas, *vbox_right, *vw_scrolledwindow,
	*scrolledwindow_canvas, *main_hidden[4],

	*menu_undo[5], *menu_redo[5], *menu_crop[5],
	*menu_need_marquee[10], *menu_need_selection[20], *menu_need_clipboard[30],
	*menu_help[2], *menu_only_24[10], *menu_not_indexed[10], *menu_only_indexed[10],
	*menu_recent[23], *menu_cline[2], *menu_view[2], *menu_layer[2],
	*menu_lasso[15], *menu_prefs[2], *menu_frames[2], *menu_alphablend[2],
	*menu_chann_x[NUM_CHANNELS+1], *menu_chan_del[2],
	*menu_chan_dis[NUM_CHANNELS+1]
	;

gboolean view_image_only = FALSE, viewer_mode = FALSE, drag_index = FALSE, q_quit;
int files_passed, file_arg_start = -1, drag_index_vals[2], cursor_corner;
char **global_argv;

GdkGC *dash_gc;


static int perim_status, perim_x, perim_y, perim_s;	// Tool perimeter

static void clear_perim_real( int ox, int oy )
{
	int x0, y0, x1, y1, zoom = 1, scale = 1;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	x0 = margin_main_x + ((perim_x + ox) * scale) / zoom;
	y0 = margin_main_y + ((perim_y + oy) * scale) / zoom;
	x1 = margin_main_x + ((perim_x + ox + perim_s - 1) * scale) / zoom + scale - 1;
	y1 = margin_main_y + ((perim_y + oy + perim_s - 1) * scale) / zoom + scale - 1;

	repaint_canvas(x0, y0, 1, y1 - y0 + 1);
	repaint_canvas(x1, y0, 1, y1 - y0 + 1);
	repaint_canvas(x0 + 1, y0, x1 - x0 - 1, 1);
	repaint_canvas(x0 + 1, y1, x1 - x0 - 1, 1);
}

void men_item_state( GtkWidget *menu_items[], gboolean state )
{	// Enable or disable menu items
	int i = 0;
	while ( menu_items[i] != NULL )
	{
		gtk_widget_set_sensitive( menu_items[i], state );
		i++;
	}
}

void pop_men_dis( GtkItemFactory *item_factory, char *items[], GtkWidget *menu_items[] )
{	// Populate disable menu item array
	int i = 0;
	while ( items[i] != NULL )
	{
		menu_items[i] = gtk_item_factory_get_item(item_factory, items[i]);
		i++;
	}
	menu_items[i] = NULL;
}

void men_dis_add( GtkWidget *widget, GtkWidget *menu_items[] )		// Add widget to disable list
{
	int i = 0;

	while ( menu_items[i] != NULL ) i++;
	menu_items[i] = widget;
	menu_items[i+1] = NULL;
}

void pressed_swap_AB( GtkMenuItem *menu_item, gpointer user_data )
{
	mem_swap_cols();
	if (mem_channel == CHN_IMAGE)
	{
		repaint_top_swatch();
		init_pal();
		gtk_widget_queue_draw( drawing_col_prev );
	}
	else pressed_opacity(channel_col_A[mem_channel]);
}

void pressed_load_recent( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	int change;
	char txt[64], *c, old_file[256];

	sprintf( txt, "file%i", item );
	c = inifile_get( txt, "." );
	strncpy( old_file, c, 250 );

	if ( layers_total==0 )
		change = check_for_changes();
	else
		change = check_layers_for_changes();

	if ( change == 2 || change == -10 )
		do_a_load(old_file);		// Load requested file
}

void pressed_crop( GtkMenuItem *menu_item, gpointer user_data )
{
	int res, x1, y1, x2, y2;

	mtMIN( x1, marq_x1, marq_x2 )
	mtMIN( y1, marq_y1, marq_y2 )
	mtMAX( x2, marq_x1, marq_x2 )
	mtMAX( y2, marq_y1, marq_y2 )

	if ( marq_status != MARQUEE_DONE ) return;
	if ( x1==0 && x2>=(mem_width-1) && y1==0 && y2>=(mem_height-1) ) return;

	res = mem_image_resize(x2 - x1 + 1, y2 - y1 + 1, -x1, -y1, 0);

	if ( res == 0 )
	{
		pressed_select_none(NULL, NULL);
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
		canvas_undo_chores();
	}
	else memory_errors(3-res);
}

void pressed_select_none( GtkMenuItem *menu_item, gpointer user_data )
{
	if ( marq_status != MARQUEE_NONE )
	{
		paint_marquee(0, marq_x1, marq_y1);
		marq_status = MARQUEE_NONE;
		marq_x1 = marq_y1 = marq_x2 = marq_y2 = -1;
		update_menus();
		gtk_widget_queue_draw( drawing_canvas );
		set_cursor();
		update_sel_bar();
	}

	if ( tool_type == TOOL_POLYGON )
	{
		if ( poly_status != POLY_NONE )
		{
			poly_points = 0;
			poly_status = POLY_NONE;
			update_menus();
			gtk_widget_queue_draw( drawing_canvas );
			set_cursor();
			update_sel_bar();
		}
	}
}

void pressed_select_all( GtkMenuItem *menu_item, gpointer user_data )
{
	int i = 0;

	paste_prepare();
	if ( tool_type != TOOL_SELECT )
	{
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE);
		i = 1;
	}

	if ( marq_status >= MARQUEE_PASTE ) i = 1;

	marq_status = MARQUEE_DONE;
	marq_x1 = 0;
	marq_y1 = 0;
	marq_x2 = mem_width - 1;
	marq_y2 = mem_height - 1;

	update_menus();
	paint_marquee(1, marq_x1-mem_width, marq_y1-mem_height);
	update_sel_bar();
	if ( i == 1 ) gtk_widget_queue_draw( drawing_canvas );		// Clear old past stuff
}

void pressed_remove_unused( GtkMenuItem *menu_item, gpointer user_data )
{
	int i;

	i = mem_remove_unused_check();
	if ( i <= 0 )
		alert_box( _("Error"), _("There were no unused colours to remove!"),
			_("OK"), NULL, NULL);
	if ( i > 0 )
	{
		spot_undo(UNDO_XPAL);

		mem_remove_unused();

		if ( mem_col_A >= mem_cols ) mem_col_A = 0;
		if ( mem_col_B >= mem_cols ) mem_col_B = 0;
		init_pal();
		gtk_widget_queue_draw( drawing_canvas );
		gtk_widget_queue_draw(drawing_col_prev);
	}
}

void pressed_default_pal( GtkMenuItem *menu_item, gpointer user_data )
{
	spot_undo(UNDO_PAL);
	mem_pal_copy( mem_pal, mem_pal_def );
	mem_cols = mem_pal_def_i;
	init_pal();
	update_all_views();
	gtk_widget_queue_draw(drawing_col_prev);
}

void pressed_remove_duplicates( GtkMenuItem *menu_item, gpointer user_data )
{
	int dups;
	char mess[256];

	if ( mem_cols < 3 )
	{
		alert_box( _("Error"), _("The palette does not contain enough colours to do a merge"),
			_("OK"), NULL, NULL );
	}
	else
	{
		dups = scan_duplicates();

		if ( dups == 0 )
		{
			alert_box( _("Error"), _("The palette does not contain 2 colours that have identical RGB values"), _("OK"), NULL, NULL );
			return;
		}
		else
		{
			if ( dups == (mem_cols - 1) )
			{
				alert_box( _("Error"), _("There are too many identical palette items to be reduced."), _("OK"), NULL, NULL );
				return;
			}
			else
			{
				snprintf(mess, 250, _("The palette contains %i colours that have identical RGB values.  Do you really want to merge them into one index and realign the canvas?"), dups );
				if ( alert_box( _("Warning"), mess, _("Yes"), _("No"), NULL ) == 1 )
				{
					spot_undo(UNDO_XPAL);

					remove_duplicates();
					init_pal();
					gtk_widget_queue_draw( drawing_canvas );
					gtk_widget_queue_draw(drawing_col_prev);
				}
			}
		}
	}
}

void pressed_create_patterns( GtkMenuItem *menu_item, gpointer user_data )
{	// Create a pattern.c file from the current image

	int row, column, pattern, sx, sy, pixel;
	FILE *fp;

//printf("w = %i h = %i c = %i\n\n", mem_width, mem_height, mem_cols );

	if ( mem_width == 94 && mem_height == 94 && mem_cols == 3 )
	{
		fp = fopen("pattern_user.c", "w");
		if ( fp == NULL ) alert_box( _("Error"),
				_("patterns_user.c could not be opened in current directory"),
				_("OK"), NULL, NULL );
		else
		{
			fprintf( fp, "char mem_patterns[81][8][8] = \n{\n" );
			pattern = 0;
			while ( pattern < 81 )
			{
				fprintf( fp, "{ " );
				sy = 2 + (pattern / 9) * 10;		// Start y pixel on main image
				sx = 3 + (pattern % 9) * 10;		// Start x pixel on main image
				for ( column = 0; column < 8; column++ )
				{
					fprintf( fp, "{" );
					for ( row = 0; row < 8; row++ )
					{
						pixel = mem_img[CHN_IMAGE][ sx+row + 94*(sy+column) ];
						fprintf( fp, "%i", pixel % 2 );
						if ( row < 10 ) fprintf( fp, "," );
					}
					if ( column < 10 ) fprintf( fp, "},\n" );
					else  fprintf( fp, "}\n" );
				}
				if ( pattern < 80 ) fprintf( fp, "},\n" );
				else  fprintf( fp, "}\n" );
				pattern++;
			}
			fprintf( fp, "};\n" );
			alert_box( _("Done"), _("patterns_user.c created in current directory"),
				_("OK"), NULL, NULL );
			fclose( fp );
		}
	}
	else
	{
		alert_box( _("Error"), _("Current image is not 94x94x3 so I cannot create patterns_user.c"), _("OK"), NULL, NULL );
	}
}

void pressed_mask( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	mem_mask_setall(item);
	mem_pal_init();
	gtk_widget_queue_draw( drawing_palette );
	/* !!! Do the same for any other kind of preview */
	if ((tool_type == TOOL_SELECT) && (marq_status >= MARQUEE_PASTE))
		update_all_views();
}

static void pressed_export_undo( GtkMenuItem *menu_item, gpointer user_data )
{
	if ( mem_undo_done>0 ) file_selector( FS_EXPORT_UNDO );
}

static void pressed_export_undo2( GtkMenuItem *menu_item, gpointer user_data )
{
	if ( mem_undo_done>0 ) file_selector( FS_EXPORT_UNDO2 );
}

static void pressed_export_ascii( GtkMenuItem *menu_item, gpointer user_data )
{
	if ( mem_cols <= 16 ) file_selector( FS_EXPORT_ASCII );
	else alert_box( _("Error"), _("You must have 16 or fewer palette colours to export ASCII art."),
		_("OK"), NULL, NULL );
}

static void pressed_export_gif( GtkMenuItem *menu_item, gpointer user_data )
{
	if ( strcmp( mem_filename, _("Untitled") ) ) file_selector( FS_EXPORT_GIF );
	else alert_box( _("Error"), _("You must save at least one frame to create an animated GIF."),
		_("OK"), NULL, NULL );
}

void pressed_open_pal( GtkMenuItem *menu_item, gpointer user_data )
{	file_selector( FS_PALETTE_LOAD ); }

void pressed_save_pal( GtkMenuItem *menu_item, gpointer user_data )
{	file_selector( FS_PALETTE_SAVE ); }

void pressed_open_file( GtkMenuItem *menu_item, gpointer user_data )
{	file_selector( FS_PNG_LOAD ); }

void pressed_save_file_as( GtkMenuItem *menu_item, gpointer user_data )
{	file_selector( FS_PNG_SAVE ); }

int gui_save(char *filename, ls_settings *settings)
{
	int res;
	char mess[512];

	/* Prepare to save image */
	memcpy(settings->img, mem_img, sizeof(chanlist));
	settings->pal = mem_pal;
	settings->width = mem_width;
	settings->height = mem_height;
	settings->bpp = mem_img_bpp;
	settings->colors = mem_cols;

	res = save_image(filename, settings);
	if ( res < 0 )
	{
		if ( res == NOT_XPM )
		{
			alert_box( _("Error"), _("You are trying to save an RGB image to an XPM file which is not possible.  I would suggest you save with a PNG extension."), _("OK"), NULL, NULL );
		}
		if ( res == NOT_GIF )
		{
			alert_box( _("Error"), _("You are trying to save an RGB image to a GIF file which is not possible.  I would suggest you save with a PNG extension."), _("OK"), NULL, NULL );
		}
		if ( res == NOT_XBM )
		{
			alert_box( _("Error"), _("You are trying to save an XBM file with a palette of more than 2 colours.  Either use another format or reduce the palette to 2 colours."), _("OK"), NULL, NULL );
		}
		if ( res == NOT_JPEG )
		{
			alert_box( _("Error"), _("You are trying to save an indexed canvas to a JPEG file which is not possible.  I would suggest you save with a PNG extension."), _("OK"), NULL, NULL );
		}
		if ( res == NOT_TIFF )
		{
			alert_box( _("Error"), _("You are trying to save an indexed canvas to a TIFF file which is not possible.  I would suggest you save with a PNG extension."), _("OK"), NULL, NULL );
		}
		if ( res == -1 )
		{
			snprintf(mess, 500, _("Unable to save file: %s"), filename);
			alert_box( _("Error"), mess, _("OK"), NULL, NULL );
		}
	}
	else
	{
		notify_unchanged();
		register_file( filename );
	}

	return res;
}

static void pressed_save_file(GtkMenuItem *menu_item, gpointer user_data)
{
	ls_settings settings;

	while (strcmp(mem_filename, _("Untitled")))
	{
		init_ls_settings(&settings, NULL);
		settings.ftype = file_type_by_ext(mem_filename, FF_IMAGE);
		if (settings.ftype == FT_NONE) break;
		settings.mode = FS_PNG_SAVE;
		if (gui_save(mem_filename, &settings) < 0) break;
		return;
	}
	file_selector(FS_PNG_SAVE);
}

char mem_clip_file[256];

void load_clip( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	char clip[256];
	int i;

	snprintf(clip, 251, "%s%i", mem_clip_file, item);
	i = load_image(clip, FS_CLIP_FILE, FT_PNG);

	if ( i!=1 ) alert_box( _("Error"), _("Unable to load clipboard"), _("OK"), NULL, NULL );
	else text_paste = FALSE;

	if ( tool_type == TOOL_SELECT && marq_status >= MARQUEE_PASTE )
		pressed_select_none( NULL, NULL );

	if ( tool_type == TOOL_POLYGON && poly_status >= POLY_NONE )
		pressed_select_none( NULL, NULL );

	update_menus();

	if ( MEM_BPP == mem_clip_bpp ) pressed_paste_centre( NULL, NULL );
}

void save_clip( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	ls_settings settings;
	char clip[256];
	int i;

	/* Prepare settings */
	init_ls_settings(&settings, NULL);
	settings.mode = FS_CLIP_FILE;
	settings.ftype = FT_PNG;
	settings.img[CHN_IMAGE] = mem_clipboard;
	settings.img[CHN_ALPHA] = mem_clip_alpha;
	settings.img[CHN_SEL] = mem_clip_mask;
	settings.pal = mem_pal;
	settings.width = mem_clip_w;
	settings.height = mem_clip_h;
	settings.bpp = mem_clip_bpp;
	settings.colors = mem_cols;

	snprintf(clip, 251, "%s%i", mem_clip_file, item);
	i = save_image(clip, &settings);

	if ( i!=0 ) alert_box( _("Error"), _("Unable to save clipboard"), _("OK"), NULL, NULL );
}

void pressed_opacity( int opacity )
{
	if (mem_channel != CHN_IMAGE)
	{
		channel_col_A[mem_channel] =
			opacity < 0 ? 0 : opacity > 255 ? 255 : opacity;
	}
	else if (mem_img_bpp == 3)
	{
		tool_opacity = opacity < 1 ? 1 : opacity > 255 ? 255 : opacity;

		if ( marq_status >= MARQUEE_PASTE )
			gtk_widget_queue_draw(drawing_canvas);
				// Update the paste on the canvas as we have changed the opacity value
	}
	else tool_opacity = 255;

	toolbar_update_settings();
}

void toggle_view( GtkMenuItem *menu_item, gpointer user_data )
{
	int i;

	view_image_only = !view_image_only;

	for ( i=0; i<1; i++ )
		if (view_image_only) gtk_widget_hide(main_hidden[i]);
			else gtk_widget_show(main_hidden[i]);

	for ( i=1; i<TOOLBAR_MAX; i++ )
	{
		if ( toolbar_boxes[i] ) gtk_widget_hide(toolbar_boxes[i]);
	}

	if ( !view_image_only ) toolbar_showhide();	// Switch toolbar/status/palette on if needed
}

void zoom_in()
{
	if (can_zoom >= 1) align_size(can_zoom + 1);
	else align_size(1.0 / (rint(1.0 / can_zoom) - 1));
}

void zoom_out()
{
	if (can_zoom > 1) align_size(can_zoom - 1);
	else align_size(1.0 / (rint(1.0 / can_zoom) + 1));
}

void zoom_grid( GtkMenuItem *menu_item, gpointer user_data )
{
	mem_show_grid = GTK_CHECK_MENU_ITEM(menu_item)->active;
	inifile_set_gboolean( "gridToggle", mem_show_grid );

	if ( drawing_canvas ) gtk_widget_queue_draw( drawing_canvas );
}

void quit_all( GtkMenuItem *menu_item, gpointer user_data )
{
	delete_event( NULL, NULL, NULL );
}

int move_arrows( int *c1, int *c2, int value )
{
	int ox1 = marq_x1, oy1 = marq_y1, ox2 = marq_x2, oy2 = marq_y2;
	int nx1, ny1, nx2, ny2;

	*c1 = *c1 + value;
	*c2 = *c2 + value;

	nx1 = marq_x1;
	ny1 = marq_y1;
	nx2 = marq_x2;
	ny2 = marq_y2;

	marq_x1 = ox1;
	marq_y1 = oy1;
	marq_x2 = ox2;
	marq_y2 = oy2;

	paint_marquee(0, nx1, ny1);
	*c1 = *c1 + value;
	*c2 = *c2 + value;
	paint_marquee(1, ox1, oy1);

	return 1;
}

static void resize_marquee( int dx, int dy )
{
	paint_marquee(0, marq_x1, marq_y1);

	marq_x2 += dx;
	marq_y2 += dy;

	paint_marquee(1, marq_x1, marq_y1);
}

/* Forward declaration */
static void mouse_event(int event, int x0, int y0, guint state, guint button,
	gdouble pressure, int mflag);

/* For "dual" mouse control */
static int unreal_move, lastdx, lastdy;

static void move_mouse(int dx, int dy, int button)
{
	static GdkModifierType bmasks[4] =
		{0, GDK_BUTTON1_MASK, GDK_BUTTON2_MASK, GDK_BUTTON3_MASK};
	GdkModifierType state;
	int x, y, nx, ny, zoom = 1, scale = 1;

	if (!unreal_move) lastdx = lastdy = 0;
	if (!mem_img[CHN_IMAGE]) return;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	gdk_window_get_pointer(drawing_canvas->window, &x, &y, &state);

	nx = ((x - margin_main_x) * zoom) / scale + lastdx + dx;
	ny = ((y - margin_main_y) * zoom) / scale + lastdy + dy;

	if (button) /* Clicks simulated without extra movements */
	{
		state |= bmasks[button];
		mouse_event(GDK_BUTTON_PRESS, nx, ny, state, button, 1.0, 1);
		state ^= bmasks[button];
		mouse_event(GDK_BUTTON_RELEASE, nx, ny, state, button, 1.0, 1);
		return;
	}

	if ((state & (GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)) ==
		(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)) button = 13;
	else if (state & GDK_BUTTON1_MASK) button = 1;
	else if (state & GDK_BUTTON3_MASK) button = 3;
	else if (state & GDK_BUTTON2_MASK) button = 2;

	if (zoom > 1) /* Fine control required */
	{
		lastdx += dx; lastdy += dy;
		mouse_event(GDK_MOTION_NOTIFY, nx, ny, state, button, 1.0, 1);

		/* Nudge cursor when needed */
		if ((abs(lastdx) >= zoom) || (abs(lastdy) >= zoom))
		{
			dx = lastdx * can_zoom;
			dy = lastdy * can_zoom;
			lastdx -= dx * zoom;
			lastdy -= dy * zoom;
			unreal_move = 3;
			/* Event can be delayed or lost */
			move_mouse_relative(dx, dy);
		}
		else unreal_move = 2;
	}
	else /* Real mouse is precise enough */
	{
		unreal_move = 1;

		/* Simulate movement if failed to actually move mouse */
		if (!move_mouse_relative(dx * scale, dy * scale))
		{
			lastdx += dx; lastdy += dy;
			mouse_event(GDK_MOTION_NOTIFY, nx, ny, state, button, 1.0, 1);
		}
	}
}

int check_arrows(int action)
{
	int mv = mem_nudge;

	if ( marq_status == MARQUEE_DONE )
	{		// User is selecting so allow CTRL+arrow keys to resize the marquee
		switch (action)
		{
			case ACT_LR_LEFT: mv = 1;
			case ACT_LR_2LEFT:
				resize_marquee(-mv, 0);
				return 1;
			case ACT_LR_RIGHT: mv = 1;
			case ACT_LR_2RIGHT:
				resize_marquee(mv, 0);
				return 1;
			case ACT_LR_DOWN: mv = 1;
			case ACT_LR_2DOWN:
				resize_marquee(0, mv);
				return 1;
			case ACT_LR_UP: mv = 1;
			case ACT_LR_2UP:
				resize_marquee(0, -mv);
				return 1;
		}
	}

	switch (action)
	{
	case ACT_SEL_LEFT: mv = 1;
	case ACT_SEL_2LEFT:
		return (move_arrows(&marq_x1, &marq_x2, -mv));
	case ACT_SEL_RIGHT: mv = 1;
	case ACT_SEL_2RIGHT:
		return (move_arrows(&marq_x1, &marq_x2, mv));
	case ACT_SEL_DOWN: mv = 1;
	case ACT_SEL_2DOWN:
		return (move_arrows(&marq_y1, &marq_y2, mv));
	case ACT_SEL_UP: mv = 1;
	case ACT_SEL_2UP:
		return (move_arrows(&marq_y1, &marq_y2, -mv));
	}
	return (0);
}

void stop_line()
{
	if ( line_status != LINE_NONE ) repaint_line(0);
	line_status = LINE_NONE;
}

void change_to_tool(int icon)
{
	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[PAINT_TOOL_ICON+icon]), TRUE );

	if ( perim_status > 0 ) clear_perim_real(0, 0);
}

gint check_zoom_keys_real(int action)
{
	static double zals[9] = { 0.1, 0.25, 0.5, 1, 4, 8, 12, 16, 20 };

	switch (action)
	{
	case ACT_ZOOM_IN:
		zoom_in(); return TRUE;
	case ACT_ZOOM_OUT:
		zoom_out(); return TRUE;
	case ACT_ZOOM_01:
	case ACT_ZOOM_025:
	case ACT_ZOOM_05:
	case ACT_ZOOM_1:
	case ACT_ZOOM_4:
	case ACT_ZOOM_8:
	case ACT_ZOOM_12:
	case ACT_ZOOM_16:
	case ACT_ZOOM_20:
		align_size(zals[action - ACT_ZOOM_01]);
		return TRUE;
	case ACT_VIEW:
		toggle_view( NULL, NULL );
		return TRUE;
	}
	return FALSE;
}

gint check_zoom_keys(int action)
{
	if ((action == ACT_QUIT) && q_quit) quit_all( NULL, NULL );

	if (check_zoom_keys_real(action)) return TRUE;

	switch (action)
	{
	case ACT_BRCOSA:
		pressed_brcosa(NULL, NULL); return TRUE;
	case ACT_PAN:
		pressed_pan(NULL, NULL); return TRUE;
	case ACT_CROP:
		pressed_crop(NULL, NULL); return TRUE;
	case ACT_SWAP_AB:
		pressed_swap_AB(NULL, NULL); return TRUE;
	case ACT_CMDLINE:
		if ( allow_cline ) pressed_cline(NULL, NULL); return TRUE;
	case ACT_PATTERN:
		pressed_choose_patterns(NULL, NULL); return TRUE;
	case ACT_BRUSH:
		pressed_choose_brush(NULL, NULL); return TRUE;
	case ACT_PAINT:
		change_to_tool(0); return TRUE;
	case ACT_SELECT:
		change_to_tool(6); return TRUE;
	}
	return FALSE;
}

#define _C (GDK_CONTROL_MASK)
#define _S (GDK_SHIFT_MASK)
#define _A (GDK_MOD1_MASK)
#define _CS (GDK_CONTROL_MASK | GDK_SHIFT_MASK)
#define _CSA (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK)

key_action main_keys[] = {
	{"QUIT",	ACT_QUIT, GDK_q, 0, 0},
	{"",		ACT_QUIT, GDK_Q, 0, 0},
	{"ZOOM_IN",	ACT_ZOOM_IN, GDK_plus, _CS, 0},
	{"",		ACT_ZOOM_IN, GDK_KP_Add, _CS, 0},
	{"",		ACT_ZOOM_IN, GDK_equal, _CS, 0},
	{"ZOOM_OUT",	ACT_ZOOM_OUT, GDK_minus, _CS, 0},
	{"",		ACT_ZOOM_OUT, GDK_KP_Subtract, _CS, 0},
	{"ZOOM_01",	ACT_ZOOM_01, GDK_KP_1, _CS, 0},
	{"",		ACT_ZOOM_01, GDK_1, _CS, 0},
	{"ZOOM_025",	ACT_ZOOM_025, GDK_KP_2, _CS, 0},
	{"",		ACT_ZOOM_025, GDK_2, _CS, 0},
	{"ZOOM_05",	ACT_ZOOM_05, GDK_KP_3, _CS, 0},
	{"",		ACT_ZOOM_05, GDK_3, _CS, 0},
	{"ZOOM_1",	ACT_ZOOM_1, GDK_KP_4, _CS, 0},
	{"",		ACT_ZOOM_1, GDK_4, _CS, 0},
	{"ZOOM_4",	ACT_ZOOM_4, GDK_KP_5, _CS, 0},
	{"",		ACT_ZOOM_4, GDK_5, _CS, 0},
	{"ZOOM_8",	ACT_ZOOM_8, GDK_KP_6, _CS, 0},
	{"",		ACT_ZOOM_8, GDK_6, _CS, 0},
	{"ZOOM_12",	ACT_ZOOM_12, GDK_KP_7, _CS, 0},
	{"",		ACT_ZOOM_12, GDK_7, _CS, 0},
	{"ZOOM_16",	ACT_ZOOM_16, GDK_KP_8, _CS, 0},
	{"",		ACT_ZOOM_16, GDK_8, _CS, 0},
	{"ZOOM_20",	ACT_ZOOM_20, GDK_KP_9, _CS, 0},
	{"",		ACT_ZOOM_20, GDK_9, _CS, 0},
	{"VIEW",	ACT_VIEW, GDK_Home, 0, 0},
	{"BRCOSA",	ACT_BRCOSA, GDK_Insert, _CS, 0},
	{"PAN",		ACT_PAN, GDK_End, _CS, 0},
	{"CROP",	ACT_CROP, GDK_Delete, _CS, 0},
	{"SWAP_AB",	ACT_SWAP_AB, GDK_x, _CSA, 0},
	{"",		ACT_SWAP_AB, GDK_X, _CSA, 0}, // !!! Naturally, this doesn't work
	{"CMDLINE",	ACT_CMDLINE, GDK_c, _CSA, 0},
	{"",		ACT_CMDLINE, GDK_C, _CSA, 0}, // !!! And this, too
#if GTK_MAJOR_VERSION == 2
	{"PATTERN",	ACT_PATTERN, GDK_F2, _CSA, 0},
	{"BRUSH",	ACT_BRUSH, GDK_F3, _CSA, 0},
#endif
// GTK+1 creates a segfault if you use F2/F3 here - This doesn't matter as only GTK+2 needs it here as in full screen mode GTK+2 does not handle menu keyboard shortcuts
	{"PAINT",	ACT_PAINT, GDK_F4, _CSA, 0},
	{"SELECT",	ACT_SELECT, GDK_F9, _CSA, 0},
	{"SEL_2LEFT",	ACT_SEL_2LEFT, GDK_Left, _CS, _S},
	{"",		ACT_SEL_2LEFT, GDK_KP_Left, _CS, _S},
	{"SEL_2RIGHT",	ACT_SEL_2RIGHT, GDK_Right, _CS, _S},
	{"",		ACT_SEL_2RIGHT, GDK_KP_Right, _CS, _S},
	{"SEL_2DOWN",	ACT_SEL_2DOWN, GDK_Down, _CS, _S},
	{"",		ACT_SEL_2DOWN, GDK_KP_Down, _CS, _S},
	{"SEL_2UP",	ACT_SEL_2UP, GDK_Up, _CS, _S},
	{"",		ACT_SEL_2UP, GDK_KP_Up, _CS, _S},
	{"SEL_LEFT",	ACT_SEL_LEFT, GDK_Left, _CS, 0},
	{"",		ACT_SEL_LEFT, GDK_KP_Left, _CS, 0},
	{"SEL_RIGHT",	ACT_SEL_RIGHT, GDK_Right, _CS, 0},
	{"",		ACT_SEL_RIGHT, GDK_KP_Right, _CS, 0},
	{"SEL_DOWN",	ACT_SEL_DOWN, GDK_Down, _CS, 0},
	{"",		ACT_SEL_DOWN, GDK_KP_Down, _CS, 0},
	{"SEL_UP",	ACT_SEL_UP, GDK_Up, _CS, 0},
	{"",		ACT_SEL_UP, GDK_KP_Up, _CS, 0},
	{"OPAC_01",	ACT_OPAC_01, GDK_KP_1, _C, _C},
	{"",		ACT_OPAC_01, GDK_1, _C, _C},
	{"OPAC_02",	ACT_OPAC_02, GDK_KP_2, _C, _C},
	{"",		ACT_OPAC_02, GDK_2, _C, _C},
	{"OPAC_03",	ACT_OPAC_03, GDK_KP_3, _C, _C},
	{"",		ACT_OPAC_03, GDK_3, _C, _C},
	{"OPAC_04",	ACT_OPAC_04, GDK_KP_4, _C, _C},
	{"",		ACT_OPAC_04, GDK_4, _C, _C},
	{"OPAC_05",	ACT_OPAC_05, GDK_KP_5, _C, _C},
	{"",		ACT_OPAC_05, GDK_5, _C, _C},
	{"OPAC_06",	ACT_OPAC_06, GDK_KP_6, _C, _C},
	{"",		ACT_OPAC_06, GDK_6, _C, _C},
	{"OPAC_07",	ACT_OPAC_07, GDK_KP_7, _C, _C},
	{"",		ACT_OPAC_07, GDK_7, _C, _C},
	{"OPAC_08",	ACT_OPAC_08, GDK_KP_8, _C, _C},
	{"",		ACT_OPAC_08, GDK_8, _C, _C},
	{"OPAC_09",	ACT_OPAC_09, GDK_KP_9, _C, _C},
	{"",		ACT_OPAC_09, GDK_9, _C, _C},
	{"OPAC_1",	ACT_OPAC_1, GDK_KP_0, _C, _C},
	{"",		ACT_OPAC_1, GDK_0, _C, _C},
	{"OPAC_P",	ACT_OPAC_P, GDK_plus, _C, _C},
	{"",		ACT_OPAC_P, GDK_KP_Add, _C, _C},
	{"",		ACT_OPAC_P, GDK_equal, _C, _C},
	{"OPAC_M",	ACT_OPAC_M, GDK_minus, _C, _C},
	{"",		ACT_OPAC_M, GDK_KP_Subtract, _C, _C},
	{"LR_2LEFT",	ACT_LR_2LEFT, GDK_Left, _CS, _CS},
	{"",		ACT_LR_2LEFT, GDK_KP_Left, _CS, _CS},
	{"LR_2RIGHT",	ACT_LR_2RIGHT, GDK_Right, _CS, _CS},
	{"",		ACT_LR_2RIGHT, GDK_KP_Right, _CS, _CS},
	{"LR_2DOWN",	ACT_LR_2DOWN, GDK_Down, _CS, _CS},
	{"",		ACT_LR_2DOWN, GDK_KP_Down, _CS, _CS},
	{"LR_2UP",	ACT_LR_2UP, GDK_Up, _CS, _CS},
	{"",		ACT_LR_2UP, GDK_KP_Up, _CS, _CS},
	{"LR_LEFT",	ACT_LR_LEFT, GDK_Left, _CS, _C},
	{"",		ACT_LR_LEFT, GDK_KP_Left, _CS, _C},
	{"LR_RIGHT",	ACT_LR_RIGHT, GDK_Right, _CS, _C},
	{"",		ACT_LR_RIGHT, GDK_KP_Right, _CS, _C},
	{"LR_DOWN",	ACT_LR_DOWN, GDK_Down, _CS, _C},
	{"",		ACT_LR_DOWN, GDK_KP_Down, _CS, _C},
	{"LR_UP",	ACT_LR_UP, GDK_Up, _CS, _C},
	{"",		ACT_LR_UP, GDK_KP_Up, _CS, _C},
	{"ESC",		ACT_ESC, GDK_Escape, _CS, 0},
	{"SCALE",	ACT_SCALE, GDK_Page_Up, _CS, 0},
	{"SIZE",	ACT_SIZE, GDK_Page_Down, _CS, 0},
	{"COMMIT",	ACT_COMMIT, GDK_Return, 0, 0},
	{"",		ACT_COMMIT, GDK_KP_Enter, 0, 0},
	{"RCLICK",	ACT_RCLICK, GDK_BackSpace, 0, 0},
	{"ARROW",	ACT_ARROW, GDK_a, _C, 0},
	{"",		ACT_ARROW, GDK_A, _C, 0},
	{"ARROW3",	ACT_ARROW3, GDK_s, _C, 0},
	{"",		ACT_ARROW3, GDK_S, _C, 0},
	{"A_PREV",	ACT_A_PREV, GDK_bracketleft, _C, 0},
	{"A_NEXT",	ACT_A_NEXT, GDK_bracketright, _C, 0},
	{"B_PREV",	ACT_B_PREV, GDK_braceleft, _C, 0},
	{"B_NEXT",	ACT_B_NEXT, GDK_braceright, _C, 0},
	{NULL,		0, 0, 0, 0}
};

int wtf_pressed(GdkEventKey *event, key_action *keys)
{
	while (keys->action)
	{
		if ((event->keyval == keys->key) &&
			((event->state & keys->kmask) == keys->kflags))
			return (keys->action);
		keys++;
	}
	return (0);
}

gint handle_keypress( GtkWidget *widget, GdkEventKey *event )
{
	int change, action;

	action = wtf_pressed(event, main_keys);
	if (!action) return (FALSE);

	if (check_zoom_keys(action)) return TRUE;		// Check HOME/zoom keys

	if (marq_status > MARQUEE_NONE)
	{
		if (check_arrows(action) == 1)
		{
			update_sel_bar();
			update_menus();
			return TRUE;
		}
	}
	change = inifile_get_gint32("pixelNudge", 8);

/* !!! Make a pref or something later !!! */
#if 0
	/* The colour-change override */
	switch (action)
	{
	case ACT_SEL_LEFT: action = ACT_B_PREV; break;
	case ACT_SEL_RIGHT: action = ACT_B_NEXT; break;
	case ACT_SEL_DOWN: action = ACT_A_NEXT; break;
	case ACT_SEL_UP: action = ACT_A_PREV; break;
	}
#endif

	switch (action)
	{
	case ACT_SEL_LEFT: change = 1;
	case ACT_SEL_2LEFT:
		move_mouse(-change, 0, 0);
		return (TRUE);
	case ACT_SEL_RIGHT: change = 1;
	case ACT_SEL_2RIGHT:
		move_mouse(change, 0, 0);
		return (TRUE);
	case ACT_SEL_DOWN: change = 1;
	case ACT_SEL_2DOWN:
		move_mouse(0, change, 0);
		return (TRUE);
	case ACT_SEL_UP: change = 1;
	case ACT_SEL_2UP:
		move_mouse(0, -change, 0);
		return (TRUE);
	// Opacity keys, i.e. CTRL + keypad
	case ACT_OPAC_01:
	case ACT_OPAC_02:
	case ACT_OPAC_03:
	case ACT_OPAC_04:
	case ACT_OPAC_05:
	case ACT_OPAC_06:
	case ACT_OPAC_07:
	case ACT_OPAC_08:
	case ACT_OPAC_09:
	case ACT_OPAC_1:
		pressed_opacity((255 * (action - ACT_OPAC_01 + 1)) / 10);
		return (TRUE);
	case ACT_OPAC_P:
		pressed_opacity(tool_opacity + 1);
		return (TRUE);
	case ACT_OPAC_M:
		pressed_opacity(tool_opacity - 1);
		return (TRUE);
	case ACT_LR_LEFT: change = 1;
	case ACT_LR_2LEFT:
		if (layer_selected)
			move_layer_relative(layer_selected, -change, 0);
		return (TRUE);
	case ACT_LR_RIGHT: change = 1;
	case ACT_LR_2RIGHT:
		if (layer_selected)
			move_layer_relative(layer_selected, change, 0);
		return (TRUE);
	case ACT_LR_DOWN: change = 1;
	case ACT_LR_2DOWN:
		if (layer_selected)
			move_layer_relative(layer_selected, 0, change);
		return (TRUE);
	case ACT_LR_UP: change = 1;
	case ACT_LR_2UP:
		if (layer_selected)
			move_layer_relative(layer_selected, 0, -change);
		return (TRUE);
	case ACT_ESC:
		if ((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON))
			pressed_select_none(NULL, NULL);
		else if (tool_type == TOOL_LINE) stop_line();
		else if ((tool_type == TOOL_GRADIENT) && (grad_status != GRAD_NONE))
		{
			repaint_grad(0);
			grad_status = GRAD_NONE;
		}
		return TRUE;
	case ACT_SCALE:
		pressed_scale(NULL, NULL); return TRUE;
	case ACT_SIZE:
		pressed_size(NULL, NULL); return TRUE;
	case ACT_A_PREV:
		if (mem_channel == CHN_IMAGE)
		{
			if (mem_col_A) mem_col_A--;
		}
		else if (channel_col_A[mem_channel])
			channel_col_A[mem_channel]--;
		break;
	case ACT_A_NEXT:
		if (mem_channel == CHN_IMAGE)
		{
			if (mem_col_A < mem_cols - 1) mem_col_A++;
		}
		else if (channel_col_A[mem_channel] < 255)
			channel_col_A[mem_channel]++;
		break;
	case ACT_B_PREV:
		if (mem_channel == CHN_IMAGE)
		{
			if (mem_col_B) mem_col_B--;
		}
		else if (channel_col_B[mem_channel])
			channel_col_B[mem_channel]--;
		break;
	case ACT_B_NEXT:
		if (mem_channel == CHN_IMAGE)
		{
			if (mem_col_B < mem_cols - 1) mem_col_B++;
		}
		else if (channel_col_B[mem_channel] < 255)
			channel_col_B[mem_channel]++;
		break;
	case ACT_COMMIT:
		if (marq_status >= MARQUEE_PASTE)
		{
			commit_paste(TRUE);
			pen_down = 0;	// Ensure each press of enter is a new undo level
		}
		else move_mouse(0, 0, 1);
		return TRUE;
	case ACT_RCLICK:
		if (marq_status < MARQUEE_PASTE) move_mouse(0, 0, 3);
		return TRUE;
	case ACT_ARROW:
	case ACT_ARROW3:
		if ((tool_type == TOOL_LINE) && (line_status > LINE_NONE) &&
			((line_x1 != line_x2) || (line_y1 != line_y2)))
		{
			int i, xa1, xa2, ya1, ya2, minx, maxx, miny, maxy, w, h;
			double uvx, uvy;	// Line length & unit vector lengths
			int oldmode = mem_undo_opacity;


				// Calculate 2 coords for arrow corners
			uvy = sqrt((line_x1 - line_x2) * (line_x1 - line_x2) +
				(line_y1 - line_y2) * (line_y1 - line_y2));
			uvx = (line_x2 - line_x1) / uvy;
			uvy = (line_y2 - line_y1) / uvy;

			xa1 = rint(line_x1 + tool_flow * (uvx - uvy * 0.5));
			xa2 = rint(line_x1 + tool_flow * (uvx + uvy * 0.5));
			ya1 = rint(line_y1 + tool_flow * (uvy + uvx * 0.5));
			ya2 = rint(line_y1 + tool_flow * (uvy - uvx * 0.5));

			pen_down = 0;
			tool_action(GDK_NOTHING, line_x1, line_y1, 1, 0);
			line_status = LINE_LINE;
			update_menus();

				// Draw arrow lines & circles
			mem_undo_opacity = TRUE;
			f_circle(xa1, ya1, tool_size);
			f_circle(xa2, ya2, tool_size);
			tline(xa1, ya1, line_x1, line_y1, tool_size);
			tline(xa2, ya2, line_x1, line_y1, tool_size);

			if (action == ACT_ARROW3)
			{
				// Draw 3rd line and fill arrowhead
				tline(xa1, ya1, xa2, ya2, tool_size );
				poly_points = 0;
				poly_add(line_x1, line_y1);
				poly_add(xa1, ya1);
				poly_add(xa2, ya2);
				poly_init();
				poly_paint();
				poly_points = 0;
			}
			mem_undo_opacity = oldmode;

				// Update screen areas
			minx = xa1 < xa2 ? xa1 : xa2;
			if (minx > line_x1) minx = line_x1;
			maxx = xa1 > xa2 ? xa1 : xa2;
			if (maxx < line_x1) maxx = line_x1;

			miny = ya1 < ya2 ? ya1 : ya2;
			if (miny > line_y1) miny = line_y1;
			maxy = ya1 > ya2 ? ya1 : ya2;
			if (maxy < line_y1) maxy = line_y1;

			i = (tool_size + 1) >> 1;
			minx -= i; miny -= i; maxx += i; maxy += i;

			w = maxx - minx + 1;
			h = maxy - miny + 1;

			main_update_area(minx, miny, w, h);
			vw_update_area(minx, miny, w, h);
		}
		return (TRUE);
	default:
		return (FALSE);
	}
	/* Finalize colour-change */
	if (mem_channel == CHN_IMAGE)
	{
		mem_col_A24 = mem_pal[mem_col_A];
		mem_col_B24 = mem_pal[mem_col_B];
		init_pal();
	}
	else pressed_opacity(channel_col_A[mem_channel]);
	return TRUE;
}

gint destroy_signal( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	quit_all( NULL, NULL );

	return FALSE;
}

int check_for_changes()			// 1=STOP, 2=IGNORE, 10=ESCAPE, -10=NOT CHECKED
{
	int i = -10;
	char *warning = _("This canvas/palette contains changes that have not been saved.  Do you really want to lose these changes?");

	if ( mem_changed == 1 )
		i = alert_box( _("Warning"), warning, _("Cancel Operation"), _("Lose Changes"), NULL );

	return i;
}

gint delete_event( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	gint x,y,width,height;
	int i = 2, j = 0;

	if ( !GTK_WIDGET_SENSITIVE(main_window) ) return TRUE;
		// Stop user prematurely exiting while drag 'n' drop loading

	if ( layers_total == 0 )
		j = check_for_changes();
	else
		j = check_layers_for_changes();

	if ( j == -10 )
	{
		if ( inifile_get_gboolean( "exitToggle", FALSE ) )
			i = alert_box( VERSION, _("Do you really want to quit?"), _("NO"), _("YES"), NULL );
	}
	else i = j;

	if ( i==2 )
	{
		gdk_window_get_size( main_window->window, &width, &height );
		gdk_window_get_root_origin( main_window->window, &x, &y );
	
		inifile_set_gint32("window_x", x );
		inifile_set_gint32("window_y", y );
		inifile_set_gint32("window_w", width );
		inifile_set_gint32("window_h", height );

		if (cline_window != NULL) delete_cline( NULL, NULL, NULL );
		if (layers_window != NULL) delete_layers_window( NULL, NULL, NULL );
			// Get rid of extra windows + remember positions

		toolbar_exit();			// Remember the toolbar settings

		gtk_main_quit ();
		return FALSE;
	}
	else return TRUE;
}

#if GTK_MAJOR_VERSION == 2
gint canvas_scroll_gtk2( GtkWidget *widget, GdkEventScroll *event )
{
	if (inifile_get_gboolean( "scrollwheelZOOM", TRUE ))
	{
		scroll_wheel(event->x / can_zoom, event->y / can_zoom,
			event->direction == GDK_SCROLL_DOWN ? -1 : 1);
		return TRUE;
	}
	if (event->state & _C) /* Convert up-down into left-right */
	{
		if (event->direction == GDK_SCROLL_UP)
			event->direction = GDK_SCROLL_LEFT;
		else if (event->direction == GDK_SCROLL_DOWN)
			event->direction = GDK_SCROLL_RIGHT;
	}
	/* Normal GTK+2 scrollwheel behaviour */
	return FALSE;
}
#endif


int grad_tool(int event, int x, int y, guint state, guint button)
{
	int i, j, *xx, *yy;

	if (tool_type != TOOL_GRADIENT) return (FALSE);

	/* Left click sets points and picks them up again */
	if ((event == GDK_BUTTON_PRESS) && (button == 1))
	{
		/* Start anew */
		if (grad_status == GRAD_NONE)
		{
			grad_x1 = grad_x2 = x;
			grad_y1 = grad_y2 = y;
			grad_status = GRAD_END;
			repaint_grad(1);
		}
		/* Place starting point */
		else if (grad_status == GRAD_START)
		{
			grad_x1 = x;
			grad_y1 = y;
			grad_status = GRAD_DONE;
		}
		/* Place end point */
		else if (grad_status == GRAD_END)
		{
			grad_x2 = x;
			grad_y2 = y;
			grad_status = GRAD_DONE;
		}
		/* Pick up nearest end */
		else if (grad_status == GRAD_DONE)
		{
			repaint_grad(0);
			i = (x - grad_x1) * (x - grad_x1) +
				(y - grad_y1) * (y - grad_y1);
			j = (x - grad_x2) * (x - grad_x2) +
				(y - grad_y2) * (y - grad_y2);
			if (i < j)
			{
				grad_x1 = x;
				grad_y1 = y;
				grad_status = GRAD_START;
			}
			else				
			{
				grad_x2 = x;
				grad_y2 = y;
				grad_status = GRAD_END;
			}
			repaint_grad(1);
		}
	}

	/* Everything but left click is irrelevant when no gradient */
	else if (grad_status == GRAD_NONE);

	/* Right click deletes the gradient */
	else if (event == GDK_BUTTON_PRESS) /* button != 1 */
	{
		repaint_grad(0);
		grad_status = GRAD_NONE;
	}

	/* Motion is irrelevant with gradient in place */
	else if (grad_status == GRAD_DONE);

	/* Motion drags points around */
	else if (event == GDK_MOTION_NOTIFY)
	{
		if (grad_status == GRAD_START) xx = &grad_x1 , yy = &grad_y1;
		else xx = &grad_x2 , yy = &grad_y2;
		if ((*xx != x) || (*yy != y))
		{
			repaint_grad(0);
			*xx = x; *yy = y;
			repaint_grad(1);
		}
	}

	/* Leave hides the dragged line */
	else if (event == GDK_LEAVE_NOTIFY) repaint_grad(0);

	return (TRUE);
}

static void mouse_event(int event, int x0, int y0, guint state, guint button,
	gdouble pressure, int mflag)
{	// Mouse event from button/motion on the canvas
	GdkCursor *temp_cursor = NULL;
	GdkCursorType pointers[] = {GDK_TOP_LEFT_CORNER, GDK_TOP_RIGHT_CORNER,
		GDK_BOTTOM_LEFT_CORNER, GDK_BOTTOM_RIGHT_CORNER};
	unsigned char pixel;
	png_color pixel24;
	int new_cursor;
	int x, y, ox, oy, i, tox = tool_ox, toy = tool_oy;


	x = x0 < 0 ? 0 : x0 >= mem_width ? mem_width - 1 : x0;
	y = y0 < 0 ? 0 : y0 >= mem_height ? mem_height - 1 : y0;

	/* ****** Release-event-specific code ****** */
	if (event == GDK_BUTTON_RELEASE)
	{
		tint_mode[2] = 0;
		pen_down = 0;
		if ( col_reverse )
		{
			col_reverse = FALSE;
			mem_swap_cols();
		}

		if (grad_tool(event, x, y, state, button)) return;

		if ((tool_type == TOOL_LINE) && (button == 1) &&
			(line_status == LINE_START))
		{
			line_status = LINE_LINE;
			repaint_line(1);
		}

		if (((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON)) &&
			(button == 1))
		{
			if (marq_status == MARQUEE_SELECTING)
				marq_status = MARQUEE_DONE;
			if (marq_status == MARQUEE_PASTE_DRAG)
				marq_status = MARQUEE_PASTE;
			cursor_corner = -1;
		}

		// Finish off dragged polygon selection
		if ((tool_type == TOOL_POLYGON) && (poly_status == POLY_DRAGGING))
			tool_action(event, x, y, button, pressure);

		update_menus();

		return;
	}

	if ((state & _CS) == _C)	// Set colour A/B
	{
		if (mem_channel != CHN_IMAGE)
		{
			pixel = get_pixel( x, y );
			if ((button == 1) && (channel_col_A[mem_channel] != pixel))
				pressed_opacity(pixel);
			if ((button == 3) && (channel_col_B[mem_channel] != pixel))
			{
				channel_col_B[mem_channel] = pixel;
				/* To update displayed value */
				pressed_opacity(channel_col_A[mem_channel]);
			}
		}
		else if (mem_img_bpp == 1)
		{
			pixel = get_pixel( x, y );
			if (button == 1 && mem_col_A != pixel)
			{
				mem_col_A = pixel;
				mem_col_A24 = mem_pal[pixel];
				repaint_top_swatch();
				update_cols();
			}
			if (button == 3 && mem_col_B != pixel)
			{
				mem_col_B = pixel;
				mem_col_B24 = mem_pal[pixel];
				repaint_top_swatch();
				update_cols();
			}
		}
		else
		{
			pixel24 = get_pixel24( x, y );
			if (button == 1 && png_cmp( mem_col_A24, pixel24 ) )
			{
				mem_col_A24 = pixel24;
				repaint_top_swatch();
				update_cols();
			}
			if (button == 3 && png_cmp( mem_col_B24, pixel24 ) )
			{
				mem_col_B24 = pixel24;
				repaint_top_swatch();
				update_cols();
			}
		}
	}
	else
	{
		if (!mflag)
		{
			if ((tool_fixy < 0) && ((state & _CS) == _CS))
			{
				tool_fixx = -1;
				tool_fixy = y;
			}
			if ((tool_fixx < 0) && ((state & _CS) == _S))
				tool_fixx = x;
			if (!(state & _S)) tool_fixx = tool_fixy = -1;
			if (!(state & _C)) tool_fixy = -1;
		}

		if ((button == 3) && (state & _S)) set_zoom_centre(x, y);

		else if (grad_tool(event, x, y, state, button)) return;

		else if ( button == 1 || button >= 3 )
			tool_action(event, x, y, button, pressure);
		if ((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON))
			update_sel_bar();
	}
	if ( button == 2 ) set_zoom_centre( x, y );

	/* ****** Now to mouse-move-specific part ****** */

	if (event != GDK_MOTION_NOTIFY) return;

	/* No use when moving cursor by keyboard */
	if (mflag) tool_fixx = tool_fixy = -1;

	if (tool_fixx > 0) x = x0 = tool_fixx;
	if (tool_fixy > 0) y = y0 = tool_fixy;

	if ( tool_type == TOOL_CLONE )
	{
		tool_ox = x;
		tool_oy = y;
	}

	ox = x; oy = y;

	if ( poly_status == POLY_SELECTING && button == 0 )
	{
		stretch_poly_line(ox, oy);
	}

	if ((x0 < 0) || (x0 >= mem_width)) x = -1;
	if ((y0 < 0) || (y0 >= mem_height)) y = -1;

	if ( tool_type == TOOL_SELECT || tool_type == TOOL_POLYGON )
	{
		if ( marq_status == MARQUEE_DONE )
		{
			if ( inifile_get_gboolean( "cursorToggle", TRUE ) )
			{
				i = close_to(ox, oy);
				if ( i!=cursor_corner ) // Stops excessive CPU/flickering
				{
					 cursor_corner = i;
					 temp_cursor = gdk_cursor_new(pointers[i]);
					 gdk_window_set_cursor(drawing_canvas->window, temp_cursor);
					 gdk_cursor_destroy(temp_cursor);
				}
			}
			else set_cursor();
		}
		if ( marq_status >= MARQUEE_PASTE )
		{
			new_cursor = 0;		// Cursor = normal
			if ( ox>=marq_x1 && ox<=marq_x2 && oy>=marq_y1 && oy<=marq_y2 )
				new_cursor = 1;		// Cursor = 4 way arrow

			if ( new_cursor != cursor_corner ) // Stops flickering on slow hardware
			{
				if ( !inifile_get_gboolean( "cursorToggle", TRUE ) ||
					new_cursor == 0 )
					set_cursor();
				else
					gdk_window_set_cursor( drawing_canvas->window, move_cursor );
				cursor_corner = new_cursor;
			}
		}
	}
	update_xy_bar(x, y);

///	TOOL PERIMETER BOX UPDATES

	if ( perim_status > 0 ) clear_perim();	// Remove old perimeter box

	if ((tool_type == TOOL_CLONE) && (button == 0) && (state & _C))
	{
		clone_x += (tox-ox);
		clone_y += (toy-oy);
	}

	if (tool_size * can_zoom > 4)
	{
		perim_x = ox - (tool_size >> 1);
		perim_y = oy - (tool_size >> 1);
		perim_s = tool_size;
		repaint_perim();			// Repaint 4 sides
	}

///	LINE UPDATES

	if (grad_tool(event, ox, oy, state, button)) return;

	if ( tool_type == TOOL_LINE && line_status != LINE_NONE )
	{
		if ( line_x1 != ox || line_y1 != oy )
		{
			repaint_line(0);
			line_x1 = ox;
			line_y1 = oy;
			repaint_line(1);
		}
	}
}

static gint main_scroll_changed()
{
	vw_focus_view();	// The user has adjusted a scrollbar so we may need to change view window

	return TRUE;
}

static gint canvas_button( GtkWidget *widget, GdkEventButton *event )
{
	int x, y, zoom = 1, scale = 1, pflag = event->type != GDK_BUTTON_RELEASE;
	gdouble pressure = 1.0;

	if (pflag) /* For button press events only */
	{
		if (!mem_img[CHN_IMAGE]) return (TRUE);

		if (tablet_working)
#if GTK_MAJOR_VERSION == 1
			pressure = event->pressure;
#endif
#if GTK_MAJOR_VERSION == 2
			gdk_event_get_axis((GdkEvent *)event, GDK_AXIS_PRESSURE,
				&pressure);
#endif
	}

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	x = ((int)(event->x - margin_main_x) * zoom) / scale;
	y = ((int)(event->y - margin_main_y) * zoom) / scale;
	mouse_event(event->type, x, y, event->state, event->button,
		pressure, unreal_move & 1);

	return (pflag);
}

// Mouse enters the canvas
static gint canvas_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
	return TRUE;
}

static gint canvas_left(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
	/* Only do this if we have an image */
	if (!mem_img[CHN_IMAGE]) return (FALSE);
	if ( status_on[STATUS_CURSORXY] )
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_CURSORXY]), "" );
	if ( status_on[STATUS_PIXELRGB] )
		gtk_label_set_text( GTK_LABEL(label_bar[STATUS_PIXELRGB]), "" );
	if (perim_status > 0) clear_perim();

	if (grad_tool(GDK_LEAVE_NOTIFY, 0, 0, 0, 0)) return (FALSE);

	if (((tool_type == TOOL_LINE) && (line_status != LINE_NONE)) ||
		((tool_type == TOOL_POLYGON) && (poly_status == POLY_SELECTING)))
		repaint_line(0);

	return (FALSE);
}

int async_bk = FALSE;

#define GREY_W 153
#define GREY_B 102
void render_background(unsigned char *rgb, int x0, int y0, int wid, int hgt, int fwid)
{
	int i, j, k, scale, dx, dy, step, ii, jj, ii0, px, py;
	int xwid = 0, xhgt = 0, wid3 = wid * 3;
	static unsigned char greyz[2] = {GREY_W, GREY_B};

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (async_bk) step = 8;
	else if (can_zoom < 1.0) step = 6;
	else
	{
		scale = rint(can_zoom);
		step = scale < 4 ? 6 : scale == 4 ? 8 : scale;
	}
	dx = x0 % step;
	dy = y0 % step;
	
	py = (x0 / step + y0 / step) & 1;
	if (hgt + dy > step)
	{
		jj = step - dy;
		xhgt = (hgt + dy) % step;
		if (!xhgt) xhgt = step;
		hgt -= xhgt;
		xhgt -= step;
	}
	else jj = hgt--;
	if (wid + dx > step)
	{
		ii0 = step - dx;
		xwid = (wid + dx) % step;
		if (!xwid) xwid = step;
		wid -= xwid;
		xwid -= step;
	}
	else ii0 = wid--;
	fwid *= 3;

	for (j = 0; ; jj += step)
	{
		if (j >= hgt)
		{
			if (j > hgt) break;
			jj += xhgt;
		}
		px = py;
		ii = ii0;
		for (i = 0; ; ii += step)
		{
			if (i >= wid)
			{
				if (i > wid) break;
				ii += xwid;
			}
			k = (ii - i) * 3;
			memset(rgb, greyz[px], k);
			rgb += k;
			px ^= 1;
			i = ii;
		}
		rgb += fwid - wid3;
		for(j++; j < jj; j++)
		{
			memcpy(rgb, rgb - fwid, wid3);
			rgb += fwid;
		}
		py ^= 1;
	}
}

/* This is for a faster way to pass parameters into render_row() */
typedef struct {
	int dx;
	int width;
	int xwid;
	int zoom;
	int scale;
	int mw;
	int opac;
	int xpm;
	int bpp;
	png_color *pal;
} renderstate;

static renderstate rr;

void setup_row(int x0, int width, double czoom, int mw, int xpm, int opac,
	int bpp, png_color *pal)
{
	/* Horizontal zoom */
	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (czoom <= 1.0)
	{
		rr.zoom = rint(1.0 / czoom);
		rr.scale = 1;
		x0 = 0;
	}
	else
	{
		rr.zoom = 1;
		rr.scale = rint(czoom);
		x0 %= rr.scale;
	}
	if (width + x0 > rr.scale)
	{
		rr.dx = rr.scale - x0;
		x0 = (width + x0) % rr.scale;
		if (!x0) x0 = rr.scale;
		width -= x0;
		rr.xwid = x0 - rr.scale;
	}
	else
	{
		rr.dx = width--;
		rr.xwid = 0;
	}
	rr.width = width;
	rr.mw = mw;

	if ((xpm > -1) && (bpp == 3)) xpm = PNG_2_INT(pal[xpm]);
	rr.xpm = xpm;
	rr.opac = opac;

	rr.bpp = bpp;
	rr.pal = pal;
}

void render_row(unsigned char *rgb, chanlist base_img, int x, int y,
	chanlist xtra_img)
{
	int alpha_blend = !overlay_alpha;
	unsigned char *src = NULL, *dest, *alpha = NULL, px, beta = 255;
	int i, j, k, ii, ds = rr.zoom * 3, da = 0;
	int w_bpp = rr.bpp, w_xpm = rr.xpm;

	if (xtra_img)
	{
		src = xtra_img[CHN_IMAGE];
		alpha = xtra_img[CHN_ALPHA];
	}
	if (channel_dis[CHN_ALPHA]) alpha = &beta; /* Ignore alpha if disabled */
	if (!src) src = base_img[CHN_IMAGE] + (rr.mw * y + x) * rr.bpp;
	if (!alpha) alpha = base_img[CHN_ALPHA] ? base_img[CHN_ALPHA] +
		rr.mw * y + x : &beta;
	if (alpha != &beta) da = rr.zoom;
	dest = rgb;
	ii = rr.dx;

	if (hide_image) /* Substitute non-transparent "image overlay" colour */
	{
		w_bpp = 3;
		w_xpm = -1;
		ds = 0;
		src = channel_rgb[CHN_IMAGE];
	}
	if (!da && (w_xpm < 0) && (rr.opac == 255)) alpha_blend = FALSE;

	/* Indexed fully opaque */
	if ((w_bpp == 1) && !alpha_blend)
	{
		for (i = 0; ; ii += rr.scale)
		{
			if (i >= rr.width)
			{
				if (i > rr.width) break;
				ii += rr.xwid;
			}
			px = *src;
			src += rr.zoom;
			for(; i < ii; i++)
			{
				dest[0] = rr.pal[px].red;
				dest[1] = rr.pal[px].green;
				dest[2] = rr.pal[px].blue;
				dest += 3;
			}
		}
	}

	/* Indexed transparent */
	else if (w_bpp == 1)
	{
		for (i = 0; ; ii += rr.scale , alpha += da)
		{
			if (i >= rr.width)
			{
				if (i > rr.width) break;
				ii += rr.xwid;
			}
			px = *src;
			src += rr.zoom;
			if (!*alpha || (px == w_xpm))
			{
				dest += (ii - i) * 3;
				i = ii;
				continue;
			}
rr2_as:
			if (rr.opac == 255)
			{
				dest[0] = rr.pal[px].red;
				dest[1] = rr.pal[px].green;
				dest[2] = rr.pal[px].blue;
			}
			else
			{
				j = 255 * dest[0] + rr.opac * (rr.pal[px].red - dest[0]);
				dest[0] = (j + (j >> 8) + 1) >> 8;
				j = 255 * dest[1] + rr.opac * (rr.pal[px].green - dest[1]);
				dest[1] = (j + (j >> 8) + 1) >> 8;
				j = 255 * dest[2] + rr.opac * (rr.pal[px].blue - dest[2]);
				dest[2] = (j + (j >> 8) + 1) >> 8;
			}
rr2_s:
			dest += 3;
			if (++i >= ii) continue;
			if (async_bk) goto rr2_as;
			dest[0] = *(dest - 3);
			dest[1] = *(dest - 2);
			dest[2] = *(dest - 1);
			goto rr2_s;
		}
	}

	/* RGB fully opaque */
	else if (!alpha_blend)
	{
		for (i = 0; ; ii += rr.scale , src += ds)
		{
			if (i >= rr.width)
			{
				if (i > rr.width) break;
				ii += rr.xwid;
			}
			for(; i < ii; i++)
			{
				dest[0] = src[0];
				dest[1] = src[1];
				dest[2] = src[2];
				dest += 3;
			}
		}
	}

	/* RGB transparent */
	else
	{
		for (i = 0; ; ii += rr.scale , src += ds , alpha += da)
		{
			if (i >= rr.width)
			{
				if (i > rr.width) break;
				ii += rr.xwid;
			}
			if (!*alpha || (MEM_2_INT(src, 0) == w_xpm))
			{
				dest += (ii - i) * 3;
				i = ii;
				continue;
			}
			k = rr.opac * alpha[0];
			k = (k + (k >> 8) + 1) >> 8;
rr4_as:
			if (k == 255)
			{
				dest[0] = src[0];
				dest[1] = src[1];
				dest[2] = src[2];
			}
			else
			{
				j = 255 * dest[0] + k * (src[0] - dest[0]);
				dest[0] = (j + (j >> 8) + 1) >> 8;
				j = 255 * dest[1] + k * (src[1] - dest[1]);
				dest[1] = (j + (j >> 8) + 1) >> 8;
				j = 255 * dest[2] + k * (src[2] - dest[2]);
				dest[2] = (j + (j >> 8) + 1) >> 8;
			}
rr4_s:
			dest += 3;
			if (++i >= ii) continue;
			if (async_bk) goto rr4_as;
			dest[0] = *(dest - 3);
			dest[1] = *(dest - 2);
			dest[2] = *(dest - 1);
			goto rr4_s;
		}
	}
}

void overlay_row(unsigned char *rgb, chanlist base_img, int x, int y,
	chanlist xtra_img)
{
	unsigned char *alpha, *sel, *mask, *dest;
	int i, j, k, ii, dw, opA, opS, opM, t0, t1, t2, t3;

	if (xtra_img)
	{
		alpha = xtra_img[CHN_ALPHA];
		sel = xtra_img[CHN_SEL];
		mask = xtra_img[CHN_MASK];
	}
	else alpha = sel = mask = NULL;
	j = rr.mw * y + x;
	if (!alpha && base_img[CHN_ALPHA]) alpha = base_img[CHN_ALPHA] + j;
	if (!sel && base_img[CHN_SEL]) sel = base_img[CHN_SEL] + j;
	if (!mask && base_img[CHN_MASK]) mask = base_img[CHN_MASK] + j;

	/* Prepare channel weights (256-based) */
	k = hide_image ? 256 : 256 - channel_opacity[CHN_IMAGE] -
		(channel_opacity[CHN_IMAGE] >> 7);
	opA = alpha && overlay_alpha && !channel_dis[CHN_ALPHA] ?
		channel_opacity[CHN_ALPHA] : 0;
	opS = sel && !channel_dis[CHN_SEL] ? channel_opacity[CHN_SEL] : 0;
	opM = mask && !channel_dis[CHN_MASK] ? channel_opacity[CHN_MASK] : 0;

	/* Nothing to do - don't waste time then */
	j = opA + opS + opM;
	if (!k || !j) return;

	opA = (k * opA) / j;
	opS = (k * opS) / j;
	opM = (k * opM) / j;
	if (!(opA + opS + opM)) return;

	dest = rgb;
	ii = rr.dx;
	for (i = dw = 0; ; ii += rr.scale , dw += rr.zoom)
	{
		if (i >= rr.width)
		{
			if (i > rr.width) break;
			ii += rr.xwid;
		}
		t0 = t1 = t2 = t3 = 0;
		if (opA)
		{
			j = opA * (alpha[dw] ^ channel_inv[CHN_ALPHA]);
			t0 += j;
			t1 += j * channel_rgb[CHN_ALPHA][0];
			t2 += j * channel_rgb[CHN_ALPHA][1];
			t3 += j * channel_rgb[CHN_ALPHA][2];
		}
		if (opS)
		{
			j = opS * (sel[dw] ^ channel_inv[CHN_SEL]);
			t0 += j;
			t1 += j * channel_rgb[CHN_SEL][0];
			t2 += j * channel_rgb[CHN_SEL][1];
			t3 += j * channel_rgb[CHN_SEL][2];
		}
		if (opM)
		{
			j = opM * (mask[dw] ^ channel_inv[CHN_MASK]);
			t0 += j;
			t1 += j * channel_rgb[CHN_MASK][0];
			t2 += j * channel_rgb[CHN_MASK][1];
			t3 += j * channel_rgb[CHN_MASK][2];
		}
		j = (256 * 255) - t0;
or_as:
		k = t1 + j * dest[0];
		dest[0] = (k + (k >> 8) + 0x100) >> 16;
		k = t2 + j * dest[1];
		dest[1] = (k + (k >> 8) + 0x100) >> 16;
		k = t3 + j * dest[2];
		dest[2] = (k + (k >> 8) + 0x100) >> 16;
or_s:
		dest += 3;
		if (++i >= ii) continue;
		if (async_bk) goto or_as;
		dest[0] = *(dest - 3);
		dest[1] = *(dest - 2);
		dest[2] = *(dest - 1);
		goto or_s;
	}
}

/* Specialized renderer for irregular overlays */
void overlay_preview(unsigned char *rgb, unsigned char *map, int col, int opacity)
{
	unsigned char *dest, crgb[3] = {INT_2_R(col), INT_2_G(col), INT_2_B(col)};
	int i, j, k, ii, dw;

	dest = rgb;
	ii = rr.dx;
	for (i = dw = 0; ; ii += rr.scale , dw += rr.zoom)
	{
		if (i >= rr.width)
		{
			if (i > rr.width) break;
			ii += rr.xwid;
		}
		k = opacity * map[dw];
		k = (k + (k >> 8) + 1) >> 8;
op_as:
		j = 255 * dest[0] + k * (crgb[0] - dest[0]);
		dest[0] = (j + (j >> 8) + 1) >> 8;
		j = 255 * dest[1] + k * (crgb[1] - dest[1]);
		dest[1] = (j + (j >> 8) + 1) >> 8;
		j = 255 * dest[2] + k * (crgb[2] - dest[2]);
		dest[2] = (j + (j >> 8) + 1) >> 8;
op_s:
		dest += 3;
		if (++i >= ii) continue;
		if (async_bk) goto op_as;
		dest[0] = *(dest - 3);
		dest[1] = *(dest - 2);
		dest[2] = *(dest - 1);
		goto op_s;
	}
}

void repaint_paste( int px1, int py1, int px2, int py2 )
{
	chanlist tlist;
	unsigned char *rgb, *tmp, *pix, *mask, *alpha, *mask0;
	unsigned char *clip_alpha, *clip_image, *t_alpha = NULL;
	int i, j, l, pw, ph, pw3, lop = 255, lx = 0, ly = 0, bpp = MEM_BPP;
	int zoom, scale, pww, j0, jj, dx, di, dc, xpm = mem_xpm_trans, opac;


	if ((px1 > px2) || (py1 > py2)) return;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	zoom = scale = 1;
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	/* Check bounds */
	i = (marq_x1 * scale + zoom - 1) / zoom;
	j = (marq_y1 * scale + zoom - 1) / zoom;
	if (px1 < i) px1 = i;
	if (py1 < j) py1 = j;
	i = (marq_x2 * scale) / zoom + scale - 1;
	j = (marq_y2 * scale) / zoom + scale - 1;
	if (px2 > i) px2 = i;
	if (py2 > j) py2 = j;

	pw = px2 - px1 + 1; ph = py2 - py1 + 1;
	if ((pw <= 0) || (ph <= 0)) return;

	rgb = malloc(i = pw * ph * 3);
	if (!rgb) return;
	memset(rgb, mem_background, i);

	/* Horizontal zoom */
	if (scale == 1)
	{
		dx = px1 * zoom;
		l = (pw - 1) * zoom + 1;
		pww = pw;
	}
	else
	{
		dx = px1 / scale;
		pww = l = px2 / scale - dx + 1;
	}

	i = l * (bpp + 2);
	pix = malloc(i);
	if (!pix)
	{
		free(rgb);
		return;
	}
	alpha = pix + l * bpp;
	mask = alpha + l;

	memset(tlist, 0, sizeof(chanlist));
	tlist[mem_channel] = pix;
	clip_image = mem_clipboard;
	clip_alpha = NULL;
	if ((mem_channel == CHN_IMAGE) && mem_img[CHN_ALPHA] && !channel_dis[CHN_ALPHA])
	{
		clip_alpha = mem_clip_alpha;
		if (!clip_alpha && RGBA_mode)
		{
			t_alpha = malloc(l);
			if (!t_alpha)
			{
				free(pix);
				free(rgb);
				return;
			}
			memset(t_alpha, channel_col_A[CHN_ALPHA], l);
		}
	}
	if (mem_channel == CHN_ALPHA)
	{
		clip_image = NULL;
		clip_alpha = mem_clipboard;
	}
	if (clip_alpha || t_alpha) tlist[CHN_ALPHA] = alpha;

	/* Setup opacity mode & mask */
	opac = tool_opacity;
	if ((mem_channel > CHN_ALPHA) || (mem_img_bpp == 1)) opac = 0;
	mask0 = NULL;
	if ((mem_channel <= CHN_ALPHA) && mem_img[CHN_MASK] && !channel_dis[CHN_MASK])
		mask0 = mem_img[CHN_MASK];

	if (layers_total && show_layers_main)
	{
		if (layer_selected)
		{
			lx = layer_table[layer_selected].x;
			ly = layer_table[layer_selected].y;
			if (zoom > 1)
			{
				i = lx / zoom;
				lx = i * zoom > lx ? i - 1 : i;
				j = ly / zoom;
				ly = j * zoom > ly ? j - 1 : j;
			}
			else
			{
				lx *= scale;
				ly *= scale;
			}
			xpm = layer_table[layer_selected].use_trans ?
				layer_table[layer_selected].trans : -1;
			lop = (layer_table[layer_selected].opacity * 255 + 50) / 100;
		}
		render_layers(rgb, lx + px1, ly + py1, pw, ph, can_zoom, 0,
			layer_selected - 1, 1);
	}
	else
	{
		async_bk = !chequers_optimize; /* Only w/o layers */
		render_background(rgb, px1, py1, pw, ph, pw);
	}

	setup_row(px1, pw, can_zoom, mem_width, xpm, lop, mem_img_bpp, mem_pal);
	j0 = -1; tmp = rgb; pw3 = pw * 3;
	for (jj = 0; jj < ph; jj++ , tmp += pw3)
	{
		j = ((py1 + jj) * zoom) / scale;
		if (j != j0)
		{
			j0 = j;
			di = mem_width * j + dx;
			dc = mem_clip_w * (j - marq_y1) + dx - marq_x1;
			if (tlist[CHN_ALPHA])
				memcpy(alpha, mem_img[CHN_ALPHA] + di, l);
			prep_mask(0, zoom, pww, mask, mask0 ? mask0 + di : NULL,
				mem_img[CHN_IMAGE] + di * mem_img_bpp);
			process_mask(0, zoom, pww, mask, alpha, mem_img[CHN_ALPHA] + di,
				clip_alpha ? clip_alpha + dc : t_alpha,
				mem_clip_mask ? mem_clip_mask + dc : NULL, opac);
			if (clip_image)
			{
				if (mem_img[mem_channel])
					memcpy(pix, mem_img[mem_channel] +
						di * bpp, l * bpp);
				else memset(pix, 0, l * bpp);
				process_img(0, zoom, pww, mask, pix,
					mem_img[mem_channel] + di * bpp,
					mem_clipboard + dc * mem_clip_bpp, opac);
			}
		}
		else if (!async_bk)
		{
			memcpy(tmp, tmp - pw3, pw3);
			continue;
		}
		render_row(tmp, mem_img, dx, j, tlist);
		overlay_row(tmp, mem_img, dx, j, tlist);
	}

	if (layers_total && show_layers_main)
		render_layers(rgb, lx + px1, ly + py1, pw, ph, can_zoom,
			layer_selected + 1, layers_total, 1);

	gdk_draw_rgb_image(drawing_canvas->window, drawing_canvas->style->black_gc,
			margin_main_x + px1, margin_main_y + py1,
			pw, ph, GDK_RGB_DITHER_NONE, rgb, pw3);
	free(pix);
	free(rgb);
	free(t_alpha);
	async_bk = FALSE;
}

void main_render_rgb(unsigned char *rgb, int px, int py, int pw, int ph)
{
	chanlist tlist;
	unsigned char *mask0 = NULL, *pvi = NULL, *pvm = NULL, *pvx = NULL;
	int alpha_blend = !overlay_alpha;
	int pw2, ph2, px2 = px - margin_main_x, py2 = py - margin_main_y;
	int j, jj, j0, l, dx, pww, zoom = 1, scale = 1, nix = 0, niy = 0;
	int lop = 255, xpm = mem_xpm_trans;

	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	pw2 = pw + px2;
	ph2 = ph + py2;

	if (px2 < 0) nix = -px2;
	if (py2 < 0) niy = -py2;
	rgb += (pw * niy + nix) * 3;

	// Update image + blank space outside
	j = (mem_width * scale + zoom - 1) / zoom;
	jj = (mem_height * scale + zoom - 1) / zoom;
	if (pw2 > j) pw2 = j;
	if (ph2 > jj) ph2 = jj;
	px2 += nix; py2 += niy;
	pw2 -= px2; ph2 -= py2;

	if ((pw2 < 1) || (ph2 < 1)) return;

	if ((!mem_img[CHN_ALPHA] || channel_dis[CHN_ALPHA]) && (xpm < 0))
		alpha_blend = FALSE;
	if (layers_total && show_layers_main)
	{
		if (layer_selected)
		{
			xpm = layer_table[layer_selected].use_trans ?
				layer_table[layer_selected].trans : -1;
			lop = (layer_table[layer_selected].opacity * 255 + 50) / 100;
		}
	}
	else if (alpha_blend) render_background(rgb, px2, py2, pw2, ph2, pw);

	dx = (px2 * zoom) / scale;
	memset(tlist, 0, sizeof(chanlist));
	if (!channel_dis[CHN_MASK]) mask0 = mem_img[CHN_MASK];
	pww = pw2;
	if (scale > 1) l = pww = (px2 + pw2 - 1) / scale - dx + 1;
	else l = (pw2 - 1) * zoom + 1;
	if (mem_preview && (mem_img_bpp == 3))
	{
		pvm = malloc(l * 4);
		if (pvm)
		{
			pvi = pvm + l;
			tlist[CHN_IMAGE] = pvi;
		}
	}
	else if (csel_overlay) pvx = malloc(l);

	setup_row(px2, pw2, can_zoom, mem_width, xpm, lop, mem_img_bpp, mem_pal);
 	j0 = -1; pw *= 3; pw2 *= 3;
	for (jj = 0; jj < ph2; jj++ , rgb += pw)
	{
		j = ((py2 + jj) * zoom) / scale;
		if (j != j0)
		{
			j0 = j;
			if (pvm)
			{
				l = mem_width * j + dx;
				prep_mask(0, zoom, pww, pvm, mask0 ? mask0 + l : NULL,
					mem_img[CHN_IMAGE] + l * 3);
				do_transform(0, zoom, pww, pvm, pvi,
					mem_img[CHN_IMAGE] + l * 3);
			}
			else if (pvx)
			{
				memset(pvx, 0, l);
				csel_scan(0, zoom, pww, pvx, mem_img[CHN_IMAGE] +
					(mem_width * j + dx) * mem_img_bpp, csel_data);
			}
		}
		else if (!async_bk)
		{
			memcpy(rgb, rgb - pw, pw2);
			continue;
		}
		render_row(rgb, mem_img, dx, j, tlist);
		if (!pvx) overlay_row(rgb, mem_img, dx, j, tlist);
		else overlay_preview(rgb, pvx, csel_preview, csel_preview_a);
	}
	free(pvm);
	free(pvx);
}

/* Draw grid on rgb memory */
void draw_grid(unsigned char *rgb, int x, int y, int w, int h)
{
	int i, j, k, dx, dy, step, step3;
	unsigned char *tmp;


	if (!mem_show_grid || (can_zoom < mem_grid_min)) return;
	step = can_zoom;

	dx = (x - margin_main_x) % step;
	if (dx < 0) dx += step;
	dy = (y - margin_main_y) % step;
	if (dy < 0) dy += step;
	if (dx) dx = (step - dx) * 3;
	w *= 3;

	for (k = dy , i = 0; i < h; i++)
	{
		tmp = rgb + i * w;
		if (!k) /* Filled line */
		{
			j = 0; step3 = 3;
		}
		else /* Spaced dots */
		{
			j = dx; step3 = step * 3;
		}
		k = (k + 1) % step;
		for (; j < w; j += step3)
		{
			tmp[j + 0] = mem_grid_rgb[0];
			tmp[j + 1] = mem_grid_rgb[1];
			tmp[j + 2] = mem_grid_rgb[2];
		}
	}
}

void repaint_canvas( int px, int py, int pw, int ph )
{
	unsigned char *rgb;
	int pw2, ph2, lx = 0, ly = 0, rx1, ry1, rx2, ry2, rpx, rpy;
	int i, j, zoom = 1, scale = 1;

	if (zoom_flag == 1) return;		// Stops excess jerking in GTK+1 when zooming

	if (px < 0)
	{
		pw += px; px = 0;
	}
	if (py < 0)
	{
		ph += py; py = 0;
	}

	if ((pw <= 0) || (ph <= 0)) return;
	rgb = malloc(i = pw * ph * 3);
	if (!rgb) return;
	memset(rgb, mem_background, i);

	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	rpx = px - margin_main_x;
	rpy = py - margin_main_y;
	pw2 = rpx + pw - 1;
	ph2 = rpy + ph - 1;

	if (layers_total && show_layers_main)
	{
		if ( layer_selected > 0 )
		{
			lx = layer_table[layer_selected].x;
			ly = layer_table[layer_selected].y;
			if (zoom > 1)
			{
				i = lx / zoom;
				lx = i * zoom > lx ? i - 1 : i;
				j = ly / zoom;
				ly = j * zoom > ly ? j - 1 : j;
			}
			else
			{
				lx *= scale;
				ly *= scale;
			}
		}
		render_layers(rgb, rpx + lx, rpy + ly,
			pw, ph, can_zoom, 0, layer_selected - 1, 1);
	}
	else async_bk = !chequers_optimize; /* Only w/o layers */
	main_render_rgb(rgb, px, py, pw, ph);
	if (layers_total && show_layers_main)
		render_layers(rgb, rpx + lx, rpy + ly,
			pw, ph, can_zoom, layer_selected + 1, layers_total, 1);

	draw_grid(rgb, px, py, pw, ph);

	gdk_draw_rgb_image(drawing_canvas->window, drawing_canvas->style->black_gc,
		px, py, pw, ph, GDK_RGB_DITHER_NONE, rgb, pw * 3);

	free(rgb);
	async_bk = FALSE;

	/* Add clipboard image to redraw if needed */
	if (show_paste && (marq_status >= MARQUEE_PASTE))
	{
		/* Enforce image bounds */
		if (rpx < 0) rpx = 0;
		if (rpy < 0) rpy = 0;
		i = ((mem_width + zoom - 1) * scale) / zoom;
		j = ((mem_height + zoom - 1) * scale) / zoom;
		if (pw2 >= i) pw2 = i - 1;
		if (ph2 >= j) ph2 = j - 1;

		/* Check paste bounds for intersection, but leave actually
		 * enforcing them to repaint_paste() */
		rx1 = (marq_x1 * scale + zoom - 1) / zoom;
		ry1 = (marq_y1 * scale + zoom - 1) / zoom;
		rx2 = (marq_x2 * scale) / zoom + scale - 1;
		ry2 = (marq_y2 * scale) / zoom + scale - 1;
		if ((rx1 <= pw2) && (rx2 >= rpx) && (ry1 <= ph2) && (ry2 >= rpy))
			repaint_paste(rpx, rpy, pw2, ph2);
	}

	/* Draw perimeter/marquee/gradient as we may have drawn over them */
/* !!! All other over-the-image things have to be redrawn here as well !!! */
	if (grad_status == GRAD_DONE)
	{
		/* Canvas-space endpoints */
		if (grad_x1 < grad_x2) rx1 = grad_x1 , rx2 = grad_x2;
		else rx1 = grad_x2 , rx2 = grad_x1;
		if (grad_y1 < grad_y2) ry1 = grad_y1 , ry2 = grad_y2;
		else ry1 = grad_y2 , ry2 = grad_y1;
		rx1 = (rx1 * scale + zoom - 1) / zoom;
		ry1 = (ry1 * scale + zoom - 1) / zoom;
		rx2 = (rx2 * scale) / zoom + scale - 1;
		ry2 = (ry2 * scale) / zoom + scale - 1;

		/* Check intersection - coarse */
		if ((rx1 <= pw2) && (rx2 >= rpx) && (ry1 <= ph2) && (ry2 >= rpy))
		{
			if (rx1 != rx2) /* Check intersection - fine */
			{
				float ty1, ty2, dy;

				if ((grad_x1 < grad_x2) ^ (grad_y1 < grad_y2))
					i = ry2 , j = ry1;
				else i = ry1 , j = ry2;

				dy = (j - i) / (float)(rx2 - rx1);
				ty1 = rx1 >= rpx ? i : i + (rpx - rx1) * dy;
				ty2 = rx2 <= pw2 ? j : i + (pw2 - rx1) * dy;

				if (!((ty1 < rpy - scale) && (ty2 < rpy - scale)) &&
					!((ty1 > ph2 + scale) && (ty2 > ph2 + scale)))
					repaint_grad(1);
			}
			else repaint_grad(1); /* Automatic intersect */
		}
	}
	if (marq_status != MARQUEE_NONE) paint_marquee(11, marq_x1, marq_y1);
	if (perim_status > 0) repaint_perim();
}

/* Update x,y,w,h area of current image */
void main_update_area(int x, int y, int w, int h)
{
	int zoom, scale;

	if (can_zoom < 1.0)
	{
		zoom = rint(1.0 / can_zoom);
		w += x;
		h += y;
		x = x < 0 ? -(-x / zoom) : (x + zoom - 1) / zoom;
		y = y < 0 ? -(-y / zoom) : (y + zoom - 1) / zoom;
		w = (w - x * zoom + zoom - 1) / zoom;
		h = (h - y * zoom + zoom - 1) / zoom;
		if ((w <= 0) || (h <= 0)) return;
	}
	else
	{
		scale = rint(can_zoom);
		x *= scale;
		y *= scale;
		w *= scale;
		h *= scale;
	}

	gtk_widget_queue_draw_area(drawing_canvas,
		x + margin_main_x, y + margin_main_y, w, h);
}

/* Get zoomed canvas size */
void canvas_size(int *w, int *h)
{
	int i;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0)
	{
		i = rint(1.0 / can_zoom);
		*w = (mem_width + i - 1) / i;
		*h = (mem_height + i - 1) / i;
	}
	else
	{
		i = rint(can_zoom);
		*w = mem_width * i;
		*h = mem_height * i;
	}
}

void clear_perim()
{
	perim_status = 0; /* Cleared */
	/* Don't bother if tool has no perimeter */
	if (NO_PERIM(tool_type)) return;
	clear_perim_real(0, 0);
	if ( tool_type == TOOL_CLONE ) clear_perim_real(clone_x, clone_y);
}

void repaint_perim_real( int r, int g, int b, int ox, int oy )
{
	int i, j, w, h, x0, y0, x1, y1, zoom = 1, scale = 1;
	unsigned char *rgb;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	x0 = margin_main_x + ((perim_x + ox) * scale) / zoom;
	y0 = margin_main_y + ((perim_y + oy) * scale) / zoom;
	x1 = margin_main_x + ((perim_x + ox + perim_s - 1) * scale) / zoom + scale - 1;
	y1 = margin_main_y + ((perim_y + oy + perim_s - 1) * scale) / zoom + scale - 1;

	w = x1 - x0 + 1;
	h = y1 - y0 + 1;
	j = (w > h ? w : h) * 3;
	rgb = calloc(j + 2 * 3, 1); /* 2 extra pixels reserved for loop */
	if (!rgb) return;
	for (i = 0; i < j; i += 6 * 3)
	{
		rgb[i + 0] = rgb[i + 3] = rgb[i + 6] = r;
		rgb[i + 1] = rgb[i + 4] = rgb[i + 7] = g;
		rgb[i + 2] = rgb[i + 5] = rgb[i + 8] = b;
	}

	gdk_draw_rgb_image(drawing_canvas->window, drawing_canvas->style->black_gc,
		x0, y0, 1, h, GDK_RGB_DITHER_NONE, rgb, 3);
	gdk_draw_rgb_image(drawing_canvas->window, drawing_canvas->style->black_gc,
		x1, y0, 1, h, GDK_RGB_DITHER_NONE, rgb, 3);

	gdk_draw_rgb_image(drawing_canvas->window, drawing_canvas->style->black_gc,
		x0 + 1, y0, w - 2, 1, GDK_RGB_DITHER_NONE, rgb, 0);
	gdk_draw_rgb_image(drawing_canvas->window, drawing_canvas->style->black_gc,
		x0 + 1, y1, w - 2, 1, GDK_RGB_DITHER_NONE, rgb, 0);
	free(rgb);
}

void repaint_perim()
{
	/* Don't bother if tool has no perimeter */
	if (NO_PERIM(tool_type)) return;
	repaint_perim_real( 255, 255, 255, 0, 0 );
	if ( tool_type == TOOL_CLONE )
		repaint_perim_real( 255, 0, 0, clone_x, clone_y );
	perim_status = 1; /* Drawn */
}

static gint canvas_motion( GtkWidget *widget, GdkEventMotion *event )
{
	int x, y, rm, zoom = 1, scale = 1, button = 0;
	GdkModifierType state;
	gdouble pressure = 1.0;

	/* Skip synthetic mouse moves */
	if (unreal_move == 3)
	{
		unreal_move = 2;
		return TRUE;
	}
	rm = unreal_move;
	unreal_move = 0;

	/* Do nothing if no image */
	if (!mem_img[CHN_IMAGE]) return (TRUE);

#if GTK_MAJOR_VERSION == 1
	if (event->is_hint)
	{
		gdk_input_window_get_pointer(event->window, event->deviceid,
			NULL, NULL, &pressure, NULL, NULL, &state);
		gdk_window_get_pointer(event->window, &x, &y, &state);
	}
	else
	{
		x = event->x;
		y = event->y;
		pressure = event->pressure;
		state = event->state;
	}
#endif
#if GTK_MAJOR_VERSION == 2
	if (event->is_hint) gdk_device_get_state(event->device, event->window,
		NULL, &state);
	x = event->x;
	y = event->y;
	state = event->state;

	if (tablet_working) gdk_event_get_axis((GdkEvent *)event,
		GDK_AXIS_PRESSURE, &pressure);
#endif

	if ((state & (GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)) ==
		(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)) button = 13;
	else if (state & GDK_BUTTON1_MASK) button = 1;
	else if (state & GDK_BUTTON3_MASK) button = 3;
	else if (state & GDK_BUTTON2_MASK) button = 2;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	x = ((x - margin_main_x) * zoom) / scale;
	y = ((y - margin_main_y) * zoom) / scale;

	mouse_event(event->type, x, y, state, button, pressure, rm & 1);

	return TRUE;
}

static gboolean configure_canvas( GtkWidget *widget, GdkEventConfigure *event )
{
	int w, h, new_margin_x = 0, new_margin_y = 0;

	if ( canvas_image_centre )
	{
		canvas_size(&w, &h);

		w = drawing_canvas->allocation.width - w;
		h = drawing_canvas->allocation.height - h;

		if (w > 0) new_margin_x = w >> 1;
		if (h > 0) new_margin_y = h >> 1;
	}

	if ((new_margin_x != margin_main_x) || (new_margin_y != margin_main_y))
	{
		margin_main_x = new_margin_x;
		margin_main_y = new_margin_y;
		gtk_widget_queue_draw(drawing_canvas);
			// Force redraw of whole canvas as the margin has shifted
	}
	vw_align_size(vw_zoom);		// Update the view window as needed

	return TRUE;
}

void force_main_configure()
{
	if ( drawing_canvas ) configure_canvas( drawing_canvas, NULL );
	if ( vw_drawing ) vw_configure( vw_drawing, NULL );
}

static gint expose_canvas( GtkWidget *widget, GdkEventExpose *event )
{
	int px, py, pw, ph;

	px = event->area.x;		// Only repaint if we need to
	py = event->area.y;
	pw = event->area.width;
	ph = event->area.height;

	repaint_canvas( px, py, pw, ph );
	paint_poly_marquee();

	return FALSE;
}

void pressed_choose_patterns( GtkMenuItem *menu_item, gpointer user_data )
{	choose_pattern(0);	}

void pressed_choose_brush( GtkMenuItem *menu_item, gpointer user_data )
{	choose_pattern(1);	}

void pressed_edit_AB( GtkMenuItem *menu_item, gpointer user_data )
{	choose_colours();	}

static void pressed_docs()
{
	show_html(inifile_get(HANDBOOK_BROWSER_INI, NULL),
		inifile_get(HANDBOOK_LOCATION_INI, NULL));
}

void set_cursor()			// Set mouse cursor
{
	if ( inifile_get_gboolean( "cursorToggle", TRUE ) )
		gdk_window_set_cursor( drawing_canvas->window, m_cursor[tool_type] );
	else
		gdk_window_set_cursor( drawing_canvas->window, NULL );
}



void toolbar_icon_event2(GtkWidget *widget, gpointer data)
{
	gint j = (gint) data;

	switch (j)
	{
	case MTB_NEW:
		pressed_new( NULL, NULL ); break;
	case MTB_OPEN:
		pressed_open_file( NULL, NULL ); break;
	case MTB_SAVE:
		pressed_save_file( NULL, NULL ); break;
	case MTB_CUT:
		pressed_copy(NULL, NULL, 1); break;
	case MTB_COPY:
		pressed_copy(NULL, NULL, 0); break;
	case MTB_PASTE:
		pressed_paste_centre( NULL, NULL ); break;
	case MTB_UNDO:
		main_undo( NULL, NULL ); break;
	case MTB_REDO:
		main_redo( NULL, NULL ); break;
	case MTB_BRCOSA:
		pressed_brcosa( NULL, NULL ); break;
	case MTB_PAN:
		pressed_pan( NULL, NULL ); break;
	}
}

void toolbar_icon_event (GtkWidget *widget, gpointer data)
{
	gint i = tool_type;

	switch ((gint)data)
	{
	case TTB_PAINT:
		tool_type = brush_tool_type; break;
	case TTB_SHUFFLE:
		tool_type = TOOL_SHUFFLE; break;
	case TTB_FLOOD:
		tool_type = TOOL_FLOOD; break;
	case TTB_LINE:
		tool_type = TOOL_LINE; break;
	case TTB_SMUDGE:
		tool_type = TOOL_SMUDGE; break;
	case TTB_CLONE:
		tool_type = TOOL_CLONE; break;
	case TTB_SELECT:
		tool_type = TOOL_SELECT; break;
	case TTB_POLY:
		tool_type = TOOL_POLYGON; break;
	case TTB_GRAD:
		tool_type = TOOL_GRADIENT; break;
	case TTB_LASSO:
		pressed_lasso(NULL, NULL, 0); break;
	case TTB_TEXT:
		pressed_text( NULL, NULL ); break;
	case TTB_ELLIPSE:
		pressed_ellipse(NULL, NULL, 0); break;
	case TTB_FELLIPSE:
		pressed_ellipse(NULL, NULL, 1); break;
	case TTB_OUTLINE:
		pressed_rectangle(NULL, NULL, 0); break;
	case TTB_FILL:
		pressed_rectangle(NULL, NULL, 1); break;
	case TTB_SELFV:
		pressed_flip_sel_v( NULL, NULL ); break;
	case TTB_SELFH:
		pressed_flip_sel_h( NULL, NULL ); break;
	case TTB_SELRCW:
		pressed_rotate_sel_clock( NULL, NULL ); break;
	case TTB_SELRCCW:
		pressed_rotate_sel_anti( NULL, NULL ); break;
	}

	if ( tool_type != i )		// User has changed tool
	{
		if ( i == TOOL_LINE && tool_type != TOOL_LINE ) stop_line();
		if ( marq_status != MARQUEE_NONE)
		{
			if ( marq_status >= MARQUEE_PASTE &&
				inifile_get_gboolean( "pasteCommit", FALSE ) )
			{
				commit_paste(TRUE);
				pen_down = 0;
			}

			marq_status = MARQUEE_NONE;			// Marquee is on so lose it!
			gtk_widget_queue_draw( drawing_canvas );	// Needed to clear selection
		}
		if ( poly_status != POLY_NONE)
		{
			poly_status = POLY_NONE;			// Marquee is on so lose it!
			poly_points = 0;
			gtk_widget_queue_draw( drawing_canvas );	// Needed to clear selection
		}
		if ( tool_type == TOOL_CLONE )
		{
			clone_x = -tool_size;
			clone_y = tool_size;
		}
		/* Persistent selection frame */
		if ((tool_type == TOOL_SELECT) && (marq_x1 >= 0) &&
			(marq_y1 >= 0) && (marq_x2 >= 0) && (marq_y2 >= 0))
		{
			marq_status = MARQUEE_DONE;
			check_marquee();
			paint_marquee(1, marq_x1, marq_y1);
		}
		update_sel_bar();
		update_menus();
		set_cursor();
	}
}

static void pressed_view_hori( GtkMenuItem *menu_item, gpointer user_data )
{
	gboolean vs = view_showing;

	if (GTK_CHECK_MENU_ITEM(menu_item)->active)
	{
		if (main_split == main_hsplit) return;
		view_hide();
		main_split = main_hsplit;
	}
	else
	{
		if (main_split == main_vsplit) return;
		view_hide();
		main_split = main_vsplit;
	}
	if (vs) view_show();
}

void set_image(gboolean state)
{
	static int depth = 0;

	if (state ? --depth : depth++) return;

	(state ? gtk_widget_show_all : gtk_widget_hide)(view_showing ? main_split :
		scrolledwindow_canvas);
}

static void parse_drag( char *txt )
{
	gboolean nlayer = TRUE;
	char fname[300], *tp, *tp2;
	int i, j;

	if ( layers_window == NULL ) pressed_layers( NULL, NULL );
		// For some reason the layers window must be initialized, or bugs happen??

	gtk_widget_set_sensitive( layers_window, FALSE );
	gtk_widget_set_sensitive( main_window, FALSE );

	tp = txt;
	while ( layers_total<MAX_LAYERS && (tp2 = strpbrk( tp, "file:" )) != NULL )
	{
		tp = tp2 + 5;
		while ( tp[0] == '/' ) tp++;
#ifndef WIN32
		// If not windows keep a leading slash
		tp--;
		// If windows strip away all leading slashes
#endif
		i = 0;
		j = 0;
		while ( tp[j] > 30 && j<295 )	// Copy filename
		{
			if ( tp[j] == '%' )	// Weed out those ghastly % substitutions
			{
				fname[i++] = read_hex_dub( tp+j+1 );
				j=j+2;
			}
			else fname[i++] = tp[j];
			j++;
		}
		fname[i] = 0;
		tp = tp + j;
//printf(">%s<\n", fname);
		if ( nlayer )
		{
			layer_new( 8, 8, 3, 16, CMASK_IMAGE );		// Add a new layer if needed
			nlayer = FALSE;
		}
		if ( do_a_load( fname ) == 0 ) nlayer = TRUE;		// Load the file
	}

	if ( layers_total > 0 ) view_show();

	gtk_widget_set_sensitive( layers_window, TRUE );
	gtk_widget_set_sensitive( main_window, TRUE );
}

static void drag_n_drop_received(GtkWidget *widget, GdkDragContext *context, gint x, gint y,
					GtkSelectionData *data, guint info, guint time)
{
	if ((data->length >= 0) && (data->format == 8))
	{
		parse_drag( (gchar *)data->data );
//printf("%s\n", (gchar *)data->data);
		gtk_drag_finish (context, TRUE, FALSE, time);
		return;
	}

	gtk_drag_finish (context, FALSE, FALSE, time);
}


void main_init()
{
	gint i;
	char txt[256];
	float f=0;

	GtkTargetEntry target_table[1] = { { "text/uri-list", 0, 1 } };
	GdkColor cfg = { -1, -1, -1, -1 }, cbg = { 0, 0, 0, 0 };
	GtkRequisition req;
	GdkPixmap *icon_pix = NULL;

	GtkWidget *vw_drawing, *menubar1, *vbox_main,
			*hbox_bar, *hbox_bottom;
	GtkAccelGroup *accel_group;
	GtkItemFactory *item_factory;

	GtkItemFactoryEntry menu_items[] = {
		{ _("/_File"),			NULL,		NULL,0, "<Branch>" },
		{ _("/File/tear"),		NULL,		NULL,0, "<Tearoff>" },
		{ _("/File/New"),		"<control>N",	pressed_new,0, NULL },
		{ _("/File/Open ..."),		"<control>O",	pressed_open_file, 0, NULL },
		{ _("/File/Save"),		"<control>S",	pressed_save_file,0, NULL },
		{ _("/File/Save As ..."),	NULL,		pressed_save_file_as, 0, NULL },
		{ _("/File/sep1"),		NULL, 	  NULL,0, "<Separator>" },
		{ _("/File/Export Undo Images ..."), NULL,	pressed_export_undo,0, NULL },
		{ _("/File/Export Undo Images (reversed) ..."), NULL, pressed_export_undo2,0, NULL },
		{ _("/File/Export ASCII Art ..."), NULL, 	pressed_export_ascii,0, NULL },
		{ _("/File/Export Animated GIF ..."), NULL, 	pressed_export_gif,0, NULL },
		{ _("/File/sep2"),		NULL,		NULL,0, "<Separator>" },
		{ _("/File/1"),  		"<shift><control>F1", pressed_load_recent, 1, NULL },
		{ _("/File/2"),  		"<shift><control>F2", pressed_load_recent, 2, NULL },
		{ _("/File/3"),  		"<shift><control>F3", pressed_load_recent, 3, NULL },
		{ _("/File/4"),  		"<shift><control>F4", pressed_load_recent, 4, NULL },
		{ _("/File/5"),  		"<shift><control>F5", pressed_load_recent, 5, NULL },
		{ _("/File/6"),  		"<shift><control>F6", pressed_load_recent, 6, NULL },
		{ _("/File/7"),  		"<shift><control>F7", pressed_load_recent, 7, NULL },
		{ _("/File/8"),  		"<shift><control>F8", pressed_load_recent, 8, NULL },
		{ _("/File/9"),  		"<shift><control>F9", pressed_load_recent, 9, NULL },
		{ _("/File/10"), 		"<shift><control>F10", pressed_load_recent, 10, NULL },
		{ _("/File/11"), 		NULL,		pressed_load_recent, 11, NULL },
		{ _("/File/12"), 		NULL,		pressed_load_recent, 12, NULL },
		{ _("/File/13"), 		NULL,		pressed_load_recent, 13, NULL },
		{ _("/File/14"), 		NULL,		pressed_load_recent, 14, NULL },
		{ _("/File/15"), 		NULL,		pressed_load_recent, 15, NULL },
		{ _("/File/16"), 		NULL,		pressed_load_recent, 16, NULL },
		{ _("/File/17"), 		NULL,		pressed_load_recent, 17, NULL },
		{ _("/File/18"), 		NULL,		pressed_load_recent, 18, NULL },
		{ _("/File/19"), 		NULL,		pressed_load_recent, 19, NULL },
		{ _("/File/20"),		NULL,		pressed_load_recent, 20, NULL },
		{ _("/File/sep1"),		NULL,		NULL,0, "<Separator>" },
		{ _("/File/Quit"),		"<control>Q",	quit_all,	0, NULL },

		{ _("/_Edit"),			NULL,		NULL,0, "<Branch>" },
		{ _("/Edit/tear"),		NULL,		NULL,0, "<Tearoff>" },
		{ _("/Edit/Undo"),		"<control>Z",	main_undo,0, NULL },
		{ _("/Edit/Redo"),		"<control>R",	main_redo,0, NULL },
		{ _("/Edit/sep1"),		NULL,		NULL,0, "<Separator>" },
		{ _("/Edit/Cut"),		"<control>X",	pressed_copy, 1, NULL },
		{ _("/Edit/Copy"),		"<control>C",	pressed_copy, 0, NULL },
		{ _("/Edit/Paste To Centre"),	"<control>V",	pressed_paste_centre, 0, NULL },
		{ _("/Edit/Paste To New Layer"), "<control><shift>V", pressed_paste_layer, 0, NULL },
		{ _("/Edit/Paste"),		"<control>K",	pressed_paste, 0, NULL },
		{ _("/Edit/Paste Text"),	"T",		pressed_text, 0, NULL },
		{ _("/Edit/sep1"),			NULL, 	  NULL,0, "<Separator>" },
		{ _("/Edit/Load Clipboard"),		NULL, 	  NULL, 0, "<Branch>" },
		{ _("/Edit/Load Clipboard/tear"),	NULL, 	  NULL, 0, "<Tearoff>" },
		{ _("/Edit/Load Clipboard/1"),		"<shift>F1",    load_clip, 1, NULL },
		{ _("/Edit/Load Clipboard/2"),		"<shift>F2",    load_clip, 2, NULL },
		{ _("/Edit/Load Clipboard/3"),		"<shift>F3",    load_clip, 3, NULL },
		{ _("/Edit/Load Clipboard/4"),		"<shift>F4",    load_clip, 4, NULL },
		{ _("/Edit/Load Clipboard/5"),		"<shift>F5",    load_clip, 5, NULL },
		{ _("/Edit/Load Clipboard/6"),		"<shift>F6",    load_clip, 6, NULL },
		{ _("/Edit/Load Clipboard/7"),		"<shift>F7",    load_clip, 7, NULL },
		{ _("/Edit/Load Clipboard/8"),		"<shift>F8",    load_clip, 8, NULL },
		{ _("/Edit/Load Clipboard/9"),		"<shift>F9",    load_clip, 9, NULL },
		{ _("/Edit/Load Clipboard/10"),		"<shift>F10",   load_clip, 10, NULL },
		{ _("/Edit/Load Clipboard/11"),		"<shift>F11",   load_clip, 11, NULL },
		{ _("/Edit/Load Clipboard/12"),		"<shift>F12",   load_clip, 12, NULL },
		{ _("/Edit/Save Clipboard"),		NULL, 	  NULL, 0, "<Branch>" },
		{ _("/Edit/Save Clipboard/tear"),	NULL, 	  NULL, 0, "<Tearoff>" },
		{ _("/Edit/Save Clipboard/1"),		"<control>F1",  save_clip, 1, NULL },
		{ _("/Edit/Save Clipboard/2"),		"<control>F2",  save_clip, 2, NULL },
		{ _("/Edit/Save Clipboard/3"),		"<control>F3",  save_clip, 3, NULL },
		{ _("/Edit/Save Clipboard/4"),		"<control>F4",  save_clip, 4, NULL },
		{ _("/Edit/Save Clipboard/5"),		"<control>F5",  save_clip, 5, NULL },
		{ _("/Edit/Save Clipboard/6"),		"<control>F6",  save_clip, 6, NULL },
		{ _("/Edit/Save Clipboard/7"),		"<control>F7",  save_clip, 7, NULL },
		{ _("/Edit/Save Clipboard/8"),		"<control>F8",  save_clip, 8, NULL },
		{ _("/Edit/Save Clipboard/9"),		"<control>F9",  save_clip, 9, NULL },
		{ _("/Edit/Save Clipboard/10"),  	"<control>F10", save_clip, 10, NULL },
		{ _("/Edit/Save Clipboard/11"),  	"<control>F11", save_clip, 11, NULL },
		{ _("/Edit/Save Clipboard/12"),  	"<control>F12", save_clip, 12, NULL },
		{ _("/Edit/sep1"),			NULL,	NULL,0, "<Separator>" },
		{ _("/Edit/Choose Pattern ..."),	"F2",	pressed_choose_patterns,0, NULL },
		{ _("/Edit/Choose Brush ..."),		"F3",	pressed_choose_brush,0, NULL },
		{ _("/Edit/Create Patterns"),		NULL,	pressed_create_patterns,0, NULL },

		{ _("/_View"),			NULL,		NULL,0, "<Branch>" },
		{ _("/View/tear"),		NULL,		NULL,0, "<Tearoff>" },
{ _("/View/Show Main Toolbar"),		"F5", pressed_toolbar_toggle, TOOLBAR_MAIN, "<CheckItem>" },
{ _("/View/Show Tools Toolbar"),	"F6", pressed_toolbar_toggle, TOOLBAR_TOOLS, "<CheckItem>" },
{ _("/View/Show Settings Toolbar"),	"F7", pressed_toolbar_toggle, TOOLBAR_SETTINGS, "<CheckItem>" },
{ _("/View/Show Palette"),		"F8", pressed_toolbar_toggle, TOOLBAR_PALETTE, "<CheckItem>" },
{ _("/View/Show Status Bar"),		NULL, pressed_toolbar_toggle, TOOLBAR_STATUS, "<CheckItem>" },
		{ _("/View/sep1"),		NULL,		NULL,0, "<Separator>" },
		{ _("/View/Toggle Image View (Home)"), NULL,	toggle_view,0, NULL },
		{ _("/View/Centralize Image"),	NULL,		pressed_centralize,0, "<CheckItem>" },
		{ _("/View/Show zoom grid"),	NULL,		zoom_grid,0, "<CheckItem>" },
		{ _("/View/sep1"),		NULL,		NULL,0, "<Separator>" },
		{ _("/View/View Window"),	"V",		pressed_view,0, "<CheckItem>" },
		{ _("/View/Horizontal Split"),	"H",		pressed_view_hori,0, "<CheckItem>" },
		{ _("/View/Focus View Window"),	NULL,		pressed_view_focus,0, "<CheckItem>" },
		{ _("/View/sep1"),		NULL,		NULL,0, "<Separator>" },
		{ _("/View/Pan Window (End)"),	NULL,		pressed_pan,0, NULL },
		{ _("/View/Command Line Window"),	"C",	pressed_cline,0, NULL },
		{ _("/View/Layers Window"),		"L",	pressed_layers, 0, NULL },

		{ _("/_Image"),  			NULL, 	NULL,0, "<Branch>" },
		{ _("/Image/tear"),			NULL, 	NULL,0, "<Tearoff>" },
		{ _("/Image/Convert To RGB"),		NULL, 	pressed_convert_rgb,0, NULL },
		{ _("/Image/Convert To Indexed"),	NULL,	pressed_quantize,0, NULL },
		{ _("/Image/sep1"),			NULL,	NULL,0, "<Separator>" },
		{ _("/Image/Scale Canvas ..."),  	NULL,	pressed_scale,0, NULL },
		{ _("/Image/Resize Canvas ..."), 	NULL,	pressed_size,0, NULL },
		{ _("/Image/Crop"),			"<control><shift>X", pressed_crop, 0, NULL },
		{ _("/Image/sep1"),			NULL,	NULL,0, "<Separator>" },
		{ _("/Image/Flip Vertically"),		NULL,	pressed_flip_image_v,0, NULL },
		{ _("/Image/Flip Horizontally"), 	"<control>M", pressed_flip_image_h,0, NULL },
		{ _("/Image/Rotate Clockwise"),  	NULL,	pressed_rotate_image_clock,0, NULL },
		{ _("/Image/Rotate Anti-Clockwise"),	NULL,	pressed_rotate_image_anti,0, NULL },
		{ _("/Image/Free Rotate ..."),		NULL,	pressed_rotate_free,0, NULL },
		{ _("/Image/sep1"),			NULL,	NULL,0, "<Separator>" },
		{ _("/Image/Information ..."),		"<control>I", pressed_information,0, NULL },
		{ _("/Image/Preferences ..."),		"<control>P", pressed_preferences,0, NULL },

		{ _("/_Selection"),			NULL,	NULL,0, "<Branch>" },
		{ _("/Selection/tear"),  		NULL,	NULL,0, "<Tearoff>" },
		{ _("/Selection/Select All"),		"<control>A",   pressed_select_all, 0, NULL },
		{ _("/Selection/Select None (Esc)"), "<shift><control>A", pressed_select_none,0, NULL },
		{ _("/Selection/Lasso Selection"),	NULL,	pressed_lasso, 0, NULL },
		{ _("/Selection/Lasso Selection Cut"),	NULL,	pressed_lasso, 1, NULL },
		{ _("/Selection/sep1"),			NULL,	NULL,0, "<Separator>" },
		{ _("/Selection/Outline Selection"), "<control>T", pressed_rectangle, 0, NULL },
		{ _("/Selection/Fill Selection"), "<shift><control>T", pressed_rectangle, 1, NULL },
		{ _("/Selection/Outline Ellipse"), "<control>L", pressed_ellipse, 0, NULL },
		{ _("/Selection/Fill Ellipse"), "<shift><control>L", pressed_ellipse, 1, NULL },
		{ _("/Selection/sep1"),			NULL,	NULL,0, "<Separator>" },
		{ _("/Selection/Flip Vertically"),	NULL,	pressed_flip_sel_v,0, NULL },
		{ _("/Selection/Flip Horizontally"),	NULL,	pressed_flip_sel_h,0, NULL },
		{ _("/Selection/Rotate Clockwise"),	NULL,	pressed_rotate_sel_clock, 0, NULL },
		{ _("/Selection/Rotate Anti-Clockwise"), NULL,	pressed_rotate_sel_anti, 0, NULL },
		{ _("/Selection/sep1"),			NULL,	NULL,0, "<Separator>" },
		{ _("/Selection/Alpha Blend A,B"),	NULL,	pressed_clip_alpha_scale,0, NULL },
		{ _("/Selection/Move Alpha to Mask"),	NULL,	pressed_clip_alphamask,0, NULL },
		{ _("/Selection/Mask Colour A,B"),	NULL,	pressed_clip_mask,0, NULL },
		{ _("/Selection/Unmask Colour A,B"),	NULL,	pressed_clip_unmask,0, NULL },
		{ _("/Selection/Mask All Colours"),	NULL,	pressed_clip_mask_all,0, NULL },
		{ _("/Selection/Clear Mask"),		NULL,	pressed_clip_mask_clear,0, NULL },

		{ _("/_Palette"),			NULL, 	NULL,0, "<Branch>" },
		{ _("/Palette/tear"),			NULL,	NULL,0, "<Tearoff>" },
		{ _("/Palette/Open ..."),		NULL,	pressed_open_pal,0, NULL },
		{ _("/Palette/Save As ..."),		NULL,	pressed_save_pal,0, NULL },
		{ _("/Palette/Load Default"),		NULL, 	pressed_default_pal,0, NULL },
		{ _("/Palette/sep1"),			NULL, 	NULL,0, "<Separator>" },
		{ _("/Palette/Mask All"),		NULL, 	pressed_mask, 255, NULL },
		{ _("/Palette/Mask None"),		NULL, 	pressed_mask, 0, NULL },
		{ _("/Palette/sep1"),			NULL,	NULL,0, "<Separator>" },
		{ _("/Palette/Swap A & B"),		"X",	pressed_swap_AB,0, NULL },
		{ _("/Palette/Edit Colour A & B ..."), "<control>E", pressed_edit_AB,0, NULL },
		{ _("/Palette/Palette Editor ..."), "<control>W", pressed_allcol,0, NULL },
		{ _("/Palette/Set Palette Size ..."),	NULL,	pressed_add_cols,0, NULL },
		{ _("/Palette/Merge Duplicate Colours"), NULL,	pressed_remove_duplicates,0, NULL },
		{ _("/Palette/Remove Unused Colours"),	NULL,	pressed_remove_unused,0, NULL },
		{ _("/Palette/sep1"),			NULL,	NULL,0, "<Separator>" },
		{ _("/Palette/Create Quantized (DL1)"), NULL,	pressed_create_dl1,0, NULL },
		{ _("/Palette/Create Quantized (DL3)"), NULL,	pressed_create_dl3,0, NULL },
		{ _("/Palette/Create Quantized (Wu)"),  NULL,	pressed_create_wu,0, NULL },
		{ _("/Palette/sep1"),			NULL,	NULL,0, "<Separator>" },
		{ _("/Palette/Sort Colours ..."),	NULL,	pressed_sort_pal,0, NULL },

		{ _("/Effe_cts"),		NULL,	 	NULL, 0, "<Branch>" },
		{ _("/Effects/tear"),		NULL,	 	NULL, 0, "<Tearoff>" },
		{ _("/Effects/Transform Colour ..."), "<control><shift>C", pressed_brcosa,0, NULL },
		{ _("/Effects/Invert"),		"<control><shift>I", pressed_invert,0, NULL },
		{ _("/Effects/Greyscale"),	"<control>G",	pressed_greyscale,0, NULL },
		{ _("/Effects/Isometric Transformation"), NULL, NULL, 0, "<Branch>" },
		{ _("/Effects/Isometric Transformation/tear"), NULL, NULL, 0, "<Tearoff>" },
		{ _("/Effects/Isometric Transformation/Left Side Down"), NULL, iso_trans, 0, NULL },
		{ _("/Effects/Isometric Transformation/Right Side Down"), NULL, iso_trans, 1, NULL },
		{ _("/Effects/Isometric Transformation/Top Side Right"), NULL, iso_trans, 2, NULL },
		{ _("/Effects/Isometric Transformation/Bottom Side Right"), NULL, iso_trans, 3, NULL },
		{ _("/Effects/sep1"),		NULL,		NULL,0, "<Separator>" },
		{ _("/Effects/Edge Detect"),	NULL,		pressed_edge_detect,0, NULL },
		{ _("/Effects/Sharpen ..."),	NULL,		pressed_sharpen,0, NULL },
		{ _("/Effects/Unsharp Mask ..."), NULL,		pressed_unsharp,0, NULL },
		{ _("/Effects/Soften ..."),	NULL,		pressed_soften,0, NULL },
		{ _("/Effects/Gaussian Blur ..."), NULL,	pressed_gauss,0, NULL },
		{ _("/Effects/Emboss"),		NULL,		pressed_emboss,0, NULL },
		{ _("/Effects/sep1"),		NULL,		NULL,0, "<Separator>" },
		{ _("/Effects/Bacteria ..."),	NULL,		pressed_bacteria, 0, NULL },

		{ _("/Channels"),		NULL,		NULL, 0, "<Branch>" },
		{ _("/Channels/tear"),		NULL,		NULL, 0, "<Tearoff>" },
		{ _("/Channels/New ..."),	NULL,		pressed_channel_create, -1, NULL },
		{ _("/Channels/Load ..."),	NULL,		pressed_channel_load, 0, NULL },
		{ _("/Channels/Save As ..."),	NULL,		pressed_channel_save, 0, NULL },
		{ _("/Channels/Delete ..."),	NULL,		pressed_channel_delete, -1, NULL },
		{ _("/Channels/sep1"),		NULL,		NULL,0, "<Separator>" },
		{ _("/Channels/Edit Image"), 	NULL, pressed_channel_edit, CHN_IMAGE, "<RadioItem>" },
		{ _("/Channels/Edit Alpha"), 	NULL, pressed_channel_edit, CHN_ALPHA, _("/Channels/Edit Image") },
		{ _("/Channels/Edit Selection"), NULL, pressed_channel_edit, CHN_SEL, _("/Channels/Edit Image") },
		{ _("/Channels/Edit Mask"), 	NULL, pressed_channel_edit, CHN_MASK, _("/Channels/Edit Image") },
		{ _("/Channels/sep1"),		NULL, NULL,0, "<Separator>" },
		{ _("/Channels/Hide Image"),	NULL, pressed_channel_toggle, 1, "<CheckItem>" },
		{ _("/Channels/Disable Alpha"), NULL, pressed_channel_disable, CHN_ALPHA, "<CheckItem>" },
		{ _("/Channels/Disable Selection"), NULL, pressed_channel_disable, CHN_SEL, "<CheckItem>" },
		{ _("/Channels/Disable Mask"), 	NULL, pressed_channel_disable, CHN_MASK, "<CheckItem>" },
		{ _("/Channels/sep1"),		NULL, NULL,0, "<Separator>" },
		{ _("/Channels/Couple RGBA Operations"), NULL, pressed_RGBA_toggle, 0, "<CheckItem>" },
		{ _("/Channels/Threshold ..."), NULL, pressed_threshold, 0, NULL },
		{ _("/Channels/sep1"),		NULL, NULL,0, "<Separator>" },
		{ _("/Channels/View Alpha as an Overlay"), NULL, pressed_channel_toggle, 0, "<CheckItem>" },
		{ _("/Channels/Configure Overlays ..."), NULL, pressed_channel_config_overlay, 0, NULL },

		{ _("/Layers"),		 	NULL, 		NULL, 0, "<Branch>" },
		{ _("/Layers/tear"),		NULL,		NULL, 0, "<Tearoff>" },
		{ _("/Layers/Save"),		"<shift><control>S", layer_press_save, 0, NULL },
		{ _("/Layers/Save As ..."),	NULL,		layer_press_save_as, 0, NULL },
		{ _("/Layers/Save Composite Image ..."), NULL,	layer_press_save_composite, 0, NULL },
		{ _("/Layers/Remove All Layers ..."), NULL,	layer_press_remove_all, 0, NULL },
		{ _("/Layers/sep1"),  	 	NULL,		NULL, 0, "<Separator>" },
		{ _("/Layers/Configure Animation ..."),		NULL, pressed_animate_window,0, NULL },
		{ _("/Layers/Preview Animation ..."), NULL,	ani_but_preview, 0, NULL },
		{ _("/Layers/Set key frame ..."), NULL,		pressed_set_key_frame, 0, NULL },
		{ _("/Layers/Remove all key frames ..."), NULL, pressed_remove_key_frames, 0, NULL },

		{ _("/_Help"),			NULL,		NULL,0, "<LastBranch>" },
		{ _("/Help/Documentation"),	NULL,		pressed_docs,0, NULL },
		{ _("/Help/About"),		"F1",		pressed_help,0, NULL }
	};

	char
	*item_undo[] = {_("/Edit/Undo"), _("/File/Export Undo Images ..."),
			_("/File/Export Undo Images (reversed) ..."),
			NULL},
	*item_redo[] = {_("/Edit/Redo"),
			NULL},
	*item_need_marquee[] = {_("/Selection/Select None (Esc)"),
			NULL},
	*item_need_selection[] = { _("/Edit/Cut"), _("/Edit/Copy"), _("/Selection/Fill Selection"),
				_("/Selection/Outline Selection"), _("/Selection/Fill Ellipse"),
				_("/Selection/Outline Ellipse"),
			NULL},
	*item_crop[] = { _("/Image/Crop"),
			NULL},
	*item_need_clipboard[] = {_("/Edit/Paste"), _("/Edit/Paste To Centre"),
			_("/Edit/Paste To New Layer"),
			_("/Selection/Flip Horizontally"), _("/Selection/Flip Vertically"),
			_("/Selection/Rotate Clockwise"), _("/Selection/Rotate Anti-Clockwise"),
			_("/Selection/Move Alpha to Mask"),
			_("/Selection/Mask Colour A,B"), _("/Selection/Clear Mask"),
			_("/Selection/Unmask Colour A,B"), _("/Selection/Mask All Colours"),
			NULL},
	*item_help[] = {_("/Help/About"), NULL},
	*item_prefs[] = {_("/Image/Preferences ..."), NULL},
	*item_only_24[] = { _("/Image/Convert To Indexed"), _("/Palette/Create Quantized (DL1)"),
			_("/Palette/Create Quantized (DL3)"), _("/Palette/Create Quantized (Wu)"),
			NULL },
	*item_not_indexed[] = { _("/Effects/Edge Detect"), _("/Effects/Emboss"),
			_("/Effects/Sharpen ..."), _("/Effects/Unsharp Mask ..."),
			_("/Effects/Soften ..."), _("/Effects/Gaussian Blur ..."),
			NULL },
	*item_only_indexed[] = { _("/Image/Convert To RGB"), _("/Effects/Bacteria ..."),
			_("/Palette/Merge Duplicate Colours"), _("/Palette/Remove Unused Colours"),
			_("/File/Export ASCII Art ..."), _("/File/Export Animated GIF ..."),
			NULL },
	*item_cline[] = {_("/View/Command Line Window"),
			NULL},
	*item_view[] = {_("/View/View Window"),
			NULL},
	*item_layer[] = {_("/View/Layers Window"),
			NULL},
	*item_lasso[] = {_("/Selection/Lasso Selection"), _("/Selection/Lasso Selection Cut"),
			_("/Edit/Cut"), _("/Edit/Copy"),
			_("/Selection/Fill Selection"), _("/Selection/Outline Selection"),
			NULL},
	*item_frames[] = {_("/Frames"), NULL},
	*item_alphablend[] = {_("/Selection/Alpha Blend A,B"), NULL},
	*item_chann_x[] = {_("/Channels/Edit Image"), _("/Channels/Edit Alpha"),
			_("/Channels/Edit Selection"), _("/Channels/Edit Mask"),
			NULL},
	*item_chan_del[] = {  _("/Channels/Delete ..."),NULL },
	*item_chan_dis[] = { _("/Channels/Hide Image"), _("/Channels/Disable Alpha"),
			_("/Channels/Disable Selection"), _("/Channels/Disable Mask"), NULL }
	;



	gdk_rgb_init();
	show_paste = inifile_get_gboolean( "pasteToggle", TRUE );
	mem_jpeg_quality = inifile_get_gint32( "jpegQuality", 85 );
	q_quit = inifile_get_gboolean( "quitToggle", TRUE );
	chequers_optimize = inifile_get_gboolean( "optimizeChequers", TRUE );

	toolbar_boxes[TOOLBAR_MAIN] = NULL;		// Needed as test to avoid segfault in toolbar.c

	for ( i=0; i<STATUS_ITEMS; i++ )
	{
		sprintf(txt, "status%iToggle", i);
		status_on[i] = inifile_get_gboolean(txt, TRUE);
	}

	mem_background = inifile_get_gint32("backgroundGrey", 180 );
	mem_undo_limit = inifile_get_gint32("undoMBlimit", 32 );
	mem_nudge = inifile_get_gint32("pixelNudge", 8 );

	accel_group = gtk_accel_group_new ();
	item_factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR,"<main>", accel_group);
	gtk_item_factory_create_items_ac(item_factory, (sizeof(menu_items)/sizeof((menu_items)[0])),
		menu_items, NULL, 2);

	menu_recent[0] = gtk_item_factory_get_item(item_factory, _("/File/sep2") );
	for ( i=1; i<21; i++ )
	{
		sprintf(txt, _("/File/%i"), i);
		menu_recent[i] = gtk_item_factory_get_widget(item_factory, txt );
	}
	menu_recent[21] = NULL;

	pop_men_dis( item_factory, item_undo, menu_undo );
	pop_men_dis( item_factory, item_redo, menu_redo );
	pop_men_dis( item_factory, item_need_marquee, menu_need_marquee );
	pop_men_dis( item_factory, item_need_selection, menu_need_selection );
	pop_men_dis( item_factory, item_need_clipboard, menu_need_clipboard );
	pop_men_dis( item_factory, item_crop, menu_crop );
	pop_men_dis( item_factory, item_help, menu_help );
	pop_men_dis( item_factory, item_prefs, menu_prefs );
	pop_men_dis( item_factory, item_frames, menu_frames );
	pop_men_dis( item_factory, item_only_24, menu_only_24 );
	pop_men_dis( item_factory, item_not_indexed, menu_not_indexed );
	pop_men_dis( item_factory, item_only_indexed, menu_only_indexed );
	pop_men_dis( item_factory, item_cline, menu_cline );
	pop_men_dis( item_factory, item_view, menu_view );
	pop_men_dis( item_factory, item_layer, menu_layer );
	pop_men_dis( item_factory, item_lasso, menu_lasso );
	pop_men_dis( item_factory, item_alphablend, menu_alphablend );
	pop_men_dis( item_factory, item_chann_x, menu_chann_x );
	pop_men_dis( item_factory, item_chan_del, menu_chan_del );
	pop_men_dis( item_factory, item_chan_dis, menu_chan_dis );

	for (i = 1; i <= 12; i++)	// Set up save clipboard slots
	{
		snprintf( txt, 60, "%s/%i", _("/Edit/Save Clipboard"), i );
		men_dis_add( gtk_item_factory_get_item(item_factory, txt), menu_need_clipboard );
	}

	gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM( gtk_item_factory_get_item(item_factory,
		_("/Channels/Couple RGBA Operations") ) ),
		inifile_get_gboolean("couple_RGBA", TRUE ) );

	gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM( gtk_item_factory_get_item(item_factory,
		_("/View/Focus View Window") ) ),
		inifile_get_gboolean("view_focus", TRUE ) );

	canvas_image_centre = inifile_get_gboolean("imageCentre", TRUE);
	gtk_check_menu_item_set_active( GTK_CHECK_MENU_ITEM( gtk_item_factory_get_item(item_factory,
		_("/View/Centralize Image") ) ), canvas_image_centre );


	toolbar_menu_widgets[1] = gtk_item_factory_get_item(item_factory,
			_("/View/Show Main Toolbar") );
	toolbar_menu_widgets[2] = gtk_item_factory_get_item(item_factory,
			_("/View/Show Tools Toolbar") );
	toolbar_menu_widgets[3] = gtk_item_factory_get_item(item_factory,
			_("/View/Show Settings Toolbar") );
	toolbar_menu_widgets[4] = gtk_item_factory_get_item(item_factory,
			_("/View/Show Palette") );
	toolbar_menu_widgets[5] = gtk_item_factory_get_item(item_factory,
			_("/View/Show Status Bar") );

	mem_continuous = inifile_get_gboolean( "continuousPainting", TRUE );
	mem_undo_opacity = inifile_get_gboolean( "opacityToggle", TRUE );
	smudge_mode = inifile_get_gboolean("smudgeOpacity", FALSE);
	mem_show_grid = inifile_get_gboolean( "gridToggle", TRUE );
	gtk_check_menu_item_set_active(
		GTK_CHECK_MENU_ITEM(
			gtk_item_factory_get_item(item_factory, _("/View/Show zoom grid") )
					), mem_show_grid );

	mem_grid_min = inifile_get_gint32("gridMin", 8 );
	mem_grid_rgb[0] = inifile_get_gint32("gridR", 50 );
	mem_grid_rgb[1] = inifile_get_gint32("gridG", 50 );
	mem_grid_rgb[2] = inifile_get_gint32("gridB", 50 );


///	MAIN WINDOW

	main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_usize(main_window, 100, 100);		// Set minimum width/height
	gtk_window_set_default_size( GTK_WINDOW(main_window),
		inifile_get_gint32("window_w", 630 ), inifile_get_gint32("window_h", 400 ) );
	gtk_widget_set_uposition( main_window,
		inifile_get_gint32("window_x", 0 ), inifile_get_gint32("window_y", 0 ) );
	gtk_window_set_title (GTK_WINDOW (main_window), VERSION );

	gtk_drag_dest_set (main_window, GTK_DEST_DEFAULT_ALL, target_table, 1, GDK_ACTION_MOVE);
	gtk_signal_connect (GTK_OBJECT (main_window), "drag_data_received",
		GTK_SIGNAL_FUNC (drag_n_drop_received), NULL);		// Drag 'n' Drop guff

	vbox_main = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_main);
	gtk_container_add (GTK_CONTAINER (main_window), vbox_main);

	menubar1 = gtk_item_factory_get_widget(item_factory,"<main>");
	gtk_accel_group_lock( accel_group );	// Stop dynamic allocation of accelerators during runtime
	gtk_window_add_accel_group(GTK_WINDOW(main_window), accel_group);

	gtk_widget_show (menubar1);
	gtk_box_pack_start (GTK_BOX (vbox_main), menubar1, FALSE, FALSE, 0);


// we need to realize the window because we use pixmaps for 
// items on the toolbar in the context of it
	gtk_widget_realize( main_window );


	toolbar_init(vbox_main);


///	PALETTE

	hbox_bottom = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox_bottom);
	gtk_box_pack_start (GTK_BOX (vbox_main), hbox_bottom, TRUE, TRUE, 0);

	toolbar_palette_init(hbox_bottom);


	vbox_right = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_right);
	gtk_box_pack_start (GTK_BOX (hbox_bottom), vbox_right, TRUE, TRUE, 0);


///	DRAWING AREA

	main_vsplit = gtk_hpaned_new ();
	gtk_widget_show (main_vsplit);
	gtk_widget_ref(main_vsplit);
	gtk_object_sink(GTK_OBJECT(main_vsplit));

	main_hsplit = gtk_vpaned_new ();
	gtk_widget_show (main_hsplit);
	gtk_widget_ref(main_hsplit);
	gtk_object_sink(GTK_OBJECT(main_hsplit));

	main_split = main_vsplit;

//	VIEW WINDOW

	vw_scrolledwindow = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_show (vw_scrolledwindow);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(vw_scrolledwindow),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_widget_ref(vw_scrolledwindow);
	gtk_object_sink(GTK_OBJECT(vw_scrolledwindow));

	vw_drawing = gtk_drawing_area_new ();
	gtk_widget_set_usize( vw_drawing, 1, 1 );
	gtk_widget_show( vw_drawing );
	gtk_scrolled_window_add_with_viewport( GTK_SCROLLED_WINDOW(vw_scrolledwindow), vw_drawing);

	init_view(vw_drawing);

//	MAIN WINDOW

	drawing_canvas = gtk_drawing_area_new ();
	gtk_widget_set_usize( drawing_canvas, 48, 48 );
	gtk_widget_show( drawing_canvas );

	scrolledwindow_canvas = gtk_scrolled_window_new (NULL, NULL);
	gtk_widget_show (scrolledwindow_canvas);
	gtk_box_pack_start (GTK_BOX (vbox_right), scrolledwindow_canvas, TRUE, TRUE, 0);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow_canvas),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_signal_connect_object(
		GTK_OBJECT(
			GTK_HSCROLLBAR(
				GTK_SCROLLED_WINDOW(scrolledwindow_canvas)->hscrollbar
			)->scrollbar.range.adjustment
		),
		"value_changed", GTK_SIGNAL_FUNC (main_scroll_changed), NULL );
	gtk_signal_connect_object(
		GTK_OBJECT(
			GTK_VSCROLLBAR(
				GTK_SCROLLED_WINDOW(scrolledwindow_canvas)->vscrollbar
			)->scrollbar.range.adjustment
		),
		"value_changed", GTK_SIGNAL_FUNC (main_scroll_changed), NULL );

	gtk_scrolled_window_add_with_viewport( GTK_SCROLLED_WINDOW(scrolledwindow_canvas),
		drawing_canvas);

	gtk_signal_connect( GTK_OBJECT(drawing_canvas), "configure_event",
		GTK_SIGNAL_FUNC (configure_canvas), NULL );
	gtk_signal_connect( GTK_OBJECT(drawing_canvas), "expose_event",
		GTK_SIGNAL_FUNC (expose_canvas), NULL );
	gtk_signal_connect( GTK_OBJECT(drawing_canvas), "button_press_event",
		GTK_SIGNAL_FUNC (canvas_button), NULL );
	gtk_signal_connect( GTK_OBJECT(drawing_canvas), "button_release_event",
		GTK_SIGNAL_FUNC (canvas_button), NULL );
	gtk_signal_connect( GTK_OBJECT(drawing_canvas), "motion_notify_event",
		GTK_SIGNAL_FUNC (canvas_motion), NULL );
	gtk_signal_connect( GTK_OBJECT(drawing_canvas), "enter_notify_event",
		GTK_SIGNAL_FUNC (canvas_enter), NULL );
	gtk_signal_connect( GTK_OBJECT(drawing_canvas), "leave_notify_event",
		GTK_SIGNAL_FUNC (canvas_left), NULL );
#if GTK_MAJOR_VERSION == 2
	gtk_signal_connect( GTK_OBJECT(drawing_canvas), "scroll_event",
		GTK_SIGNAL_FUNC (canvas_scroll_gtk2), NULL );
#endif

	gtk_widget_set_events (drawing_canvas, GDK_ALL_EVENTS_MASK);
	gtk_widget_set_extension_events (drawing_canvas, GDK_EXTENSION_EVENTS_CURSOR);

////	STATUS BAR

	hbox_bar = gtk_hbox_new (FALSE, 0);
	if ( toolbar_status[TOOLBAR_STATUS] ) gtk_widget_show (hbox_bar);
	gtk_box_pack_end (GTK_BOX (vbox_right), hbox_bar, FALSE, FALSE, 0);


	for ( i=0; i<STATUS_ITEMS; i++ )
	{
		label_bar[i] = gtk_label_new("");
		gtk_widget_show (label_bar[i]);
		if ( i==STATUS_GEOMETRY || i==STATUS_PIXELRGB || i==STATUS_SELEGEOM ) f = 0;	// LEFT
		if ( i==STATUS_CURSORXY || i==STATUS_UNDOREDO ) f = 0.5;	// MIDDLE
		gtk_misc_set_alignment (GTK_MISC (label_bar[i]), f, 0);
	}
	for ( i=0; i<STATUS_ITEMS; i++ )
	{
		if ( i<3 ) gtk_box_pack_start (GTK_BOX (hbox_bar), label_bar[i], FALSE, FALSE, 0);
		else	gtk_box_pack_end (GTK_BOX (hbox_bar), label_bar[7-i], FALSE, FALSE, 0);
	}
	if ( status_on[STATUS_CURSORXY] ) gtk_widget_set_usize(label_bar[STATUS_CURSORXY], 90, -2);
	if ( status_on[STATUS_UNDOREDO] ) gtk_widget_set_usize(label_bar[STATUS_UNDOREDO], 50, -2);
	gtk_label_set_text( GTK_LABEL(label_bar[STATUS_UNDOREDO]), "0+0" );



	/* To prevent statusbar wobbling */
	gtk_widget_size_request(hbox_bar, &req);
	gtk_widget_set_usize(hbox_bar, -1, req.height);


/////////	End of main window widget setup

	gtk_signal_connect_object (GTK_OBJECT (main_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_event), NULL);
	gtk_signal_connect_object (GTK_OBJECT (main_window), "key_press_event",
		GTK_SIGNAL_FUNC (handle_keypress), NULL);

	men_item_state( menu_frames, FALSE );
	men_item_state( menu_undo, FALSE );
	men_item_state( menu_redo, FALSE );
	men_item_state( menu_need_marquee, FALSE );
	men_item_state( menu_need_selection, FALSE );
	men_item_state( menu_need_clipboard, FALSE );

	recent_files = inifile_get_gint32( "recentFiles", 10 );
	mtMIN( recent_files, recent_files, 20 )
	mtMAX( recent_files, recent_files, 0 )

	update_recent_files();
	toolbar_boxes[TOOLBAR_STATUS] = hbox_bar;	// Hide status bar
	main_hidden[0] = menubar1;			// Hide menu bar

	view_hide();					// Hide paned view initially

	gtk_widget_show (main_window);

	gtk_widget_grab_focus(scrolledwindow_canvas);	// Stops first icon in toolbar being selected
	gdk_window_raise( main_window->window );

	icon_pix = gdk_pixmap_create_from_xpm_d( main_window->window, NULL, NULL, icon_xpm );
	gdk_window_set_icon( main_window->window, NULL, icon_pix, NULL );

	set_cursor();
	init_status_bar();

	snprintf(txt, 250, "%s%c.clipboard", get_home_directory(), DIR_SEP);
	snprintf(mem_clip_file, 250, "%s", inifile_get("clipFilename", txt));

	if (files_passed > 1)
		pressed_cline(NULL, NULL);
	else
		men_item_state(menu_cline, FALSE);

	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );

	dash_gc = gdk_gc_new(drawing_canvas->window);		// Set up gc for polygon marquee
	gdk_gc_set_background(dash_gc, &cbg);
	gdk_gc_set_foreground(dash_gc, &cfg);
	gdk_gc_set_line_attributes( dash_gc, 1, GDK_LINE_DOUBLE_DASH, GDK_CAP_NOT_LAST, GDK_JOIN_MITER);

	init_tablet();						// Set up the tablet

	toolbar_showhide();
	if (viewer_mode) toggle_view(NULL, NULL);
}

void spot_undo(int mode)
{
	mem_undo_next(mode);		// Do memory stuff for undo
	update_menus();			// Update menu undo issues
}

#ifdef U_NLS
void setup_language()			// Change language
{
	char *txt = inifile_get( "languageSETTING", "system" ), txt2[64];

	if ( strcmp( "system", txt ) != 0 )
	{
		snprintf( txt2, 60, "LANGUAGE=%s", txt );
		putenv( txt2 );
		snprintf( txt2, 60, "LANG=%s", txt );
		putenv( txt2 );
		snprintf( txt2, 60, "LC_ALL=%s", txt );
		putenv( txt2 );
	}
#if GTK_MAJOR_VERSION == 1
	else	txt="";

	setlocale(LC_ALL, txt);
#endif
#if GTK_MAJOR_VERSION == 2
	gtk_set_locale();	// GTK+1 hates this - it really slows things down
#endif
}
#endif

void update_titlebar()		// Update filename in titlebar
{
	char txt[300], txt2[600], *extra = "-";

#if GTK_MAJOR_VERSION == 2
	cleanse_txt( txt2, mem_filename );		// Clean up non ASCII chars
#else
	strcpy( txt2, mem_filename );
#endif

	if ( mem_changed == 1 ) extra = _("(Modified)");

	snprintf( txt, 290, "%s %s %s", VERSION, extra, txt2 );

	gtk_window_set_title (GTK_WINDOW (main_window), txt );
}

void notify_changed()		// Image/palette has just changed - update vars as needed
{
	if ( mem_changed != 1 )
	{
		mem_changed = 1;
		update_titlebar();
	}
}

void notify_unchanged()		// Image/palette has just been unchanged (saved) - update vars as needed
{
	if ( mem_changed != 0 )
	{
		mem_changed = 0;
		update_titlebar();
	}
}


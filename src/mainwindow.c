/*	mainwindow.c
	Copyright (C) 2004-2008 Mark Tyler and Dmitry Groshev

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
#include "shifter.h"
#include "spawn.h"
#include "font.h"


char *channames[NUM_CHANNELS + 1], *allchannames[NUM_CHANNELS + 1];

///	INIFILE ENTRY LISTS

typedef struct {
	char *name;
	int *var;
	int defv;
} inilist;

static inilist ini_bool[] = {
	{ "layermainToggle",	&show_layers_main,	FALSE },
	{ "sharperReduce",	&sharper_reduce,	FALSE },
	{ "tablet_USE",		&tablet_working,	FALSE },
	{ "tga565",		&tga_565,		FALSE },
	{ "tgaDefdir",		&tga_defdir,		FALSE },
	{ "disableTransparency", &opaque_view,		FALSE },
	{ "smudgeOpacity",	&smudge_mode,		FALSE },
	{ "undoableLoad",	&undo_load,		FALSE },
	{ "couple_RGBA",	&RGBA_mode,		TRUE  },
	{ "gridToggle",		&mem_show_grid,		TRUE  },
	{ "optimizeChequers",	&chequers_optimize,	TRUE  },
	{ "quitToggle",		&q_quit,		TRUE  },
	{ "continuousPainting",	&mem_continuous,	TRUE  },
	{ "opacityToggle",	&mem_undo_opacity,	TRUE  },
	{ "imageCentre",	&canvas_image_centre,	TRUE  },
	{ "view_focus",		&vw_focus_on,		TRUE  },
	{ "pasteToggle",	&show_paste,		TRUE  },
	{ "cursorToggle",	&cursor_tool,		TRUE  },
#if STATUS_ITEMS != 5
#error Wrong number of "status?Toggle" inifile items defined
#endif
	{ "status0Toggle",	status_on + 0,		TRUE  },
	{ "status1Toggle",	status_on + 1,		TRUE  },
	{ "status2Toggle",	status_on + 2,		TRUE  },
	{ "status3Toggle",	status_on + 3,		TRUE  },
	{ "status4Toggle",	status_on + 4,		TRUE  },
	{ NULL,			NULL }
};

static inilist ini_int[] = {
	{ "jpegQuality",	&jpeg_quality,		85  },
	{ "pngCompression",	&png_compression,	9   },
	{ "tgaRLE",		&tga_RLE,		0   },
	{ "jpeg2000Rate",	&jp2_rate,		1   },
	{ "silence_limit",	&silence_limit,		18  },
	{ "gradientOpacity",	&grad_opacity,		128 },
	{ "gridMin",		&mem_grid_min,		8   },
	{ "undoMBlimit",	&mem_undo_limit,	32  },
	{ "backgroundGrey",	&mem_background,	180 },
	{ "pixelNudge",		&mem_nudge,		8   },
	{ "recentFiles",	&recent_files,		10  },
	{ "lastspalType",	&spal_mode,		2   },
	{ "panSize",		&max_pan,		128 },
	{ "undoDepth",		&mem_undo_depth,	DEF_UNDO },
	{ NULL,			NULL }
};

#include "graphics/icon.xpm"

GtkWidget *main_window, *main_vsplit, *main_hsplit, *main_split,
	*drawing_palette, *drawing_canvas, *vbox_right, *vw_scrolledwindow,
	*scrolledwindow_canvas, *main_hidden[4],

	*menu_undo[5], *menu_redo[5], *menu_crop[5], *menu_need_marquee[10],
	*menu_need_selection[20], *menu_need_clipboard[30], *menu_only_24[10],
	*menu_not_indexed[10], *menu_only_indexed[10], *menu_lasso[15],
	*menu_alphablend[2], *menu_chan_del[2], *menu_rgba[2],
	*menu_widgets[TOTAL_MENU_IDS];

int view_image_only, viewer_mode, drag_index, q_quit, cursor_tool;
int files_passed, file_arg_start, drag_index_vals[2], cursor_corner;
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

/* Enable or disable menu items */
void men_item_state( GtkWidget *menu_items[], gboolean state )
{
	while (*menu_items) gtk_widget_set_sensitive(*menu_items++, state);
}

/* Add widget to disable list */
static void men_dis_add( GtkWidget *widget, GtkWidget *menu_items[] )
{
	while (*menu_items++);
	*(menu_items - 1) = widget;
	*menu_items = NULL;
}

static void pressed_swap_AB( GtkMenuItem *menu_item, gpointer user_data )
{
	mem_swap_cols();
	if (mem_channel == CHN_IMAGE) update_cols();
	else pressed_opacity(channel_col_A[mem_channel]);
}

static void pressed_load_recent( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	int change;
	char txt[64], *c;

	sprintf( txt, "file%i", item );
	c = inifile_get( txt, "." );

	if ( layers_total==0 )
		change = check_for_changes();
	else
		change = check_layers_for_changes();

	if ( change == 2 || change == -10 )
		do_a_load(c);		// Load requested file
}

static void pressed_crop( GtkMenuItem *menu_item, gpointer user_data )
{
	int res, x1, y1, x2, y2;

	mtMIN( x1, marq_x1, marq_x2 )
	mtMIN( y1, marq_y1, marq_y2 )
	mtMAX( x2, marq_x1, marq_x2 )
	mtMAX( y2, marq_y1, marq_y2 )

	if ( marq_status != MARQUEE_DONE ) return;
	if ( x1==0 && x2>=(mem_width-1) && y1==0 && y2>=(mem_height-1) ) return;

	res = mem_image_resize(x2 - x1 + 1, y2 - y1 + 1, -x1, -y1, 0);

	if (!res)
	{
		pressed_select_none(NULL, NULL);
		gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );
		canvas_undo_chores();
	}
	else memory_errors(res);
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

static void pressed_select_all( GtkMenuItem *menu_item, gpointer user_data )
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

static void pressed_remove_unused( GtkMenuItem *menu_item, gpointer user_data )
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
		mem_undo_prepare();

		if ( mem_col_A >= mem_cols ) mem_col_A = 0;
		if ( mem_col_B >= mem_cols ) mem_col_B = 0;
		init_pal();
		update_all_views();
	}
}

static void pressed_default_pal( GtkMenuItem *menu_item, gpointer user_data )
{
	spot_undo(UNDO_PAL);
	mem_pal_copy( mem_pal, mem_pal_def );
	mem_cols = mem_pal_def_i;
	init_pal();
	update_all_views();
}

static void pressed_remove_duplicates( GtkMenuItem *menu_item, gpointer user_data )
{
	int dups;
	char *mess;

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
				mess = g_strdup_printf(_("The palette contains %i colours that have identical RGB values.  Do you really want to merge them into one index and realign the canvas?"), dups);
				if ( alert_box( _("Warning"), mess, _("Yes"), _("No"), NULL ) == 1 )
				{
					spot_undo(UNDO_XPAL);

					remove_duplicates();
					mem_undo_prepare();
					init_pal();
					update_all_views();
				}
				g_free(mess);
			}
		}
	}
}

static void pressed_dither_A()
{
	mem_find_dither(mem_col_A24.red, mem_col_A24.green, mem_col_A24.blue);
	update_cols();
}

static void pressed_mask( GtkMenuItem *menu_item, gpointer user_data, gint item )
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
	if (mem_filename[0]) file_selector( FS_EXPORT_GIF );
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
	int res = -2, fflags = file_formats[settings->ftype].flags;
	char *mess = NULL, *f8;

	/* Mismatched format - raise an error right here */
	if (!(fflags & FF_SAVE_MASK))
	{
		int maxc = 0;
		char *fform = NULL, *fname = file_formats[settings->ftype].name;

		/* RGB to indexed */
		if (mem_img_bpp == 3) fform = _("RGB");
		/* Indexed to RGB */
		else if (!(fflags & FF_IDX)) fform = _("indexed");
		/* More than 16 colors */
		else if (fflags & FF_16) maxc = 16;
		/* More than 2 colors */
		else maxc = 2;
		/* Build message */
		if (fform) mess = g_strdup_printf(_("You are trying to save an %s image to an %s file which is not possible.  I would suggest you save with a PNG extension."),
			fform, fname);
		else mess = g_strdup_printf(_("You are trying to save an %s file with a palette of more than %d colours.  Either use another format or reduce the palette to %d colours."),
			fname, maxc, maxc);
	}
	else
	{
		/* Prepare to save image */
		memcpy(settings->img, mem_img, sizeof(chanlist));
		settings->pal = mem_pal;
		settings->width = mem_width;
		settings->height = mem_height;
		settings->bpp = mem_img_bpp;
		settings->colors = mem_cols;

		res = save_image(filename, settings);
	}
	if (res < 0)
	{
		if (res == -1)
		{
			f8 = gtkuncpy(NULL, filename, 0);
			mess = g_strdup_printf(_("Unable to save file: %s"), f8);
			g_free(f8);
		}
		else if ((res == WRONG_FORMAT) && (settings->ftype == FT_XPM))
			mess = g_strdup(_("You are trying to save an XPM file with more than 4096 colours.  Either use another format or posterize the image to 4 bits, or otherwise reduce the number of colours."));
		if (mess)
		{
			alert_box( _("Error"), mess, _("OK"), NULL, NULL );
			g_free(mess);
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

	while (mem_filename[0])
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

char mem_clip_file[PATHBUF];

void load_clip( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	char clip[PATHBUF];
	int i;

	snprintf(clip, PATHBUF, "%s%i", mem_clip_file, item);
	i = load_image(clip, FS_CLIP_FILE, FT_PNG);

	if ( i!=1 ) alert_box( _("Error"), _("Unable to load clipboard"), _("OK"), NULL, NULL );
	else text_paste = TEXT_PASTE_NONE;

	if ( tool_type == TOOL_SELECT && marq_status >= MARQUEE_PASTE )
		pressed_select_none( NULL, NULL );

	if ( tool_type == TOOL_POLYGON && poly_status >= POLY_NONE )
		pressed_select_none( NULL, NULL );

	update_menus();

	if (MEM_BPP >= mem_clip_bpp) pressed_paste_centre( NULL, NULL );
}

void save_clip( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	ls_settings settings;
	char clip[PATHBUF];
	int i;

	/* Prepare settings */
	init_ls_settings(&settings, NULL);
	settings.mode = FS_CLIP_FILE;
	settings.ftype = FT_PNG;
	memcpy(settings.img, mem_clip.img, sizeof(chanlist));
	settings.pal = mem_pal;
	settings.width = mem_clip_w;
	settings.height = mem_clip_h;
	settings.bpp = mem_clip_bpp;
	settings.colors = mem_cols;

	snprintf(clip, PATHBUF, "%s%i", mem_clip_file, item);
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

	grad_def_update();
	toolbar_update_settings();
	if ((tool_type == TOOL_GRADIENT) && grad_opacity)
		gtk_widget_queue_draw(drawing_canvas);
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

	if ( drawing_canvas ) gtk_widget_queue_draw( drawing_canvas );
}

static gboolean delete_event( GtkWidget *widget, GdkEvent *event, gpointer data );
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
		zoom_in(); break;
	case ACT_ZOOM_OUT:
		zoom_out(); break;
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
		break;
	case ACT_VIEW:
		toggle_view( NULL, NULL );
		break;
	case ACT_VWZOOM_IN:
		if (vw_zoom >= 1) vw_align_size(vw_zoom + 1);
		else vw_align_size(1.0 / (rint(1.0 / vw_zoom) - 1));
		break;
	case ACT_VWZOOM_OUT:
		if (vw_zoom > 1) vw_align_size(vw_zoom - 1);
		else vw_align_size(1.0 / (rint(1.0 / vw_zoom) + 1));
		break;
	default: return FALSE;
	}
	return TRUE;
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

typedef struct {
	char *actname;
	int action, key, kmask, kflags;
} key_action;

static key_action main_keys[] = {
	{"QUIT",	ACT_QUIT, GDK_q, 0, 0},
	{"ZOOM_IN",	ACT_ZOOM_IN, GDK_plus, _CS, 0},
	{"",		ACT_ZOOM_IN, GDK_KP_Add, _CS, 0},
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
	{"CMDLINE",	ACT_CMDLINE, GDK_c, _CSA, 0},
#if GTK_MAJOR_VERSION == 2
	{"PATTERN",	ACT_PATTERN, GDK_F2, _CSA, 0},
	{"BRUSH",	ACT_BRUSH, GDK_F3, _CSA, 0},
#endif
// Only GTK+2 needs it here as in full screen mode GTK+2 does not handle menu keyboard shortcuts
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
	{"OPAC_01",	ACT_OPAC_01, GDK_KP_1, _CS, _C},
	{"",		ACT_OPAC_01, GDK_1, _CS, _C},
	{"OPAC_02",	ACT_OPAC_02, GDK_KP_2, _CS, _C},
	{"",		ACT_OPAC_02, GDK_2, _CS, _C},
	{"OPAC_03",	ACT_OPAC_03, GDK_KP_3, _CS, _C},
	{"",		ACT_OPAC_03, GDK_3, _CS, _C},
	{"OPAC_04",	ACT_OPAC_04, GDK_KP_4, _CS, _C},
	{"",		ACT_OPAC_04, GDK_4, _CS, _C},
	{"OPAC_05",	ACT_OPAC_05, GDK_KP_5, _CS, _C},
	{"",		ACT_OPAC_05, GDK_5, _CS, _C},
	{"OPAC_06",	ACT_OPAC_06, GDK_KP_6, _CS, _C},
	{"",		ACT_OPAC_06, GDK_6, _CS, _C},
	{"OPAC_07",	ACT_OPAC_07, GDK_KP_7, _CS, _C},
	{"",		ACT_OPAC_07, GDK_7, _CS, _C},
	{"OPAC_08",	ACT_OPAC_08, GDK_KP_8, _CS, _C},
	{"",		ACT_OPAC_08, GDK_8, _CS, _C},
	{"OPAC_09",	ACT_OPAC_09, GDK_KP_9, _CS, _C},
	{"",		ACT_OPAC_09, GDK_9, _CS, _C},
	{"OPAC_1",	ACT_OPAC_1, GDK_KP_0, _CS, _C},
	{"",		ACT_OPAC_1, GDK_0, _CS, _C},
	{"OPAC_P",	ACT_OPAC_P, GDK_plus, _CS, _C},
	{"",		ACT_OPAC_P, GDK_KP_Add, _CS, _C},
	{"OPAC_M",	ACT_OPAC_M, GDK_minus, _CS, _C},
	{"",		ACT_OPAC_M, GDK_KP_Subtract, _CS, _C},
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
	{"ARROW3",	ACT_ARROW3, GDK_s, _C, 0},
	{"A_PREV",	ACT_A_PREV, GDK_bracketleft, _CS, 0},
	{"A_NEXT",	ACT_A_NEXT, GDK_bracketright, _CS, 0},
	{"B_PREV",	ACT_B_PREV, GDK_bracketleft, _CS, _S},
	{"",		ACT_B_PREV, GDK_braceleft, _CS, _S},
	{"B_NEXT",	ACT_B_NEXT, GDK_bracketright, _CS, _S},
	{"",		ACT_B_NEXT, GDK_braceright, _CS, _S},
	{"TO_IMAGE",	ACT_TO_IMAGE, GDK_KP_1, _CS, _S},
	{"",		ACT_TO_IMAGE, GDK_1, _CS, _S},
	{"TO_ALPHA",	ACT_TO_ALPHA, GDK_KP_2, _CS, _S},
	{"",		ACT_TO_ALPHA, GDK_2, _CS, _S},
	{"TO_SEL",	ACT_TO_SEL, GDK_KP_3, _CS, _S},
	{"",		ACT_TO_SEL, GDK_3, _CS, _S},
	{"TO_MASK",	ACT_TO_MASK, GDK_KP_4, _CS, _S},
	{"",		ACT_TO_MASK, GDK_4, _CS, _S},
	{"VWZOOM_IN",	ACT_VWZOOM_IN, GDK_plus, _CS, _S},
	{"",		ACT_VWZOOM_IN, GDK_KP_Add, _CS, _S},
	{"VWZOOM_OUT",	ACT_VWZOOM_OUT, GDK_minus, _CS, _S},
	{"",		ACT_VWZOOM_OUT, GDK_KP_Subtract, _CS, _S},
	{NULL,		0, 0, 0, 0}
};

static guint main_keycodes[sizeof(main_keys) / sizeof(key_action)];

static void fill_keycodes()
{
	int i;

	for (i = 0; main_keys[i].action; i++)
	{
		main_keycodes[i] = keyval_key(main_keys[i].key);
	}
}

/* "Tool of last resort" for when shortcuts don't work */
static void rebind_keys()
{
	fill_keycodes();
#if GTK_MAJOR_VERSION > 1
	gtk_signal_emit_by_name(GTK_OBJECT(main_window), "keys_changed", NULL);
#endif
}

int wtf_pressed(GdkEventKey *event)
{
	int i, cmatch = 0;
	guint realkey = real_key(event);
	guint lowkey = low_key(event);

	for (i = 0; main_keys[i].action; i++)
	{
		/* Relevant modifiers should match first */
		if ((event->state & main_keys[i].kmask) != main_keys[i].kflags)
			continue;
		/* Let keyval have priority; this is also a workaround for
		 * GTK2 bug #136280 */
		if (lowkey == main_keys[i].key) return (main_keys[i].action);
		/* Let keycodes match when keyvals don't */
		if (realkey == main_keycodes[i]) cmatch = main_keys[i].action;
	}
	/* Return keycode match, if any */
	return (cmatch);
}

static int check_smart_menu_keys(GdkEventKey *event);

static gboolean handle_keypress(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	int change, action;

	action = wtf_pressed(event);
	if (!action) action = check_smart_menu_keys(event);
	if (!action) return (FALSE);

#if GTK_MAJOR_VERSION == 1
	/* Return value alone doesn't stop GTK1 from running other handlers */
	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
#endif

	if (action == ACT_DUMMY) return (TRUE);
	if (check_zoom_keys(action)) return (TRUE);	// Check HOME/zoom keys

	/* Gradient tool has precedence over selection */
	if ((tool_type != TOOL_GRADIENT) && (marq_status > MARQUEE_NONE))
	{
		if (check_arrows(action) == 1)
		{
			update_sel_bar();
			update_menus();
			return TRUE;
		}
	}
	change = mem_nudge;

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
	// Channel keys, i.e. SHIFT + keypad
	case ACT_TO_IMAGE:
	case ACT_TO_ALPHA:
	case ACT_TO_SEL:
	case ACT_TO_MASK:
		pressed_channel_edit(NULL, NULL, action - ACT_TO_IMAGE + CHN_IMAGE);
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
		else if ((tool_type == TOOL_GRADIENT) &&
			(gradient[mem_channel].status != GRAD_NONE))
		{
			gradient[mem_channel].status = GRAD_NONE;
			if (grad_opacity) gtk_widget_queue_draw(drawing_canvas);
			else repaint_grad(0);
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
			mem_col_A24 = mem_pal[mem_col_A];
		}
		else if (channel_col_A[mem_channel])
			channel_col_A[mem_channel]--;
		break;
	case ACT_A_NEXT:
		if (mem_channel == CHN_IMAGE)
		{
			if (mem_col_A < mem_cols - 1) mem_col_A++;
			mem_col_A24 = mem_pal[mem_col_A];
		}
		else if (channel_col_A[mem_channel] < 255)
			channel_col_A[mem_channel]++;
		break;
	case ACT_B_PREV:
		if (mem_channel == CHN_IMAGE)
		{
			if (mem_col_B) mem_col_B--;
			mem_col_B24 = mem_pal[mem_col_B];
		}
		else if (channel_col_B[mem_channel])
			channel_col_B[mem_channel]--;
		break;
	case ACT_B_NEXT:
		if (mem_channel == CHN_IMAGE)
		{
			if (mem_col_B < mem_cols - 1) mem_col_B++;
			mem_col_B24 = mem_pal[mem_col_B];
		}
		else if (channel_col_B[mem_channel] < 255)
			channel_col_B[mem_channel]++;
		break;
	case ACT_COMMIT:
		if (marq_status >= MARQUEE_PASTE)
		{
			commit_paste(NULL);
			pen_down = 0;	// Ensure each press of enter is a new undo level
			mem_undo_prepare();
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

// !!! Call this, or let undo engine do it?
//			mem_undo_prepare();
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
			mem_undo_prepare();

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
		return (TRUE);
	}
	/* Finalize colour-change */
	if (mem_channel == CHN_IMAGE) update_cols();
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

void var_init()
{
	inilist *ilp;

	/* Load listed settings */
	for (ilp = ini_bool; ilp->name; ilp++)
		*(ilp->var) = inifile_get_gboolean(ilp->name, ilp->defv);
	for (ilp = ini_int; ilp->name; ilp++)
		*(ilp->var) = inifile_get_gint32(ilp->name, ilp->defv);
}

void string_init()
{
	char *cnames[NUM_CHANNELS + 1] =
		{ _("Image"), _("Alpha"), _("Selection"), _("Mask"), NULL };
	int i;

	for (i = 0; i < NUM_CHANNELS + 1; i++)
		allchannames[i] = channames[i] = cnames[i];
	channames[CHN_IMAGE] = "";
}

static gboolean delete_event( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	inilist *ilp;
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
		win_store_pos(main_window, "window");

		if (cline_window != NULL) delete_cline( NULL, NULL, NULL );
		if (layers_window != NULL) delete_layers_window( NULL, NULL, NULL );
			// Get rid of extra windows + remember positions

		toolbar_exit();			// Remember the toolbar settings

		/* Store listed settings */
		for (ilp = ini_bool; ilp->name; ilp++)
			inifile_set_gboolean(ilp->name, *(ilp->var));
		for (ilp = ini_int; ilp->name; ilp++)
			inifile_set_gint32(ilp->name, *(ilp->var));

		gtk_main_quit ();
		return FALSE;
	}
	else return TRUE;
}

#if GTK_MAJOR_VERSION == 2
gint canvas_scroll_gtk2( GtkWidget *widget, GdkEventScroll *event )
{
	if (inifile_get_gboolean( "scrollwheelZOOM", FALSE ))
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
	double d, stroke;
	grad_info *grad = gradient + mem_channel;

	/* Handle stroke gradients */
	if (tool_type != TOOL_GRADIENT)
	{
		/* Not a gradient stroke */
		if (!mem_gradient || (grad->status != GRAD_NONE) ||
			(grad->wmode == GRAD_MODE_NONE) ||
			(event == GDK_BUTTON_RELEASE) || !button)
			return (FALSE);

		/* Limit coordinates to canvas */
		x = x < 0 ? 0 : x >= mem_width ? mem_width - 1 : x;
		y = y < 0 ? 0 : y >= mem_height ? mem_height - 1 : y;

		/* Standing still */
		if ((tool_ox == x) && (tool_oy == y)) return (FALSE);
		if (!pen_down || (tool_type > TOOL_SPRAY)) /* Begin stroke */
		{
			grad_path = grad->xv = grad->yv = 0.0;
		}
		else /* Continue stroke */
		{
			i = x - tool_ox; j = y - tool_oy;
			stroke = sqrt(i * i + j * j);
			/* First step - anchor rear end */
			if (grad_path == 0.0)
			{
				d = tool_size * 0.5 / stroke;
				grad_x0 = tool_ox - i * d;
				grad_y0 = tool_oy - j * d;
			}
			/* Scalar product */
			d = (tool_ox - grad_x0) * (x - tool_ox) +
				(tool_oy - grad_y0) * (y - tool_oy);
			if (d < 0.0) /* Going backward - flip rear */
			{
				d = tool_size * 0.5 / stroke;
				grad_x0 = x - i * d;
				grad_y0 = y - j * d;
				grad_path += tool_size + stroke;
			}
			else /* Going forward or sideways - drag rear */
			{
				stroke = sqrt((x - grad_x0) * (x - grad_x0) +
					(y - grad_y0) * (y - grad_y0));
				d = tool_size * 0.5 / stroke;
				grad_x0 = x + (grad_x0 - x) * d;
				grad_y0 = y + (grad_y0 - y) * d;
				grad_path += stroke - tool_size * 0.5;
			}
			d = 2.0 / (double)tool_size;
			grad->xv = (x - grad_x0) * d;
			grad->yv = (y - grad_y0) * d;
		}
		return (FALSE); /* Let drawing tools run */
	}

	/* Left click sets points and picks them up again */
	if ((event == GDK_BUTTON_PRESS) && (button == 1))
	{
		/* Start anew */
		if (grad->status == GRAD_NONE)
		{
			grad->x1 = grad->x2 = x;
			grad->y1 = grad->y2 = y;
			grad->status = GRAD_END;
			grad_update(grad);
			repaint_grad(1);
		}
		/* Place starting point */
		else if (grad->status == GRAD_START)
		{
			grad->x1 = x;
			grad->y1 = y;
			grad->status = GRAD_DONE;
			grad_update(grad);
			if (grad_opacity) gtk_widget_queue_draw(drawing_canvas);
		}
		/* Place end point */
		else if (grad->status == GRAD_END)
		{
			grad->x2 = x;
			grad->y2 = y;
			grad->status = GRAD_DONE;
			grad_update(grad);
			if (grad_opacity) gtk_widget_queue_draw(drawing_canvas);
		}
		/* Pick up nearest end */
		else if (grad->status == GRAD_DONE)
		{
			if (!grad_opacity) repaint_grad(0);
			i = (x - grad->x1) * (x - grad->x1) +
				(y - grad->y1) * (y - grad->y1);
			j = (x - grad->x2) * (x - grad->x2) +
				(y - grad->y2) * (y - grad->y2);
			if (i < j)
			{
				grad->x1 = x;
				grad->y1 = y;
				grad->status = GRAD_START;
			}
			else
			{
				grad->x2 = x;
				grad->y2 = y;
				grad->status = GRAD_END;
			}
			grad_update(grad);
			if (grad_opacity)
			{
				gtk_widget_queue_draw(drawing_canvas);
				while (gtk_events_pending())
					gtk_main_iteration();
			}
			repaint_grad(1);
		}
	}

	/* Everything but left click is irrelevant when no gradient */
	else if (grad->status == GRAD_NONE);

	/* Right click deletes the gradient */
	else if (event == GDK_BUTTON_PRESS) /* button != 1 */
	{
		grad->status = GRAD_NONE;
		if (grad_opacity) gtk_widget_queue_draw(drawing_canvas);
		else repaint_grad(0);
		grad_update(grad);
	}

	/* Motion is irrelevant with gradient in place */
	else if (grad->status == GRAD_DONE);

	/* Motion drags points around */
	else if (event == GDK_MOTION_NOTIFY)
	{
		if (grad->status == GRAD_START) xx = &(grad->x1) , yy = &(grad->y1);
		else xx = &(grad->x2) , yy = &(grad->y2);
		if ((*xx != x) || (*yy != y))
		{
			repaint_grad(0);
			*xx = x; *yy = y;
			grad_update(grad);
			repaint_grad(1);
		}
	}

	/* Leave hides the dragged line */
	else if (event == GDK_LEAVE_NOTIFY) repaint_grad(0);

	return (TRUE);
}

/* Mouse event from button/motion on the canvas */
static void mouse_event(int event, int x0, int y0, guint state, guint button,
	gdouble pressure, int mflag)
{
	static int tool_fixx = -1, tool_fixy = -1;	// Fixate on axis
	GdkCursor *temp_cursor = NULL;
	GdkCursorType pointers[] = {GDK_TOP_LEFT_CORNER, GDK_TOP_RIGHT_CORNER,
		GDK_BOTTOM_LEFT_CORNER, GDK_BOTTOM_RIGHT_CORNER};
	int new_cursor;
	int i, pixel, x, y, ox, oy, tox = tool_ox, toy = tool_oy;


	ox = x = x0 < 0 ? 0 : x0 >= mem_width ? mem_width - 1 : x0;
	oy = y = y0 < 0 ? 0 : y0 >= mem_height ? mem_height - 1 : y0;

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

		if (grad_tool(event, x0, y0, state, button)) return;

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

		mem_undo_prepare();
		update_menus();

		return;
	}

	/* ****** Common click/motion handling code ****** */

	if (!mflag) /* Coordinate fixation */
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
	/* No use when moving cursor by keyboard */
	else if (event == GDK_MOTION_NOTIFY) tool_fixx = tool_fixy = -1;

	if (tool_fixx > 0) x = x0 = tool_fixx;
	if (tool_fixy > 0) y = y0 = tool_fixy;

	while ((state & _CS) == _C)	// Set colour A/B
	{
		if (button == 2) /* Auto-dither */
		{
			if ((mem_channel == CHN_IMAGE) && (mem_img_bpp == 3))
				pressed_dither_A();
			break;
		}
		if ((button != 1) && (button != 3)) break;
		pixel = get_pixel(ox, oy);
		if (mem_channel != CHN_IMAGE)
		{
			if (button == 1)
			{
				if (channel_col_A[mem_channel] == pixel) break;
				pressed_opacity(pixel);
			}
			else /* button == 3 */
			{
				if (channel_col_B[mem_channel] == pixel) break;
				channel_col_B[mem_channel] = pixel;
				/* To update displayed value */
				pressed_opacity(channel_col_A[mem_channel]);
			}
			break;
		}
		if (mem_img_bpp == 1)
		{
			if (button == 1)
			{
				if (mem_col_A == pixel) break;
				mem_col_A = pixel;
				mem_col_A24 = mem_pal[pixel];
			}
			else /* button == 3 */
			{
				if (mem_col_B == pixel) break;
				mem_col_B = pixel;
				mem_col_B24 = mem_pal[pixel];
			}
		}
		else
		{
			if (button == 1)
			{
				if (PNG_2_INT(mem_col_A24) == pixel) break;
				mem_col_A24.red = INT_2_R(pixel);
				mem_col_A24.green = INT_2_G(pixel);
				mem_col_A24.blue = INT_2_B(pixel);
			}
			else /* button == 3 */
			{
				if (PNG_2_INT(mem_col_B24) == pixel) break;
				mem_col_B24.red = INT_2_R(pixel);
				mem_col_B24.green = INT_2_G(pixel);
				mem_col_B24.blue = INT_2_B(pixel);
			}
		}
		update_cols();
		break;
	}

	if ((state & _CS) == _C); /* Done above */

	else if ((button == 2) || ((button == 3) && (state & _S)))
		set_zoom_centre(ox, oy);

	else if (grad_tool(event, x0, y0, state, button));

	/* Pure moves are handled elsewhere */
	else if (button) tool_action(event, x, y, button, pressure);

	if ((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON) ||
		(tool_type == TOOL_GRADIENT)) update_sel_bar();

	/* ****** Now to mouse-move-specific part ****** */

	if (event != GDK_MOTION_NOTIFY) return;

	if ( tool_type == TOOL_CLONE )
	{
		tool_ox = x;
		tool_oy = y;
	}

	if ( poly_status == POLY_SELECTING && button == 0 )
	{
		stretch_poly_line(x, y);
	}

	if ( tool_type == TOOL_SELECT || tool_type == TOOL_POLYGON )
	{
		if ( marq_status == MARQUEE_DONE )
		{
			if (cursor_tool)
			{
				i = close_to(x, y);
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
			if ( x>=marq_x1 && x<=marq_x2 && y>=marq_y1 && y<=marq_y2 )
				new_cursor = 1;		// Cursor = 4 way arrow

			if ( new_cursor != cursor_corner ) // Stops flickering on slow hardware
			{
				if (!cursor_tool || !new_cursor) set_cursor();
				else gdk_window_set_cursor(drawing_canvas->window, move_cursor);
				cursor_corner = new_cursor;
			}
		}
	}
	update_xy_bar(x, y);

///	TOOL PERIMETER BOX UPDATES

	if (perim_status > 0) clear_perim();	// Remove old perimeter box

	if ((tool_type == TOOL_CLONE) && (button == 0) && (state & _C))
	{
		clone_x += tox - x;
		clone_y += toy - y;
	}

	if (tool_size * can_zoom > 4)
	{
		perim_x = x - (tool_size >> 1);
		perim_y = y - (tool_size >> 1);
		perim_s = tool_size;
		repaint_perim(NULL);			// Repaint 4 sides
	}

///	LINE UPDATES

	if ((tool_type == TOOL_LINE) && (line_status != LINE_NONE) &&
		((line_x1 != x) || (line_y1 != y)))
	{
		repaint_line(0);
		line_x1 = x;
		line_y1 = y;
		repaint_line(1);
	}
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
	/* !!! Have to skip grab/ungrab related events if doing something */
//	if (event->mode != GDK_CROSSING_NORMAL) return (TRUE);

	return TRUE;
}

static gint canvas_left(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
	/* Skip grab/ungrab related events */
	if (event->mode != GDK_CROSSING_NORMAL) return (FALSE);

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

static int async_bk;

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

typedef struct {
	unsigned char *wmask, *gmask, *walpha, *galpha, *talpha;
	unsigned char *wimg, *gimg, *rgb;
	int opac, len, bpp;
} grad_render_state;

static unsigned char *init_grad_render(grad_render_state *grstate, int len,
	chanlist tlist)
{
	int opac = 0, i1, i2, bpp = MEM_BPP;
	unsigned char *gstore, *tmp;

// !!! Only the "slow path" for now
	if (gradient[mem_channel].status != GRAD_DONE) return (NULL);

	if ((mem_channel <= CHN_ALPHA) && (mem_img_bpp != 1))
		opac = grad_opacity;

	i1 = (mem_channel == CHN_IMAGE) && RGBA_mode &&
		mem_img[CHN_ALPHA] ? 2 : 0;
	i2 = !opac && (mem_channel <= CHN_ALPHA) &&
		(grad_opacity < 255) ? 3 : 0;

	gstore = malloc((2 + 2 * bpp + i1 + i2) * len);
	if (!gstore) return (NULL);
	memset(grstate, 0, sizeof(grad_render_state));
	grstate->opac = opac;
	grstate->len = len;
	grstate->bpp = bpp;

	grstate->wmask = gstore;		/* Mask */
	grstate->gmask = tmp = gstore + len;	/* Gradient opacity */
	grstate->gimg = tmp = tmp + len;	/* Gradient image */
	grstate->wimg = tmp = tmp + bpp * len;	/* Resulting image */
	tlist[mem_channel] = grstate->wimg;
	if (i2) /* Indexed to RGB */
	{
		grstate->rgb = tmp + (bpp + i1) * len;
		tlist[CHN_IMAGE] = grstate->rgb;
	}
	if (i1) /* Coupled alpha */
	{
		grstate->galpha = tmp = tmp + bpp * len; /* Gradient alpha */
		grstate->walpha = tmp + len;		/* Resulting alpha */
		grstate->talpha = grstate->galpha;	/* Transient alpha */
		tlist[CHN_ALPHA] = grstate->walpha;
	}
	else if (mem_channel == CHN_ALPHA) /* Primary alpha */
	{
		grstate->walpha = grstate->wimg;
		grstate->wimg = NULL;
		grstate->talpha = grstate->gimg;
	}
	return (gstore);
}

static void grad_render(int start, int step, int cnt, int x, int y,
	unsigned char *mask0, grad_render_state *grstate)
{
	int l = mem_width * y + x, li = l * mem_img_bpp;
	unsigned char *tmp = mem_img[mem_channel] + l * grstate->bpp;

	prep_mask(start, step, cnt, grstate->wmask, mask0, mem_img[CHN_IMAGE] + li);
	if (!grstate->opac) memset(grstate->gmask, 255, grstate->len);
	prep_grad(start, step, cnt, x, y, grstate->wmask, grstate->gmask,
		grstate->gimg, grstate->galpha);
	if (grstate->walpha)
		memcpy(grstate->walpha, mem_img[CHN_ALPHA] + l, grstate->len);
	process_mask(start, step, cnt, grstate->wmask, grstate->walpha,
		mem_img[CHN_ALPHA] + l, grstate->talpha, grstate->gmask,
		grstate->opac, channel_dis[CHN_ALPHA]);
	if (grstate->wimg)
	{
		memcpy(grstate->wimg, tmp, grstate->len * grstate->bpp);
		process_img(start, step, cnt, grstate->wmask, grstate->wimg,
			tmp, grstate->gimg, grstate->opac, grstate->bpp);
	}
	if (mem_channel > CHN_ALPHA)
		blend_channel(start, step, cnt, grstate->wmask, grstate->wimg,
			tmp, grad_opacity);
	else if (grstate->rgb)
		blend_indexed(start, step, cnt, grstate->rgb, mem_img[CHN_IMAGE] + l,
			grstate->wimg ? grstate->wimg : mem_img[CHN_IMAGE] + l,
			mem_img[CHN_ALPHA] + l, grstate->walpha, grad_opacity);
}

typedef struct {
	chanlist tlist;		// Channel overrides
	unsigned char *mask0;	// Active mask channel
	int mw, mh;		// Screen-space image size - useless for now!
	int px2, py2;		// Clipped area position
	int pw2, ph2;		// Clipped area size
	int dx;			// Image-space X offset
	int lx;			// Allocated row length
	int pww;		// Logical row length
	int zoom;		// Decimation factor
	int scale;		// Replication factor
	int lop;		// Base opacity
	int xpm;		// Transparent color
} main_render_state;

typedef struct {
	unsigned char *pvi;	// Temp image row
	unsigned char *pvm;	// Temp mask row
} xform_render_state;

typedef struct {
	chanlist tlist;		// Channel overrides for rendering clipboard
	unsigned char *clip_image;	// Pasted into current channel
	unsigned char *clip_alpha;	// Pasted into alpha channel
	unsigned char *t_alpha;		// Fake pasted alpha
	unsigned char *pix, *alpha;	// Destinations for the above
	unsigned char *mask, *wmask;	// Temp mask: one we use, other we init
	unsigned char *mask0;		// Image mask channel to use
	int opacity, bpp;	// Just that
	int pixf;		// Flag: need current channel override filled
	int dx;			// Memory-space X offset
	int lx;			// Allocated row length
	int pww;		// Logical row length
} paste_render_state;

/* !!! This function copies existing override set to build its own modified
 * !!! one, so override set must not be changed after calling it */
static unsigned char *init_paste_render(paste_render_state *p,
	main_render_state *r, unsigned char *xmask)
{
	unsigned char *temp, *tmp;
	int i, x, y, w, h, mx, my, ddx, bpp, scale = r->scale, zoom = r->zoom;
	int ti = 0, tm = 0, ta = 0, fa = 0;

	/* Clip paste area to update area */
	x = (marq_x1 * scale + zoom - 1) / zoom;
	y = (marq_y1 * scale + zoom - 1) / zoom;
	if (x < r->px2) x = r->px2;
	if (y < r->py2) y = r->py2;
	w = (marq_x2 * scale) / zoom + scale;
	h = (marq_y2 * scale) / zoom + scale;
	mx = r->px2 + r->pw2;
	w = (w < mx ? w : mx) - x;
	my = r->py2 + r->ph2;
	h = (h < my ? h : my) - y;
	if ((w <= 0) || (h <= 0)) return (NULL);

	memset(p, 0, sizeof(paste_render_state));
	memcpy(p->tlist, r->tlist, sizeof(chanlist));

// !!! Store area dimensions somewhere for other functions' use
//	xywh[0] = x;
//	xywh[1] = y;
//	xywh[2] = w;
//	xywh[3] = h;

	/* Setup row position and size */
	p->dx = (x * zoom) / scale;
	if (zoom > 1) p->lx = (w - 1) * zoom + 1 , p->pww = w;
	else p->lx = p->pww = (x + w - 1) / scale - p->dx + 1;

	/* Decide what goes where */
	if ((mem_channel == CHN_IMAGE) && !channel_dis[CHN_ALPHA])
	{
		p->clip_alpha = mem_clip_alpha;
		if (mem_img[CHN_ALPHA])
		{
			if (!mem_clip_alpha && RGBA_mode)
				fa = 1; /* Need fake alpha */
			if (mem_clip_alpha || fa)
				ta = 1; /* Need temp alpha */
		}
	}
	if (mem_channel == CHN_ALPHA)
	{
		p->clip_alpha = mem_clipboard;
		ta = 1; /* Need temp alpha */
	}
	else p->clip_image = mem_clipboard;
	ddx = p->dx - r->dx;

	/* Allocate temp area */
	bpp = p->bpp = MEM_BPP;
	tm = !xmask; /* Need temp mask if not have one ready */
	ti = p->clip_image && !p->tlist[mem_channel]; /* Same for temp image */
	i = r->lx * (ti * bpp + ta) + p->lx * (tm + fa);
	temp = tmp = malloc(i);
	if (!temp) return (NULL);

	/* Setup "image" (current) channel override */
	if (p->clip_image)
	{
		if (ti) p->tlist[mem_channel] = tmp , tmp += r->lx * bpp;
		p->pix = p->tlist[mem_channel] + ddx * bpp;
		/* Need it prefilled if no override data incoming */
		p->pixf = ti;
	}

	/* Setup alpha channel override */
	if (ta)
	{
		p->tlist[CHN_ALPHA] = tmp;
		p->alpha = tmp + ddx;
		tmp += r->lx;
	}

	/* Setup mask */
	if (mem_channel <= CHN_ALPHA) p->mask0 = r->mask0;
	if (tm) p->mask = p->wmask = tmp , tmp += p->lx;
	else
	{
		p->mask = xmask + ddx;
		if (r->mask0 != p->mask0)
		/* Mask has wrong data - reuse memory but refill values */
			p->wmask = xmask + ddx;
	}

	/* Setup fake alpha */
	if (fa)
	{
		p->t_alpha = tmp;
		memset(tmp, channel_col_A[CHN_ALPHA], p->lx);
	}

	/* Setup opacity mode */
	if ((mem_channel <= CHN_ALPHA) && (mem_img_bpp == 3))
		p->opacity = tool_opacity;

	return (temp);
}

static void paste_render(int start, int step, int y, paste_render_state *p)
{
	int ld = mem_width * y + p->dx;
	int dc = mem_clip_w * (y - marq_y1) + p->dx - marq_x1;
	int bpp = p->bpp;
	int cnt = p->pww;

	if (p->wmask) prep_mask(start, step, cnt, p->wmask, p->mask0 ?
		p->mask0 + ld : NULL, mem_img[CHN_IMAGE] + ld * mem_img_bpp);
	process_mask(start, step, cnt, p->mask, p->alpha, mem_img[CHN_ALPHA] + ld,
		p->clip_alpha ? p->clip_alpha + dc : p->t_alpha,
		mem_clip_mask ? mem_clip_mask + dc : NULL, p->opacity, 0);
	if (!p->clip_image) return;
	if (!p->pixf) /* Fill just the underlying part */
		memcpy(p->pix, mem_img[mem_channel] + ld * bpp, p->lx * bpp);
	process_img(start, step, cnt, p->mask, p->pix, mem_img[mem_channel] + ld * bpp,
		mem_clipboard + dc * mem_clip_bpp, p->opacity, mem_clip_bpp);
}

static int main_render_rgb(unsigned char *rgb, int px, int py, int pw, int ph)
{
	main_render_state r;
	unsigned char **tlist = r.tlist;
	int j, jj, j0, l, pw23, nix = 0, niy = 0, alpha_blend = !overlay_alpha;
	unsigned char *xtemp = NULL;
	xform_render_state xrstate;
	unsigned char *cstemp = NULL;
	unsigned char *gtemp = NULL;
	grad_render_state grstate;
	unsigned char *ptemp = NULL;
	paste_render_state prstate;


	memset(&r, 0, sizeof(r));

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	r.zoom = r.scale = 1;
	if (can_zoom < 1.0) r.zoom = rint(1.0 / can_zoom);
	else r.scale = rint(can_zoom);

	/* Align buffer with image */
	r.px2 = px - margin_main_x;
	r.py2 = py - margin_main_y;
	if (r.px2 < 0) nix = -r.px2;
	if (r.py2 < 0) niy = -r.py2;
	rgb += (pw * niy + nix) * 3;

	/* Clip update area to image bounds */
	r.mw = (mem_width * r.scale + r.zoom - 1) / r.zoom;
	r.mh = (mem_height * r.scale + r.zoom - 1) / r.zoom;
	r.pw2 = pw + r.px2;
	r.ph2 = ph + r.py2;
	if (r.pw2 > r.mw) r.pw2 = r.mw;
	if (r.ph2 > r.mh) r.ph2 = r.mh;
	r.px2 += nix; r.py2 += niy;
	r.pw2 -= r.px2; r.ph2 -= r.py2;
	if ((r.pw2 < 1) || (r.ph2 < 1)) return (FALSE);

	if (!channel_dis[CHN_MASK]) r.mask0 = mem_img[CHN_MASK];
	if (!mem_img[CHN_ALPHA] || channel_dis[CHN_ALPHA]) alpha_blend = FALSE;

	r.xpm = mem_xpm_trans; r.lop = 255;
	if (layers_total && show_layers_main)
	{
		if (layer_selected)
		{
			r.xpm = layer_table[layer_selected].use_trans ?
				layer_table[layer_selected].trans : -1;
			r.lop = (layer_table[layer_selected].opacity * 255 + 50) / 100;
		}
	}
	else if (alpha_blend || (mem_xpm_trans >= 0))
		render_background(rgb, r.px2, r.py2, r.pw2, r.ph2, pw * 3);

	/* Setup row position and size */
	r.dx = (r.px2 * r.zoom) / r.scale;
	if (r.zoom > 1) r.lx = (r.pw2 - 1) * r.zoom + 1 , r.pww = r.pw2;
	else r.lx = r.pww = (r.px2 + r.pw2 - 1) / r.scale - r.dx + 1;

	/* Color transform preview */
	if (mem_preview && (mem_img_bpp == 3))
	{
		xtemp = xrstate.pvm = malloc(r.lx * 4);
		if (xtemp) r.tlist[CHN_IMAGE] = xrstate.pvi = xtemp + r.lx;
	}

	/* Color selective mode preview */
	else if (csel_overlay) cstemp = malloc(r.lx);

	/* Gradient preview */
	else if ((tool_type == TOOL_GRADIENT) && grad_opacity)
	{
		if (mem_channel >= CHN_ALPHA) r.mask0 = NULL;
		gtemp = init_grad_render(&grstate, r.lx, r.tlist);
	}

	/* Paste preview - can only coexist with transform */
	if (show_paste && (marq_status >= MARQUEE_PASTE) && !cstemp && !gtemp)
		ptemp = init_paste_render(&prstate, &r, xtemp ? xrstate.pvm : NULL);

	/* Start rendering */
	setup_row(r.px2, r.pw2, can_zoom, mem_width, r.xpm, r.lop,
		gtemp && grstate.rgb ? 3 : mem_img_bpp, mem_pal);
 	j0 = -1; pw *= 3; pw23 = r.pw2 * 3;
	for (jj = 0; jj < r.ph2; jj++ , rgb += pw)
	{
		j = ((r.py2 + jj) * r.zoom) / r.scale;
		if (j != j0)
		{
			j0 = j;
			l = mem_width * j + r.dx;
			tlist = r.tlist; /* Default override */

			/* Color transform preview */
			if (xtemp)
			{
				prep_mask(0, r.zoom, r.pww, xrstate.pvm,
					r.mask0 ? r.mask0 + l : NULL,
					mem_img[CHN_IMAGE] + l * 3);
				do_transform(0, r.zoom, r.pww, xrstate.pvm,
					xrstate.pvi, mem_img[CHN_IMAGE] + l * 3);
			}
			/* Color selective mode preview */
			else if (cstemp)
			{
				memset(cstemp, 0, r.lx);
				csel_scan(0, r.zoom, r.pww, cstemp,
					mem_img[CHN_IMAGE] + l * mem_img_bpp,
					csel_data);
			}
			/* Gradient preview */
			else if (gtemp) grad_render(0, r.zoom, r.pww, r.dx, j,
				r.mask0 ? r.mask0 + l : NULL, &grstate);

			/* Paste preview - should be after transform */
			if (ptemp && (j >= marq_y1) && (j <= marq_y2))
			{
				tlist = prstate.tlist; /* Paste-area override */
				if (tlist[CHN_ALPHA]) memcpy(tlist[CHN_ALPHA],
					mem_img[CHN_ALPHA] + l, r.lx);
				if (prstate.pixf) memcpy(tlist[mem_channel],
					mem_img[mem_channel] + l * prstate.bpp,
					r.lx * prstate.bpp);
				paste_render(0, r.zoom, j, &prstate);
			}
		}
		else if (!async_bk)
		{
			memcpy(rgb, rgb - pw, pw23);
			continue;
		}
		render_row(rgb, mem_img, r.dx, j, tlist);
		if (!cstemp) overlay_row(rgb, mem_img, r.dx, j, tlist);
		else overlay_preview(rgb, cstemp, csel_preview, csel_preview_a);
	}
	free(xtemp);
	free(cstemp);
	free(gtemp);
	free(ptemp);

	return (!!ptemp); /* "There was paste" */
}

/* Draw grid on rgb memory */
static void draw_grid(unsigned char *rgb, int x, int y, int w, int h, int l)
{
	int i, j, k, dx, dy, step, step3;
	unsigned char *tmp;

	step = can_zoom;

	dx = (x - margin_main_x) % step;
	if (dx < 0) dx += step;
	dy = (y - margin_main_y) % step;
	if (dy < 0) dy += step;
	if (dx) dx = (step - dx) * 3;
	w *= 3; l *= 3;

	for (k = dy , i = 0; i < h; i++)
	{
		tmp = rgb + i * l;
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

/* Redirectable RGB blitting */
void draw_rgb(int x, int y, int w, int h, unsigned char *rgb, int step, rgbcontext *ctx)
{
	if (!ctx) gdk_draw_rgb_image(drawing_canvas->window, drawing_canvas->style->black_gc,
		x, y, w, h, GDK_RGB_DITHER_NONE, rgb, step);
	else
	{
		unsigned char *dest;
		int l;

		if ((w <= 0) || (h <= 0)) return;
		w += x; h += y;
		if ((x >= ctx->x1) || (y >= ctx->y1) ||
			(w <= ctx->x0) || (h <= ctx->y0)) return;
		if (x < ctx->x0) rgb += 3 * (ctx->x0 - x) , x = ctx->x0;
		if (y < ctx->y0) rgb += step * (ctx->y0 - y) , y = ctx->y0;
		if (w > ctx->x1) w = ctx->x1;
		if (h > ctx->y1) h = ctx->y1;
		w = (w - x) * 3;
		l = (ctx->x1 - ctx->x0) * 3;
		dest = ctx->rgb + (y - ctx->y0) * l + (x - ctx->x0) * 3;
		for (h -= y; h; h--)
		{
			memcpy(dest, rgb, w);
			dest += l; rgb += step;
		}
	}
}

void repaint_canvas( int px, int py, int pw, int ph )
{
	rgbcontext ctx;
	unsigned char *rgb;
	int pw2, ph2, lx = 0, ly = 0, rx1, ry1, rx2, ry2, rpx, rpy;
	int i, j, zoom = 1, scale = 1, paste_f;

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

	/* Init context */
	ctx.x0 = px; ctx.y0 = py; ctx.x1 = px + pw; ctx.y1 = py + ph;
	ctx.rgb = rgb;

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
		render_layers(rgb, pw * 3, rpx + lx, rpy + ly, pw, ph,
			can_zoom, 0, layer_selected - 1, 1);
	}
	else async_bk = !chequers_optimize; /* Only w/o layers */
	paste_f = main_render_rgb(rgb, px, py, pw, ph);
	if (layers_total && show_layers_main)
		render_layers(rgb, pw * 3, rpx + lx, rpy + ly, pw, ph,
			can_zoom, layer_selected + 1, layers_total, 1);

	/* No grid at all */
	if (!mem_show_grid || (scale < mem_grid_min));
	/* No paste - single area */
	else if (!paste_f) draw_grid(rgb, px, py, pw, ph, pw);
	/* With paste - zero to four areas */
	else
	{
		int y0, y1, x0, x1, h1, w1;
		unsigned char *r;

		/* Areas _do_ intersect - cut intersection out */
		/* Top rectangle */
		y0 = marq_y1 > 0 ? marq_y1 : 0;
		y0 = margin_main_y + y0 * scale;
		h1 = y0 - py;
		if (h1 <= 0) y0 = py;
		else draw_grid(rgb, px, py, pw, h1, pw);

		/* Bottom rectangle */
		y1 = marq_y2 < mem_height ? marq_y2 + 1 : mem_height;
		y1 = margin_main_y + y1 * scale;
		h1 = py + ph - y1;
		if (h1 <= 0) y1 = py + ph;
		else draw_grid(rgb + (y1 - py) * pw * 3, px, y1, pw, h1, pw);

		/* Middle rectangles */
		h1 = y1 - y0;
		r = rgb + (y0 - py) * pw * 3;

		/* Left rectangle */
		x0 = marq_x1 > 0 ? marq_x1 : 0;
		x0 = margin_main_x + x0 * scale;
		w1 = x0 - px;
		if (w1 > 0) draw_grid(r, px, y0, w1, h1, pw);
		
		/* Right rectangle */
		x1 = marq_x2 < mem_width ? marq_x2 + 1 : mem_width;
		x1 = margin_main_x + x1 * scale;
		w1 = px + pw - x1;
		if (w1 > 0) draw_grid(r + (x1 - px) * 3, x1, y0, w1, h1, pw);
	}

	async_bk = FALSE;

	/* Redraw gradient line if needed */
	while (gradient[mem_channel].status == GRAD_DONE)
	{
		grad_info *grad = gradient + mem_channel;

		/* Don't clutter screen needlessly */
		if (!mem_gradient && (tool_type != TOOL_GRADIENT)) break;

		/* Canvas-space endpoints */
		if (grad->x1 < grad->x2) rx1 = grad->x1 , rx2 = grad->x2;
		else rx1 = grad->x2 , rx2 = grad->x1;
		if (grad->y1 < grad->y2) ry1 = grad->y1 , ry2 = grad->y2;
		else ry1 = grad->y2 , ry2 = grad->y1;
		rx1 = (rx1 * scale) / zoom;
		ry1 = (ry1 * scale) / zoom;
		rx2 = (rx2 * scale) / zoom + scale - 1;
		ry2 = (ry2 * scale) / zoom + scale - 1;

		/* Check intersection - coarse */
		if ((rx1 > pw2) || (rx2 < rpx) || (ry1 > ph2) || (ry2 < rpy))
			break;
		if (rx1 != rx2) /* Check intersection - fine */
		{
			float ty1, ty2, dy;

			if ((grad->x1 < grad->x2) ^ (grad->y1 < grad->y2))
				i = ry2 , j = ry1;
			else i = ry1 , j = ry2;

			dy = (j - i) / (float)(rx2 - rx1);
			ty1 = rx1 >= rpx ? i : i + (rpx - rx1 - 0.5) * dy;
			ty2 = rx2 <= pw2 ? j : i + (pw2 - rx1 + 0.5) * dy;

			if (((ty1 < rpy - scale) && (ty2 < rpy - scale)) ||
				((ty1 > ph2 + scale) && (ty2 > ph2 + scale)))
				break;
		}
// !!! Wrong order - overlays clipboard !!!
		refresh_grad(&ctx);
		break;
	}

	/* Draw perimeter & marquee as we may have drawn over them */
/* !!! All other over-the-image things have to be redrawn here as well !!! */
	if (marq_status != MARQUEE_NONE) refresh_marquee(&ctx);
	if (perim_status > 0) repaint_perim(&ctx);

	gdk_draw_rgb_image(drawing_canvas->window, drawing_canvas->style->black_gc,
		px, py, pw, ph, GDK_RGB_DITHER_NONE, rgb, pw * 3);
	free(rgb);
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

void repaint_perim_real(int r, int g, int b, int ox, int oy, rgbcontext *ctx)
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

	draw_rgb(x0, y0, 1, h, rgb, 3, ctx);
	draw_rgb(x1, y0, 1, h, rgb, 3, ctx);

	draw_rgb(x0 + 1, y0, w - 2, 1, rgb, 0, ctx);
	draw_rgb(x0 + 1, y1, w - 2, 1, rgb, 0, ctx);
	free(rgb);
}

void repaint_perim(rgbcontext *ctx)
{
	/* Don't bother if tool has no perimeter */
	if (NO_PERIM(tool_type)) return;
	repaint_perim_real(255, 255, 255, 0, 0, ctx);
	if ( tool_type == TOOL_CLONE )
		repaint_perim_real(255, 0, 0, clone_x, clone_y, ctx);
	perim_status = 1; /* Drawn */
}

static gboolean canvas_motion(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
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
	float old_zoom;
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
	old_zoom = vw_zoom;
	vw_zoom = -1; /* Force resize */
	vw_align_size(old_zoom);		// Update the view window as needed
	vw_zoom = old_zoom;

	return TRUE;
}

void force_main_configure()
{
	if (drawing_canvas) configure_canvas(drawing_canvas, NULL);
	if (view_showing && vw_drawing) vw_configure(vw_drawing, NULL);
}

static gboolean expose_canvas(GtkWidget *widget, GdkEventExpose *event,
	gpointer user_data)
{
	int px, py, pw, ph;

	/* Stops excess jerking in GTK+1 when zooming */
	if (zoom_flag) return (TRUE);

	px = event->area.x;		// Only repaint if we need to
	py = event->area.y;
	pw = event->area.width;
	ph = event->area.height;

	repaint_canvas( px, py, pw, ph );
	paint_poly_marquee();

	return (TRUE);
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
	if (!drawing_canvas->window) return; /* Do nothing if canvas hidden */
	gdk_window_set_cursor(drawing_canvas->window,
		cursor_tool ? m_cursor[tool_type] : NULL);
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
		pressed_rotate_sel(NULL, NULL, 0); break;
	case TTB_SELRCCW:
		pressed_rotate_sel(NULL, NULL, 1); break;
	}

	if ( tool_type != i )		// User has changed tool
	{
		if (i == TOOL_LINE) stop_line();
		if ((i == TOOL_GRADIENT) &&
			(gradient[mem_channel].status != GRAD_NONE))
		{
			if (gradient[mem_channel].status != GRAD_DONE)
				gradient[mem_channel].status = GRAD_NONE;
			else if (grad_opacity)
				gtk_widget_queue_draw(drawing_canvas);
			else if (!mem_gradient) repaint_grad(0);
		}
		if ( marq_status != MARQUEE_NONE)
		{
			if ( marq_status >= MARQUEE_PASTE &&
				inifile_get_gboolean( "pasteCommit", FALSE ) )
			{
				commit_paste(NULL);
				pen_down = 0;
				mem_undo_prepare();
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
// !!! To NOT show selection frame while placing gradient
//		if ((tool_type == TOOL_SELECT)
		if (((tool_type == TOOL_SELECT) || (tool_type == TOOL_GRADIENT))
			&& (marq_x1 >= 0) && (marq_y1 >= 0)
			&& (marq_x2 >= 0) && (marq_y2 >= 0))
		{
			marq_status = MARQUEE_DONE;
			check_marquee();
			paint_marquee(1, marq_x1, marq_y1);
		}
		if ((tool_type == TOOL_GRADIENT) &&
			(gradient[mem_channel].status == GRAD_DONE))
		{
			if (grad_opacity)
				gtk_widget_queue_draw(drawing_canvas);
			else repaint_grad(1);
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
	if (state) set_cursor(); /* Canvas window is now a new one */
}

static void parse_drag( char *txt )
{
	gboolean nlayer = TRUE;
	char fname[PATHBUF], *tp, *tp2;
	int i, j;

	if ( layers_window == NULL ) pressed_layers( NULL, NULL );
		// For some reason the layers window must be initialized, or bugs happen??

	gtk_widget_set_sensitive( layers_window, FALSE );
	gtk_widget_set_sensitive( main_window, FALSE );

	tp = txt;
	while ((layers_total < MAX_LAYERS) && (tp2 = strstr(tp, "file:")))
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
		while ((tp[j] > 31) && (j < PATHBUF - 1))	// Copy filename
		{
			if ( tp[j] == '%' )	// Weed out those ghastly % substitutions
			{
				fname[i++] = read_hex_dub( tp+j+1 );
				j += 2;
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


typedef struct
{
	char *path; /* Full path for now */
	signed char radio_BTS; /* -2..-5 are for BTS */
	unsigned short ID;
	int actmap;
	char *shortcut; /* Text form for now */
	void (*handler)();
	int parm;
} menu_item;

static GtkWidget **need_lists[] = {
	menu_undo, menu_redo, menu_crop, menu_need_marquee,
	menu_need_selection, menu_need_clipboard, menu_only_24,
	menu_not_indexed, menu_only_indexed, menu_lasso, menu_alphablend,
	menu_chan_del, menu_rgba };

void mapped_dis_add(GtkWidget *widget, int actmap)
{
	int i;

	while (actmap)
	{
		i = actmap; actmap &= actmap - 1; i = (i ^ actmap) - 1;
		i = (i & 0x55555555) + ((i >> 1) & 0x55555555);
		i = (i & 0x33333333) + ((i >> 2) & 0x33333333);
		i = (i & 0x0F0F0F0F) + ((i >> 4) & 0x0F0F0F0F);
		i = (i & 0x00FF00FF) + ((i >> 8) & 0x00FF00FF);
		i = (i & 0xFFFF) + (i >> 16);
		men_dis_add(widget, need_lists[i]);
	}
}

/* The following is main menu auto-rearrange code. If the menu is too long for
 * the window, some of its items are moved into "overflow" submenu - and moved
 * back to menubar when the window is made wider. This way, we can support
 * small-screen devices without penalizing large-screen ones. - WJ */

#define MENU_RESIZE_MAX 16

typedef struct {
	GtkWidget *item, *fallback;
	guint key;
	int width;
} r_menu_slot;

int r_menu_state;
r_menu_slot r_menu[MENU_RESIZE_MAX];

/* Handle keyboard accels for overflow menu */
static int check_smart_menu_keys(GdkEventKey *event)
{
	guint lowkey;
	int i;

	/* No overflow - nothing to do */
	if (r_menu_state == 0) return (0);
	/* Alt+key only */
	if ((event->state & _CSA) != _A) return (0);

	lowkey = low_key(event);
	for (i = 1; i <= r_menu_state; i++)
	{
		if (r_menu[i].key != lowkey) continue;
		/* Just popup - if we're here, overflow menu is offscreen anyway */
		gtk_menu_popup(GTK_MENU(GTK_MENU_ITEM(r_menu[i].fallback)->submenu),
			NULL, NULL, NULL, NULL, 0, 0);

		return (ACT_DUMMY);
	}
	return (0);
}

/* Invalidate width cache after width-affecting change */
static void check_width_cache(int width)
{
	r_menu_slot *slot;

	if (r_menu[r_menu_state].width == width) return;
	if (r_menu[r_menu_state].width)
		for (slot = r_menu; slot->item; slot++) slot->width = 0;
	r_menu[r_menu_state].width = width;
}

/* Show/hide widgets according to new state */
static void change_to_state(int state)
{
	int i, oldst = r_menu_state;

	if (oldst < state)
	{
		for (i = oldst + 1; i <= state; i++)
			gtk_widget_hide(r_menu[i].item);
		if (oldst == 0) gtk_widget_show(r_menu[0].item);
	}
	else
	{
		for (i = oldst; i > state; i--)
			gtk_widget_show(r_menu[i].item);
		if (state == 0) gtk_widget_hide(r_menu[0].item);
	}
	r_menu_state = state;
}

/* Move submenus between menubar and overflow submenu */
static void switch_states(int newstate, int oldstate)
{
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
			item = GTK_MENU_ITEM(r_menu[i].item);
			gtk_menu_item_set_submenu(item, submenu);
			gtk_widget_unref(submenu);
		}
	}
	else /* To overflow submenu */
	{
		for (i = oldstate + 1; i <= newstate; i++)
		{
			item = GTK_MENU_ITEM(r_menu[i].item);
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
static int smart_menu_full_width(GtkWidget *widget, int width)
{
	check_width_cache(width);
	if (!r_menu[0].width)
	{
		GtkRequisition req;
		GtkWidget *child = GTK_BIN(widget)->child;
		int oldst = r_menu_state;
		gpointer lock = toggle_updates(widget, NULL);
		change_to_state(0);
		gtk_widget_size_request(child, &req);
		r_menu[0].width = req.width;
		change_to_state(oldst);
		child->requisition.width = width;
		toggle_updates(widget, lock);
	}
	return (r_menu[0].width);
}

/* Switch to the state which best fits the allocated width */
static void smart_menu_state_to_width(GtkWidget *widget, int rwidth, int awidth)
{
	GtkWidget *child = GTK_BIN(widget)->child;
	gpointer lock = NULL;
	int state, oldst, newst;

	check_width_cache(rwidth);
	state = oldst = r_menu_state;
	while (TRUE)
	{
		newst = rwidth < awidth ? state - 1 : state + 1;
		if ((newst < 0) || !r_menu[newst].item) break;
		if (!r_menu[newst].width)
		{
			GtkRequisition req;
			if (!lock) lock = toggle_updates(widget, NULL);
			change_to_state(newst);
			gtk_widget_size_request(child, &req);
			r_menu[newst].width = req.width;
		}
		state = newst;
		if ((rwidth < awidth) ^ (r_menu[state].width <= awidth)) break;
	}
	while ((r_menu[state].width > awidth) && r_menu[state + 1].item) state++;
	if (state != r_menu_state)
	{
		if (!lock) lock = toggle_updates(widget, NULL);
		change_to_state(state);
		child->requisition.width = r_menu[state].width;
	}
	if (state != oldst) switch_states(state, oldst);
	if (lock) toggle_updates(widget, lock);
}

static void smart_menu_size_req(GtkWidget *widget, GtkRequisition *req,
	gpointer user_data)
{
	GtkRequisition child_req;
	GtkWidget *child = GTK_BIN(widget)->child;
	int fullw;

	req->width = req->height = GTK_CONTAINER(widget)->border_width * 2;
	if (!child || !GTK_WIDGET_VISIBLE(child)) return;

	gtk_widget_size_request(child, &child_req);
	fullw = smart_menu_full_width(widget, child_req.width);

	req->width += fullw;
	req->height += child_req.height;
}

static void smart_menu_size_alloc(GtkWidget *widget, GtkAllocation *alloc,
	gpointer user_data)
{
	static int in_alloc;
	GtkRequisition child_req;
	GtkAllocation child_alloc;
	GtkWidget *child = GTK_BIN(widget)->child;
	int border = GTK_CONTAINER(widget)->border_width, border2 = border * 2;

	widget->allocation = *alloc;
	if (!child || !GTK_WIDGET_VISIBLE(child)) return;

	/* Maybe recursive calls to this cannot happen, but if they can,
	 * crash would be quite spectacular - so, better safe than sorry */
	if (in_alloc) /* Postpone reaction */
	{
		in_alloc |= 2;
		return;
	}

	/* !!! Always keep child widget requisition set according to its
	 * !!! mode, or this code will break down in interesting ways */
	gtk_widget_get_child_requisition(child, &child_req);
/* !!! Alternative approach - reliable but slow */
//	gtk_widget_size_request(child, &child_req);
	while (TRUE)
	{
		in_alloc = 1;
		child_alloc.x = alloc->x + border;
		child_alloc.y = alloc->y + border;
		child_alloc.width = alloc->width > border2 ?
			alloc->width - border2 : 0;
		child_alloc.height = alloc->height > border2 ?
			alloc->height - border2 : 0;
		if ((child_alloc.width != child->allocation.width) &&
			(r_menu_state > 0 ? child_alloc.width != child_req.width :
			child_alloc.width < child_req.width))
			smart_menu_state_to_width(widget, child_req.width,
				child_alloc.width);
		if (in_alloc < 2) break;
		alloc = &widget->allocation;
	}
	in_alloc = 0;

	gtk_widget_size_allocate(child, &child_alloc);
}

static GtkWidget *fill_menu(menu_item *items, GtkAccelGroup *accel_group)
{
	static char *bts[6] = { "<CheckItem>", NULL, "<Branch>", "<Tearoff>",
		"<Separator>", "<LastBranch>" };
	GtkItemFactoryEntry wf;
	GtkItemFactory *factory;
	GtkWidget *widget, *wrap, *rwidgets[MENU_RESIZE_MAX];
	char *radio[32], *rnames[MENU_RESIZE_MAX];
	int i, j, rn = 0;
#if GTK_MAJOR_VERSION == 1
	GSList *en;
#endif

	memset(&wf, 0, sizeof(wf));
	memset(radio, 0, sizeof(radio));
	rnames[0] = NULL;
	factory = gtk_item_factory_new(GTK_TYPE_MENU_BAR, "<main>", accel_group);
	for (; items->path; items++)
	{
		wf.path = _(items->path);
		wf.accelerator = items->shortcut;
		wf.callback = items->handler;
		wf.callback_action = items->parm;
		wf.item_type = items->radio_BTS < 1 ? bts[-items->radio_BTS & 15] :
			radio[items->radio_BTS] ? radio[items->radio_BTS] :
			"<RadioItem>";
		if ((items->radio_BTS > 0) && !radio[items->radio_BTS])
			radio[items->radio_BTS] = wf.path;
		gtk_item_factory_create_item(factory, &wf, NULL, 2);
		/* !!! Workaround - internal path may differ from input path */
		widget = gtk_item_factory_get_item(factory,
			((GtkItemFactoryItem *)factory->items->data)->path);
		mapped_dis_add(widget, items->actmap);
		/* For now, remember only requested widgets */
		if (items->ID) menu_widgets[items->ID] = widget;
		/* Remember what is size-aware */
		if (items->radio_BTS > -16) continue;
		rnames[rn] = wf.path;
		rwidgets[rn++] = widget;
	}

	/* Setup overflow submenu */
	r_menu[0].item = rwidgets[--rn];
	memset(&wf, 0, sizeof(wf));
	for (i = 0; i < rn; i++)
	{
		j = rn - i;
		widget = r_menu[j].item = rwidgets[i];
#if GTK_MAJOR_VERSION == 1
		en = gtk_accel_group_entries_from_object(GTK_OBJECT(widget));
	/* !!! This'll get confused if both underline and normal accelerators
	 * are defined for the item */
		r_menu[j].key = en ? ((GtkAccelEntry *)en->data)->accelerator_key :
			GDK_VoidSymbol;
#else
		r_menu[j].key = gtk_label_get_mnemonic_keyval(GTK_LABEL(
			GTK_BIN(widget)->child));
#endif
		wf.path = g_strconcat(rnames[rn], rnames[i], NULL);
		wf.item_type = "<Branch>";
		gtk_item_factory_create_item(factory, &wf, NULL, 2);
		/* !!! Workaround - internal path may differ from input path */
		widget = gtk_item_factory_get_item(factory,
			((GtkItemFactoryItem *)factory->items->data)->path);
		g_free(wf.path);
		r_menu[j].fallback = widget;
		gtk_widget_hide(widget);
	}
	gtk_widget_hide(r_menu[0].item);

	/* Wrap menubar with resize-controller widget */
	widget = gtk_item_factory_get_widget(factory, "<main>");
	gtk_widget_show(widget);
	wrap = wj_size_bin();
	gtk_container_add(GTK_CONTAINER(wrap), widget);
	gtk_signal_connect(GTK_OBJECT(wrap), "size_request",
		GTK_SIGNAL_FUNC(smart_menu_size_req), NULL);
	gtk_signal_connect(GTK_OBJECT(wrap), "size_allocate",
		GTK_SIGNAL_FUNC(smart_menu_size_alloc), NULL);

	return (wrap);
}

#undef _
#define _(X) X

/* !!! Keep MENU_RESIZE_MAX larger than number of resize-enabled items */

static menu_item main_menu[] = {
	{ _("/_File"), -2 -16 },
	{ _("/File/tear"), -3 },
	{ _("/File/New"), -1, 0, 0, "<control>N", pressed_new, 0 },
	{ _("/File/Open ..."), -1, 0, 0, "<control>O", pressed_open_file, 0 },
	{ _("/File/Save"), -1, 0, 0, "<control>S", pressed_save_file, 0 },
	{ _("/File/Save As ..."), -1, 0, 0, NULL, pressed_save_file_as, 0 },
	{ _("/File/sep1"), -4 },
	{ _("/File/Export Undo Images ..."), -1, 0, NEED_UNDO, NULL, pressed_export_undo, 0 },
	{ _("/File/Export Undo Images (reversed) ..."), -1, 0, NEED_UNDO, NULL, pressed_export_undo2, 0 },
	{ _("/File/Export ASCII Art ..."), -1, 0, NEED_IDX, NULL, pressed_export_ascii, 0 },
	{ _("/File/Export Animated GIF ..."), -1, 0, NEED_IDX, NULL, pressed_export_gif, 0 },
	{ _("/File/sep2"), -4 },
	{ _("/File/Actions"), -2 },
	{ _("/File/Actions/tear"), -3 },
	{ _("/File/Actions/1"), -1, MENU_FACTION1, 0, NULL, pressed_file_action, 1 },
	{ _("/File/Actions/2"), -1, MENU_FACTION2, 0, NULL, pressed_file_action, 2 },
	{ _("/File/Actions/3"), -1, MENU_FACTION3, 0, NULL, pressed_file_action, 3 },
	{ _("/File/Actions/4"), -1, MENU_FACTION4, 0, NULL, pressed_file_action, 4 },
	{ _("/File/Actions/5"), -1, MENU_FACTION5, 0, NULL, pressed_file_action, 5 },
	{ _("/File/Actions/6"), -1, MENU_FACTION6, 0, NULL, pressed_file_action, 6 },
	{ _("/File/Actions/7"), -1, MENU_FACTION7, 0, NULL, pressed_file_action, 7 },
	{ _("/File/Actions/8"), -1, MENU_FACTION8, 0, NULL, pressed_file_action, 8 },
	{ _("/File/Actions/9"), -1, MENU_FACTION9, 0, NULL, pressed_file_action, 9 },
	{ _("/File/Actions/10"), -1, MENU_FACTION10, 0, NULL, pressed_file_action, 10 },
	{ _("/File/Actions/11"), -1, MENU_FACTION11, 0, NULL, pressed_file_action, 11 },
	{ _("/File/Actions/12"), -1, MENU_FACTION12, 0, NULL, pressed_file_action, 12 },
	{ _("/File/Actions/13"), -1, MENU_FACTION13, 0, NULL, pressed_file_action, 13 },
	{ _("/File/Actions/14"), -1, MENU_FACTION14, 0, NULL, pressed_file_action, 14 },
	{ _("/File/Actions/15"), -1, MENU_FACTION15, 0, NULL, pressed_file_action, 15 },
	{ _("/File/Actions/sepC"), -4, MENU_FACTION_S },
	{ _("/File/Actions/Configure"), -1, 0, 0, NULL, pressed_file_configure, 0 },
	{ _("/File/sepR"), -4, MENU_RECENT_S },
	{ _("/File/1"), -1, MENU_RECENT1, 0, "<shift><control>F1", pressed_load_recent, 1 },
	{ _("/File/2"), -1, MENU_RECENT2, 0, "<shift><control>F2", pressed_load_recent, 2 },
	{ _("/File/3"), -1, MENU_RECENT3, 0, "<shift><control>F3", pressed_load_recent, 3 },
	{ _("/File/4"), -1, MENU_RECENT4, 0, "<shift><control>F4", pressed_load_recent, 4 },
	{ _("/File/5"), -1, MENU_RECENT5, 0, "<shift><control>F5", pressed_load_recent, 5 },
	{ _("/File/6"), -1, MENU_RECENT6, 0, "<shift><control>F6", pressed_load_recent, 6 },
	{ _("/File/7"), -1, MENU_RECENT7, 0, "<shift><control>F7", pressed_load_recent, 7 },
	{ _("/File/8"), -1, MENU_RECENT8, 0, "<shift><control>F8", pressed_load_recent, 8 },
	{ _("/File/9"), -1, MENU_RECENT9, 0, "<shift><control>F9", pressed_load_recent, 9 },
	{ _("/File/10"), -1, MENU_RECENT10, 0, "<shift><control>F10", pressed_load_recent, 10 },
	{ _("/File/11"), -1, MENU_RECENT11, 0, NULL, pressed_load_recent, 11 },
	{ _("/File/12"), -1, MENU_RECENT12, 0, NULL, pressed_load_recent, 12 },
	{ _("/File/13"), -1, MENU_RECENT13, 0, NULL, pressed_load_recent, 13 },
	{ _("/File/14"), -1, MENU_RECENT14, 0, NULL, pressed_load_recent, 14 },
	{ _("/File/15"), -1, MENU_RECENT15, 0, NULL, pressed_load_recent, 15 },
	{ _("/File/16"), -1, MENU_RECENT16, 0, NULL, pressed_load_recent, 16 },
	{ _("/File/17"), -1, MENU_RECENT17, 0, NULL, pressed_load_recent, 17 },
	{ _("/File/18"), -1, MENU_RECENT18, 0, NULL, pressed_load_recent, 18 },
	{ _("/File/19"), -1, MENU_RECENT19, 0, NULL, pressed_load_recent, 19 },
	{ _("/File/20"), -1, MENU_RECENT20, 0, NULL, pressed_load_recent, 20 },
	{ _("/File/sep3"), -4 },
	{ _("/File/Quit"), -1, 0, 0, "<control>Q", quit_all, 0 },

	{ _("/_Edit"), -2 -16 },
	{ _("/Edit/tear"), -3 },
	{ _("/Edit/Undo"), -1, 0, NEED_UNDO, "<control>Z", main_undo, 0 },
	{ _("/Edit/Redo"), -1, 0, NEED_REDO, "<control>R", main_redo, 0 },
	{ _("/Edit/sep1"), -4 },
	{ _("/Edit/Cut"), -1, 0, NEED_SEL2, "<control>X", pressed_copy, 1 },
	{ _("/Edit/Copy"), -1, 0, NEED_SEL2, "<control>C", pressed_copy, 0 },
	{ _("/Edit/Paste To Centre"), -1, 0, NEED_CLIP, "<control>V", pressed_paste_centre, 0 },
	{ _("/Edit/Paste To New Layer"), -1, 0, NEED_CLIP, "<control><shift>V", pressed_paste_layer, 0 },
	{ _("/Edit/Paste"), -1, 0, NEED_CLIP, "<control>K", pressed_paste, 0 },
	{ _("/Edit/Paste Text"), -1, 0, 0, "T", pressed_text, 0 },
#ifdef U_FREETYPE
	{ _("/Edit/Paste Text (FreeType)"), -1, 0, 0, "<control>T", pressed_mt_text, 0 },
#endif
	{ _("/Edit/sep2"), -4 },
	{ _("/Edit/Load Clipboard"), -2 },
	{ _("/Edit/Load Clipboard/tear"), -3 },
	{ _("/Edit/Load Clipboard/1"), -1, 0, 0, "<shift>F1", load_clip, 1 },
	{ _("/Edit/Load Clipboard/2"), -1, 0, 0, "<shift>F2", load_clip, 2 },
	{ _("/Edit/Load Clipboard/3"), -1, 0, 0, "<shift>F3", load_clip, 3 },
	{ _("/Edit/Load Clipboard/4"), -1, 0, 0, "<shift>F4", load_clip, 4 },
	{ _("/Edit/Load Clipboard/5"), -1, 0, 0, "<shift>F5", load_clip, 5 },
	{ _("/Edit/Load Clipboard/6"), -1, 0, 0, "<shift>F6", load_clip, 6 },
	{ _("/Edit/Load Clipboard/7"), -1, 0, 0, "<shift>F7", load_clip, 7 },
	{ _("/Edit/Load Clipboard/8"), -1, 0, 0, "<shift>F8", load_clip, 8 },
	{ _("/Edit/Load Clipboard/9"), -1, 0, 0, "<shift>F9", load_clip, 9 },
	{ _("/Edit/Load Clipboard/10"), -1, 0, 0, "<shift>F10", load_clip, 10 },
	{ _("/Edit/Load Clipboard/11"), -1, 0, 0, "<shift>F11", load_clip, 11 },
	{ _("/Edit/Load Clipboard/12"), -1, 0, 0, "<shift>F12", load_clip, 12 },
	{ _("/Edit/Save Clipboard"), -2 },
	{ _("/Edit/Save Clipboard/tear"), -3 },
	{ _("/Edit/Save Clipboard/1"), -1, 0, NEED_CLIP, "<control>F1", save_clip, 1 },
	{ _("/Edit/Save Clipboard/2"), -1, 0, NEED_CLIP, "<control>F2", save_clip, 2 },
	{ _("/Edit/Save Clipboard/3"), -1, 0, NEED_CLIP, "<control>F3", save_clip, 3 },
	{ _("/Edit/Save Clipboard/4"), -1, 0, NEED_CLIP, "<control>F4", save_clip, 4 },
	{ _("/Edit/Save Clipboard/5"), -1, 0, NEED_CLIP, "<control>F5", save_clip, 5 },
	{ _("/Edit/Save Clipboard/6"), -1, 0, NEED_CLIP, "<control>F6", save_clip, 6 },
	{ _("/Edit/Save Clipboard/7"), -1, 0, NEED_CLIP, "<control>F7", save_clip, 7 },
	{ _("/Edit/Save Clipboard/8"), -1, 0, NEED_CLIP, "<control>F8", save_clip, 8 },
	{ _("/Edit/Save Clipboard/9"), -1, 0, NEED_CLIP, "<control>F9", save_clip, 9 },
	{ _("/Edit/Save Clipboard/10"), -1, 0, NEED_CLIP, "<control>F10", save_clip, 10 },
	{ _("/Edit/Save Clipboard/11"), -1, 0, NEED_CLIP, "<control>F11", save_clip, 11 },
	{ _("/Edit/Save Clipboard/12"), -1, 0, NEED_CLIP, "<control>F12", save_clip, 12 },
	{ _("/Edit/sep3"), -4 },
	{ _("/Edit/Choose Pattern ..."), -1, 0, 0, "F2", pressed_choose_patterns, 0 },
	{ _("/Edit/Choose Brush ..."), -1, 0, 0, "F3", pressed_choose_brush, 0 },

	{ _("/_View"), -2 -16 },
	{ _("/View/tear"), -3 },
	{ _("/View/Show Main Toolbar"), 0, MENU_TBMAIN, 0, "F5", pressed_toolbar_toggle, TOOLBAR_MAIN },
	{ _("/View/Show Tools Toolbar"), 0, MENU_TBTOOLS, 0, "F6", pressed_toolbar_toggle, TOOLBAR_TOOLS },
	{ _("/View/Show Settings Toolbar"), 0, MENU_TBSET, 0, "F7", pressed_toolbar_toggle, TOOLBAR_SETTINGS },
	{ _("/View/Show Palette"), 0, MENU_SHOWPAL, 0, "F8", pressed_toolbar_toggle, TOOLBAR_PALETTE },
	{ _("/View/Show Status Bar"), 0, MENU_SHOWSTAT, 0, NULL, pressed_toolbar_toggle, TOOLBAR_STATUS },
	{ _("/View/sep1"), -4 },
	{ _("/View/Toggle Image View"), -1, 0, 0, "Home", toggle_view, 0 },
	{ _("/View/Centralize Image"), 0, MENU_CENTER, 0, NULL, pressed_centralize, 0 },
	{ _("/View/Show zoom grid"), 0, MENU_SHOWGRID, 0, NULL, zoom_grid, 0 },
	{ _("/View/sep2"), -4 },
	{ _("/View/View Window"), 0, MENU_VIEW, 0, "V", pressed_view, 0 },
	{ _("/View/Horizontal Split"), 0, 0, 0, "H", pressed_view_hori, 0 },
	{ _("/View/Focus View Window"), 0, MENU_VWFOCUS, 0, NULL, pressed_view_focus, 0 },
	{ _("/View/sep3"), -4 },
	{ _("/View/Pan Window (End)"), -1, 0, 0, NULL, pressed_pan, 0 },
	{ _("/View/Command Line Window"), -1, MENU_CLINE, 0, "C", pressed_cline, 0 },
	{ _("/View/Layers Window"), -1, MENU_LAYER, 0, "L", pressed_layers, 0 },

	{ _("/_Image"), -2 -16 },
	{ _("/Image/tear"), -3 },
	{ _("/Image/Convert To RGB"), -1, 0, NEED_IDX, NULL, pressed_convert_rgb, 0 },
	{ _("/Image/Convert To Indexed ..."), -1, 0, NEED_24, NULL, pressed_quantize, 0 },
	{ _("/Image/sep1"), -4 },
	{ _("/Image/Scale Canvas ..."), -1, 0, 0, NULL, pressed_scale, 0 },
	{ _("/Image/Resize Canvas ..."), -1, 0, 0, NULL, pressed_size, 0 },
	{ _("/Image/Crop"), -1, 0, NEED_CROP, "<control><shift>X", pressed_crop, 0 },
	{ _("/Image/sep2"), -4 },
	{ _("/Image/Flip Vertically"), -1, 0, 0, NULL, pressed_flip_image_v, 0 },
	{ _("/Image/Flip Horizontally"), -1, 0, 0, "<control>M", pressed_flip_image_h, 0 },
	{ _("/Image/Rotate Clockwise"), -1, 0, 0, NULL, pressed_rotate_image, 0 },
	{ _("/Image/Rotate Anti-Clockwise"), -1, 0, 0, NULL, pressed_rotate_image, 1 },
	{ _("/Image/Free Rotate ..."), -1, 0, 0, NULL, pressed_rotate_free, 0 },
	{ _("/Image/sep3"), -4 },
	{ _("/Image/Information ..."), -1, 0, 0, "<control>I", pressed_information, 0 },
	{ _("/Image/Preferences ..."), -1, MENU_PREFS, 0, "<control>P", pressed_preferences, 0 },

	{ _("/_Selection"), -2 -16 },
	{ _("/Selection/tear"), -3 },
	{ _("/Selection/Select All"), -1, 0, 0, "<control>A", pressed_select_all, 0 },
	{ _("/Selection/Select None (Esc)"), -1, 0, NEED_MARQ, "<shift><control>A", pressed_select_none, 0 },
	{ _("/Selection/Lasso Selection"), -1, 0, NEED_LASSO, NULL, pressed_lasso, 0 },
	{ _("/Selection/Lasso Selection Cut"), -1, 0, NEED_LASSO, NULL, pressed_lasso, 1 },
	{ _("/Selection/sep1"), -4 },
	{ _("/Selection/Outline Selection"), -1, 0, NEED_SEL2, "<control>T", pressed_rectangle, 0 },
	{ _("/Selection/Fill Selection"), -1, 0, NEED_SEL2, "<shift><control>T", pressed_rectangle, 1 },
	{ _("/Selection/Outline Ellipse"), -1, 0, NEED_SEL, "<control>L", pressed_ellipse, 0 },
	{ _("/Selection/Fill Ellipse"), -1, 0, NEED_SEL, "<shift><control>L", pressed_ellipse, 1 },
	{ _("/Selection/sep2"), -4 },
	{ _("/Selection/Flip Vertically"), -1, 0, NEED_CLIP, NULL, pressed_flip_sel_v, 0 },
	{ _("/Selection/Flip Horizontally"), -1, 0, NEED_CLIP, NULL, pressed_flip_sel_h, 0 },
	{ _("/Selection/Rotate Clockwise"), -1, 0, NEED_CLIP, NULL, pressed_rotate_sel, 0 },
	{ _("/Selection/Rotate Anti-Clockwise"), -1, 0, NEED_CLIP, NULL, pressed_rotate_sel, 1 },
	{ _("/Selection/sep3"), -4 },
	{ _("/Selection/Alpha Blend A,B"), -1, 0, NEED_ACLIP, NULL, pressed_clip_alpha_scale, 0 },
	{ _("/Selection/Move Alpha to Mask"), -1, 0, NEED_CLIP, NULL, pressed_clip_alphamask, 0 },
	{ _("/Selection/Mask Colour A,B"), -1, 0, NEED_CLIP, NULL, pressed_clip_mask, 0 },
	{ _("/Selection/Unmask Colour A,B"), -1, 0, NEED_CLIP, NULL, pressed_clip_mask, 255 },
	{ _("/Selection/Mask All Colours"), -1, 0, NEED_CLIP, NULL, pressed_clip_mask_all, 0 },
	{ _("/Selection/Clear Mask"), -1, 0, NEED_CLIP, NULL, pressed_clip_mask_clear, 0 },

	{ _("/_Palette"), -2 -16 },
	{ _("/Palette/tear"), -3 },
	{ _("/Palette/Open ..."), -1, 0, 0, NULL, pressed_open_pal, 0 },
	{ _("/Palette/Save As ..."), -1, 0, 0, NULL, pressed_save_pal, 0 },
	{ _("/Palette/Load Default"), -1, 0, 0, NULL, pressed_default_pal, 0 },
	{ _("/Palette/sep1"), -4 },
	{ _("/Palette/Mask All"), -1, 0, 0, NULL, pressed_mask, 255 },
	{ _("/Palette/Mask None"), -1, 0, 0, NULL, pressed_mask, 0 },
	{ _("/Palette/sep2"), -4 },
	{ _("/Palette/Swap A & B"), -1, 0, 0, "X", pressed_swap_AB, 0 },
	{ _("/Palette/Edit Colour A & B ..."), -1, 0, 0, "<control>E", pressed_edit_AB, 0 },
	{ _("/Palette/Dither A"), -1, 0, NEED_24, NULL, pressed_dither_A, 0 },
	{ _("/Palette/Palette Editor ..."), -1, 0, 0, "<control>W", pressed_allcol, 0 },
	{ _("/Palette/Set Palette Size ..."), -1, 0, 0, NULL, pressed_add_cols, 0 },
	{ _("/Palette/Merge Duplicate Colours"), -1, 0, NEED_IDX, NULL, pressed_remove_duplicates, 0 },
	{ _("/Palette/Remove Unused Colours"), -1, 0, NEED_IDX, NULL, pressed_remove_unused, 0 },
	{ _("/Palette/sep3"), -4 },
	{ _("/Palette/Create Quantized ..."), -1, 0, NEED_24, NULL, pressed_quantize, 1 },
	{ _("/Palette/sep4"), -4 },
	{ _("/Palette/Sort Colours ..."), -1, 0, 0, NULL, pressed_sort_pal, 0 },
	{ _("/Palette/Palette Shifter ..."), -1, 0, 0, NULL, pressed_shifter, 0 },

	{ _("/Effe_cts"), -2 -16 },
	{ _("/Effects/tear"), -3 },
	{ _("/Effects/Transform Colour ..."), -1, 0, 0, "<control><shift>C", pressed_brcosa, 0 },
	{ _("/Effects/Invert"), -1, 0, 0, "<control><shift>I", pressed_invert, 0 },
	{ _("/Effects/Greyscale"), -1, 0, 0, "<control>G", pressed_greyscale, 0 },
	{ _("/Effects/Greyscale (Gamma corrected)"), -1, 0, 0, "<control><shift>G", pressed_greyscale, 1 },
	{ _("/Effects/Isometric Transformation"), -2 },
	{ _("/Effects/Isometric Transformation/tear"), -3 },
	{ _("/Effects/Isometric Transformation/Left Side Down"), -1, 0, 0, NULL, iso_trans, 0 },
	{ _("/Effects/Isometric Transformation/Right Side Down"), -1, 0, 0, NULL, iso_trans, 1 },
	{ _("/Effects/Isometric Transformation/Top Side Right"), -1, 0, 0, NULL, iso_trans, 2 },
	{ _("/Effects/Isometric Transformation/Bottom Side Right"), -1, 0, 0, NULL, iso_trans, 3 },
	{ _("/Effects/sep1"), -4 },
	{ _("/Effects/Edge Detect"), -1, 0, NEED_NOIDX, NULL, pressed_edge_detect, 0 },
	{ _("/Effects/Difference of Gaussians ..."), -1, 0, NEED_NOIDX, NULL, pressed_dog, 0 },
	{ _("/Effects/Sharpen ..."), -1, 0, NEED_NOIDX, NULL, pressed_sharpen, 0 },
	{ _("/Effects/Unsharp Mask ..."), -1, 0, NEED_NOIDX, NULL, pressed_unsharp, 0 },
	{ _("/Effects/Soften ..."), -1, 0, NEED_NOIDX, NULL, pressed_soften, 0 },
	{ _("/Effects/Gaussian Blur ..."), -1, 0, NEED_NOIDX, NULL, pressed_gauss, 0 },
	{ _("/Effects/Emboss"), -1, 0, NEED_NOIDX, NULL, pressed_emboss, 0 },
	{ _("/Effects/sep2"), -4 },
	{ _("/Effects/Bacteria ..."), -1, 0, NEED_IDX, NULL, pressed_bacteria, 0 },

	{ _("/Cha_nnels"), -2 -16 },
	{ _("/Channels/tear"), -3 },
	{ _("/Channels/New ..."), -1, 0, 0, NULL, pressed_channel_create, -1 },
	{ _("/Channels/Load ..."), -1, 0, 0, NULL, pressed_channel_load, 0 },
	{ _("/Channels/Save As ..."), -1, 0, 0, NULL, pressed_channel_save, 0 },
	{ _("/Channels/Delete ..."), -1, 0, NEED_CHAN, NULL, pressed_channel_delete, -1 },
	{ _("/Channels/sep1"), -4 },
	{ _("/Channels/Edit Image"), 1, MENU_CHAN0, 0, NULL, pressed_channel_edit, CHN_IMAGE },
	{ _("/Channels/Edit Alpha"), 1, MENU_CHAN1, 0, NULL, pressed_channel_edit, CHN_ALPHA },
	{ _("/Channels/Edit Selection"), 1, MENU_CHAN2, 0, NULL, pressed_channel_edit, CHN_SEL },
	{ _("/Channels/Edit Mask"), 1, MENU_CHAN3, 0, NULL, pressed_channel_edit, CHN_MASK },
	{ _("/Channels/sep2"), -4 },
	{ _("/Channels/Hide Image"), 0, MENU_DCHAN0, 0, NULL, pressed_channel_toggle, 1 },
	{ _("/Channels/Disable Alpha"), 0, MENU_DCHAN1, 0, NULL, pressed_channel_disable, CHN_ALPHA },
	{ _("/Channels/Disable Selection"), 0, MENU_DCHAN2, 0, NULL, pressed_channel_disable, CHN_SEL },
	{ _("/Channels/Disable Mask"), 0, MENU_DCHAN3, 0, NULL, pressed_channel_disable, CHN_MASK },
	{ _("/Channels/sep3"), -4 },
	{ _("/Channels/Couple RGBA Operations"), 0, MENU_RGBA, 0, NULL, pressed_RGBA_toggle, 0 },
	{ _("/Channels/Threshold ..."), -1, 0, 0, NULL, pressed_threshold, 0 },
	{ _("/Channels/Unassociate Alpha"), -1, 0, NEED_RGBA, NULL, pressed_unassociate, 0 },
	{ _("/Channels/sep4"), -4 },
	{ _("/Channels/View Alpha as an Overlay"), 0, 0, 0, NULL, pressed_channel_toggle, 0 },
	{ _("/Channels/Configure Overlays ..."), -1, 0, 0, NULL, pressed_channel_config_overlay, 0 },

	{ _("/_Layers"), -2 -16 },
	{ _("/Layers/tear"), -3 },
	{ _("/Layers/Save"), -1, 0, 0, "<shift><control>S", layer_press_save, 0 },
	{ _("/Layers/Save As ..."), -1, 0, 0, NULL, layer_press_save_as, 0 },
	{ _("/Layers/Save Composite Image ..."), -1, 0, 0, NULL, layer_press_save_composite, 0 },
	{ _("/Layers/Remove All Layers ..."), -1, 0, 0, NULL, layer_press_remove_all, 0 },
	{ _("/Layers/sep1"), -4 },
	{ _("/Layers/Configure Animation ..."), -1, 0, 0, NULL, pressed_animate_window, 0 },
	{ _("/Layers/Preview Animation ..."), -1, 0, 0, NULL, ani_but_preview, 0 },
	{ _("/Layers/Set key frame ..."), -1, 0, 0, NULL, pressed_set_key_frame, 0 },
	{ _("/Layers/Remove all key frames ..."), -1, 0, 0, NULL, pressed_remove_key_frames, 0 },

	{ _("/More..."), -2 -16 }, /* This will hold overflow submenu */

	{ _("/_Help"), -5 },
	{ _("/Help/Documentation"), -1, 0, 0, NULL, pressed_docs, 0 },
	{ _("/Help/About"), -1, MENU_HELP, 0, "F1", pressed_help, 0 },
	{ _("/Help/sep1"), -4 },
	{ _("/Help/Rebind Shortcut Keycodes"), -1, 0, 0, NULL, rebind_keys, 0 },

	{ NULL, 0 }
	};

#undef _
#define _(X) __(X)

void main_init()
{
	static const GtkTargetEntry target_table[1] = { { "text/uri-list", 0, 1 } };
	static GdkColor cfg = { -1, -1, -1, -1 }, cbg = { 0, 0, 0, 0 };
	GtkRequisition req;
	GdkPixmap *icon_pix = NULL;
	GtkAdjustment *adj;
	GtkWidget *menubar1, *vbox_main, *hbox_bar, *hbox_bottom;
	GtkAccelGroup *accel_group;
	char txt[PATHBUF];
	int i;


	gdk_rgb_init();
	init_tablet();					// Set up the tablet

	toolbar_boxes[TOOLBAR_MAIN] = NULL;		// Needed as test to avoid segfault in toolbar.c

	accel_group = gtk_accel_group_new ();
	menubar1 = fill_menu(main_menu, accel_group);

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_widgets[MENU_RGBA]), RGBA_mode);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_widgets[MENU_VWFOCUS]), vw_focus_on);

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_widgets[MENU_CENTER]),
		canvas_image_centre);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_widgets[MENU_SHOWGRID]),
		mem_show_grid);

	mem_grid_rgb[0] = inifile_get_gint32("gridR", 50 );
	mem_grid_rgb[1] = inifile_get_gint32("gridG", 50 );
	mem_grid_rgb[2] = inifile_get_gint32("gridB", 50 );


///	MAIN WINDOW

	main_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
	gtk_widget_set_usize(main_window, 100, 100);		// Set minimum width/height
	win_restore_pos(main_window, "window", 0, 0, 630, 400);
	gtk_window_set_title (GTK_WINDOW (main_window), VERSION );

	gtk_drag_dest_set (main_window, GTK_DEST_DEFAULT_ALL, target_table, 1, GDK_ACTION_MOVE);
	gtk_signal_connect (GTK_OBJECT (main_window), "drag_data_received",
		GTK_SIGNAL_FUNC (drag_n_drop_received), NULL);		// Drag 'n' Drop guff

	vbox_main = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_main);
	gtk_container_add (GTK_CONTAINER (main_window), vbox_main);

	gtk_accel_group_lock( accel_group );	// Stop dynamic allocation of accelerators during runtime
	gtk_window_add_accel_group(GTK_WINDOW(main_window), accel_group);

	pack(vbox_main, menubar1);


// we need to realize the window because we use pixmaps for 
// items on the toolbar in the context of it
	gtk_widget_realize( main_window );


	toolbar_init(vbox_main);


///	PALETTE

	hbox_bottom = xpack(vbox_main, gtk_hbox_new(FALSE, 0));
	gtk_widget_show(hbox_bottom);

	toolbar_palette_init(hbox_bottom);

	vbox_right = xpack(hbox_bottom, gtk_vbox_new(FALSE, 0));
	gtk_widget_show(vbox_right);


///	DRAWING AREA

	main_vsplit = gtk_hpaned_new ();
	paned_mouse_fix(main_vsplit);
	gtk_widget_show (main_vsplit);
	gtk_widget_ref(main_vsplit);
	gtk_object_sink(GTK_OBJECT(main_vsplit));

	main_hsplit = gtk_vpaned_new ();
	paned_mouse_fix(main_hsplit);
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
	fix_scroll(vw_scrolledwindow);

	init_view();

//	MAIN WINDOW

	drawing_canvas = gtk_drawing_area_new ();
	gtk_widget_set_usize( drawing_canvas, 48, 48 );
	gtk_widget_show( drawing_canvas );

	scrolledwindow_canvas = xpack(vbox_right, gtk_scrolled_window_new(NULL, NULL));
	gtk_widget_show (scrolledwindow_canvas);

	/* Handle "changed" signal only in GTK+2, because in GTK+1 resizes are
	 * tracked by configure handler, and forced realign from there looks
	 * better than idle-time realign from here - WJ */
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow_canvas),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas));
#if GTK_MAJOR_VERSION == 2
	gtk_signal_connect(GTK_OBJECT(adj), "changed",
		GTK_SIGNAL_FUNC(vw_focus_idle), NULL);
#endif
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed",
		GTK_SIGNAL_FUNC(vw_focus_idle), NULL);
	adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas));
#if GTK_MAJOR_VERSION == 2
	gtk_signal_connect(GTK_OBJECT(adj), "changed",
		GTK_SIGNAL_FUNC(vw_focus_idle), NULL);
#endif
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed",
		GTK_SIGNAL_FUNC(vw_focus_idle), NULL);

	gtk_scrolled_window_add_with_viewport( GTK_SCROLLED_WINDOW(scrolledwindow_canvas),
		drawing_canvas);
	fix_scroll(scrolledwindow_canvas);

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

	hbox_bar = pack_end(vbox_right, gtk_hbox_new(FALSE, 0));
	if ( toolbar_status[TOOLBAR_STATUS] ) gtk_widget_show (hbox_bar);


	for (i = 0; i < STATUS_ITEMS; i++)
	{
		label_bar[i] = gtk_label_new("");
		gtk_misc_set_alignment(GTK_MISC(label_bar[i]),
			(i == STATUS_CURSORXY) || (i == STATUS_UNDOREDO) ? 0.5 : 0.0, 0.0);
		gtk_widget_show(label_bar[i]);
	}
	for (i = 0; i < STATUS_ITEMS; i++)
	{
		if (i < 3) pack(hbox_bar, label_bar[i]);
		else pack_end(hbox_bar, label_bar[(STATUS_ITEMS - 1) + 3 - i]);
	}
	if ( status_on[STATUS_CURSORXY] ) gtk_widget_set_usize(label_bar[STATUS_CURSORXY], 90, -2);
	if ( status_on[STATUS_UNDOREDO] ) gtk_widget_set_usize(label_bar[STATUS_UNDOREDO], 70, -2);
	gtk_label_set_text( GTK_LABEL(label_bar[STATUS_UNDOREDO]), "0+0" );



	/* To prevent statusbar wobbling */
	gtk_widget_size_request(hbox_bar, &req);
	gtk_widget_set_usize(hbox_bar, -1, req.height);


/////////	End of main window widget setup

	gtk_signal_connect( GTK_OBJECT (main_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_event), NULL );
	gtk_signal_connect( GTK_OBJECT(main_window), "key_press_event",
		GTK_SIGNAL_FUNC (handle_keypress), NULL );

	men_item_state( menu_undo, FALSE );
	men_item_state( menu_redo, FALSE );
	men_item_state( menu_need_marquee, FALSE );
	men_item_state( menu_need_selection, FALSE );
	men_item_state( menu_need_clipboard, FALSE );

	recent_files = recent_files < 0 ? 0 : recent_files > 20 ? 20 : recent_files;
	update_recent_files();
	toolbar_boxes[TOOLBAR_STATUS] = hbox_bar;	// Hide status bar
	main_hidden[0] = menubar1;			// Hide menu bar

	view_hide();					// Hide paned view initially

	gtk_widget_show (main_window);

	/* !!! Have to wait till canvas is displayed, to init keyboard */
	fill_keycodes(main_keys);

	gtk_widget_grab_focus(scrolledwindow_canvas);	// Stops first icon in toolbar being selected
	gdk_window_raise( main_window->window );

	icon_pix = gdk_pixmap_create_from_xpm_d( main_window->window, NULL, NULL, icon_xpm );
	gdk_window_set_icon( main_window->window, NULL, icon_pix, NULL );
	gdk_pixmap_unref(icon_pix);

	set_cursor();
	init_status_bar();
	init_factions();				// Initialize file action menu

	snprintf(txt, PATHBUF, "%s%c.clipboard", get_home_directory(), DIR_SEP);
	strncpy0(mem_clip_file, inifile_get("clipFilename", txt), PATHBUF);

	if (files_passed > 1) pressed_cline(NULL, NULL);
	else gtk_widget_set_sensitive(menu_widgets[MENU_CLINE], FALSE);

	gtk_toggle_button_set_active( GTK_TOGGLE_BUTTON(icon_buttons[DEFAULT_TOOL_ICON]), TRUE );

	dash_gc = gdk_gc_new(drawing_canvas->window);		// Set up gc for polygon marquee
	gdk_gc_set_background(dash_gc, &cbg);
	gdk_gc_set_foreground(dash_gc, &cfg);
	gdk_gc_set_line_attributes( dash_gc, 1, GDK_LINE_DOUBLE_DASH, GDK_CAP_NOT_LAST, GDK_JOIN_MITER);

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
	char txt[300], txt2[PATHTXT], *extra = "-";

	gtkuncpy(txt2, mem_filename, PATHTXT);

	if ( mem_changed == 1 ) extra = _("(Modified)");

	snprintf( txt, 290, "%s %s %s", VERSION, extra, txt2[0] ? txt2 :
		_("Untitled"));

	gtk_window_set_title (GTK_WINDOW (main_window), txt );
}

void notify_changed()		// Image/palette has just changed - update vars as needed
{
	mem_tempname = NULL;
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


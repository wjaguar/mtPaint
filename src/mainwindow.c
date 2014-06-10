/*	mainwindow.c
	Copyright (C) 2004-2014 Mark Tyler and Dmitry Groshev

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
#include "ani.h"
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
#include "channels.h"
#include "toolbar.h"
#include "csel.h"
#include "shifter.h"
#include "spawn.h"
#include "font.h"
#include "icons.h"
#include "thread.h"
#include "vcode.h"


typedef struct {
	int idx_c, nidx_c, cnt_c;
	int cline_d, settings_d, layers_d;
	int *strs_c;
	void **dock, **dockbook, **dockpage1;
} main_dd;

#define GREY_W 153
#define GREY_B 102
const unsigned char greyz[2] = {GREY_W, GREY_B}; // For opacity squares

char *channames[NUM_CHANNELS + 1], *allchannames[NUM_CHANNELS + 1];
char *cspnames[NUM_CSPACES];

char *channames_[NUM_CHANNELS + 1] =
		{ _("Image"), _("Alpha"), _("Selection"), _("Mask"), NULL };
char *cspnames_[NUM_CSPACES] =
		{ _("RGB"), _("sRGB"), "LXN" };

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
	{ "showMenuIcons",	&show_menu_icons,	FALSE },
	{ "showTileGrid",	&show_tile_grid,	FALSE },
	{ "applyICC",		&apply_icc,		FALSE },
	{ "scrollwheelZOOM",	&scroll_zoom,		FALSE },
	{ "cursorZoom",		&cursor_zoom,		FALSE },
	{ "pasteCommit",	&paste_commit,		TRUE  },
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
	{ "autopreviewToggle",	&brcosa_auto,		TRUE  },
	{ "colorGrid",		&color_grid,		TRUE  },
	{ "defaultGamma",	&use_gamma,		TRUE  },
#if STATUS_ITEMS != 5
#error Wrong number of "status?Toggle" inifile items defined
#endif
	{ "status0Toggle",	status_on + 0,		TRUE  },
	{ "status1Toggle",	status_on + 1,		TRUE  },
	{ "status2Toggle",	status_on + 2,		TRUE  },
	{ "status3Toggle",	status_on + 3,		TRUE  },
	{ "status4Toggle",	status_on + 4,		TRUE  },
#if TOOLBAR_MAX != 6
#error Wrong number of "toolbar?" inifile items defined
#endif
	{ "toolbar1",		toolbar_status + 1,	TRUE  },
	{ "toolbar2",		toolbar_status + 2,	TRUE  },
	{ "toolbar3",		toolbar_status + 3,	TRUE  },
	{ "toolbar4",		toolbar_status + 4,	TRUE  },
	{ "toolbar5",		toolbar_status + 5,	TRUE  },
	{ "fontAntialias0",	&font_aa,		TRUE  },
	{ "fontAntialias1",	&font_bk,		FALSE },
	{ "fontAntialias2",	&font_r,		FALSE },
#ifdef U_FREETYPE
	{ "fontAntialias3",	&font_obl,		FALSE },
#endif
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
	{ "undoCommon",		&mem_undo_common,	25  },
	{ "maxThreads",		&maxthreads,		0   },
	{ "backgroundGrey",	&mem_background,	180 },
	{ "pixelNudge",		&mem_nudge,		8   },
	{ "recentFiles",	&recent_files,		10  },
	{ "lastspalType",	&spal_mode,		2   },
	{ "posterizeMode",	&posterize_mode,	0   },
	{ "panSize",		&max_pan,		128 },
	{ "undoDepth",		&mem_undo_depth,	DEF_UNDO },
	{ "tileWidth",		&tgrid_dx,		32  },
	{ "tileHeight",		&tgrid_dy,		32  },
	{ "gridRGB",		grid_rgb + GRID_NORMAL,	RGB_2_INT( 50,  50,  50) },
	{ "gridBorder",		grid_rgb + GRID_BORDER,	RGB_2_INT(  0, 219,   0) },
	{ "gridTrans",		grid_rgb + GRID_TRANS,	RGB_2_INT(  0, 109, 109) },
	{ "gridTile",		grid_rgb + GRID_TILE,	RGB_2_INT(170, 170, 170) },
	{ "gridSegment",	grid_rgb + GRID_SEGMENT,RGB_2_INT(219, 219,   0) },
	{ "fontBackground",	&font_bkg,		0   },
	{ "fontAngle",		&font_angle,		0   },
#ifdef U_FREETYPE
	{ "fontSizeBitmap",	&font_bmsize,		1   },
	{ "fontSize",		&font_size,		12  },
	{ "font_dirs",		&font_dirs,		0   },
#endif
	{ NULL,			NULL }
};


void **main_window_, **settings_dock, **layers_dock, **main_split;
GtkWidget *main_window,
	*drawing_palette, *drawing_canvas, *vw_scrolledwindow,
	*scrolledwindow_canvas,

	*menu_widgets[TOTAL_MENU_IDS];

static GtkWidget *main_menubar;

int	view_image_only, viewer_mode, drag_index, q_quit, cursor_tool;
int	show_menu_icons, paste_commit, scroll_zoom;
int	files_passed, drag_index_vals[2], cursor_corner, use_gamma;
int	view_vsplit;
char **file_args;

static int show_dock;
static int mouse_left_canvas;

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

typedef struct {
	GtkWidget *widget;
	int actmap;
} dis_information;

static dis_information *dis_array;
static int dis_count, dis_allow;
static int dis_miss = ~0;

void mapped_dis_add(GtkWidget *widget, int actmap)
{
	if (!actmap) return;
	if (dis_count >= dis_allow) dis_array = realloc(dis_array,
		(dis_allow += 128) * sizeof(dis_information));
	/* If no memory, just die of SIGSEGV */
	dis_array[dis_count].widget = widget;
	dis_array[dis_count].actmap = actmap;
	dis_count++;
}

/* Enable or disable menu items */
void mapped_item_state(int statemap)
{
	int i;

	if (dis_miss == statemap) return; // Nothing changed
	for (i = 0; i < dis_count; i++)
		gtk_widget_set_sensitive(dis_array[i].widget,
			!!(dis_array[i].actmap & statemap));
	dis_miss = statemap;
}

static void pressed_load_recent(int item)
{
	char txt[64];

	if ((layers_total ? check_layers_for_changes() :
		check_for_changes()) == 1) return;

	sprintf(txt, "file%i", item);
	do_a_load(inifile_get(txt, "."), undo_load);	// Load requested file
}

static void pressed_crop()
{
	int res, rect[4];


	if ( marq_status != MARQUEE_DONE ) return;
	marquee_at(rect);
	if ((rect[0] == 0) && (rect[2] >= mem_width) &&
		(rect[1] == 0) && (rect[3] >= mem_height)) return;

	res = mem_image_resize(rect[2], rect[3], -rect[0], -rect[1], 0);

	if (!res)
	{
		pressed_select(FALSE);
		change_to_tool(DEFAULT_TOOL_ICON);
		update_stuff(UPD_GEOM);
	}
	else memory_errors(res);
}

void pressed_select(int all)
{
	int i = 0;

	/* Remove old selection */
	if (marq_status != MARQUEE_NONE)
	{
		i = UPD_SEL;
		if (marq_status >= MARQUEE_PASTE) i = UPD_SEL | CF_DRAW;
		else paint_marquee(MARQ_HIDE, 0, 0, NULL);
		marq_status = MARQUEE_NONE;
	}
	if ((tool_type == TOOL_POLYGON) && (poly_status != POLY_NONE))
	{
		poly_points = 0;
		poly_status = POLY_NONE;
		i = UPD_SEL | CF_DRAW; // Have to erase polygon
	}
	/* And deal with selection persistence too */
	marq_x1 = marq_y1 = marq_x2 = marq_y2 = -1;

	while (all) /* Select entire canvas */
	{
		i |= UPD_SEL;
		marq_x1 = 0;
		marq_y1 = 0;
		marq_x2 = mem_width - 1;
		marq_y2 = mem_height - 1;
		if (tool_type != TOOL_SELECT)
		{
			/* Switch tool, and let that & marquee persistence
			 * do all the rest except full redraw */
			change_to_tool(TTB_SELECT);
			i &= CF_DRAW;
			break;
		}
		marq_status = MARQUEE_DONE;
		if (i & CF_DRAW) break; // Full redraw will draw marquee too
		paint_marquee(MARQ_SHOW, 0, 0, NULL);
		break;
	}
	if (i) update_stuff(i);
}

static void pressed_remove_unused()
{
	if (mem_remove_unused_check() <= 0) alert_box(_("Error"),
		_("There were no unused colours to remove!"), NULL);
	else
	{
		spot_undo(UNDO_XPAL);

		mem_remove_unused();
		mem_undo_prepare();

		update_stuff(UPD_TPAL);
	}
}

static void pressed_default_pal()
{
	spot_undo(UNDO_PAL);
	mem_pal_copy( mem_pal, mem_pal_def );
	mem_cols = mem_pal_def_i;
	update_stuff(UPD_PAL);
}

static void pressed_remove_duplicates()
{
	char *mess;
	int dups = scan_duplicates();

	if (!dups)
	{
		alert_box(_("Error"), _("The palette does not contain 2 colours that have identical RGB values"), NULL);
		return;
	}
	mess = g_strdup_printf(__("The palette contains %i colours that have identical RGB values.  Do you really want to merge them into one index and realign the canvas?"), dups);
	if (alert_box(_("Warning"), mess, _("Yes"), _("No"), NULL) == 1)
	{
		spot_undo(UNDO_XPAL);

		remove_duplicates();
		mem_undo_prepare();
		update_stuff(UPD_PAL);
	}
	g_free(mess);
}

static void pressed_dither_A()
{
	mem_find_dither(mem_col_A24.red, mem_col_A24.green, mem_col_A24.blue);
	update_stuff(UPD_ABP);
}

static void pressed_mask(int val)
{
	mem_mask_setall(val);
	update_stuff(UPD_CMASK);
}

// System clipboard import

static GtkTargetEntry clip_formats[] = {
	{ NULL, 0, FT_NONE },
	{ "application/x-mtpaint-pmm", 0, FT_PMM | FTM_EXTEND },
	{ "application/x-mtpaint-clipboard", 0, FT_PNG | FTM_EXTEND },
	{ "image/png", 0, FT_PNG },
	{ "image/bmp", 0, FT_BMP },
	{ "image/x-bmp", 0, FT_BMP },
	{ "image/x-MS-bmp", 0, FT_BMP },
#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
	/* These two don't make sense without X */
	{ "PIXMAP", 0, FT_PIXMAP },
	{ "BITMAP", 0, FT_PIXMAP },
	/* !!! BITMAP requests are handled same as PIXMAP - because it is only
	 * done to appease buggy XPaint which requests both and crashes if
	 * receiving only one - WJ */
#endif
};
#define CLIP_TARGETS (sizeof(clip_formats) / sizeof(GtkTargetEntry))
static GdkAtom clip_atoms[CLIP_TARGETS];

/* Seems it'll be better to prefer BMP when talking to the likes of GIMP -
 * they send PNGs really slowly (likely, compressed to the max); but not
 * everyone supports alpha in BMPs. */

static int clipboard_check_fn(GtkSelectionData *data, gpointer user_data)
{
	GdkAtom *targets, tst;
	int i, j, k, n = data->length / sizeof(GdkAtom);

	if ((n <= 0) || (data->format != 32) ||
		(data->type != GDK_SELECTION_TYPE_ATOM)) return (FALSE);

	/* Convert names to atoms if not done already */
	if (!clip_atoms[1])
	{
		for (i = 1; i < CLIP_TARGETS; i++) clip_atoms[i] =
			gdk_atom_intern(clip_formats[i].target, FALSE);
	}

	/* Search for best match */
	targets = (GdkAtom *)data->data;
	for (i = 0 , k = CLIP_TARGETS; i < n; i++)
	{
		tst = *targets++;
//g_print("\"%s\" ", gdk_atom_name(tst));
		for (j = 1; (j < k) && (tst != clip_atoms[j]); j++);
		k = j;
	}
//g_print(": %d\n", k);
	*(int *)user_data = k;
	return (k < CLIP_TARGETS);
}

static int check_clipboard(int which)
{
	int res;

	if (internal_clipboard(which)) return (0); // if we're who put data there
	if (!process_clipboard(which, "TARGETS",
		GTK_SIGNAL_FUNC(clipboard_check_fn), &res)) return (0); // no luck
	return (res);
}

static int clipboard_import_fn(GtkSelectionData *data, gpointer user_data)
{
//g_print("!!! %X %d\n", data->data, data->length);
	return (load_mem_image((unsigned char *)data->data, data->length,
		((int *)user_data)[0], ((int *)user_data)[1]) == 1);
}

int import_clipboard(int mode)
{
	int i = 0, n = 0, udata[2] = { mode };

#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
	// If no luck with CLIPBOARD, check PRIMARY too
	for (; !i && (n < 2); n++)
#endif
	if ((i = check_clipboard(n)))
	{
		udata[1] = (int)clip_formats[i].info |
			((mode == FS_PNG_LOAD) && undo_load ? FTM_UNDO : 0);
		i = process_clipboard(n, clip_formats[i].target,
			GTK_SIGNAL_FUNC(clipboard_import_fn), udata);
	}
	return (i);
}

static void setup_clip_save(ls_settings *settings)
{
	init_ls_settings(settings, NULL);
	memcpy(settings->img, mem_clip.img, sizeof(chanlist));
	settings->pal = mem_pal;
	settings->width = mem_clip_w;
	settings->height = mem_clip_h;
	settings->bpp = mem_clip_bpp;
	settings->colors = mem_cols;
}

static int clipboard_export_fn(GtkSelectionData *data, gpointer user_data)
{
	ls_settings settings;
	unsigned char *buf;
	int res, len, type = (int)user_data;

//g_print("Entered! %X %d\n", data, type);
	if (!data) return (FALSE); // Someone else stole system clipboard
	if (!mem_clipboard) return (FALSE); // Our own clipboard got emptied

	/* Prepare settings */
	setup_clip_save(&settings);
	settings.mode = FS_CLIPBOARD;
	settings.ftype = type;
	settings.png_compression = 1; // Speed is of the essence

	res = save_mem_image(&buf, &len, &settings);
//g_print("Save returned %d\n", res);
	if (res) return (FALSE); // No luck creating in-memory image

#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
	if ((type & FTM_FTYPE) == FT_PIXMAP)
	{
		/* !!! XID of pixmap gets returned in buffer pointer */
		gtk_selection_data_set(data, data->target, 32, (guchar *)&buf, len);
		return (TRUE);
	}
#endif

/* !!! Should allocation for data copying fail, GTK+ will die horribly - so
 * maybe it'll be better to hack up the function and pass the original data
 * instead; but to do so, I'd need to use g_try_*() allocation functions in
 * memFILE writing path - WJ */
	gtk_selection_data_set(data, data->target, 8, buf, len);
	free(buf);
	return (TRUE);
}

static int export_clipboard()
{
	int res;

	if (!mem_clipboard) return (FALSE);
	res = offer_clipboard(0, clip_formats + 1, CLIP_TARGETS - 1,
		GTK_SIGNAL_FUNC(clipboard_export_fn));
#if (GTK_MAJOR_VERSION == 1) || defined GDK_WINDOWING_X11
	/* Offer both CLIPBOARD and PRIMARY */
	res |= offer_clipboard(1, clip_formats + 1, CLIP_TARGETS - 1,
		GTK_SIGNAL_FUNC(clipboard_export_fn));
#endif
	return (res);
}

int gui_save(char *filename, ls_settings *settings)
{
	int res = -2, fflags = file_formats[settings->ftype].flags;
	char *mess = NULL, *f8;

	/* Mismatched format - raise an error right here */
	if ((fflags & FF_NOSAVE) || !(fflags & FF_SAVE_MASK))
	{
		int maxc = 0;
		char *fform = NULL, *fname = file_formats[settings->ftype].name;

		/* RGB to indexed (or to unsaveable) */
		if (mem_img_bpp == 3) fform = __("RGB");
		/* Indexed to RGB, or to unsaveable format */
		else if (!(fflags & FF_IDX) || (fflags & FF_NOSAVE))
			fform = __("indexed");
		/* More than 16 colors */
		else if (fflags & FF_16) maxc = 16;
		/* More than 2 colors */
		else maxc = 2;
		/* Build message */
		if (fform) mess = g_strdup_printf(__("You are trying to save an %s image to an %s file which is not possible.  I would suggest you save with a PNG extension."),
			fform, fname);
		else mess = g_strdup_printf(__("You are trying to save an %s file with a palette of more than %d colours.  Either use another format or reduce the palette to %d colours."),
			fname, maxc, maxc);
	}
	else
	{
		/* Commit paste if required */
		if (paste_commit && (marq_status >= MARQUEE_PASTE))
		{
			commit_paste(FALSE, NULL);
			pen_down = 0;
			mem_undo_prepare();
			pressed_select(FALSE);
		}

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
			mess = g_strdup_printf(__("Unable to save file: %s"), f8);
			g_free(f8);
		}
		else if ((res == WRONG_FORMAT) && (settings->ftype == FT_XPM))
			mess = g_strdup(_("You are trying to save an XPM file with more than 4096 colours.  Either use another format or posterize the image to 4 bits, or otherwise reduce the number of colours."));
		if (mess)
		{
			alert_box(_("Error"), mess, NULL);
			g_free(mess);
		}
	}
	else
	{
		notify_unchanged(filename);
		register_file(filename);
	}

	return res;
}

static void pressed_save_file()
{
	ls_settings settings;

	while (mem_filename)
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

static void load_clip(int item)
{
	char clip[PATHBUF];
	int i;

	if (item == -1) // System clipboard
		i = import_clipboard(FS_CLIPBOARD);
	else // Disk file
	{
		snprintf(clip, PATHBUF, "%s%i", mem_clip_file, item);
		i = load_image(clip, FS_CLIP_FILE, FT_PNG) == 1;
	}

	if (!i) alert_box(_("Error"), _("Unable to load clipboard"), NULL);

	update_stuff(UPD_XCOPY);
	if (i && (MEM_BPP >= mem_clip_bpp)) pressed_paste(TRUE);
}

static void save_clip(int item)
{
	ls_settings settings;
	char clip[PATHBUF];
	int i;

	if (item == -1) // Exporting clipboard
	{
		export_clipboard();
		return;
	}

	/* Prepare settings */
	setup_clip_save(&settings);
	settings.mode = FS_CLIP_FILE;
	settings.ftype = FT_PNG;

	snprintf(clip, PATHBUF, "%s%i", mem_clip_file, item);
	i = save_image(clip, &settings);

	if (i) alert_box(_("Error"), _("Unable to save clipboard"), NULL);
}

void pressed_opacity(int opacity)
{
	if (IS_INDEXED) opacity = 255;
	tool_opacity = opacity < 1 ? 1 : opacity > 255 ? 255 : opacity;
	update_stuff(UPD_OPAC);
}

void pressed_value(int value)
{
	if (mem_channel == CHN_IMAGE) return;
	channel_col_A[mem_channel] =
		value < 0 ? 0 : value > 255 ? 255 : value;
	update_stuff(UPD_CAB);
}

static void toggle_view()
{
	if ((view_image_only = !view_image_only))
	{
		int i;

		gtk_widget_hide(main_menubar);
		for (i = TOOLBAR_MAIN; i < TOOLBAR_MAX; i++)
			if (toolbar_boxes[i]) cmd_showhide(toolbar_boxes[i], FALSE);
	}
	else
	{
		gtk_widget_show(main_menubar);
		toolbar_showhide();	// Switch toolbar/status/palette on if needed
	}
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

static void zoom_grid(int state)
{
	mem_show_grid = state;
	update_stuff(UPD_RENDER);
}

static void delete_event(main_dd *dt, void **wdata);

static void quit_all(int mode)
{
	if (mode || q_quit) delete_event(GET_DDATA(main_window_), main_window_);
}

/* Autoscroll canvas if required */
static int real_move_mouse(int *vxy, int x, int y, int dx, int dy)
{
	int nxy[4];

	if ((x >= vxy[0]) && (x < vxy[2]) && (y >= vxy[1]) && (y < vxy[3]) &&
		wjcanvas_scroll_in(drawing_canvas, x + dx, y + dy))
	{
		wjcanvas_get_vport(drawing_canvas, nxy);
		dx += vxy[0] - nxy[0];
		dy += vxy[1] - nxy[1];
	}
	return (move_mouse_relative(dx, dy));
}

/* Forward declaration */
static void mouse_event(int event, int xc, int yc, guint state, guint button,
	gdouble pressure, int mflag, int dx, int dy);

/* For "dual" mouse control */
static int unreal_move, lastdx, lastdy;

static void move_mouse(int dx, int dy, int button)
{
	static GdkModifierType bmasks[4] =
		{ 0, GDK_BUTTON1_MASK, GDK_BUTTON2_MASK, GDK_BUTTON3_MASK };
	GdkModifierType state;
	int x, y, vxy[4], zoom = 1, scale = 1;

	if (!unreal_move) lastdx = lastdy = 0;
	if (!mem_img[CHN_IMAGE]) return;
	dx += lastdx; dy += lastdy;

	gdk_window_get_pointer(drawing_canvas->window, &x, &y, &state);
	wjcanvas_get_vport(drawing_canvas, vxy);
	x += vxy[0]; y += vxy[1];

	if (button) /* Clicks simulated without extra movements */
	{
		mouse_event(GDK_BUTTON_PRESS, x, y, state, button, 1.0, 1, dx, dy);
		state |= bmasks[button]; // Shows state _prior_ to event
		mouse_event(GDK_BUTTON_RELEASE, x, y, state, button, 1.0, 1, dx, dy);
		return;
	}

	if ((state & (GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)) ==
		(GDK_BUTTON1_MASK | GDK_BUTTON3_MASK)) button = 13;
	else if (state & GDK_BUTTON1_MASK) button = 1;
	else if (state & GDK_BUTTON3_MASK) button = 3;
	else if (state & GDK_BUTTON2_MASK) button = 2;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	if (zoom > 1) /* Fine control required */
	{
		lastdx = dx; lastdy = dy;
		mouse_event(GDK_MOTION_NOTIFY, x, y, state, button, 1.0, 1, dx, dy);

		/* Nudge cursor when needed */
		if ((abs(lastdx) >= zoom) || (abs(lastdy) >= zoom))
		{
			dx = lastdx * can_zoom;
			dy = lastdy * can_zoom;
			lastdx -= dx * zoom;
			lastdy -= dy * zoom;
			unreal_move = 3;
			/* Event can be delayed or lost */
			real_move_mouse(vxy, x, y, dx, dy);
		}
		else unreal_move = 2;
	}
	else /* Real mouse is precise enough */
	{
		unreal_move = 1;

		/* Simulate movement if failed to actually move mouse */
		if (!real_move_mouse(vxy, x, y, dx * scale, dy * scale))
		{
			lastdx = dx; lastdy = dy;
			mouse_event(GDK_MOTION_NOTIFY, x, y, state, button, 1.0, 1, dx, dy);
		}
	}
}

void stop_line()
{
	int i = line_status == LINE_LINE;

	line_status = LINE_NONE;
	if (i) repaint_line(NULL);
}

int check_zoom_keys_real(int act_m)
{
	int action = act_m >> 16;

	if ((action == ACT_ZOOM) || (action == ACT_VIEW) || (action == ACT_VWZOOM))
	{
		action_dispatch(action, (act_m & 0xFFFF) - 0x8000, 0, TRUE);
		return (TRUE);
	}
	return (FALSE);
}

int check_zoom_keys(int act_m)
{
	int action = act_m >> 16;

	if (check_zoom_keys_real(act_m)) return (TRUE);
	if ((action == ACT_DOCK) || (action == ACT_QUIT) ||
		(action == DLG_BRCOSA) || (action == ACT_PAN) ||
		(action == ACT_CROP) || (action == ACT_SWAP_AB) ||
		(action == DLG_CHOOSER) || (action == ACT_TOOL))
		action_dispatch(action, (act_m & 0xFFFF) - 0x8000, 0, TRUE);
	else return (FALSE);

	return (TRUE);
}

#define _C (GDK_CONTROL_MASK)
#define _S (GDK_SHIFT_MASK)
#define _A (GDK_MOD1_MASK)
#define _CS (GDK_CONTROL_MASK | GDK_SHIFT_MASK)
#define _CSA (GDK_CONTROL_MASK | GDK_SHIFT_MASK | GDK_MOD1_MASK)

static const int mod_bits[] = { 0, _C, _S, _CS, _CSA };

#define MOD_0   0x00
#define MOD_c   0x01
#define MOD_s   0x02
#define MOD_cs  0x03
#define MOD_csa 0x04
#define MOD_S   0x22
#define MOD_cS  0x23
#define MOD_Cs  0x13
#define MOD_CS  0x33

typedef struct {
	short action, mode;
	int key;
	unsigned char mod;
} key_action;

static key_action main_keys[] = {
	{ ACT_QUIT,	0,		GDK_q,		MOD_0   },
	{ ACT_ZOOM,	0,		GDK_plus,	MOD_cs  },
	{ ACT_ZOOM,	0,		GDK_KP_Add,	MOD_cs  },
	{ ACT_ZOOM,	-1,		GDK_minus,	MOD_cs  },
	{ ACT_ZOOM,	-1,		GDK_KP_Subtract, MOD_cs },
	{ ACT_ZOOM,	-10,		GDK_KP_1,	MOD_cs  },
	{ ACT_ZOOM,	-10,		GDK_1,		MOD_cs  },
	{ ACT_ZOOM,	-4,		GDK_KP_2,	MOD_cs  },
	{ ACT_ZOOM,	-4,		GDK_2,		MOD_cs  },
	{ ACT_ZOOM,	-2,		GDK_KP_3,	MOD_cs  },
	{ ACT_ZOOM,	-2,		GDK_3,		MOD_cs  },
	{ ACT_ZOOM,	1,		GDK_KP_4,	MOD_cs  },
	{ ACT_ZOOM,	1,		GDK_4,		MOD_cs  },
	{ ACT_ZOOM,	4,		GDK_KP_5,	MOD_cs  },
	{ ACT_ZOOM,	4,		GDK_5,		MOD_cs  },
	{ ACT_ZOOM,	8,		GDK_KP_6,	MOD_cs  },
	{ ACT_ZOOM,	8,		GDK_6,		MOD_cs  },
	{ ACT_ZOOM,	12,		GDK_KP_7,	MOD_cs  },
	{ ACT_ZOOM,	12,		GDK_7,		MOD_cs  },
	{ ACT_ZOOM,	16,		GDK_KP_8,	MOD_cs  },
	{ ACT_ZOOM,	16,		GDK_8,		MOD_cs  },
	{ ACT_ZOOM,	20,		GDK_KP_9,	MOD_cs  },
	{ ACT_ZOOM,	20,		GDK_9,		MOD_cs  },
	{ ACT_VIEW,	0,		GDK_Home,	MOD_0   },
	{ DLG_BRCOSA,	0,		GDK_Insert,	MOD_cs  },
	{ ACT_PAN,	0,		GDK_End,	MOD_cs  },
	{ ACT_CROP,	0,		GDK_Delete,	MOD_cs  },
	{ ACT_SWAP_AB,	0,		GDK_x,		MOD_csa },
	{ DLG_CHOOSER,	CHOOSE_PATTERN,	GDK_F2,		MOD_csa },
	{ DLG_CHOOSER,	CHOOSE_BRUSH,	GDK_F3,		MOD_csa },
	{ DLG_CHOOSER,	CHOOSE_COLOR,	GDK_e,		MOD_csa },
	{ ACT_TOOL,	TTB_PAINT,	GDK_F4,		MOD_csa },
	{ ACT_TOOL,	TTB_SELECT,	GDK_F9,		MOD_csa },
	{ ACT_TOOL,	TTB_FLOOD,	GDK_f,		MOD_csa },
	{ ACT_TOOL,	TTB_LINE,	GDK_d,		MOD_csa },
	{ ACT_DOCK,	0,		GDK_F12,	MOD_csa },
	{ ACT_SEL_MOVE,	5,		GDK_Left,	MOD_cS  },
	{ ACT_SEL_MOVE,	5,		GDK_KP_Left,	MOD_cS  },
	{ ACT_SEL_MOVE,	7,		GDK_Right,	MOD_cS  },
	{ ACT_SEL_MOVE,	7,		GDK_KP_Right,	MOD_cS  },
	{ ACT_SEL_MOVE,	3,		GDK_Down,	MOD_cS  },
	{ ACT_SEL_MOVE,	3,		GDK_KP_Down,	MOD_cS  },
	{ ACT_SEL_MOVE,	9,		GDK_Up,		MOD_cS  },
	{ ACT_SEL_MOVE,	9,		GDK_KP_Up,	MOD_cS  },
	{ ACT_SEL_MOVE,	4,		GDK_Left,	MOD_cs  },
	{ ACT_SEL_MOVE,	4,		GDK_KP_Left,	MOD_cs  },
	{ ACT_SEL_MOVE,	6,		GDK_Right,	MOD_cs  },
	{ ACT_SEL_MOVE,	6,		GDK_KP_Right,	MOD_cs  },
	{ ACT_SEL_MOVE,	2,		GDK_Down,	MOD_cs  },
	{ ACT_SEL_MOVE,	2,		GDK_KP_Down,	MOD_cs  },
	{ ACT_SEL_MOVE,	8,		GDK_Up,		MOD_cs  },
	{ ACT_SEL_MOVE,	8,		GDK_KP_Up,	MOD_cs  },
	{ ACT_OPAC,	1,		GDK_KP_1,	MOD_Cs  },
	{ ACT_OPAC,	1,		GDK_1,		MOD_Cs  },
	{ ACT_OPAC,	2,		GDK_KP_2,	MOD_Cs  },
	{ ACT_OPAC,	2,		GDK_2,		MOD_Cs  },
	{ ACT_OPAC,	3,		GDK_KP_3,	MOD_Cs  },
	{ ACT_OPAC,	3,		GDK_3,		MOD_Cs  },
	{ ACT_OPAC,	4,		GDK_KP_4,	MOD_Cs  },
	{ ACT_OPAC,	4,		GDK_4,		MOD_Cs  },
	{ ACT_OPAC,	5,		GDK_KP_5,	MOD_Cs  },
	{ ACT_OPAC,	5,		GDK_5,		MOD_Cs  },
	{ ACT_OPAC,	6,		GDK_KP_6,	MOD_Cs  },
	{ ACT_OPAC,	6,		GDK_6,		MOD_Cs  },
	{ ACT_OPAC,	7,		GDK_KP_7,	MOD_Cs  },
	{ ACT_OPAC,	7,		GDK_7,		MOD_Cs  },
	{ ACT_OPAC,	8,		GDK_KP_8,	MOD_Cs  },
	{ ACT_OPAC,	8,		GDK_8,		MOD_Cs  },
	{ ACT_OPAC,	9,		GDK_KP_9,	MOD_Cs  },
	{ ACT_OPAC,	9,		GDK_9,		MOD_Cs  },
	{ ACT_OPAC,	10,		GDK_KP_0,	MOD_Cs  },
	{ ACT_OPAC,	10,		GDK_0,		MOD_Cs  },
	{ ACT_OPAC,	0,		GDK_plus,	MOD_Cs  },
	{ ACT_OPAC,	0,		GDK_KP_Add,	MOD_Cs  },
	{ ACT_OPAC,	-1,		GDK_minus,	MOD_Cs  },
	{ ACT_OPAC,	-1,		GDK_KP_Subtract, MOD_Cs },
	{ ACT_LR_MOVE,	5,		GDK_Left,	MOD_CS  },
	{ ACT_LR_MOVE,	5,		GDK_KP_Left,	MOD_CS  },
	{ ACT_LR_MOVE,	7,		GDK_Right,	MOD_CS  },
	{ ACT_LR_MOVE,	7,		GDK_KP_Right,	MOD_CS  },
	{ ACT_LR_MOVE,	3,		GDK_Down,	MOD_CS  },
	{ ACT_LR_MOVE,	3,		GDK_KP_Down,	MOD_CS  },
	{ ACT_LR_MOVE,	9,		GDK_Up,		MOD_CS  },
	{ ACT_LR_MOVE,	9,		GDK_KP_Up,	MOD_CS  },
	{ ACT_LR_MOVE,	4,		GDK_Left,	MOD_Cs  },
	{ ACT_LR_MOVE,	4,		GDK_KP_Left,	MOD_Cs  },
	{ ACT_LR_MOVE,	6,		GDK_Right,	MOD_Cs  },
	{ ACT_LR_MOVE,	6,		GDK_KP_Right,	MOD_Cs  },
	{ ACT_LR_MOVE,	2,		GDK_Down,	MOD_Cs  },
	{ ACT_LR_MOVE,	2,		GDK_KP_Down,	MOD_Cs  },
	{ ACT_LR_MOVE,	8,		GDK_Up,		MOD_Cs  },
	{ ACT_LR_MOVE,	8,		GDK_KP_Up,	MOD_Cs  },
	{ ACT_ESC,	0,		GDK_Escape,	MOD_cs  },
	{ DLG_SCALE,	0,		GDK_Page_Up,	MOD_cs  },
	{ DLG_SIZE,	0,		GDK_Page_Down,	MOD_cs  },
	{ ACT_COMMIT,	0,		GDK_Return,	MOD_s   },
	{ ACT_COMMIT,	1,		GDK_Return,	MOD_S   },
	{ ACT_COMMIT,	0,		GDK_KP_Enter,	MOD_s   },
	{ ACT_COMMIT,	1,		GDK_KP_Enter,	MOD_S   },
	{ ACT_RCLICK,	0,		GDK_BackSpace,	MOD_0   },
	{ ACT_ARROW,	2,		GDK_a,		MOD_csa },
	{ ACT_ARROW,	3,		GDK_s,		MOD_csa },
	{ ACT_A,	-1,		GDK_bracketleft, MOD_cs },
	{ ACT_A,	1,		GDK_bracketright, MOD_cs},
	{ ACT_B,	-1,		GDK_bracketleft, MOD_cS },
	{ ACT_B,	-1,		GDK_braceleft,	MOD_cS  },
	{ ACT_B,	1,		GDK_bracketright, MOD_cS},
	{ ACT_B,	1,		GDK_braceright,	MOD_cS  },
	{ ACT_CHANNEL,	CHN_IMAGE,	GDK_KP_1,	MOD_cS  },
	{ ACT_CHANNEL,	CHN_IMAGE,	GDK_1,		MOD_cS  },
	{ ACT_CHANNEL,	CHN_ALPHA,	GDK_KP_2,	MOD_cS  },
	{ ACT_CHANNEL,	CHN_ALPHA,	GDK_2,		MOD_cS  },
	{ ACT_CHANNEL,	CHN_SEL,	GDK_KP_3,	MOD_cS  },
	{ ACT_CHANNEL,	CHN_SEL,	GDK_3,		MOD_cS  },
	{ ACT_CHANNEL,	CHN_MASK,	GDK_KP_4,	MOD_cS  },
	{ ACT_CHANNEL,	CHN_MASK,	GDK_4,		MOD_cS  },
	{ ACT_VWZOOM,	0,		GDK_plus,	MOD_cS  },
	{ ACT_VWZOOM,	0,		GDK_KP_Add,	MOD_cS  },
	{ ACT_VWZOOM,	-1,		GDK_minus,	MOD_cS  },
	{ ACT_VWZOOM,	-1,		GDK_KP_Subtract, MOD_cS },
	{ 0, 0, 0, 0 }
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

static int wtf_pressed_(key_ext *key)
{
	key_action *ap = main_keys, *cmatch = NULL;
	guint *kcd = main_keycodes;

	for (; ap->action; kcd++ , ap++)
	{
		/* Relevant modifiers should match first */
		if ((key->state & mod_bits[ap->mod & 0xF]) !=
			mod_bits[ap->mod >> 4]) continue;
		/* Let keyval have priority; this is also a workaround for
		 * GTK2 bug #136280 */
		if (key->lowkey == ap->key) break;
		/* Let keycodes match when keyvals don't */
		if (key->realkey == *kcd) cmatch = ap;
	}
/* !!! If the starting layout has the keyval+mods combo mapped to one key, and
 * the current layout to another, both will work till "rebind keys" is done.
 * I like this better than shortcuts moving with every layout switch - WJ */
	/* If we have only a keycode match */
	if (cmatch && !ap->action) ap = cmatch;
	/* Return 0 if no match */
	if (!ap->action) return (0);
	/* Return the matching action */
	return ((ap->action << 16) + (ap->mode + 0x8000));
}

int wtf_pressed(GdkEventKey *event)
{
	key_ext key = {
		event->keyval, low_key(event), real_key(event), event->state };
	return wtf_pressed_(&key);
}

int dock_focused()
{
	GtkWidget *focus = GTK_WINDOW(main_window)->focus_widget;
	GtkWidget *dock = ((main_dd *)GET_DDATA(main_window_))->dockbook[0];
	return (focus && ((focus == dock) || gtk_widget_is_ancestor(focus, dock)));
}

static int check_smart_menu_keys(GdkEventKey *event);

static gboolean handle_keypress(GtkWidget *widget, GdkEventKey *event,
	gpointer user_data)
{
	static GdkEventKey *now_handling;
	int act_m = 0, handled = 0;

	/* Do nothing if called recursively */
	if (event == now_handling) return (FALSE);

	/* Builtin handlers have priority outside of dock */
	if (!dock_focused());
	/* Pressing Escape moves focus out of dock - to nowhere */
	else if (event->keyval == GDK_Escape)
	{
		gtk_window_set_focus(GTK_WINDOW(main_window), NULL);
		act_m = ACTMOD_DUMMY;
	}
#if GTK_MAJOR_VERSION == 2
	/* We let docked widgets process the keys first */
	else if (gtk_window_propagate_key_event(GTK_WINDOW(widget), event))
		return (TRUE);
#endif
	/* Default handlers have priority inside dock */
	else
	{
		// Be ready to handle nested events
		GdkEventKey *was_handling = now_handling;
		gint result = 0;

		now_handling = event;
		gtk_signal_emit_by_name(GTK_OBJECT(widget), "key_press_event",
			event, &result);
		now_handling = was_handling;
		if (result) act_m = ACTMOD_DUMMY;
		handled = ACTMOD_DUMMY;
	}

	if (!act_m) 
	{
		act_m = wtf_pressed(event);
		if (!act_m) act_m = check_smart_menu_keys(event);
		if (!act_m) act_m = handled;
		if (!act_m) return (FALSE);
	}

#if GTK_MAJOR_VERSION == 1
	/* Return value alone doesn't stop GTK1 from running other handlers */
	gtk_signal_emit_stop_by_name(GTK_OBJECT(widget), "key_press_event");
#endif

	if (act_m != ACTMOD_DUMMY)
		action_dispatch(act_m >> 16, (act_m & 0xFFFF) - 0x8000, 0, TRUE);
	return (TRUE);
}

static void draw_arrow(int mode)
{
	int i, xa1, xa2, ya1, ya2, minx, maxx, miny, maxy, w, h;
	double uvx, uvy;	// Line length & unit vector lengths
	int oldmode = mem_undo_opacity;
	grad_info svgrad;


	if (!((tool_type == TOOL_LINE) && (line_status != LINE_NONE) &&
		((line_x1 != line_x2) || (line_y1 != line_y2)))) return;
	svgrad = gradient[mem_channel];
	line_to_gradient();

	// Calculate 2 coords for arrow corners
	uvy = sqrt((line_x1 - line_x2) * (line_x1 - line_x2) +
		(line_y1 - line_y2) * (line_y1 - line_y2));
	uvx = (line_x1 - line_x2) / uvy;
	uvy = (line_y1 - line_y2) / uvy;

	xa1 = rint(line_x2 + tool_flow * (uvx - uvy * 0.5));
	xa2 = rint(line_x2 + tool_flow * (uvx + uvy * 0.5));
	ya1 = rint(line_y2 + tool_flow * (uvy + uvx * 0.5));
	ya2 = rint(line_y2 + tool_flow * (uvy - uvx * 0.5));

// !!! Call this, or let undo engine do it?
//	mem_undo_prepare();
	pen_down = 0;
	tool_action(GDK_NOTHING, line_x2, line_y2, 1, 1.0);
	line_status = LINE_LINE;

	// Draw arrow lines & circles
	mem_undo_opacity = TRUE;
	f_circle(xa1, ya1, tool_size);
	f_circle(xa2, ya2, tool_size);
	tline(xa1, ya1, line_x2, line_y2, tool_size);
	tline(xa2, ya2, line_x2, line_y2, tool_size);

	if (mode == 3)
	{
		// Draw 3rd line and fill arrowhead
		tline(xa1, ya1, xa2, ya2, tool_size );
		poly_points = 0;
		poly_add(line_x2, line_y2);
		poly_add(xa1, ya1);
		poly_add(xa2, ya2);
		poly_paint();
		poly_points = 0;
	}
	mem_undo_opacity = oldmode;
	gradient[mem_channel] = svgrad;
	mem_undo_prepare();
	pen_down = 0;

	// Update screen areas
	minx = xa1 < xa2 ? xa1 : xa2;
	if (minx > line_x2) minx = line_x2;
	maxx = xa1 > xa2 ? xa1 : xa2;
	if (maxx < line_x2) maxx = line_x2;

	miny = ya1 < ya2 ? ya1 : ya2;
	if (miny > line_y2) miny = line_y2;
	maxy = ya1 > ya2 ? ya1 : ya2;
	if (maxy < line_y2) maxy = line_y2;

	i = (tool_size + 1) >> 1;
	minx -= i; miny -= i; maxx += i; maxy += i;

	w = maxx - minx + 1;
	h = maxy - miny + 1;

	update_stuff(UPD_IMGP);
	main_update_area(minx, miny, w, h);
	vw_update_area(minx, miny, w, h);
}

int check_for_changes()			// 1=STOP, 2=IGNORE, -10=NOT CHANGED
{
	if (!mem_changed) return (-10);
	return (alert_box(_("Warning"),
		_("This canvas/palette contains changes that have not been saved.  Do you really want to lose these changes?"),
		_("Cancel Operation"), _("Lose Changes"), NULL));
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
	int i;

	for (i = 0; i < NUM_CHANNELS + 1; i++)
		allchannames[i] = channames[i] = __(channames_[i]);
	channames[CHN_IMAGE] = "";
	for (i = 0; i < NUM_CSPACES; i++)
		cspnames[i] = __(cspnames_[i]);
}

static void toggle_dock(int state);

static void delete_event(main_dd *dt, void **wdata)
{
	inilist *ilp;
	int i;

	i = layers_total ? check_layers_for_changes() : check_for_changes();
	if (i == -10)
	{
		i = 2;
		if (inifile_get_gboolean("exitToggle", FALSE))
			i = alert_box(MT_VERSION, _("Do you really want to quit?"),
				_("NO"), _("YES"), NULL);
	}
	if (i != 2) return; // Cancel quitting

	toggle_dock(FALSE); // To remember dock size

	// Get rid of extra windows + remember positions
	delete_layers_window();

	// Remember the toolbar settings
	toolbar_settings_exit(NULL, NULL);

	/* Store listed settings */
	for (ilp = ini_bool; ilp->name; ilp++)
		inifile_set_gboolean(ilp->name, *(ilp->var));
	for (ilp = ini_int; ilp->name; ilp++)
		inifile_set_gint32(ilp->name, *(ilp->var));

	run_destroy(wdata);
}

#if GTK_MAJOR_VERSION == 2
gint canvas_scroll_gtk2( GtkWidget *widget, GdkEventScroll *event )
{
	if (scroll_zoom)
	{
		if (event->direction == GDK_SCROLL_DOWN) zoom_out();
		else zoom_in();
		return (TRUE);
	}
	if (event->state & _C) /* Convert up-down into left-right */
	{
		if (event->direction == GDK_SCROLL_UP)
			event->direction = GDK_SCROLL_LEFT;
		else if (event->direction == GDK_SCROLL_DOWN)
			event->direction = GDK_SCROLL_RIGHT;
	}
	/* Normal GTK+2 scrollwheel behaviour */
	return (FALSE);
}
#endif


int grad_tool(int event, int x, int y, guint state, guint button)
{
	int i, j, old[4];
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

	copy4(old, grad->xy);
	/* Left click sets points and picks them up again */
	if ((event == GDK_BUTTON_PRESS) && (button == 1))
	{
		/* Start anew */
		if (grad->status == GRAD_NONE)
		{
			grad->xy[0] = grad->xy[2] = x;
			grad->xy[1] = grad->xy[3] = y;
			grad->status = GRAD_END;
			grad_update(grad);
			repaint_grad(NULL);
		}
		/* Place starting point */
		else if (grad->status == GRAD_START)
		{
			grad->xy[0] = x;
			grad->xy[1] = y;
			grad->status = GRAD_DONE;
			grad_update(grad);
			if (grad_opacity) gtk_widget_queue_draw(drawing_canvas);
		}
		/* Place end point */
		else if (grad->status == GRAD_END)
		{
			grad->xy[2] = x;
			grad->xy[3] = y;
			grad->status = GRAD_DONE;
			grad_update(grad);
			if (grad_opacity) gtk_widget_queue_draw(drawing_canvas);
		}
		/* Pick up nearest end */
		else if (grad->status == GRAD_DONE)
		{
			i = (x - grad->xy[0]) * (x - grad->xy[0]) +
				(y - grad->xy[1]) * (y - grad->xy[1]);
			j = (x - grad->xy[2]) * (x - grad->xy[2]) +
				(y - grad->xy[3]) * (y - grad->xy[3]);
			if (i < j)
			{
				grad->xy[0] = x;
				grad->xy[1] = y;
				grad->status = GRAD_START;
			}
			else
			{
				grad->xy[2] = x;
				grad->xy[3] = y;
				grad->status = GRAD_END;
			}
			grad_update(grad);
			if (grad_opacity) gtk_widget_queue_draw(drawing_canvas);
			else repaint_grad(old);
		}
	}

	/* Everything but left click is irrelevant when no gradient */
	else if (grad->status == GRAD_NONE);

	/* Right click deletes the gradient */
	else if (event == GDK_BUTTON_PRESS) /* button != 1 */
	{
		grad->status = GRAD_NONE;
		if (grad_opacity) gtk_widget_queue_draw(drawing_canvas);
		else repaint_grad(NULL);
		grad_update(grad);
	}

	/* Motion is irrelevant with gradient in place */
	else if (grad->status == GRAD_DONE);

	/* Motion drags points around */
	else if (event == GDK_MOTION_NOTIFY)
	{
		int *xy = grad->xy + (grad->status == GRAD_START ? 0 : 2);
		if ((xy[0] != x) || (xy[1] != y))
		{
			xy[0] = x;
			xy[1] = y;
			grad_update(grad);
			repaint_grad(old);
		}
	}

	/* Leave hides the dragged line */
	else if (event == GDK_LEAVE_NOTIFY) repaint_grad(NULL);

	return (TRUE);
}

static int get_bkg(int xc, int yc, int dclick);

static inline int xmmod(int x, int y)
{
	return (x - (x % y));
}

/* Mouse event from button/motion on the canvas */
static void mouse_event(int event, int xc, int yc, guint state, guint button,
	gdouble pressure, int mflag, int dx, int dy)
{
	static int tool_fix, tool_fixv;	// Fixate on axis
	int new_cursor;
	int i, x0, y0, x, y, ox, oy, tox = tool_ox, toy = tool_oy;
	int zoom = 1, scale = 1;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	x = x0 = floor_div((xc - margin_main_x) * zoom, scale) + dx;
	y = y0 = floor_div((yc - margin_main_y) * zoom, scale) + dy;

	ox = x0 < 0 ? 0 : x0 >= mem_width ? mem_width - 1 : x0;
	oy = y0 < 0 ? 0 : y0 >= mem_height ? mem_height - 1 : y0;

	if (!mflag) /* Coordinate fixation */
	{
		if (!(state & _S)) tool_fix = 0;
		else if (!(state & _C)) /* Shift */
		{
			if (tool_fix != 1) tool_fixv = x0;
			tool_fix = 1;
		}
		else /* Ctrl+Shift */
		{
			if (tool_fix != 2) tool_fixv = y0;
			tool_fix = 2;
		}
	}
	/* No use when moving cursor by keyboard */
	else if (event == GDK_MOTION_NOTIFY) tool_fix = 0;

	if (!tool_fix);
	/* For rectangular selection it makes sense to fix its width/height */
	else if ((tool_type == TOOL_SELECT) && (button == 1) &&
		((marq_status == MARQUEE_DONE) || (marq_status == MARQUEE_SELECTING)))
	{
		if (tool_fix == 1) x = x0 = marq_x2;
		else y = y0 = marq_y2;
	}
	else if (tool_fix == 1) x = x0 = tool_fixv;
	else /* if (tool_fix == 2) */ y = y0 = tool_fixv;

	if (tgrid_snap) /* Snap to grid */
	{
		int xy[2] = { x0, y0 };

		/* For everything but rectangular selection, snap with rounding
		 * feels more natural than with flooring - WJ */
		if (tool_type != TOOL_SELECT)
		{
			xy[0] += tgrid_dx >> 1;
			xy[1] += tgrid_dy >> 1;
		}
		snap_xy(xy);
		if ((x = x0 = xy[0]) < 0) x += xmmod(tgrid_dx - x - 1, tgrid_dx);
		if (x >= mem_width) x -= xmmod(tgrid_dx + x - mem_width, tgrid_dx);
		if ((y = y0 = xy[1]) < 0) y += xmmod(tgrid_dy - y - 1, tgrid_dy);
		if (y >= mem_height) y -= xmmod(tgrid_dy + y - mem_height, tgrid_dy);
	}

	x = x < 0 ? 0 : x >= mem_width ? mem_width - 1 : x;
	y = y < 0 ? 0 : y >= mem_height ? mem_height - 1 : y;

	/* ****** Release-event-specific code ****** */

	if (event == GDK_BUTTON_RELEASE)
	{
		tint_mode[2] = 0;
		pen_down = 0;
		if ( col_reverse )
		{
			col_reverse = FALSE;
			mem_swap_cols(FALSE);
		}

		if (grad_tool(event, x0, y0, state, button)) return;

		if ((tool_type == TOOL_LINE) && (button == 1) &&
			(line_status == LINE_START))
		{
			line_status = LINE_LINE;
			repaint_line(NULL);
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

	while ((state & _CS) == _C)	// Set colour A/B
	{
		int ab = button == 3; /* A for left, B for right */
		int pixel, alpha, rgba, upd = 0;

		if (button == 2) /* Auto-dither */
		{
			if ((mem_channel == CHN_IMAGE) && (mem_img_bpp == 3))
				pressed_dither_A();
			break;
		}
		if ((button != 1) && (button != 3)) break;
		/* Default alpha */
		rgba = RGBA_mode && (mem_channel == CHN_IMAGE);
		alpha = channel_col_[ab][CHN_ALPHA];
		/* Pick color from tracing image if possible */
		pixel = get_bkg(xc + dx * scale, yc + dy * scale,
			event == GDK_2BUTTON_PRESS);
		if (pixel >= 0) alpha = 255; // opaque
		/* Otherwise, average brush or selection area on Ctrl+double click */
		while ((pixel < 0) && (event == GDK_2BUTTON_PRESS) && (MEM_BPP == 3))
		{
			int rect[4];

			/* Have brush square */
			if (!NO_PERIM(tool_type))
			{
				int ts2 = tool_size >> 1;
				rect[0] = ox - ts2; rect[1] = oy - ts2;
				rect[2] = rect[3] = tool_size;
			}
			/* Have selection marquee */
			else if ((marq_status > MARQUEE_NONE) &&
				(marq_status < MARQUEE_PASTE)) marquee_at(rect);
			else break;
			/* Clip to image */
			rect[2] += rect[0];
			rect[3] += rect[1];
			if (!clip(rect, 0, 0, mem_width, mem_height, rect)) break;
			/* Average alpha if needed */
			if (rgba && mem_img[CHN_ALPHA]) alpha = average_channel(
				mem_img[CHN_ALPHA], mem_width, rect);
			/* Average image channel as RGBA or RGB */
			pixel = average_pixels(mem_img[CHN_IMAGE],
				rgba && alpha && !channel_dis[CHN_ALPHA] ?
				mem_img[CHN_ALPHA] : NULL, mem_width, rect);
			break;
		}
		/* Failing that, just pick color from image */
		if (pixel < 0)
		{
			pixel = get_pixel(ox, oy);
			if (mem_img[CHN_ALPHA])
				alpha = mem_img[CHN_ALPHA][mem_width * oy + ox];
		}

		if (rgba)
		{
			upd = channel_col_[ab][CHN_ALPHA] ^ alpha;
			channel_col_[ab][CHN_ALPHA] = alpha;
		}

		if (mem_channel != CHN_IMAGE)
		{
			upd |= channel_col_[ab][mem_channel] ^ pixel;
			channel_col_[ab][mem_channel] = pixel;
		}
		else if (mem_img_bpp == 1)
		{
			upd |= mem_col_[ab] ^ pixel;
			mem_col_[ab] = pixel;
			mem_col_24[ab] = mem_pal[pixel];
		}
		else
		{
			png_color *col = mem_col_24 + ab;

			upd |= PNG_2_INT(*col) ^ pixel;
			col->red = INT_2_R(pixel);
			col->green = INT_2_G(pixel);
			col->blue = INT_2_B(pixel);
		}
		if (upd) update_stuff(UPD_CAB);
		break;
	}

	if ((state & _CS) == _C); /* Done above */

	else if ((button == 2) || ((button == 3) && (state & _S)))
		set_zoom_centre(ox, oy);

	else if (grad_tool(event, x0, y0, state, button));

	/* Pure moves are handled elsewhere */
	else if (button) tool_action(event, x, y, button, pressure);

	/* ****** Now to mouse-move-specific part ****** */

	if (event == GDK_MOTION_NOTIFY)
	{
		if (tool_type == TOOL_CLONE)
		{
			tool_ox = x;
			tool_oy = y;
		}

		if ((poly_status == POLY_SELECTING) && (button == 0))
			stretch_poly_line(x, y);

		if ((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON))
		{
			if (marq_status == MARQUEE_DONE)
			{
				i = close_to(x, y);
				if (i != cursor_corner) // Stops excessive CPU/flickering
					set_cursor(corner_cursor[cursor_corner = i]);
			}
			if (marq_status >= MARQUEE_PASTE)
			{
				new_cursor = (x >= marq_x1) && (x <= marq_x2) &&
					(y >= marq_y1) && (y <= marq_y2); // Normal/4way
				if (new_cursor != cursor_corner) // Stops flickering on slow hardware
					set_cursor((cursor_corner = new_cursor) ?
						move_cursor : NULL);
			}
		}
		update_xy_bar(x, y);

		/* TOOL PERIMETER BOX UPDATES */

		if (perim_status > 0) clear_perim(); // Remove old perimeter box

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
			repaint_perim(NULL); // Repaint 4 sides
		}

		/* LINE UPDATES */

		if ((tool_type == TOOL_LINE) && (line_status == LINE_LINE) &&
			((line_x2 != x) || (line_y2 != y)))
		{
			int old[4];

			copy4(old, line_xy);
			line_x2 = x;
			line_y2 = y;
			repaint_line(old);
		}
	}

	update_sel_bar();
}

static gboolean canvas_button(GtkWidget *widget, GdkEventButton *event)
{
	int vport[4], pflag = event->type != GDK_BUTTON_RELEASE;
	gdouble pressure = 1.0;

	mouse_left_canvas = FALSE;
	if (pflag) /* For button press events only */
	{
		/* Steal focus from dock window */
		if (dock_focused())
			gtk_window_set_focus(GTK_WINDOW(main_window), NULL);

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

	wjcanvas_get_vport(widget, vport);
	mouse_event(event->type, event->x + vport[0], event->y + vport[1],
		event->state, event->button, pressure, unreal_move & 1, 0, 0);

	return (pflag);
}

// Mouse enters the canvas
static gint canvas_enter(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
	/* !!! Have to skip grab/ungrab related events if doing something */
//	if (event->mode != GDK_CROSSING_NORMAL) return (TRUE);

	mouse_left_canvas = FALSE;

	return (FALSE);
}

static gint canvas_left(GtkWidget *widget, GdkEventCrossing *event, gpointer user_data)
{
	/* Skip grab/ungrab related events */
	if (event->mode != GDK_CROSSING_NORMAL) return (FALSE);

	/* Only do this if we have an image */
	if (!mem_img[CHN_IMAGE]) return (FALSE);
	mouse_left_canvas = TRUE;
	if (status_on[STATUS_CURSORXY])
		cmd_setv(label_bar[STATUS_CURSORXY], "", LABEL_VALUE);
	if (status_on[STATUS_PIXELRGB])
		cmd_setv(label_bar[STATUS_PIXELRGB], "", LABEL_VALUE);
	if (perim_status > 0) clear_perim();

	if (grad_tool(GDK_LEAVE_NOTIFY, 0, 0, 0, 0)) return (FALSE);

	if (((tool_type == TOOL_POLYGON) && (poly_status == POLY_SELECTING)) ||
		((tool_type == TOOL_LINE) && (line_status == LINE_LINE)))
		repaint_line(NULL);

	return (FALSE);
}

static void render_background(unsigned char *rgb, int x0, int y0, int wid, int hgt, int fwid)
{
	int i, j, k, scale, dx, dy, step, ii, jj, ii0, px, py;
	int xwid = 0, xhgt = 0, wid3 = wid * 3;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (!chequers_optimize) step = 8 , async_bk = TRUE;
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

/// TRACING IMAGE

unsigned char *bkg_rgb;
int bkg_x, bkg_y, bkg_w, bkg_h, bkg_scale, bkg_flag;

int config_bkg(int src)
{
	image_info *img;
	int l;

	if (!src) return (TRUE); // No change

	// Remove old
	free(bkg_rgb);
	bkg_rgb = NULL;
	bkg_w = bkg_h = 0;

	img = src == 2 ? &mem_image : src == 3 ? &mem_clip : NULL;
	if (!img || !img->img[CHN_IMAGE]) return (TRUE); // No image

	l = img->width * img->height;
	bkg_rgb = malloc(l * 3);
	if (!bkg_rgb) return (FALSE);

	if (img->bpp == 1)
	{
		unsigned char *src = img->img[CHN_IMAGE], *dest = bkg_rgb;
		int i, j;

		for (i = 0; i < l; i++ , dest += 3)
		{
			j = *src++;
			dest[0] = mem_pal[j].red;
			dest[1] = mem_pal[j].green;
			dest[2] = mem_pal[j].blue;
		}
	}
	else memcpy(bkg_rgb, img->img[CHN_IMAGE], l * 3);
	bkg_w = img->width;
	bkg_h = img->height;
	return (TRUE);
}

static void render_bkg(rgbcontext *ctx)
{
	unsigned char *src, *dest;
	int i, x0, x, y, ty, w3, l3, d0, dd, adj, bs, rxy[4];
	int zoom = 1, scale = 1;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	bs = bkg_scale * zoom;
	adj = bs - scale > 0 ? bs - scale : 0;
	if (!clip(rxy, floor_div(bkg_x * scale + adj, bs) + margin_main_x,
		floor_div(bkg_y * scale + adj, bs) + margin_main_y,
		floor_div((bkg_x + bkg_w) * scale + adj, bs) + margin_main_x,
		floor_div((bkg_y + bkg_h) * scale + adj, bs) + margin_main_y,
		ctx->xy)) return;
	async_bk |= scale > 1;

	w3 = (ctx->xy[2] - ctx->xy[0]) * 3;
	dest = ctx->rgb + (rxy[1] - ctx->xy[1]) * w3 + (rxy[0] - ctx->xy[0]) * 3;
	l3 = (rxy[2] - rxy[0]) * 3;

	d0 = (rxy[0] - margin_main_x) * bs;
	x0 = floor_div(d0, scale);
	d0 -= x0 * scale;
	x0 -= bkg_x;

	for (ty = -1 , i = rxy[1]; i < rxy[3]; i++)
	{
		y = floor_div((i - margin_main_y) * bs, scale) - bkg_y;
		if (y != ty)
		{
			src = bkg_rgb + (y * bkg_w + x0) * 3;
			for (dd = d0 , x = rxy[0]; x < rxy[2]; x++ , dest += 3)
			{
				dest[0] = src[0];
				dest[1] = src[1];
				dest[2] = src[2];
				for (dd += bs; dd >= scale; dd -= scale)
					src += 3;
			}
			ty = y;
			dest += w3 - l3;
		}
		else
		{
			memcpy(dest, dest - w3, l3);
			dest += w3;
		}
	}
}

static int get_bkg(int xc, int yc, int dclick)
{
	int xb, yb, xi, yi, x, scale;

	/* No background / not RGB / wrong scale */
	if (!bkg_flag || (mem_channel != CHN_IMAGE) || (mem_img_bpp != 3) ||
		(can_zoom < 1.0)) return (-1);
	scale = rint(can_zoom);
	xi = floor_div(xc - margin_main_x, scale);
	yi = floor_div(yc - margin_main_y, scale);
	/* Inside image */
	if ((xi >= 0) && (xi < mem_width) && (yi >= 0) && (yi < mem_height))
	{
		/* Pixel must be transparent */
		x = mem_width * yi + xi;
		if (mem_img[CHN_ALPHA] && !channel_dis[CHN_ALPHA] &&
			!mem_img[CHN_ALPHA][x]); // Alpha transparency
		else if (mem_xpm_trans < 0) return (-1);
		else if (x *= 3 , MEM_2_INT(mem_img[CHN_IMAGE], x) !=
			PNG_2_INT(mem_pal[mem_xpm_trans])) return (-1);

		/* Double click averages background under image pixel */
		if (dclick)
		{
			int vxy[4];

			vxy[2] = (vxy[0] = xi * bkg_scale - bkg_x) + bkg_scale;
			vxy[3] = (vxy[1] = yi * bkg_scale - bkg_y) + bkg_scale;

			return (clip(vxy, 0, 0, bkg_w, bkg_h, vxy) ?
				average_pixels(bkg_rgb, NULL, bkg_w, vxy) : -1);
		}
	}
	xb = floor_div((xc - margin_main_x) * bkg_scale, scale) - bkg_x;
	yb = floor_div((yc - margin_main_y) * bkg_scale, scale) - bkg_y;
	/* Outside of background */
	if ((xb < 0) || (xb >= bkg_w) || (yb < 0) || (yb >= bkg_h)) return (-1);
	x = (bkg_w * yb + xb) * 3;
	return (MEM_2_INT(bkg_rgb, x));
}

/* This is set when background is at different scale than image */
int async_bk;

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
		if (x0 < 0) x0 += rr.scale;
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
	unsigned char *wmask, *gmask, *walpha, *galpha;
	unsigned char *wimg, *gimg, *rgb, *xbuf;
	int opac, len, bpp;
} grad_render_state;

static unsigned char *init_grad_render(grad_render_state *g, int len,
	chanlist tlist)
{
	unsigned char *gstore;
	int coupled_alpha, idx2rgb, opac = 0, bpp = MEM_BPP;


// !!! Only the "slow path" for now
	if (gradient[mem_channel].status != GRAD_DONE) return (NULL);

	if (!IS_INDEXED) opac = grad_opacity;

	memset(g, 0, sizeof(grad_render_state));
	coupled_alpha = (mem_channel == CHN_IMAGE) && RGBA_mode && mem_img[CHN_ALPHA];
	idx2rgb = !opac && (grad_opacity < 255);
	gstore = multialloc(MA_SKIP_ZEROSIZE,
		&g->wmask, len,				/* Mask */
		&g->gmask, len,				/* Gradient opacity */
		&g->gimg, len * bpp,			/* Gradient image */
		&g->wimg, len * bpp,			/* Resulting image */
		&g->galpha, coupled_alpha * len,	/* Gradient alpha */
		&g->walpha, coupled_alpha * len,	/* Resulting alpha */
		&g->rgb, idx2rgb * len * 3,		/* Indexed to RGB */
		&g->xbuf, NEED_XBUF_DRAW * len * bpp,
		NULL);
	if (!gstore) return (NULL);

	tlist[mem_channel] = g->wimg;
	if (g->rgb) tlist[CHN_IMAGE] = g->rgb;
	if (g->walpha) tlist[CHN_ALPHA] = g->walpha;
	g->opac = opac;
	g->len = len;
	g->bpp = bpp;

	return (gstore);
}

static void grad_render(int start, int step, int cnt, int x, int y,
	unsigned char *mask0, grad_render_state *g)
{
	int l = mem_width * y + x, li = l * mem_img_bpp;
	unsigned char *tmp = mem_img[mem_channel] + l * g->bpp;

	prep_mask(start, step, cnt, g->wmask, mask0, mem_img[CHN_IMAGE] + li);
	if (!g->opac) memset(g->gmask, 255, g->len);

	grad_pixels(start, step, cnt, x, y, g->wmask, g->gmask, g->gimg, g->galpha);
	if (g->walpha) memcpy(g->walpha, mem_img[CHN_ALPHA] + l, g->len);

	process_mask(start, step, cnt, g->wmask, g->walpha, mem_img[CHN_ALPHA] + l,
		g->galpha, g->gmask, g->opac, channel_dis[CHN_ALPHA]);

	memcpy(g->wimg, tmp, g->len * g->bpp);
	process_img(start, step, cnt, g->wmask, g->wimg, tmp, g->gimg,
		g->xbuf, g->bpp, g->opac);

	if (g->rgb) blend_indexed(start, step, cnt, g->rgb, mem_img[CHN_IMAGE] + l,
		g->wimg ? g->wimg : mem_img[CHN_IMAGE] + l, mem_img[CHN_ALPHA] + l,
		g->walpha, grad_opacity);
}

typedef struct {
	chanlist tlist;		// Channel overrides
	unsigned char *mask0;	// Active mask channel
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
	unsigned char *buf;	// Allocation pointer, or NULL if none
	chanlist tlist;		// Channel overrides for rendering clipboard
	unsigned char *clip_image;	// Pasted into current channel
	unsigned char *clip_alpha;	// Pasted into alpha channel
	unsigned char *t_alpha;		// Fake pasted alpha
	unsigned char *pix, *alpha;	// Destinations for the above
	unsigned char *mask, *wmask;	// Temp mask: one we use, other we init
	unsigned char *mask0;		// Image mask channel to use
	unsigned char *xform;	// Buffer for color transform preview
	unsigned char *xbuf;	// Extra buffer for process_img()
	int opacity, bpp;	// Just that
	int pixf;		// Flag: need current channel override filled
	int dx;			// Memory-space X offset
	int lx;			// Allocated row length
	int pww;		// Logical row length
} paste_render_state;

/* !!! This function copies existing override set to build its own modified
 * !!! one, so override set must not be changed after calling it */
static int init_paste_render(paste_render_state *p, main_render_state *r,
	unsigned char *xmask)
{
	int x, y, w, h, mx, my, ddx, bpp, scale = r->scale, zoom = r->zoom;
	int temp_image, temp_mask, temp_alpha, fake_alpha, xform_buffer, xbuf;


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
	if ((w <= 0) || (h <= 0)) return (FALSE);

	memset(p, 0, sizeof(paste_render_state));
	memcpy(p->tlist, r->tlist, sizeof(chanlist));

	/* Setup row position and size */
	p->dx = (x * zoom) / scale;
	if (zoom > 1) p->lx = (w - 1) * zoom + 1 , p->pww = w;
	else p->lx = p->pww = (x + w - 1) / scale - p->dx + 1;

	/* Decide what goes where */
	temp_alpha = fake_alpha = 0;
	if ((mem_channel == CHN_IMAGE) && !channel_dis[CHN_ALPHA])
	{
		p->clip_alpha = mem_clip_alpha;
		if (mem_img[CHN_ALPHA])
		{
			fake_alpha = !mem_clip_alpha && RGBA_mode; // Need fake alpha
			temp_alpha = mem_clip_alpha || fake_alpha; // Need temp alpha
		}
	}
	p->clip_image = mem_clipboard;
	ddx = p->dx - r->dx;

	/* Allocate temp area */
	bpp = p->bpp = MEM_BPP;
	temp_mask = !xmask; // Need temp mask if not have one ready
	temp_image = p->clip_image && !p->tlist[mem_channel]; // Same for temp image
	xform_buffer = mem_preview_clip && (bpp == 3) && (mem_clip_bpp == 3);
	xbuf = NEED_XBUF_PASTE;

	if (temp_image | temp_alpha | temp_mask | fake_alpha | xform_buffer | xbuf)
	{
		p->buf = multialloc(MA_SKIP_ZEROSIZE,
			&p->tlist[mem_channel], temp_image * r->lx * bpp,
			&p->tlist[CHN_ALPHA], temp_alpha * r->lx,
			&p->mask, temp_mask * p->lx,
			&p->t_alpha, fake_alpha * p->lx,
			&p->xform, xform_buffer * p->lx * 3,
			&p->xbuf, xbuf * p->lx * bpp,
			NULL);
		if (!p->buf) return (FALSE);
	}

	/* Setup "image" (current) channel override */
	p->pix = p->tlist[mem_channel] + ddx * bpp;
	p->pixf = temp_image; /* Need it prefilled if no override data incoming */

	/* Setup alpha channel override */
	if (temp_alpha) p->alpha = p->tlist[CHN_ALPHA] + ddx;

	/* Setup mask */
	if (mem_channel <= CHN_ALPHA) p->mask0 = r->mask0;
	if (!(p->wmask = p->mask))
	{
		p->mask = xmask + ddx;
		if (r->mask0 != p->mask0)
		/* Mask has wrong data - reuse memory but refill values */
			p->wmask = p->mask;
	}

	/* Setup fake alpha */
	if (fake_alpha) memset(p->t_alpha, channel_col_A[CHN_ALPHA], p->lx);

	/* Setup opacity mode */
	if (!IS_INDEXED) p->opacity = tool_opacity;

	return (TRUE);
}

static void paste_render(int start, int step, int y, paste_render_state *p)
{
	int ld = mem_width * y + p->dx;
	int dc = mem_clip_w * (y - marq_y1) + p->dx - marq_x1;
	int bpp = p->bpp;
	int cnt = p->pww;
	unsigned char *clip_src = mem_clipboard + dc * mem_clip_bpp;

	if (p->wmask) prep_mask(start, step, cnt, p->wmask, p->mask0 ?
		p->mask0 + ld : NULL, mem_img[CHN_IMAGE] + ld * mem_img_bpp);
	process_mask(start, step, cnt, p->mask, p->alpha, mem_img[CHN_ALPHA] + ld,
		p->clip_alpha ? p->clip_alpha + dc : p->t_alpha,
		mem_clip_mask ? mem_clip_mask + dc : NULL, p->opacity, 0);
	if (!p->pixf) /* Fill just the underlying part */
		memcpy(p->pix, mem_img[mem_channel] + ld * bpp, p->lx * bpp);
	if (p->xform) /* Apply color transform if preview requested */
	{
		do_transform(start, step, cnt, NULL, p->xform, clip_src);
		clip_src = p->xform;
	}
	if (mem_clip_bpp < bpp)
	{
		/* Convert paletted clipboard to RGB */
		do_convert_rgb(start, step, cnt, p->xbuf, clip_src,
			mem_clip_paletted ? mem_clip_pal : mem_pal);
		clip_src = p->xbuf;
	}
	process_img(start, step, cnt, p->mask, p->pix, mem_img[mem_channel] + ld * bpp,
		clip_src, p->xbuf, bpp, p->opacity);
}

static int main_render_rgb(unsigned char *rgb, int x, int y, int w, int h, int pw)
{
	main_render_state r;
	unsigned char **tlist = r.tlist;
	int j, jj, j0, l, pw23;
	unsigned char *xtemp = NULL;
	xform_render_state uninit_(xrstate);
	unsigned char *cstemp = NULL;
	unsigned char *gtemp = NULL;
	grad_render_state grstate;
	int pflag = FALSE;
	paste_render_state prstate;


	memset(&r, 0, sizeof(r));

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	r.zoom = r.scale = 1;
	if (can_zoom < 1.0) r.zoom = rint(1.0 / can_zoom);
	else r.scale = rint(can_zoom);

	r.px2 = x; r.py2 = y;
	r.pw2 = w; r.ph2 = h;

	if (!channel_dis[CHN_MASK]) r.mask0 = mem_img[CHN_MASK];

	r.xpm = mem_xpm_trans;
	r.lop = 255;
	if (show_layers_main && layers_total && layer_selected)
		r.lop = (layer_table[layer_selected].opacity * 255 + 50) / 100;

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
		if (mem_channel > CHN_ALPHA) r.mask0 = NULL;
		gtemp = init_grad_render(&grstate, r.lx, r.tlist);
	}

	/* Paste preview - can only coexist with transform */
	if (show_paste && (marq_status >= MARQUEE_PASTE) && !cstemp && !gtemp)
		pflag = init_paste_render(&prstate, &r, xtemp ? xrstate.pvm : NULL);

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
			if (pflag && (j >= marq_y1) && (j <= marq_y2))
			{
				tlist = prstate.tlist; /* Paste-area override */
				if (prstate.alpha) memcpy(tlist[CHN_ALPHA],
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
	if (pflag) free(prstate.buf);

	return (pflag); /* "There was paste" */
}

/// GRID

int grid_rgb[GRID_MAX];	// Grid colors to use
int mem_show_grid;	// Boolean show toggle
int mem_grid_min;	// Minimum zoom to show it at
int color_grid;		// If to use grid coloring
int show_tile_grid;	// Tile grid toggle
int tgrid_x0, tgrid_y0;	// Tile grid origin
int tgrid_dx, tgrid_dy;	// Tile grid spacing
int tgrid_snap;		// Coordinates snap toggle

/* Snap coordinate pair to tile grid (floored) */
void snap_xy(int *xy)
{
	int dx, dy;

	dx = (xy[0] - tgrid_x0) % tgrid_dx;
	xy[0] -= dx + (dx < 0 ? tgrid_dx : 0);
	dy = (xy[1] - tgrid_y0) % tgrid_dy;
	xy[1] -= dy + (dy < 0 ? tgrid_dy : 0);
}

/* Buffer stores interleaved transparency bits for two pixel rows; current
 * row in bits 0, 2, etc., previous one in bits 1, 3, etc. */
static void scan_trans(unsigned char *dest, int delta, int y, int x, int w)
{
	static unsigned char beta = 255;
	unsigned char *src, *srca = &beta;
	int i, ofs, bit, buf, xpm, da = 0, bpp = mem_img_bpp;


	delta += delta; dest += delta >> 3; delta &= 7;
	if (y >= mem_height) // Clear
	{
		for (i = 0; i < w; i++ , dest++) *dest = (*dest & 0x55) << 1;
		return;
	}

	xpm = mem_xpm_trans < 0 ? -1 : bpp == 1 ? mem_xpm_trans :
		PNG_2_INT(mem_pal[mem_xpm_trans]);
	ofs = y * mem_width + x;
	src = mem_img[CHN_IMAGE] + ofs * bpp;
	if (mem_img[CHN_ALPHA]) srca = mem_img[CHN_ALPHA] + ofs , da = 1;
	bit = 1 << delta;
	buf = (*dest & 0x55) << 1;
	for (i = 0; i < w; i++ , src += bpp , srca += da)
	{
		if (*srca && ((bpp == 1 ? *src : MEM_2_INT(src, 0)) != xpm))
			buf |= bit;
		if ((bit <<= 2) < 0x100) continue;
		*dest++ = buf; buf = (*dest & 0x55) << 1; bit = 1;
	}
	*dest = buf;
}

/* Draw grid on rgb memory */
static void draw_grid(unsigned char *rgb, int x, int y, int w, int h, int l)
{
/* !!! This limit IN THEORY can be violated by a sufficiently huge screen, if
 * clipping to image isn't enforced in some fashion; this code can be made to
 * detect the violation and do the clipping (on X axis) for itself - really
 * colored is only the image proper + 1 pixel to bottom & right of it */
	unsigned char lbuf[(MAX_WIDTH * 2 + 2 + 7) / 8 + 2];
	int dx, dy, step = can_zoom;
	int i, j, k, yy, wx, ww, x0, xw;


	l *= 3;
	dx = (x - margin_main_x) % step;
	if (dx <= 0) dx += step;
	dy = (y - margin_main_y) % step;
	if (dy <= 0) dy += step;

	/* Use dumb code for uncolored grid */
	if (!color_grid || (x + w <= margin_main_x) || (y + h <= margin_main_y) ||
		(x > margin_main_x + mem_width * step) ||
		(y > margin_main_y + mem_height * step))
	{
		unsigned char *tmp;
		int i, j, k, step3, tc;

		tc = grid_rgb[color_grid ? GRID_TRANS : GRID_NORMAL];
		dx = (step - dx) * 3;
		w *= 3;

		for (k = dy , i = 0; i < h; i++ , k++)
		{
			tmp = rgb + i * l;
			if (k == step) /* Filled line */
			{
				j = k = 0; step3 = 3;
			}
			else /* Spaced dots */
			{
				j = dx; step3 = step * 3;
			}
			for (tmp += j; j < w; j += step3 , tmp += step3)
			{
				tmp[0] = INT_2_R(tc);
				tmp[1] = INT_2_G(tc);
				tmp[2] = INT_2_B(tc);
			}
		}
		return;
	}

	wx = floor_div(x - margin_main_x - 1, step);
	ww = (w + dx + step - 1) / step + 1;
	memset(lbuf, 0, (ww + ww + 7 + 16) >> 3); // Init to transparent

	x0 = wx < 0 ? 0 : wx;
	xw = (wx + ww < mem_width ? wx + ww : mem_width) - x0;
	wx = x0 - wx;
	yy = floor_div(y - margin_main_y, step);

	/* Initial row fill */
	if (dy == step) yy--;
	if ((yy >= 0) && (yy < mem_height)) // Else it stays cleared
		scan_trans(lbuf, wx, yy, x0, xw);

	for (k = dy , i = 0; i < h; i++ , k++)
	{
		unsigned char *tmp = rgb + i * l, *buf = lbuf + 2;

		// Horizontal span
		if (k == step)
		{
			int nv, tc, kk;

			yy++; k = 0;
			// Fill one more row
			if ((yy >= 0) && (yy <= mem_height + 1))
				scan_trans(lbuf, wx, yy, x0, xw);
			nv = (lbuf[0] + (lbuf[1] << 8)) ^ 0x2FFFF; // Invert
			tc = grid_rgb[((nv & 3) + 1) >> 1];
			for (kk = dx , j = 0; j < w; j++ , kk++ , tmp += 3)
			{
				if (kk != step) // Span
				{
					tmp[0] = INT_2_R(tc);
					tmp[1] = INT_2_G(tc);
					tmp[2] = INT_2_B(tc);
					continue;
				}
				// Intersection
				/* 0->0, 15->2, in-between remains between */
				tc = grid_rgb[((nv & 0xF) * 9 + 0x79) >> 7];
				tmp[0] = INT_2_R(tc);
				tmp[1] = INT_2_G(tc);
				tmp[2] = INT_2_B(tc);
				nv >>= 2;
				if (nv < 0x400)
					nv ^= (*buf++ << 8) ^ 0x2FD00; // Invert
				tc = grid_rgb[((nv & 3) + 1) >> 1];
				kk = 0;
			}
		}
		// Vertical spans
		else
		{
			int nv = (lbuf[0] + (lbuf[1] << 8)) ^ 0x2FFFF; // Invert
			j = step - dx; tmp += j * 3;
			for (; j < w; j += step , tmp += step * 3)
			{
				/* 0->0, 5->2, in-between remains between */
				int tc = grid_rgb[((nv & 5) + 3) >> 2];
				tmp[0] = INT_2_R(tc);
				tmp[1] = INT_2_G(tc);
				tmp[2] = INT_2_B(tc);
				nv >>= 2;
				if (nv < 0x400)
					nv ^= (*buf++ << 8) ^ 0x2FD00; // Invert
			}
		}
	}
}

/* Draw tile grid on rgb memory */
static void draw_tgrid(unsigned char *rgb, int x, int y, int w, int h, int l)
{
	unsigned char *tmp, *tm2;
	int i, j, k, dx, dy, nx, ny, xx, yy, tc, zoom = 1, scale = 1;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);
	if ((tgrid_dx < zoom * 2) || (tgrid_dy < zoom * 2)) return; // Too dense

	dx = tgrid_x0 ? tgrid_dx - tgrid_x0 : 0;
	nx = (x * zoom - dx) / (tgrid_dx * scale);
	if (nx < 0) nx = 0; nx++; dx++;
	dy = tgrid_y0 ? tgrid_dy - tgrid_y0 : 0;
	ny = (y * zoom - dy) / (tgrid_dy * scale);
	if (ny < 0) ny = 0; ny++; dy++;
	xx = ((nx * tgrid_dx - dx) * scale) / zoom + scale - 1;
	yy = ((ny * tgrid_dy - dy) * scale) / zoom + scale - 1;
	if ((xx >= x + w) && (yy >= y + h)) return; // Entirely inside grid cell

	l *= 3; tc = grid_rgb[GRID_TILE];
	for (i = 0; i < h; i++)
	{
		tmp = rgb + l * i;
		if (y + i == yy) /* Filled line */
		{
			for (j = 0; j < w; j++ , tmp += 3)
			{
				tmp[0] = INT_2_R(tc);
				tmp[1] = INT_2_G(tc);
				tmp[2] = INT_2_B(tc);
			}
			yy = ((++ny * tgrid_dy - dy) * scale) / zoom + scale - 1;
			continue;
		}
		/* Spaced dots */
		for (k = xx , j = nx + 1; k < x + w; j++)
		{
			tm2 = tmp + (k - x) * 3;
			tm2[0] = INT_2_R(tc);
			tm2[1] = INT_2_G(tc);
			tm2[2] = INT_2_B(tc);
			k = ((j * tgrid_dx - dx) * scale) / zoom + scale - 1;
		}
	}
}

/* Draw segmentation contours on rgb memory */
static void draw_segments(unsigned char *rgb, int x, int y, int w, int h, int l)
{
	unsigned char lbuf[(MAX_WIDTH * 2 + 7) / 8];
	int i, j, k, j0, kk, dx, dy, wx, ww, yy, vf, tc, zoom = 1, scale = 1;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	l *= 3;
	dx = x % scale;
	wx = x / scale;
	ww = (w + dx + scale - 1) / scale;

	dy = y % scale;
	yy = y / scale;

	/* Initial row fill */
	mem_seg_scan(lbuf, yy, wx, ww, zoom, seg_preview);

	tc = grid_rgb[GRID_SEGMENT];
	j0 = !!dx;
	vf = (scale == 1) | 2; // Draw both edges in one pixel if no zoom
	for (k = dy , i = 0; i < h; i++ , k++)
	{
		unsigned char *tmp, *buf;
		int nv;

		if (k == scale)
		{
			mem_seg_scan(lbuf, ++yy, wx, ww, zoom, seg_preview);
			k = 0;
		}

		/* Horizontal lines */
		if (!k && (scale > 1))
		{
			tmp = rgb + i * l;
			buf = lbuf;
			nv = *buf++ + 0x100;
			for (kk = dx, j = 0; j < w; j++ , tmp += 3)
			{
				if (nv & 1) // Draw grid line
				{
					tmp[0] = INT_2_R(tc);
					tmp[1] = INT_2_G(tc);
					tmp[2] = INT_2_B(tc);
				}
				if (++kk == scale)
				{
					if ((nv >>= 2) == 1) nv = *buf++ + 0x100;
					kk = 0;
				}
			}
		}

		/* Vertical/mixed lines */
		tmp = rgb + i * l + (j0 * scale - dx) * 3;
		buf = lbuf;
		nv = (*buf++ + 0x100) >> (j0 * 2);
		for (j = j0; j < ww; j++ , tmp += scale * 3)
		{
			if (nv & vf)
			{
				tmp[0] = INT_2_R(tc);
				tmp[1] = INT_2_G(tc);
				tmp[2] = INT_2_B(tc);
			}
			if ((nv >>= 2) == 1) nv = *buf++ + 0x100;
		}
	}
}

/* Redirectable RGB blitting */
void draw_rgb(int x, int y, int w, int h, unsigned char *rgb, int step, rgbcontext *ctx)
{
	unsigned char *dest;
	int l, rxy[4], vxy[4];

	if (!ctx)
	{
		wjcanvas_get_vport(drawing_canvas, vxy);
		if (!clip(rxy, x, y, x + w, y + h, vxy)) return;
		gdk_draw_rgb_image(drawing_canvas->window, drawing_canvas->style->black_gc,
			rxy[0] - vxy[0], rxy[1] - vxy[1], rxy[2] - rxy[0], rxy[3] - rxy[1],
			GDK_RGB_DITHER_NONE, rgb, step);
	}
	else
	{
		if (!clip(rxy, x, y, x + w, y + h, ctx->xy)) return;
		rgb += (rxy[1] - y) * step + (rxy[0] - x) * 3;
		l = (ctx->xy[2] - ctx->xy[0]) * 3;
		dest = ctx->rgb + (rxy[1] - ctx->xy[1]) * l + (rxy[0] - ctx->xy[0]) * 3;
		w = (rxy[2] - rxy[0]) * 3;
		for (h = rxy[3] - rxy[1]; h; h--)
		{
			memcpy(dest, rgb, w);
			dest += l; rgb += step;
		}
	}
}

/* Redirectable polygon drawing */
void draw_poly(int *xy, int cnt, int shift, int x00, int y00, rgbcontext *ctx)
{
#define PT_BATCH 100
	GdkPoint white[PT_BATCH], black[PT_BATCH], *p;
	linedata line;
	unsigned char *rgb;
	int w = 0, nw = 0, nb = 0;
	int i, x0, y0, x1, y1, dx, dy, a0, a, vxy[4];

	if (ctx)
	{
		copy4(vxy, ctx->xy);
		w = vxy[2] - vxy[0];
	}
	else wjcanvas_get_vport(drawing_canvas, vxy);
	--vxy[2]; --vxy[3];

	x1 = x00 + *xy++; y1 = y00 + *xy++;
	a = x1 < vxy[0] ? 1 : x1 > vxy[2] ? 2:
		y1 < vxy[1] ? 3 : y1 > vxy[3] ? 4 : 5;
	for (i = 1; i < cnt; i++)
	{
		x0 = x1; y0 = y1; a0 = a;
		x1 = x00 + *xy++; y1 = y00 + *xy++;
		dx = abs(x1 - x0); dy = abs(y1 - y0);
		if (dx < dy) dx = dy; shift += dx;
		switch (a0) // Basic clipping
		{
		// Already visible - skip if same point
		case 0: if (!dx) continue; break;
		// Left of window - skip if still there
		case 1: if (x1 < vxy[0]) continue; break;
		// Right of window
		case 2: if (x1 > vxy[2]) continue; break;
		// Top of window
		case 3: if (y1 < vxy[1]) continue; break;
		// Bottom of window
		case 4: if (y1 > vxy[3]) continue; break;
		// First point - never skip
		case 5: a0 = 0; break;
		}
		// May be visible - find where the other end goes
		a = x1 < vxy[0] ? 1 : x1 > vxy[2] ? 2 :
			y1 < vxy[1] ? 3 : y1 > vxy[3] ? 4 : 0;
		line_init(line, x0, y0, x1, y1);
		if (a0 + a) // If both ends inside area, no clipping needed
		{
			if (line_clip(line, vxy, &a0) < 0) continue;
			dx -= a0;
		}
		for (dx = shift - dx; line[2] >= 0; line_step(line) , dx++)
		{
			if (ctx) // Draw to RGB
			{
				rgb = ctx->rgb + ((line[1] - ctx->xy[1]) * w +
					(line[0] - ctx->xy[0])) * 3;
				rgb[0] = rgb[1] = rgb[2] = ((~dx >> 2) & 1) * 255;
				continue;
			}
			if (dx & 4) // Draw to canvas in black
			{
				p = black + nb++;
				p->x = line[0] - vxy[0];
				p->y = line[1] - vxy[1];
				if (nb < PT_BATCH) continue;
			}
			else // Draw to canvas in white
			{
				p = white + nw++;
				p->x = line[0] - vxy[0];
				p->y = line[1] - vxy[1];
				if (nw < PT_BATCH) continue;
			}
			// Batch drawing to canvas
			if (nb) gdk_draw_points(drawing_canvas->window,
				drawing_canvas->style->black_gc, black, nb);
			if (nw)	gdk_draw_points(drawing_canvas->window,
				drawing_canvas->style->white_gc, white, nw);
			nb = nw = 0;
		}
	}
	// Finish drawing
	if (nb) gdk_draw_points(drawing_canvas->window,
		drawing_canvas->style->black_gc, black, nb);
	if (nw) gdk_draw_points(drawing_canvas->window,
		drawing_canvas->style->white_gc, white, nw);
#undef PT_BATCH
}

/* Clip area to image & align rgb pointer with it */
static unsigned char *clip_to_image(int *rect, unsigned char *rgb, int *vxy)
{
	int rxy[4], mw, mh;

	/* Clip update area to image bounds */
	canvas_size(&mw, &mh);
	if (!clip(rxy, margin_main_x, margin_main_y,
		margin_main_x + mw, margin_main_y + mh, vxy)) return (NULL);

	rect[0] = rxy[0] - margin_main_x;
	rect[1] = rxy[1] - margin_main_y;
	rect[2] = rxy[2] - rxy[0];
	rect[3] = rxy[3] - rxy[1];

	/* Align buffer with image */
	rgb += ((vxy[2] - vxy[0]) * (rxy[1] - vxy[1]) + (rxy[0] - vxy[0])) * 3;
	return (rgb);
}

/* Map clipping rectangle to line-space, for use with line_clip() */
void prepare_line_clip(int *lxy, int *vxy, int scale)
{
	int i;

	for (i = 0; i < 4; i++)
		lxy[i] = floor_div(vxy[i] - margin_main_xy[i & 1] - (i >> 1), scale);
}

void repaint_canvas(int px, int py, int pw, int ph)
{
	rgbcontext ctx;
	unsigned char *rgb, *irgb;
	int rect[4], vxy[4], vport[4], rpx, rpy;
	int i, lr, zoom = 1, scale = 1, paste_f = FALSE;


	/* Clip area & init context */
	wjcanvas_get_vport(drawing_canvas, vport);
	if (!clip(ctx.xy, px, py, px + pw, py + ph, vport)) return;
	pw = ctx.xy[2] - (px = ctx.xy[0]);
	ph = ctx.xy[3] - (py = ctx.xy[1]);

	rgb = malloc(i = pw * ph * 3);
	if (!rgb) return;
	memset(rgb, mem_background, i);
	ctx.rgb = rgb;

	/* Find out which part is image */
	irgb = clip_to_image(rect, rgb, ctx.xy);

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	rpx = px - margin_main_x;
	rpy = py - margin_main_y;

	lr = layers_total && show_layers_main;
	if (bkg_flag && bkg_rgb) render_bkg(&ctx); /* Tracing image */
	else if (!lr) /* Render default background if no layers shown */
	{
		if (irgb && ((mem_xpm_trans >= 0) ||
			(!overlay_alpha && mem_img[CHN_ALPHA] && !channel_dis[CHN_ALPHA])))
			render_background(irgb, rect[0], rect[1], rect[2], rect[3], pw * 3);
	}
	/* Render underlying layers */
	if (lr) render_layers(rgb, rpx, rpy, pw, ph,
		can_zoom, 0, layer_selected - 1, layer_selected);

	if (irgb) paste_f = main_render_rgb(irgb, rect[0], rect[1], rect[2], rect[3], pw);

	if (lr) render_layers(rgb, rpx, rpy, pw, ph,
		can_zoom, layer_selected + 1, layers_total, layer_selected);

	/* No grid at all */
	if (!mem_show_grid || (scale < mem_grid_min));
	/* No paste - single area */
	else if (!paste_f) draw_grid(rgb, px, py, pw, ph, pw);
	/* With paste - zero to four areas */
	else
	{
		int n, x0, y0, w0, h0, rect04[5 * 4], *p = rect04;
		unsigned char *r;

		w0 = (marq_x2 < mem_width ? marq_x2 + 1 : mem_width) * scale;
		x0 = marq_x1 > 0 ? marq_x1 * scale : 0;
		w0 -= x0; x0 += margin_main_x;
		h0 = (marq_y2 < mem_height ? marq_y2 + 1 : mem_height) * scale;
		y0 = marq_y1 > 0 ? marq_y1 * scale : 0;
		h0 -= y0; y0 += margin_main_y;

		n = clip4(rect04, px, py, pw, ph, x0, y0, w0, h0);
		while (n--)
		{
			p += 4;
			r = rgb + ((p[1] - py) * pw + (p[0] - px)) * 3;
			draw_grid(r, p[0], p[1], p[2], p[3], pw);
		}
	}

	/* Tile grid */
	if (show_tile_grid && irgb)
		draw_tgrid(irgb, rect[0], rect[1], rect[2], rect[3], pw);

	/* Segmentation preview */
	if (seg_preview && irgb)
		draw_segments(irgb, rect[0], rect[1], rect[2], rect[3], pw);

	async_bk = FALSE;

/* !!! All other over-the-image things have to be redrawn here as well !!! */
	prepare_line_clip(vxy, ctx.xy, scale);
	/* Redraw gradient line if needed */
	i = gradient[mem_channel].status;
	if ((mem_gradient || (tool_type == TOOL_GRADIENT)) &&
		(mouse_left_canvas ? (i == GRAD_DONE) : (i != GRAD_NONE)))
		refresh_line(3, vxy, &ctx);

	/* Draw marquee as we may have drawn over it */
	if (marq_status != MARQUEE_NONE)
		paint_marquee(MARQ_SHOW, 0, 0, &ctx);
	if ((tool_type == TOOL_POLYGON) && poly_points)
		paint_poly_marquee(&ctx, TRUE);

	/* Redraw line if needed */
	if ((((tool_type == TOOL_POLYGON) && (poly_status == POLY_SELECTING)) ||
		((tool_type == TOOL_LINE) && (line_status == LINE_LINE))) &&
		!mouse_left_canvas)
		refresh_line(tool_type == TOOL_LINE ? 1 : 2, vxy, &ctx);

	/* Redraw perimeter if needed */
	if (perim_status && !mouse_left_canvas) repaint_perim(&ctx);

	gdk_draw_rgb_image(drawing_canvas->window, drawing_canvas->style->black_gc,
		px - vport[0], py - vport[1], pw, ph,
		GDK_RGB_DITHER_NONE, rgb, pw * 3);
	free(rgb);
}

/* Update x,y,w,h area of current image */
void main_update_area(int x, int y, int w, int h)
{
	int zoom, scale, vport[4], rxy[4];

	if (can_zoom < 1.0)
	{
		zoom = rint(1.0 / can_zoom);
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
		scale = rint(can_zoom);
		x *= scale;
		y *= scale;
		w *= scale;
		h *= scale;
		if (color_grid && mem_show_grid && (scale >= mem_grid_min))
			w++ , h++; // Redraw grid lines bordering the area
	}

	x += margin_main_x; y += margin_main_y;
	wjcanvas_get_vport(drawing_canvas, vport);
	if (clip(rxy, x, y, x + w, y + h, vport))
		gtk_widget_queue_draw_area(drawing_canvas,
			rxy[0] - vport[0], rxy[1] - vport[1],
			rxy[2] - rxy[0], rxy[3] - rxy[1]);
}

/* Get zoomed canvas size */
void canvas_size(int *w, int *h)
{
	int zoom = 1, scale = 1;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	*w = (mem_width * scale + zoom - 1) / zoom;
	*h = (mem_height * scale + zoom - 1) / zoom;
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
	int x, y, rm, vport[4], button = 0;
	GdkModifierType state;
	gdouble pressure = 1.0;


	mouse_left_canvas = FALSE;

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

	/* If cursor got warped, will have another movement event to handle */
	if (button && (tool_type == TOOL_SELECT) &&
		wjcanvas_bind_mouse(widget, event, x, y)) return (TRUE);

	wjcanvas_get_vport(widget, vport);
	mouse_event(event->type, x + vport[0], y + vport[1],
		state, button, pressure, rm & 1, 0, 0);

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
	vw_realign();	// Update the view window as needed

	return (TRUE);
}

void force_main_configure()
{
	if (drawing_canvas) configure_canvas(drawing_canvas, NULL);
	if (view_showing && vw_drawing) vw_configure(vw_drawing, NULL);
}

#define REPAINT_CANVAS_COST 512

static gboolean expose_canvas(GtkWidget *widget, GdkEventExpose *event,
	gpointer user_data)
{
	int vport[4];

	/* Stops excess jerking in GTK+1 when zooming */
	if (zoom_flag) return (TRUE);

	wjcanvas_get_vport(widget, vport);
	repaint_expose(event, vport, repaint_canvas, REPAINT_CANVAS_COST);

	return (TRUE);
}

void set_cursor(void **what)	// Set mouse cursor
{
	if (!drawing_canvas->window) return; /* Do nothing if canvas hidden */
	what = !cursor_tool ? NULL : what ? what : m_cursor[tool_type];
	gdk_window_set_cursor(drawing_canvas->window, what ? what[0] : NULL);
}

void change_to_tool(int icon)
{
	grad_info *grad;
	void *var, **slot = icon_buttons[icon];
	int i, t, update = UPD_SEL;

// !!! Use V-code API for this later
	if (!GTK_WIDGET_SENSITIVE(slot[0])) return; // Blocked
// !!! Unnecessarily complicated approach - better add a peek operation
	var = cmd_read(slot, NULL);
	if ((int)var != TOOL_ID(slot)) cmd_set(slot, TRUE);

	switch (icon)
	{
	case TTB_PAINT:
		t = brush_type; break;
	case TTB_SHUFFLE:
		t = TOOL_SHUFFLE; break;
	case TTB_FLOOD:
		t = TOOL_FLOOD; break;
	case TTB_LINE:
		t = TOOL_LINE; break;
	case TTB_SMUDGE:
		t = TOOL_SMUDGE; break;
	case TTB_CLONE:
		t = TOOL_CLONE; break;
	case TTB_SELECT:
		t = TOOL_SELECT; break;
	case TTB_POLY:
		t = TOOL_POLYGON; break;
	case TTB_GRAD:
		t = TOOL_GRADIENT; break;
	default: return;
	}

	/* Tool hasn't changed (likely, recursion changed it from under us) */
	if (t == tool_type) return;

	if (perim_status) clear_perim();
	i = tool_type;
	tool_type = t;

	grad = gradient + mem_channel;
	if (i == TOOL_LINE) stop_line();
	if ((i == TOOL_GRADIENT) && (grad->status != GRAD_NONE))
	{
		if (grad->status != GRAD_DONE) grad->status = GRAD_NONE;
		else if (grad_opacity) update |= CF_DRAW;
		else if (!mem_gradient) repaint_grad(NULL);
	}
	if ( marq_status != MARQUEE_NONE)
	{
		if (paste_commit && (marq_status >= MARQUEE_PASTE))
		{
			commit_paste(FALSE, NULL);
			pen_down = 0;
			mem_undo_prepare();
		}

		marq_status = MARQUEE_NONE;	// Marquee is on so lose it!
		update |= CF_DRAW;			// Needed to clear selection
	}
	if ( poly_status != POLY_NONE)
	{
		poly_status = POLY_NONE;	// Marquee is on so lose it!
		poly_points = 0;
		update |= CF_DRAW;			// Needed to clear selection
	}
	if ( tool_type == TOOL_CLONE )
	{
		clone_x = -tool_size;
		clone_y = tool_size;
	}
	/* Persistent selection frame */
// !!! To NOT show selection frame while placing gradient
//	if ((tool_type == TOOL_SELECT)
	if (((tool_type == TOOL_SELECT) || (tool_type == TOOL_GRADIENT))
		&& (marq_x1 >= 0) && (marq_y1 >= 0)
		&& (marq_x2 >= 0) && (marq_y2 >= 0))
	{
		marq_status = MARQUEE_DONE;
		paint_marquee(MARQ_SHOW, 0, 0, NULL);
	}
	if ((tool_type == TOOL_GRADIENT) && (grad->status != GRAD_NONE))
	{
		if (grad_opacity) update |= CF_DRAW;
		else repaint_grad(NULL);
	}
	update_stuff(update);
	if (!(update & CF_DRAW)) repaint_perim(NULL);
}

static void pressed_view_hori(int state)
{
	if (!state != view_vsplit) return; // unchanged
	view_vsplit = !view_vsplit;
	if (view_showing)
	{
		view_showing = FALSE;
		view_show(); // rearrange
	}
}

void set_image(gboolean state)
{
	static int depth = 0;

	if (state ? --depth : depth++) return;

	cmd_showhide(main_split, state);
	if (state) set_cursor(NULL); /* Canvas window now maybe a new one */
}

static char read_hex_dub(char *in)	// Read hex double
{
	static const char chars[] = "0123456789abcdef0123456789ABCDEF";
	const char *p1, *p2;

	p1 = strchr(chars, in[0]);
	p2 = strchr(chars, in[1]);
	return (p1 && p2 ? (((p1 - chars) & 15) << 4) + ((p2 - chars) & 15) : '?');
}

static void parse_drag( char *txt )
{
#ifdef WIN32
	char fname[PATHTXT];
#else
	char fname[PATHBUF];
#endif
	char ch, *tp, *tp2;
	int i, j, nlayer = TRUE;


	set_image(FALSE);

	tp = txt;
	while ((layers_total < MAX_LAYERS) && (tp2 = strstr(tp, "file:")))
	{
		tp = tp2 + 5;
		while (*tp == '/') tp++;
#ifndef WIN32
		// If not windows keep a leading slash
		tp--;
		// If windows strip away all leading slashes
#endif
		i = 0;
		while ((ch = *tp++) > 31) // Copy filename
		{
			if (ch == '%')	// Weed out those ghastly % substitutions
			{
				ch = read_hex_dub(tp);
				tp += 2;
			}
			fname[i++] = ch;
			if (i >= sizeof(fname) - 1) break;
		}
		fname[i] = 0;
		tp--;

#ifdef WIN32
		/* !!! GTK+ uses UTF-8 encoding for URIs on Windows */
		gtkncpy(fname, fname, PATHBUF);
		reseparate(fname);
#endif
		j = detect_image_format(fname);
		if ((j > 0) && (j != FT_NONE) && (j != FT_LAYERS1))
		{
			if (!nlayer || layer_add(0, 0, 1, 0, mem_pal_def, 0))
				nlayer = load_image(fname, FS_LAYER_LOAD, j) == 1;
			if (nlayer) set_new_filename(layers_total, fname);
		}
	}
	if (!nlayer) layer_delete(layers_total);

	layer_refresh_list();
	layer_choose(layers_total);
	layers_notify_changed();
	if (layers_total) view_show();
	set_image(TRUE);
}


static const GtkTargetEntry uri_list = { "text/uri-list", 0, 1 };

static gboolean drag_n_drop_tried(GtkWidget *widget, GdkDragContext *context,
	gint x, gint y, guint time, gpointer user_data)
{
	GdkAtom target = gdk_atom_intern("text/uri-list", FALSE);
	gpointer tp = GUINT_TO_POINTER(target);
	GList *src;

	/* Check if drop could provide a supported format */
	for (src = context->targets; src && (src->data != tp); src = src->next);
	if (!src) return (FALSE);
	/* Trigger "drag_data_received" event */
	gtk_drag_get_data(widget, context, target, time);
	return (TRUE);
}

static void drag_n_drop_received(GtkWidget *widget, GdkDragContext *context,
	gint x, gint y, GtkSelectionData *data, guint info, guint time)
{
	int success;

	if ((success = ((data->length >= 0) && (data->format == 8))))
		parse_drag((gchar *)data->data);
	/* Accept move as a copy (disallow deleting source) */
	gtk_drag_finish(context, success, FALSE, time);
}


typedef struct
{
	char *path; /* Full path for now */
	signed char radio_BTS; /* -2..-5 are for BTS */
	unsigned short ID;
	int actmap;
	char *shortcut; /* Text form for now */
	short action, mode;
	XPM_TYPE xpm_icon_image;
} menu_item;

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

	/* View mode - do nothing */
	if (view_image_only) return (0);
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

		return (ACTMOD_DUMMY);
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
	GtkWidget *w;
	int i, oldst = r_menu_state;

	if (oldst < state)
	{
		for (i = oldst + 1; i <= state; i++)
			gtk_widget_hide(r_menu[i].item);
		if (oldst == 0)
		{
			w = r_menu[0].item;
			gtk_widget_set_state(w, GTK_STATE_NORMAL);
			gtk_widget_show(w);
		}
	}
	else
	{
		for (i = oldst; i > state; i--)
		{
			w = r_menu[i].item;
			gtk_widget_set_state(w, GTK_STATE_NORMAL);
			gtk_widget_show(w);
		}
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
		GtkWidget *child = BOX_CHILD_0(widget);
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
	GtkWidget *child = BOX_CHILD_0(widget);
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
	GtkWidget *child;
	int fullw;

	req->width = req->height = GTK_CONTAINER(widget)->border_width * 2;
	if (!GTK_BOX(widget)->children) return;
	child = BOX_CHILD_0(widget);
	if (!GTK_WIDGET_VISIBLE(child)) return;

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
	GtkWidget *child;
	int border = GTK_CONTAINER(widget)->border_width, border2 = border * 2;

	widget->allocation = *alloc;
	if (!GTK_BOX(widget)->children) return;
	child = BOX_CHILD_0(widget);
	if (!GTK_WIDGET_VISIBLE(child)) return;

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

static void pressed_pal_copy();
static void pressed_pal_paste();
static void pressed_sel_ramp(int vert);

static const signed char arrow_dx[4] = { 0, -1, 1, 0 },
	arrow_dy[4] = { 1, 0, 0, -1 };

static void move_marquee(int action, int *xy, int change, int dir)
{
	int dx = tgrid_dx, dy = tgrid_dy;
	if (!tgrid_snap) dx = dy = change;
	paint_marquee(action,
		xy[0] + dx * arrow_dx[dir], xy[1] + dy * arrow_dy[dir], NULL);
	update_stuff(UPD_SGEOM);
}

void action_dispatch(int action, int mode, int state, int kbd)
{
	int change = mode & 1 ? mem_nudge : 1, dir = (mode >> 1) - 1;

	switch (action)
	{
	case ACT_QUIT:
		quit_all(mode); break;
	case ACT_ZOOM:
		if (!mode) zoom_in();
		else if (mode == -1) zoom_out();
		else align_size(mode > 0 ? mode : -1.0 / mode);
		break;
	case ACT_VIEW:
		toggle_view(); break;
	case ACT_PAN:
		pressed_pan(); break;
	case ACT_CROP:
		pressed_crop(); break;
	case ACT_SWAP_AB:
		mem_swap_cols(TRUE); break;
	case ACT_TOOL:
		if (state || kbd) change_to_tool(mode); // Ignore DEactivating buttons
		break;
	case ACT_SEL_MOVE:
		/* Gradient tool has precedence over selection */
		if ((tool_type != TOOL_GRADIENT) && (marq_status > MARQUEE_NONE))
			move_marquee(MARQ_MOVE, marq_xy, change, dir);
		else move_mouse(change * arrow_dx[dir], change * arrow_dy[dir], 0);
		break;
	case ACT_OPAC:
		pressed_opacity(mode > 0 ? (255 * mode) / 10 :
			tool_opacity + 1 + mode + mode);
		break;
	case ACT_LR_MOVE:
		/* User is selecting so allow CTRL+arrow keys to resize the
		 * marquee; for consistency, gradient tool blocks this */
		if ((tool_type != TOOL_GRADIENT) && (marq_status == MARQUEE_DONE))
			move_marquee(MARQ_SIZE, marq_xy + 2, change, dir);
		else if (bkg_flag && !layer_selected)
		{
			/* !!! Later, maybe localize redraw to the changed part */
			bkg_x += change * arrow_dx[dir];
			bkg_y += change * arrow_dy[dir];
			update_stuff(UPD_RENDER);
		}
		else if (layers_total) move_layer_relative(layer_selected,
			change * arrow_dx[dir], change * arrow_dy[dir]);
		break;
	case ACT_ESC:
		if ((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON))
			pressed_select(FALSE);
		else if (tool_type == TOOL_LINE)
		{
			stop_line();
			update_sel_bar();
		}
		else if ((tool_type == TOOL_GRADIENT) &&
			(gradient[mem_channel].status != GRAD_NONE))
		{
			gradient[mem_channel].status = GRAD_NONE;
			if (grad_opacity) update_stuff(UPD_RENDER);
			else repaint_grad(NULL);
			update_sel_bar();
		}
		break;
	case ACT_COMMIT:
		if (marq_status >= MARQUEE_PASTE)
		{
			commit_paste(mode, NULL);
			pen_down = 0;	// Ensure each press of enter is a new undo level
			mem_undo_prepare();
		}
		else move_mouse(0, 0, 1);
		break;
	case ACT_RCLICK:
		if (marq_status < MARQUEE_PASTE) move_mouse(0, 0, 3);
		break;
	case ACT_ARROW: draw_arrow(mode); break;
	case ACT_A:
	case ACT_B:
		action = action == ACT_B;
		if (mem_channel == CHN_IMAGE)
		{
			mode += mem_col_[action];
			if ((mode >= 0) && (mode < mem_cols))
				mem_col_[action] = mode;
			mem_col_24[action] = mem_pal[mem_col_[action]];
		}
		else
		{
			mode += channel_col_[action][mem_channel];
			if ((mode >= 0) && (mode <= 255))
				channel_col_[action][mem_channel] = mode;
		}
		update_stuff(UPD_CAB);
		break;
	case ACT_CHANNEL:
		if (kbd) state = TRUE;
		if (mode < 0) pressed_channel_create(mode);
		else pressed_channel_edit(state, mode);
		break;
	case ACT_VWZOOM:
		if (!mode)
		{
			if (vw_zoom >= 1) vw_align_size(vw_zoom + 1);
			else vw_align_size(1.0 / (rint(1.0 / vw_zoom) - 1));
		}
		else if (mode == -1)
		{
			if (vw_zoom > 1) vw_align_size(vw_zoom - 1);
			else vw_align_size(1.0 / (rint(1.0 / vw_zoom) + 1));
		}
//		else vw_align_size(mode > 0 ? mode : -1.0 / mode);
		break;
	case ACT_SAVE:
		pressed_save_file(); break;
	case ACT_FACTION:
		pressed_file_action(mode); break;
	case ACT_LOAD_RECENT:
		pressed_load_recent(mode); break;
	case ACT_DO_UNDO:
		pressed_do_undo(mode); break;
	case ACT_COPY:
		pressed_copy(mode); break;
	case ACT_PASTE:
		pressed_paste(mode); break;
	case ACT_COPY_PAL:
		pressed_pal_copy(); break;
	case ACT_PASTE_PAL:
		pressed_pal_paste(); break;
	case ACT_LOAD_CLIP:
		load_clip(mode); break;
	case ACT_SAVE_CLIP:
		save_clip(mode); break;
	case ACT_TBAR:
		pressed_toolbar_toggle(state, mode); break;
	case ACT_DOCK:
		if (!kbd) toggle_dock(show_dock = state);
		else if (GTK_WIDGET_SENSITIVE(menu_widgets[MENU_DOCK]))
			gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
				menu_widgets[MENU_DOCK]), !show_dock);
		break;
	case ACT_CENTER:
		pressed_centralize(state); break;
	case ACT_GRID:
		zoom_grid(state); break;
	case ACT_SNAP:
		tgrid_snap = state;
		break;
	case ACT_VWWIN:
		if (state) view_show();
		else view_hide();
		break;
	case ACT_VWSPLIT:
		pressed_view_hori(state); break;
	case ACT_VWFOCUS:
		pressed_view_focus(state); break;
	case ACT_FLIP_V:
		pressed_flip_image_v(); break;
	case ACT_FLIP_H:
		pressed_flip_image_h(); break;
	case ACT_ROTATE:
		pressed_rotate_image(mode); break;
	case ACT_SELECT:
		pressed_select(mode); break;
	case ACT_LASSO:
		pressed_lasso(mode); break;
	case ACT_OUTLINE:
		pressed_rectangle(mode); break;
	case ACT_ELLIPSE:
		pressed_ellipse(mode); break;
	case ACT_SEL_FLIP_V:
		pressed_flip_sel_v(); break;
	case ACT_SEL_FLIP_H:
		pressed_flip_sel_h(); break;
	case ACT_SEL_ROT:
		pressed_rotate_sel(mode); break;
	case ACT_RAMP:
		pressed_sel_ramp(mode); break;
	case ACT_SEL_ALPHA_AB:
		pressed_clip_alpha_scale(); break;
	case ACT_SEL_ALPHAMASK:
		pressed_clip_alphamask(); break;
	case ACT_SEL_MASK_AB:
		pressed_clip_mask(mode); break;
	case ACT_SEL_MASK:
		if (!mode) pressed_clip_mask_all();
		else pressed_clip_mask_clear();
		break;
	case ACT_PAL_DEF:
		pressed_default_pal(); break;
	case ACT_PAL_MASK:
		pressed_mask(mode); break;
	case ACT_DITHER_A:
		pressed_dither_A(); break;
	case ACT_PAL_MERGE:
		pressed_remove_duplicates(); break;
	case ACT_PAL_CLEAN:
		pressed_remove_unused(); break;
	case ACT_ISOMETRY:
		iso_trans(mode); break;
	case ACT_CHN_DIS:
		pressed_channel_disable(state, mode); break;
	case ACT_SET_RGBA:
		pressed_RGBA_toggle(state); break;
	case ACT_SET_OVERLAY:
		pressed_channel_toggle(state, mode); break;
	case ACT_LR_SAVE:
		layer_press_save(); break;
	case ACT_LR_ADD:
		if (mode == LR_NEW) generic_new_window(1);
		else if (mode == LR_DUP) layer_press_duplicate();
		else if (mode == LR_PASTE) pressed_paste_layer();
		else /* if (mode == LR_COMP) */ layer_add_composite();
		break;
	case ACT_LR_DEL:
		if (!mode) layer_press_delete();
		else layer_press_remove_all();
		break;
	case ACT_DOCS:
		show_html(inifile_get(HANDBOOK_BROWSER_INI, NULL),
			inifile_get(HANDBOOK_LOCATION_INI, NULL));
		break;
	case ACT_REBIND_KEYS:
		rebind_keys(); break;
	case ACT_MODE:
		mode_change(mode, state); break;
	case ACT_LR_SHIFT:
		shift_layer(mode); break;
	case ACT_LR_CENTER:
		layer_press_centre(); break;
	case DLG_BRCOSA:
		pressed_brcosa(); break;
	case DLG_CHOOSER:
		choose_pattern(mode); break;
	case DLG_SCALE:
		pressed_scale_size(TRUE); break;
	case DLG_SIZE:
		pressed_scale_size(FALSE); break;
	case DLG_NEW:
		generic_new_window(0); break;
	case DLG_FSEL:
		file_selector(mode); break;
	case DLG_FACTIONS:
		pressed_file_configure(); break;
	case DLG_TEXT:
		pressed_text(); break;
	case DLG_TEXT_FT:
		pressed_mt_text(); break;
	case DLG_LAYERS:
		if (mode) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
			menu_widgets[MENU_LAYER]), FALSE); // Closed by toolbar
		else if (state) pressed_layers();
		else delete_layers_window();
		break;
	case DLG_INDEXED:
		pressed_quantize(mode); break;
	case DLG_ROTATE:
		pressed_rotate_free(); break;
	case DLG_INFO:
		pressed_information(); break;
	case DLG_PREFS:
		pressed_preferences(); break;
	case DLG_COLORS:
		colour_selector(mode); break;
	case DLG_PAL_SIZE:
		pressed_add_cols(); break;
	case DLG_PAL_SORT:
		pressed_sort_pal(); break;
	case DLG_PAL_SHIFTER:
		pressed_shifter(); break;
	case DLG_CHN_DEL:
		pressed_channel_delete(); break;
	case DLG_ANI:
		pressed_animate_window(); break;
	case DLG_ANI_VIEW:
		ani_but_preview(NULL); break;
	case DLG_ANI_KEY:
		pressed_set_key_frame(); break;
	case DLG_ANI_KILLKEY:
		pressed_remove_key_frames(); break;
	case DLG_ABOUT:
		pressed_help(); break;
	case DLG_SKEW:
		pressed_skew(); break;
	case DLG_FLOOD:
		flood_settings(); break;
	case DLG_SMUDGE:
		smudge_settings(); break;
	case DLG_GRAD:
		gradient_setup(mode); break;
	case DLG_STEP:
		step_settings(); break;
	case DLG_FILT:
		blend_settings(); break;
	case DLG_TRACE:
		bkg_setup(); break;
	case DLG_PICK_GRAD:
		pressed_pick_gradient(); break;
	case DLG_SEGMENT:
		pressed_segment(); break;
	case FILT_2RGB:
		pressed_convert_rgb(); break;
	case FILT_INVERT:
		pressed_invert(); break;
	case FILT_GREY:
		pressed_greyscale(mode); break;
	case FILT_EDGE:
		pressed_edge_detect(); break;
	case FILT_DOG:
		pressed_dog(); break;
	case FILT_SHARPEN:
		pressed_sharpen(); break;
	case FILT_UNSHARP:
		pressed_unsharp(); break;
	case FILT_SOFTEN:
		pressed_soften(); break;
	case FILT_GAUSS:
		pressed_gauss(); break;
	case FILT_FX:
		pressed_fx(mode); break;
	case FILT_BACT:
		pressed_bacteria(); break;
	case FILT_THRES:
		pressed_threshold(); break;
	case FILT_UALPHA:
		pressed_unassociate(); break;
	case FILT_KUWAHARA:
		pressed_kuwahara(); break;
	}
}

static void menu_action(GtkMenuItem *widget, gpointer user_data)
{
	menu_item *item = user_data;

	action_dispatch(item->action, item->mode, item->radio_BTS < 0 ? TRUE :
		GTK_CHECK_MENU_ITEM(widget)->active, FALSE);
}

#if GTK2VERSION >= 4 /* Not needed before GTK+ 2.4 */

/* Ignore shortcut key only when item itself is insensitive or hidden */
static gboolean menu_allow_key(GtkWidget *widget, guint signal_id, gpointer user_data)
{
	return (GTK_WIDGET_IS_SENSITIVE(widget) && GTK_WIDGET_VISIBLE(widget));
}

#endif

static GtkWidget *fill_menu(menu_item *items, GtkAccelGroup *accel_group)
{
	GtkWidget *wrap, *mbar, *item, *parent, *label;
	GtkWidget *parents[16], *radio[32];
	char *s, *names[MENU_RESIZE_MAX];
	int i, itp, depth, rn = 0, sp = 0;

#if GTK_MAJOR_VERSION == 2
	gtk_accel_map_add_entry("<f>/a/k/e", 0, 0);
#endif
	memset(radio, 0, sizeof(radio));
	parents[sp++] = mbar = gtk_menu_bar_new();
	for (; items->path; items++)
	{
		guint keyval, mods;

		s = items->path;
		depth = strspn(s, "/");
		if (sp < depth) continue; // Nesting error
		sp = depth; // Select level
		if (s[depth]) s = __(s); /* Translate an existing name */
		s += depth;

		itp = items->radio_BTS;
		if (itp <= -16) itp += 16;
		if (itp == 0) item = gtk_check_menu_item_new_with_label("");
		else if (itp > 0)
		{
			GSList *group = radio[itp] ? gtk_radio_menu_item_group(
				GTK_RADIO_MENU_ITEM(radio[itp])) : NULL;
			radio[itp] = item = gtk_radio_menu_item_new_with_label(
				group, "");
		}
		else if (itp == -3) item = gtk_tearoff_menu_item_new();
#if GTK_MAJOR_VERSION == 2
		else if ((itp == -1) && show_menu_icons && items->xpm_icon_image)
		{
			item = gtk_image_menu_item_new_with_label("");
			gtk_image_menu_item_set_image(GTK_IMAGE_MENU_ITEM(item),
				xpm_image(items->xpm_icon_image));
		}
#endif
		else if (itp == -4)
		{
			item = gtk_menu_item_new();
			gtk_widget_set_sensitive(item, FALSE);
		}
		else item = gtk_menu_item_new_with_label("");
		gtk_widget_show(item);

		parent = parents[sp - 1];
		gtk_container_add(GTK_CONTAINER(parent), item);

		if (itp >= 0) gtk_check_menu_item_set_show_toggle(
			GTK_CHECK_MENU_ITEM(item), TRUE);
		if ((itp != -4) && (itp != -3))
		{
			label = GTK_BIN(item)->child;
			keyval = gtk_label_parse_uline(GTK_LABEL(label), s);
			if ((parent == mbar) && (keyval != GDK_VoidSymbol))
#if GTK_MAJOR_VERSION == 1
				gtk_widget_add_accelerator(item, "activate_item",
				accel_group, keyval, GDK_MOD1_MASK, GTK_ACCEL_LOCKED);
#else
				gtk_label_set_text_with_mnemonic(GTK_LABEL(label), s);
#endif
		}
		if (itp == -5) gtk_menu_item_right_justify(GTK_MENU_ITEM(item));
		if ((itp == -2) || (itp == -5))
		{
			parents[sp++] = parent = gtk_menu_new();
			gtk_menu_set_accel_group(GTK_MENU(parent), accel_group);
			gtk_menu_item_set_submenu(GTK_MENU_ITEM(item), parent);
		}
		else if (items->action) gtk_signal_connect(GTK_OBJECT(item),
			"activate", GTK_SIGNAL_FUNC(menu_action), items);
		if (items->shortcut)
		{
			gtk_accelerator_parse(items->shortcut, &keyval, &mods);
			gtk_widget_add_accelerator(item, "activate",
				accel_group, keyval, mods, GTK_ACCEL_VISIBLE);
		}
#if GTK_MAJOR_VERSION == 2
		// !!! Otherwise GTK+ won't add spacing to an empty accel field
		else gtk_widget_set_accel_path(item, "<f>/a/k/e", accel_group);
#endif
#if GTK2VERSION >= 4
		/* !!! GTK+ 2.4+ ignores invisible menu items' keys by default */
		gtk_signal_connect(GTK_OBJECT(item), "can_activate_accel",
			GTK_SIGNAL_FUNC(menu_allow_key), NULL);
#endif
		mapped_dis_add(item, items->actmap);
		/* For now, remember only requested widgets */
		if (items->ID) menu_widgets[items->ID] = item;
		/* Remember what is size-aware */
		if (items->radio_BTS > -16) continue;
		r_menu[rn].item = item;
		r_menu[rn].key = keyval;
		names[rn] = s;
		rn++;
	}

	/* Setup overflow submenu */
	parent = GTK_MENU_ITEM(r_menu[--rn].item)->submenu;
	for (i = 0; i < rn; i++)
	{
		r_menu[i].fallback = item = gtk_menu_item_new_with_label("");
		gtk_container_add(GTK_CONTAINER(parent), item);
		gtk_label_parse_uline(GTK_LABEL(GTK_BIN(item)->child), names[i]);
	}
	for (i = 0; i <= rn / 2; i++) // Swap ends
	{
		r_menu_slot tmp = r_menu[i];
		r_menu[i] = r_menu[rn - i];
		r_menu[rn - i] = tmp;
	}
	gtk_widget_hide(r_menu[0].item);

	/* Wrap menubar with resize-controller widget */
	gtk_widget_show(mbar);
	wrap = wj_size_box();
	gtk_container_add(GTK_CONTAINER(wrap), mbar);
	gtk_signal_connect(GTK_OBJECT(wrap), "size_request",
		GTK_SIGNAL_FUNC(smart_menu_size_req), NULL);
	gtk_signal_connect(GTK_OBJECT(wrap), "size_allocate",
		GTK_SIGNAL_FUNC(smart_menu_size_alloc), NULL);

	return (wrap);
}

static int do_pal_copy(png_color *tpal, unsigned char *img,
	unsigned char *alpha, unsigned char *mask,
	unsigned char *mask2, png_color *wpal,
	int w, int h, int bpp, int step)
{
	int i, j, n;

	mem_pal_copy(tpal, mem_pal);
	for (n = i = 0; i < h; i++)
	{
		for (j = 0; j < w; j++ , img += bpp)
		{
			/* Skip empty parts */
			if ((mask2 && !mask2[j]) || (mask && !mask[j]) ||
				(alpha && !alpha[j])) continue;
			if (bpp == 1) tpal[n] = wpal[*img];
			else
			{
				tpal[n].red = img[0];
				tpal[n].green = img[1];
				tpal[n].blue = img[2];
			}
			if (++n >= 256) return (256);
		}
		img += (step - w) * bpp;
		if (alpha) alpha += step;
		if (mask) mask += step;
		if (mask2) mask2 += w;
	}
	return (n);
}

static void pressed_pal_copy()
{
	png_color tpal[256];
	int n = 0;

	/* Source is selection */
	if ((marq_status == MARQUEE_DONE) || (poly_status == POLY_DONE))
	{
		unsigned char *mask2 = NULL;
		int i, bpp, rect[4];

		marquee_at(rect);
		if ((poly_status == POLY_DONE) &&
			(mask2 = calloc(1, rect[2] * rect[3])))
			poly_draw(TRUE, mask2, rect[2]);

		i = rect[1] * mem_width + rect[0];
		bpp = MEM_BPP;
		n = do_pal_copy(tpal, mem_img[mem_channel] + i * bpp,
			(mem_channel == CHN_IMAGE) && mem_img[CHN_ALPHA] ?
			mem_img[CHN_ALPHA] + i : NULL,
			(mem_channel <= CHN_ALPHA) && mem_img[CHN_SEL] ?
			mem_img[CHN_SEL] + i : NULL,
			mask2, mem_pal,
			rect[2], rect[3], bpp, mem_width);

		if (mask2) free(mask2);
	}
	/* Source is clipboard */
	else if (mem_clipboard) n = do_pal_copy(tpal, mem_clipboard,
		mem_clip_alpha, mem_clip_mask,
		NULL, mem_clip_paletted ? mem_clip_pal : mem_pal,
		mem_clip_w, mem_clip_h, mem_clip_bpp, mem_clip_w);

	if (!n) return; // Empty set - ignore
	spot_undo(UNDO_PAL);
	mem_pal_copy(mem_pal, tpal);
	mem_cols = n;
	update_stuff(UPD_PAL);
}

static void pressed_pal_paste()
{
	unsigned char *dest;
	int i, h, w = mem_cols;

	// If selection exists, use it to set the width of the clipboard
	if ((tool_type == TOOL_SELECT) && (marq_status == MARQUEE_DONE))
	{
		w = abs(marq_x1 - marq_x2) + 1;
		if (w > mem_cols) w = mem_cols;
	}
	h = (mem_cols + w - 1) / w;

	mem_clip_new(w, h, 1, CMASK_IMAGE, FALSE);
	if (!mem_clipboard)
	{
		memory_errors(1);
		return;
	}
	mem_clip_paletted = 1;
	mem_pal_copy(mem_clip_pal, mem_pal);
	memset(dest = mem_clipboard, 0, w * h);
	for (i = 0; i < mem_cols; i++) dest[i] = i;

	pressed_paste(TRUE);
}

///	DOCK AREA

static void toggle_dock(int state)
{
	main_dd *dt = GET_DDATA(main_window_);

	cmd_set(dt->dock, state);
// !!! Later make this canvas' "realize" handler
	set_cursor(NULL); /* Because canvas window is now a new one */
}

static void pressed_sel_ramp(int vert)
{
	unsigned char *c0, *c1, *img, *dest;
	int i, j, k, l, s1, s2, bpp = MEM_BPP, rect[4];

	if (marq_status != MARQUEE_DONE) return;

	marquee_at(rect);

	if (vert)		// Vertical ramp
	{
		k = rect[3] - 1; l = rect[2];
		s1 = mem_width * bpp; s2 = bpp;
	}
	else			// Horizontal ramp
	{
		k = rect[2] - 1; l = rect[3];
		s1 = bpp; s2 = mem_width * bpp;
	}

	spot_undo(UNDO_FILT);
	img = mem_img[mem_channel] + (rect[1] * mem_width + rect[0]) * bpp;
	for (i = 0; i < l; i++)
	{
		c0 = img; c1 = img + k * s1;
		dest = img;
		for (j = 1; j < k; j++)
		{
			dest += s1;
			dest[0] = (c0[0] * (k - j) + c1[0] * j) / k;
			if (bpp == 1) continue;
			dest[1] = (c0[1] * (k - j) + c1[1] * j) / k;
			dest[2] = (c0[2] * (k - j) + c1[2] * j) / k;
		}
		img += s2;
	}

	update_stuff(UPD_IMG);
}

/* !!! Keep MENU_RESIZE_MAX larger than number of resize-enabled items */

static menu_item main_menu[] = {
	{ _("/_File"), -2 -16 },
	{ "//", -3 },
	{ _("//New"), -1, 0, 0, "<control>N", DLG_NEW, 0, XPM_ICON(new) },
	{ _("//Open ..."), -1, 0, 0, "<control>O", DLG_FSEL, FS_PNG_LOAD, XPM_ICON(open) },
	{ _("//Save"), -1, 0, 0, "<control>S", ACT_SAVE, 0, XPM_ICON(save) },
	{ _("//Save As ..."), -1, 0, 0, NULL, DLG_FSEL, FS_PNG_SAVE },
	{ "//", -4 },
	{ _("//Export Undo Images ..."), -1, 0, NEED_UNDO, NULL, DLG_FSEL, FS_EXPORT_UNDO },
	{ _("//Export Undo Images (reversed) ..."), -1, 0, NEED_UNDO, NULL, DLG_FSEL, FS_EXPORT_UNDO2 },
	{ _("//Export ASCII Art ..."), -1, 0, NEED_IDX, NULL, DLG_FSEL, FS_EXPORT_ASCII },
	{ _("//Export Animated GIF ..."), -1, 0, NEED_IDX, NULL, DLG_FSEL, FS_EXPORT_GIF },
	{ "//", -4 },
	{ _("//Actions"), -2 },
	{ "///", -3 },
	{ "///", -1, MENU_FACTION1, 0, NULL, ACT_FACTION, 1 },
	{ "///", -1, MENU_FACTION2, 0, NULL, ACT_FACTION, 2 },
	{ "///", -1, MENU_FACTION3, 0, NULL, ACT_FACTION, 3 },
	{ "///", -1, MENU_FACTION4, 0, NULL, ACT_FACTION, 4 },
	{ "///", -1, MENU_FACTION5, 0, NULL, ACT_FACTION, 5 },
	{ "///", -1, MENU_FACTION6, 0, NULL, ACT_FACTION, 6 },
	{ "///", -1, MENU_FACTION7, 0, NULL, ACT_FACTION, 7 },
	{ "///", -1, MENU_FACTION8, 0, NULL, ACT_FACTION, 8 },
	{ "///", -1, MENU_FACTION9, 0, NULL, ACT_FACTION, 9 },
	{ "///", -1, MENU_FACTION10, 0, NULL, ACT_FACTION, 10 },
	{ "///", -1, MENU_FACTION11, 0, NULL, ACT_FACTION, 11 },
	{ "///", -1, MENU_FACTION12, 0, NULL, ACT_FACTION, 12 },
	{ "///", -1, MENU_FACTION13, 0, NULL, ACT_FACTION, 13 },
	{ "///", -1, MENU_FACTION14, 0, NULL, ACT_FACTION, 14 },
	{ "///", -1, MENU_FACTION15, 0, NULL, ACT_FACTION, 15 },
	{ "///", -4, MENU_FACTION_S },
	{ _("///Configure"), -1, 0, 0, NULL, DLG_FACTIONS, 0 },
	{ "//", -4, MENU_RECENT_S },
	{ "//", -1, MENU_RECENT1, 0, "<shift><control>F1", ACT_LOAD_RECENT, 1 },
	{ "//", -1, MENU_RECENT2, 0, "<shift><control>F2", ACT_LOAD_RECENT, 2 },
	{ "//", -1, MENU_RECENT3, 0, "<shift><control>F3", ACT_LOAD_RECENT, 3 },
	{ "//", -1, MENU_RECENT4, 0, "<shift><control>F4", ACT_LOAD_RECENT, 4 },
	{ "//", -1, MENU_RECENT5, 0, "<shift><control>F5", ACT_LOAD_RECENT, 5 },
	{ "//", -1, MENU_RECENT6, 0, "<shift><control>F6", ACT_LOAD_RECENT, 6 },
	{ "//", -1, MENU_RECENT7, 0, "<shift><control>F7", ACT_LOAD_RECENT, 7 },
	{ "//", -1, MENU_RECENT8, 0, "<shift><control>F8", ACT_LOAD_RECENT, 8 },
	{ "//", -1, MENU_RECENT9, 0, "<shift><control>F9", ACT_LOAD_RECENT, 9 },
	{ "//", -1, MENU_RECENT10, 0, "<shift><control>F10", ACT_LOAD_RECENT, 10 },
	{ "//", -1, MENU_RECENT11, 0, NULL, ACT_LOAD_RECENT, 11 },
	{ "//", -1, MENU_RECENT12, 0, NULL, ACT_LOAD_RECENT, 12 },
	{ "//", -1, MENU_RECENT13, 0, NULL, ACT_LOAD_RECENT, 13 },
	{ "//", -1, MENU_RECENT14, 0, NULL, ACT_LOAD_RECENT, 14 },
	{ "//", -1, MENU_RECENT15, 0, NULL, ACT_LOAD_RECENT, 15 },
	{ "//", -1, MENU_RECENT16, 0, NULL, ACT_LOAD_RECENT, 16 },
	{ "//", -1, MENU_RECENT17, 0, NULL, ACT_LOAD_RECENT, 17 },
	{ "//", -1, MENU_RECENT18, 0, NULL, ACT_LOAD_RECENT, 18 },
	{ "//", -1, MENU_RECENT19, 0, NULL, ACT_LOAD_RECENT, 19 },
	{ "//", -1, MENU_RECENT20, 0, NULL, ACT_LOAD_RECENT, 20 },
	{ "//", -4 },
	{ _("//Quit"), -1, 0, 0, "<control>Q", ACT_QUIT, 1 },

	{ _("/_Edit"), -2 -16 },
	{ "//", -3 },
	{ _("//Undo"), -1, 0, NEED_UNDO, "<control>Z", ACT_DO_UNDO, 0, XPM_ICON(undo) },
	{ _("//Redo"), -1, 0, NEED_REDO, "<control>R", ACT_DO_UNDO, 1, XPM_ICON(redo) },
	{ "//", -4 },
	{ _("//Cut"), -1, 0, NEED_SEL2, "<control>X", ACT_COPY, 1, XPM_ICON(cut) },
	{ _("//Copy"), -1, 0, NEED_SEL2, "<control>C", ACT_COPY, 0, XPM_ICON(copy) },
	{ _("//Copy To Palette"), -1, 0, NEED_PSEL, NULL, ACT_COPY_PAL, 0 },
	{ _("//Paste To Centre"), -1, 0, NEED_CLIP, "<control>V", ACT_PASTE, 1, XPM_ICON(paste) },
	{ _("//Paste To New Layer"), -1, 0, NEED_PCLIP, "<control><shift>V", ACT_LR_ADD, LR_PASTE },
	{ _("//Paste"), -1, 0, NEED_CLIP, "<control>K", ACT_PASTE, 0 },
	{ _("//Paste Text"), -1, 0, 0, "<shift>T", DLG_TEXT, 0, XPM_ICON(text) },
#ifdef U_FREETYPE
	{ _("//Paste Text (FreeType)"), -1, 0, 0, "T", DLG_TEXT_FT, 0 },
#endif
	{ _("//Paste Palette"), -1, 0, 0, NULL, ACT_PASTE_PAL, 0 },
	{ "//", -4 },
	{ _("//Load Clipboard"), -2 },
	{ "///", -3 },
	{ "///1", -1, 0, 0, "<shift>F1", ACT_LOAD_CLIP, 1 },
	{ "///2", -1, 0, 0, "<shift>F2", ACT_LOAD_CLIP, 2 },
	{ "///3", -1, 0, 0, "<shift>F3", ACT_LOAD_CLIP, 3 },
	{ "///4", -1, 0, 0, "<shift>F4", ACT_LOAD_CLIP, 4 },
	{ "///5", -1, 0, 0, "<shift>F5", ACT_LOAD_CLIP, 5 },
	{ "///6", -1, 0, 0, "<shift>F6", ACT_LOAD_CLIP, 6 },
	{ "///7", -1, 0, 0, "<shift>F7", ACT_LOAD_CLIP, 7 },
	{ "///8", -1, 0, 0, "<shift>F8", ACT_LOAD_CLIP, 8 },
	{ "///9", -1, 0, 0, "<shift>F9", ACT_LOAD_CLIP, 9 },
	{ "///10", -1, 0, 0, "<shift>F10", ACT_LOAD_CLIP, 10 },
	{ "///11", -1, 0, 0, "<shift>F11", ACT_LOAD_CLIP, 11 },
	{ "///12", -1, 0, 0, "<shift>F12", ACT_LOAD_CLIP, 12 },
	{ _("//Save Clipboard"), -2 },
	{ "///", -3 },
	{ "///1", -1, 0, NEED_CLIP, "<control>F1", ACT_SAVE_CLIP, 1 },
	{ "///2", -1, 0, NEED_CLIP, "<control>F2", ACT_SAVE_CLIP, 2 },
	{ "///3", -1, 0, NEED_CLIP, "<control>F3", ACT_SAVE_CLIP, 3 },
	{ "///4", -1, 0, NEED_CLIP, "<control>F4", ACT_SAVE_CLIP, 4 },
	{ "///5", -1, 0, NEED_CLIP, "<control>F5", ACT_SAVE_CLIP, 5 },
	{ "///6", -1, 0, NEED_CLIP, "<control>F6", ACT_SAVE_CLIP, 6 },
	{ "///7", -1, 0, NEED_CLIP, "<control>F7", ACT_SAVE_CLIP, 7 },
	{ "///8", -1, 0, NEED_CLIP, "<control>F8", ACT_SAVE_CLIP, 8 },
	{ "///9", -1, 0, NEED_CLIP, "<control>F9", ACT_SAVE_CLIP, 9 },
	{ "///10", -1, 0, NEED_CLIP, "<control>F10", ACT_SAVE_CLIP, 10 },
	{ "///11", -1, 0, NEED_CLIP, "<control>F11", ACT_SAVE_CLIP, 11 },
	{ "///12", -1, 0, NEED_CLIP, "<control>F12", ACT_SAVE_CLIP, 12 },
	{ _("//Import Clipboard from System"), -1, 0, 0, NULL, ACT_LOAD_CLIP, -1 },
	{ _("//Export Clipboard to System"), -1, 0, NEED_CLIP, NULL, ACT_SAVE_CLIP, -1 },
	{ "//", -4 },
	{ _("//Choose Pattern ..."), -1, 0, 0, "F2", DLG_CHOOSER, CHOOSE_PATTERN },
	{ _("//Choose Brush ..."), -1, 0, 0, "F3", DLG_CHOOSER, CHOOSE_BRUSH },
	{ _("//Choose Colour ..."), -1, 0, 0, "E", DLG_CHOOSER, CHOOSE_COLOR },

	{ _("/_View"), -2 -16 },
	{ "//", -3 },
	{ _("//Show Main Toolbar"), 0, MENU_TBMAIN, 0, "F5", ACT_TBAR, TOOLBAR_MAIN },
	{ _("//Show Tools Toolbar"), 0, MENU_TBTOOLS, 0, "F6", ACT_TBAR, TOOLBAR_TOOLS },
	{ _("//Show Settings Toolbar"), 0, MENU_TBSET, 0, "F7", ACT_TBAR, TOOLBAR_SETTINGS },
	{ _("//Show Dock"), 0, MENU_DOCK, 0, "F12", ACT_DOCK, 0 },
	{ _("//Show Palette"), 0, MENU_SHOWPAL, 0, "F8", ACT_TBAR, TOOLBAR_PALETTE },
	{ _("//Show Status Bar"), 0, MENU_SHOWSTAT, 0, NULL, ACT_TBAR, TOOLBAR_STATUS },
	{ "//", -4 },
	{ _("//Toggle Image View"), -1, 0, 0, "Home", ACT_VIEW, 0 },
	{ _("//Centralize Image"), 0, MENU_CENTER, 0, NULL, ACT_CENTER, 0 },
	{ _("//Show Zoom Grid"), 0, MENU_SHOWGRID, 0, NULL, ACT_GRID, 0 },
	{ _("//Snap To Tile Grid"), 0, 0, 0, "B", ACT_SNAP, 0 },
	{ _("//Configure Grid ..."), -1, 0, 0, NULL, DLG_COLORS, COLSEL_GRID },
	{ _("//Tracing Image ..."), -1, 0, 0, NULL, DLG_TRACE, 0 },
	{ "//", -4 },
	{ _("//View Window"), 0, MENU_VIEW, 0, "V", ACT_VWWIN, 0 },
	{ _("//Horizontal Split"), 0, 0, 0, "H", ACT_VWSPLIT, 0 },
	{ _("//Focus View Window"), 0, MENU_VWFOCUS, 0, NULL, ACT_VWFOCUS, 0 },
	{ "//", -4 },
	{ _("//Pan Window"), -1, 0, 0, "End", ACT_PAN, 0, XPM_ICON(pan) },
	{ _("//Layers Window"), 0, MENU_LAYER, 0, "L", DLG_LAYERS, 0 },

	{ _("/_Image"), -2 -16 },
	{ "//", -3 },
	{ _("//Convert To RGB"), -1, 0, NEED_IDX, NULL, FILT_2RGB, 0 },
	{ _("//Convert To Indexed ..."), -1, 0, NEED_24, NULL, DLG_INDEXED, 0 },
	{ "//", -4 },
	{ _("//Scale Canvas ..."), -1, 0, 0, "Page_Up", DLG_SCALE, 0 },
	{ _("//Resize Canvas ..."), -1, 0, 0, "Page_Down", DLG_SIZE, 0 },
	{ _("//Crop"), -1, 0, NEED_CROP, "<control><shift>X", ACT_CROP, 0 },
	{ "//", -4 },
	{ _("//Flip Vertically"), -1, 0, 0, NULL, ACT_FLIP_V, 0 },
	{ _("//Flip Horizontally"), -1, 0, 0, "<control>M", ACT_FLIP_H, 0 },
	{ _("//Rotate Clockwise"), -1, 0, 0, NULL, ACT_ROTATE, 0 },
	{ _("//Rotate Anti-Clockwise"), -1, 0, 0, NULL, ACT_ROTATE, 1 },
	{ _("//Free Rotate ..."), -1, 0, 0, NULL, DLG_ROTATE, 0 },
	{ _("//Skew ..."), -1, 0, 0, NULL, DLG_SKEW, 0 },
	{ "//", -4 },
// !!! Maybe support indexed mode too, later
	{ _("//Segment ..."), -1, 0, NEED_24, NULL, DLG_SEGMENT, 0 },
	{ "//", -4 },
	{ _("//Information ..."), -1, 0, 0, "<control>I", DLG_INFO, 0 },
	{ _("//Preferences ..."), -1, MENU_PREFS, 0, "<control>P", DLG_PREFS, 0 },

	{ _("/_Selection"), -2 -16 },
	{ "//", -3 },
	{ _("//Select All"), -1, 0, 0, "<control>A", ACT_SELECT, 1 },
	{ _("//Select None (Esc)"), -1, 0, NEED_MARQ, "<shift><control>A", ACT_SELECT, 0 },
	{ _("//Lasso Selection"), -1, 0, NEED_LAS2, "J", ACT_LASSO, 0, XPM_ICON(lasso) },
	{ _("//Lasso Selection Cut"), -1, 0, NEED_LASSO, NULL, ACT_LASSO, 1 },
	{ "//", -4 },
	{ _("//Outline Selection"), -1, 0, NEED_SEL2, "<control>T", ACT_OUTLINE, 0, XPM_ICON(rect1) },
	{ _("//Fill Selection"), -1, 0, NEED_SEL2, "<shift><control>T", ACT_OUTLINE, 1, XPM_ICON(rect2) },
	{ _("//Outline Ellipse"), -1, 0, NEED_SEL, "<control>L", ACT_ELLIPSE, 0, XPM_ICON(ellipse2) },
	{ _("//Fill Ellipse"), -1, 0, NEED_SEL, "<shift><control>L", ACT_ELLIPSE, 1, XPM_ICON(ellipse) },
	{ "//", -4 },
	{ _("//Flip Vertically"), -1, 0, NEED_CLIP, NULL, ACT_SEL_FLIP_V, 0, XPM_ICON(flip_vs) },
	{ _("//Flip Horizontally"), -1, 0, NEED_CLIP, NULL, ACT_SEL_FLIP_H, 0, XPM_ICON(flip_hs) },
	{ _("//Rotate Clockwise"), -1, 0, NEED_CLIP, NULL, ACT_SEL_ROT, 0, XPM_ICON(rotate_cs) },
	{ _("//Rotate Anti-Clockwise"), -1, 0, NEED_CLIP, NULL, ACT_SEL_ROT, 1, XPM_ICON(rotate_as) },
	{ "//", -4 },
	{ _("//Horizontal Ramp"), -1, 0, NEED_SEL, NULL, ACT_RAMP, 0 },
	{ _("//Vertical Ramp"), -1, 0, NEED_SEL, NULL, ACT_RAMP, 1 },
	{ "//", -4 },
	{ _("//Alpha Blend A,B"), -1, 0, NEED_ACLIP, NULL, ACT_SEL_ALPHA_AB, 0 },
	{ _("//Move Alpha to Mask"), -1, 0, NEED_CLIP, NULL, ACT_SEL_ALPHAMASK, 0 },
	{ _("//Mask Colour A,B"), -1, 0, NEED_CLIP, NULL, ACT_SEL_MASK_AB, 0 },
	{ _("//Unmask Colour A,B"), -1, 0, NEED_CLIP, NULL, ACT_SEL_MASK_AB, 255 },
	{ _("//Mask All Colours"), -1, 0, NEED_CLIP, NULL, ACT_SEL_MASK, 0 },
	{ _("//Clear Mask"), -1, 0, NEED_CLIP, NULL, ACT_SEL_MASK, 255 },

	{ _("/_Palette"), -2 -16 },
	{ "//", -3 },
	{ _("//Load ..."), -1, 0, 0, NULL, DLG_FSEL, FS_PALETTE_LOAD, XPM_ICON(open) },
	{ _("//Save As ..."), -1, 0, 0, NULL, DLG_FSEL, FS_PALETTE_SAVE, XPM_ICON(save) },
	{ _("//Load Default"), -1, 0, 0, NULL, ACT_PAL_DEF, 0 },
	{ "//", -4 },
	{ _("//Mask All"), -1, 0, 0, NULL, ACT_PAL_MASK, 255 },
	{ _("//Mask None"), -1, 0, 0, NULL, ACT_PAL_MASK, 0 },
	{ "//", -4 },
	{ _("//Swap A & B"), -1, 0, 0, "X", ACT_SWAP_AB, 0 },
	{ _("//Edit Colour A & B ..."), -1, 0, 0, "<control>E", DLG_COLORS, COLSEL_EDIT_AB },
	{ _("//Dither A"), -1, 0, NEED_24, NULL, ACT_DITHER_A, 0 },
	{ _("//Palette Editor ..."), -1, 0, 0, "<control>W", DLG_COLORS, COLSEL_EDIT_ALL },
	{ _("//Set Palette Size ..."), -1, 0, 0, NULL, DLG_PAL_SIZE, 0 },
	{ _("//Merge Duplicate Colours"), -1, 0, NEED_IDX, NULL, ACT_PAL_MERGE, 0 },
	{ _("//Remove Unused Colours"), -1, 0, NEED_IDX, NULL, ACT_PAL_CLEAN, 0 },
	{ "//", -4 },
	{ _("//Create Quantized ..."), -1, 0, NEED_24, NULL, DLG_INDEXED, 1 },
	{ "//", -4 },
	{ _("//Sort Colours ..."), -1, 0, 0, NULL, DLG_PAL_SORT, 0 },
	{ _("//Palette Shifter ..."), -1, 0, 0, NULL, DLG_PAL_SHIFTER, 0 },
	{ _("//Pick Gradient ..."), -1, 0, 0, NULL, DLG_PICK_GRAD, 0 },

	{ _("/Effe_cts"), -2 -16 },
	{ "//", -3 },
	{ _("//Transform Colour ..."), -1, 0, 0, "<control><shift>C", DLG_BRCOSA, 0, XPM_ICON(brcosa) },
	{ _("//Invert"), -1, 0, 0, "<control><shift>I", FILT_INVERT, 0 },
	{ _("//Greyscale"), -1, 0, 0, "<control>G", FILT_GREY, 0 },
	{ _("//Greyscale (Gamma corrected)"), -1, 0, 0, "<control><shift>G", FILT_GREY, 1 },
	{ _("//Isometric Transformation"), -2 },
	{ "///", -3 },
	{ _("///Left Side Down"), -1, 0, 0, NULL, ACT_ISOMETRY, 0 },
	{ _("///Right Side Down"), -1, 0, 0, NULL, ACT_ISOMETRY, 1 },
	{ _("///Top Side Right"), -1, 0, 0, NULL, ACT_ISOMETRY, 2 },
	{ _("///Bottom Side Right"), -1, 0, 0, NULL, ACT_ISOMETRY, 3 },
	{ "//", -4 },
	{ _("//Edge Detect ..."), -1, 0, NEED_NOIDX, NULL, FILT_EDGE, 0 },
	{ _("//Difference of Gaussians ..."), -1, 0, NEED_NOIDX, NULL, FILT_DOG, 0 },
	{ _("//Sharpen ..."), -1, 0, NEED_NOIDX, NULL, FILT_SHARPEN, 0 },
	{ _("//Unsharp Mask ..."), -1, 0, NEED_NOIDX, NULL, FILT_UNSHARP, 0 },
	{ _("//Soften ..."), -1, 0, NEED_NOIDX, NULL, FILT_SOFTEN, 0 },
	{ _("//Gaussian Blur ..."), -1, 0, NEED_NOIDX, NULL, FILT_GAUSS, 0 },
	{ _("//Kuwahara-Nagao Blur ..."), -1, 0, NEED_24, NULL, FILT_KUWAHARA, 0 },
	{ _("//Emboss"), -1, 0, NEED_NOIDX, NULL, FILT_FX, FX_EMBOSS },
	{ _("//Dilate"), -1, 0, NEED_NOIDX, NULL, FILT_FX, FX_DILATE },
	{ _("//Erode"), -1, 0, NEED_NOIDX, NULL, FILT_FX, FX_ERODE },
	{ "//", -4 },
	{ _("//Bacteria ..."), -1, 0, NEED_IDX, NULL, FILT_BACT, 0 },

	{ _("/Cha_nnels"), -2 -16 },
	{ "//", -3 },
	{ _("//New ..."), -1, 0, 0, NULL, ACT_CHANNEL, -1 },
	{ _("//Load ..."), -1, 0, 0, NULL, DLG_FSEL, FS_CHANNEL_LOAD, XPM_ICON(open) },
	{ _("//Save As ..."), -1, 0, 0, NULL, DLG_FSEL, FS_CHANNEL_SAVE, XPM_ICON(save) },
	{ _("//Delete ..."), -1, 0, NEED_CHAN, NULL, DLG_CHN_DEL, -1 },
	{ "//", -4 },
	{ _("//Edit Image"), 1, MENU_CHAN0, 0, "<shift>1", ACT_CHANNEL, CHN_IMAGE },
	{ _("//Edit Alpha"), 1, MENU_CHAN1, 0, "<shift>2", ACT_CHANNEL, CHN_ALPHA },
	{ _("//Edit Selection"), 1, MENU_CHAN2, 0, "<shift>3", ACT_CHANNEL, CHN_SEL },
	{ _("//Edit Mask"), 1, MENU_CHAN3, 0, "<shift>4", ACT_CHANNEL, CHN_MASK },
	{ "//", -4 },
	{ _("//Hide Image"), 0, MENU_DCHAN0, 0, NULL, ACT_SET_OVERLAY, 1 },
	{ _("//Disable Alpha"), 0, MENU_DCHAN1, 0, NULL, ACT_CHN_DIS, CHN_ALPHA },
	{ _("//Disable Selection"), 0, MENU_DCHAN2, 0, NULL, ACT_CHN_DIS, CHN_SEL },
	{ _("//Disable Mask"), 0, MENU_DCHAN3, 0, NULL, ACT_CHN_DIS, CHN_MASK },
	{ "//", -4 },
	{ _("//Couple RGBA Operations"), 0, MENU_RGBA, 0, NULL, ACT_SET_RGBA, 0 },
	{ _("//Threshold ..."), -1, 0, 0, NULL, FILT_THRES, 0 },
	{ _("//Unassociate Alpha"), -1, 0, NEED_RGBA, NULL, FILT_UALPHA, 0 },
	{ "//", -4 },
	{ _("//View Alpha as an Overlay"), 0, 0, 0, NULL, ACT_SET_OVERLAY, 0 },
	{ _("//Configure Overlays ..."), -1, 0, 0, NULL, DLG_COLORS, COLSEL_OVERLAYS },

	{ _("/_Layers"), -2 -16 },
	{ "//", -3 },
	{ _("//New Layer"), -1, 0, 0, NULL, ACT_LR_ADD, LR_NEW, XPM_ICON(new) },
	{ _("//Save"), -1, 0, 0, "<shift><control>S", ACT_LR_SAVE, 0, XPM_ICON(save) },
	{ _("//Save As ..."), -1, 0, 0, NULL, DLG_FSEL, FS_LAYER_SAVE },
	{ _("//Save Composite Image ..."), -1, 0, 0, NULL, DLG_FSEL, FS_COMPOSITE_SAVE },
	{ _("//Composite to New Layer"), -1, 0, 0, NULL, ACT_LR_ADD, LR_COMP },
	{ _("//Remove All Layers"), -1, 0, 0, NULL, ACT_LR_DEL, 1 },
	{ "//", -4 },
	{ _("//Configure Animation ..."), -1, 0, 0, NULL, DLG_ANI, 0 },
	{ _("//Preview Animation ..."), -1, 0, 0, NULL, DLG_ANI_VIEW, 0 },
	{ _("//Set Key Frame ..."), -1, 0, 0, NULL, DLG_ANI_KEY, 0 },
	{ _("//Remove All Key Frames ..."), -1, 0, 0, NULL, DLG_ANI_KILLKEY, 0 },

	{ _("/More..."), -2 -16 }, /* This will hold overflow submenu */

	{ _("/_Help"), -5 },
	{ _("//Documentation"), -1, 0, 0, NULL, ACT_DOCS, 0 },
	{ _("//About"), -1, MENU_HELP, 0, "F1", DLG_ABOUT, 0 },
	{ "//", -4 },
	{ _("//Rebind Shortcut Keycodes"), -1, 0, 0, NULL, ACT_REBIND_KEYS, 0 },

	{ NULL, 0 }
	};

static void **create_menu(void **r, GtkWidget ***wpp, void **wdata)
{
	GtkAccelGroup *accel_group = gtk_accel_group_new ();
	int i;


///	MAIN WINDOW

	main_window_ = wdata; // !!! For the code which needs this too early
	main_window = GET_REAL_WINDOW(wdata);
	/* !!! Konqueror needs GDK_ACTION_MOVE to do a drop; we never accept
	 * move as a move, so have to do some non-default processing - WJ */
	gtk_drag_dest_set(main_window, GTK_DEST_DEFAULT_HIGHLIGHT |
		GTK_DEST_DEFAULT_MOTION, &uri_list, 1, GDK_ACTION_COPY |
		GDK_ACTION_MOVE);
	gtk_signal_connect(GTK_OBJECT(main_window), "drag_data_received",
		GTK_SIGNAL_FUNC(drag_n_drop_received), NULL);
	gtk_signal_connect(GTK_OBJECT(main_window), "drag_drop",
		GTK_SIGNAL_FUNC(drag_n_drop_tried), NULL);

///	MENU

	main_menubar = fill_menu(main_menu, accel_group);

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_widgets[MENU_RGBA]), RGBA_mode);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_widgets[MENU_VWFOCUS]), vw_focus_on);

	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_widgets[MENU_CENTER]),
		canvas_image_centre);
	gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(menu_widgets[MENU_SHOWGRID]),
		mem_show_grid);

	for ( i=1; i<TOOLBAR_MAX; i++ )
	{
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
			menu_widgets[MENU_TBMAIN - TOOLBAR_MAIN + i]),
			toolbar_status[i]);	// Menu toggles = status
	}

	pack(**wpp, main_menubar);

	gtk_accel_group_lock( accel_group );	// Stop dynamic allocation of accelerators during runtime
	gtk_window_add_accel_group(GTK_WINDOW(main_window), accel_group);

	return (r);
}

static void **create_internals(void **r, GtkWidget ***wpp, void **wdata)
{
	GtkAdjustment *adj;
	GtkWidget *box = **wpp;


//	MAIN WINDOW

	drawing_canvas = wjcanvas_new();
	wjcanvas_size(drawing_canvas, 48, 48);
	gtk_widget_show(drawing_canvas);

	scrolledwindow_canvas = xpack(box, gtk_scrolled_window_new(NULL, NULL));
	gtk_widget_show(scrolledwindow_canvas);

	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (scrolledwindow_canvas),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	adj = gtk_scrolled_window_get_hadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas));
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed",
		GTK_SIGNAL_FUNC(vw_focus_idle), NULL);
	adj = gtk_scrolled_window_get_vadjustment(GTK_SCROLLED_WINDOW(scrolledwindow_canvas));
	gtk_signal_connect(GTK_OBJECT(adj), "value_changed",
		GTK_SIGNAL_FUNC(vw_focus_idle), NULL);

	add_with_wjframe(scrolledwindow_canvas, drawing_canvas);

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

//	VIEW WINDOW

	vw_scrolledwindow = xpack(box, gtk_scrolled_window_new(NULL, NULL));
//	!!! Left hidden
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW(vw_scrolledwindow),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);

	vw_drawing = wjcanvas_new();
	wjcanvas_size(vw_drawing, 1, 1);
	gtk_widget_show(vw_drawing);
	add_with_wjframe(vw_scrolledwindow, vw_drawing);

	init_view();

	return (r);
}

/////////	End of main window widget setup
static void **finish_internals(void **r, GtkWidget ***wpp, void **wdata)
{
	gtk_signal_connect( GTK_OBJECT(main_window), "key_press_event",
		GTK_SIGNAL_FUNC (handle_keypress), NULL );

	mapped_item_state(0);

	recent_files = recent_files < 0 ? 0 : recent_files > 20 ? 20 : recent_files;
	update_recent_files();

	return (r);
}

static void **finish_dock(void **r, GtkWidget ***wpp, void **wdata)
{
	main_dd *dt = GET_DDATA(wdata);

	// Display dock area if requested
	show_dock = dt->cline_d;
	if (show_dock)
	{
		gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
			menu_widgets[MENU_DOCK]), TRUE);
		// !!! Filelist in the dock should have focus now
	}
	else
	{
		// Stops first icon in toolbar being selected
		gtk_widget_grab_focus(scrolledwindow_canvas);
	}

	return (r);
}

static int cline_keypress(main_dd *dt, void **wdata, int what, void **where,
	key_ext *keydata)
{
	return (check_zoom_keys(wtf_pressed_(keydata))); // Check HOME/zoom keys
}

static void cline_select(main_dd *dt, void **wdata, int what, void **where)
{
	cmd_read(where, dt);

	if (dt->idx_c == dt->nidx_c) return; // no change
	if ((layers_total ? check_layers_for_changes() : check_for_changes()) == 1)
		cmd_set(where, dt->idx_c); // Go back
	// Load requested file
	else do_a_load(file_args[dt->idx_c = dt->nidx_c], undo_load);
}

static void dock_undock_evt(main_dd *dt, void **wdata, int what, void **where)
{
	int dstate;

	cmd_read(where, dt);
	dstate = dt->settings_d | dt->layers_d;

	/* Hide settings + layers page if it's empty */
	cmd_showhide(dt->dockpage1, dstate);

	/* Show tabs only when it makes sense */
	cmd_setv(dt->dockbook, (void *)(dt->cline_d && dstate), NBOOK_TABS);

	/* Close dock if nothing left in it */
	dstate |= dt->cline_d;
	if (!dstate) gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(
		menu_widgets[MENU_DOCK]), FALSE);
	gtk_widget_set_sensitive(menu_widgets[MENU_DOCK], dstate);
}

#define WBbase main_dd
static void *main_code[] = {
	MAINWINDOW(MT_VERSION, icon_xpm, 100, 100), EVENT(CANCEL, delete_event),
	WXYWH("window", 630, 400),
	REF(dock), DOCK("dockSize"),
	EXEC(create_menu),
	CALL(toolbar_code),
	XHBOX,
///	PALETTE
	CALL(toolbar_palette_code),
	XVBOX,
///	DRAWING AREA
	REFv(main_split), HVSPLIT,
	EXEC(create_internals),
	WDONE, // hvsplit
////	STATUS BAR
	REFv(toolbar_boxes[TOOLBAR_STATUS]),
	STATUSBAR, UNLESSv(toolbar_status[TOOLBAR_STATUS]), HIDDEN,
	/* Labels visibility is set later by init_status_bar() */
	REFv(label_bar[STATUS_GEOMETRY]), STLABEL(0, 0, 0),
	REFv(label_bar[STATUS_CURSORXY]), STLABEL(90, 1, 0),
	REFv(label_bar[STATUS_PIXELRGB]), STLABEL(0, 0, 0),
	REFv(label_bar[STATUS_SELEGEOM]), STLABEL(0, 0, 1),
	REFv(label_bar[STATUS_UNDOREDO]), STLABEL(70, 1, 1),
	WDONE, // statusbar
	WDONE, // xvbox
	EXEC(finish_internals),
	WDONE, // xhbox
	WDONE, // left pane
	BORDER(NBOOK, 0),
	REF(dockbook), NBOOKr, KEEPWIDTH,
	IFx(cline_d, 1),
		PAGEi(XPM_ICON(cline), 0),
		BORDER(XSCROLL, 0),
		XSCROLL(1, 1), // auto/auto
		WLIST,
		RTXTCOLUMNDi(0, 0),
		COLUMNDATA(strs_c, sizeof(int)), CLEANUP(strs_c),
		LISTCu(nidx_c, cnt_c, cline_select), FOCUS,
		EVENT(KEY, cline_keypress),
		WDONE,
	ENDIF(1),
	REF(dockpage1), PAGEir(XPM_ICON(layers), 5),
	REFv(settings_dock),
	MOUNT(settings_d, create_settings_box, dock_undock_evt),
	HSEPt,
	REFv(layers_dock),
	PMOUNT(layers_d, create_layers_box, dock_undock_evt, "layers_h", 400),
	TRIGGER,
	WDONE, // page
	WDONE, // nbook
	EXEC(finish_dock), // !!! Later will be TRIGGER on menuitem
	WDONE, // right pane
	RAISED, WSHOW
};
#undef WBbase

void main_init()
{
	main_dd tdata;
	char txt[PATHTXT];

	memset(&tdata, 0, sizeof(tdata));
	/* Prepare commandline list */
	if ((tdata.cline_d = files_passed > 1))
	{
		memx2 mem;
		int i, *p;

		memset(&mem, 0, sizeof(mem));
		getmemx2(&mem, 4000); // default size
		mem.here += getmemx2(&mem, files_passed * sizeof(int)); // minsize
		for (i = 0; i < files_passed; i++)
		{
			p = (int *)mem.buf + i;
			*p = mem.here - ((char *)p - mem.buf);
			/* Convert name string (to UTF-8 in GTK+2) and store it */
			gtkuncpy(txt, file_args[i], sizeof(txt));
			addstr(&mem, txt, 0);
		}
		tdata.cnt_c = files_passed;
		tdata.strs_c = (int *)mem.buf;
	}
	main_window_ = run_create(main_code, &tdata, sizeof(tdata));

	/* !!! Have to wait till canvas is displayed, to init keyboard */
	fill_keycodes(main_keys);

// !!! And what are _these_ doing sitting here, instead of before WSHOW?

	set_cursor(NULL);
	init_status_bar();
	init_factions();				// Initialize file action menu

	file_in_homedir(txt, ".clipboard", PATHBUF);
	strncpy0(mem_clip_file, inifile_get("clipFilename", txt), PATHBUF);

	change_to_tool(DEFAULT_TOOL_ICON);

	toolbar_showhide();
	if (viewer_mode) toggle_view();
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
	/* !!! Slow or not, but NLS is *really* broken on GTK+1 without it - WJ */
	gtk_set_locale();	// GTK+1 hates this - it really slows things down
}
#endif

void update_titlebar()		// Update filename in titlebar
{
	static int changed = -1;
	static char *name = "";
	char txt[300], txt2[PATHTXT];


	/* Don't send needless updates */
	if (!main_window || ((mem_changed == changed) && (mem_filename == name)))
		return;
	changed = mem_changed;
	name = mem_filename;

	snprintf(txt, 290, "%s %s %s", MT_VERSION,
		changed ? __("(Modified)") : "-",
		name ? gtkuncpy(txt2, name, PATHTXT) : __("Untitled"));

	gtk_window_set_title(GTK_WINDOW(main_window), txt);
}

void notify_changed()		// Image/palette has just changed - update vars as needed
{
	mem_tempfiles = NULL;
	if (!mem_changed)
	{
		mem_changed = TRUE;
		update_titlebar();
	}
}

/* Image has just been unchanged: saved to file, or loaded if "filename" is NULL */
void notify_unchanged(char *filename)
{
	if (mem_changed)
	{
		if (filename) mem_file_modified(filename);
		mem_changed = FALSE;
		update_titlebar();
	}
}


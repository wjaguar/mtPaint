/*	mainwindow.c
	Copyright (C) 2004-2016 Mark Tyler and Dmitry Groshev

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
#include "vcode.h"
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


typedef struct {
	int idx_c, nidx_c, cnt_c;
	int cline_d, settings_d, layers_d;
	int impmode;
	int *strs_c;
	void **drop, **clip;
	void **clipboard;
	void **dockpage1;
	void **keyslot;
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
	{ "tga565",		&tga_565,		FALSE },
	{ "tgaDefdir",		&tga_defdir,		FALSE },
	{ "disableTransparency", &opaque_view,		FALSE },
	{ "smudgeOpacity",	&smudge_mode,		FALSE },
	{ "showMenuIcons",	&show_menu_icons,	FALSE },
	{ "showTileGrid",	&show_tile_grid,	FALSE },
	{ "applyICC",		&apply_icc,		FALSE },
	{ "scrollwheelZOOM",	&scroll_zoom,		FALSE },
	{ "cursorZoom",		&cursor_zoom,		FALSE },
	{ "layerOverlay",	&layer_overlay,		FALSE },
	{ "paintGamma",		&paint_gamma,		FALSE },
	{ "tablet_use_size",	tablet_tool_use + 0,	FALSE },
	{ "tablet_use_flow",	tablet_tool_use + 1,	FALSE },
	{ "tablet_use_opacity",	tablet_tool_use + 2,	FALSE },
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
	{ "undoableLoad",	&undo_load,		TRUE  },
	{ "tiffPredictor",	&tiff_predictor,	TRUE  },
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
	{ "ftSetDPI",		&ft_setdpi,		TRUE  },
#endif
	{ "fontSetDPI",		&font_setdpi,		FALSE },
	{ NULL,			NULL }
};

static inilist ini_int[] = {
	{ "jpegQuality",	&jpeg_quality,		85  },
	{ "pngCompression",	&png_compression,	9   },
	{ "tgaRLE",		&tga_RLE,		0   },
	{ "jpeg2000Rate",	&jp2_rate,		1   },
	{ "lzmaPreset",		&lzma_preset,		9   },
	{ "silence_limit",	&silence_limit,		18  },
	{ "gradientOpacity",	&grad_opacity,		128 },
	{ "gridMin",		&mem_grid_min,		8   },
	{ "undoMBlimit",	&mem_undo_limit,	0   },
	{ "undoCommon",		&mem_undo_common,	25  },
	{ "maxThreads",		&maxthreads,		0   },
	{ "kpixThreads",	&kpix_threads,		256 },
	{ "backgroundGrey",	&mem_background,	180 },
	{ "pixelNudge",		&mem_nudge,		8   },
	{ "recentFiles",	&recent_files,		10  },
	{ "lastspalType",	&spal_mode,		2   },
	{ "posterizeMode",	&def_bcsp.pmode,	0   },
	{ "panSize",		&max_pan,		128 },
	{ "undoDepth",		&mem_undo_depth,	DEF_UNDO },
	{ "tileWidth",		&tgrid_dx,		32  },
	{ "tileHeight",		&tgrid_dy,		32  },
	{ "gridRGB",		grid_rgb + GRID_NORMAL,	RGB_2_INT( 50,  50,  50) },
	{ "gridBorder",		grid_rgb + GRID_BORDER,	RGB_2_INT(  0, 219,   0) },
	{ "gridTrans",		grid_rgb + GRID_TRANS,	RGB_2_INT(  0, 109, 109) },
	{ "gridTile",		grid_rgb + GRID_TILE,	RGB_2_INT(170, 170, 170) },
	{ "gridSegment",	grid_rgb + GRID_SEGMENT,RGB_2_INT(219, 219,   0) },
	{ "palAB",		&mem_pal_ab_c,		RGB_2_INT(53,   53, 162) },
	{ "palIndex",		&mem_pal_id_c,		RGB_2_INT(200, 200, 200) },
	{ "tablet_value_size",	tablet_tool_factor + 0,	MAX_TF },
	{ "tablet_value_flow",	tablet_tool_factor + 1,	MAX_TF },
	{ "tablet_value_opacity", tablet_tool_factor + 2,MAX_TF },
	{ "fontBackground",	&font_bkg,		0   },
	{ "fontAngle",		&font_angle,		0   },
	{ "fontAlign",		&font_align,		0   },
	{ "fontDPI",		&font_dpi,		72  },
#ifdef U_FREETYPE
	{ "fontSizeBitmap",	&font_bmsize,		1   },
	{ "fontSize",		&font_size,		12  },
	{ "font_dirs",		&font_dirs,		0   },
#endif
	{ NULL,			NULL }
};


void **main_window_, **main_keys, **settings_dock, **layers_dock, **main_split,
	**drawing_canvas, **scrolledwindow_canvas,
	**menu_slots[TOTAL_MENU_IDS];

static void **dock_area, **dock_book, **main_menubar;

int	view_image_only, viewer_mode, drag_index, q_quit, cursor_tool;
int	show_menu_icons, paste_commit, scroll_zoom;
int	drag_index_vals[2], cursor_corner, use_gamma, view_vsplit;
int	files_passed, cmd_mode, tablet_working;
char **file_args, **script_cmds;

static int show_dock;
static int mouse_left_canvas;
static int cvxy[2];	// canvas window position

typedef struct {
	int mode;	// (tool_type + 1) if drawn, 0 if not
	int x, y, s;	// Top left corner and size
	int cx, cy;	// Clone perimeter offset
} perim_info;

static perim_info perim_state;	// Tool perimeter
static int perim_wx, perim_wy;	// Cursor position

#define perim_status	perim_state.mode
#define perim_x		perim_state.x
#define perim_y		perim_state.y
#define perim_s		perim_state.s
#define perim_cx	perim_state.cx
#define perim_cy	perim_state.cy

static void repaint_perim(rgbcontext *ctx);	// Redraw perimeter around mouse cursor
static void clear_perim(perim_info *p);		// Clear perimeter around mouse cursor
static void move_perim(int x, int y);		// Move perimeter to a new location

static void clear_perim_real(int x0, int y0, int s)
{
	int x1, y1, zoom = 1, scale = 1;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	x1 = margin_main_x + ((x0 + s - 1) * scale) / zoom + scale - 1;
	y1 = margin_main_y + ((y0 + s - 1) * scale) / zoom + scale - 1;
	x0 = margin_main_x + (x0 * scale) / zoom;
	y0 = margin_main_y + (y0 * scale) / zoom;

	repaint_canvas(x0, y0, 1, y1 - y0 + 1);
	repaint_canvas(x1, y0, 1, y1 - y0 + 1);
	repaint_canvas(x0 + 1, y0, x1 - x0 - 1, 1);
	repaint_canvas(x0 + 1, y1, x1 - x0 - 1, 1);
}

static void pressed_load_recent(int item)
{
	char txt[64];

	if ((layers_total ? check_layers_for_changes() :
		check_for_changes()) == 1) return;

	sprintf(txt, "file%i", item);
	do_a_load(inifile_get(txt, ""), undo_load);	// Load requested file
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

static multi_ext *script_rect(int *rect);
static void select_poly(multi_ext *mx);

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
		multi_ext *mx;
		int rxy[4];

		if ((mx = script_rect(rxy))) // Script wants a polygon
		{
			change_to_tool(TTB_POLY);
			select_poly(mx);
			free(mx);
			break;
		}
		clip(rxy, 0, 0, mem_width - 1, mem_height - 1, rxy);
		/* We are selecting an area, so block inside-out selections */
		if ((rxy[0] > rxy[2]) || (rxy[1] > rxy[3])) break;
		i |= UPD_SEL;
		copy4(marq_xy, rxy);
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
	if (mem_remove_unused_check() <= 0)
	{
		if (!script_cmds) alert_box(_("Error"),
			_("There were no unused colours to remove!"), NULL);
	}
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
		if (!script_cmds) alert_box(_("Error"),
			_("The palette does not contain 2 colours that have identical RGB values"), NULL);
		return;
	}
	mess = g_strdup_printf(__("The palette contains %i colours that have identical RGB values.  Do you really want to merge them into one index and realign the canvas?"), dups);
	if (script_cmds || (alert_box(_("Warning"), mess, _("Yes"), _("No"), NULL) == 1))
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

// System clipboard import

static clipform_dd clip_formats[] = {
	{ "application/x-mtpaint-pmm", (void *)(FT_PMM | FTM_EXTEND) },
	{ "application/x-mtpaint-clipboard", (void *)(FT_PNG | FTM_EXTEND) },
	{ "image/png", (void *)(FT_PNG) },
	{ "image/bmp", (void *)(FT_BMP) },
	{ "image/x-bmp", (void *)(FT_BMP) },
	{ "image/x-MS-bmp", (void *)(FT_BMP) },
#ifdef HAVE_PIXMAPS
	/* These two don't make sense without X */
	{ "PIXMAP", (void *)(FT_PIXMAP), sizeof(XID_type), 32 },
	{ "BITMAP", (void *)(FT_PIXMAP), sizeof(XID_type), 32 },
	/* !!! BITMAP requests are handled same as PIXMAP - because it is only
	 * done to appease buggy XPaint which requests both and crashes if
	 * receiving only one - WJ */
#endif
};
#define CLIP_TARGETS (sizeof(clip_formats) / sizeof(clip_formats[0]))

/* Seems it'll be better to prefer BMP when talking to the likes of GIMP -
 * they send PNGs really slowly (likely, compressed to the max); but not
 * everyone supports alpha in BMPs. */

static int clipboard_import_fn(main_dd *dt, void **wdata, int what, void **where,
	copy_ext *cdata)
{
	int form = (int)cdata->format->id;
	if ((dt->impmode == FS_PNG_LOAD) && undo_load) form |= FTM_UNDO;
	return (load_mem_image(cdata->data, cdata->len, dt->impmode, form) == 1);
}

int import_clipboard(int mode)
{
	main_dd *dt = GET_DDATA(main_window_);
	if (cmd_mode) return (FALSE); // !!! Needs active GTK+ to work
	dt->impmode = mode;
	return (cmd_checkv(dt->clipboard, CLIP_PROCESS));
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

static void clipboard_export_fn(main_dd *dt, void **wdata, int what, void **where,
	copy_ext *cdata)
{
	ls_settings settings;
	unsigned char *buf, *pp[2];
	int res, len, type;

	if (!cdata->format) return; // Someone else stole system clipboard
	if (!mem_clipboard) return; // Our own clipboard got emptied

	/* Prepare settings */
	setup_clip_save(&settings);
	settings.mode = FS_CLIPBOARD;
	settings.ftype = type = (int)cdata->format->id;
	settings.png_compression = 1; // Speed is of the essence

	res = save_mem_image(&buf, &len, &settings);
	if (res) return; // No luck creating in-memory image

	pp[1] = (pp[0] = buf) + len; 
	cmd_setv(where, pp, COPY_DATA);
	free(buf);
}

static int export_clipboard()
{
	main_dd *dt = GET_DDATA(main_window_);
	if (!mem_clipboard) return (FALSE);
	return (cmd_checkv(dt->clipboard, CLIP_OFFER));
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
		if (!script_cmds && paste_commit && (marq_status >= MARQUEE_PASTE))
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

static void toggle_view()
{
	view_image_only = !view_image_only;

	cmd_showhide(main_menubar, !view_image_only);
	if (view_image_only)
	{
		int i;

		for (i = TOOLBAR_MAIN; i < TOOLBAR_MAX; i++)
			if (toolbar_boxes[i]) cmd_showhide(toolbar_boxes[i], FALSE);
	}
	else toolbar_showhide(); // Switch toolbar/status/palette on if needed
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

/* Forward declaration */
static void mouse_event(mouse_ext *m, int mflag, int dx, int dy);

/* For "dual" mouse control */
static int unreal_move, lastdx, lastdy;

static void move_mouse(int dx, int dy, int button)
{
	static unsigned int bmasks[4] = { 0, _B1mask, _B2mask, _B3mask };
	mouse_ext m;
	int dxy[2], zoom = 1, scale = 1;

	if (!unreal_move) lastdx = lastdy = 0;
	if (!mem_img[CHN_IMAGE]) return;
	dx += lastdx; dy += lastdy;

	cmd_peekv(drawing_canvas, &m, sizeof(m), CANVAS_FIND_MOUSE);

	if (button) /* Clicks simulated without extra movements */
	{
		m.button = button;
		m.count = 1; // press
		mouse_event(&m, 1, dx, dy);
		m.state |= bmasks[button]; // Shows state _prior_ to event
		m.count = -1; // release
		mouse_event(&m, 1, dx, dy);
		return;
	}

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	if (zoom > 1) /* Fine control required */
	{
		lastdx = dx; lastdy = dy;
		mouse_event(&m, 1, dx, dy); // motion

		/* Nudge cursor when needed */
		if ((abs(lastdx) >= zoom) || (abs(lastdy) >= zoom))
		{
			lastdx -= (dxy[0] = lastdx * can_zoom) * zoom;
			lastdy -= (dxy[1] = lastdy * can_zoom) * zoom;
			unreal_move = 3;
			/* Event can be delayed or lost */
			cmd_setv(drawing_canvas, dxy, CANVAS_BMOVE_MOUSE);
		}
		else unreal_move = 2;
	}
	else /* Real mouse is precise enough */
	{
		unreal_move = 1;

		/* Simulate movement if failed to actually move mouse */
		dxy[0] = dx * scale;
		dxy[1] = dy * scale;
		cmd_setv(drawing_canvas, dxy, CANVAS_BMOVE_MOUSE);
		if (dxy[0] | dxy[1]) // failed
		{
			lastdx = dx; lastdy = dy;
			mouse_event(&m, 1, dx, dy); // motion
		}
	}
	/* !!! The code above doesn't even try to handle situation where
	 * autoscroll works but cursor warp doesn't. If one such ever arises,
	 * values returned from CANVAS_BMOVE_MOUSE will have to be used - WJ */
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

static void menu_action(void *dt, void **wdata, int what, void **where);

static void *keylist_code[] = {
	uMENUBAR(menu_action),
	uMENUITEM(_("Zoom in"), ACTMOD(ACT_ZOOM, 0)),
		SHORTCUT(plus, 0), SHORTCUT(KP_Add, 0),
	uMENUITEM(_("Zoom out"), ACTMOD(ACT_ZOOM, -1)),
		SHORTCUT(minus, 0), SHORTCUT(KP_Subtract, 0),
	uMENUITEM(_("10% zoom"), ACTMOD(ACT_ZOOM, -10)),
		SHORTCUT(1, 0), SHORTCUT(KP_1, 0),
	uMENUITEM(_("25% zoom"), ACTMOD(ACT_ZOOM, -4)),
		SHORTCUT(2, 0), SHORTCUT(KP_2, 0),
	uMENUITEM(_("50% zoom"), ACTMOD(ACT_ZOOM, -2)),
		SHORTCUT(3, 0), SHORTCUT(KP_3, 0),
	uMENUITEM(_("100% zoom"), ACTMOD(ACT_ZOOM, 1)),
		SHORTCUT(4, 0), SHORTCUT(KP_4, 0),
	uMENUITEM(_("400% zoom"), ACTMOD(ACT_ZOOM, 4)),
		SHORTCUT(5, 0), SHORTCUT(KP_5, 0),
	uMENUITEM(_("600% zoom"), ACTMOD(ACT_ZOOM, 6)),
		SHORTCUT0,
	uMENUITEM(_("800% zoom"), ACTMOD(ACT_ZOOM, 8)),
		SHORTCUT(6, 0), SHORTCUT(KP_6, 0),
	uMENUITEM(_("1000% zoom"), ACTMOD(ACT_ZOOM, 10)),
		SHORTCUT0,
	uMENUITEM(_("1200% zoom"), ACTMOD(ACT_ZOOM, 12)),
		SHORTCUT(7, 0), SHORTCUT(KP_7, 0),
	uMENUITEM(_("1600% zoom"), ACTMOD(ACT_ZOOM, 16)),
		SHORTCUT(8, 0), SHORTCUT(KP_8, 0),
	uMENUITEM(_("2000% zoom"), ACTMOD(ACT_ZOOM, 20)),
		SHORTCUT(9, 0), SHORTCUT(KP_9, 0),
	uMENUITEM(_("4000% zoom"), ACTMOD(ACT_ZOOM, 40)),
		SHORTCUT0,
	uMENUITEM(_("8000% zoom"), ACTMOD(ACT_ZOOM, 80)),
		SHORTCUT0,
	uMENUITEM(_("10% opacity"), ACTMOD(ACT_OPAC, 1)),
		SHORTCUT(1, C), SHORTCUT(KP_1, C),
	uMENUITEM(_("20% opacity"), ACTMOD(ACT_OPAC, 2)),
		SHORTCUT(2, C), SHORTCUT(KP_2, C),
	uMENUITEM(_("30% opacity"), ACTMOD(ACT_OPAC, 3)),
		SHORTCUT(3, C), SHORTCUT(KP_3, C),
	uMENUITEM(_("40% opacity"), ACTMOD(ACT_OPAC, 4)),
		SHORTCUT(4, C), SHORTCUT(KP_4, C),
	uMENUITEM(_("50% opacity"), ACTMOD(ACT_OPAC, 5)),
		SHORTCUT(5, C), SHORTCUT(KP_5, C),
	uMENUITEM(_("60% opacity"), ACTMOD(ACT_OPAC, 6)),
		SHORTCUT(6, C), SHORTCUT(KP_6, C),
	uMENUITEM(_("70% opacity"), ACTMOD(ACT_OPAC, 7)),
		SHORTCUT(7, C), SHORTCUT(KP_7, C),
	uMENUITEM(_("80% opacity"), ACTMOD(ACT_OPAC, 8)),
		SHORTCUT(8, C), SHORTCUT(KP_8, C),
	uMENUITEM(_("90% opacity"), ACTMOD(ACT_OPAC, 9)),
		SHORTCUT(9, C), SHORTCUT(KP_9, C),
	uMENUITEM(_("100% opacity"), ACTMOD(ACT_OPAC, 10)),
		SHORTCUT(0, C), SHORTCUT(KP_0, C),
	uMENUITEM(_("Increase opacity"), ACTMOD(ACT_OPAC, 0)),
		SHORTCUT(plus, C), SHORTCUT(KP_Add, C),
	uMENUITEM(_("Decrease opacity"), ACTMOD(ACT_OPAC, -1)),
		SHORTCUT(minus, C), SHORTCUT(KP_Subtract, C),
	uMENUITEM(_("Draw open arrow head"), ACTMOD(ACT_ARROW, 2)),
		SHORTCUT(a, 0),
	uMENUITEM(_("Draw closed arrow head"), ACTMOD(ACT_ARROW, 3)),
		SHORTCUT(s, 0),
	uMENUITEM(_("Previous colour A"), ACTMOD(ACT_A, -1)),
		SHORTCUT(bracketleft, 0),
	uMENUITEM(_("Next colour A"), ACTMOD(ACT_A, 1)),
		SHORTCUT(bracketright, 0),
	uMENUITEM(_("Previous colour B"), ACTMOD(ACT_B, -1)),
		SHORTCUT(bracketleft, S), SHORTCUT(braceleft, S),
	uMENUITEM(_("Next colour B"), ACTMOD(ACT_B, 1)),
		SHORTCUT(bracketright, S), SHORTCUT(braceright, S),
	uMENUITEM(_("View window - Zoom in"), ACTMOD(ACT_VWZOOM, 0)),
		SHORTCUT(plus, S), SHORTCUT(KP_Add, S),
	uMENUITEM(_("View window - Zoom out"), ACTMOD(ACT_VWZOOM, -1)),
		SHORTCUT(minus, S), SHORTCUT(KP_Subtract, S),
///	FIXED KEYS
	uMENUITEM(NULL, ACTMOD(ACT_QUIT, 0)),
		SHORTCUT(q, 0), SHORTCUT(q, S), SHORTCUT(q, A),
		SHORTCUT(q, CS), SHORTCUT(q, CA),
		SHORTCUT(q, SA), SHORTCUT(q, CSA),
	uMENUITEM(NULL, ACTMOD(ACT_SEL_MOVE, 5)),
		SHORTCUT(Left, S), SHORTCUT(KP_Left, S),
	uMENUITEM(NULL, ACTMOD(ACT_SEL_MOVE, 7)),
		SHORTCUT(Right, S), SHORTCUT(KP_Right, S),
	uMENUITEM(NULL, ACTMOD(ACT_SEL_MOVE, 3)),
		SHORTCUT(Down, S), SHORTCUT(KP_Down, S),
	uMENUITEM(NULL, ACTMOD(ACT_SEL_MOVE, 9)),
		SHORTCUT(Up, S), SHORTCUT(KP_Up, S),
	uMENUITEM(NULL, ACTMOD(ACT_SEL_MOVE, 4)),
		SHORTCUT(Left, 0), SHORTCUT(KP_Left, 0),
	uMENUITEM(NULL, ACTMOD(ACT_SEL_MOVE, 6)),
		SHORTCUT(Right, 0), SHORTCUT(KP_Right, 0),
	uMENUITEM(NULL, ACTMOD(ACT_SEL_MOVE, 2)),
		SHORTCUT(Down, 0), SHORTCUT(KP_Down, 0),
	uMENUITEM(NULL, ACTMOD(ACT_SEL_MOVE, 8)),
		SHORTCUT(Up, 0), SHORTCUT(KP_Up, 0),
	uMENUITEM(NULL, ACTMOD(ACT_LR_MOVE, 5)),
		SHORTCUT(Left, CS), SHORTCUT(KP_Left, CS),
	uMENUITEM(NULL, ACTMOD(ACT_LR_MOVE, 7)),
		SHORTCUT(Right, CS), SHORTCUT(KP_Right, CS),
	uMENUITEM(NULL, ACTMOD(ACT_LR_MOVE, 3)),
		SHORTCUT(Down, CS), SHORTCUT(KP_Down, CS),
	uMENUITEM(NULL, ACTMOD(ACT_LR_MOVE, 9)),
		SHORTCUT(Up, CS), SHORTCUT(KP_Up, CS),
	uMENUITEM(NULL, ACTMOD(ACT_LR_MOVE, 4)),
		SHORTCUT(Left, C), SHORTCUT(KP_Left, C),
	uMENUITEM(NULL, ACTMOD(ACT_LR_MOVE, 6)),
		SHORTCUT(Right, C), SHORTCUT(KP_Right, C),
	uMENUITEM(NULL, ACTMOD(ACT_LR_MOVE, 2)),
		SHORTCUT(Down, C), SHORTCUT(KP_Down, C),
	uMENUITEM(NULL, ACTMOD(ACT_LR_MOVE, 8)),
		SHORTCUT(Up, C), SHORTCUT(KP_Up, C),
	uMENUITEM(NULL, ACTMOD(ACT_ESC, 0)),
		SHORTCUT(Escape, 0), SHORTCUT(Escape, A),
	uMENUITEM(NULL, ACTMOD(ACT_COMMIT, 0)),
		SHORTCUT(Return, 0), SHORTCUT(Return, C),
		SHORTCUT(Return, A), SHORTCUT(Return, CA),
		SHORTCUT(KP_Enter, 0), SHORTCUT(KP_Enter, C),
		SHORTCUT(KP_Enter, A), SHORTCUT(KP_Enter, CA),
	uMENUITEM(NULL, ACTMOD(ACT_COMMIT, 1)),
		SHORTCUT(Return, S), SHORTCUT(Return, CS),
		SHORTCUT(Return, SA), SHORTCUT(Return, CSA),
		SHORTCUT(KP_Enter, S), SHORTCUT(KP_Enter, CS),
		SHORTCUT(KP_Enter, SA), SHORTCUT(KP_Enter, CSA),
	uMENUITEM(NULL, ACTMOD(ACT_RCLICK, 0)),
		SHORTCUT(BackSpace, 0), SHORTCUT(BackSpace, C),
		SHORTCUT(BackSpace, S), SHORTCUT(BackSpace, A),
		SHORTCUT(BackSpace, CS), SHORTCUT(BackSpace, CA),
		SHORTCUT(BackSpace, SA), SHORTCUT(BackSpace, CSA),
	RET
};

int key_action(key_ext *key, int toggle)
{
	main_dd *dt = GET_DDATA(main_window_);
	void *v, **slot;
	int act_m;

	cmd_setv(main_keys, key, KEYMAP_KEY);
	slot = dt->keyslot;
	// Leave unmapped key to be handled elsewhere
	if (!slot) act_m = 0;
	// Do nothing if slot is insensitive
	else if (!cmd_checkv(slot, SLOT_SENSITIVE)) act_m = ACTMOD_DUMMY;
	// Activate toggleable widget if allowed
	else if (toggle && (v = slot_data(slot, dt)))
	{
		cmd_set(slot, cmd_checkv(slot, SLOT_RADIO) || !*(int *)v);
		act_m = ACTMOD_DUMMY;
	}
	else act_m = TOOL_ID(slot);
	return (act_m);
}

int dock_focused()
{
	return (cmd_checkv(dock_book, SLOT_FOCUSED));
}

static int handle_keypress(main_dd *dt, void **wdata, int what, void **where,
	key_ext *keydata)
{
	int act_m = key_action(keydata, TRUE);

	if (!act_m) return (FALSE);

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
	do_tool_action(TC_LINE_ARROW, line_x2, line_y2, MAX_PRESSURE);
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
	lr_update_area(layer_selected, minx, miny, w, h);
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

	/* Initialize undo memory space */
	if (mem_undo_limit <= 0)
	{
		unsigned mem = sys_mem_size();
		/* Limit usable space to 2 Gb on 32-bit systems */
		if ((sizeof(void *) <= 4) && (mem > 2048)) mem = 2048;
		/* Take 1/4 of memory space, rounded up to nearest 32 Mb */
		mem_undo_limit = ((mem / 4) + 31) & ~31;
		/* But no less than 32 Mb */
		if (!mem_undo_limit) mem_undo_limit = 32;
	}

#ifdef U_TIFF
	/* Load TIFF types */
	{
		int i, tr, ti, tb;
		tr = inifile_get_gint32("tiffTypeRGB", 1 /* COMPRESSION_NONE */);
		ti = inifile_get_gint32("tiffTypeI", 1 /* COMPRESSION_NONE */);
		tb = inifile_get_gint32("tiffTypeBW", 1 /* COMPRESSION_NONE */);
		for (i = 0; tiff_formats[i].name; i++)
		{
			if (tiff_formats[i].id == tr) tiff_rtype = i;
			if (tiff_formats[i].id == ti) tiff_itype = i;
			if (tiff_formats[i].id == tb) tiff_btype = i;
		}
	}
#endif
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

	/* Store listed settings */
	for (ilp = ini_bool; ilp->name; ilp++)
		inifile_set_gboolean(ilp->name, *(ilp->var));
	for (ilp = ini_int; ilp->name; ilp++)
		inifile_set_gint32(ilp->name, *(ilp->var));

#ifdef U_TIFF
	/* Store TIFF types */
	inifile_set_gint32("tiffTypeRGB", tiff_formats[tiff_rtype].id);
	inifile_set_gint32("tiffTypeI", tiff_formats[tiff_itype].id);
	inifile_set_gint32("tiffTypeBW", tiff_formats[tiff_btype].id);
#endif

	if (files_passed <= 1) inifile_set_gboolean("showDock", show_dock);
	toggle_dock(FALSE); // To remember dock size

	// Get rid of extra windows + remember positions
	delete_layers_window();

	// Remember the toolbar settings
	toolbar_settings_exit(NULL, NULL);

	run_destroy(wdata);
}

static void canvas_scroll(main_dd *dt, void **wdata, int what, void **where,
	scroll_ext *scroll)
{
	if (scroll_zoom)
	{
		action_dispatch(origin_slot(where) == drawing_canvas ?
			ACT_ZOOM : ACT_VWZOOM, scroll->yscroll > 0 ? -1 : 0,
			FALSE, TRUE);
		scroll->xscroll = scroll->yscroll = 0;
	}
	else if (scroll->state & _Cmask) /* Convert up-down into left-right */
	{
		scroll->xscroll = scroll->yscroll;
		scroll->yscroll = 0;
	}
}


void grad_stroke(int x, int y)
{
	grad_info *grad = gradient + mem_channel;
	double d, stroke;
	int i, j;

	/* No usable gradient */
	if (grad->wmode == GRAD_MODE_NONE) return;

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
}

#define GF_UPDATE 1
#define GF_REDRAW 2
#define GF_DRAW   4

static void do_grad_action(int cmd, int x, int y)
{
	grad_info *grad = gradient + mem_channel;
	int old[4], prev = grad->status, upd = 0;

	copy4(old, grad->xy);
	switch (cmd)
	{
	case TC_GRAD_START:
		grad->xy[0] = grad->xy[2] = x;
		grad->xy[1] = grad->xy[3] = y;
		grad->status = GRAD_END;
		upd = GF_UPDATE | GF_DRAW;
		break;
	case TC_GRAD_PICK0:
		grad->status = GRAD_START;
		// Fallthrough
	case TC_GRAD_SET0:
		grad->xy[0] = x;
		grad->xy[1] = y;
		if (cmd == TC_GRAD_SET0) prev = grad->status = GRAD_DONE;
		upd = GF_UPDATE | GF_REDRAW;
		break;
	case TC_GRAD_PICK1:
		grad->status = GRAD_END;
		// Fallthrough
	case TC_GRAD_SET1:
		grad->xy[2] = x;
		grad->xy[3] = y;
		if (cmd == TC_GRAD_SET1) prev = grad->status = GRAD_DONE;
		upd = GF_UPDATE | GF_REDRAW;
		break;
	case TC_GRAD_DRAG0: case TC_GRAD_DRAG1:
	{
		int *xy = grad->xy + (cmd == TC_GRAD_DRAG0 ? 0 : 2);
		if ((xy[0] != x) || (xy[1] != y))
		{
			xy[0] = x;
			xy[1] = y;
			upd = GF_UPDATE | GF_REDRAW;
		}
		break;
	}
	case TC_GRAD_CLEAR:
		grad->status = GRAD_NONE;
		upd = GF_UPDATE | GF_DRAW;
		break;
	}

	if (upd & GF_UPDATE) grad_update(grad);
	if (upd & (GF_REDRAW | GF_DRAW))
	{
		if ((prev == GRAD_DONE) && grad_opacity)
			cmd_repaint(drawing_canvas);
		else repaint_grad(upd & GF_REDRAW ? old : NULL);
	}
}

static int grad_action(int count, int button, int x, int y)
{
	grad_info *grad = gradient + mem_channel;
	int i, j, cmd = TC_NONE;

	/* Left click sets points and picks them up again */
	if ((count == 1) && (button == 1))
	{
		/* Start anew */
		if (grad->status == GRAD_NONE) cmd = TC_GRAD_START;
		/* Place starting point */
		else if (grad->status == GRAD_START) cmd = TC_GRAD_SET0;
		/* Place end point */
		else if (grad->status == GRAD_END) cmd = TC_GRAD_SET1;
		/* Pick up nearest end */
		else if (grad->status == GRAD_DONE)
		{
			i = (x - grad->xy[0]) * (x - grad->xy[0]) +
				(y - grad->xy[1]) * (y - grad->xy[1]);
			j = (x - grad->xy[2]) * (x - grad->xy[2]) +
				(y - grad->xy[3]) * (y - grad->xy[3]);
			cmd = i < j ? TC_GRAD_PICK0 : TC_GRAD_PICK1;
		}
	}

	/* Everything but left click is irrelevant when no gradient */
	else if (grad->status == GRAD_NONE);

	/* Right click deletes the gradient */
	else if (count == 1) cmd = TC_GRAD_CLEAR; /* button != 1 */

	/* Motion is irrelevant with gradient in place */
	else if (grad->status == GRAD_DONE);

	/* Motion drags points around */
	else if (!count)
		cmd = grad->status == GRAD_START ? TC_GRAD_DRAG0 : TC_GRAD_DRAG1;

	return (cmd);
}

/* Pick color A/B from canvas, or use one given */
static int pick_color(int ox, int oy, int ab, int area[4], int pixel)
{
	int rgba = RGBA_mode && (mem_channel == CHN_IMAGE);
	int upd = 0, alpha = 255; // opaque

	if (pixel < 0) // If no pixel value is given
	{
		/* Default alpha */
		alpha = channel_col_[ab][CHN_ALPHA];
		/* Average brush or selection area on double click */
		while (area)
		{
			int rect[4];

			/* Clip to image */
			copy4(rect, area);
			rect[2] += rect[0];
			rect[3] += rect[1];
			if (!clip(rect, 0, 0, mem_width, mem_height, rect)) break;
			/* Average utility channel */
			if (mem_channel != CHN_IMAGE) pixel = average_channel(
				mem_img[mem_channel], mem_width, rect);
			if (MEM_BPP != 3) break;
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
	return (upd);
}

static void tool_done()
{
	tint_mode[2] = 0; // Paranoia
	clone_status &= CLONE_ABS;
	pen_down = 0;
	if (col_reverse)
	{
		col_reverse = FALSE;
		mem_swap_cols(FALSE);
	}

	mem_undo_prepare();
}

static int get_bkg(int xc, int yc, int dclick);

static inline int xmmod(int x, int y)
{
	return (x - (x % y));
}

/* Mouse event from button/motion on the canvas */
static void mouse_event(mouse_ext *m, int mflag, int dx, int dy)
{
	static int tool_fix, tool_fixv;	// Fixate on axis
	int new_cursor;
	int i, x0, y0, ox, oy;
	int x, y, cmd;
	int zoom = 1, scale = 1;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	x0 = floor_div((m->x - margin_main_x) * zoom, scale) + dx;
	y0 = floor_div((m->y - margin_main_y) * zoom, scale) + dy;

	ox = x0 < 0 ? 0 : x0 >= mem_width ? mem_width - 1 : x0;
	oy = y0 < 0 ? 0 : y0 >= mem_height ? mem_height - 1 : y0;

	if (!mflag) /* Coordinate fixation */
	{
		if (!(m->state & _Smask)) tool_fix = 0;
		else if (!(m->state & _Cmask)) /* Shift */
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
	else if (!m->count) tool_fix = 0;

	if (!tool_fix);
	/* For rectangular selection it makes sense to fix its width/height */
	else if ((tool_type == TOOL_SELECT) && (m->button == 1) &&
		((marq_status == MARQUEE_DONE) || (marq_status == MARQUEE_SELECTING)))
	{
		if (tool_fix == 1) x0 = marq_x2;
		else y0 = marq_y2;
	}
	else if (tool_fix == 1) x0 = tool_fixv;
	else /* if (tool_fix == 2) */ y0 = tool_fixv;

	x = x0; y = y0;
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

	if (m->count < 0)
	{
		cmd = TC_NONE;
		if ((tool_type == TOOL_LINE) && (m->button == 1) &&
			(line_status == LINE_START)) cmd = TC_LINE_NEXT;

		if (((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON)) &&
			(m->button == 1) && ((marq_status == MARQUEE_SELECTING) ||
			(marq_status == MARQUEE_PASTE_DRAG))) cmd = TC_SEL_STOP;

		// Finish off dragged polygon selection
		if ((tool_type == TOOL_POLYGON) && (poly_status == POLY_DRAGGING))
			cmd = TC_POLY_CLOSE;

		if (cmd != TC_NONE) do_tool_action(cmd, x, y, 0);

		tool_done();
		update_menus();

		return;
	}

	/* ****** Common click/motion handling code ****** */

	if ((m->state & _CSmask) == _Cmask)
	{
		/* Delete point from polygon */
		if ((m->button == 3) && (tool_type == TOOL_POLYGON))
		{
			if (m->count == 1) do_tool_action(TC_POLY_DEL, x, y, 0);
		}
		else if (m->button == 2) /* Auto-dither */
		{
			if ((mem_channel == CHN_IMAGE) && (mem_img_bpp == 3))
				pressed_dither_A();
		}
		/* Snap clone source to point */
		else if ((m->button == 1) && (tool_type == TOOL_CLONE))
		{
			clone_x = x;
			clone_y = y;
			clone_dx = clone_dy = 0;
			move_perim(x, y);
		}
		/* Set colour A/B */
		else if ((m->button == 1) || (m->button == 3))
		{
			int pix, rect[4];

			/* Pick color from tracing image when possible */
			pix = get_bkg(m->x + dx * scale, m->y + dy * scale,
				m->count == 2);

			/* Double click averages an area */
			rect[2] = 0;
			if (pix >= 0); // Got it from tracing image
			else if (m->count != 2); // No double click
			// Have brush square
			else if (!NO_PERIM(tool_type))
			{
				int ts2 = tool_size >> 1;
				rect[0] = ox - ts2; rect[1] = oy - ts2;
				rect[2] = rect[3] = tool_size;
			}
			// Have selection marquee
			else if ((marq_status > MARQUEE_NONE) &&
				(marq_status < MARQUEE_PASTE)) marquee_at(rect);

			if (pick_color(ox, oy,
				m->button == 3, // A for left, B for right
				rect[2] ? rect : NULL, // Area to average
				pix)) update_stuff(UPD_CAB);
		}
	}

	else if ((m->button == 2) || ((m->button == 3) && (m->state & _Smask)))
		set_zoom_centre(ox, oy);

	else if (tool_type == TOOL_GRADIENT)
	{
		cmd = grad_action(m->count, m->button, x0, y0);
		if (cmd != TC_NONE) do_grad_action(cmd, x0, y0);
	}

	/* Pure moves are handled elsewhere */
	else if (m->button)
	{
		cmd = tool_action(m->count, m->button, x, y);
		if (cmd != TC_NONE)
			do_tool_action(cmd | TCF_ONCE, x, y, m->pressure);
	}


	/* ****** Now to mouse-move-specific part ****** */

	if (!m->count)
	{
		if ((poly_status == POLY_SELECTING) && !m->button)
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

		if ((tool_type == TOOL_CLONE) && !(clone_status & CLONE_DRAG))
		{
			if ((clone_status == CLONE_ABS) ||
				(clone_status == (CLONE_REL | CLONE_TRACK)))
			{
				/* Source stays put */
				clone_dx = clone_x - x;
				clone_dy = clone_y - y;
			}
			else
			{
				/* Source follows cursor */
				clone_x = x + clone_dx;
				clone_y = y + clone_dy;
			}
			clone_status = (!m->button && (m->state & _Cmask) ?
				CLONE_TRACK : 0) | (clone_status & CLONE_ABS);
		}

		move_perim(x, y);

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

	update_sel_bar(FALSE);
}

static int canvas_mouse(main_dd *dt, void **wdata, int what, void **where,
	mouse_ext *mouse)
{
	int rm = unreal_move;

	mouse_left_canvas = FALSE;

	/* Steal focus from dock window */
	if ((mouse->count > 0) && dock_focused())
		cmd_setv(main_window_, NULL, WINDOW_FOCUS);

	/* Skip synthetic mouse moves */
	if (!mouse->count)
	{
		if (unreal_move == 3)
		{
			unreal_move = 2;
			return (TRUE);
		}
		unreal_move = 0;
	}

	/* Do nothing if no image */
	if ((mouse->count >= 0) && !mem_img[CHN_IMAGE]) return (TRUE);

	/* If cursor got warped, will have another movement event to handle */
	if (!mouse->count && mouse->button && (tool_type == TOOL_SELECT) &&
		cmd_checkv(where, MOUSE_BOUND)) return (TRUE);

	mouse_event(mouse, rm & 1, 0, 0);

	return (mouse->count >= 0);
}

// Mouse enters/leaves the canvas
static void canvas_enter_leave(main_dd *dt, void **wdata, int what, void **where,
	void *enter)
{
	if (enter)
	{
		mouse_left_canvas = FALSE;
		return;
	}
	/* If leaving canvas */
	/* Only do this if we have an image */
	if (!mem_img[CHN_IMAGE]) return;
	mouse_left_canvas = TRUE;
	if (status_on[STATUS_CURSORXY])
		cmd_setv(label_bar[STATUS_CURSORXY], "", LABEL_VALUE);
	if (status_on[STATUS_PIXELRGB])
		cmd_setv(label_bar[STATUS_PIXELRGB], "", LABEL_VALUE);
	if (perim_status > 0) clear_perim(&perim_state);

	clone_status &= ~CLONE_TRACK; // No tracking w/o perimeter

	if (tool_type == TOOL_GRADIENT)
	{ 
		/* Let leave hide the dragged line */
		grad_info *grad = gradient + mem_channel;
		if ((grad->status == GRAD_START) || (grad->status == GRAD_END))
			repaint_grad(NULL);
	}

	if (((tool_type == TOOL_POLYGON) && (poly_status == POLY_SELECTING)) ||
		((tool_type == TOOL_LINE) && (line_status == LINE_LINE)))
		repaint_line(NULL);
}

static int render_background(unsigned char *rgb, int x0, int y0, int wid, int hgt, int fwid)
{
	int i, j, k, scale, dx, dy, step, ii, jj, ii0, px, py;
	int xwid = 0, xhgt = 0, wid3 = wid * 3;

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (!chequers_optimize) step = 8;
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

	return (!chequers_optimize); // Request async_bk
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

static int render_bkg(rgbcontext *ctx)
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
		ctx->xy)) return (FALSE);

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

	return (scale > 1); // Request async_bk
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

void setup_row(renderstate *r, int x0, int width, int zoom, int scale, int mw,
	int xpm, int opac, int bpp, png_color *pal)
{
	renderstate rr;

	/* Horizontal zoom */
	rr.zoom = zoom;
	rr.scale = scale;
	x0 %= rr.scale;
	if (x0 < 0) x0 += rr.scale;

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
	rr.cmask = 0;

	*r = rr;
}

void render_row(renderstate *r, unsigned char *rgb, chanlist base_img,
	int x, int y, chanlist xtra_img)
{
	renderstate rr = *r;
	int alpha_blend = !overlay_alpha;
	unsigned char *src = NULL, *dest, *alpha = NULL, px, beta = 255;
	int i, j, k, ii, ds = rr.zoom * 3, da = 0;
	int w_bpp = rr.bpp, w_xpm = rr.xpm;

	if (xtra_img)
	{
		src = xtra_img[CHN_IMAGE];
		alpha = xtra_img[CHN_ALPHA];
	}
	if (rr.cmask & CMASK_ALPHA) alpha = &beta; /* Ignore alpha if disabled */
	if (!src) src = base_img[CHN_IMAGE] + (rr.mw * y + x) * rr.bpp;
	if (!alpha) alpha = base_img[CHN_ALPHA] ? base_img[CHN_ALPHA] +
		rr.mw * y + x : &beta;
	if (alpha != &beta) da = rr.zoom;
	dest = rgb;
	ii = rr.dx;

	/* Substitute non-transparent "image overlay" colour */
	if (rr.cmask & CMASK_IMAGE)
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

void overlay_row(renderstate *r, unsigned char *rgb, chanlist base_img,
	int x, int y, chanlist xtra_img)
{
	renderstate rr = *r;
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
	k = rr.cmask & CMASK_IMAGE ? 256 : 256 - channel_opacity[CHN_IMAGE] -
		(channel_opacity[CHN_IMAGE] >> 7);
	opA = alpha && overlay_alpha && !(rr.cmask & CMASK_ALPHA) ?
		channel_opacity[CHN_ALPHA] : 0;
	opS = sel && !(rr.cmask & CMASK_SEL) ? channel_opacity[CHN_SEL] : 0;
	opM = mask && !(rr.cmask & CMASK_MASK) ? channel_opacity[CHN_MASK] : 0;

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
void overlay_preview(renderstate *r, unsigned char *rgb, unsigned char *map,
	int col, int opacity)
{
	renderstate rr = *r;
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
	int rgb_s;		// For when need RGB with 1bpp channel
	int mask_s;		// For most everything
	int overlay_s;		// For extra overlay
	int channel_s;		// For composited channel
	int alpha_s;		// For composited alpha
	int n_channel_s;	// For generated/transformed channel
	int n_alpha_s;		// For generated alpha
	int n_opacity_s;	// For generated opacity
	unsigned char *rgb,
		*mask,
		*overlay,
		*channel,
		*alpha,
		*n_channel,
		*n_alpha,
		*n_opacity;	// Pointers to same
} render_mem_req;

typedef struct {
	unsigned char *wmask, *gmask, *walpha, *galpha;
	unsigned char *wimg, *gimg, *rgb;
	int opac, len, bpp;
} grad_render_state;

static int grad_render_req(render_mem_req *mr, int len)
{
	int bpp;

// !!! Only the "slow path" for now
	if (gradient[mem_channel].status != GRAD_DONE) return (FALSE);

	bpp = MEM_BPP;
	mr->mask_s = mr->n_opacity_s = len;
	mr->channel_s = mr->n_channel_s = len * bpp;
	if ((mem_channel == CHN_IMAGE) && RGBA_mode && mem_img[CHN_ALPHA])
		mr->alpha_s = mr->n_alpha_s = len;
	if (IS_INDEXED && (grad_opacity < 255)) mr->rgb_s = len * 3;
	return (TRUE);
}

static void init_grad_render(render_mem_req *mr, grad_render_state *g, int len,
	chanlist tlist)
{
	memset(g, 0, sizeof(grad_render_state));

	g->wmask = mr->mask;		/* Mask */
	g->gmask = mr->n_opacity;	/* Gradient opacity */
	g->gimg = mr->n_channel;	/* Gradient image */
	g->wimg = mr->channel;		/* Resulting image */
	g->galpha = mr->n_alpha;	/* Gradient alpha */
	g->walpha = mr->alpha;		/* Resulting alpha */
	g->rgb = mr->rgb;		/* Indexed to RGB */

	tlist[CHN_ALPHA] = g->walpha;
	tlist[mem_channel] = g->wimg;
	if (g->rgb) tlist[CHN_IMAGE] = g->rgb;
	g->opac = IS_INDEXED ? 0 : grad_opacity;
	g->len = len;
	g->bpp = MEM_BPP;
}

static void grad_render(int start, int step, int cnt, int x, int y,
	unsigned char *mask0, grad_render_state *g)
{
	int l = mem_width * y + x, li = l * mem_img_bpp;

	prep_mask(start, step, cnt, g->wmask, mask0, mem_img[CHN_IMAGE] + li);

	grad_pixels(start, step, cnt, x, y, g->wmask, g->gmask, g->gimg, g->galpha);

	if (g->walpha) memcpy(g->walpha, mem_img[CHN_ALPHA] + l, g->len);
	process_mask(start, step, cnt, g->wmask, g->walpha, mem_img[CHN_ALPHA] + l,
		g->galpha, g->gmask, g->opac, channel_dis[CHN_ALPHA]);

	memcpy(g->wimg, mem_img[mem_channel] + l * g->bpp, g->len * g->bpp);
	process_img(start, step, cnt, g->wmask, g->wimg, g->wimg, g->gimg,
		g->gimg, g->bpp, 0);

	if (g->rgb) blend_indexed(start, step, cnt, g->rgb, mem_img[CHN_IMAGE] + l,
		g->wimg, mem_img[CHN_ALPHA] + l, g->walpha, grad_opacity);
}

typedef struct {
	chanlist tlist;		// Channel overrides
	unsigned char *mask0;	// Active mask channel
	unsigned char *pvi;	// Xform render: temp image row
	unsigned char *pvm;	// Xform render: temp mask row
	int rxy[4];		// Clipped area
	int dx;			// Image-space X offset
	int lx;			// Allocated row length
	int pww;		// Logical row length
	int zoom;		// Decimation factor
	int scale;		// Replication factor
	int lop;		// Base opacity
	int xpm;		// Transparent color
} main_render_state;

typedef struct {
	chanlist tlist;		// Channel overrides for rendering clipboard
	unsigned char *clip_alpha;	// Pasted into alpha channel
	unsigned char *t_alpha;		// Fake pasted alpha
	unsigned char *pix, *alpha;	// Destinations for the above
	unsigned char *mask, *wmask;	// Temp mask: one we use, other we init
	unsigned char *mask0;		// Image mask channel to use
	unsigned char *xbuf;	// Extra buffer for process_img()
	int opacity, bpp;	// Just that
	int pixf;		// Flag: need current channel override filled
	int dx;			// Memory-space X offset
	int lx;			// Allocated row length
	int pww;		// Logical row length
	int xform;		// Flag: do color transform
	int maskf;		// Flag: need mask filled
} paste_render_state;

/* !!! If ever combined with more than color transform, must not unconditionally
 * !!! assign potentially shorter (paste area sized) lengths to buffers */
static int paste_render_req(render_mem_req *mr, paste_render_state *p,
	main_render_state *r)
{
	int rxy[4], bpp, scale = r->scale, zoom = r->zoom;


	/* Clip paste area to update area */
	if (!clip(rxy, (marq_x1 * scale + zoom - 1) / zoom,
		(marq_y1 * scale + zoom - 1) / zoom,
		(marq_x2 * scale) / zoom + scale,
		(marq_y2 * scale) / zoom + scale, r->rxy)) return (FALSE);

	/* Setup row position and size */
	p->dx = (rxy[0] * zoom) / scale;
	p->pww = xy_span(rxy, scale, 0);
	p->lx = (p->pww - 1) * zoom + 1;

	if ((mem_channel == CHN_IMAGE) && mem_img[CHN_ALPHA] &&
		!channel_dis[CHN_ALPHA])
	{
		// Need temp alpha
		if (mem_clip_alpha || RGBA_mode) mr->alpha_s = r->lx;
		// Need fake alpha
		if (!mem_clip_alpha && RGBA_mode) mr->n_alpha_s = p->lx;
	}

	bpp = p->bpp = MEM_BPP;
	// Need temp mask
	if ((p->maskf = !mr->mask_s)) mr->mask_s = p->lx;
	// Need temp image
	p->pixf = !mr->channel_s; /* Need it prefilled if no override data incoming */
	mr->channel_s = r->lx * bpp;
	// Need xform buffer
	p->xform = mem_preview_clip && (bpp == 3) && (mem_clip_bpp == 3);
	if (p->xform || NEED_XBUF_PASTE) mr->n_channel_s = p->lx * bpp;

	/* Setup opacity mode */
	if (!IS_INDEXED) p->opacity = tool_opacity;

	return (TRUE);
}

/* !!! This function copies existing override set to build its own modified
 * !!! one, so override set must not be changed after calling it */
static void init_paste_render(render_mem_req *mr, paste_render_state *p,
	main_render_state *r)
{
	int ddx = p->dx - r->dx;


	memcpy(p->tlist, r->tlist, sizeof(chanlist));

	if ((mem_channel == CHN_IMAGE) && !channel_dis[CHN_ALPHA])
		p->clip_alpha = mem_clip_alpha;

	p->tlist[CHN_ALPHA] = mr->alpha;
	p->tlist[mem_channel] = mr->channel;
	p->mask = mr->mask;
	p->t_alpha = mr->n_alpha;
	p->xbuf = mr->n_channel;

	/* Setup "image" (current) channel override */
	p->pix = p->tlist[mem_channel] + ddx * p->bpp;

	/* Setup alpha channel override */
	if (mr->alpha) p->alpha = p->tlist[CHN_ALPHA] + ddx;

	/* Setup mask */
	if (mem_channel <= CHN_ALPHA) p->mask0 = r->mask0;
	if (p->maskf) p->wmask = p->mask;
	else
	{
		p->mask += ddx;
		if (r->mask0 != p->mask0)
		/* Mask has wrong data - reuse memory but refill values */
			p->wmask = p->mask;
	}

	/* Setup fake alpha */
	if (p->t_alpha) memset(p->t_alpha, channel_col_A[CHN_ALPHA], p->lx);
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
	if (p->xform) /* Apply color transform if preview requested */
	{
		do_transform(start, step, cnt, p->mask, p->xbuf, clip_src, 0);
		clip_src = p->xbuf;
	}
	if (mem_clip_bpp < bpp)
	{
		/* Convert paletted clipboard to RGB */
		do_convert_rgb(start, step, cnt, p->xbuf, clip_src,
			mem_clip_paletted ? mem_clip_pal : mem_pal);
		clip_src = p->xbuf;
	}
	process_img(start, step, cnt, p->mask, p->pix, p->pix, clip_src,
		p->xbuf, bpp, 0);
}

typedef struct {
	main_render_state r;
	paste_render_state p;
	render_mem_req m;
	int tflag, gflag, pflag, lr;
	int pw;
	int cxy[4];
	unsigned char *rgb, *irgb;
	threaddata *tdata; // For simplicity
} u_render_state;

static void main_render_req(u_render_state *u)
{
	main_render_state r = u->r;


	if (!channel_dis[CHN_MASK]) r.mask0 = mem_img[CHN_MASK];

	r.xpm = mem_xpm_trans;
	r.lop = 255;
	if (u->lr && layer_selected)
		r.lop = (layer_table_p[layer_selected].opacity * 255 + 50) / 100;

	/* Setup row position and size */
	r.dx = (r.rxy[0] * r.zoom) / r.scale;
	r.pww = xy_span(r.rxy, r.scale, 0);
	r.lx = (r.pww - 1) * r.zoom + 1;

	/* ****** Memory request phase ****** */

	/* Color transform preview */
	if ((u->tflag = mem_preview && (mem_img_bpp == 3)))
	{
		u->m.mask_s = r.lx;
		if (mem_channel == CHN_IMAGE) u->m.channel_s = r.lx * 3;
		else u->m.rgb_s = r.lx * 3;
	}

	/* Color selective mode preview */
	else if (csel_overlay) u->m.overlay_s = r.lx;

	/* Gradient preview */
	else if ((tool_type == TOOL_GRADIENT) && grad_opacity)
		u->gflag = grad_render_req(&u->m, r.lx);

	/* Paste preview - can only coexist with transform */
	if (show_paste && (marq_status >= MARQUEE_PASTE) && !u->m.overlay_s &&
		!u->gflag) u->pflag = paste_render_req(&u->m, &u->p, &r);

	/* Pass the data */
	u->r = r;
}

static void main_render(u_render_state *u, int py, int ph)
{
	main_render_state r = u->r;
	grad_render_state grstate;
	renderstate rs;
	unsigned char *rgb, **tlist = r.tlist, *overlay = u->m.overlay;
	int j, jj, j0, l, pw2, pw;

	/* ****** Init phase ****** */

	/* Color transform preview */
	if (u->tflag)
	{
		r.pvm = u->m.mask;
		r.tlist[CHN_IMAGE] = r.pvi = u->m.rgb ? u->m.rgb : u->m.channel;
	}

	/* Gradient preview */
	if (u->gflag)
	{
		if (mem_channel > CHN_ALPHA) r.mask0 = NULL;
		init_grad_render(&u->m, &grstate, r.lx, r.tlist);
	}

	/* Paste preview */
	if (u->pflag) init_paste_render(&u->m, &u->p, &r);

	/* Start rendering */
	pw2 = r.rxy[2] - r.rxy[0];
	setup_row(&rs, r.rxy[0], pw2, r.zoom, r.scale, mem_width, r.xpm, r.lop,
		u->gflag && grstate.rgb ? 3 : mem_img_bpp, mem_pal);
	rs.cmask = (hide_image ? CMASK_IMAGE : 0) |
		(channel_dis[CHN_ALPHA] ? CMASK_ALPHA : 0) |
		(channel_dis[CHN_SEL] ? CMASK_SEL : 0) |
		(channel_dis[CHN_MASK] ? CMASK_MASK : 0);
 	j0 = -1; pw2 *= 3;
	rgb = u->irgb + (py - r.rxy[1]) * (pw = u->pw);
	for (jj = 0; jj < ph; jj++ , rgb += pw)
	{
		j = ((py + jj) * r.zoom) / r.scale;
		if (j != j0)
		{
			j0 = j;
			l = mem_width * j + r.dx;
			tlist = r.tlist; /* Default override */

			/* Color transform preview */
			if (u->tflag)
			{
				unsigned char *src = mem_img[CHN_IMAGE] + l * 3;
				prep_mask(0, r.zoom, r.pww, r.pvm,
					r.mask0 ? r.mask0 + l : NULL, src);
				do_transform(0, r.zoom, r.pww, r.pvm,
					r.pvi, src, 255);
				process_img(0, r.zoom, r.pww, r.pvm,
					r.pvi, r.pvi, src, NULL, 3, BLENDF_SET);
			}
			/* Color selective mode preview */
			else if (overlay)
			{
				memset(overlay, 0, r.lx);
				csel_scan(0, r.zoom, r.pww, overlay,
					mem_img[CHN_IMAGE] + l * mem_img_bpp,
					csel_data);
			}
			/* Gradient preview */
			else if (u->gflag) grad_render(0, r.zoom, r.pww, r.dx, j,
				r.mask0 ? r.mask0 + l : NULL, &grstate);

			/* Paste preview - should be after transform */
			if (u->pflag && (j >= marq_y1) && (j <= marq_y2))
			{
				tlist = u->p.tlist; /* Paste-area override */
				if (u->p.alpha) memcpy(tlist[CHN_ALPHA],
					mem_img[CHN_ALPHA] + l, r.lx);
				if (u->p.pixf) memcpy(tlist[mem_channel],
					mem_img[mem_channel] + l * u->p.bpp,
					r.lx * u->p.bpp);
				paste_render(0, r.zoom, j, &u->p);
			}
		}
		else if (!async_bk)
		{
			memcpy(rgb, rgb - pw, pw2);
			continue;
		}
		render_row(&rs, rgb, mem_img, r.dx, j, tlist);
		if (!overlay) overlay_row(&rs, rgb, mem_img, r.dx, j, tlist);
		else overlay_preview(&rs, rgb, overlay, csel_preview, csel_preview_a);
	}
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

/* Draw dashed line to RGB memory or straight to canvas */
void draw_dash(int c0, int c1, int ofs, int x, int y, int w, int h, rgbcontext *ctx)
{
	rgbcontext cw;
	unsigned char *dest;
	int i, k, l, ww, step, cc[2] = { c0, c1 };

	if (!ctx)
	{
		cw.xy[2] = (cw.xy[0] = x) + w;
		cw.xy[3] = (cw.xy[1] = y) + h;
		cw.rgb = NULL;
		cmd_setv(drawing_canvas, ctx = &cw, CANVAS_PAINT);
		if (!cw.rgb) return;
	}
	else if (!clip(cw.xy, x, y, x + w, y + h, ctx->xy)) return;

	ofs += cw.xy[0] + cw.xy[1] - x - y; // offset in pattern
	ww = (ctx->xy[2] - ctx->xy[0]) * 3;
	if (w == 1) // Vertical
	{
		step = ww;
		l = cw.xy[3] - cw.xy[1];
	}
	else // Horizontal
	{
		step = 3;
		l = cw.xy[2] - cw.xy[0];
	}
	dest = ctx->rgb + (cw.xy[1] - ctx->xy[1]) * ww +
		(cw.xy[0] - ctx->xy[0]) * 3;

	for (i = 0; i < l; i++)
	{
		k = cc[((i + ofs) / 3) & 1];
		dest[0] = INT_2_R(k);
		dest[1] = INT_2_G(k);
		dest[2] = INT_2_B(k);
		dest += step;
	}

	if (ctx == &cw) cmd_setv(drawing_canvas, ctx, CANVAS_PAINT);
}

/* Polygon drawing to RGB memory */
void draw_poly(int *xy, int cnt, int shift, int x00, int y00, rgbcontext *ctx)
{
	linedata line;
	unsigned char *rgb;
	int i, x0, y0, x1, y1, dx, dy, a0, a, w, vxy[4];

	copy4(vxy, ctx->xy);
	w = vxy[2] - vxy[0];
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
		// Draw to RGB
		for (dx = shift - dx; line[2] >= 0; line_step(line) , dx++)
		{
			rgb = ctx->rgb + ((line[1] - ctx->xy[1]) * w +
				(line[0] - ctx->xy[0])) * 3;
			rgb[0] = rgb[1] = rgb[2] = ((~dx >> 2) & 1) * 255;
		}
	}
}

/* Clip area to image & align rgb pointer with it */
static unsigned char *clip_to_image(int *rect, unsigned char *rgb, int *vxy)
{
	int rxy[4], mw, mh;

	/* Clip update area to image bounds */
	xy_origin(rxy, vxy, margin_main_x, margin_main_y);
	canvas_size(&mw, &mh);
	if (!clip(rect, 0, 0, mw, mh, rxy)) return (NULL);

	/* Align buffer with image */
	rgb += ((rxy[2] - rxy[0]) * (rect[1] - rxy[1]) + (rect[0] - rxy[0])) * 3;
	return (rgb);
}

/* Map clipping rectangle to line-space, for use with line_clip() */
void prepare_line_clip(int *lxy, int *vxy, int scale)
{
	int i;

	for (i = 0; i < 4; i++)
		lxy[i] = floor_div(vxy[i] - margin_main_xy[i & 1] - (i >> 1), scale);
}

static void canvas_render(u_render_state *u, int py, int ph)
{
	int cxy[4], rxy[4], pw = u->pw;
	unsigned char *rgb = NULL;

	/* Render underlying layers */
	if (u->lr)
	{
		copy4(cxy, u->cxy);
		rgb = u->rgb + (py - cxy[1]) * pw;
		cxy[3] = (cxy[1] = py) + ph;
		render_layers(rgb, cxy, pw, u->r.zoom, u->r.scale,
			0, layer_selected - 1, FALSE);
	}

	/* Render canvas image */
	if (u->irgb && clip(rxy, 0, py, INT_MAX, py + ph, u->r.rxy))
		main_render(u, rxy[1], rxy[3] - rxy[1]);

	/* Render overlying layers */
	if (u->lr) render_layers(rgb, cxy, pw, u->r.zoom, u->r.scale,
		layer_selected + 1, layers_total, FALSE);
}

#ifdef U_THREADS

static void do_canvas_render(tcb *thread)
{
	u_render_state *u = thread->data;
	int *rr = u->lr ? u->cxy : u->r.rxy;
	int y0 = rr[1], scale = u->r.scale, h = thread->nsteps * scale;
	int d = floor_mod(y0, scale);

	if (thread->step0) y0 += thread->step0 * scale - d;
	else h -= d;
	if (h > rr[3] - y0) h = rr[3] - y0;

	canvas_render(u, y0, h);
}

#endif

int kpix_threads;	// Min kpixels per render thread

static int paint_canvas(void *dt, void **wdata, int what, void **where,
	rgbcontext *ctx)
{
	u_render_state u;
	unsigned char *irgb, *rgb = ctx->rgb;
	int rect[4], vxy[4];
	int i, px, py, pw, ph, zoom = 1, scale = 1, paste_f = FALSE;

	pw = ctx->xy[2] - (px = ctx->xy[0]);
	ph = ctx->xy[3] - (py = ctx->xy[1]);
	memset(rgb, mem_background, pw * ph * 3);

	/* Find out which part is image */
	irgb = clip_to_image(rect, rgb, ctx->xy);

	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	/* Prepare data for renderer */
	memset(&u, 0, sizeof(u));
	u.r.zoom = zoom;
	u.r.scale = scale;
	u.rgb = rgb;
	u.irgb = irgb;
	u.pw = pw * 3;
	xy_origin(u.cxy, ctx->xy, margin_main_x, margin_main_y);

	u.lr = layers_total && show_layers_main;

	/* Set up image for renderer */
	if (irgb)
	{
		copy4(u.r.rxy, rect);
		main_render_req(&u);
		paste_f = u.pflag;
	}

	if (bkg_flag && bkg_rgb) async_bk = render_bkg(ctx); /* Tracing image */
	else if (!u.lr) /* Render default background if no layers shown */
	{
		if (irgb && ((mem_xpm_trans >= 0) ||
			(!overlay_alpha && mem_img[CHN_ALPHA] && !channel_dis[CHN_ALPHA])))
			async_bk = render_background(irgb, rect[0], rect[1],
				rect[2] - rect[0], rect[3] - rect[1], pw * 3);
	}

	while (irgb || u.lr)
	{
#ifdef U_THREADS
		int nt, nt2, pww = 0, wh = 0;
		size_t vpix = 0;

		/* Calculate amount of work for threads */
		if (irgb)
		{
			wh = xy_span(rect, scale, 1);
			pww = xy_span(rect, scale, 0);
			vpix = pww * wh;
		}
		if (u.lr)
		{
			size_t vp2 = render_layers(NULL, u.cxy, 0, zoom, scale,
				0, layers_total, FALSE);
			if (!vp2) break; // Nothing here, move along
			// !!! Heuristic weight; maybe 1/8 would be better?
			vpix += (vp2 - vpix) / 4;

			u.rgb += ((u.cxy[1] - py + margin_main_y) * pw +
				u.cxy[0] - px + margin_main_x) * 3;

			wh = xy_span(u.cxy, scale, 1);
			pww = xy_span(u.cxy, async_bk ? 1 : scale, 0);
		}

		nt = image_threads(pww, wh);
		nt2 = ceil_div(vpix, kpix_threads * 1024);
		if (nt2 > nt) nt2 = nt;

		u.tdata = talloc(MA_SKIP_ZEROSIZE | MA_FLAG_NONE, nt2,
			&u, sizeof(u), NULL,
#else
		u.tdata = multialloc(MA_SKIP_ZEROSIZE | MA_FLAG_NONE,
#endif
			&u.m.rgb, u.m.rgb_s,
			&u.m.mask, u.m.mask_s,
			&u.m.overlay, u.m.overlay_s,
			&u.m.channel, u.m.channel_s,
			&u.m.alpha, u.m.alpha_s,
			&u.m.n_channel, u.m.n_channel_s,
			&u.m.n_alpha, u.m.n_alpha_s,
			&u.m.n_opacity, u.m.n_opacity_s,
			NULL);

#ifdef U_THREADS
		if (u.tdata && (u.tdata != MEM_NONE)) // Threads w/allocation
		{
			nt /= u.tdata->count;
			if (nt > MAX_TH_STRIPS) nt = MAX_TH_STRIPS;
			u.tdata->chunks = nt;
			u.tdata->silent = TRUE;
			launch_threads(do_canvas_render, u.tdata, NULL, wh);
		}
		else
#endif
		if (u.tdata) // Single thread
		{
			int *rr = u.lr ? u.cxy : rect;
			canvas_render(&u, rr[1], rr[3] - rr[1]);
		}
		if (u.tdata != MEM_NONE) free(u.tdata);
		break;
	}

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
	if (show_tile_grid && irgb) draw_tgrid(irgb, rect[0], rect[1],
		rect[2] - rect[0], rect[3] - rect[1], pw);

	/* Segmentation preview */
	if (seg_preview && irgb) draw_segments(irgb, rect[0], rect[1],
		rect[2] - rect[0], rect[3] - rect[1], pw);

	async_bk = FALSE;

/* !!! All other over-the-image things have to be redrawn here as well !!! */
	prepare_line_clip(vxy, ctx->xy, scale);
	/* Redraw gradient line if needed */
	i = gradient[mem_channel].status;
	if ((mem_gradient || (tool_type == TOOL_GRADIENT)) &&
		(mouse_left_canvas ? (i == GRAD_DONE) : (i != GRAD_NONE)))
		refresh_line(3, vxy, ctx);

	/* Draw marquee as we may have drawn over it */
	if ((marq_status != MARQUEE_NONE) && irgb)
		paint_marquee(MARQ_SHOW, 0, 0, ctx);
	if ((tool_type == TOOL_POLYGON) && poly_points)
		paint_poly_marquee(ctx);

	/* Redraw line if needed */
	if ((((tool_type == TOOL_POLYGON) && (poly_status == POLY_SELECTING)) ||
		((tool_type == TOOL_LINE) && (line_status == LINE_LINE))) &&
		!mouse_left_canvas)
		refresh_line(tool_type == TOOL_LINE ? 1 : 2, vxy, ctx);

	/* Redraw perimeter if needed */
	if (perim_status) repaint_perim(ctx);

	return (TRUE); // now draw this
}

void repaint_canvas(int px, int py, int pw, int ph)
{
	rgbcontext ctx = { { px, py, px + pw, py + ph }, NULL };

	cmd_setv(drawing_canvas, &ctx, CANVAS_PAINT);
	if (!ctx.rgb) return;
	paint_canvas(NULL, NULL, 0, NULL, &ctx);
	cmd_setv(drawing_canvas, &ctx, CANVAS_PAINT);
}

/* Update x,y,w,h area of current image */
void main_update_area(int x, int y, int w, int h)
{
	int zoom, scale, rxy[4];

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

	rxy[2] = (rxy[0] = x + margin_main_x) + w;
	rxy[3] = (rxy[1] = y + margin_main_y) + h;
	cmd_setv(drawing_canvas, rxy, CANVAS_REPAINT);
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

static void clear_perim(perim_info *p)
{
	int ps = p->mode;
	p->mode = 0; /* Cleared */
	clear_perim_real(p->x, p->y, p->s);
	if (ps == TOOL_CLONE + 1)
		clear_perim_real(p->x + p->cx, p->y + p->cy, p->s);
}

static void repaint_perim_real(int c, int ox, int oy, rgbcontext *ctx)
{
	int w, h, x0, y0, x1, y1, zoom = 1, scale = 1;


	/* !!! This uses the fact that zoom factor is either N or 1/N !!! */
	if (can_zoom < 1.0) zoom = rint(1.0 / can_zoom);
	else scale = rint(can_zoom);

	ox += perim_x; oy += perim_y;
	x0 = margin_main_x + (ox * scale) / zoom;
	y0 = margin_main_y + (oy * scale) / zoom;
	x1 = margin_main_x + ((ox + perim_s - 1) * scale) / zoom + scale - 1;
	y1 = margin_main_y + ((oy + perim_s - 1) * scale) / zoom + scale - 1;

	w = x1 - x0 + 1;
	h = y1 - y0 + 1;

	draw_dash(c, RGB_2_INT(0, 0, 0), 0, x0, y0, 1, h, ctx);
	draw_dash(c, RGB_2_INT(0, 0, 0), 0, x1, y0, 1, h, ctx);

	draw_dash(c, RGB_2_INT(0, 0, 0), 0, x0 + 1, y0, w - 2, 1, ctx);
	draw_dash(c, RGB_2_INT(0, 0, 0), 0, x0 + 1, y1, w - 2, 1, ctx);
}

static void repaint_perim(rgbcontext *ctx)
{
	repaint_perim_real(RGB_2_INT(255, 255, 255), 0, 0, ctx);
	if (perim_status == TOOL_CLONE + 1)
		repaint_perim_real(RGB_2_INT(255, 0, 0), perim_cx, perim_cy, ctx);
}

static void move_perim(int x, int y)
{
	perim_info p = perim_state;

	perim_status = 0; /* Clear */
	perim_wx = x; /* Remember */
	perim_wy = y;
	if ((tool_size * can_zoom > 4) && !NO_PERIM(tool_type) && !mouse_left_canvas)
	{
		perim_x = x - (tool_size >> 1);
		perim_y = y - (tool_size >> 1);
		perim_s = tool_size;
		perim_cx = clone_dx;
		perim_cy = clone_dy;
		perim_status = tool_type + 1; /* Draw */
	}
	if (p.mode) clear_perim(&p);
	if (perim_status) repaint_perim(NULL);
}

static void configure_canvas()
{
	int new_margin_x = 0, new_margin_y = 0;

	if (canvas_image_centre)
	{
		int w, h, wh[2];
		cmd_peekv(drawing_canvas, wh, sizeof(wh), CANVAS_SIZE);
		canvas_size(&w, &h);
		if ((wh[0] -= w) > 0) new_margin_x = wh[0] >> 1;
		if ((wh[1] -= h) > 0) new_margin_y = wh[1] >> 1;
	}

	if ((new_margin_x != margin_main_x) || (new_margin_y != margin_main_y))
	{
		margin_main_x = new_margin_x;
		margin_main_y = new_margin_y;
		cmd_repaint(drawing_canvas);
			// Force redraw of whole canvas as the margin has shifted
	}
	vw_realign();	// Update the view window as needed
}

void force_main_configure()
{
	if (cmd_mode) return;
	if (drawing_canvas) configure_canvas();
	if (view_showing && vw_drawing) vw_configure();
}

void set_cursor(void **what)	// Set mouse cursor
{
	cmd_cursor(drawing_canvas, !cursor_tool ? NULL : what ? what :
		m_cursor[tool_type]);
}

void change_to_tool(int icon)
{
	grad_info *grad;
	void *var, **slot = icon_buttons[icon];
	int i, t, update = UPD_SEL;

	if (!cmd_checkv(slot, SLOT_SENSITIVE)) return; // Blocked
// !!! Unnecessarily complicated approach - better add a peek operation
	var = cmd_read(slot, NULL);
	if (*(int *)var != TOOL_ID(slot)) cmd_set(slot, TRUE);

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

	/* Make sure tool release actions are done */
	tool_done();

	if (perim_status) clear_perim(&perim_state);
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
		if (!script_cmds && paste_commit && (marq_status >= MARQUEE_PASTE))
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
	if (tool_type == TOOL_CLONE) init_clone();
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
	move_perim(perim_wx, perim_wy); // New perimeter in old location
}

static void pressed_view_hori(int state)
{
	view_vsplit = !!state;
	if (view_showing) view_show(); // rearrange
}

void set_image(int state)
{
	static int depth = 0;

	if (state ? --depth : depth++) return;

	cmd_showhide(main_split, state);
}

static char read_hex_dub(char *in)	// Read hex double
{
	static const char chars[] = "0123456789abcdef0123456789ABCDEF";
	const char *p1, *p2;

	p1 = strchr(chars, in[0]);
	p2 = strchr(chars, in[1]);
	return (p1 && p2 ? (((p1 - chars) & 15) << 4) + ((p2 - chars) & 15) : '?');
}

static void parse_drag(main_dd *dt, void **wdata, int what, void **where,
	drag_ext *drag)
{
#ifdef WIN32
	char fname[PATHTXT];
#else
	char fname[PATHBUF];
#endif
	char ch, *tp, *tp2, *txt = drag->data;
	int i, j, nlayer = TRUE;


	if (drag->len <= 0) return;

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


static clipform_dd uri_list = { "text/uri-list" };


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

///	SCRIPTING

/* Number of args is ((char **)res[0] - res - 1); res[] is NULL-terminated */
static char **wj_parse_argv(char *src)
{
	char c, q, q0, *dest, *tmp, **v;
	int n = 0, l = strlen(src);

	dest = tmp = malloc(l + 1);
	q0 = q = ' '; // Start outside word
	while ((c = *src++))
	{
		/* Various quoted states */
		if (q == '#')
		{
			if (c == '\n') q = ' ';
		}
		else if (q == '\\')
		{
			q = q0;
			if (q == '"')
			{
				if ((c != '"') && (c != '\\') && (c != '`') &&
					(c != '$') && (c != '\n')) *dest++ = '\\';
				*dest++ = c;
			}
			else if (c != '\n')
			{
				*dest++ = c;
				q = 0;
			}
		}
		else if (q == '"')
		{
			if (c == '\\') q0 = q , q = c;
			else if (c == '"') q = 0;
			else *dest++ = c;
		}
		else if (q == '\'')
		{
			if (c == '\'') q = 0;
			else *dest++ = c;
		}
		/* Unquoted state - in a word or in between */
		else if ((c == '\n') || (c == ' ') || (c == '\t'))
		{
			if (!q) *dest++ = 0 , n++;
			q = ' ';
		}
		else if (c == '\\') q0 = q , q = c;
		else if ((c == '#') && q) q = c;
		else if ((c == '"') || (c == '\'')) q = c;
		else
		{
			*dest++ = c;
			q = 0;
		}
	}
	/* Final state */
	if (!q) *dest++ = 0 , n++;
	else if ((q == '\\') || (q == '"') || (q == '\'')) n = -1; // Error
	l = dest - tmp;
	if ((n > 0) && (v = realloc(tmp, sizeof(*v) * (n + 1) + l)))
	{
		tmp = (void *)(v + n + 1);
		memmove(tmp, v, l);
		for (l = 0; l < n; l++)
		{
			v[l] = tmp;
			tmp += strlen(tmp) + 1;
		}
		v[n] = NULL;
		return (v);
	}
	/* Syntax error, or empty string */
	free(tmp);
	return (NULL);
}

static void **command_slot(char *cmd)
{
	void **slot;
	int l, m;

	l = strspn(++cmd, "/");
	l += strcspn(cmd + l, "=/");
	slot = find_slot(NEXT_SLOT(main_menubar), cmd, l,
		(cmd[0] != '/') && (cmd[l] == '/'));
	m = 2;
	while (slot && (cmd[l] == '/'))
	{
		cmd += l + 1;
		l = strcspn(cmd, "=/");
		slot = find_slot(NEXT_SLOT(slot), cmd, l, m++);
	}
	return (slot);
}

#define MAX_NESTING 16 /* Defuse recursion bombs */

int run_script(char **res)
{
	static int level;
	void **slot;
	char **cur, *str = NULL, *err = NULL;
	int n;

	level++;
	if (!res || !res[0]) err = _("Empty string or broken quoting");
	else if (res[0][0] != '-') err = _("Script must begin with a command");
	else if (level > MAX_NESTING) err = _("Script nesting limit exceeded");
	else
	{
		user_break = 0;
		for (cur = res; cur[0]; cur++)
		{
			if (cur[0][0] != '-') continue; // Skip to next command
			if (!strcmp(cur[0], "--")) break; // End marker
			slot = command_slot(cur[0]);
			if (!slot) str = _("'%s' does not match any item");
			else if (!cmd_checkv(slot, SLOT_SCRIPTABLE))
				str = _("'%s' matches a non-scriptable item");
			else if (!cmd_checkv(slot, SLOT_SENSITIVE))
				str = _("'%s' matches a disabled item");
			else
			{
				char *chain[3], *tmp = strchr(cur[0], '=');

				script_cmds = cur + 1;
				/* Send default value on, if no toggle-item */
				if (tmp && !slot_data(slot, NULL))
				{
					chain[0] = tmp;
					chain[1] = (void *)&chain[1];
					chain[2] = (void *)script_cmds;
					script_cmds = chain;
				}

				/* Activate the item */
				n = cmd_setstr(slot, tmp + !!tmp); // skip "="
				script_cmds = NULL;

				if (n < 0) str = _("'%s' value does not fit item");
				else if (!user_break) continue;
			}
			break; // Terminate on error
		}
		if (str) err = str = g_strdup_printf(__(str), cur[0]);
		update_stuff(CF_NOW); // Do cumulative update
	}
	level--;
	if (err) alert_box(_("Error"), err, NULL);
	if (str) g_free(str);
	return (!err ? 1 : str ? -1 : 0); // 1 = clean, -1 = buggy, 0 = wrong
}

#define SCRIPT_ITEMS 10
#define SCRIPTS_MAX FACTION_ROWS_TOTAL
#define MAXNAMELEN 2048

#define SCRIPT1_NAME "# Resharpen image"
#define SCRIPT1_CODE "# Resharpen image after rescaling\n" \
	"-effect/unsharp r=1 am=0.4 -effect/unsharp r=30 am=0.1"

static char *script_ini[2] = { "scriptName%d", "script%d" };

static void launch_script(int n)
{
	char txt[64], **res;

	sprintf(txt, script_ini[1], n);
	res = wj_parse_argv(inifile_get(txt, ""));
	run_script(res);
	free(res);
}

static void update_script_menu()	// Populate menu
{
	int i, v, items = 0;
	char txt[64], *nm, *sm, *ns = SCRIPT1_NAME, *ss = SCRIPT1_CODE;
	void **slot;

	/* Show valid slots in menu */
	for (i = 1; i <= SCRIPT_ITEMS; i++)
	{
		sprintf(txt, script_ini[0], i);
		nm = inifile_get(txt, ns);

		sprintf(txt, script_ini[1], i);
		sm = inifile_get(txt, ss);

		slot = menu_slots[MENU_SCRIPT1 - 1 + i];

		if ((v = nm && nm[0] && (nm[0] != '#') && sm && sm[0]))
			cmd_setv(slot, nm, LABEL_VALUE);

		cmd_showhide(slot, v); // Hide by default
		cmd_sensitive(slot, v); // Make insensitive for shortcuts
		items += v;
		ns = ss = "";
	}

	/* Hide submenu if no valid slots */
	cmd_showhide(menu_slots[MENU_SCRIPT_M], items);
	cmd_showhide(menu_slots[MENU_SCRIPT], !items);
}	

typedef struct {
	char *rows[SCRIPTS_MAX][4];
	char *script, *name;
	void **list, **nm, **tx, **exec;
	int nidx, idx, cnt, lock, changed;
} script_dd;

static void update_text(script_dd *dt)
{
	if (dt->changed)
	{
		char **rp = dt->rows[dt->idx];
		cmd_read(dt->tx, dt);
		if (rp[1] != rp[3]) free(rp[1]);
		rp[1] = strdup(dt->script);
		dt->changed = FALSE;
	}
}

static void script_click(script_dd *dt, void **wdata, int what, void **where)
{
	char txt[64], **rp;
	int i, idx[SCRIPTS_MAX];

	if (what != op_EVT_CANCEL) update_text(dt);

	if (origin_slot(where) == dt->exec) // Run script
	{
		char **res = wj_parse_argv(dt->rows[dt->idx][1]);
		run_script(res);
		free(res);
		return;
	}

	cmd_showhide(wdata, FALSE);
	if (what != op_EVT_CANCEL) // Store scripts
	{
		cmd_peekv(dt->list, idx, sizeof(idx), LISTC_ORDER);
		/* Copy out reordered strings */
		for (i = 0; i < SCRIPTS_MAX; i++)
		{
			if (idx[i] == i) continue;
			rp = dt->rows[i];
			if (rp[0] == rp[2]) rp[0] = strdup(rp[0]);
			if (rp[1] == rp[3]) rp[1] = strdup(rp[1]);
		}
		/* Store into inifile */
		for (i = 0; i < SCRIPTS_MAX; i++)
		{
			rp = dt->rows[i];
			sprintf(txt, script_ini[0], idx[i] + 1);
			if (rp[0] != rp[2]) inifile_set(txt, rp[0]);
			sprintf(txt, script_ini[1], idx[i] + 1);
			if (rp[1] != rp[3]) inifile_set(txt, rp[1]);
		}
		/* Display in menu */
		update_script_menu();
	}

	for (i = 0; i < SCRIPTS_MAX; i++) // Release string memory
	{
		rp = dt->rows[i];
		if (rp[0] != rp[2]) free(rp[0]);
		if (rp[1] != rp[3]) free(rp[1]);
	}

	run_destroy(wdata);
}

static void script_changed(script_dd *dt, void **wdata, int what, void **where)
{
	char **rp;

	if (dt->lock) return;
	if (origin_slot(where) == dt->tx) // Text entry
	{
		dt->changed = TRUE; // Read it later
		return;
	}

	cmd_read(where, dt); // Name entry
	rp = dt->rows[dt->idx];
	if (rp[0] == rp[2]) rp[0] = malloc(MAXNAMELEN);
	strncpy0(rp[0], dt->name, MAXNAMELEN);
	cmd_setv(dt->list, (void *)dt->idx, LISTC_RESET_ROW);
}

static void script_select_row(script_dd *dt, void **wdata, int what, void **where)
{
	char **rp;

	cmd_read(where, dt);

	if (dt->nidx == dt->idx) return; // no change
	dt->lock = TRUE;
	
	/* Update outgoing row */
	update_text(dt);

	/* Get fields from array for incoming row */
	rp = dt->rows[dt->idx = dt->nidx];
	cmd_setv(dt->nm, rp[0], ENTRY_VALUE);
	cmd_setv(dt->tx, rp[1], TEXT_VALUE);

	dt->lock = FALSE;
}

#define WBbase script_dd
static void *script_code[] = {
	WINDOWm(_("Script")),
//	WXYWH("script", 400, 400),
	DEFSIZE(400, 400),
	HSEP,
	XVBOXB,
	BORDER(SCROLL, 0), BORDER(FRAME, 0), BORDER(ENTRY, 0),
	VSPLIT,
	XSCROLL(1, 1), // auto/auto; 1st page
		WLIST,
		PTXTCOLUMN(rows[0][0], WBsizeof(rows[0]), 200, 0),
		REF(list), LISTCd(nidx, cnt, script_select_row), TRIGGER,
	VBOXS, // 2nd page
		FHBOXB(_("Action")),
		REF(nm), XENTRY(name), EVENT(CHANGE, script_changed),
		WDONE, // FHBOX
		REF(tx), TEXT(script), EVENT(CHANGE, script_changed),
	WDONE, // VBOX
	WDONE, // VSPLIT
	WDONE, // XVBOX
	HSEP,
	EQBOX, CANCELBTN(_("Cancel"), script_click),
		REF(exec), BUTTON(_("Execute"), script_click),
		BUTTON(_("OK"), script_click), FOCUS,
	WSHOW
};
#undef WBbase

static void pressed_script()
{
	script_dd tdata;
	char txt[64], *ns = SCRIPT1_NAME, *ss = SCRIPT1_CODE;
	int i;

	memset(&tdata, 0, sizeof(tdata));
	for (i = 0; i < SCRIPTS_MAX; i++)
	{
		sprintf(txt, script_ini[0], i + 1);
		tdata.rows[i][0] = tdata.rows[i][2] = inifile_get(txt, ns);
		sprintf(txt, script_ini[1], i + 1);
		tdata.rows[i][1] = tdata.rows[i][3] = inifile_get(txt, ss);
		ns = ss = "";
	}

	tdata.idx = 1;
	tdata.cnt = SCRIPTS_MAX;
	run_create(script_code, &tdata, sizeof(tdata));
}

typedef struct {
	char *name;
} name_dd;

#define WBbase name_dd
static void *name_code[] = {
	TOPVBOX, uPATHSTR(name), WSHOW
};
#undef WBbase

static void load_script()
{
	name_dd *dt, tdata = { NULL };
	char *txt, *tx2 = NULL, **res = NULL;
	void **wdata;

	wdata = run_create_(name_code, &tdata, sizeof(tdata), script_cmds);
	run_query(wdata);
	dt = GET_DDATA(wdata);
	txt = slurp_file(dt->name, 0);
	run_destroy(wdata);
	if (!txt)
	{
		alert_box(_("Error"), _("Unable to load script"), NULL);
		return;
	}

	/* Stepping through arguments is done within run_create_(), so here
	 * script_cmds is done with and free for overloading - WJ */
	if (!cmd_mode) tx2 = gtkuncpy(NULL, txt, 0); // Files are in system encoding
	res = wj_parse_argv(tx2 ? tx2 : txt);
	run_script(res);
	free(res);
	free(tx2);
	free(txt);
}

static void do_script(int what)
{
	void **where = what == TOOLBAR_LAYERS ? layers_dock :
		what == TOOLBAR_SETTINGS ? settings_dock :
		/* what == TOOLBAR_TOOLS */ toolbar_boxes[TOOLBAR_TOOLS];
	cmd_run_script(where, script_cmds);
}

/* Script mode color picker */
static int pick_pixel(multi_ext *mx, void **where, int ab)
{
	int area[4], *row = mx->rows[0] + 1;

	/* Sanity check */
	if ((mx->mincols < 2) || (mx->fractcol >= 0) || (mx->nrows > 2))
		return (0);

	/* 2 corners */
	if ((mx->nrows == 2) && (mx->ncols == 2))
	{
		area[0] = row[0];
		area[1] = row[1];
		row = mx->rows[1] + 1;
		area[2] = row[0] + 1;
		area[3] = row[1] + 1;
	}
	/* 1 point */
	else if (mx->ncols == 2)
	{
		area[2] = (area[0] = row[0]) + 1;
		area[3] = (area[1] = row[1]) + 1;
	}
	/* Area with size */
	else if (mx->ncols == 4)
	{
		area[2] = (area[0] = row[0]) + row[2];
		area[3] = (area[1] = row[1]) + row[3];
	}
	/* Error */
	else return (0);

	/* Drop previous regular value */
	cmd_set(origin_slot(where), -1);

	if (clip(area, 0, 0, mem_width, mem_height, area))
	{
		int sz = (area[2] -= area[0]) | (area[3] -= area[1]);
		pick_color(area[0], area[1], ab, sz != 1 ? area : NULL, -1);
	}
	return (1);
}

typedef struct {
	int n, mode;
} idx_dd;

static int idx_multi_evt(idx_dd *dt, void **wdata, int what, void **where,
	multi_ext *mx)
{
	/* A/B mode */
	if (dt->mode < 0x100) return (pick_pixel(mx, where, dt->mode));
	/* Unmask/mask mode */
	if ((mx->fractcol >= 0) || (mx->nrows > 1)) return (0);  // Error
	cmd_set(origin_slot(where), 256); // Drop previous regular value
	mem_mask_setv(mx->rows[0] + 1, mx->rows[0][0], dt->mode - 0x100);
	return (1);
}

#define WBbase idx_dd
static void *idx_code[] = {
	TOPVBOX, SPIN(n, -1, 256), EVENT(MULTI, idx_multi_evt), WSHOW
};
#undef WBbase

static int script_idx(int mode)
{
	idx_dd tdata;
	void **res;
	int n;

	if (!script_cmds) return (-1);
	tdata.n = -1;
	tdata.mode = mode;
	res = run_create_(idx_code, &tdata, sizeof(tdata), script_cmds);
	run_query(res);
	n = ((idx_dd *)GET_DDATA(res))->n;
	run_destroy(res);
	return (n);
}

typedef struct {
	int a, b;
} ab_dd;

static int ab_pick_pixel(ab_dd *dt, void **wdata, int what, void **where,
	multi_ext *mx)
{
	return (pick_pixel(mx, where, cmd_read(where, dt) == &dt->b));
}

#define WBbase ab_dd
static void *ab_code[] = {
	TOPVBOX,
	SPIN(a, -1, 256), ALTNAME("a"), EVENT(MULTI, ab_pick_pixel),
	SPIN(b, -1, 256), OPNAME("b"), EVENT(MULTI, ab_pick_pixel),
	WSHOW
};
#undef WBbase

static void script_ab()
{
	static ab_dd tdata = { -1, -1 };
	ab_dd *dt;
	void **res;

	res = run_create_(ab_code, &tdata, sizeof(tdata), script_cmds);
	run_query(res);
	dt = GET_DDATA(res);
	if ((dt->a >= 0) && (dt->a < mem_cols))
		mem_col_A24 = mem_pal[mem_col_A = dt->a];
	if ((dt->b >= 0) && (dt->b < mem_cols))
		mem_col_B24 = mem_pal[mem_col_B = dt->b];
	run_destroy(res);
	update_stuff(UPD_AB);
}

typedef struct {
	int x[3], y[3], n[3], brush;
	void **nspin;
} nxy_dd;

static void bp_evt(nxy_dd *dt, void **wdata, int what, void **where)
{
	int w = dt->x[2] + 1;
	if (cmd_read(where, dt) == dt->n)
	{
		int n = dt->n[0];
		dt->x[0] = n % w;
		dt->y[0] = n / w;
		if (dt->brush) mem_set_brush(n);
		else mem_tool_pat = n;
	}
	else if ((what == op_EVT_SCRIPT) && (dt->x[0] >= 0) && (dt->y[0] >= 0))
		cmd_set(dt->nspin, dt->y[0] * w + dt->x[0]);
}

static char *brush_txt[TOOL_SPRAY + 1] = { "Square", "Circle",
	"Horizontal", "Vertical", "Slash", "Backslash", "Spray" };

#define WBbase nxy_dd
static void *bp_code[] = {
	TOPVBOX,
	REF(nspin), SPINa(n), EVENT(CHANGE, bp_evt),
	SPINa(x), EVENT(SCRIPT, bp_evt), OPNAME("X"),
	SPINa(y), EVENT(SCRIPT, bp_evt), OPNAME("Y"),
	IFx(brush, 1),
		RPACKv(brush_txt, TOOL_SPRAY + 1, 1, brush_type),
			 EVENT(SELECT, bp_evt), OPNAME("Type"),
		SPINv(tool_size, 1, 255), EVENT(CHANGE, bp_evt), OPNAME("Size"),
		SPINv(tool_flow, 1, 255), EVENT(CHANGE, bp_evt), OPNAME("Flow"),
	ENDIF(1),
	WSHOW
};
#undef WBbase

static void script_bp(int mode)
{
	nxy_dd tdata;

	memset(&tdata, 0, sizeof(tdata));
	tdata.x[1] = tdata.y[1] = -1;
	if (mode == CHOOSE_BRUSH)
	{
		tdata.x[2] = BRUSH_GRID_W - 1;
		tdata.y[2] = BRUSH_GRID_H - 1;
		tdata.x[0] = tdata.y[0] = -1;
		tdata.brush = TRUE;
	}
	else
	{
		tdata.x[2] = PATTERN_GRID_W - 1;
		tdata.y[2] = PATTERN_GRID_H - 1;
		tdata.n[0] = mem_tool_pat;
		tdata.x[0] = mem_tool_pat % PATTERN_GRID_W;
		tdata.y[0] = mem_tool_pat / PATTERN_GRID_W;
	}
	tdata.n[2] = (tdata.x[2] + 1) * (tdata.y[2] + 1) - 1;
	run_destroy(run_create_(bp_code, &tdata, sizeof(tdata), script_cmds));
	if (mode == CHOOSE_BRUSH)
	{
		change_to_tool(TTB_PAINT);
		update_stuff(UPD_BRUSH);
	}
	else update_stuff(UPD_PAT);
}

typedef struct {
	int w, h, rxy[4];
	multi_ext *mx;
	void **group;
} rect_dd;

static int make_select(rect_dd *dt, void **wdata, int what, void **where,
	multi_ext *mx)
{
	/* Drop previous polygon if any */
	free(dt->mx);
	dt->mx = NULL;

	/* Sanity check */
	if ((mx->mincols < 2) || (mx->fractcol >= 0)) return (0);
	if ((mx->ncols != 2) && ((mx->nrows != 1) || (mx->ncols != 4)))
		return (0);

	/* Point/rectangle selection */
	if (mx->nrows <= 2)
	{
		int *row = mx->rows[0] + 1;
		dt->rxy[0] = row[0];
		dt->rxy[1] = row[1];
		if (mx->nrows > 1) row = mx->rows[1] + 1;
		dt->rxy[2] = row[0];
		dt->rxy[3] = row[1];
		if (mx->ncols > 2)
		{
			dt->rxy[2] += row[2] - 1;
			dt->rxy[3] += row[3] - 1;
		}
		cmd_reset(dt->group, dt); // Update widgets
		return (1);
	}

	/* Polygon selection */
	dt->mx = mx;
	return (-1); // Keep the data
}

#define WBbase rect_dd
static void *rect_code[] = {
	TOPVBOX, EVENT(MULTI, make_select), OPNAME(""),
	REF(group), GROUPR,
	SPIN(rxy[0], 0, MAX_WIDTH - 1), OPNAME("x0"),
	SPIN(rxy[1], 0, MAX_HEIGHT - 1), OPNAME("y0"),
	SPIN(rxy[2], 0, MAX_WIDTH - 1), OPNAME("x1"),
	SPIN(rxy[3], 0, MAX_HEIGHT - 1), OPNAME("y1"),
	SPIN(w, 0, MAX_WIDTH), OPNAME("width"),
	SPIN(h, 0, MAX_HEIGHT), OPNAME("height"),
	WSHOW
};
#undef WBbase

static multi_ext *script_rect(int *rect)
{
	static rect_dd tdata = { 0, 0, { 0, 0, MAX_WIDTH - 1, MAX_HEIGHT - 1 } };
	rect_dd *dt;
	multi_ext *mx;
	void **res;

	copy4(rect, tdata.rxy);
	if (!script_cmds) return (NULL);

	res = run_create_(rect_code, &tdata, sizeof(tdata), script_cmds);
	run_query(res);
	dt = GET_DDATA(res);
	mx = dt->mx;
	if (dt->w) dt->rxy[2] = dt->rxy[0] + dt->w - 1;
	if (dt->h) dt->rxy[3] = dt->rxy[1] + dt->h - 1;
	copy4(rect, dt->rxy);
	run_destroy(res);
	return (mx);
}

typedef struct {
	int swap, mx, dx, dy, align, x, y;
} paste_dd;

static char *paste_align[] = { "Left", "Right", "Top", "Bottom", "Centre" };

static void align_evt(paste_dd *dt, void **wdata, int what, void **where)
{
	cmd_read(where, dt);
	switch (dt->align)
	{
	case 0: // Left
		dt->dx = 0; break;
	case 1: // Right
		dt->dx = mem_clip_w - 1; break;
	case 2: // Top
		dt->dy = 0; break;
	case 3: // Bottom
		dt->dy = mem_clip_h - 1; break;
	case 4: // Center
		dt->dx = mem_clip_w / 2; dt->dy = mem_clip_h / 2; break;
	}
}

static int do_paste(paste_dd *dt, void **wdata, int what, void **where,
	multi_ext *mx)
{
	int i, fr, mode, rows, dx = dt->dx, dy = dt->dy, p = MAX_PRESSURE;

	/* Sanity check */
	if ((mx->ncols > 3) || (mx->mincols < 2) ||
		((mx->fractcol >= 0) && (mx->fractcol != 2))) return (0); // Error

	do_tool_action(TC_PASTE_DRAG, marq_x1, marq_y1, 0);

	mode = dt->swap ? TC_PASTE_PSWAP | TCF_PRES | TCF_ONCE :
		TC_PASTE_PAINT | TCF_PRES | TCF_ONCE;
	fr = mx->fractcol == 2;
	rows = mx->nrows;
	for (i = 0; i < rows; i++)
	{
		int *row = mx->rows[i];
		if (row[0] > 2) p = bounded(fr ? row[3] :
			(row[3] * MAX_PRESSURE) / 100, 0, MAX_PRESSURE);
		do_tool_action(mode, row[1] - dx, row[2] - dy, p);
	}

	do_tool_action(TC_SEL_STOP, marq_x1, marq_y1, 0);
	tool_done();
	dt->mx = TRUE;
	return (1);
}

#define WBbase paste_dd
static void *paste_code[] = {
	TOPVBOX,
	OPT(paste_align, 5, align), EVENT(SCRIPT, align_evt),
		ALTNAME("Align"), FLATTEN, EVENT(MULTI, do_paste),
	CHECK("swap", swap), EVENT(CHANGE, align_evt),
	SPIN(x, -MAX_WIDTH, MAX_WIDTH), OPNAME("x0"),
	SPIN(y, -MAX_HEIGHT, MAX_HEIGHT), OPNAME("y0"),
	WSHOW
};
#undef WBbase

void script_paste(int centre)
{
	paste_dd tdata, *dt;
	void **res;

	memset(&tdata, 0, sizeof(tdata));
	tdata.x = marq_x1;
	tdata.y = marq_y1;
	if (centre)
	{
		tdata.x += tdata.dx = mem_clip_w / 2;
		tdata.y += tdata.dy = mem_clip_h / 2;
		tdata.align = 4;
	}
	res = run_create_(paste_code, &tdata, sizeof(tdata), script_cmds);
	run_query(res);
	dt = GET_DDATA(res);
	if (!dt->mx) // No commits yet - paste at x & y
	{
		int x = dt->x - dt->dx, y = dt->y - dt->dy;
		if ((marq_x1 ^ x) | (marq_y1 ^ y)) // Moved
			paint_marquee(MARQ_MOVE, x, y, NULL);
		commit_paste(dt->swap, NULL);
		tool_done();
	}
	run_destroy(res);
	update_stuff(UPD_SGEOM);
}

static void do_act_esc()
{
	if ((tool_type == TOOL_SELECT) || (tool_type == TOOL_POLYGON))
		pressed_select(FALSE);
	else if (tool_type == TOOL_LINE)
	{
		stop_line();
		update_sel_bar(FALSE);
	}
	else if ((tool_type == TOOL_GRADIENT) &&
		(gradient[mem_channel].status != GRAD_NONE))
	{
		do_grad_action(TC_GRAD_CLEAR, 0, 0);
		update_sel_bar(FALSE);
	}
}

static void select_poly(multi_ext *mx)
{
	int i, rows = mx->nrows;

	for (i = 0; i < rows; i++)
	{
		int *row = mx->rows[i] + 1;
		do_tool_action(i ? TC_POLY_ADD | TCF_ONCE : TC_POLY_START,
			bounded(row[0], 0, mem_width - 1),
			bounded(row[1], 0, mem_height - 1), 0);
	}
	do_tool_action(TC_POLY_CLOSE, 0, 0, 0);
}

static int tool_command(main_dd *dt, void **wdata, int what, void **where,
	multi_ext *mx)
{
	int *row;
	int i, p, fr, rows = mx->nrows, res = 0;

	do_act_esc();
	switch (tool_type)
	{
	case TOOL_GRADIENT:
		if ((rows == 1) && (mx->ncols < 3)) res = 1; // Valid clear op
		if ((rows != 2) || (mx->ncols != 2) || (mx->mincols != 2) ||
			(mx->fractcol >= 0)) break; // Error
		row = mx->rows[0] + 1;
		do_grad_action(TC_GRAD_START,
			bounded(row[0], -MAX_WIDTH, MAX_WIDTH),
			bounded(row[1], -MAX_HEIGHT, MAX_HEIGHT));
		row = mx->rows[1] + 1;
		do_grad_action(TC_GRAD_SET1,
			bounded(row[0], -MAX_WIDTH, MAX_WIDTH),
			bounded(row[1], -MAX_HEIGHT, MAX_HEIGHT));
		res = 1;
		break;
	case TOOL_LINE:
		if ((rows < 2) || (mx->ncols != 2) || (mx->mincols != 2) ||
			(mx->fractcol >= 0)) break; // Error
		for (i = 0; i < rows; i++)
		{
			row = mx->rows[i] + 1;
			do_tool_action(TC_LINE_START,
				bounded(row[0], 0, mem_width - 1),
				bounded(row[1], 0, mem_height - 1), MAX_PRESSURE);
			line_status = LINE_LINE;
		}
		stop_line();
		res = 1;
		break;
	case TOOL_SELECT:
		if ((rows > 2) || (mx->ncols != 2) || (mx->mincols != 2) ||
			(mx->fractcol >= 0)) break; // Error
		row = mx->rows[0] + 1;
		do_tool_action(TC_SEL_START,
				bounded(row[0], 0, mem_width - 1),
				bounded(row[1], 0, mem_height - 1), 0);
		if (rows > 1) row = mx->rows[1] + 1;
		do_tool_action(TC_SEL_TO,
				bounded(row[0], 0, mem_width - 1),
				bounded(row[1], 0, mem_height - 1), 0);
		do_tool_action(TC_SEL_STOP, 0, 0, 0);
		res = 1;
		break;
	case TOOL_POLYGON:
		if ((mx->ncols != 2) || (mx->mincols != 2) ||
			(mx->fractcol >= 0)) break; // Error
		select_poly(mx);
		res = 1;
		break;
	default: /* Regular painting tools */
		if ((mx->ncols > 3) || (mx->mincols < 2) ||
			((mx->fractcol >= 0) && (mx->fractcol != 2))) break; // Error
		fr = mx->fractcol == 2;
		p = MAX_PRESSURE;
		for (i = 0; i < rows; i++)
		{
			row = mx->rows[i];
			if (row[0] > 2) p = bounded(fr ? row[3] :
				(row[3] * MAX_PRESSURE) / 100, 0, MAX_PRESSURE);
			do_tool_action(TC_PAINT | TCF_PRES | TCF_ONCE,
				bounded(row[1], 0, mem_width - 1),
				bounded(row[2], 0, mem_height - 1), p);
		}
		res = 1;
		break;
	}

	if (res) /* Do as for button release */
	{
		tool_done();
		update_menus();
	}
	return (res);
}

void *scriptbar_code[] = {
	EVENT(MULTI, tool_command), ALTNAME(""), // for tools toolbar
	RET
};

static float new_zoom(int mode, float zoom)
{
	return (!mode ? // Zoom in
		(zoom >= 1 ? zoom + 1 : 1.0 / (rint(1.0 / zoom) - 1)) :
		mode == -1 ? // Zoom out
		(zoom > 1 ? zoom - 1 : 1.0 / (rint(1.0 / zoom) + 1)) :
		mode > 0 ? mode : -1.0 / mode); // Zoom to given factor/divisor
}

void action_dispatch(int action, int mode, int state, int kbd)
{
	int change = mode & 1 ? mem_nudge : 1, dir = (mode >> 1) - 1;

	switch (action)
	{
	case ACT_QUIT:
		quit_all(mode); break;
	case ACT_ZOOM:
		align_size(new_zoom(mode, can_zoom));
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
	case ACT_ESC: do_act_esc(); break;
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
		dir = script_idx(action);
		if (!mode && (dir < 0)); // Nothing to do
		else if (mem_channel == CHN_IMAGE)
		{
			mode = mode ? mode + mem_col_[action] : dir;
			if ((mode >= 0) && (mode < mem_cols))
				mem_col_[action] = mode;
			mem_col_24[action] = mem_pal[mem_col_[action]];
		}
		else
		{
			mode = mode ? mode + channel_col_[action][mem_channel] : dir;
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
		vw_align_size(new_zoom(mode, vw_zoom));
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
		pressed_paste(mode);
		if (script_cmds && (marq_status >= MARQUEE_PASTE))
			script_paste(mode); // Move and commit
		break;
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
		else if (cmd_checkv(menu_slots[MENU_DOCK], SLOT_SENSITIVE))
			cmd_set(menu_slots[MENU_DOCK], !show_dock);
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
		mem_mask_setv(NULL, script_idx(mode + 0x100), mode);
		update_stuff(UPD_CMASK);
		break;
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
		/* "Tool of last resort" for when shortcuts don't work */
		cmd_reset(main_keys, NULL); break;
	case ACT_MODE:
		mode_change(mode, state); break;
	case ACT_LR_SHIFT:
		shift_layer(mode); break;
	case ACT_LR_CENTER:
		layer_press_centre(); break;
	case ACT_SCRIPT:
		do_script(mode); break;
	case ACT_RUN_SCRIPT:
		if (!mode) load_script();
		else launch_script(mode);
		break;
	case DLG_BRCOSA:
		pressed_brcosa(NULL); break;
	case DLG_CHOOSER:
		if (!script_cmds) choose_pattern(mode);
		else if (mode == CHOOSE_COLOR) script_ab();
		else script_bp(mode);
		break;
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
		if (mode) cmd_set(menu_slots[MENU_LAYER], FALSE); // Closed by toolbar
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
	case DLG_CLONE:
		clone_settings(); break;
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
	case DLG_SCRIPT:
		pressed_script(); break;
	case DLG_LASSO:
		lasso_settings(); break;
	case DLG_KEYS:
		keys_selector(); break;
	case FILT_2RGB:
		if (mem_img_bpp == 1) pressed_convert_rgb();
		// Allow a noop in script mode
		else if (script_cmds) spot_undo(UNDO_PAL);
		break;
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

static void menu_action(void *dt, void **wdata, int what, void **where)
{
	int act_m = TOOL_ID(where), res = TRUE;
	void *cause = cmd_read(where, dt);
	// Good for radioitems too, as all action codes are nonzero
	if (cause) res = *(int *)cause;

	action_dispatch(act_m >> 16, (act_m & 0xFFFF) - 0x8000, res, FALSE);
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

	mem_clip_new(w, h, 1, CMASK_IMAGE, NULL);
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
	cmd_set(dock_area, state);
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

static int menu_view, menu_layer;
static int menu_chan = ACTMOD(ACT_CHANNEL, CHN_IMAGE); // initial state

static void *main_menu_code[] = {
	REFv(main_menubar), SMARTMENU(menu_action),
	SSUBMENU(_("/_File")),
	MENUTEAR, //
	MENUITEMis(_("//New"), ACTMOD(DLG_NEW, 0), XPM_ICON(new)),
		SHORTCUT(n, C),
	MENUITEMis(_("//Open ..."), ACTMOD(DLG_FSEL, FS_PNG_LOAD), XPM_ICON(open)),
		SHORTCUT(o, C),
	MENUITEMis(_("//Save"), ACTMOD(ACT_SAVE, 0), XPM_ICON(save)),
		SHORTCUT(s, C),
	MENUITEMs(_("//Save As ..."), ACTMOD(DLG_FSEL, FS_PNG_SAVE)),
	MENUSEP, //
	MENUITEMs(_("//Export Undo Images ..."), ACTMOD(DLG_FSEL, FS_EXPORT_UNDO)),
		ACTMAP(NEED_UNDO),
	MENUITEMs(_("//Export Undo Images (reversed) ..."), ACTMOD(DLG_FSEL, FS_EXPORT_UNDO2)),
		ACTMAP(NEED_UNDO),
	MENUITEMs(_("//Export ASCII Art ..."), ACTMOD(DLG_FSEL, FS_EXPORT_ASCII)),
		ACTMAP(NEED_IDX),
	MENUITEMs(_("//Export Animated GIF ..."), ACTMOD(DLG_FSEL, FS_EXPORT_GIF)),
		ACTMAP(NEED_IDX),
	MENUSEP, //
	SUBMENU(_("//Actions")),
	MENUTEAR, ///
	REFv(menu_slots[MENU_FACTION1]),
	MENUITEM("///1", ACTMOD(ACT_FACTION, 1)),
	REFv(menu_slots[MENU_FACTION2]),
	MENUITEM("///2", ACTMOD(ACT_FACTION, 2)),
	REFv(menu_slots[MENU_FACTION3]),
	MENUITEM("///3", ACTMOD(ACT_FACTION, 3)),
	REFv(menu_slots[MENU_FACTION4]),
	MENUITEM("///4", ACTMOD(ACT_FACTION, 4)),
	REFv(menu_slots[MENU_FACTION5]),
	MENUITEM("///5", ACTMOD(ACT_FACTION, 5)),
	REFv(menu_slots[MENU_FACTION6]),
	MENUITEM("///6", ACTMOD(ACT_FACTION, 6)),
	REFv(menu_slots[MENU_FACTION7]),
	MENUITEM("///7", ACTMOD(ACT_FACTION, 7)),
	REFv(menu_slots[MENU_FACTION8]),
	MENUITEM("///8", ACTMOD(ACT_FACTION, 8)),
	REFv(menu_slots[MENU_FACTION9]),
	MENUITEM("///9", ACTMOD(ACT_FACTION, 9)),
	REFv(menu_slots[MENU_FACTION10]),
	MENUITEM("///10", ACTMOD(ACT_FACTION, 10)),
	REFv(menu_slots[MENU_FACTION11]),
	MENUITEM("///11", ACTMOD(ACT_FACTION, 11)),
	REFv(menu_slots[MENU_FACTION12]),
	MENUITEM("///12", ACTMOD(ACT_FACTION, 12)),
	REFv(menu_slots[MENU_FACTION13]),
	MENUITEM("///13", ACTMOD(ACT_FACTION, 13)),
	REFv(menu_slots[MENU_FACTION14]),
	MENUITEM("///14", ACTMOD(ACT_FACTION, 14)),
	REFv(menu_slots[MENU_FACTION15]),
	MENUITEM("///15", ACTMOD(ACT_FACTION, 15)),
	REFv(menu_slots[MENU_FACTION_S]),
	MENUSEPr, ///
	MENUITEM(_("///Configure"), ACTMOD(DLG_FACTIONS, 0)),
	WDONE,
	REFv(menu_slots[MENU_RECENT_S]),
	MENUSEPr, //
	REFv(menu_slots[MENU_RECENT1]),
	MENUITEM("//1", ACTMOD(ACT_LOAD_RECENT, 1)),
		SHORTCUT(F1, CS),
	REFv(menu_slots[MENU_RECENT2]),
	MENUITEM("//2", ACTMOD(ACT_LOAD_RECENT, 2)),
		SHORTCUT(F2, CS),
	REFv(menu_slots[MENU_RECENT3]),
	MENUITEM("//3", ACTMOD(ACT_LOAD_RECENT, 3)),
		SHORTCUT(F3, CS),
	REFv(menu_slots[MENU_RECENT4]),
	MENUITEM("//4", ACTMOD(ACT_LOAD_RECENT, 4)),
		SHORTCUT(F4, CS),
	REFv(menu_slots[MENU_RECENT5]),
	MENUITEM("//5", ACTMOD(ACT_LOAD_RECENT, 5)),
		SHORTCUT(F5, CS),
	REFv(menu_slots[MENU_RECENT6]),
	MENUITEM("//6", ACTMOD(ACT_LOAD_RECENT, 6)),
		SHORTCUT(F6, CS),
	REFv(menu_slots[MENU_RECENT7]),
	MENUITEM("//7", ACTMOD(ACT_LOAD_RECENT, 7)),
		SHORTCUT(F7, CS),
	REFv(menu_slots[MENU_RECENT8]),
	MENUITEM("//8", ACTMOD(ACT_LOAD_RECENT, 8)),
		SHORTCUT(F8, CS),
	REFv(menu_slots[MENU_RECENT9]),
	MENUITEM("//9", ACTMOD(ACT_LOAD_RECENT, 9)),
		SHORTCUT(F9, CS),
	REFv(menu_slots[MENU_RECENT10]),
	MENUITEM("//10", ACTMOD(ACT_LOAD_RECENT, 10)),
		SHORTCUT(F10, CS),
	REFv(menu_slots[MENU_RECENT11]),
	MENUITEM("//11", ACTMOD(ACT_LOAD_RECENT, 11)),
	REFv(menu_slots[MENU_RECENT12]),
	MENUITEM("//12", ACTMOD(ACT_LOAD_RECENT, 12)),
	REFv(menu_slots[MENU_RECENT13]),
	MENUITEM("//13", ACTMOD(ACT_LOAD_RECENT, 13)),
	REFv(menu_slots[MENU_RECENT14]),
	MENUITEM("//14", ACTMOD(ACT_LOAD_RECENT, 14)),
	REFv(menu_slots[MENU_RECENT15]),
	MENUITEM("//15", ACTMOD(ACT_LOAD_RECENT, 15)),
	REFv(menu_slots[MENU_RECENT16]),
	MENUITEM("//16", ACTMOD(ACT_LOAD_RECENT, 16)),
	REFv(menu_slots[MENU_RECENT17]),
	MENUITEM("//17", ACTMOD(ACT_LOAD_RECENT, 17)),
	REFv(menu_slots[MENU_RECENT18]),
	MENUITEM("//18", ACTMOD(ACT_LOAD_RECENT, 18)),
	REFv(menu_slots[MENU_RECENT19]),
	MENUITEM("//19", ACTMOD(ACT_LOAD_RECENT, 19)),
	REFv(menu_slots[MENU_RECENT20]),
	MENUITEM("//20", ACTMOD(ACT_LOAD_RECENT, 20)),
	MENUSEP, //
	MENUITEMs(_("//Quit"), ACTMOD(ACT_QUIT, 1)),
		SHORTCUT(q, C),
	WDONE,
	SSUBMENU(_("/_Edit")),
	MENUTEAR, //
	MENUITEMis(_("//Undo"), ACTMOD(ACT_DO_UNDO, 0), XPM_ICON(undo)),
		ACTMAP(NEED_UNDO), SHORTCUT(z, C),
	MENUITEMis(_("//Redo"), ACTMOD(ACT_DO_UNDO, 1), XPM_ICON(redo)),
		ACTMAP(NEED_REDO), SHORTCUT(r, C),
	MENUSEP, //
	MENUITEMis(_("//Cut"), ACTMOD(ACT_COPY, 1), XPM_ICON(cut)),
		ACTMAP(NEED_SEL2), SHORTCUT(x, C),
	MENUITEMis(_("//Copy"), ACTMOD(ACT_COPY, 0), XPM_ICON(copy)),
		ACTMAP(NEED_SEL2), SHORTCUT(c, C),
	MENUITEMs(_("//Copy To Palette"), ACTMOD(ACT_COPY_PAL, 0)),
		ACTMAP(NEED_PSEL),
	MENUITEMis(_("//Paste To Centre"), ACTMOD(ACT_PASTE, 1), XPM_ICON(paste)),
		ACTMAP(NEED_CLIP), SHORTCUT(v, C),
	MENUITEMs(_("//Paste To New Layer"), ACTMOD(ACT_LR_ADD, LR_PASTE)),
		ACTMAP(NEED_PCLIP), SHORTCUT(v, CS),
	MENUITEMs(_("//Paste"), ACTMOD(ACT_PASTE, 0)),
		ACTMAP(NEED_CLIP), SHORTCUT(k, C),
	MENUITEMis(_("//Paste Text"), ACTMOD(DLG_TEXT, 0), XPM_ICON(text)),
		SHORTCUT(t, S),
		IFvx(cmd_mode, 1), // Disable GUI-only text renderer
			UNLESSv(texteng_con), ACTMAP(NEED_SKIP),
		ENDIF(1),
#ifdef U_FREETYPE
	MENUITEMs(_("//Paste Text (FreeType)"), ACTMOD(DLG_TEXT_FT, 0)),
		SHORTCUT(t, 0),
#endif
	MENUITEMs(_("//Paste Palette"), ACTMOD(ACT_PASTE_PAL, 0)),
	MENUSEP, //
	SUBMENU(_("//Load Clipboard")),
	MENUTEAR, ///
	MENUITEMs("///1", ACTMOD(ACT_LOAD_CLIP, 1)),
		SHORTCUT(F1, S),
	MENUITEMs("///2", ACTMOD(ACT_LOAD_CLIP, 2)),
		SHORTCUT(F2, S),
	MENUITEMs("///3", ACTMOD(ACT_LOAD_CLIP, 3)),
		SHORTCUT(F3, S),
	MENUITEMs("///4", ACTMOD(ACT_LOAD_CLIP, 4)),
		SHORTCUT(F4, S),
	MENUITEMs("///5", ACTMOD(ACT_LOAD_CLIP, 5)),
		SHORTCUT(F5, S),
	MENUITEMs("///6", ACTMOD(ACT_LOAD_CLIP, 6)),
		SHORTCUT(F6, S),
	MENUITEMs("///7", ACTMOD(ACT_LOAD_CLIP, 7)),
		SHORTCUT(F7, S),
	MENUITEMs("///8", ACTMOD(ACT_LOAD_CLIP, 8)),
		SHORTCUT(F8, S),
	MENUITEMs("///9", ACTMOD(ACT_LOAD_CLIP, 9)),
		SHORTCUT(F9, S),
	MENUITEMs("///10", ACTMOD(ACT_LOAD_CLIP, 10)),
		SHORTCUT(F10, S),
	MENUITEMs("///11", ACTMOD(ACT_LOAD_CLIP, 11)),
		SHORTCUT(F11, S),
	MENUITEMs("///12", ACTMOD(ACT_LOAD_CLIP, 12)),
		SHORTCUT(F12, S),
	WDONE,
	SUBMENU(_("//Save Clipboard")),
	MENUTEAR, ///
	MENUITEMs("///1", ACTMOD(ACT_SAVE_CLIP, 1)),
		ACTMAP(NEED_CLIP), SHORTCUT(F1, C),
	MENUITEMs("///2", ACTMOD(ACT_SAVE_CLIP, 2)),
		ACTMAP(NEED_CLIP), SHORTCUT(F2, C),
	MENUITEMs("///3", ACTMOD(ACT_SAVE_CLIP, 3)),
		ACTMAP(NEED_CLIP), SHORTCUT(F3, C),
	MENUITEMs("///4", ACTMOD(ACT_SAVE_CLIP, 4)),
		ACTMAP(NEED_CLIP), SHORTCUT(F4, C),
	MENUITEMs("///5", ACTMOD(ACT_SAVE_CLIP, 5)),
		ACTMAP(NEED_CLIP), SHORTCUT(F5, C),
	MENUITEMs("///6", ACTMOD(ACT_SAVE_CLIP, 6)),
		ACTMAP(NEED_CLIP), SHORTCUT(F6, C),
	MENUITEMs("///7", ACTMOD(ACT_SAVE_CLIP, 7)),
		ACTMAP(NEED_CLIP), SHORTCUT(F7, C),
	MENUITEMs("///8", ACTMOD(ACT_SAVE_CLIP, 8)),
		ACTMAP(NEED_CLIP), SHORTCUT(F8, C),
	MENUITEMs("///9", ACTMOD(ACT_SAVE_CLIP, 9)),
		ACTMAP(NEED_CLIP), SHORTCUT(F9, C),
	MENUITEMs("///10", ACTMOD(ACT_SAVE_CLIP, 10)),
		ACTMAP(NEED_CLIP), SHORTCUT(F10, C),
	MENUITEMs("///11", ACTMOD(ACT_SAVE_CLIP, 11)),
		ACTMAP(NEED_CLIP), SHORTCUT(F11, C),
	MENUITEMs("///12", ACTMOD(ACT_SAVE_CLIP, 12)),
		ACTMAP(NEED_CLIP), SHORTCUT(F12, C),
	WDONE,
	MENUITEM(_("//Import Clipboard from System"), ACTMOD(ACT_LOAD_CLIP, -1)),
	MENUITEM(_("//Export Clipboard to System"), ACTMOD(ACT_SAVE_CLIP, -1)),
		ACTMAP(NEED_CLIP),
	MENUSEP, //
	MENUITEMs(_("//Choose Pattern ..."), ACTMOD(DLG_CHOOSER, CHOOSE_PATTERN)),
		SHORTCUT(F2, 0),
	MENUITEMs(_("//Choose Brush ..."), ACTMOD(DLG_CHOOSER, CHOOSE_BRUSH)),
		SHORTCUT(F3, 0),
	MENUITEMs(_("//Choose Colour ..."), ACTMOD(DLG_CHOOSER, CHOOSE_COLOR)),
		SHORTCUT(e, 0),
	// for scripting
	uMENUITEMs("//layers", ACTMOD(ACT_SCRIPT, TOOLBAR_LAYERS)),
	uMENUITEMs("//settings", ACTMOD(ACT_SCRIPT, TOOLBAR_SETTINGS)),
	uMENUITEMs("//tools", ACTMOD(ACT_SCRIPT, TOOLBAR_TOOLS)),
	WDONE,
	SSUBMENU(_("/_View")),
	MENUTEAR, //
	MENUCHECKv(_("//Show Main Toolbar"), ACTMOD(ACT_TBAR, TOOLBAR_MAIN),
		toolbar_status[TOOLBAR_MAIN]), SHORTCUT(F5, 0),
	MENUCHECKv(_("//Show Tools Toolbar"), ACTMOD(ACT_TBAR, TOOLBAR_TOOLS),
		toolbar_status[TOOLBAR_TOOLS]), SHORTCUT(F6, 0),
	REFv(menu_slots[MENU_TBSET]),
	MENUCHECKv(_("//Show Settings Toolbar"), ACTMOD(ACT_TBAR, TOOLBAR_SETTINGS),
		toolbar_status[TOOLBAR_SETTINGS]), SHORTCUT(F7, 0),
	REFv(menu_slots[MENU_DOCK]),
	MENUCHECKv(_("//Show Dock"), ACTMOD(ACT_DOCK, 0), show_dock),
		SHORTCUT(F12, 0), MTRIGGER(menu_action), // to show dock initially
	MENUCHECKv(_("//Show Palette"), ACTMOD(ACT_TBAR, TOOLBAR_PALETTE),
		toolbar_status[TOOLBAR_PALETTE]), SHORTCUT(F8, 0),
	MENUCHECKv(_("//Show Status Bar"), ACTMOD(ACT_TBAR, TOOLBAR_STATUS),
		toolbar_status[TOOLBAR_STATUS]),
	MENUSEP, //
	MENUITEM(_("//Toggle Image View"), ACTMOD(ACT_VIEW, 0)),
		SHORTCUT(Home, 0), SHORTCUT(Home, C), SHORTCUT(Home, S),
		SHORTCUT(Home, A), SHORTCUT(Home, CS), SHORTCUT(Home, CA),
		SHORTCUT(Home, SA), SHORTCUT(Home, CSA),
	MENUCHECKv(_("//Centralize Image"), ACTMOD(ACT_CENTER, 0), canvas_image_centre),
	MENUCHECKv(_("//Show Zoom Grid"), ACTMOD(ACT_GRID, 0), mem_show_grid),
	MENUCHECKvs(_("//Snap To Tile Grid"), ACTMOD(ACT_SNAP, 0), tgrid_snap),
		SHORTCUT(b, 0),
	MENUITEM(_("//Configure Grid ..."), ACTMOD(DLG_COLORS, COLSEL_GRID)),
	MENUITEM(_("//Tracing Image ..."), ACTMOD(DLG_TRACE, 0)),
	MENUSEP, //
	REFv(menu_slots[MENU_VIEW]),
	MENUCHECKv(_("//View Window"), ACTMOD(ACT_VWWIN, 0), menu_view),
		SHORTCUT(v, 0),
	MENUCHECKv(_("//Horizontal Split"), ACTMOD(ACT_VWSPLIT, 0), view_vsplit),
		SHORTCUT(h, 0),
	MENUCHECKv(_("//Focus View Window"), ACTMOD(ACT_VWFOCUS, 0), vw_focus_on),
	MENUSEP, //
	MENUITEMi(_("//Pan Window"), ACTMOD(ACT_PAN, 0), XPM_ICON(pan)),
		SHORTCUT(End, 0),
	REFv(menu_slots[MENU_LAYER]),
	MENUCHECKv(_("//Layers Window"), ACTMOD(DLG_LAYERS, 0), menu_layer),
		SHORTCUT(l, 0),
	WDONE,
	SSUBMENU(_("/_Image")),
	MENUTEAR, //
	uMENUITEMs("//Convert To RGB", ACTMOD(FILT_2RGB, 0)), // for scripting
	MENUITEM(_("//Convert To RGB"), ACTMOD(FILT_2RGB, 0)),
		ACTMAP(NEED_IDX),
	MENUITEMs(_("//Convert To Indexed ..."), ACTMOD(DLG_INDEXED, 0)),
		ACTMAP(NEED_24),
	MENUSEP, //
	MENUITEMs(_("//Scale Canvas ..."), ACTMOD(DLG_SCALE, 0)),
		SHORTCUT(Page_Up, 0),
	MENUITEMs(_("//Resize Canvas ..."), ACTMOD(DLG_SIZE, 0)),
		SHORTCUT(Page_Down, 0),
	MENUITEMs(_("//Crop"), ACTMOD(ACT_CROP, 0)),
		ACTMAP(NEED_CROP), SHORTCUT(x, CS), SHORTCUT(Delete, 0),
	MENUSEP, //
	MENUITEMs(_("//Flip Vertically"), ACTMOD(ACT_FLIP_V, 0)),
	MENUITEMs(_("//Flip Horizontally"), ACTMOD(ACT_FLIP_H, 0)),
		SHORTCUT(m, C),
	uMENUITEMs("//rotate", ACTMOD(DLG_ROTATE, 0)), // for scripting
	MENUITEMs(_("//Rotate Clockwise"), ACTMOD(ACT_ROTATE, 0)),
	MENUITEMs(_("//Rotate Anti-Clockwise"), ACTMOD(ACT_ROTATE, 1)),
	MENUITEMs(_("//Free Rotate ..."), ACTMOD(DLG_ROTATE, 0)),
	MENUITEMs(_("//Skew ..."), ACTMOD(DLG_SKEW, 0)),
	MENUSEP, //
// !!! Maybe support indexed mode too, later
	MENUITEMs(_("//Segment ..."), ACTMOD(DLG_SEGMENT, 0)),
		ACTMAP(NEED_24),
	uMENUITEMs("//Script ...", ACTMOD(ACT_RUN_SCRIPT, 0)), // for scripting
	REFv(menu_slots[MENU_SCRIPT]),
	MENUITEM(_("//Script ..."), ACTMOD(DLG_SCRIPT, 0)),
	REFv(menu_slots[MENU_SCRIPT_M]),
	SUBMENU(_("//Scripts")),
	MENUTEAR, ///
	REFv(menu_slots[MENU_SCRIPT1]),
	MENUITEMs("///1", ACTMOD(ACT_RUN_SCRIPT, 1)),
	REFv(menu_slots[MENU_SCRIPT2]),
	MENUITEMs("///2", ACTMOD(ACT_RUN_SCRIPT, 2)),
	REFv(menu_slots[MENU_SCRIPT3]),
	MENUITEMs("///3", ACTMOD(ACT_RUN_SCRIPT, 3)),
	REFv(menu_slots[MENU_SCRIPT4]),
	MENUITEMs("///4", ACTMOD(ACT_RUN_SCRIPT, 4)),
	REFv(menu_slots[MENU_SCRIPT5]),
	MENUITEMs("///5", ACTMOD(ACT_RUN_SCRIPT, 5)),
	REFv(menu_slots[MENU_SCRIPT6]),
	MENUITEMs("///6", ACTMOD(ACT_RUN_SCRIPT, 6)),
	REFv(menu_slots[MENU_SCRIPT7]),
	MENUITEMs("///7", ACTMOD(ACT_RUN_SCRIPT, 7)),
	REFv(menu_slots[MENU_SCRIPT8]),
	MENUITEMs("///8", ACTMOD(ACT_RUN_SCRIPT, 8)),
	REFv(menu_slots[MENU_SCRIPT9]),
	MENUITEMs("///9", ACTMOD(ACT_RUN_SCRIPT, 9)),
	REFv(menu_slots[MENU_SCRIPT10]),
	MENUITEMs("///10", ACTMOD(ACT_RUN_SCRIPT, 10)),
	MENUSEP, ///
	MENUITEM(_("///Configure"), ACTMOD(DLG_SCRIPT, 0)),
	WDONE,
	MENUSEP, //
	MENUITEM(_("//Information ..."), ACTMOD(DLG_INFO, 0)),
		SHORTCUT(i, C),
	REFv(menu_slots[MENU_PREFS]),
	MENUITEMs(_("//Preferences ..."), ACTMOD(DLG_PREFS, 0)),
		SHORTCUT(p, C),
	WDONE,
	SSUBMENU(_("/_Selection")),
	MENUTEAR, //
	MENUITEMs(_("//Select All"), ACTMOD(ACT_SELECT, 1)),
		SHORTCUT(a, C),
	MENUITEMs(_("//Select None (Esc)"), ACTMOD(ACT_SELECT, 0)),
		ACTMAP(NEED_MARQ), SHORTCUT(a, CS),
	MENUITEMis(_("//Lasso Selection"), ACTMOD(ACT_LASSO, 0), XPM_ICON(lasso)),
		ACTMAP(NEED_LAS2), SHORTCUT(j, 0),
	MENUITEMs(_("//Lasso Selection Cut"), ACTMOD(ACT_LASSO, 1)),
		ACTMAP(NEED_LASSO),
	MENUSEP, //
	MENUITEMis(_("//Outline Selection"), ACTMOD(ACT_OUTLINE, 0), XPM_ICON(rect1)),
		ACTMAP(NEED_SEL2), SHORTCUT(t, C),
	MENUITEMis(_("//Fill Selection"), ACTMOD(ACT_OUTLINE, 1), XPM_ICON(rect2)),
		ACTMAP(NEED_SEL2), SHORTCUT(t, CS),
	MENUITEMis(_("//Outline Ellipse"), ACTMOD(ACT_ELLIPSE, 0), XPM_ICON(ellipse2)),
		ACTMAP(NEED_SEL), SHORTCUT(l, C),
	MENUITEMis(_("//Fill Ellipse"), ACTMOD(ACT_ELLIPSE, 1), XPM_ICON(ellipse)),
		ACTMAP(NEED_SEL), SHORTCUT(l, CS),
	MENUSEP, //
	MENUITEMis(_("//Flip Vertically"), ACTMOD(ACT_SEL_FLIP_V, 0), XPM_ICON(flip_vs)),
		ACTMAP(NEED_CLIP),
	MENUITEMis(_("//Flip Horizontally"), ACTMOD(ACT_SEL_FLIP_H, 0), XPM_ICON(flip_hs)),
		ACTMAP(NEED_CLIP),
	MENUITEMis(_("//Rotate Clockwise"), ACTMOD(ACT_SEL_ROT, 0), XPM_ICON(rotate_cs)),
		ACTMAP(NEED_CLIP),
	MENUITEMis(_("//Rotate Anti-Clockwise"), ACTMOD(ACT_SEL_ROT, 1), XPM_ICON(rotate_as)),
		ACTMAP(NEED_CLIP),
	MENUSEP, //
	MENUITEMs(_("//Horizontal Ramp"), ACTMOD(ACT_RAMP, 0)),
		ACTMAP(NEED_SEL),
	MENUITEMs(_("//Vertical Ramp"), ACTMOD(ACT_RAMP, 1)),
		ACTMAP(NEED_SEL),
	MENUSEP, //
	MENUITEMs(_("//Alpha Blend A,B"), ACTMOD(ACT_SEL_ALPHA_AB, 0)),
		ACTMAP(NEED_ACLIP),
	MENUITEMs(_("//Move Alpha to Mask"), ACTMOD(ACT_SEL_ALPHAMASK, 0)),
		ACTMAP(NEED_CLIP),
	MENUITEMs(_("//Mask Colour A,B"), ACTMOD(ACT_SEL_MASK_AB, 0)),
		ACTMAP(NEED_CLIP),
	MENUITEMs(_("//Unmask Colour A,B"), ACTMOD(ACT_SEL_MASK_AB, 255)),
		ACTMAP(NEED_CLIP),
	MENUITEMs(_("//Mask All Colours"), ACTMOD(ACT_SEL_MASK, 0)),
		ACTMAP(NEED_CLIP),
	MENUITEMs(_("//Clear Mask"), ACTMOD(ACT_SEL_MASK, 255)),
		ACTMAP(NEED_CLIP),
	WDONE,
	SSUBMENU(_("/_Palette")),
	uMENUITEMs("//a", ACTMOD(ACT_A, 0)), // for scripting
	uMENUITEMs("//b", ACTMOD(ACT_B, 0)), // for scripting
	MENUTEAR, //
	MENUITEMis(_("//Load ..."), ACTMOD(DLG_FSEL, FS_PALETTE_LOAD), XPM_ICON(open)),
	MENUITEMis(_("//Save As ..."), ACTMOD(DLG_FSEL, FS_PALETTE_SAVE), XPM_ICON(save)),
	MENUITEMs(_("//Load Default"), ACTMOD(ACT_PAL_DEF, 0)),
	MENUSEP, //
	MENUITEMs(_("//Mask All"), ACTMOD(ACT_PAL_MASK, 1)),
	MENUITEMs(_("//Mask None"), ACTMOD(ACT_PAL_MASK, 0)),
	MENUSEP, //
	MENUITEMs(_("//Swap A & B"), ACTMOD(ACT_SWAP_AB, 0)),
		SHORTCUT(x, 0),
	MENUITEMs(_("//Edit Colour A & B ..."), ACTMOD(DLG_COLORS, COLSEL_EDIT_AB)),
		SHORTCUT(e, C),
	MENUITEMs(_("//Dither A"), ACTMOD(ACT_DITHER_A, 0)),
		ACTMAP(NEED_24),
	MENUITEMs(_("//Palette Editor ..."), ACTMOD(DLG_COLORS, COLSEL_EDIT_ALL)),
		SHORTCUT(w, C),
	MENUITEMs(_("//Set Palette Size ..."), ACTMOD(DLG_PAL_SIZE, 0)),
	MENUITEMs(_("//Merge Duplicate Colours"), ACTMOD(ACT_PAL_MERGE, 0)),
		ACTMAP(NEED_IDX),
	MENUITEMs(_("//Remove Unused Colours"), ACTMOD(ACT_PAL_CLEAN, 0)),
		ACTMAP(NEED_IDX),
	MENUSEP, //
	MENUITEMs(_("//Create Quantized ..."), ACTMOD(DLG_INDEXED, 1)),
		ACTMAP(NEED_24),
	MENUSEP, //
	MENUITEMs(_("//Sort Colours ..."), ACTMOD(DLG_PAL_SORT, 0)),
	MENUITEMs(_("//Palette Shifter ..."), ACTMOD(DLG_PAL_SHIFTER, 0)),
	MENUITEMs(_("//Pick Gradient ..."), ACTMOD(DLG_PICK_GRAD, 0)),
	WDONE,
	SSUBMENU(_("/Effe_cts")),
	MENUTEAR, //
	MENUITEMis(_("//Transform Colour ..."), ACTMOD(DLG_BRCOSA, 0), XPM_ICON(brcosa)),
		SHORTCUT(c, CS), SHORTCUT(Insert, 0),
	MENUITEMs(_("//Invert"), ACTMOD(FILT_INVERT, 0)),
		SHORTCUT(i, CS),
	MENUITEMs(_("//Greyscale"), ACTMOD(FILT_GREY, 0)),
		SHORTCUT(g, C),
	MENUITEMs(_("//Greyscale (Gamma corrected)"), ACTMOD(FILT_GREY, 1)),
		SHORTCUT(g, CS),
	SUBMENU(_("//Isometric Transformation")),
	MENUTEAR, ///
	MENUITEMs(_("///Left Side Down"), ACTMOD(ACT_ISOMETRY, 0)),
	MENUITEMs(_("///Right Side Down"), ACTMOD(ACT_ISOMETRY, 1)),
	MENUITEMs(_("///Top Side Right"), ACTMOD(ACT_ISOMETRY, 2)),
	MENUITEMs(_("///Bottom Side Right"), ACTMOD(ACT_ISOMETRY, 3)),
	WDONE,
	MENUSEP, //
	MENUITEMs(_("//Edge Detect ..."), ACTMOD(FILT_EDGE, 0)),
		ACTMAP(NEED_NOIDX),
	MENUITEMs(_("//Difference of Gaussians ..."), ACTMOD(FILT_DOG, 0)),
		ACTMAP(NEED_NOIDX),
	MENUITEMs(_("//Sharpen ..."), ACTMOD(FILT_SHARPEN, 0)),
		ACTMAP(NEED_NOIDX),
	MENUITEMs(_("//Unsharp Mask ..."), ACTMOD(FILT_UNSHARP, 0)),
		ACTMAP(NEED_NOIDX),
	MENUITEMs(_("//Soften ..."), ACTMOD(FILT_SOFTEN, 0)),
		ACTMAP(NEED_NOIDX),
	MENUITEMs(_("//Gaussian Blur ..."), ACTMOD(FILT_GAUSS, 0)),
		ACTMAP(NEED_NOIDX),
	MENUITEMs(_("//Kuwahara-Nagao Blur ..."), ACTMOD(FILT_KUWAHARA, 0)),
		ACTMAP(NEED_24),
	MENUITEMs(_("//Emboss"), ACTMOD(FILT_FX, FX_EMBOSS)),
		ACTMAP(NEED_NOIDX),
	MENUITEMs(_("//Dilate"), ACTMOD(FILT_FX, FX_DILATE)),
		ACTMAP(NEED_NOIDX),
	MENUITEMs(_("//Erode"), ACTMOD(FILT_FX, FX_ERODE)),
		ACTMAP(NEED_NOIDX),
	MENUSEP, //
	MENUITEMs(_("//Bacteria ..."), ACTMOD(FILT_BACT, 0)),
		ACTMAP(NEED_IDX),
	WDONE,
	SSUBMENU(_("/Cha_nnels")),
	MENUTEAR, //
	MENUITEMs(_("//New ..."), ACTMOD(ACT_CHANNEL, -1)),
	MENUITEMis(_("//Load ..."), ACTMOD(DLG_FSEL, FS_CHANNEL_LOAD), XPM_ICON(open)),
	MENUITEMis(_("//Save As ..."), ACTMOD(DLG_FSEL, FS_CHANNEL_SAVE), XPM_ICON(save)),
	MENUITEMs(_("//Delete ..."), ACTMOD(DLG_CHN_DEL, -1)),
		ACTMAP(NEED_CHAN),
	MENUSEP, //
	REFv(menu_slots[MENU_CHAN0]),
	MENURITEMvs(_("//Edit Image"), ACTMOD(ACT_CHANNEL, CHN_IMAGE), menu_chan),
		SHORTCUT(1, S), SHORTCUT(KP_1, S),
	REFv(menu_slots[MENU_CHAN1]),
	MENURITEMvs(_("//Edit Alpha"), ACTMOD(ACT_CHANNEL, CHN_ALPHA), menu_chan),
		SHORTCUT(2, S), SHORTCUT(KP_2, S),
	REFv(menu_slots[MENU_CHAN2]),
	MENURITEMvs(_("//Edit Selection"), ACTMOD(ACT_CHANNEL, CHN_SEL), menu_chan),
		SHORTCUT(3, S), SHORTCUT(KP_3, S),
	REFv(menu_slots[MENU_CHAN3]),
	MENURITEMvs(_("//Edit Mask"), ACTMOD(ACT_CHANNEL, CHN_MASK), menu_chan),
		SHORTCUT(4, S), SHORTCUT(KP_4, S),
	MENUSEP, //
	REFv(menu_slots[MENU_DCHAN0]),
	MENUCHECKv(_("//Hide Image"), ACTMOD(ACT_SET_OVERLAY, 1), hide_image),
		SHORTCUT(h, C),
	REFv(menu_slots[MENU_DCHAN1]),
	MENUCHECKvs(_("//Disable Alpha"), ACTMOD(ACT_CHN_DIS, CHN_ALPHA),
		channel_dis[CHN_ALPHA]),
	REFv(menu_slots[MENU_DCHAN2]),
	MENUCHECKvs(_("//Disable Selection"), ACTMOD(ACT_CHN_DIS, CHN_SEL),
		channel_dis[CHN_SEL]),
	REFv(menu_slots[MENU_DCHAN3]),
	MENUCHECKvs(_("//Disable Mask"), ACTMOD(ACT_CHN_DIS, CHN_MASK),
		channel_dis[CHN_MASK]),
	MENUSEP, //
	MENUCHECKvs(_("//Couple RGBA Operations"), ACTMOD(ACT_SET_RGBA, 0), RGBA_mode),
	MENUITEMs(_("//Threshold ..."), ACTMOD(FILT_THRES, 0)),
	MENUITEMs(_("//Unassociate Alpha"), ACTMOD(FILT_UALPHA, 0)),
		ACTMAP(NEED_RGBA),
	MENUSEP, //
	REFv(menu_slots[MENU_OALPHA]),
	MENUCHECKv(_("//View Alpha as an Overlay"), ACTMOD(ACT_SET_OVERLAY, 0), overlay_alpha),
	MENUITEM(_("//Configure Overlays ..."), ACTMOD(DLG_COLORS, COLSEL_OVERLAYS)),
	WDONE,
	SSUBMENU(_("/_Layers")),
	MENUTEAR, //
	MENUITEMis(_("//New Layer"), ACTMOD(ACT_LR_ADD, LR_NEW), XPM_ICON(new)),
	MENUITEMis(_("//Save"), ACTMOD(ACT_LR_SAVE, 0), XPM_ICON(save)),
		SHORTCUT(s, CS),
	MENUITEMs(_("//Save As ..."), ACTMOD(DLG_FSEL, FS_LAYER_SAVE)),
	MENUITEMs(_("//Save Composite Image ..."), ACTMOD(DLG_FSEL, FS_COMPOSITE_SAVE)),
	MENUITEMs(_("//Composite to New Layer"), ACTMOD(ACT_LR_ADD, LR_COMP)),
	MENUITEMs(_("//Remove All Layers"), ACTMOD(ACT_LR_DEL, 1)),
	MENUSEP, //
	MENUITEM(_("//Configure Animation ..."), ACTMOD(DLG_ANI, 0)),
	MENUITEM(_("//Preview Animation ..."), ACTMOD(DLG_ANI_VIEW, 0)),
	MENUITEM(_("//Set Key Frame ..."), ACTMOD(DLG_ANI_KEY, 0)),
	MENUITEM(_("//Remove All Key Frames ..."), ACTMOD(DLG_ANI_KILLKEY, 0)),
	WDONE,
	SSUBMENU(_("/More...")),
	WDONE,
	ESUBMENU(_("/_Help")),
	MENUITEM(_("//Documentation"), ACTMOD(ACT_DOCS, 0)),
	REFv(menu_slots[MENU_HELP]),
	MENUITEM(_("//About"), ACTMOD(DLG_ABOUT, 0)),
		SHORTCUT(F1, 0),
	MENUSEP, //
	MENUITEM(_("//Keyboard Shortcuts ..."), ACTMOD(DLG_KEYS, 0)),
	MENUITEM(_("//Rebind Shortcut Keycodes"), ACTMOD(ACT_REBIND_KEYS, 0)),
	WDONE,
	SMDONE, // smartmenu
	ENDSCRIPT,
	RET
};

static int dock_esc(main_dd *dt, void **wdata, int what, void **where,
	key_ext *keydata)
{
	/* Pressing Escape moves focus out of dock - to nowhere */
	if (keydata->key == GDK_Escape)
	{
		cmd_setv(main_window_, NULL, WINDOW_FOCUS);
		return (TRUE);
	}
	return (FALSE);
}

static int cline_keypress(main_dd *dt, void **wdata, int what, void **where,
	key_ext *keydata)
{
	return (check_zoom_keys(wtf_pressed(keydata))); // Check HOME/zoom keys
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
	cmd_setv(dock_book, (void *)(dt->cline_d && dstate), NBOOK_TABS);

	/* Close dock if nothing left in it */
	dstate |= dt->cline_d;
	if (!dstate) cmd_set(menu_slots[MENU_DOCK], FALSE);
	cmd_sensitive(menu_slots[MENU_DOCK], dstate);
}

#define REPAINT_CANVAS_COST 512

#define WBbase main_dd
static void *main_code[] = {
///	MAIN WINDOW
	MAINWINDOW(MT_VERSION, icon_xpm, 100, 100), EVENT(CANCEL, delete_event),
	EVENT(KEY, handle_keypress),
	REF(drop), CLIPFORM(uri_list, 1),
	/* !!! Konqueror needs GDK_ACTION_MOVE to do a drop; we never accept
	 * move as a move, so have to do some non-default processing - WJ */
	DRAGDROPm(drop, NULL, parse_drag),
	WXYWH("window", 630, 400),
///	KEYMAP
	REFv(main_keys), KEYMAP(keyslot, "main"),
	CALL(keylist_code),
	REFv(dock_area), DOCK("dockSize"),
///	MENU
	CALL(main_menu_code),
///	TOOLBARS
	CALL(toolbar_code),
	XHBOX,
///	PALETTE
	CALL(toolbar_palette_code),
	XVBOX,
///	DRAWING AREA
	REFv(main_split), HVSPLIT,
//	MAIN WINDOW
	REFv(scrolledwindow_canvas), CSCROLLv(cvxy), EVENT(CHANGE, vw_focus_idle),
	REFv(drawing_canvas), CANVAS(48, 48, REPAINT_CANVAS_COST, paint_canvas),
	EVENT(CHANGE, configure_canvas),
	EVENT(XMOUSE, canvas_mouse), EVENT(RXMOUSE, canvas_mouse),
	EVENT(MXMOUSE, canvas_mouse), EVENT(CROSS, canvas_enter_leave),
	EVENT(SCROLL, canvas_scroll),
//	VIEW WINDOW
	CALL(init_view_code),
	EVENT(SCROLL, canvas_scroll), // for vw_drawing widget
	WDONE, // hvsplit
////	STATUS BAR
	REFv(toolbar_boxes[TOOLBAR_STATUS]),
	STATUSBAR, UNLESSv(toolbar_status[TOOLBAR_STATUS]), HIDDEN,
	/* Labels visibility is set later by init_status_bar() */
	REFv(label_bar[STATUS_GEOMETRY]), STLABEL(0, 0),
	REFv(label_bar[STATUS_CURSORXY]), STLABEL(90, 1),
	REFv(label_bar[STATUS_PIXELRGB]), STLABEL(0, 0),
	REFv(label_bar[STATUS_SELEGEOM]), STLABELe(0, 0),
	REFv(label_bar[STATUS_UNDOREDO]), STLABELe(70, 1),
	WDONE, // statusbar
	WDONE, // xvbox
	WDONE, // xhbox
	WDONE, // left pane
	BORDER(NBOOK, 0),
	REFv(dock_book), NBOOKr, KEEPWIDTH, WANTKEYS(dock_esc),
	IFx(cline_d, 1),
		PAGEi(XPM_ICON(cline), 0),
		BORDER(SCROLL, 0),
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
	WDONE, // right pane
	REF(clip), CLIPFORM(clip_formats, CLIP_TARGETS),
	REF(clipboard), CLIPBOARD(clip, 3, clipboard_export_fn, clipboard_import_fn),
	RAISED, WEND
};
#undef WBbase

void main_init()
{
	main_dd tdata;
	char *tdev, txt[PATHTXT];

	memset(&tdata, 0, sizeof(tdata));
	/* Prepare commandline list */
	if ((show_dock = tdata.cline_d = files_passed > 1))
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
	show_dock |= inifile_get_gboolean("showDock", FALSE);
	main_window_ = run_create_(main_code, &tdata, sizeof(tdata), script_cmds);

	recent_files = recent_files < 0 ? 0 : recent_files > 20 ? 20 : recent_files;
	update_recent_files();

	if (!tdata.cline_d) // Stops first icon in toolbar being selected
		cmd_setv(main_window_, NULL, WINDOW_FOCUS);

	init_status_bar();
	init_factions();				// Initialize file action menu
	update_script_menu();

	file_in_homedir(txt, ".clipboard", PATHBUF);
	strncpy0(mem_clip_file, inifile_get("clipFilename", txt), PATHBUF);

	set_cursor(NULL);
	change_to_tool(DEFAULT_TOOL_ICON);

	/* Skip the GUI-specific updates in commandline mode */
	if (cmd_mode) return;

	cmd_peekv(main_window_, &tdev, sizeof(tdev), WDATA_TABLET); // Check tablet state
	tablet_working = !!tdev;

	cmd_showhide(main_window_, TRUE);

	if (viewer_mode) toggle_view();
	else toolbar_showhide();
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

	if (strcmp("system", txt))
	{
		snprintf( txt2, 60, "LANGUAGE=%s", txt );
		putenv( txt2 );
		snprintf( txt2, 60, "LANG=%s", txt );
		putenv( txt2 );
		snprintf( txt2, 60, "LC_ALL=%s", txt );
		putenv( txt2 );
	}
	else txt = "";


#if GTK_MAJOR_VERSION > 1
	if (cmd_mode)
#endif
	setlocale(LC_ALL, txt);
	/* !!! Slow or not, but NLS is *really* broken on GTK+1 without it - WJ */
	// GTK+1 hates this - it really slows things down
	if (!cmd_mode) gtk_set_locale();
}
#endif

void update_titlebar()		// Update filename in titlebar
{
	static int changed = -1;
	static char *name = "";
	char txt[300], txt2[PATHTXT];


	/* Don't send needless updates */
	if (!main_window_ || ((mem_changed == changed) && (mem_filename == name)))
		return;
	changed = mem_changed;
	name = mem_filename;

	snprintf(txt, 290, "%s %s %s", MT_VERSION,
		changed ? __("(Modified)") : "-",
		name ? gtkuncpy(txt2, name, PATHTXT) : __("Untitled"));

	cmd_setv(main_window_, txt, WINDOW_TITLE);
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


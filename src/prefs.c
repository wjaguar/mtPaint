/*	prefs.c
	Copyright (C) 2005-2016 Mark Tyler and Dmitry Groshev

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
#include "canvas.h"
#include "layer.h"
#include "inifile.h"
#include "viewer.h"
#include "mainwindow.h"
#include "thread.h"

#include "prefs.h"


///	PREFERENCES WINDOW

int tablet_tool_use[3];				// Size, flow, opacity
int tablet_tool_factor[3];			// Size, flow, opacity

#ifdef U_NLS

#define PREF_LANGS 21

static char *pref_lang_ini_code[PREF_LANGS] = { "system",
	"zh_CN.utf8", "zh_TW.utf8",
	"cs_CZ", "nl_NL", "en_GB", "fr_FR",
	"gl_ES", "de_DE", "hu_HU", "it_IT",
	"ja_JP.utf8", "pl_PL", "pt_PT",
	"pt_BR", "ru_RU", "sk_SK",
	"es_ES", "sv_SE", "tl_PH", "tr_TR" };

static char *pref_langs[PREF_LANGS] = { _("Default System Language"),
	_("Chinese (Simplified)"), _("Chinese (Taiwanese)"), _("Czech"),
	_("Dutch"), _("English (UK)"), _("French"), _("Galician"), _("German"),
	_("Hungarian"), _("Italian"), _("Japanese"), _("Polish"),
	_("Portuguese"), _("Portuguese (Brazilian)"), _("Russian"), _("Slovak"),
	_("Spanish"), _("Swedish"), _("Tagalog"), _("Turkish") };

#endif


typedef struct {
	int undo_depth[3], trans[3], hot_x[3], hot_y[3];
	int confx, centre, zoom;
	int lang, script;
	char *factor_str;
	char **tiffrs, **tiffis, **tiffbs;
	void **label_tablet_pressure, **label_tablet_device;
} pref_dd;

static void tablet_update_device(pref_dd *dt)
{
	char *nm;

	cmd_peekv(main_window_, &nm, sizeof(nm), WDATA_TABLET);
	tablet_working = !!nm;
	nm = g_strdup_printf("%s = %s", __("Current Device"), nm ? nm : "NONE");
	cmd_setv(dt->label_tablet_device, nm, LABEL_VALUE);
	g_free(nm);
}

static void prefs_evt(pref_dd *dt, void **wdata, int what)
{
	char *p, oldpal[PATHBUF], oldpat[PATHBUF];

	if (what != op_EVT_CANCEL) // OK/Apply
	{
		// Preserve old values
		strncpy0(oldpal, inifile_get(DEFAULT_PAL_INI, ""), PATHBUF);
		strncpy0(oldpat, inifile_get(DEFAULT_PAT_INI, ""), PATHBUF);

		/* Read back from V-code slots */
		run_query(wdata);
		mem_undo_depth = dt->undo_depth[0];
		mem_xbm_hot_x = dt->hot_x[0];
		mem_xbm_hot_y = dt->hot_y[0];

#ifdef U_NLS
		inifile_set("languageSETTING", pref_lang_ini_code[dt->lang]);
		setup_language();
		string_init();			// Translate static strings
#endif

		inifile_set("clipFilename", mem_clip_file);
		if (strcmp(oldpal, p = inifile_get(DEFAULT_PAL_INI, "")))
			load_def_palette(p);
		if (strcmp(oldpat, p = inifile_get(DEFAULT_PAT_INI, "")))
			load_def_patterns(p);

		update_stuff(UPD_PREFS);
		/* Apply this undoable setting after everything else */
		mem_set_trans(dt->trans[0]);
	}

	if (what != op_EVT_CLICK) // OK/Cancel
	{
		run_destroy(wdata);
		cmd_sensitive(menu_slots[MENU_PREFS], TRUE);
	}
}

static int tablet_preview(pref_dd *dt, void **wdata, int what, void **where,
	mouse_ext *mouse)
{
	if (mouse->button == 1)
	{
		char txt[64];
		sprintf(txt, "%s = %.2f", __("Pressure"),
			mouse->pressure * (1.0 / MAX_PRESSURE));
		cmd_setv(dt->label_tablet_pressure, txt, LABEL_VALUE);
	}

	return (TRUE);
}

///	V-CODE

#define WBbase pref_dd
static void *pref_code[] = {
	WINDOW(_("Preferences")), // nonmodal
	MKSHRINK, // shrinkable
	BORDER(SCROLL, 0),
	XSCROLL(1, 1), // auto/auto
	WANTMAX, // max size
	BORDER(NBOOK, 0),
	NBOOKl,
	BORDER(TABLE, 10),
	BORDER(LABEL, 4), BORDER(SPIN, 4),
///	---- TAB1 - GENERAL
	PAGE(_("General")), GROUPN,
#ifdef U_THREADS
	TABLE2(4),
	TSPINv(_("Max threads (0 to autodetect)"), maxthreads, 0, 256),
#else
	TABLE2(3),
#endif
	TSPINv(_("Max memory used for undo (MB)"), mem_undo_limit, 1, 2048),
	TSPINa(_("Max undo levels"), undo_depth),
	TSPINv(_("Communal layer undo space (%)"), mem_undo_common, 0, 100),
	WDONE,
	CHECKv(_("Use gamma correction by default"), use_gamma),
	/* !!! Only processing is scriptable, interface is not */
	UNLESSx(script, 1),
	CHECKv(_("Optimize alpha chequers"), chequers_optimize),
	CHECKv(_("Disable view window transparencies"), opaque_view),
	CHECKv(_("Enable overlays by layer"), layer_overlay),
///	LANGUAGE SWITCHBOX
#ifdef U_NLS
	BORDER(OPT, 0),
	FVBOXBS(_("Language")),
	BORDER(LABEL, 0),
	MLABELc(_("Select preferred language translation\n\n"
	"You will need to restart mtPaint\nfor this to take full effect")),
	BORDER(LABEL, 4),
	OPT(pref_langs, PREF_LANGS, lang),
	WDONE,
#endif
	WDONE,
///	---- TAB2 - INTERFACE
	PAGE(_("Interface")),
	TABLE2(3),
	TSPINv(_("Greyscale backdrop"), mem_background, 0, 255),
	TSPINv(_("Selection nudge pixels"), mem_nudge, 2, MAX_WIDTH),
	TSPINv(_("Max Pan Window Size"), max_pan, 64, 256),
	WDONE,
	CHECKv(_("Display clipboard while pasting"), show_paste),
	CHECKv(_("Mouse cursor = Tool"), cursor_tool),
	CHECKb(_("Confirm Exit"), confx, "exitToggle"),
	CHECKv(_("Q key quits mtPaint"), q_quit),
	CHECKv(_("Changing tool commits paste"), paste_commit),
	CHECKb(_("Centre tool settings dialogs"), centre, "centerSettings"),
	CHECKb(_("New image sets zoom to 100%"), zoom, "zoomToggle"),
	CHECKv(_("Zoom on cursor position"), cursor_zoom),
#if GTK_MAJOR_VERSION == 2
	CHECKv(_("Mouse Scroll Wheel = Zoom"), scroll_zoom),
	CHECKv(_("Use menu icons"), show_menu_icons),
#endif
	ENDIF(1), // !script
	WDONE,
///	---- TAB3 - FILES
	PAGE(_("Files")), GROUPN,
	TABLE2(8),
	TSPINa(_("Transparency index"), trans),
	TSPINa(_("XBM X hotspot"), hot_x),
	TSPINa(_("XBM Y hotspot"), hot_y),
	TSPINv(_("JPEG Save Quality (100=High)"), jpeg_quality, 0, 100),
	TSPINv(_("JPEG2000 Compression (0=Lossless)"), jp2_rate, 0, 100),
	TSPINv(_("PNG Compression (0=None)"), png_compression, 0, 9),
	TSPINv(_("Recently Used Files"), recent_files, 0, MAX_RECENT),
	TSPINv(_("Progress bar silence limit"), silence_limit, 0, 28),
	WDONE,
	CHECKv(_("TGA RLE Compression"), tga_RLE),
	CHECKv(_("Read 16-bit TGAs as 5:6:5 BGR"), tga_565),
	CHECKv(_("Write TGAs in bottom-up row order"), tga_defdir),
	CHECKv(_("Undoable image loading"), undo_load),
#ifdef U_LCMS
	CHECKv(_("Apply colour profile"), apply_icc),
#endif
	WDONE,
#ifdef U_TIFF
///	---- TAB4 - TIFF
	PAGE("TIFF"), GROUPN,
	BORDER(OPT, 2),
	TABLE2(4),
// !!! Here also DPI (default)
	TOPTDv(_("Compression (RGB)"), tiffrs, tiff_rtype),
	TOPTDv(_("Compression (indexed)"), tiffis, tiff_itype),
	TOPTDv(_("Compression (monochrome)"), tiffbs, tiff_btype),
	TSPINv(_("LZMA2 Compression (0=None)"), lzma_preset, 0, 9),
	WDONE,
	CHECKv(_("Enable predictor"), tiff_predictor),
	WDONE,
#endif
	/* !!! Interface is not scriptable */
	UNLESSx(script, 1),
///	---- TAB5 - PATHS
	PAGE(_("Paths")),
	PATHv(_("Clipboard Files"), _("Select Clipboard File"),
		FS_CLIP_FILE, mem_clip_file),
	PATHs(_("HTML Browser Program"), _("Select Browser Program"),
		FS_SELECT_FILE, HANDBOOK_BROWSER_INI),
	PATHs(_("Location of Handbook index"), _("Select Handbook Index File"),
		FS_SELECT_FILE, HANDBOOK_LOCATION_INI),
	PATHs(_("Default Palette"), _("Select Default Palette"),
		FS_SELECT_FILE, DEFAULT_PAL_INI),
	PATHs(_("Default Patterns"), _("Select Default Patterns File"),
		FS_SELECT_FILE, DEFAULT_PAT_INI),
#if GTK_MAJOR_VERSION == 2
	PATHs(_("Default Theme"), _("Select Default Theme File"),
		FS_SELECT_FILE, DEFAULT_THEME_INI),
#endif
	WDONE,
///	---- TAB6 - STATUS BAR
	PAGE(_("Status Bar")),
	CHECKv(_("Canvas Geometry"), status_on[0]),
	CHECKv(_("Cursor X,Y"), status_on[1]),
	CHECKv(_("Pixel [I] {RGB}"), status_on[2]),
	CHECKv(_("Selection Geometry"), status_on[3]),
	CHECKv(_("Undo / Redo"), status_on[4]),
	WDONE,
///	---- TAB7 - TABLET
	PAGE(_("Tablet")),
	FVBOXB(_("Device Settings")),
	BORDER(LABEL, 0),
	REF(label_tablet_device), MLABELxr("", 5, 5, 0),
	BORDER(BUTTON, 0),
	TABLETBTN(_("Configure Device")),
		EVENT(CHANGE, tablet_update_device), TRIGGER,
	DEFBORDER(BUTTON),
	BORDER(TABLE, 0),
	XTABLE(2, 4),
	BORDER(CHECK, 0),
	TLABELx(_("Tool Variable"), 5, 5, 5),
	TLLABELpx(factor_str, 1, 0, 5, 5, 4),
	TLCHECKv(_("Size"), tablet_tool_use[0], 0, 1),
	TLCHECKv(_("Flow"), tablet_tool_use[1], 0, 2),
	TLCHECKv(_("Opacity"), tablet_tool_use[2], 0, 3),
	//	Size/Flow/Opacity sliders
	BORDER(SPINSLIDE, 0),
	TLSPINSLIDEvs(tablet_tool_factor[0], -MAX_TF, MAX_TF, 1, 1),
	TLSPINSLIDEvs(tablet_tool_factor[1], -MAX_TF, MAX_TF, 1, 2),
	TLSPINSLIDEvs(tablet_tool_factor[2], -MAX_TF, MAX_TF, 1, 3),
	WDONE, WDONE,
	FVBOXB(_("Test Area")),
	COLORPATCHv("\xFF\xFF\xFF", 128, 64), // white
	EVENT(XMOUSE, tablet_preview), EVENT(MXMOUSE, tablet_preview),
	REF(label_tablet_pressure), MLABELr(""),
	WDONE, WDONE,
	ENDIF(1), // !script
	WDONE, // notebook
///	Bottom of Prefs window
	OKBOX3(_("OK"), prefs_evt, _("Cancel"), prefs_evt, _("Apply"), prefs_evt),
	WSHOW
};
#undef WBbase

void pressed_preferences()
{
#ifdef U_TIFF
	char *tiffts[3][TIFF_MAX_TYPES];
#endif
	char txt[128];
	pref_dd tdata = { { mem_undo_depth & ~1, MIN_UNDO & ~1, MAX_UNDO & ~1 },
		{ mem_xpm_trans, -1, mem_cols - 1 },
		{ mem_xbm_hot_x, -1, mem_width - 1 },
		{ mem_xbm_hot_y, -1, mem_height - 1 },
		FALSE, TRUE, FALSE,
		0, !!script_cmds, txt,
		NULL, NULL, NULL, NULL };

	// Make sure the user can only open 1 prefs window
	cmd_sensitive(menu_slots[MENU_PREFS], FALSE);
#ifdef U_TIFF
	tiff_rtype = tiff_type_selector(FF_RGB, tiff_rtype, tdata.tiffrs = tiffts[0]);
	tiff_itype = tiff_type_selector(FF_256, tiff_itype, tdata.tiffis = tiffts[1]);
	tiff_btype = tiff_type_selector(FF_BW,  tiff_btype, tdata.tiffbs = tiffts[2]);
#endif
#ifdef U_NLS
	{
		char *cur = inifile_get("languageSETTING", "system");
		int i;

		for (i = 0; i < PREF_LANGS; i++)
			if (!strcmp(pref_lang_ini_code[i], cur)) tdata.lang = i;
	}
#endif
	snprintf(txt, sizeof(txt), "%s (%%)", __("Factor"));

	run_create_(pref_code, &tdata, sizeof(tdata), script_cmds);
}

///	KEYMAPPER WINDOW

typedef struct {
	keymap_dd *km;
	char **slots, **keys, **ext;
	char **keylist;
	char *newkey;
	void **sel, **keyb, **keysel, **add, **remove;
	int slot, nslots, csize;
	int key, nkeys, lsize;
} keysel_dd;

static void refresh_keys(keysel_dd *dt)
{
	key_dd *kd;
	int i = dt->km->nkeys, j = 0, n = dt->slot, l = dt->km->nkeys;

	for (kd = dt->km->keys , i = l; i-- > 0; kd++)
		if (kd->slot - 1 == n) dt->keylist[j++] = kd->name;
	dt->nkeys = j;
	cmd_reset(dt->keysel, dt);
	cmd_sensitive(dt->add, l < dt->km->maxkeys);
	cmd_sensitive(dt->remove, j);
}

static void slot_select(keysel_dd *dt, void **wdata, int what, void **where)
{
	key_dd *kd;
	int i, j, l = dt->km->nkeys;

	cmd_read(where, dt);

	/* Init */
	if (dt->nslots < dt->km->nslots)
	{
		dt->nslots = dt->km->nslots;
		for (kd = dt->km->keys , i = l; i-- > 0; kd++)
		{
			if ((j = kd->slot - 1) < 0) continue;
			if (dt->keys[j]) dt->ext[j] = ">>>";
			else dt->keys[j] = kd->name;
		}
		cmd_reset(origin_slot(where), dt);
	}

	/* Collect keys for this row */
	dt->key = 0;
	refresh_keys(dt);
}

static void addrem_evt(keysel_dd *dt, void **wdata, int what, void **where)
{
	key_dd *kd;
	char *name, *msg;
	int i, j, l, m, old, add = origin_slot(where) == dt->add;

	/* Get */
	name = dt->keylist[dt->key]; // Default is remove
	if (add)
	{
		cmd_read(dt->keyb, dt);
		name = dt->newkey;
		if (!name) return; // No key yet
	}

	/* Find */
	l = dt->km->nkeys;
	kd = dt->km->keys;
	for (i = 0; i < l; i++) if (kd[i].name == name) break;

	/* Check */
	m = old = dt->slot + 1;
	if ((i < l) && add && (old = kd[i].slot))
	{
		if (old < 0)
		{
			alert_box(_("Error"), _("This key is builtin"), NULL);
			return;
		}
		msg = g_strdup_printf(__("This key is mapped to:\n\"%s\""),
			dt->slots[old - 1]);
		j = alert_box(_("Warning"), msg, _("Cancel"),
			old != m ? _("Remap") : NULL, NULL);
		g_free(msg);
		if (j != 2) return;
	}

	/* Delete */
	if (i < l) memmove(kd + i, kd + i + 1, sizeof(*kd) * (--l - i));

	/* Insert */
	if (add)
	{
		kd += l++;
		kd->name = name;
		kd->slot = m;
	}

	/* Update */
	dt->km->nkeys = l;

	/* Refresh */
	old--; m--;
	dt->ext[old] = dt->keys[old] = dt->ext[m] = dt->keys[m] = NULL;
	for (kd = dt->km->keys , i = l; i-- > 0; kd++)
	{
		j = kd->slot - 1;
		if ((j != old) && (j != m)) continue;
		if (dt->keys[j]) dt->ext[j] = ">>>";
		else dt->keys[j] = kd->name;
	}
	cmd_setv(dt->sel, (void *)m, LISTC_RESET_ROW);
	if (old != m) cmd_setv(dt->sel, (void *)old, LISTC_RESET_ROW);
	refresh_keys(dt);
}

static void done_evt(keysel_dd *dt, void **wdata, int what)
{
	if (what != op_EVT_CANCEL) // OK/Apply
		cmd_setv(main_keys, dt->km, KEYMAP_MAP);

	if (what != op_EVT_CLICK) // OK/Cancel
		run_destroy(wdata);
}

#define WBbase keysel_dd
static void *keysel_code[] = {
	WINDOWm(_("Keyboard Shortcuts")),
	MKSHRINK,
//	WXYWH("keysel_window", 600, 400),
	TALLOC(keys, csize), TALLOC(ext, csize),
	TALLOC(keylist, lsize),

	BORDER(SCROLL, 0), BORDER(BUTTON, 0),
	XHBOXBS,
	XSCROLL(1, 2), // auto/always
	WANTMAXW, // max width
	WLIST,
	PTXTCOLUMNp(slots, sizeof(char *), 0, 0),
	PTXTCOLUMNp(keys, sizeof(char *), 0, 0),
	PTXTCOLUMNp(ext, sizeof(char *), 0, 0),
	REF(sel), LISTCu(slot, nslots, slot_select), TRIGGER,
	VBOXS,
	REF(keyb), KEYBUTTON(newkey),
	MINWIDTH(150),
	XSCROLL(1, 1), // auto/auto
	WLIST,
	PTXTCOLUMNp(keylist, sizeof(char *), 0, 0),
	REF(keysel), LISTCu(key, nkeys, NULL),
	REF(add), UBUTTON(_("Add"), addrem_evt),
	REF(remove), UBUTTON(_("Remove"), addrem_evt),
	WDONE, // XVBOX
	WDONE, // XHBOX

	DEFBORDER(BUTTON),
	OKBOX3(_("OK"), done_evt, _("Cancel"), done_evt, _("Apply"), done_evt),
	CLEANUP(km),
	WSHOW
};
#undef WBbase

void keys_selector()
{
	keysel_dd tdata;

	memset(&tdata, 0, sizeof(tdata));
	cmd_peekv(main_keys, &tdata.km, sizeof(tdata.km), KEYMAP_MAP);

	tdata.slots = tdata.km->slotnames + 1;
	tdata.csize = tdata.km->nslots * sizeof(char *);
	tdata.lsize = tdata.km->maxkeys * sizeof(char *);

	run_create(keysel_code, &tdata, sizeof(tdata));
}

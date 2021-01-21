/*	toolbar.c
	Copyright (C) 2006-2021 Mark Tyler and Dmitry Groshev

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



void **icon_buttons[TOTAL_TOOLS];

int toolbar_status[TOOLBAR_MAX];			// True=show
void **drawing_col_prev, **drawing_palette;
void **toolbar_boxes[TOOLBAR_MAX],		// Used for showing/hiding
	**toolbar_zoom_view;

void **m_cursor[TOTAL_CURSORS];			// My mouse cursors
void **move_cursor, **busy_cursor, **corner_cursor[4]; // System cursors

int patterns_grid_w, patterns_grid_h;



static int v_zoom_main = 100, v_zoom_view = 100;

static void **toolbar_zoom_main;
static void **toolbar_labels[2],	// Colour A & B
	**ts_spinslides[4],		// Size, flow, opacity, value
	**ts_label_channel;		// Channel name

static unsigned char mem_prev[PREVIEW_WIDTH * PREVIEW_HEIGHT * 3];
					// RGB colours, tool, pattern preview

static int click_colours(void *dt, void **wdata, int what, void **where,
	mouse_ext *mouse)
{
	if (mem_img[CHN_IMAGE])
	{
		if (mouse->y > 31) choose_pattern(0);
		else if (mouse->x < 48) colour_selector(COLSEL_EDIT_AB);
		else choose_pattern(1);
	}

	return (FALSE);
}

void toolbar_zoom_update()	// Update the zoom combos to reflect current zoom
{
	if (!toolbar_zoom_main || !toolbar_zoom_view) return;

	cmd_set(toolbar_zoom_main, (int)(can_zoom * 100));
	cmd_set(toolbar_zoom_view, (int)(vw_zoom * 100));
}

static void toolbar_zoom_change(void *dt, void **wdata, int what, void **where)
{
	int *cause = cmd_read(where, dt);
	float new = *cause < 100 ? 1.0 / (100 / *cause) : *cause / 100.0;

	if (cause == &v_zoom_main)
	{
		int z = cursor_zoom;
		cursor_zoom = FALSE; // !!! Cursor in dropdown - don't zoom on it
		align_size(new);
		cursor_zoom = z;
	}
	else vw_align_size(new);
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
	run_create_(toolwindow_code, &tdata, sizeof(tdata), script_cmds);
}

static int set_settings(filterwindow_dd *dt, void **wdata)
{
	run_query(wdata);
	return TRUE;
}

#define WBbase filterwindow_dd
static void *smudge_code[] = {
	CHECKv(_("Respect opacity mode"), smudge_mode), RET };
#undef WBbase

void smudge_settings() /* Smudge opacity mode */
{
	static filterwindow_dd tdata = {
		_("Smudge settings"), smudge_code, FW_FN(set_settings) };
	run_create_(toolwindow_code, &tdata, sizeof(tdata), script_cmds);
}

void init_clone()
{
	if (clone_mode) // Aligned aka relative
	{
		clone_status = CLONE_REL;
		clone_dx = clone_dx0;
		clone_dy = clone_dy0;
		if (!(clone_dx0 | clone_dy0)) // Both 0 for default values
			clone_dx = -tool_size , clone_dy = tool_size;
	}
	else // Absolute
	{
		clone_status = CLONE_ABS;
		if (clone_x0 >= 0) clone_x = clone_x0; // -1 for no change
		if (clone_y0 >= 0) clone_y = clone_y0;
	}
}

static int set_clone(filterwindow_dd *dt, void **wdata)
{
	run_query(wdata);
	if (tool_type == TOOL_CLONE) init_clone();
	return (TRUE);
}

#define WBbase filterwindow_dd
static void *clone_code[] = {
	TABLE(3, 3),
	BORDER(LABEL, 0),
	TLABEL(_("Position")), GROUPN,
	TLXSPINv(clone_x0, -1, MAX_WIDTH - 1, 1, 0), OPNAME("X"),
	TLXSPINv(clone_y0, -1, MAX_HEIGHT - 1, 2, 0), OPNAME("Y"),
	TLABEL(_("Offset")), GROUPN,
	TLXSPINv(clone_dx0, -MAX_WIDTH, MAX_WIDTH, 1, 1), OPNAME("X"),
	TLXSPINv(clone_dy0, -MAX_HEIGHT, MAX_HEIGHT, 2, 1), OPNAME("Y"),
	TLCHECKv(_("Aligned"), clone_mode, 0, 2),
	WDONE, RET };
#undef WBbase

void clone_settings()
{
	static filterwindow_dd tdata = {
		_("Clone settings"), clone_code, FW_FN(set_clone) };
	run_create_(toolwindow_code, &tdata, sizeof(tdata), script_cmds);
}

#define WBbase filterwindow_dd
static void *lasso_code[] = {
	CHECKv(_("By selection channel"), lasso_sel), RET };
#undef WBbase

void lasso_settings() /* Lasso selection channel */
{
	static filterwindow_dd tdata = {
		_("Lasso settings"), lasso_code, FW_FN(set_settings) };
	run_create_(toolwindow_code, &tdata, sizeof(tdata), script_cmds);
}

#define WBbase filterwindow_dd
static void *step_code[] = {
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
	run_create_(toolwindow_code, &tdata, sizeof(tdata), script_cmds);
}

typedef struct {
	filterwindow_dd fw;
	int mode, reverse, xform, src;
	int red, green, blue;
	char **lnames;
	void **xb;
} blend_dd;

static int set_blend(blend_dd *dt, void **wdata)
{
	int i, j;

	run_query(wdata);
	i = dt->mode < 0 ? BLEND_NORMAL : dt->mode; // Paranoia
	j = !dt->red + (!dt->green << 1) + (!dt->blue << 2);

	/* Don't accept stop-all or do-nothing */
	if ((j == 7) || !(i | j | dt->xform | dt->src)) return (FALSE);

	blend_mode = i | (dt->reverse ? BLEND_REVERSE : 0) | 
		(dt->xform ? BLEND_XFORM : 0) | (j << BLEND_RGBSHIFT);
	blend_src = dt->src;

	return (TRUE);
}

static void blend_xf(blend_dd *dt, void **wdata, int what, void **where)
{
	if (what == op_EVT_CHANGE) /* Toggling transform mode */
	{
		/* OK if initialized */
		if (mem_bcsp[1].bcsp[BRCOSA_POSTERIZE]) return;
		/* OK if toggled off */
		cmd_read(where, dt);
		if (!dt->xform) return;
	}
	/* Hide for awhile */
	cmd_showhide(GET_WINDOW(wdata), FALSE);
	/* Send user to do init */
	pressed_brcosa(dt->xb);
}

static char *blends[BLEND_NMODES] = {
	_("Normal"), _("Hue"), _("Saturation"), _("Value"),
	_("Colour"), _("Saturate More"),
	_("Multiply"), _("Divide"), _("Screen"), _("Dodge"),
	_("Burn"), _("Hard Light"), _("Soft Light"), _("Difference"),
	_("Darken"), _("Lighten"), _("Grain Extract"),
	_("Grain Merge"), _("Threshold") };

#define WBbase blend_dd
static void *blend_code[] = {
	COMBO(blends, BLEND_NMODES, mode),
	CHECK(_("Reverse"), reverse),
	HBOX,
	REF(xb), CHECK(_("Transform Colour"), xform), EVENT(CHANGE, blend_xf),
	EBUTTONs(_("Settings"), blend_xf),
	WDONE, // HBOX
	BORDER(OPT, 0),
	FHBOXB(_("Source")), XOPTD(lnames, src), WDONE,
	HSEP,
	EQBOXS,
	CHECK(_("Red"), red),
	CHECK(_("Green"), green),
	CHECK(_("Blue"), blue),
	WDONE, RET
};
#undef WBbase

void blend_settings() /* Blend mode */
{
	char *names[SRC_LAYER + MAX_LAYERS + 2];
	char ns[MAX_LAYERS + 1][LAYER_NAMELEN + 5];
	blend_dd tdata = {
		{ _("Blend mode"), blend_code, FW_FN(set_blend) },
		blend_mode & BLEND_MMASK,
		blend_mode & BLEND_REVERSE,
		blend_mode & BLEND_XFORM,
		blend_src,
		~blend_mode & (1 << BLEND_RGBSHIFT),
		~blend_mode & (2 << BLEND_RGBSHIFT),
		~blend_mode & (4 << BLEND_RGBSHIFT),
		names };
	int i;

	names[SRC_NORMAL] = _("Normal");
	names[SRC_IMAGE] = _("Image");
	for (i = 0; i <= layers_total; i++)
	{
		snprintf(ns[i], sizeof(ns[i]), "%d %s", i, layer_table[i].name);
		names[SRC_LAYER + i] = ns[i];
	}
	names[SRC_LAYER + i] = NULL;

	run_create_(toolwindow_code, &tdata, sizeof(tdata), script_cmds);
}

#define GP_WIDTH 256
#define GP_HEIGHT 16
static void **grad_view;

typedef struct {
	int size, flow, opac, chan[NUM_CHANNELS];
	unsigned char rgb[GP_WIDTH * GP_HEIGHT * 3];
} settings_dd;

static void ts_spinslide_moved(settings_dd *dt, void **wdata, int what, void **where)
{
	int n, *cause = cmd_read(where, dt);

	if (cause == &dt->size) tool_size = dt->size;
	else if (cause == &dt->flow) tool_flow = dt->flow;
	else if (cause == &dt->opac)
	{
		if (dt->opac != tool_opacity) pressed_opacity(dt->opac);
	}
	else /* Somewhere in dt->chan[] */
	{
		n = cause - dt->chan;
		if (n == CHN_IMAGE) n = mem_channel;
		if ((n != CHN_IMAGE) && (*cause != channel_col_A[n]))
		{
			channel_col_A[n] = *cause;
			update_stuff(UPD_CAB);
		}
	}
}


void toolbar_settings_exit(void *dt, void **wdata)
{
	if (!wdata) wdata = toolbar_boxes[TOOLBAR_SETTINGS];
	if (!wdata) return;
	toolbar_boxes[TOOLBAR_SETTINGS] = NULL;
	cmd_set(menu_slots[MENU_TBSET], FALSE);
	run_destroy(wdata);
}

static void toolbar_click(void *dt, void **wdata, int what, void **where)
{
	int act_m, res = TRUE;

	if (what == op_EVT_CHANGE)
	{
		void *cause = cmd_read(where, dt);
		// Good for radiobuttons too, as all action codes are nonzero
		if (cause) res = *(int *)cause;
		act_m = TOOL_ID(where);
	}
	else act_m = TOOL_IR(where); // op_EVT_CLICK

	action_dispatch(act_m >> 16, (act_m & 0xFFFF) - 0x8000, res, FALSE);
}

#define WBbase settings_dd
static void *settings_code[] = {
	TOPVBOXV, // Keep height at max requested, to let dock contents stay put
	BORDER(TOOLBAR, 0),
	TOOLBARx(toolbar_click, toolbar_click),
	SCRIPTED,
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
	REFv(grad_view), RGBIMAGEP(rgb, GP_WIDTH, GP_HEIGHT), // in TBBOXTOG
	WDONE,
	/* Colors A & B */
	BORDER(LABEL, 0),
	REFv(toolbar_labels[0]), MLABELxr("", 5, 2, 0),
	REFv(toolbar_labels[1]), MLABELxr("", 5, 2, 0),
	ETABLE(2, 4), BORDER(SPINSLIDE, 0),
	TLABEL(_("Size")),
	REFv(ts_spinslides[0]), TLSPINSLIDEx(size, 1, 255, 1, 0),
	EVENT(CHANGE, ts_spinslide_moved),
	TLABEL(_("Flow")),
	REFv(ts_spinslides[1]), TLSPINSLIDEx(flow, 1, 255, 1, 1),
	EVENT(CHANGE, ts_spinslide_moved),
	TLABEL(_("Opacity")),
	REFv(ts_spinslides[2]), TLSPINSLIDEx(opac, 0, 255, 1, 2),
	EVENT(CHANGE, ts_spinslide_moved),
	uSPIN(chan[CHN_ALPHA], 0, 255), EVENT(SCRIPT, ts_spinslide_moved),
	OPNAME("Alpha"),
	uSPIN(chan[CHN_SEL], 0, 255), EVENT(SCRIPT, ts_spinslide_moved),
	OPNAME("Selection"),
	uSPIN(chan[CHN_MASK], 0, 255), EVENT(SCRIPT, ts_spinslide_moved),
	OPNAME("Mask"),
	REFv(ts_label_channel), TLABELr(""), HIDDEN,
	REFv(ts_spinslides[3]), TLSPINSLIDEx(chan[CHN_IMAGE], 0, 255, 1, 3),
	EVENT(CHANGE, ts_spinslide_moved), HIDDEN, ALTNAME("Value"),
	WEND
};
#undef WBbase

void **create_settings_box()
{
	static char *noscript;
	static settings_dd tdata; // zeroed out, get updated later
	int i;

	for (i = 0; i < TOTAL_SETTINGS; i++) // Initialize buttons' state
		v_settings[i] = *vars_settings[i];
	return (run_create_(settings_code, &tdata, sizeof(tdata),
		cmd_mode ? &noscript : NULL));
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
	if (cmd_mode) return;
	if (toolbar_boxes[TOOLBAR_SETTINGS]) // Used when Home key is pressed
	{
		cmd_showhide(toolbar_boxes[TOOLBAR_SETTINGS], TRUE);
		return;
	}

///	SETTINGS TOOLBAR

	toolbar_boxes[TOOLBAR_SETTINGS] = run_create(sbar_code, sbar_code, 0);
}

static int zooms[] = {
	10, 20, 25, 33, 50, 100, 200, 300, 400, 600, 800, 1000, 1200, 1600, 2000,
	4000, 8000, 0 };

/* !!! For later change_to_tool(DEFAULT_TOOL_ICON) to go through, this need be
 * set to something different initially */
static int tool_id = TTB_PAINT;

void *toolbar_code[] = {
	REFv(m_cursor[TOOL_SQUARE]), XBMCURSOR(square, 0, 0),
	REFv(m_cursor[TOOL_CIRCLE]), XBMCURSOR(circle, 0, 0),
	REFv(m_cursor[TOOL_HORIZONTAL]), XBMCURSOR(horizontal, 0, 0),
	REFv(m_cursor[TOOL_VERTICAL]), XBMCURSOR(vertical, 0, 0),
	REFv(m_cursor[TOOL_SLASH]), XBMCURSOR(slash, 0, 0),
	REFv(m_cursor[TOOL_BACKSLASH]), XBMCURSOR(backslash, 0, 0),
	REFv(m_cursor[TOOL_SPRAY]), XBMCURSOR(spray, 0, 0),
	REFv(m_cursor[TOOL_SHUFFLE]), XBMCURSOR(shuffle, 0, 0),
	REFv(m_cursor[TOOL_FLOOD]), XBMCURSOR(flood, 2, 19),
	REFv(m_cursor[TOOL_SELECT]), XBMCURSOR(select, 10, 10),
	REFv(m_cursor[TOOL_LINE]), XBMCURSOR(line, 0, 0),
	REFv(m_cursor[TOOL_SMUDGE]), XBMCURSOR(smudge, 0, 0),
	REFv(m_cursor[TOOL_POLYGON]), XBMCURSOR(polygon, 10, 10),
	REFv(m_cursor[TOOL_CLONE]), XBMCURSOR(clone, 0, 0),
	REFv(m_cursor[TOOL_GRADIENT]), XBMCURSOR(grad, 0, 0),
	REFv(move_cursor), SYSCURSOR(FLEUR),
	REFv(busy_cursor), SYSCURSOR(WATCH),
	REFv(corner_cursor[0]), SYSCURSOR(TOP_LEFT_CORNER),
	REFv(corner_cursor[1]), SYSCURSOR(TOP_RIGHT_CORNER),
	REFv(corner_cursor[2]), SYSCURSOR(BOTTOM_LEFT_CORNER),
	REFv(corner_cursor[3]), SYSCURSOR(BOTTOM_RIGHT_CORNER),
	TWOBOX,
///	MAIN TOOLBAR
	REFv(toolbar_boxes[TOOLBAR_MAIN]),
	SMARTTBAR(toolbar_click),
	UNLESSv(toolbar_status[TOOLBAR_MAIN]), HIDDEN, // Only show if user wants
	TBBUTTON(_("New Image"), XPM_ICON(new), ACTMOD(DLG_NEW, 0)),
	TBBUTTON(_("Load Image File"), XPM_ICON(open), ACTMOD(DLG_FSEL, FS_PNG_LOAD)),
	TBBUTTON(_("Save Image File"), XPM_ICON(save), ACTMOD(ACT_SAVE, 0)),
	TBSPACE,
	TBBUTTON(_("Cut"),XPM_ICON(cut), ACTMOD(ACT_COPY, 1)),
		ACTMAP(NEED_SEL2),
	TBBUTTON(_("Copy"), XPM_ICON(copy), ACTMOD(ACT_COPY, 0)),
		ACTMAP(NEED_SEL2),
	TBBUTTON(_("Paste"), XPM_ICON(paste), ACTMOD(ACT_PASTE, 0)),
		ACTMAP(NEED_CLIP),
	TBSPACE,
	TBBUTTON(_("Undo"), XPM_ICON(undo), ACTMOD(ACT_DO_UNDO, 0)),
		ACTMAP(NEED_UNDO),
	TBBUTTON(_("Redo"), XPM_ICON(redo), ACTMOD(ACT_DO_UNDO, 1)),
		ACTMAP(NEED_REDO),
	TBSPACE,
	TBBUTTON(_("Transform Colour"), XPM_ICON(brcosa), ACTMOD(DLG_BRCOSA, 0)),
	TBBUTTON(_("Pan Window"), XPM_ICON(pan), ACTMOD(ACT_PAN, 0)),
	SMARTTBMORE(_("More...")),
	REFv(toolbar_zoom_main),
	PCTCOMBOv(v_zoom_main, zooms, toolbar_zoom_change),
	REFv(toolbar_zoom_view),
	PCTCOMBOv(v_zoom_view, zooms, toolbar_zoom_change), HIDDEN,
	WDONE,
///	TOOLS TOOLBAR
	REFv(toolbar_boxes[TOOLBAR_TOOLS]),
	SMARTTBARx(toolbar_click, toolbar_click),
	UNLESSv(toolbar_status[TOOLBAR_TOOLS]), HIDDEN, // Only show if user wants
	SCRIPTED,
	/* !!! scriptbar_code[] adds ALTNAME and event to SMARTTBARx */
	CALL(scriptbar_code),
	REFv(icon_buttons[TTB_PAINT]),
	TBRBUTTONv(_("Paint"), XPM_ICON(paint),
		ACTMOD(ACT_TOOL, TTB_PAINT), tool_id), SHORTCUT(F4, 0),
	/* !!! This, with matching tool_id, would set default tool - which is
	 * exactly the wrong thing to do in here */
//		EVENT(CHANGE, toolbar_click), TRIGGER,
	REFv(icon_buttons[TTB_SHUFFLE]),
	TBRBUTTONv(_("Shuffle"), XPM_ICON(shuffle),
		ACTMOD(ACT_TOOL, TTB_SHUFFLE), tool_id),
	REFv(icon_buttons[TTB_FLOOD]),
	TBRBUTTONxv(_("Flood Fill"), XPM_ICON(flood),
		ACTMOD(ACT_TOOL, TTB_FLOOD), ACTMOD(DLG_FLOOD, 0), tool_id),
		SHORTCUT(f, 0),
	REFv(icon_buttons[TTB_LINE]),
	TBRBUTTONv(_("Straight Line"), XPM_ICON(line),
		ACTMOD(ACT_TOOL, TTB_LINE), tool_id), SHORTCUT(d, 0),
	REFv(icon_buttons[TTB_SMUDGE]),
	TBRBUTTONxv(_("Smudge"), XPM_ICON(smudge),
		ACTMOD(ACT_TOOL, TTB_SMUDGE), ACTMOD(DLG_SMUDGE, 0), tool_id),
		ACTMAP(NEED_24),
	REFv(icon_buttons[TTB_CLONE]),
	TBRBUTTONxv(_("Clone"), XPM_ICON(clone),
		ACTMOD(ACT_TOOL, TTB_CLONE), ACTMOD(DLG_CLONE, 0), tool_id),
	REFv(icon_buttons[TTB_SELECT]),
	TBRBUTTONv(_("Make Selection"), XPM_ICON(select),
		ACTMOD(ACT_TOOL, TTB_SELECT), tool_id), SHORTCUT(F9, 0),
	REFv(icon_buttons[TTB_POLY]),
	TBRBUTTONv(_("Polygon Selection"), XPM_ICON(polygon),
		ACTMOD(ACT_TOOL, TTB_POLY), tool_id),
	REFv(icon_buttons[TTB_GRAD]),
	TBRBUTTONxv(_("Place Gradient"), XPM_ICON(grad_place),
		ACTMOD(ACT_TOOL, TTB_GRAD), ACTMOD(DLG_GRAD, 0), tool_id),
	TBSPACE,
	TBBUTTONx(_("Lasso Selection"), XPM_ICON(lasso),
		ACTMOD(ACT_LASSO, 0), ACTMOD(DLG_LASSO, 0)),
		ACTMAP(NEED_LAS2),
	TBBUTTONx(_("Paste Text"), XPM_ICON(text),
		ACTMOD(DLG_TEXT, 0), ACTMOD(DLG_TEXT_FT, 0)), UNNAME,
	/* Not a good access point for scripting the text tools: cannot disable
	 * only one of the two, here. Better to remove the temptation */
	TBSPACE,
	TBBUTTON(_("Ellipse Outline"), XPM_ICON(ellipse2),
		ACTMOD(ACT_ELLIPSE, 0)),
		ACTMAP(NEED_SEL),
	TBBUTTON(_("Filled Ellipse"), XPM_ICON(ellipse),
		ACTMOD(ACT_ELLIPSE, 1)),
		ACTMAP(NEED_SEL),
	TBBUTTON(_("Outline Selection"), XPM_ICON(rect1),
		ACTMOD(ACT_OUTLINE, 0)),
		ACTMAP(NEED_SEL2),
	TBBUTTON(_("Fill Selection"), XPM_ICON(rect2),
		ACTMOD(ACT_OUTLINE, 1)),
		ACTMAP(NEED_SEL2),
	TBSPACE,
	TBBUTTON(_("Flip Selection Vertically"), XPM_ICON(flip_vs),
		ACTMOD(ACT_SEL_FLIP_V, 0)),
		ACTMAP(NEED_PCLIP),
	TBBUTTON(_("Flip Selection Horizontally"), XPM_ICON(flip_hs),
		ACTMOD(ACT_SEL_FLIP_H, 0)),
		ACTMAP(NEED_PCLIP),
	TBBUTTON(_("Rotate Selection Clockwise"), XPM_ICON(rotate_cs),
		ACTMOD(ACT_SEL_ROT, 0)),
		ACTMAP(NEED_PCLIP),
	TBBUTTON(_("Rotate Selection Anti-Clockwise"), XPM_ICON(rotate_as),
		ACTMOD(ACT_SEL_ROT, 1)),
		ACTMAP(NEED_PCLIP),
	ENDSCRIPT,
	SMARTTBMORE(_("More...")), WDONE,
	WDONE, // twobox
	RET
};

void ts_update_gradient()
{
	settings_dd *dt;
	unsigned char pal[256 * 3], cset[3], *dest, *rgb, *tmp = NULL;
	int i, j, k, op, op2, frac, slot, idx = 255, wrk[NUM_CHANNELS + 3];

	if (!grad_view) return; // Paranoia
	dt = GET_DDATA(wdata_slot(grad_view));

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
		pal2rgb(pal, mem_pal, 256, 256);
	else tmp = cset , --slot; /* Use gradient colors */
	if (!IS_INDEXED) idx = 0; /* Allow intermediate opacities */

	/* Draw the preview, ignoring RGBA coupling */
	memset(rgb = dt->rgb, mem_background, GP_WIDTH * GP_HEIGHT * 3);
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
	cmd_reset(grad_view, dt);
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
	cmd_sensitive(ts_spinslides[3], !i); // For scripting
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

static int motion_palette(void *dt, void **wdata, int what, void **where,
	mouse_ext *mouse)
{
	int pindex;

	/* If cursor got warped, will have another movement event to handle */
	if (drag_index && cmd_checkv(where, MOUSE_BOUND)) return (TRUE);

	pindex = (mouse->y - PALETTE_SWATCH_Y) / PALETTE_SWATCH_H;
	pindex = pindex < 0 ? 0 : pindex >= mem_cols ? mem_cols - 1 : pindex;

	if (drag_index && (drag_index_vals[1] != pindex))
	{
		mem_pal_index_move(drag_index_vals[1], pindex);
		update_stuff(UPD_MVPAL);
		drag_index_vals[1] = pindex;
	}

	return (TRUE);
}

static int release_palette(void *dt, void **wdata, int what, void **where,
	mouse_ext *mouse)
{
	if (drag_index)
	{
		drag_index = FALSE;
		cmd_cursor(drawing_palette, NULL);
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

static int click_palette(void *dt, void **wdata, int what, void **where,
	mouse_ext *mouse)
{
	int pindex, px = mouse->x, py = mouse->y;


	/* Filter out multiple clicks */
	if (mouse->count > 1) return (TRUE);

	if (py < PALETTE_SWATCH_Y) return (TRUE);
	pindex = (py - PALETTE_SWATCH_Y) / PALETTE_SWATCH_H;
	if (pindex >= mem_cols) return (TRUE);

	if (px < PALETTE_SWATCH_X) colour_selector(COLSEL_EDIT_ALL + pindex);
	else if (px < PALETTE_CROSS_X)		// Colour A or B changed
	{
		if ((mouse->button == 1) && (mouse->state & _Smask))
		{
			mem_pal_copy(brcosa_palette, mem_pal);
			drag_index = TRUE;
			drag_index_vals[0] = drag_index_vals[1] = pindex;
			cmd_cursor(drawing_palette, move_cursor);
		}
		else if ((mouse->button == 1) || (mouse->button == 3))
		{
			int ab = (mouse->button == 3) || (mouse->state & _Cmask);

			mem_col_[ab] = pindex;
			mem_col_24[ab] = mem_pal[pindex];
			update_stuff(UPD_AB);
		}
	}
	else /* if (px >= PALETTE_CROSS_X) */		// Mask changed
	{
		mem_mask_setv(NULL, pindex, !mem_prot_mask[pindex]);
		update_stuff(UPD_CMASK);
	}

	return (TRUE);
}

void *toolbar_palette_code[] = {
	REFv(toolbar_boxes[TOOLBAR_PALETTE]), VBOXr,
	UNLESSv(toolbar_status[TOOLBAR_PALETTE]), HIDDEN,
	HBOXP,
	REFv(drawing_col_prev),
	CCANVASIMGv(mem_prev, PREVIEW_WIDTH, PREVIEW_HEIGHT), // centered in box
	EVENT(RMOUSE, click_colours),
	WDONE, // HBOXP
	BORDER(SCROLL, 0),
	XSCROLL(0, 2), // never/always
	REFv(drawing_palette),
	CANVASIMGv(mem_pals, PALETTE_WIDTH, 64), // initial size
	EVENT(MOUSE, click_palette),
	EVENT(MMOUSE, motion_palette),
	EVENT(RMOUSE, release_palette),
	WDONE,
	RET
};

void toolbar_showhide()				// Show/Hide all 4 toolbars
{
	static const unsigned char bar[4] =
		{ TOOLBAR_MAIN, TOOLBAR_TOOLS, TOOLBAR_PALETTE, TOOLBAR_STATUS };
	int i;

	// Don't touch regular toolbars in view mode
	if (!view_image_only) for (i = 0; i < 4; i++)
	{
		int n = bar[i];
		if (toolbar_boxes[n]) cmd_showhide(toolbar_boxes[n],
			toolbar_status[n]);
	}

	if (!toolbar_status[TOOLBAR_SETTINGS])
		toolbar_settings_exit(NULL, NULL);
	else
	{
		toolbar_settings_init();
		cmd_setv(main_window_, (void *)1, WINDOW_RAISE);
	}
}


void pressed_toolbar_toggle(int state, int which)
{						// Menu toggle for toolbars
	toolbar_status[which] = state;
	toolbar_showhide();
}



///	PATTERNS/TOOL PREVIEW AREA


#define EXT_PATTERNS 256

static unsigned char patterns[8 * 8 * (DEF_PATTERNS + EXT_PATTERNS)];
#define pattern0 (patterns + 8 * 8 * DEF_PATTERNS)

void mem_set_brush(int val)			// Set brush, update size/flow/preview
{
	int offset, j, o, o2;

	brush_type = mem_brush_list[val][0];
	tool_size = mem_brush_list[val][1];
	if (mem_brush_list[val][2] > 0) tool_flow = mem_brush_list[val][2];

	offset = 3 * (2 + BRUSH_CELL * (val % BRUSH_GRID_W) +
		2 * PATCH_WIDTH + BRUSH_CELL * (val / BRUSH_GRID_W) * PATCH_WIDTH);
			// Offset in brush RGB
	for (j = 0; j < BRUSH_CELL - 2 * 2; j++)
	{
		o = 3 * (PREVIEW_BRUSH_X + PREVIEW_WIDTH * PREVIEW_BRUSH_Y +
			PREVIEW_WIDTH * j);	// Preview offset
		o2 = offset + 3 * PATCH_WIDTH * j;	// Offset in brush RGB
		memcpy(mem_prev + o, mem_brushes + o2, (BRUSH_CELL - 2 * 2) * 3);
	}
}

#if (PREVIEW_BRUSH_X + BRUSH_CELL - 2 * 2 > PREVIEW_WIDTH) || \
	(PREVIEW_BRUSH_Y + BRUSH_CELL - 2 * 2 > PREVIEW_HEIGHT)
#error "Mismatched preview size"
#endif

#include "graphics/xbm_patterns.xbm"
#if (xbm_patterns_width % 8) || (xbm_patterns_width < 8 * 2) || \
	(xbm_patterns_width > 8 * 16) || (xbm_patterns_height % 8) || \
	(xbm_patterns_height < 8 * 2) || (xbm_patterns_height > 8 * 16)
#error "Unacceptable width/height of patterns bitmap"
#endif

int set_master_pattern(char *m)
{
	static const unsigned char bayer_map[16] = {
		 0,  8,  2, 10,
		12,  4, 14,  6,
		 3, 11,  1,  9,
		15,  7, 13,  5};
	char *tail, buf[16 * 3];
	unsigned char map[16], mk[16];
	int i, n;

	buf[0] = '\0';
	if (!m) m = inifile_get("masterPattern", "");
	if (*m) /* Have string to parse */
	{
		memset(mk, 0, 16);
		for (i = 0; i < 16; i++)
		{
			n = strtol(m, &tail, 10);
			if ((tail == m) || (n < 0) || (n > 15) || mk[n]++) break;
			map[i] = n;
			m = tail + ((i < 15) && (*tail == ','));
		}
		/* Canonicalize string if good */
		if ((i >= 16) && !m[strspn(m, "\t ")]) sprintf(buf,
			"%d,%d,%d,%d %d,%d,%d,%d %d,%d,%d,%d %d,%d,%d,%d",
			 map[0],  map[1],  map[2],  map[3],
			 map[4],  map[5],  map[6],  map[7],
			 map[8],  map[9], map[10], map[11],
			map[12], map[13], map[14], map[15]);
		inifile_set("masterPattern", buf);
	}
	if (!buf[0]) memcpy(map, bayer_map, 16);

	/* Expand map into a set of 16 8x8 patterns */
	for (i = 0; i < DEF_PATTERNS * 8 * 8; i++)
		patterns[i] = map[(i & 3) + ((i >> 1) & 0xC)] >= (i >> 6);

	return (!!buf[0]);
}

/* Create RGB dump of patterns to display: arranged as in their source file,
 * each 8x8 pattern repeated 4x4 with 2 pixels border and 4 pixels separation */
void render_patterns(unsigned char *buf)
{
	png_color *p;
	unsigned char *dest, *src = pattern0;
	int j, x, y, h, rowl = patterns_grid_w * PATTERN_CELL * 3;

	dest = buf + rowl * 2 + 2 * 3;
	for (y = 0; y < patterns_grid_h; y++)
	{
		for (h = 0; h < 8; h++)
		{
			for (x = 0; x < patterns_grid_w; x++)
			{
				for (j = 0; j < 8; j++)
				{
					p = mem_col_24 + *src++;
					*dest++ = p->red;
					*dest++ = p->green;
					*dest++ = p->blue;
				}
				memcpy(dest, dest - 8 * 3, 8 * 3);
				memcpy(dest + 8 * 3, dest - 8 * 3, 8 * 3 * 2);
				src += 8 * 8 - 8;
				dest += (PATTERN_CELL - 8) * 3;
			}
			src -= patterns_grid_w * 8 * 8 - 8;
		}
		memcpy(dest, dest - rowl * 8, rowl * 8);
		memcpy(dest + rowl * 8 , dest - rowl * 8, rowl * 8 * 2);
		src += (patterns_grid_w - 1) * 8 * 8;
		dest += rowl * (PATTERN_CELL - 8);
	}
}

/* Test/set 0-1 indexed image as new patterns */
int set_patterns(ls_settings *settings)
{
	unsigned char *dest = pattern0, *src = settings->img[CHN_IMAGE];
	int j, l, ll, w = settings->width, h = settings->height;

	/* Check dimensions (2x2..16x16 of 8x8 cells) and depth */
	if ((w % 8) || (w < 8 * 2) || (w > 8 * 16) ||
		(h % 8) || (h < 8 * 2) || (h > 8 * 16) || (settings->bpp != 1))
		return (0); // Forget it
	if (!src) return (-1); // Can try
	if (settings->colors != 2) return (0); // 0-1 it must be

	ll = l = (patterns_grid_w = w / 8) * (patterns_grid_h = h / 8);
	while (l-- > 0)
	{
		for (j = 8; j-- > 0; src += w , dest += 8) memcpy(dest, src, 8);
		src -= ((dest - pattern0) % (w * 8) ? w * 8 : w) - 8;
	}
	return (ll);
}

void mem_pat_update()			// Update indexed and then RGB pattern preview
{
	int i, j, k, l;

	if (!patterns_grid_w) /* Unpack the XBM */
	{
		unsigned char *src = xbm_patterns_bits, *dest = pattern0;
		int w;

		patterns_grid_w = w = xbm_patterns_width / 8;
		l = w * (patterns_grid_h = xbm_patterns_height / 8);
		while (l-- > 0)
		{
			for (j = 8; j-- > 0; src += w)
				for (i = *src + 0x100; i > 1; i >>= 1) *dest++ = i & 1;
			src -= ((dest - pattern0) % (w * 8 * 8) ? w * 8 : w) - 1;
		}
	}

	/* Set up default Bayer 4x4 patterns */
	if (!mem_pattern) set_master_pattern(NULL);

	if ( mem_img_bpp == 1 )
	{
		mem_col_A24 = mem_pal[mem_col_A];
		mem_col_B24 = mem_pal[mem_col_B];
	}

	/* Set up pattern maps */
	mem_pattern = pattern0 + mem_tool_pat * 8 * 8;
	for (i = 0; i < 8 * 8; i++)
	{
		j = mem_pattern[i];
		mem_col_pat[i] = mem_col_[j];
		mem_col_pat24[i * 3 + 0] = mem_col_24[j].red;
		mem_col_pat24[i * 3 + 1] = mem_col_24[j].green;
		mem_col_pat24[i * 3 + 2] = mem_col_24[j].blue;
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

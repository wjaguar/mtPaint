/*	prefs.c
	Copyright (C) 2005-2014 Mark Tyler and Dmitry Groshev

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
#include "vcode.h"
#include "png.h"
#include "canvas.h"
#include "inifile.h"
#include "viewer.h"
#include "mainwindow.h"
#include "thread.h"

#include "prefs.h"


///	PREFERENCES WINDOW

static void **label_tablet_device, **label_tablet_pressure;

static char	*tablet_ini[] = { "tablet_value_size", "tablet_value_flow", "tablet_value_opacity" },
		*tablet_ini2[] = { "tablet_use_size", "tablet_use_flow", "tablet_use_opacity" };

#if GTK_MAJOR_VERSION == 1
static GdkDeviceInfo *tablet_device;
#endif
#if GTK_MAJOR_VERSION == 2
static GdkDevice *tablet_device;
#endif
static GtkWidget *inputd;

int tablet_working;		// Has the device been initialized?

int tablet_tool_use[3];				// Size, flow, opacity
float tablet_tool_factor[3];			// Size, flow, opacity

#ifdef U_NLS

#define PREF_LANGS 21

static char *pref_lang_ini_code[PREF_LANGS] = { "system",
	"zh_CN.utf8", "zh_TW.utf8",
	"cs_CZ", "nl_NL", "en_GB", "fr_FR",
	"gl_ES", "de_DE", "hu_HU", "it_IT",
	"ja_JP.utf8", "pl_PL", "pt_PT",
	"pt_BR", "ru_RU", "sk_SK",
	"es_ES", "sv_SE", "tl_PH", "tr_TR" };

#undef _
#define _(X) X

static char *pref_langs[PREF_LANGS] = { _("Default System Language"),
	_("Chinese (Simplified)"), _("Chinese (Taiwanese)"), _("Czech"),
	_("Dutch"), _("English (UK)"), _("French"), _("Galician"), _("German"),
	_("Hungarian"), _("Italian"), _("Japanese"), _("Polish"),
	_("Portuguese"), _("Portuguese (Brazilian)"), _("Russian"), _("Slovak"),
	_("Spanish"), _("Swedish"), _("Tagalog"), _("Turkish") };

#undef _
#define _(X) __(X)

#endif


static gboolean delete_inputd()
{
	int i, j;
	char txt[32];

#if GTK_MAJOR_VERSION == 1
	GdkDeviceInfo *dev = tablet_device;
#else /* #if GTK_MAJOR_VERSION == 2 */
	GdkDevice *dev = GTK_INPUT_DIALOG (inputd)->current_device;
#endif

	if ( tablet_working )		// Store tablet settings in INI file for future session
	{
		inifile_set( "tablet_name", dev->name );
		inifile_set_gint32( "tablet_mode", dev->mode );

		for ( i=0; i<dev->num_axes; i++ )
		{
#if GTK_MAJOR_VERSION == 1
			j = dev->axes[i];
#else /* #if GTK_MAJOR_VERSION == 2 */
			j = dev->axes[i].use;
#endif
			sprintf(txt, "tablet_axes_v%i", i);
			inifile_set_gint32( txt, j );
		}
	}

	gtk_widget_destroy(inputd);
	inputd = NULL;
	return (TRUE);
}

static void tablet_update_pressure( double pressure )
{
	char txt[64];

	sprintf(txt, "%s = %.2f", _("Pressure"), pressure * (1.0 / MAX_PRESSURE));
	cmd_setv(label_tablet_pressure, txt, LABEL_VALUE);
}

static void tablet_update_device( char *device )
{
	char txt[64];

	sprintf(txt, "%s = %s", _("Current Device"), device);
	cmd_setv(label_tablet_device, txt, LABEL_VALUE);
}


#if GTK_MAJOR_VERSION == 1
static void tablet_gtk1_newdevice(devid)			// Get new device info
{
	GList *dlist = gdk_input_list_devices();
	GdkDeviceInfo *device = NULL;

	tablet_device = NULL;
	while ( dlist != NULL )
	{
		device = dlist->data;
		if ( device->deviceid == devid )		// Device found
		{
			tablet_device = device;
			break;
		}
		dlist = dlist->next;
	}
}

static void tablet_enable_device(GtkInputDialog *inputdialog, guint32 devid)
{
	tablet_gtk1_newdevice(devid);

	if ( tablet_device != NULL )
	{
		tablet_working = TRUE;
		tablet_update_device( tablet_device->name );
	}
	else	tablet_working = FALSE;
}

static void tablet_disable_device(GtkInputDialog *inputdialog, guint32 devid)
{
	tablet_working = FALSE;
	tablet_update_device( "NONE" );
}
#endif
#if GTK_MAJOR_VERSION == 2
static void tablet_enable_device(GtkInputDialog *inputdialog, GdkDevice *deviceid, gpointer user_data)
{
	tablet_working = TRUE;
	tablet_update_device( deviceid->name );
	tablet_device = deviceid;
}

static void tablet_disable_device(GtkInputDialog *inputdialog, GdkDevice *deviceid, gpointer user_data)
{
	tablet_working = FALSE;
	tablet_update_device( "NONE" );
}
#endif


static void conf_tablet()
{
	GtkWidget *close;
	GtkAccelGroup* ag;

	if (inputd) return;	// Stops multiple dialogs being opened

	inputd = gtk_input_dialog_new();
	gtk_window_set_position(GTK_WINDOW(inputd), GTK_WIN_POS_CENTER);

	close = GTK_INPUT_DIALOG(inputd)->close_button;
	gtk_signal_connect(GTK_OBJECT(close), "clicked",
		GTK_SIGNAL_FUNC(delete_inputd), NULL);
	gtk_signal_connect(GTK_OBJECT(inputd), "delete_event",
		GTK_SIGNAL_FUNC(delete_inputd), NULL);
	ag = gtk_accel_group_new();
	gtk_widget_add_accelerator(close, "clicked",
		ag, GDK_Escape, 0, (GtkAccelFlags)0);

	gtk_signal_connect(GTK_OBJECT(inputd), "enable-device",
		GTK_SIGNAL_FUNC(tablet_enable_device), NULL);
	gtk_signal_connect(GTK_OBJECT(inputd), "disable-device",
		GTK_SIGNAL_FUNC(tablet_disable_device), NULL);

	if (GTK_INPUT_DIALOG(inputd)->keys_list)
		gtk_widget_hide(GTK_INPUT_DIALOG(inputd)->keys_list);
	if (GTK_INPUT_DIALOG(inputd)->keys_listbox)
		gtk_widget_hide(GTK_INPUT_DIALOG(inputd)->keys_listbox);

	gtk_widget_hide(GTK_INPUT_DIALOG(inputd)->save_button);

	gtk_widget_show(inputd);
	gtk_window_add_accel_group(GTK_WINDOW(inputd), ag);
}

typedef struct {
	int undo_depth[3], trans[3], hot_x[3], hot_y[3];
	int tf[3];
	int lang;
	char *factor_str;
} pref_dd;

static void prefs_evt(pref_dd *dt, void **wdata, int what)
{
	char *p, oldpal[PATHBUF], oldpat[PATHBUF];
	int i, j;

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

		for (i = 0; i < 3; i++)
		{
			inifile_set_gboolean(tablet_ini2[i], tablet_tool_use[i]);
			j = dt->tf[i];
			inifile_set_gint32(tablet_ini[i], j);
			tablet_tool_factor[i] = j / 100.0;
		}

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
		if (inputd) delete_inputd();
		run_destroy(wdata);
		cmd_sensitive(menu_slots[MENU_PREFS], TRUE);
	}
}

static int tablet_preview(pref_dd *dt, void **wdata, int what, void **where,
	mouse_ext *mouse)
{
	if (mouse->button == 1) tablet_update_pressure(mouse->pressure);
  
	return (TRUE);
}

///	V-CODE

#undef _
#define _(X) X

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
	PAGE(_("General")),
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
	CHECKv(_("Optimize alpha chequers"), chequers_optimize),
	CHECKv(_("Disable view window transparencies"), opaque_view),
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
	CHECKb(_("Confirm Exit"), "exitToggle", FALSE),
	CHECKv(_("Q key quits mtPaint"), q_quit),
	CHECKv(_("Changing tool commits paste"), paste_commit),
	CHECKb(_("Centre tool settings dialogs"), "centerSettings", TRUE),
	CHECKb(_("New image sets zoom to 100%"), "zoomToggle", FALSE),
	CHECKv(_("Zoom on cursor position"), cursor_zoom),
#if GTK_MAJOR_VERSION == 2
	CHECKv(_("Mouse Scroll Wheel = Zoom"), scroll_zoom),
	CHECKv(_("Use menu icons"), show_menu_icons),
#endif
	WDONE,
///	---- TAB3 - FILES
	PAGE(_("Files")),
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
///	---- TAB4 - PATHS
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
///	---- TAB5 - STATUS BAR
	PAGE(_("Status Bar")),
	CHECKv(_("Canvas Geometry"), status_on[0]),
	CHECKv(_("Cursor X,Y"), status_on[1]),
	CHECKv(_("Pixel [I] {RGB}"), status_on[2]),
	CHECKv(_("Selection Geometry"), status_on[3]),
	CHECKv(_("Undo / Redo"), status_on[4]),
	WDONE,
///	---- TAB6 - TABLET
	PAGE(_("Tablet")),
	FVBOXB(_("Device Settings")),
	BORDER(LABEL, 0),
	REFv(label_tablet_device), MLABELxr("", 5, 5, 0),
	BORDER(BUTTON, 0),
	UBUTTON(_("Configure Device"), conf_tablet),
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
	TLSPINSLIDEs(tf[0], -100, 100, 1, 1),
	TLSPINSLIDEs(tf[1], -100, 100, 1, 2),
	TLSPINSLIDEs(tf[2], -100, 100, 1, 3),
	WDONE, WDONE,
	FVBOXB(_("Test Area")),
	COLORPATCHv("\xFF\xFF\xFF", 128, 64), // white
	EVENT(XMOUSE, tablet_preview), EVENT(MXMOUSE, tablet_preview),
	REFv(label_tablet_pressure), MLABELr(""),
	WDONE, WDONE,
	WDONE, // notebook
///	Bottom of Prefs window
	OKBOX3(_("OK"), prefs_evt, _("Cancel"), prefs_evt, _("Apply"), prefs_evt),
	WSHOW
};
#undef WBbase

#undef _
#define _(X) __(X)

void pressed_preferences()
{
	char txt[128];
	pref_dd tdata = { { mem_undo_depth & ~1, MIN_UNDO & ~1, MAX_UNDO & ~1 },
		{ mem_xpm_trans, -1, mem_cols - 1 },
		{ mem_xbm_hot_x, -1, mem_width - 1 },
		{ mem_xbm_hot_y, -1, mem_height - 1 },
		{ rint(tablet_tool_factor[0] * 100.0),
		  rint(tablet_tool_factor[1] * 100.0),
		  rint(tablet_tool_factor[2] * 100.0) },
		0, txt };

	// Make sure the user can only open 1 prefs window
	cmd_sensitive(menu_slots[MENU_PREFS], FALSE);
#ifdef U_NLS
	{
		char *cur = inifile_get("languageSETTING", "system");
		int i;

		for (i = 0; i < PREF_LANGS; i++)
			if (!strcmp(pref_lang_ini_code[i], cur)) tdata.lang = i;
	}
#endif
	snprintf(txt, sizeof(txt), "%s (%%)", _("Factor"));

	run_create(pref_code, &tdata, sizeof(tdata));

	tablet_update_device(tablet_working ? tablet_device->name : "NONE");
}


void init_tablet()				// Set up variables
{
	int i;
	char *devname, txt[64];

	GList *dlist;

#if GTK_MAJOR_VERSION == 1
	GdkDeviceInfo *device = NULL;
	gint use;
#endif
#if GTK_MAJOR_VERSION == 2
	GdkDevice *device = NULL;
	GdkAxisUse use;
#endif

	if (tablet_working)	// User has got tablet working in past so try to initialize it
	{
		tablet_working = FALSE;
		devname = inifile_get( "tablet_name", "?" );	// Device name last used
#if GTK_MAJOR_VERSION == 1
		dlist = gdk_input_list_devices();
#endif
#if GTK_MAJOR_VERSION == 2
		dlist = gdk_devices_list();
#endif
		while ( dlist != NULL )
		{
			device = dlist->data;
			if ( strcmp(device->name, devname ) == 0 )
			{		// Previously used device was found

#if GTK_MAJOR_VERSION == 1
				gdk_input_set_mode(device->deviceid,
					inifile_get_gint32("tablet_mode", 0));
#endif
#if GTK_MAJOR_VERSION == 2
				gdk_device_set_mode(device,
					inifile_get_gint32("tablet_mode", 0));
#endif
				for ( i=0; i<device->num_axes; i++ )
				{
					sprintf(txt, "tablet_axes_v%i", i);
					use = inifile_get_gint32( txt, GDK_AXIS_IGNORE );
#if GTK_MAJOR_VERSION == 1
					device->axes[i] = use;
					gdk_input_set_axes(device->deviceid, device->axes);
#endif
#if GTK_MAJOR_VERSION == 2
					gdk_device_set_axis_use(device, i, use);
#endif
				}
				tablet_device = device;
				tablet_working = TRUE;		// Success!
				break;
			}
			dlist = dlist->next;		// Not right device so look for next one
		}
	}

	for ( i=0; i<3; i++ )
	{
		tablet_tool_use[i] = inifile_get_gboolean( tablet_ini2[i], FALSE );
		tablet_tool_factor[i] = ((float) inifile_get_gint32( tablet_ini[i], 100 )) / 100;
	}
}

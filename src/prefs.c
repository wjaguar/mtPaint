/*	prefs.c
	Copyright (C) 2005-2013 Mark Tyler and Dmitry Groshev

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
#include "canvas.h"
#include "inifile.h"
#include "viewer.h"
#include "mainwindow.h"
#include "thread.h"
#include "vcode.h"

#include "prefs.h"


///	PREFERENCES WINDOW

static GtkWidget *check_tablet[3], *hscale_tablet[3], *label_tablet_device, *label_tablet_pressure;

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


static gboolean expose_tablet_preview(GtkWidget *widget, GdkEventExpose *event)
{
        gdk_draw_rectangle(widget->window, widget->style->white_gc, TRUE,
		event->area.x, event->area.y, event->area.width, event->area.height);

	return (FALSE);
}


static void delete_inputd()
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
}

static void tablet_update_pressure( double pressure )
{
	char txt[64];

	sprintf(txt, "%s = %.2f", _("Pressure"), pressure);
	gtk_label_set_text( GTK_LABEL(label_tablet_pressure), txt );
}

static void tablet_update_device( char *device )
{
	char txt[64];

	sprintf(txt, "%s = %s", _("Current Device"), device);
	gtk_label_set_text( GTK_LABEL(label_tablet_device), txt );
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


static void conf_tablet(GtkWidget *widget)
{
	GtkWidget *close;
	GtkAccelGroup* ag;

	if (inputd) return;	// Stops multiple dialogs being opened

	inputd = gtk_input_dialog_new();
	gtk_window_set_position(GTK_WINDOW(inputd), GTK_WIN_POS_CENTER);

	close = GTK_INPUT_DIALOG(inputd)->close_button;
	gtk_signal_connect(GTK_OBJECT(close), "clicked",
		GTK_SIGNAL_FUNC(delete_inputd), NULL);
	ag = gtk_accel_group_new();
	gtk_widget_add_accelerator(close, "clicked",
		ag, GDK_Escape, 0, (GtkAccelFlags)0);
	delete_to_click(inputd, close);

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
	int lang;
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
			tablet_tool_use[i] = gtk_toggle_button_get_active(
				GTK_TOGGLE_BUTTON(check_tablet[i]));
			inifile_set_gboolean( tablet_ini2[i], tablet_tool_use[i] );
			j = mt_spinslide_get_value(hscale_tablet[i]);
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
		gtk_widget_set_sensitive(menu_widgets[MENU_PREFS], TRUE);
	}
}

static gint tablet_preview_button (GtkWidget *widget, GdkEventButton *event)
{
	gdouble pressure = 0.0;

	if (event->button == 1)
	{
#if GTK_MAJOR_VERSION == 1
		pressure = event->pressure;
#endif
#if GTK_MAJOR_VERSION == 2
		gdk_event_get_axis ((GdkEvent *)event, GDK_AXIS_PRESSURE, &pressure);
#endif
		tablet_update_pressure( pressure );
	}

	return TRUE;
}

static gboolean tablet_preview_motion(GtkWidget *widget, GdkEventMotion *event)
{
	gdouble pressure = 0.0;
	GdkModifierType state;

#if GTK_MAJOR_VERSION == 1
	if (event->is_hint)
	{
		gdk_input_window_get_pointer (event->window, event->deviceid,
				NULL, NULL, &pressure, NULL, NULL, &state);
	}
	else
	{
		pressure = event->pressure;
		state = event->state;
	}
#else /* #if GTK_MAJOR_VERSION == 2 */
	if (event->is_hint) gdk_device_get_state (event->device, event->window, NULL, &state);
	else state = event->state;

	gdk_event_get_axis ((GdkEvent *)event, GDK_AXIS_PRESSURE, &pressure);
#endif

	if (state & GDK_BUTTON1_MASK)
		tablet_update_pressure( pressure );
  
	return TRUE;
}

///	EXTENSIONS TO V-CODE

static void **create_pref_tablet(void **r, GtkWidget ***wpp)
{
	char *tablet_txt[] = { _("Size"), _("Flow"), _("Opacity") };
	GtkWidget *vbox_2, *label, *button1, *table3, *drawingarea_tablet;
	char txt[128];
	int i;

	vbox_2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_2);
	add_with_frame(**wpp, _("Device Settings"), vbox_2);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_2), 5);

	label_tablet_device = pack(vbox_2, gtk_label_new(""));
	gtk_widget_show (label_tablet_device);
	gtk_misc_set_alignment (GTK_MISC (label_tablet_device), 0, 0.5);
	gtk_misc_set_padding (GTK_MISC (label_tablet_device), 5, 5);

	button1 = add_a_button( _("Configure Device"), 0, vbox_2, FALSE );
	gtk_signal_connect(GTK_OBJECT(button1), "clicked",
		GTK_SIGNAL_FUNC(conf_tablet), NULL);

	table3 = xpack(vbox_2, gtk_table_new(4, 2, FALSE));
	gtk_widget_show (table3);

	label = add_to_table( _("Tool Variable"), table3, 0, 0, 0 );
	gtk_misc_set_padding (GTK_MISC (label), 5, 5);
	snprintf(txt, 60, "%s (%%)", _("Factor"));
	label = add_to_table( txt, table3, 0, 1, 0 );
	gtk_misc_set_padding (GTK_MISC (label), 5, 5);
	gtk_misc_set_alignment (GTK_MISC (label), 0.4, 0.5);

	for ( i=0; i<3; i++ )
	{
		check_tablet[i] = gtk_check_button_new_with_label(tablet_txt[i]);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_tablet[i]),
			tablet_tool_use[i]);
		gtk_widget_show(check_tablet[i]);
		to_table_l(check_tablet[i], table3, i + 1, 0, 1, 0);

	//	Size/Flow/Opacity sliders

		hscale_tablet[i] = mt_spinslide_new(150, -2);
		mt_spinslide_set_range(hscale_tablet[i], -100, 100);
		gtk_table_attach(GTK_TABLE(table3), hscale_tablet[i], 1, 2,
			i + 1, i + 2, GTK_EXPAND | GTK_FILL, GTK_FILL, 0, 0);
		mt_spinslide_set_value(hscale_tablet[i],
			rint(tablet_tool_factor[i] * 100.0));
	}

	vbox_2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_2);
	add_with_frame(**wpp, _("Test Area"), vbox_2);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_2), 5);

	drawingarea_tablet = xpack(vbox_2, gtk_drawing_area_new());
	gtk_widget_show (drawingarea_tablet);
	gtk_widget_set_usize (drawingarea_tablet, 128, 64);
	gtk_signal_connect( GTK_OBJECT(drawingarea_tablet), "expose_event",
		GTK_SIGNAL_FUNC (expose_tablet_preview), (gpointer) drawingarea_tablet );

	gtk_signal_connect (GTK_OBJECT (drawingarea_tablet), "motion_notify_event",
		GTK_SIGNAL_FUNC (tablet_preview_motion), NULL);
	gtk_signal_connect (GTK_OBJECT (drawingarea_tablet), "button_press_event",
		GTK_SIGNAL_FUNC (tablet_preview_button), NULL);

	gtk_widget_set_events (drawingarea_tablet, GDK_EXPOSURE_MASK
		| GDK_LEAVE_NOTIFY_MASK
		| GDK_BUTTON_PRESS_MASK
		| GDK_POINTER_MOTION_MASK
		| GDK_POINTER_MOTION_HINT_MASK);

	gtk_widget_set_extension_events (drawingarea_tablet, GDK_EXTENSION_EVENTS_CURSOR);

	label_tablet_pressure = pack(vbox_2, gtk_label_new(""));
	gtk_widget_show (label_tablet_pressure);
	gtk_misc_set_alignment (GTK_MISC (label_tablet_pressure), 0, 0.5);

	return (r);
}

///	V-CODE

#undef _
#define _(X) X

#define WBbase pref_dd
static void *pref_code[] = {
	WINDOW(_("Preferences")), // nonmodal
	MKSHRINK, // shrinkable
	BORDER(XSCROLL, 0),
	XSCROLL(1, 1), // auto/auto
	WANTMAX, // max size
	SNBOOK,
	BORDER(TABLE, 10),
	BORDER(TLABEL, 4), BORDER(SPIN, 4),
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
	BORDER(LABEL, 0),
	BORDER(OPT, 0),
	FVBOXs(_("Language"), 5),
	MLABEL(_("Select preferred language translation\n\n"
	"You will need to restart mtPaint\nfor this to take full effect")),
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
	EXEC(create_pref_tablet),
	WDONE,
	WDONE, // notebook
///	Bottom of Prefs window
	BORDER(OKBOX, 0),
	OKBOX(_("OK"), prefs_evt, _("Cancel"), prefs_evt),
	OKADD(_("Apply"), prefs_evt),
	WSHOW
};
#undef WBbase

#undef _
#define _(X) __(X)

void pressed_preferences()
{
	pref_dd tdata = { { mem_undo_depth & ~1, MIN_UNDO & ~1, MAX_UNDO & ~1 },
		{ mem_xpm_trans, -1, mem_cols - 1 },
		{ mem_xbm_hot_x, -1, mem_width - 1 },
		{ mem_xbm_hot_y, -1, mem_height - 1 }, 0 };

	// Make sure the user can only open 1 prefs window
	gtk_widget_set_sensitive(menu_widgets[MENU_PREFS], FALSE);
#ifdef U_NLS
	{
		char *cur = inifile_get("languageSETTING", "system");
		int i;

		for (i = 0; i < PREF_LANGS; i++)
			if (!strcmp(pref_lang_ini_code[i], cur)) tdata.lang = i;
	}
#endif

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

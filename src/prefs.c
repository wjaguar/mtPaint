/*	prefs.c
	Copyright (C) 2005-2011 Mark Tyler and Dmitry Groshev

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

#include "prefs.h"


///	PREFERENCES WINDOW

GtkWidget *prefs_window, *prefs_status[STATUS_ITEMS];
static GtkWidget *spinbutton_maxmem, *spinbutton_maxundo, *spinbutton_commundo;
#ifdef U_THREADS
static GtkWidget *spinbutton_threads;
#endif
static GtkWidget *spinbutton_greys, *spinbutton_nudge, *spinbutton_pan;
static GtkWidget *spinbutton_trans, *spinbutton_hotx, *spinbutton_hoty,
	*spinbutton_jpeg, *spinbutton_jp2, *spinbutton_png, *spinbutton_recent,
	*spinbutton_silence;
static GtkWidget *checkbutton_tgaRLE, *checkbutton_tga565, *checkbutton_tgadef,
	*checkbutton_undo;
#ifdef U_LCMS
static GtkWidget *checkbutton_icc;
#endif

static GtkWidget *checkbutton_paste, *checkbutton_cursor, *checkbutton_exit, *checkbutton_quit;
static GtkWidget *checkbutton_zoom[4],		// zoom 100%, wheel, optimize cheq, disable trans
	*checkbutton_commit, *checkbutton_center, *checkbutton_gamma, *checkbutton_czoom;
#if GTK_MAJOR_VERSION == 2
static GtkWidget *checkbutton_menuicons, *entry_theme;
#endif
static GtkWidget *clipboard_entry, *entry_handbook[2], *entry_def[2];
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

char *pref_lang_ini_code[PREF_LANGS] = { "system",
	"zh_CN.utf8", "zh_TW.utf8",
	"cs_CZ", "nl_NL", "en_GB", "fr_FR",
	"gl_ES", "de_DE", "hu_HU", "it_IT",
	"ja_JP.utf8", "pl_PL", "pt_PT",
	"pt_BR", "ru_RU", "sk_SK",
	"es_ES", "sv_SE", "tl_PH", "tr_TR" };

int pref_lang;

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

static void delete_prefs(GtkWidget *widget)
{
	if (inputd) delete_inputd();
	destroy_dialog(prefs_window);
	gtk_widget_set_sensitive(menu_widgets[MENU_PREFS], TRUE);
	clipboard_entry = NULL;
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



static void prefs_apply(GtkWidget *widget)
{
	char path[PATHBUF];
	int i, j, xpm_trans;

	for ( i=0; i<STATUS_ITEMS; i++ )
	{
		status_on[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(prefs_status[i]));
	}

	mem_undo_limit = read_spin(spinbutton_maxmem);
	mem_undo_depth = read_spin(spinbutton_maxundo);
	mem_undo_common = read_spin(spinbutton_commundo);
	mem_background = read_spin(spinbutton_greys);
	mem_nudge = read_spin(spinbutton_nudge);
	max_pan = read_spin(spinbutton_pan);
#ifdef U_THREADS
	maxthreads = read_spin(spinbutton_threads);
#endif
	xpm_trans = read_spin(spinbutton_trans);
	mem_xbm_hot_x = read_spin(spinbutton_hotx);
	mem_xbm_hot_y = read_spin(spinbutton_hoty);
	jpeg_quality = read_spin(spinbutton_jpeg);
	jp2_rate = read_spin(spinbutton_jp2);
	png_compression = read_spin(spinbutton_png);
	recent_files = read_spin(spinbutton_recent);
	silence_limit = read_spin(spinbutton_silence);

	for (i = 0; i < 3; i++)
	{
		tablet_tool_use[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_tablet[i]));
		inifile_set_gboolean( tablet_ini2[i], tablet_tool_use[i] );
		j = mt_spinslide_get_value(hscale_tablet[i]);
		inifile_set_gint32(tablet_ini[i], j);
		tablet_tool_factor[i] = j / 100.0;
	}


	tga_RLE = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_tgaRLE)) ? 1 : 0;
	tga_565 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_tga565));
	tga_defdir = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_tgadef));
	undo_load = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_undo));
#ifdef U_LCMS
	apply_icc = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_icc));
#endif

	show_paste = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_paste));
	cursor_tool = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_cursor));
	inifile_set_gboolean( "exitToggle",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_exit)) );

	inifile_set_gboolean( "zoomToggle",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_zoom[0])) );
#if GTK_MAJOR_VERSION == 2
	inifile_set_gboolean( "scrollwheelZOOM",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_zoom[1])) );
#endif
	chequers_optimize = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_zoom[2]));
	opaque_view = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_zoom[3]));

	paste_commit = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_commit));

	q_quit = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_quit));

	cursor_zoom = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_czoom));
	inifile_set_gboolean("centerSettings",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_center)));
	use_gamma = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_gamma));
#if GTK_MAJOR_VERSION == 2
	show_menu_icons = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_menuicons));
#endif


#ifdef U_NLS
	inifile_set("languageSETTING", pref_lang_ini_code[pref_lang]);
	setup_language();
	string_init();				// Translate static strings
#endif

	gtkncpy(mem_clip_file, gtk_entry_get_text(GTK_ENTRY(clipboard_entry)), PATHBUF);
	inifile_set("clipFilename", mem_clip_file);

	gtkncpy(path, gtk_entry_get_text(GTK_ENTRY(entry_handbook[0])), PATHBUF);
	inifile_set(HANDBOOK_BROWSER_INI, path);

	gtkncpy(path, gtk_entry_get_text(GTK_ENTRY(entry_handbook[1])), PATHBUF);
	inifile_set(HANDBOOK_LOCATION_INI, path);

	gtkncpy(path, gtk_entry_get_text(GTK_ENTRY(entry_def[0])), PATHBUF);
	if (strcmp(path, inifile_get(DEFAULT_PAL_INI, "")))
		load_def_palette(path);
	inifile_set(DEFAULT_PAL_INI, path);

	gtkncpy(path, gtk_entry_get_text(GTK_ENTRY(entry_def[1])), PATHBUF);
	if (strcmp(path, inifile_get(DEFAULT_PAT_INI, "")))
		load_def_patterns(path);
	inifile_set(DEFAULT_PAT_INI, path);

#if GTK_MAJOR_VERSION == 2
	gtkncpy(path, gtk_entry_get_text(GTK_ENTRY(entry_theme)), PATHBUF);
	inifile_set(DEFAULT_THEME_INI, path);
#endif

	update_stuff(UPD_PREFS);
	/* Apply this undoable setting after everything else */
	mem_set_trans(xpm_trans);
}

static void prefs_ok(GtkWidget *widget)
{
	prefs_apply(NULL);
	delete_prefs(NULL);
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

/* Try to avoid scrolling - request full size of contents */
static void pref_scroll_size_req(GtkWidget *widget, GtkRequisition *requisition,
	gpointer user_data)
{
	GtkWidget *child = GTK_BIN(widget)->child;

	if (child && GTK_WIDGET_VISIBLE(child))
	{
		GtkRequisition wreq;
		int n, border = GTK_CONTAINER(widget)->border_width * 2;

		gtk_widget_get_child_requisition(child, &wreq);
		n = wreq.width + border;
		if (requisition->width < n) requisition->width = n;
		n = wreq.height + border;
		if (requisition->height < n) requisition->height = n;
	}
}

void pressed_preferences()
{
	int i;
#ifdef U_NLS
	char *pref_langs[PREF_LANGS] = { _("Default System Language"),
		_("Chinese (Simplified)"), _("Chinese (Taiwanese)"),
		_("Czech"), _("Dutch"), _("English (UK)"), _("French"),
		_("Galician"), _("German"), _("Hungarian"), _("Italian"),
		_("Japanese"), _("Polish"), _("Portuguese"),
		_("Portuguese (Brazilian)"), _("Russian"), _("Slovak"),
		_("Spanish"), _("Swedish"), _("Tagalog"), _("Turkish") };
#endif


	GtkWidget *vbox3, *hbox4, *table3, *table4, *drawingarea_tablet;
	GtkWidget *button1, *notebook1, *page, *vbox_2, *label, *scroll;

	char *tab_tex2[] = { _("Transparency index"), _("XBM X hotspot"), _("XBM Y hotspot"),
		_("JPEG Save Quality (100=High)"), _("JPEG2000 Compression (0=Lossless)"),
		_("PNG Compression (0=None)"), _("Recently Used Files"),
		_("Progress bar silence limit") };
	char *stat_tex[] = { _("Canvas Geometry"), _("Cursor X,Y"),
		_("Pixel [I] {RGB}"), _("Selection Geometry"), _("Undo / Redo") },
		*tablet_txt[] = { _("Size"), _("Flow"), _("Opacity") };
	char txt[PATHTXT];


	// Make sure the user can only open 1 prefs window
	gtk_widget_set_sensitive(menu_widgets[MENU_PREFS], FALSE);

	prefs_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Preferences"), GTK_WIN_POS_CENTER, FALSE );
	// Let user and WM make window smaller - contents are scrollable
	gtk_window_set_policy(GTK_WINDOW(prefs_window), TRUE, TRUE, FALSE);
	vbox3 = add_vbox(prefs_window);

///	SETUP SCROLLING

	scroll = xpack(vbox3, gtk_scrolled_window_new(NULL, NULL));
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scroll),
		GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
	gtk_signal_connect(GTK_OBJECT(scroll), "size_request",
		GTK_SIGNAL_FUNC(pref_scroll_size_req), NULL);

///	SETUP NOTEBOOK
	
	notebook1 = gtk_notebook_new();
	gtk_notebook_set_tab_pos(GTK_NOTEBOOK(notebook1), GTK_POS_LEFT);
//	gtk_notebook_set_scrollable(GTK_NOTEBOOK(notebook1), TRUE);
	gtk_scrolled_window_add_with_viewport(GTK_SCROLLED_WINDOW(scroll), notebook1);
	gtk_viewport_set_shadow_type(GTK_VIEWPORT(GTK_BIN(scroll)->child), GTK_SHADOW_NONE);
	gtk_widget_show_all(scroll);
	vport_noshadow_fix(GTK_BIN(scroll)->child);

///	---- TAB1 - GENERAL

	page = add_new_page(notebook1, _("General"));
#ifdef U_THREADS
	table3 = add_a_table(4, 2, 10, page);
	add_to_table(_("Max threads (0 to autodetect)"), table3, 3, 0, 5);
	spinbutton_threads = spin_to_table(table3, 3, 1, 5, maxthreads, 0, 256);
#else
	table3 = add_a_table(3, 2, 10, page);
#endif

///	TABLE TEXT
	add_to_table(_("Max memory used for undo (MB)"), table3, 0, 0, 5);
	add_to_table(_("Max undo levels"), table3, 1, 0, 5);
	add_to_table(_("Communal layer undo space (%)"), table3, 2, 0, 5);

///	TABLE SPINBUTTONS
	spinbutton_maxmem = spin_to_table(table3, 0, 1, 5, mem_undo_limit, 1, 2048);
	spinbutton_maxundo = spin_to_table(table3, 1, 1, 5, mem_undo_depth & ~1,
		MIN_UNDO & ~1, MAX_UNDO & ~1);
	spinbutton_commundo = spin_to_table(table3, 2, 1, 5, mem_undo_common, 0, 100);

	checkbutton_gamma = add_a_toggle(_("Use gamma correction by default"),
		page, use_gamma);
	checkbutton_zoom[2] = add_a_toggle( _("Optimize alpha chequers"),
		page, chequers_optimize );
	checkbutton_zoom[3] = add_a_toggle( _("Disable view window transparencies"),
		page, opaque_view );

///	LANGUAGE SWITCHBOX
#ifdef U_NLS
	vbox_2 = gtk_vbox_new(FALSE, 5);
	gtk_container_set_border_width(GTK_CONTAINER(vbox_2), 5);

	label = pack(vbox_2, gtk_label_new( _("Select preferred language translation\n\n"
		"You will need to restart mtPaint\nfor this to take full effect")));

	for (i = 0; i < PREF_LANGS; i++)
	{
		if (!strcmp(pref_lang_ini_code[i],
			inifile_get("languageSETTING", "system"))) break;
	}
	pack(vbox_2, wj_option_menu(pref_langs, PREF_LANGS, i, &pref_lang, NULL));

	gtk_widget_show_all(vbox_2);
	add_with_frame(page, _("Language"), vbox_2);
#endif

///	---- TAB2 - INTERFACE

	page = add_new_page(notebook1, _("Interface"));
	table3 = add_a_table(3, 2, 10, page);

///	TABLE TEXT
	add_to_table(_("Greyscale backdrop"), table3, 0, 0, 5);
	add_to_table(_("Selection nudge pixels"), table3, 1, 0, 5);
	add_to_table(_("Max Pan Window Size"), table3, 2, 0, 5);

///	TABLE SPINBUTTONS
	spinbutton_greys = spin_to_table(table3, 0, 1, 5, mem_background, 0, 255);
	spinbutton_nudge = spin_to_table(table3, 1, 1, 5, mem_nudge, 2, MAX_WIDTH);
	spinbutton_pan = spin_to_table(table3, 2, 1, 5, max_pan, 64, 256);

	checkbutton_paste = add_a_toggle( _("Display clipboard while pasting"),
		page, show_paste );
	checkbutton_cursor = add_a_toggle( _("Mouse cursor = Tool"),
		page, cursor_tool );
	checkbutton_exit = add_a_toggle( _("Confirm Exit"),
		page, inifile_get_gboolean("exitToggle", FALSE) );
	checkbutton_quit = add_a_toggle( _("Q key quits mtPaint"),
		page, q_quit );
	checkbutton_commit = add_a_toggle(_("Changing tool commits paste"),
		page, paste_commit);
	checkbutton_center = add_a_toggle(_("Centre tool settings dialogs"),
		page, inifile_get_gboolean("centerSettings", TRUE));
	checkbutton_zoom[0] = add_a_toggle( _("New image sets zoom to 100%"),
		page, inifile_get_gboolean("zoomToggle", FALSE) );
	checkbutton_czoom = add_a_toggle( _("Zoom on cursor position"), page, cursor_zoom);
#if GTK_MAJOR_VERSION == 2
	checkbutton_zoom[1] = add_a_toggle( _("Mouse Scroll Wheel = Zoom"),
		page, inifile_get_gboolean("scrollwheelZOOM", FALSE) );
	checkbutton_menuicons = add_a_toggle(_("Use menu icons"), page, show_menu_icons);
#endif

///	---- TAB3 - FILES

	page = add_new_page(notebook1, _("Files"));
	table4 = add_a_table(7, 2, 10, page);

	for (i = 0; i < 8; i++) add_to_table(tab_tex2[i], table4, i, 0, 4);

// !!! TODO: Change this into table-driven & see if code size decreases !!!
	spinbutton_trans   = spin_to_table(table4, 0, 1, 4, mem_xpm_trans, -1, mem_cols - 1);
	spinbutton_hotx    = spin_to_table(table4, 1, 1, 4, mem_xbm_hot_x, -1, mem_width - 1);
	spinbutton_hoty    = spin_to_table(table4, 2, 1, 4, mem_xbm_hot_y, -1, mem_height - 1);
	spinbutton_jpeg    = spin_to_table(table4, 3, 1, 4, jpeg_quality, 0, 100);
	spinbutton_jp2     = spin_to_table(table4, 4, 1, 4, jp2_rate, 0, 100);
	spinbutton_png     = spin_to_table(table4, 5, 1, 4, png_compression, 0, 9);
	spinbutton_recent  = spin_to_table(table4, 6, 1, 4, recent_files, 0, MAX_RECENT);
	spinbutton_silence = spin_to_table(table4, 7, 1, 4, silence_limit, 0, 28);
	checkbutton_tgaRLE = add_a_toggle(_("TGA RLE Compression"), page, tga_RLE);
	checkbutton_tga565 = add_a_toggle(_("Read 16-bit TGAs as 5:6:5 BGR"), page, tga_565);
	checkbutton_tgadef = add_a_toggle(_("Write TGAs in bottom-up row order"), page, tga_defdir);
	checkbutton_undo   = add_a_toggle(_("Undoable image loading"), page, undo_load);
#ifdef U_LCMS
	checkbutton_icc    = add_a_toggle(_("Apply colour profile"), page, apply_icc);
#endif

///	---- TAB4 - PATHS

	page = add_new_page(notebook1, _("Paths"));

	clipboard_entry = mt_path_box(_("Clipboard Files"), page,
		_("Select Clipboard File"), FS_CLIP_FILE);
	gtkuncpy(txt, mem_clip_file, PATHTXT);
	gtk_entry_set_text(GTK_ENTRY(clipboard_entry), txt);

	entry_handbook[0] = mt_path_box(_("HTML Browser Program"), page,
		_("Select Browser Program"), FS_SELECT_FILE);
	gtkuncpy(txt, inifile_get(HANDBOOK_BROWSER_INI, ""), PATHTXT);
	gtk_entry_set_text(GTK_ENTRY(entry_handbook[0]), txt);

	entry_handbook[1] = mt_path_box(_("Location of Handbook index"), page,
		_("Select Handbook Index File"), FS_SELECT_FILE);
	gtkuncpy(txt, inifile_get(HANDBOOK_LOCATION_INI, ""), PATHTXT);
	gtk_entry_set_text(GTK_ENTRY(entry_handbook[1]), txt);

	entry_def[0] = mt_path_box(_("Default Palette"), page,
		_("Select Default Palette"), FS_SELECT_FILE);
	gtkuncpy(txt, inifile_get(DEFAULT_PAL_INI, ""), PATHTXT);
	gtk_entry_set_text(GTK_ENTRY(entry_def[0]), txt);

	entry_def[1] = mt_path_box(_("Default Patterns"), page,
		_("Select Default Patterns File"), FS_SELECT_FILE);
	gtkuncpy(txt, inifile_get(DEFAULT_PAT_INI, ""), PATHTXT);
	gtk_entry_set_text(GTK_ENTRY(entry_def[1]), txt);

#if GTK_MAJOR_VERSION == 2
	entry_theme = mt_path_box(_("Default Theme"), page,
		_("Select Default Theme File"), FS_SELECT_FILE);
	gtkuncpy(txt, inifile_get(DEFAULT_THEME_INI, ""), PATHTXT);
	gtk_entry_set_text(GTK_ENTRY(entry_theme), txt);
#endif

///	---- TAB5 - STATUS BAR

	page = add_new_page(notebook1, _("Status Bar"));

	for ( i=0; i<STATUS_ITEMS; i++ )
	{
		prefs_status[i] = add_a_toggle( stat_tex[i], page, status_on[i] );
	}

///	---- TAB6 - TABLET

	page = add_new_page(notebook1, _("Tablet"));

	vbox_2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_2);
	add_with_frame(page, _("Device Settings"), vbox_2);
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
	add_with_frame(page, _("Test Area"), vbox_2);
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



///	Bottom of Prefs window

	hbox4 = pack(vbox3, OK_box(0, prefs_window, _("OK"), GTK_SIGNAL_FUNC(prefs_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(delete_prefs)));
	OK_box_add(hbox4, _("Apply"), GTK_SIGNAL_FUNC(prefs_apply));

	gtk_window_set_transient_for( GTK_WINDOW(prefs_window), GTK_WINDOW(main_window) );
	gtk_widget_show (prefs_window);

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

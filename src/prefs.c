/*	prefs.c
	Copyright (C) 2005-2007 Mark Tyler and Dmitry Groshev

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

#include "prefs.h"


///	PREFERENCES WINDOW

GtkWidget *prefs_window, *prefs_status[STATUS_ITEMS];
static GtkWidget *spinbutton_maxmem, *spinbutton_maxundo, *spinbutton_greys,
	*spinbutton_nudge, *spinbutton_pan;
static GtkWidget *spinbutton_trans, *spinbutton_hotx, *spinbutton_hoty,
	*spinbutton_jpeg, *spinbutton_jp2, *spinbutton_png, *spinbutton_recent,
	*spinbutton_silence;
static GtkWidget *checkbutton_tgaRLE, *checkbutton_tga565, *checkbutton_tgadef,
	*checkbutton_undo;

static GtkWidget *checkbutton_paste, *checkbutton_cursor, *checkbutton_exit, *checkbutton_quit;
static GtkWidget *checkbutton_zoom[4],		// zoom 100%, wheel, optimize cheq, disable trans
	*checkbutton_commit, *checkbutton_center, *checkbutton_gamma;
GtkWidget *clipboard_entry, *entry_handbook[2];
static GtkWidget *spinbutton_grid[4];
static GtkWidget *check_tablet[3], *hscale_tablet[3], *label_tablet_device, *label_tablet_pressure;

static char	*tablet_ini[] = { "tablet_value_size", "tablet_value_flow", "tablet_value_opacity" },
		*tablet_ini2[] = { "tablet_use_size", "tablet_use_flow", "tablet_use_opacity" };

#if GTK_MAJOR_VERSION == 1
static GdkDeviceInfo *tablet_device;
#endif
#if GTK_MAJOR_VERSION == 2
static GdkDevice *tablet_device;
#endif

int tablet_working;		// Has the device been initialized?

int tablet_tool_use[3];				// Size, flow, opacity
float tablet_tool_factor[3];			// Size, flow, opacity


#ifdef U_NLS

#define PREF_LANGS 13

char	*pref_lang_ini_code[PREF_LANGS] = { "system", "zh_CN.utf8", "zh_TW.utf8", "cs_CZ", "en_GB",
		"fr_FR", "de_DE", "pl_PL", "pt_PT", "pt_BR", "sk_SK", "es_ES", "tr_TR" };

int pref_lang;

#endif


static gint expose_tablet_preview( GtkWidget *widget, GdkEventExpose *event )
{
	unsigned char *rgb;
	int i, x = event->area.x, y = event->area.y, w = event->area.width, h = event->area.height;

	rgb = malloc( w*h*3 );
	if ( rgb == NULL ) return FALSE;

	for ( i=0; i<(w*h*3); i++ ) rgb[i] = 255;		// Pure white

	gdk_draw_rgb_image (widget->window, widget->style->black_gc,
			x, y, w, h, GDK_RGB_DITHER_NONE, rgb, w*3 );

	free( rgb );

	return FALSE;
}


static GtkWidget *inputd = NULL;


gint delete_inputd( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i, j;
	char txt[32];

#if GTK_MAJOR_VERSION == 1
	GdkDeviceInfo *dev = tablet_device;
#endif
#if GTK_MAJOR_VERSION == 2
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
#endif
#if GTK_MAJOR_VERSION == 2
			j = dev->axes[i].use;
#endif
			sprintf(txt, "tablet_axes_v%i", i);
			inifile_set_gint32( txt, j );
		}
	}

	gtk_widget_destroy(inputd);
	inputd = NULL;

	return FALSE;
}

static void delete_prefs(GtkWidget *widget)
{
	if ( inputd != NULL ) delete_inputd( NULL, NULL, NULL );
	gtk_widget_destroy(prefs_window);
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


gint conf_tablet( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	GtkAccelGroup* ag = gtk_accel_group_new();

	if (inputd != NULL) return FALSE;	// Stops multiple dialogs being opened

	inputd = gtk_input_dialog_new();
	gtk_window_set_position( GTK_WINDOW(inputd), GTK_WIN_POS_CENTER );

	gtk_signal_connect(GTK_OBJECT (GTK_INPUT_DIALOG (inputd)->close_button), "clicked",
		GTK_SIGNAL_FUNC(delete_inputd), (gpointer) inputd);
	gtk_widget_add_accelerator (GTK_INPUT_DIALOG (inputd)->close_button, "clicked",
		ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_signal_connect(GTK_OBJECT (inputd), "destroy",
		GTK_SIGNAL_FUNC(delete_inputd), (gpointer) inputd);

	gtk_signal_connect(GTK_OBJECT (inputd), "enable-device",
		GTK_SIGNAL_FUNC(tablet_enable_device), (gpointer) inputd);
	gtk_signal_connect(GTK_OBJECT (inputd), "disable-device",
		GTK_SIGNAL_FUNC(tablet_disable_device), (gpointer) inputd);

	if ( GTK_INPUT_DIALOG (inputd)->keys_list != NULL )
		gtk_widget_hide (GTK_INPUT_DIALOG (inputd)->keys_list);
	if ( GTK_INPUT_DIALOG (inputd)->keys_listbox != NULL )
		gtk_widget_hide (GTK_INPUT_DIALOG (inputd)->keys_listbox);

	gtk_widget_hide (GTK_INPUT_DIALOG (inputd)->save_button);

	gtk_widget_show (inputd);
	gtk_window_add_accel_group(GTK_WINDOW (inputd), ag);

	return FALSE;
}



static void prefs_apply(GtkWidget *widget)
{
	char path[PATHBUF];
	int i, j;

	for ( i=0; i<STATUS_ITEMS; i++ )
	{
		status_on[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(prefs_status[i]));
	}

	mem_undo_limit = read_spin(spinbutton_maxmem);
	mem_undo_depth = read_spin(spinbutton_maxundo);
	mem_background = read_spin(spinbutton_greys);
	mem_nudge = read_spin(spinbutton_nudge);
	mem_xpm_trans = read_spin(spinbutton_trans);
	mem_xbm_hot_x = read_spin(spinbutton_hotx);
	mem_xbm_hot_y = read_spin(spinbutton_hoty);
	jpeg_quality = read_spin(spinbutton_jpeg);
	jp2_rate = read_spin(spinbutton_jp2);
	png_compression = read_spin(spinbutton_png);
	recent_files = read_spin(spinbutton_recent);
	silence_limit = read_spin(spinbutton_silence);

	mem_grid_min = read_spin(spinbutton_grid[0]);
	for ( i=0; i<3; i++ )
		mem_grid_rgb[i] = read_spin(spinbutton_grid[i + 1]);

	for (i = 0; i < 3; i++)
	{
		tablet_tool_use[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_tablet[i]));
		inifile_set_gboolean( tablet_ini2[i], tablet_tool_use[i] );
		j = mt_spinslide_get_value(hscale_tablet[i]);
		inifile_set_gint32(tablet_ini[i], j);
		tablet_tool_factor[i] = j / 100.0;
	}

	inifile_set_gint32( "gridR", mem_grid_rgb[0] );
	inifile_set_gint32( "gridG", mem_grid_rgb[1] );
	inifile_set_gint32( "gridB", mem_grid_rgb[2] );


	inifile_set_gint32( "panSize", read_spin(spinbutton_pan));
	tga_RLE = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_tgaRLE)) ? 1 : 0;
	tga_565 = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_tga565));
	tga_defdir = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_tgadef));
	undo_load = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_undo));

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

	inifile_set_gboolean( "pasteCommit",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_commit)) );

	q_quit = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_quit));

	inifile_set_gboolean("centerSettings",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_center)));
	inifile_set_gboolean("defaultGamma",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_gamma)));

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

	update_undo_depth();		// If undo depth was changed
	update_all_views();		// Update canvas for changes
	set_cursor();

	update_recent_files();
	init_status_bar();
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

static gint tablet_preview_motion(GtkWidget *widget, GdkEventMotion *event)
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
#endif
#if GTK_MAJOR_VERSION == 2
	if (event->is_hint) gdk_device_get_state (event->device, event->window, NULL, &state);
	else state = event->state;

	gdk_event_get_axis ((GdkEvent *)event, GDK_AXIS_PRESSURE, &pressure);
#endif

	if (state & GDK_BUTTON1_MASK)
		tablet_update_pressure( pressure );
  
	return TRUE;
}

void pressed_preferences( GtkMenuItem *menu_item, gpointer user_data )
{
	int i;
#ifdef U_NLS
	char *pref_langs[PREF_LANGS] = { _("Default System Language"), _("Chinese (Simplified)"),
		 _("Chinese (Taiwanese)"), _("Czech"), _("English (UK)"),
		_("French"), _("German"), _("Polish"), _("Portuguese"), _("Portuguese (Brazilian)"),
		_("Slovak"), _("Spanish"), _("Turkish")
					};
#endif


	GtkWidget *vbox3, *hbox4, *table3, *table4, *table5, *drawingarea_tablet;
	GtkWidget *button1, *notebook1, *page, *vbox_2, *label;

	char *tab_tex[] = { _("Max memory used for undo (MB)"),
		_("Max undo levels"), _("Greyscale backdrop"),
		_("Selection nudge pixels"), _("Max Pan Window Size") };
	char *tab_tex2[] = { _("Transparency index"), _("XBM X hotspot"), _("XBM Y hotspot"),
//		_("JPEG Save Quality (100=High)   "),
		_("JPEG Save Quality (100=High)"), _("JPEG2000 Compression (0=Lossless)"),
		_("PNG Compression (0=None)"), _("Recently Used Files"),
		_("Progress bar silence limit") };
	char *tab_tex3[] = { _("Minimum grid zoom"), _("Grid colour RGB") };
	char *stat_tex[] = { _("Canvas Geometry"), _("Cursor X,Y"),
		_("Pixel [I] {RGB}"), _("Selection Geometry"), _("Undo / Redo") },
		*tablet_txt[] = { _("Size"), _("Flow"), _("Opacity") };
	char txt[PATHTXT];


	// Make sure the user can only open 1 prefs window
	gtk_widget_set_sensitive(menu_widgets[MENU_PREFS], FALSE);

	prefs_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Preferences"), GTK_WIN_POS_CENTER, FALSE );

	vbox3 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox3);
	gtk_container_add (GTK_CONTAINER (prefs_window), vbox3);

///	SETUP NOTEBOOK

	notebook1 = xpack(vbox3, gtk_notebook_new());
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook1), GTK_POS_TOP);
	gtk_widget_show (notebook1);

///	---- TAB1 - GENERAL

	page = add_new_page(notebook1, _("General"));
	table3 = add_a_table( 5, 2, 10, page );

///	TABLE TEXT
	for (i = 0; i < 5; i++ ) add_to_table( tab_tex[i], table3, i, 0, 5 );

///	TABLE SPINBUTTONS
	spinbutton_maxmem = spin_to_table(table3, 0, 1, 5, mem_undo_limit, 1, 1000);
	spinbutton_maxundo = spin_to_table(table3, 1, 1, 5, mem_undo_depth & ~1,
		MIN_UNDO & ~1, MAX_UNDO & ~1);
	spinbutton_greys = spin_to_table(table3, 2, 1, 5, mem_background, 0, 255);
	spinbutton_nudge = spin_to_table(table3, 3, 1, 5, mem_nudge, 2, 512);
	spinbutton_pan = spin_to_table(table3, 4, 1, 5,
		inifile_get_gint32("panSize", 128 ), 64, 256);

	checkbutton_paste = add_a_toggle( _("Display clipboard while pasting"),
		page, show_paste );
	checkbutton_cursor = add_a_toggle( _("Mouse cursor = Tool"),
		page, cursor_tool );
	checkbutton_exit = add_a_toggle( _("Confirm Exit"),
		page, inifile_get_gboolean("exitToggle", FALSE) );
	checkbutton_quit = add_a_toggle( _("Q key quits mtPaint"),
		page, q_quit );
	checkbutton_commit = add_a_toggle( _("Changing tool commits paste"),
		page, inifile_get_gboolean("pasteCommit", FALSE) );
	checkbutton_center = add_a_toggle(_("Centre tool settings dialogs"),
		page, inifile_get_gboolean("centerSettings", TRUE));
	checkbutton_gamma = add_a_toggle(_("Use gamma correction by default"),
		page, inifile_get_gboolean("defaultGamma", FALSE));


///	---- TAB2 - FILES

	page = add_new_page(notebook1, _("Files"));
	table4 = add_a_table(7, 2, 10, page);

	for (i = 0; i < 8; i++) add_to_table(tab_tex2[i], table4, i, 0, 4);

// !!! TODO: Change this into table-driven & see if code size decreases !!!
	spinbutton_trans   = spin_to_table(table4, 0, 1, 4, mem_xpm_trans, -1, mem_cols - 1);
	spinbutton_hotx    = spin_to_table(table4, 1, 1, 4, mem_xbm_hot_x, -1, mem_width - 1);
	spinbutton_hoty    = spin_to_table(table4, 2, 1, 4, mem_xbm_hot_y, -1, mem_height - 1);
	spinbutton_jpeg    = spin_to_table(table4, 3, 1, 4, jpeg_quality, 0, 100);
	spinbutton_jp2     = spin_to_table(table4, 4, 1, 4, jp2_rate, 1, 100);
	spinbutton_png     = spin_to_table(table4, 5, 1, 4, png_compression, 0, 9);
	spinbutton_recent  = spin_to_table(table4, 6, 1, 4, recent_files, 0, MAX_RECENT);
	spinbutton_silence = spin_to_table(table4, 7, 1, 4, silence_limit, 0, 28);
	checkbutton_tgaRLE = add_a_toggle(_("TGA RLE Compression"), page, tga_RLE);
	checkbutton_tga565 = add_a_toggle(_("Read 16-bit TGAs as 5:6:5 BGR"), page, tga_565);
	checkbutton_tgadef = add_a_toggle(_("Write TGAs in bottom-up row order"), page, tga_defdir);
	checkbutton_undo   = add_a_toggle(_("Undoable image loading"), page, undo_load);

///	---- TAB3 - PATHS

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

///	---- TAB4 - STATUS BAR

	page = add_new_page(notebook1, _("Status Bar"));

	for ( i=0; i<STATUS_ITEMS; i++ )
	{
		prefs_status[i] = add_a_toggle( stat_tex[i], page, status_on[i] );
	}

///	---- TAB5 - ZOOM

	page = add_new_page(notebook1, _("Zoom"));
	table5 = add_a_table( 2, 4, 10, page );

///	TABLE TEXT
	for ( i=0; i<2; i++ ) add_to_table( tab_tex3[i], table5, i, 0, 5 );

///	TABLE SPINBUTTONS
	spinbutton_grid[0] = spin_to_table(table5, 0, 1, 5, mem_grid_min, 2, 12);
	for (i = 0; i < 3; i++)
	{
		spinbutton_grid[i + 1] = spin_to_table(table5, 1, i + 1, 5,
			mem_grid_rgb[i], 0, 255);
	}

	checkbutton_zoom[0] = add_a_toggle( _("New image sets zoom to 100%"),
		page, inifile_get_gboolean("zoomToggle", FALSE) );
#if GTK_MAJOR_VERSION == 2
	checkbutton_zoom[1] = add_a_toggle( _("Mouse Scroll Wheel = Zoom"),
		page, inifile_get_gboolean("scrollwheelZOOM", TRUE) );
#endif
	checkbutton_zoom[2] = add_a_toggle( _("Optimize alpha chequers"),
		page, chequers_optimize );
	checkbutton_zoom[3] = add_a_toggle( _("Disable view window transparencies"),
		page, opaque_view );



///	---- TAB6 - TABLET

	page = add_new_page(notebook1, _("Tablet"));

	vbox_2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_2);
	add_with_frame(page, _("Device Settings"), vbox_2, 5);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_2), 5);

	label_tablet_device = pack(vbox_2, gtk_label_new(""));
	gtk_widget_show (label_tablet_device);
	gtk_misc_set_alignment (GTK_MISC (label_tablet_device), 0, 0.5);
	gtk_misc_set_padding (GTK_MISC (label_tablet_device), 5, 5);

	button1 = add_a_button( _("Configure Device"), 0, vbox_2, FALSE );
	gtk_signal_connect(GTK_OBJECT(button1), "clicked", GTK_SIGNAL_FUNC(conf_tablet), NULL);

	table3 = xpack(vbox_2, gtk_table_new(4, 2, FALSE));
	gtk_widget_show (table3);

	label = add_to_table( _("Tool Variable"), table3, 0, 0, 0 );
	gtk_misc_set_padding (GTK_MISC (label), 5, 5);
	snprintf(txt, 60, "%s, %%", _("Factor"));
	label = add_to_table( txt, table3, 0, 1, 0 );
	gtk_misc_set_padding (GTK_MISC (label), 5, 5);
	gtk_misc_set_alignment (GTK_MISC (label), 0.4, 0.5);

	for ( i=0; i<3; i++ )
	{
		check_tablet[i] = gtk_check_button_new_with_label (tablet_txt[i]);
		gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(check_tablet[i]),
			tablet_tool_use[i] );
		gtk_widget_show (check_tablet[i]);
		gtk_table_attach (GTK_TABLE (table3), check_tablet[i], 0, 1, i+1, i+2,
			(GtkAttachOptions) (GTK_FILL),
			(GtkAttachOptions) (0), 0, 0);

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
	add_with_frame(page, _("Test Area"), vbox_2, 5);
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



///	---- TAB7 - LANGUAGE

#ifdef U_NLS

	page = add_new_page(notebook1, _("Language"));
	gtk_container_set_border_width(GTK_CONTAINER(page), 10);

	add_hseparator( page, 200, 10 );
	label = pack(page, gtk_label_new( _("Select preferred language translation\n\n"
				"You will need to restart mtPaint\nfor this to take full effect")));
	gtk_widget_show (label);
	add_hseparator( page, 200, 10 );

	for (i = 0; i < PREF_LANGS; i++)
	{
		if (!strcmp(pref_lang_ini_code[i],
			inifile_get("languageSETTING", "system"))) break;
	}
	xpack(page, wj_radio_pack(pref_langs, PREF_LANGS, 7, i, &pref_lang, NULL));

#endif



///	Bottom of Prefs window

	hbox4 = pack(vbox3, OK_box(0, prefs_window, _("OK"), GTK_SIGNAL_FUNC(prefs_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(delete_prefs)));
	OK_box_add(hbox4, _("Apply"), GTK_SIGNAL_FUNC(prefs_apply), 1);

	gtk_window_set_transient_for( GTK_WINDOW(prefs_window), GTK_WINDOW(main_window) );
	gtk_widget_show (prefs_window);

	if ( tablet_working )
	{
		tablet_update_device( tablet_device->name );
	} else	tablet_update_device( "NONE" );
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

/*	prefs.c
	Copyright (C) 2005-2006 Mark Tyler and Dmitry Groshev

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

#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

#include "global.h"
#include "mygtk.h"
#include "memory.h"
#include "png.h"
#include "canvas.h"
#include "inifile.h"
#include "viewer.h"
#include "mainwindow.h"


///	PREFERENCES WINDOW

GtkWidget *prefs_window, *prefs_status[STATUS_ITEMS];
static GtkWidget *spinbutton_maxmem, *spinbutton_greys, *spinbutton_nudge, *spinbutton_pan;
static GtkWidget *spinbutton_trans, *spinbutton_hotx, *spinbutton_hoty, *spinbutton_jpeg, *spinbutton_recent;
static GtkWidget *checkbutton_paste, *checkbutton_cursor, *checkbutton_exit, *checkbutton_quit;
static GtkWidget *checkbutton_zoom[4],		// zoom 100%, wheel, optimize cheq, disable trans
	*checkbutton_commit, *checkbutton_center, *checkbutton_gamma;
GtkWidget *clipboard_entry;
static GtkWidget *spinbutton_grid[4];
static GtkWidget *check_tablet[3], *hscale_tablet[3], *label_tablet_device, *label_tablet_pressure;

static char	*tablet_ini[] = { "tablet_value_size", "tablet_value_flow", "tablet_value_opacity" },
		*tablet_ini2[] = { "tablet_use_size", "tablet_use_flow", "tablet_use_opacity" },
		*tablet_ini3[] = { "tablet_name", "tablet_mode", "tablet_axes_v" };

#if GTK_MAJOR_VERSION == 1
static GdkDeviceInfo *tablet_device = NULL;
#endif
#if GTK_MAJOR_VERSION == 2
static GdkDevice *tablet_device = NULL;
#endif

gboolean tablet_working = FALSE;		// Has the device been initialized?

gboolean tablet_tool_use[3];			// Size, flow, opacity
float tablet_tool_factor[3];			// Size, flow, opacity


#ifdef U_NLS

#define PREF_LANGS 8

char	*pref_lang_ini_code[PREF_LANGS] = { "system", "cs_CZ", "en_GB", "fr_FR", "de_DE", 
		"pt_PT", "pt_BR", "es_ES" };

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

gint clip_file_browse( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	file_selector( FS_CLIP_FILE );

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
		inifile_set( tablet_ini3[0], dev->name );
		inifile_set_gint32( tablet_ini3[1], dev->mode );

		for ( i=0; i<dev->num_axes; i++ )
		{
#if GTK_MAJOR_VERSION == 1
			j = dev->axes[i];
#endif
#if GTK_MAJOR_VERSION == 2
			j = dev->axes[i].use;
#endif
			sprintf(txt, "%s%i", tablet_ini3[2], i);
			inifile_set_gint32( txt, j );
		}
	}

	inifile_set_gboolean( "tablet_USE", tablet_working );
	gtk_widget_destroy(inputd);
	inputd = NULL;

	return FALSE;
}

gint delete_prefs( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	if ( inputd != NULL ) delete_inputd( NULL, NULL, NULL );
	gtk_widget_destroy(prefs_window);
	men_item_state( menu_prefs, TRUE );
	clipboard_entry = NULL;

	return FALSE;
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



gint prefs_apply( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	int i;
	char txt[64];

	for ( i=0; i<STATUS_ITEMS; i++ )
	{
		sprintf(txt, "status%iToggle", i);
		inifile_set_gboolean( txt,
			gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(prefs_status[i])) );
		status_on[i] = inifile_get_gboolean(txt, TRUE);
	}

	mem_undo_limit = read_spin(spinbutton_maxmem);
	mem_background = read_spin(spinbutton_greys);
	mem_nudge = read_spin(spinbutton_nudge);
	mem_xpm_trans = read_spin(spinbutton_trans);
	mem_xbm_hot_x = read_spin(spinbutton_hotx);
	mem_xbm_hot_y = read_spin(spinbutton_hoty);
	mem_jpeg_quality = read_spin(spinbutton_jpeg);
	recent_files = read_spin(spinbutton_recent);
	mem_grid_min = read_spin(spinbutton_grid[0]);
	for ( i=0; i<3; i++ )
		mem_grid_rgb[i] = read_spin(spinbutton_grid[i + 1]);

	for ( i=0; i<3; i++ )
	{
		tablet_tool_use[i] = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_tablet[i]));
		inifile_set_gboolean( tablet_ini2[i], tablet_tool_use[i] );
		tablet_tool_factor[i] = GTK_HSCALE(hscale_tablet[i])->scale.range.adjustment->value;
		inifile_set_gint32( tablet_ini[i], tablet_tool_factor[i]*100 );
	}

	inifile_set_gint32( "gridMin", mem_grid_min );
	inifile_set_gint32( "gridR", mem_grid_rgb[0] );
	inifile_set_gint32( "gridG", mem_grid_rgb[1] );
	inifile_set_gint32( "gridB", mem_grid_rgb[2] );


	inifile_set_gint32( "panSize", read_spin(spinbutton_pan));
	inifile_set_gint32( "undoMBlimit", mem_undo_limit );
	inifile_set_gint32( "backgroundGrey", mem_background );
	inifile_set_gint32( "pixelNudge", mem_nudge );
	inifile_set_gint32( "jpegQuality", mem_jpeg_quality );
	inifile_set_gint32( "recentFiles", recent_files );

	inifile_set_gboolean( "pasteToggle",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_paste)) );
	inifile_set_gboolean( "cursorToggle",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_cursor)) );
	inifile_set_gboolean( "exitToggle",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_exit)) );

	inifile_set_gboolean( "zoomToggle",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_zoom[0])) );
#if GTK_MAJOR_VERSION == 2
	inifile_set_gboolean( "scrollwheelZOOM",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_zoom[1])) );
#endif
	chequers_optimize = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_zoom[2]));
	inifile_set_gboolean( "optimizeChequers", chequers_optimize );
	opaque_view = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_zoom[3]));
	inifile_set_gboolean( "disableTransparency", FALSE );

	inifile_set_gboolean( "pasteCommit",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_commit)) );

	q_quit = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_quit));
	inifile_set_gboolean( "quitToggle", q_quit );

	inifile_set_gboolean("centerSettings",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_center)));
	inifile_set_gboolean("defaultGamma",
		gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checkbutton_gamma)));

#ifdef U_NLS
	inifile_set("languageSETTING", pref_lang_ini_code[pref_lang]);
	setup_language();
#endif

	strncpy(mem_clip_file, gtk_entry_get_text(GTK_ENTRY(clipboard_entry)), 250);
	inifile_set("clipFilename", mem_clip_file);

	show_paste = inifile_get_gboolean( "pasteToggle", TRUE );

	update_all_views();		// Update canvas for changes
	set_cursor();

	update_recent_files();
	init_status_bar();

	return FALSE;
}

gint prefs_ok( GtkWidget *widget, GdkEvent *event, gpointer data )
{
	prefs_apply( NULL, NULL, NULL );
	delete_prefs( NULL, NULL, NULL );

	return FALSE;
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
	char *pref_langs[PREF_LANGS] = { _("Default System Language"), _("Czech"), _("English (UK)"),
		_("French"), _("German"), _("Portuguese"), _("Portuguese (Brazilian)"), _("Spanish")
					};
#endif


	GtkWidget *vbox3, *hbox4, *table3, *table4, *table5, *drawingarea_tablet, *frame;
	GtkWidget *button1, *button2, *notebook1, *vbox_1, *vbox_2, *vbox_3, *label;
	GtkAccelGroup* ag = gtk_accel_group_new();

	char *tab_tex[] = { _("Max memory used for undo (MB)"), _("Greyscale backdrop"),
		_("Selection nudge pixels"), _("Max Pan Window Size") };
	char *tab_tex2[] = { _("Transparency index"), _("XBM X hotspot"), _("XBM Y hotspot"),
		_("JPEG Save Quality (100=High)   "), _("Recently Used Files") };
	char *tab_tex3[] = { _("Minimum grid zoom"), _("Grid colour RGB") };
	char *stat_tex[] = { _("Canvas Geometry"), _("Cursor X,Y"),
		_("Pixel [I] {RGB}"), _("Selection Geometry"), _("Undo / Redo") },
		*tablet_txt[] = { _("Size"), _("Flow"), _("Opacity") };
	char txt[64];


	men_item_state( menu_prefs, FALSE );	// Make sure the user can only open 1 prefs window

	prefs_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Preferences"), GTK_WIN_POS_CENTER, FALSE );

	vbox3 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox3);
	gtk_container_add (GTK_CONTAINER (prefs_window), vbox3);

///	SETUP NOTEBOOK

	notebook1 = gtk_notebook_new ();
	gtk_box_pack_start (GTK_BOX (vbox3), notebook1, TRUE, TRUE, 0);
	gtk_notebook_set_tab_pos (GTK_NOTEBOOK (notebook1), GTK_POS_TOP);
	gtk_widget_show (notebook1);

///	---- TAB1 - GENERAL

	vbox_1 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_1);
	gtk_container_add (GTK_CONTAINER (notebook1), vbox_1);

	label = gtk_label_new( _("General") );
	gtk_widget_show (label);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK (notebook1),
		gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook1), 0), label);


	table3 = add_a_table( 3, 2, 10, vbox_1 );

///	TABLE TEXT
	for ( i=0; i<4; i++ ) add_to_table( tab_tex[i], table3, i, 0, 5, GTK_JUSTIFY_LEFT, 0, 0.5 );

///	TABLE SPINBUTTONS
	spin_to_table( table3, &spinbutton_maxmem, 0, 1, 5, inifile_get_gint32("undoMBlimit", 32 ), 1, 1000 );
	spin_to_table( table3, &spinbutton_greys, 1, 1, 5, inifile_get_gint32("backgroundGrey", 180 ), 0, 255 );
	spin_to_table( table3, &spinbutton_nudge, 2, 1, 5, inifile_get_gint32("pixelNudge", 8 ), 2, 512 );
	spin_to_table( table3, &spinbutton_pan, 3, 1, 5, inifile_get_gint32("panSize", 128 ), 64, 256 );

	checkbutton_paste = add_a_toggle( _("Display clipboard while pasting"),
		vbox_1, inifile_get_gboolean("pasteToggle", TRUE) );
	checkbutton_cursor = add_a_toggle( _("Mouse cursor = Tool"),
		vbox_1, inifile_get_gboolean("cursorToggle", TRUE) );
	checkbutton_exit = add_a_toggle( _("Confirm Exit"),
		vbox_1, inifile_get_gboolean("exitToggle", FALSE) );
	checkbutton_quit = add_a_toggle( _("Q key quits mtPaint"),
		vbox_1, inifile_get_gboolean("quitToggle", TRUE) );
	checkbutton_commit = add_a_toggle( _("Changing tool commits paste"),
		vbox_1, inifile_get_gboolean("pasteCommit", FALSE) );
	checkbutton_center = add_a_toggle(_("Center tool settings dialogs"),
		vbox_1, inifile_get_gboolean("centerSettings", TRUE));
	checkbutton_gamma = add_a_toggle(_("Use gamma correction by default"),
		vbox_1, inifile_get_gboolean("defaultGamma", FALSE));


///	---- TAB2 - FILES

	vbox_2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_2);
	gtk_container_add (GTK_CONTAINER (notebook1), vbox_2);

	label = gtk_label_new( _("Files") );
	gtk_widget_show (label);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK (notebook1),
		gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook1), 1), label);

	table4 = add_a_table( 5, 2, 10, vbox_2 );

	for ( i=0; i<5; i++ ) add_to_table( tab_tex2[i], table4, i, 0, 0, GTK_JUSTIFY_LEFT, 0, 0.5 );

	spin_to_table( table4, &spinbutton_trans, 0, 1, 5, mem_xpm_trans, -1, mem_cols-1 );
	spin_to_table( table4, &spinbutton_hotx, 1, 1, 5, mem_xbm_hot_x, -1, mem_width-1 );
	spin_to_table( table4, &spinbutton_hoty, 2, 1, 5, mem_xbm_hot_y, -1, mem_height-1 );
	spin_to_table( table4, &spinbutton_jpeg, 3, 1, 5, mem_jpeg_quality, 0, 100 );
	spin_to_table( table4, &spinbutton_recent, 4, 1, 5, recent_files, 0, MAX_RECENT );

	add_hseparator( vbox_2, -2, 10 );
	label = gtk_label_new( _("Clipboard Files") );
	gtk_widget_show( label );
	gtk_box_pack_start( GTK_BOX(vbox_2), label, FALSE, FALSE, 0 );

	clipboard_entry = gtk_entry_new();
	gtk_widget_show( clipboard_entry );
	gtk_box_pack_start( GTK_BOX(vbox_2), clipboard_entry, FALSE, FALSE, 0 );
	gtk_entry_set_text(GTK_ENTRY(clipboard_entry), mem_clip_file);

	button1 = add_a_button( _("Browse"), 4, vbox_2, FALSE );
	gtk_signal_connect(GTK_OBJECT(button1), "clicked",
		GTK_SIGNAL_FUNC(clip_file_browse), NULL);

///	---- TAB3 - STATUS BAR

	hbox4 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox4);
	gtk_container_add (GTK_CONTAINER (notebook1), hbox4);

	vbox_3 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_3);
	gtk_box_pack_start (GTK_BOX (hbox4), vbox_3, FALSE, FALSE, 0);

	label = gtk_label_new( _("Status Bar") );
	gtk_widget_show (label);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK (notebook1),
		gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook1), 2), label);

	for ( i=0; i<STATUS_ITEMS; i++ )
	{
		sprintf(txt, "status%iToggle", i);
		prefs_status[i] = add_a_toggle( stat_tex[i], vbox_3, inifile_get_gboolean(txt, TRUE) );
	}

///	---- TAB4 - ZOOM

	hbox4 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox4);
	gtk_container_add (GTK_CONTAINER (notebook1), hbox4);

	vbox_3 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_3);
	gtk_box_pack_start (GTK_BOX (hbox4), vbox_3, FALSE, FALSE, 0);

	label = gtk_label_new( _("Zoom") );
	gtk_widget_show (label);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK (notebook1),
		gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook1), 3), label);
	table5 = add_a_table( 2, 4, 10, vbox_3 );

///	TABLE TEXT
	for ( i=0; i<2; i++ ) add_to_table( tab_tex3[i], table5, i, 0, 5, GTK_JUSTIFY_LEFT, 0, 0.5 );

///	TABLE SPINBUTTONS
	spin_to_table( table5, &spinbutton_grid[0], 0, 1, 5, mem_grid_min, 2, 12 );
	spin_to_table( table5, &spinbutton_grid[1], 1, 1, 5, mem_grid_rgb[0], 0, 255 );
	spin_to_table( table5, &spinbutton_grid[2], 1, 2, 5, mem_grid_rgb[1], 0, 255 );
	spin_to_table( table5, &spinbutton_grid[3], 1, 3, 5, mem_grid_rgb[2], 0, 255 );

	checkbutton_zoom[0] = add_a_toggle( _("New image sets zoom to 100%"),
		vbox_3, inifile_get_gboolean("zoomToggle", FALSE) );
#if GTK_MAJOR_VERSION == 2
	checkbutton_zoom[1] = add_a_toggle( _("Mouse Scroll Wheel = Zoom"),
		vbox_3, inifile_get_gboolean("scrollwheelZOOM", TRUE) );
#endif
	checkbutton_zoom[2] = add_a_toggle( _("Optimize alpha chequers"),
		vbox_3, chequers_optimize );
	checkbutton_zoom[3] = add_a_toggle( _("Disable view window transparencies"),
		vbox_3, opaque_view );



///	---- TAB5 - TABLET

	hbox4 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox4);
	gtk_container_add (GTK_CONTAINER (notebook1), hbox4);

	vbox_3 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_3);
	gtk_box_pack_start (GTK_BOX (hbox4), vbox_3, FALSE, FALSE, 0);

	label = gtk_label_new( _("Tablet") );
	gtk_widget_show (label);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK (notebook1),
		gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook1), 4), label);



	frame = gtk_frame_new (_("Device Settings"));
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (vbox_3), frame, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	vbox_2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_2);
	gtk_container_add (GTK_CONTAINER (frame), vbox_2);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_2), 5);

	label_tablet_device = gtk_label_new ("");
	gtk_widget_show (label_tablet_device);
	gtk_box_pack_start (GTK_BOX (vbox_2), label_tablet_device, FALSE, FALSE, 0);
	gtk_misc_set_alignment (GTK_MISC (label_tablet_device), 0, 0.5);
	gtk_misc_set_padding (GTK_MISC (label_tablet_device), 5, 5);

	button1 = add_a_button( _("Configure Device"), 0, vbox_2, FALSE );
	gtk_signal_connect(GTK_OBJECT(button1), "clicked", GTK_SIGNAL_FUNC(conf_tablet), NULL);

	table3 = gtk_table_new (4, 2, FALSE);
	gtk_widget_show (table3);
	gtk_box_pack_start (GTK_BOX (vbox_2), table3, TRUE, TRUE, 0);

	label = add_to_table( _("Tool Variable"), table3, 0, 0, 0, 0, 0, 0 );
	gtk_misc_set_padding (GTK_MISC (label), 5, 5);
	label = add_to_table( _("Factor"), table3, 0, 1, 0, 0, 0, 0 );
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

		hscale_tablet[i] = add_slider2table( 0, -1, 1, table3, 1+i, 1, 200, -2 );
		gtk_adjustment_set_value( GTK_HSCALE(hscale_tablet[i])->scale.range.adjustment,
			tablet_tool_factor[i] );
		gtk_scale_set_value_pos (GTK_SCALE (hscale_tablet[i]), GTK_POS_RIGHT);
		gtk_scale_set_digits (GTK_SCALE (hscale_tablet[i]), 2);
		gtk_scale_set_draw_value (GTK_SCALE (hscale_tablet[i]), TRUE);
	}


	frame = gtk_frame_new (_("Test Area"));
	gtk_widget_show (frame);
	gtk_box_pack_start (GTK_BOX (vbox_3), frame, FALSE, FALSE, 0);
	gtk_container_set_border_width (GTK_CONTAINER (frame), 5);

	vbox_2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_2);
	gtk_container_add (GTK_CONTAINER (frame), vbox_2);
	gtk_container_set_border_width (GTK_CONTAINER (vbox_2), 5);

	drawingarea_tablet = gtk_drawing_area_new ();
	gtk_widget_show (drawingarea_tablet);
	gtk_box_pack_start (GTK_BOX (vbox_2), drawingarea_tablet, TRUE, TRUE, 0);
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



	label_tablet_pressure = gtk_label_new ("");
	gtk_widget_show (label_tablet_pressure);
	gtk_box_pack_start (GTK_BOX (vbox_2), label_tablet_pressure, FALSE, FALSE, 0);
	gtk_misc_set_alignment (GTK_MISC (label_tablet_pressure), 0, 0.5);



///	---- TAB6 - LANGUAGE

#ifdef U_NLS

	vbox_2 = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox_2);
	gtk_container_add (GTK_CONTAINER (notebook1), vbox_2);
	gtk_container_set_border_width( GTK_CONTAINER(vbox_2), 10 );

	label = gtk_label_new( _("Language") );
	gtk_widget_show (label);
	gtk_notebook_set_tab_label(GTK_NOTEBOOK (notebook1),
		gtk_notebook_get_nth_page (GTK_NOTEBOOK (notebook1), 5), label);

	add_hseparator( vbox_2, 200, 10 );
	label = gtk_label_new( _("Select preferred language translation\n\n"
				"You will need to restart mtPaint\nfor this to take full effect") );
	gtk_widget_show (label);
	gtk_box_pack_start( GTK_BOX(vbox_2), label, FALSE, FALSE, 0 );
	add_hseparator( vbox_2, 200, 10 );

	for (i = 0; i < PREF_LANGS; i++)
	{
		if (!strcmp(pref_lang_ini_code[i],
			inifile_get("languageSETTING", "system"))) break;
	}
	hbox4 = wj_radio_pack(pref_langs, PREF_LANGS, 8, i, &pref_lang, NULL);
	gtk_box_pack_start(GTK_BOX(vbox_2), hbox4, TRUE, TRUE, 0);

#endif



///	Bottom of Prefs window

	hbox4 = gtk_hbox_new (FALSE, 0);
	gtk_widget_show (hbox4);
	gtk_box_pack_start (GTK_BOX (vbox3), hbox4, FALSE, FALSE, 0);

	button1 = add_a_button(_("Cancel"), 5, hbox4, TRUE);
	gtk_signal_connect(GTK_OBJECT(button1), "clicked", GTK_SIGNAL_FUNC(delete_prefs), NULL);
	gtk_widget_add_accelerator (button1, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	button1 = add_a_button(_("Apply"), 5, hbox4, TRUE);
	gtk_signal_connect(GTK_OBJECT(button1), "clicked", GTK_SIGNAL_FUNC(prefs_apply), NULL);

	button2 = add_a_button(_("OK"), 5, hbox4, TRUE);
	gtk_signal_connect(GTK_OBJECT(button2), "clicked", GTK_SIGNAL_FUNC(prefs_ok), NULL);
	gtk_widget_add_accelerator (button2, "clicked", ag, GDK_Return, 0, (GtkAccelFlags) 0);
	gtk_widget_add_accelerator (button2, "clicked", ag, GDK_KP_Enter, 0, (GtkAccelFlags) 0);


	gtk_signal_connect_object (GTK_OBJECT (prefs_window), "delete_event",
		GTK_SIGNAL_FUNC (delete_prefs), NULL);

	gtk_window_set_transient_for( GTK_WINDOW(prefs_window), GTK_WINDOW(main_window) );
	gtk_widget_show (prefs_window);
	gtk_window_add_accel_group(GTK_WINDOW (prefs_window), ag);

	if ( tablet_working )
	{
		tablet_update_device( tablet_device->name );
	} else	tablet_update_device( "NONE" );
}


void init_tablet()				// Set up variables
{
	int i;
	char *devname, txt[32];

	gboolean use_tablet;
	GList *dlist;

#if GTK_MAJOR_VERSION == 1
	GdkDeviceInfo *device = NULL;
	gint use;
#endif
#if GTK_MAJOR_VERSION == 2
	GdkDevice *device = NULL;
	GdkAxisUse use;
#endif

	use_tablet = inifile_get_gboolean( "tablet_USE", FALSE );

	if ( use_tablet )		// User has got tablet working in past so try to initialize it
	{
		devname = inifile_get( tablet_ini3[0], "?" );	// Device name last used
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
					inifile_get_gint32( tablet_ini3[1], 0 ) );
#endif
#if GTK_MAJOR_VERSION == 2
				gdk_device_set_mode(device, inifile_get_gint32( tablet_ini3[1], 0 ) );
#endif
				for ( i=0; i<device->num_axes; i++ )
				{
					sprintf(txt, "%s%i", tablet_ini3[2], i);
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

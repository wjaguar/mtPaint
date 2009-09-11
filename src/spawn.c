/*	spawn.c
	Copyright (C) 2007 Mark Tyler

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
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#include "global.h"

#include "inifile.h"
#include "memory.h"
#include "png.h"
#include "mygtk.h"
#include "canvas.h"
#include "mainwindow.h"
#include "spawn.h"


// Process spawning code

int spawn_process(char *argv[], char *directory)
{
	pid_t child, grandchild;
	int res = -1, err, fd_flags, fds[2], status;

	child = fork();
	if ( child < 0 ) return 1;

	if (child == 0)				// Child
	{
		if (pipe(fds) == -1) _exit(1);		// Set up the pipe

		grandchild = fork();

		if (grandchild == 0)		// Grandchild
		{
			if (directory) chdir(directory);

			if (close(fds[0]) != -1)	// Close the read pipe
				/* Make the write end close-on-exec */
			if ((fd_flags = fcntl(fds[1], F_GETFD)) != -1)
				if (fcntl(fds[1], F_SETFD, fd_flags | FD_CLOEXEC) != -1)
					execvp(argv[0], &argv[0]); /* Run program */

			/* If we are here, an error occurred */
			/* Send the error code to the parent */
			err = errno;
			write(fds[1], &err, sizeof(err));
			_exit(1);
		}
		/* Close the write pipe - has to be done BEFORE read */
		close(fds[1]);

		/* Get the error code from the grandchild */
		if (grandchild > 0) res = read(fds[0], &err, sizeof(err));

		close(fds[0]);			// Close the read pipe

		_exit(res);
	}

	waitpid(child, &status, 0);		// Wait for child to die
	res = WEXITSTATUS(status);		// Find out the childs exit value

	return (res);
}

int spawn_expansion(char *cline, char *directory)
	// Replace %f with "current filename", then run via shell
{
	int res = -1, max;
	char *argv[4] = { "sh", "-c", cline, NULL }, *s1, *s2;

	s1 = strstr( cline, "%f" );	// Has user included the current filename?
	if (s1)
	{
		max = strlen(cline) + strlen(mem_filename) + 5;
		s2 = malloc(max);
		if (s2)
		{
			strncpy(s2, cline, s1-cline);		// Text before %f
			sprintf(s2 + (s1 - cline), "\"%s\"%s", mem_filename, s1+2);
			argv[2] = s2;
			res = spawn_process(argv, directory);
			free(s2);
		}
		else return -1;
	}
	else
	{
		res = spawn_process(argv, directory);
	}

		// Note that on Linux systems, returning 0 means that the shell was
		// launched OK, but gives no info on the success of the program being run by the shell.
		// To find out what happened you will need to use the output window.
		// MT 18-1-2007

	return res;
}



// Front End stuff

GtkWidget *menu_faction[FACTION_PRESETS_TOTAL+2];

static GtkWidget *faction_entry[3]		// Action, command, directory
		;
static int faction_current_row;
static char *faction_ini[4] = { "faction_action_%i", "faction_command_%i",
	"faction_directory_%i", "faction_preset_%i" };


void pressed_file_action( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	gchar *comm, *dir, txt[64];

	sprintf(txt, faction_ini[1], item);
	comm = inifile_get(txt,"");
	sprintf(txt, faction_ini[2], item);
	dir = inifile_get(txt,"");

	spawn_expansion(comm, dir);
}


static gint faction_window_destroy( GtkWidget *widget, GdkEvent *event, GtkWidget *window )
{
	gtk_widget_destroy(window);

	init_factions();

	return FALSE;
}

static gint faction_window_close( GtkWidget *widget, GtkWidget *window )
{
	return faction_window_destroy( widget, NULL, window );
}

static gint faction_execute( GtkWidget *widget, GtkWidget *window )
{
	gchar	*comm = (char *)gtk_entry_get_text( GTK_ENTRY(faction_entry[1]) ),
		*dir  = (char *)gtk_entry_get_text( GTK_ENTRY(faction_entry[2]) );

	spawn_expansion(comm, dir);

	return FALSE;
}

static void faction_entry_changed(int entry, GtkWidget *clist)
{
	char *st = (char *)gtk_entry_get_text( GTK_ENTRY(faction_entry[entry]) ),
		txt[64];

	if ( entry !=2 )
		gtk_clist_set_text( GTK_CLIST(clist), faction_current_row, entry, st );	// Update list

	sprintf(txt, faction_ini[entry], faction_current_row+1);
	inifile_set(txt, st);						// Update inifile

//printf("row=%i entry=%i text=%s ini=%s\n", faction_current_row, entry, st, txt);
}

static gint faction_action_changed(GtkWidget *widget, GtkWidget *clist)
{
	faction_entry_changed(0, clist);

	return FALSE;
}

static gint faction_command_changed(GtkWidget *widget, GtkWidget *clist)
{
	faction_entry_changed(1, clist);

	return FALSE;
}

static gint faction_directory_changed(GtkWidget *widget, GtkWidget *clist)
{
	faction_entry_changed(2, clist);

	return FALSE;
}

static void faction_block_events(GtkWidget *clist)		// Block entry update events
{
	gtk_signal_handler_block_by_func( GTK_OBJECT(faction_entry[0]),
		(GtkSignalFunc) faction_action_changed, clist );
	gtk_signal_handler_block_by_func( GTK_OBJECT(faction_entry[1]),
		(GtkSignalFunc) faction_command_changed, clist );
	gtk_signal_handler_block_by_func( GTK_OBJECT(faction_entry[2]),
		(GtkSignalFunc) faction_directory_changed, clist );
}

static void faction_unblock_events(GtkWidget *clist)		// Unblock entry update events
{
	gtk_signal_handler_unblock_by_func( GTK_OBJECT(faction_entry[0]),
		(GtkSignalFunc) faction_action_changed, clist );
	gtk_signal_handler_unblock_by_func( GTK_OBJECT(faction_entry[1]),
		(GtkSignalFunc) faction_command_changed, clist );
	gtk_signal_handler_unblock_by_func( GTK_OBJECT(faction_entry[2]),
		(GtkSignalFunc) faction_directory_changed, clist );
}


static void faction_select_row(GtkWidget *clist, gint row, gint col, GdkEvent *event, gpointer *pointer)
{
	char txt[64];

	faction_current_row = row;

	faction_block_events(clist);

	sprintf(txt, faction_ini[0], row+1);
	gtk_entry_set_text(GTK_ENTRY(faction_entry[0]), inifile_get( txt, "" ));

	sprintf(txt, faction_ini[1], row+1);
	gtk_entry_set_text(GTK_ENTRY(faction_entry[1]), inifile_get( txt, "" ));

	sprintf(txt, faction_ini[2], row+1);
	gtk_entry_set_text(GTK_ENTRY(faction_entry[2]), inifile_get( txt, "" ));

	faction_unblock_events(clist);
}

static void update_faction_menu()
{
	int i, j, items=0;
	char txt[64], *st;

	for ( i=0; i<FACTION_PRESETS_TOTAL; i++ )			// Populate menu
	{
		sprintf(txt, faction_ini[3], i+1);
		j = inifile_get_gint32( txt, -1 );

		if ( j>=0 && j<FACTION_ROWS_TOTAL )
		{
			items++;
			sprintf(txt, faction_ini[0], j);
			st = inifile_get( txt, "" );
			gtk_widget_show( menu_faction[i+1] );	// Show menu item widget
			gtk_label_set_text( GTK_LABEL( GTK_MENU_ITEM(menu_faction[i+1]
						)->item.bin.child ), st );
					// Change text of menu item
		}
		else
		{
			gtk_widget_hide( menu_faction[i+1] );	// Hide menu item widget
		}
	}

	if ( items == 0 ) gtk_widget_hide( menu_faction[0] );	// No presets so hide separator
	else gtk_widget_show( menu_faction[0] );		// Use separator
}

void init_factions()
{
	int i, j;
	char	*row_def[][3] = {
		{"View EXIF data (leafpad)", "exif %f | leafpad"},
		{"View filesystem data (xterm)", "xterm -hold -e ls -l %f"},
		{"Edit in Gimp", "gimp %f"},
		{"View in GQview", "gqview %f"},
		{"Print image", "kprinter %f"},
		{"Email image", "seamonkey -compose attachment=file://%f"},
		{"Send image to Firefox", "firefox %f"},
		{"Send image to OpenOffice", "soffice %f"},
		{"Edit Clipboards", "mtpaint ~/.clip*"},
		{"Time delayed screenshot", "sleep 10; mtpaint -s"},
		{"View image information", "xterm -hold -sb -rightbar -geometry 100x100 -e identify -verbose %f"},
		{"Create temp directory", "mkdir ~/images"},
		{"Remove temp directory", "rm -rf ~/images"},
		{"GIF to PNG conversion (in situ)", "mogrify -format png *.gif"},
		{"ICO to PNG conversion (temp directory)", "ls --file-type *.ico | xargs -I FILE convert FILE ~/images/FILE.png"},
		{"Convert image to ICO file", "mogrify -format ico %f"},
		{"Create thumbnails in temp directory", "ls --file-type * | xargs -I FILE convert FILE -thumbnail 90x90 ~/images/th_FILE.jpg"},
		{"Create thumbnails (in situ)", "ls --file-type * | xargs -I FILE convert FILE -thumbnail 90x90 th_FILE.jpg"},
		{"Peruse temp images", "mtpaint ~/images/*"},
		{"Rename *.jpeg to *.jpg", "rename .jpeg .jpg *.jpeg"},
		{"Remove spaces from filenames", "for file in *\" \"*; do mv \"$file\" `echo $file | sed -e 's/ /_/g'`; done"},
//		{"", ""},
		{NULL, NULL, NULL}
		},
		txt[64];

	for ( i=0; row_def[i][0]; i++ )		// Needed for first time usage - populate inifile list
	{
		sprintf(txt, faction_ini[0], i+1);
		inifile_get( txt, row_def[i][0] );

		sprintf(txt, faction_ini[1], i+1);
		inifile_get( txt, row_def[i][1] );

		j = i+1;
		if ( j<=FACTION_PRESETS_TOTAL )
		{
			sprintf(txt, faction_ini[3], j);
			inifile_get_gint32( txt, j );
		}
	}

	update_faction_menu();			// Prepare menus
}

void pressed_file_configure()
{
	int i, j;
	GtkWidget *vbox, *hbox, *win, *sw, *clist, *button, *entry, *omenu;
	GtkAccelGroup* ag = gtk_accel_group_new();
	gchar *clist_titles[3] = { _("Action"), _("Command"), _("Preset"),},
		*row_text[3], *omenu_txt[FACTION_PRESETS_TOTAL+1],
		txt[64], num[FACTION_PRESETS_TOTAL][5];


	win = add_a_window( GTK_WINDOW_TOPLEVEL, _("Configure File Actions"), GTK_WIN_POS_CENTER, TRUE );
	gtk_window_set_default_size( GTK_WINDOW(win), 500, 450 );

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_container_add (GTK_CONTAINER (win), vbox);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC,
			GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 5);

	clist = gtk_clist_new_with_titles( 3, clist_titles );
	gtk_clist_set_column_width( GTK_CLIST(clist), 0, 200);
	gtk_clist_set_column_width( GTK_CLIST(clist), 1, 200);
	gtk_clist_set_column_width( GTK_CLIST(clist), 2, 50 );
	gtk_signal_connect(GTK_OBJECT(clist), "select_row", GTK_SIGNAL_FUNC(faction_select_row), NULL);
	gtk_container_add(GTK_CONTAINER( sw ), clist);
	gtk_clist_column_titles_passive( GTK_CLIST(clist) );

	for ( i=0; i<FACTION_ROWS_TOTAL; i++ )
	{
		sprintf(txt, faction_ini[0], i+1);
		row_text[0] = inifile_get( txt, "" );

		sprintf(txt, faction_ini[1], i+1);
		row_text[1] = inifile_get( txt, "" );

		row_text[2] = "";		// Done next

		gtk_clist_append( GTK_CLIST(clist), row_text );
	}

	for ( i=0; i<FACTION_PRESETS_TOTAL; i++ )
	{
		sprintf(txt, faction_ini[3], i+1);
		j = inifile_get_gint32( txt, -1 );

		if ( j>0 )
		{
			sprintf(txt, "%i", j);
			gtk_clist_set_text( GTK_CLIST(clist), j-1, 2, txt ); // Enter preset into table
		}
	}


	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	add_with_frame(vbox, _("Action / Preset"), hbox, 5);
	entry = gtk_entry_new();
	faction_entry[0] = entry;
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 5);

	omenu_txt[0] = _("None");
	for ( i=1; i<=FACTION_PRESETS_TOTAL; i++ )
	{
		sprintf( num[i-1], "%i", i );
		omenu_txt[i] = num[i-1];
	}

	omenu = wj_option_menu(omenu_txt, FACTION_PRESETS_TOTAL+1, 0, NULL, NULL);
	gtk_box_pack_start(GTK_BOX(hbox), omenu, FALSE, FALSE, 4);
	gtk_signal_connect( GTK_OBJECT(faction_entry[0]), "changed",
			GTK_SIGNAL_FUNC(faction_action_changed), clist);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	add_with_frame(vbox, _("Command"), hbox, 5);
	entry = gtk_entry_new();
	faction_entry[1] = entry;
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 5);
	gtk_signal_connect( GTK_OBJECT(faction_entry[1]), "changed",
			GTK_SIGNAL_FUNC(faction_command_changed), clist);

	faction_entry[2] = mt_path_box(_("Directory"), vbox, FS_SPAWN_DIR);
	gtk_signal_connect( GTK_OBJECT(faction_entry[2]), "changed",
			GTK_SIGNAL_FUNC(faction_directory_changed), clist);

	faction_block_events(clist);

	hbox = gtk_hbox_new (FALSE, 0);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	button = add_a_button(_("Execute"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(faction_execute), (gpointer)win);

	button = add_a_button(_("Close"), 4, hbox, TRUE);
	gtk_signal_connect(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(faction_window_close), (gpointer)win);
	gtk_signal_connect(GTK_OBJECT (win), "delete_event",
		GTK_SIGNAL_FUNC(faction_window_destroy), (gpointer)win);

	gtk_widget_add_accelerator (button, "clicked",
		ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_window_set_transient_for( GTK_WINDOW(win), GTK_WINDOW(main_window) );
	gtk_widget_show_all(win);
	gtk_window_add_accel_group(GTK_WINDOW (win), ag);

	faction_current_row = 0;
	gtk_clist_select_row( GTK_CLIST(clist), 0, 0 );		// Select first item
	faction_unblock_events(clist);				// Enable updates
}


void spawn_set_new_directory(char *fname)
{
	gtk_entry_set_text(GTK_ENTRY(faction_entry[2]), fname);
}

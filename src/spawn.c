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
#include "mainwindow.h"
#include "spawn.h"

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



void pressed_file_action( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
}


static gint faction_window_destroy( GtkWidget *widget, GdkEvent *event, GtkWidget *window )
{
	gtk_widget_destroy(window);

	return FALSE;
}

static gint faction_window_close( GtkWidget *widget, GtkWidget *window )
{
	return faction_window_destroy( widget, NULL, window );
}

static gint faction_execute( GtkWidget *widget, GtkWidget *window )
{
	return FALSE;
}

static void faction_select_row(GtkWidget *clist, gint row, gint col, GdkEvent *event, gpointer *pointer)
{
	gchar *txt[1];

	gtk_clist_get_text( GTK_CLIST(clist), row, 0, txt);

//printf("row=%i col=%i txt=%s\n", row, col, txt[0]);
}

static void update_faction_menu()
{
	int i, j;
	char txt[64], *st;

	for ( i=0; i<10; i++ )			// Populate menu
	{
		sprintf(txt, "faction_preset_%i", i+1);
		j = inifile_get_gint32( txt, 0 );

		if ( j>=0 && j<FACTION_ROWS_TOTAL )
		{
			sprintf(txt, "faction_action_%i", j);
			st = inifile_get( txt, "" );
				// Show menu item widget
				// Change text of menu item
		}
		else
		{
				// Hide menu item widget
		}
	}
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
		{"Email image", "kmail %f"},
		{"Send Image to Firefox", "firefox %f"},
		{"Send Image to OpenOffice", "soffice %f"},
		{"Edit Clipboards", "mtpaint ~/.clip*"},
		{"View image information", "identify --verbose %f"},
		{"Convert TIFF files to JPEG", "mogrify -format jpeg *.tiff"},
		{NULL, NULL, NULL}
		},
		txt[64];

	for ( i=0; row_def[i][0]; i++ )		// Needed for first time usage - populate inifile list
	{
		sprintf(txt, "faction_action_%i", i+1);
		inifile_get( txt, row_def[i][0] );

		sprintf(txt, "faction_command_%i", i+1);
		inifile_get( txt, row_def[i][1] );

		j = i+1;
		if ( j<=10 )			// Only 10 presets used
		{
			sprintf(txt, "faction_preset_%i", j);
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
		*row_text[3], *omenu_txt[] = { _("None"), "1", "2", "3", "4", "5",
		"6", "7", "8", "9", "10" },
		txt[64];


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
		sprintf(txt, "faction_action_%i", i+1);
		row_text[0] = inifile_get( txt, "" );

		sprintf(txt, "faction_command_%i", i+1);
		row_text[1] = inifile_get( txt, "" );

		row_text[2] = "";		// Done next

		gtk_clist_append( GTK_CLIST(clist), row_text );
	}

	for ( i=0; i<10; i++ )
	{
		sprintf(txt, "faction_preset_%i", i+1);
		j = inifile_get_gint32( txt, -1 );

		if ( j>=0 )
		{
			sprintf(txt, "%i", j);
			gtk_clist_set_text( GTK_CLIST(clist), j-1, 2, txt ); // Enter preset into table
		}
	}


	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	add_with_frame(vbox, _("Action / Preset"), hbox, 5);
	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 5);
	omenu = wj_option_menu(omenu_txt, 11, 0, NULL, NULL);
	gtk_box_pack_start(GTK_BOX(hbox), omenu, FALSE, FALSE, 4);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	add_with_frame(vbox, _("Command"), hbox, 5);
	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 5);

	hbox = gtk_hbox_new(FALSE, 0);
	gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
	add_with_frame(vbox, _("Directory"), hbox, 5);
	entry = gtk_entry_new();
	gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 5);
	button = add_a_button(_("Browse"), 2, hbox, FALSE);
//	gtk_signal_connect(GTK_OBJECT(button), "clicked",
//		GTK_SIGNAL_FUNC(click_faction_browse), NULL);


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

	gtk_clist_select_row( GTK_CLIST(clist), 0, 0 );		// Select first item
}

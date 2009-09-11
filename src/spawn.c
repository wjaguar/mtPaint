/*	spawn.c
	Copyright (C) 2007 Mark Tyler and Dmitry Groshev

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

#include "global.h"

#include "inifile.h"
#include "memory.h"
#include "png.h"
#include "mygtk.h"
#include "canvas.h"
#include "mainwindow.h"
#include "spawn.h"


int spawn_expansion(char *cline, char *directory)
	// Replace %f with "current filename", then run via shell
{
	int res = -1, max;
	char *s1, *s2;
#ifdef WIN32
	char *argv[4] = { getenv("COMSPEC"), "/C", cline, NULL };
#else
	char *argv[4] = { "sh", "-c", cline, NULL };
#endif

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

GtkWidget *menu_faction[FACTION_PRESETS_TOTAL + 2];

static GtkWidget *faction_list, *faction_entry[3];
static int faction_current_row;
static char *faction_ini[3] = { "fact%dName", "fact%dCommand", "fact%dDir" };


void pressed_file_action( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
	char *comm, *dir, txt[64];

	sprintf(txt, faction_ini[1], item);
	comm = inifile_get(txt,"");
	sprintf(txt, faction_ini[2], item);
	dir = inifile_get(txt,"");

	spawn_expansion(comm, dir);
}

static void faction_changed(GtkEditable *entry, gpointer user_data)
{
	gtk_clist_set_text(GTK_CLIST(faction_list), faction_current_row,
		(int)user_data, gtk_entry_get_text(GTK_ENTRY(entry)));
}

/* Block/unblock entry update events */
static void faction_block_events()
{
	int i;

	for (i = 0; i < 3; i++)
	{
		gtk_signal_handler_block_by_func(GTK_OBJECT(faction_entry[i]),
			(GtkSignalFunc)faction_changed, (gpointer)i);
	}
}

static void faction_unblock_events()
{
	int i;

	for (i = 0; i < 3; i++)
	{
		gtk_signal_handler_unblock_by_func(GTK_OBJECT(faction_entry[i]),
			(GtkSignalFunc)faction_changed, (gpointer)i);
	}
}

static void faction_select_row(GtkCList *clist, gint row, gint col, GdkEvent *event, gpointer *pointer)
{
	gchar *celltext;
	int i, j;

	faction_current_row = row;
	faction_block_events();
	for (i = 0; i < 3; i++)
	{
		j = gtk_clist_get_text(GTK_CLIST(faction_list), row, i, &celltext);
		gtk_entry_set_text(GTK_ENTRY(faction_entry[i]), j ? celltext : "");
	}
	faction_unblock_events();
}

static void faction_moved(GtkCList *clist, gint src, gint dest, gpointer user_data)
{
	faction_current_row = dest;
}

static void update_faction_menu()	// Populate menu
{
	int i, items = 0;
	char txt[64], *nm, *cm;

	/* Show valid slots in menu */
	for (i = 1; i <= FACTION_PRESETS_TOTAL; i++)
	{
		gtk_widget_hide(menu_faction[i]); /* Hide by default */
		sprintf(txt, faction_ini[0], i);
		nm = inifile_get(txt, "");

// !!! Temporary - till the bug in inifile loader isn't fixed !!!
		if (!nm || !nm[0] || (nm[0] == '!')) continue;

//		if (!nm || !nm[0] || (nm[0] == '#')) continue;
		sprintf(txt, faction_ini[1], i);
		cm = inifile_get(txt, "");

// !!! Temporary - till the bug in inifile loader isn't fixed !!!
		if (!cm || !cm[0] || (cm[0] == '!')) continue;

//		if (!cm || !cm[0] || (cm[0] == '#')) continue;
		gtk_label_set_text(GTK_LABEL(GTK_MENU_ITEM(menu_faction[i])->
			item.bin.child), nm);
		gtk_widget_show(menu_faction[i]);
		items++;
	}

	/* Hide separator if no valid slots */
	(items ? gtk_widget_show : gtk_widget_hide)(menu_faction[0]);
}	

void init_factions()
{
#ifndef WIN32
	int i, j;
	static char *row_def[][3] = {
		{"View EXIF data (leafpad)", "exif %f | leafpad"},
		{"View filesystem data (xterm)", "xterm -hold -e ls -l %f"},
		{"Edit in Gimp", "gimp %f"},
		{"View in GQview", "gqview %f"},
		{"Print image", "kprinter %f"},
		{"Email image", "seamonkey -compose attachment=file://%f"},
		{"Send image to Firefox", "firefox %f"},
		{"Send image to OpenOffice", "soffice %f"},
		{"Edit Clipboards", "mtpaint ~/.clip*"},
		{"Time delayed screenshot", "sleep 10; mtpaint -s &"},
		{"View image information", "xterm -hold -sb -rightbar -geometry 100x100 -e identify -verbose %f"},
		{"!Create temp directory", "mkdir ~/images"},
		{"!Remove temp directory", "rm -rf ~/images"},
		{"!GIF to PNG conversion (in situ)", "mogrify -format png *.gif"},
		{"!ICO to PNG conversion (temp directory)", "ls --file-type *.ico | xargs -I FILE convert FILE ~/images/FILE.png"},
		{"Convert image to ICO file", "mogrify -format ico %f"},
		{"Create thumbnails in temp directory", "ls --file-type * | xargs -I FILE convert FILE -thumbnail 120x120 -sharpen 1 -quality 95 ~/images/th_FILE.jpg"},
		{"Create thumbnails (in situ)", "ls --file-type * | xargs -I FILE convert FILE -thumbnail 120x120 -sharpen 1 -quality 95 th_FILE.jpg"},
		{"Peruse temp images", "mtpaint ~/images/*"},
		{"Rename *.jpeg to *.jpg", "rename .jpeg .jpg *.jpeg"},
		{"Remove spaces from filenames", "for file in *\" \"*; do mv \"$file\" `echo $file | sed -e 's/ /_/g'`; done"},
		{"Remove extra .jpg. from filename", "rename .jpg. . *.jpg.jpg"},
//		{"", ""},
		{NULL, NULL, NULL}
		},
		txt[64];

	for (i = 0; row_def[i][0]; i++)		// Needed for first time usage - populate inifile list
	{
		for (j = 0; j < 3; j++)
		{
			sprintf(txt, faction_ini[j], i + 1);
			inifile_get(txt, row_def[i][j]);
		}
	}
#endif

	update_faction_menu();			// Prepare menu
}

static void faction_ok(GtkWidget *widget, gpointer user_data)
{
	char txt[64];
	gchar *celltext;
	int i, j, k;

	for (i = 0; i < FACTION_ROWS_TOTAL; i++)
	{
		for (j = 0; j < 3; j++)
		{
			k = gtk_clist_get_text(GTK_CLIST(faction_list), i, j,
				&celltext);
			sprintf(txt, faction_ini[j], i + 1);
			inifile_set(txt, k ? celltext : "");
		}
	}
	update_faction_menu();
	gtk_widget_destroy(widget);
}

static void faction_execute(GtkWidget *widget, gpointer user_data)
{
	spawn_expansion((char *)gtk_entry_get_text(GTK_ENTRY(faction_entry[1])),
		(char *)gtk_entry_get_text(GTK_ENTRY(faction_entry[2])));
}

void pressed_file_configure()
{
	gchar *clist_titles[3] = { _("Action"), _("Command"), "" };
	GtkWidget *vbox, *hbox, *win, *sw, *clist, *entry;
	gchar *row_text[3];
	char txt[64];
	int i, j;


	win = add_a_window( GTK_WINDOW_TOPLEVEL, _("Configure File Actions"), GTK_WIN_POS_CENTER, TRUE );
	gtk_window_set_default_size( GTK_WINDOW(win), 500, 400 );

	vbox = gtk_vbox_new(FALSE, 0);
	gtk_container_add(GTK_CONTAINER(win), vbox);
	gtk_container_set_border_width(GTK_CONTAINER(vbox), 5);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 5);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC,
			GTK_POLICY_AUTOMATIC);

	faction_list = clist = gtk_clist_new_with_titles(3, clist_titles);
	gtk_container_add(GTK_CONTAINER(sw), clist);
	/* Store directories in a hidden third column */
	gtk_clist_set_column_visibility(GTK_CLIST(clist), 2, FALSE);
//	gtk_clist_set_column_width(GTK_CLIST(clist), 0, 200);
//	gtk_clist_set_column_width(GTK_CLIST(clist), 1, 200);
	gtk_clist_column_titles_passive(GTK_CLIST(clist));
	gtk_clist_set_selection_mode(GTK_CLIST(clist), GTK_SELECTION_BROWSE);
	clist_enable_drag(clist);

	for (i = 1; i <= FACTION_ROWS_TOTAL; i++)
	{
		for (j = 0; j < 3; j++)
		{
			sprintf(txt, faction_ini[j], i);
			row_text[j] = inifile_get(txt, "");
		}
		gtk_clist_append(GTK_CLIST(clist), row_text);
	}

	gtk_signal_connect(GTK_OBJECT(clist), "select_row",
		GTK_SIGNAL_FUNC(faction_select_row), NULL);
	gtk_signal_connect(GTK_OBJECT(clist), "row_move",
		GTK_SIGNAL_FUNC(faction_moved), NULL);

	/* Entries */

	for (j = 0; j < 2; j++)
	{
		hbox = gtk_hbox_new(FALSE, 0);
		gtk_container_set_border_width(GTK_CONTAINER(hbox), 5);
		add_with_frame(vbox, !j ? _("Action") : _("Command"), hbox, 5);
		faction_entry[j] = entry = gtk_entry_new();
		gtk_box_pack_start(GTK_BOX(hbox), entry, TRUE, TRUE, 5);
		gtk_signal_connect(GTK_OBJECT(entry), "changed",
			GTK_SIGNAL_FUNC(faction_changed), (gpointer)j);
	}

	faction_entry[2] = mt_path_box(_("Directory"), vbox,
		_("Select Directory"), FS_SELECT_DIR);
	gtk_signal_connect(GTK_OBJECT(faction_entry[2]), "changed",
		GTK_SIGNAL_FUNC(faction_changed), (gpointer)2);

	/* Cancel / Execute / OK */

	hbox = OK_box(0, win, _("OK"), GTK_SIGNAL_FUNC(faction_ok),
		_("Cancel"), GTK_SIGNAL_FUNC(gtk_widget_destroy));
	OK_box_add(hbox, _("Execute"), GTK_SIGNAL_FUNC(faction_execute), 1);
	gtk_box_pack_start(GTK_BOX(vbox), hbox, FALSE, FALSE, 0);

	gtk_window_set_transient_for(GTK_WINDOW(win), GTK_WINDOW(main_window));
	gtk_widget_show_all(win);
	gtk_clist_select_row(GTK_CLIST(clist), 0, 0);	// Select first item
}


// Process spawning code

#ifdef WIN32

/* With Glib's g_spawn*() smart-ass tricks making it utterly unusable in
 * Windows, the only way is to reimplement the functionality - WJ */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

int spawn_process(char *argv[], char *directory)
{
	PROCESS_INFORMATION pi;
	STARTUPINFO st;
	gchar *cmdline, *c;
	int res;

	memset(&pi, 0, sizeof(pi));
	memset(&st, 0, sizeof(st));
	st.cb = sizeof(st);
	cmdline = g_strjoinv(" ", argv);
	if (directory && !directory[0]) directory = NULL;

	c = g_locale_from_utf8(cmdline, -1, NULL, NULL, NULL);
	if (c)
	{
		g_free(cmdline);
		cmdline = c;
		c = NULL;
	}
	if (directory)
	{
		c = g_locale_from_utf8(directory, -1, NULL, NULL, NULL);
		if (c) directory = c;
	}

	res = CreateProcessA(NULL, cmdline, NULL, NULL, FALSE,
		/* CREATE_NEW_CONSOLE | */
		CREATE_DEFAULT_ERROR_MODE | NORMAL_PRIORITY_CLASS,
		NULL, directory, &st, &pi);
	g_free(cmdline);
	g_free(c);
	if (!res) return (1);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return (0);
}

#else

#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

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

#endif

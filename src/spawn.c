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
#include "memory.h"
#include "mygtk.h"


static void spawn_add_text(GtkTextBuffer *buffer, char *txt)
{
	GtkTextIter iter;

	gtk_text_buffer_get_end_iter(buffer, &iter);
	gtk_text_buffer_insert(buffer, &iter, txt, -1);
}


static void spawn_output_init(char *command)
{
	GtkWidget *spawn_window, *vbox, *button, *view, *sw;
	GtkTextBuffer *buffer;
	PangoFontDescription *font_desc;
	GtkAccelGroup* ag = gtk_accel_group_new();


	spawn_window = add_a_window( GTK_WINDOW_TOPLEVEL, _("Output"), GTK_WIN_POS_CENTER, TRUE );
	gtk_window_set_default_size( GTK_WINDOW(spawn_window), 400, 400 );

	vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (vbox);
	gtk_container_add (GTK_CONTAINER (spawn_window), vbox);

	sw = gtk_scrolled_window_new (NULL, NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (sw), GTK_POLICY_AUTOMATIC,
			GTK_POLICY_AUTOMATIC);
	gtk_box_pack_start(GTK_BOX(vbox), sw, TRUE, TRUE, 5);

	view = gtk_text_view_new();
	gtk_text_view_set_editable(GTK_TEXT_VIEW (view), FALSE);
	buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW (view));
	spawn_add_text(buffer, command);
	spawn_add_text(buffer, "\n\nwotch!!\n\nbye\n");
	font_desc = pango_font_description_from_string ("Monospace 10");
	gtk_widget_modify_font (view, font_desc);
	pango_font_description_free (font_desc);
	gtk_container_add(GTK_CONTAINER (sw), view);

	button = add_a_button(_("Close"), 4, vbox, FALSE);
	gtk_signal_connect_object(GTK_OBJECT(button), "clicked",
		GTK_SIGNAL_FUNC(gtk_widget_destroy), spawn_window);
	gtk_signal_connect_object (GTK_OBJECT (spawn_window), "delete_event",
		GTK_SIGNAL_FUNC(gtk_widget_destroy), spawn_window);
	gtk_widget_add_accelerator (button, "clicked", ag, GDK_Escape, 0, (GtkAccelFlags) 0);

	gtk_widget_show_all (spawn_window);
	gtk_window_add_accel_group(GTK_WINDOW (spawn_window), ag);
}


static gboolean deliver_signal(GIOChannel *source, GIOCondition condition, gpointer data)
{
	char buf[1024];
	ssize_t bytes_read;

	while (1)
	{
		bytes_read = read(g_io_channel_unix_get_fd(source), buf, 1000);
		if (bytes_read <= 0 || bytes_read>1000) return TRUE;
		buf[bytes_read] = 0;
		printf("Received data:\n%s\n", buf);
	}

	return (TRUE);				// keep the event source
}

static gboolean close_pipes( GIOChannel *source, GIOCondition condition, gpointer data )
{
//	close ( sa->seti_output );
	g_io_channel_close ( source );
	g_io_channel_unref ( source );
//	g_source_remove ( sa->watch_in );
//	g_source_remove ( sa->watch_hup );
//	close_channel(source,sa);
	return TRUE;
}

static int spawn_process_output(char *argv[], char *directory)
		// Spawn process and collect output
{
	int pid, in_pipe[2];//, out_pipe[2];
	GIOChannel *input_channel;//, *output_channel;

printf("spawn_process_output\n");

	if ( pipe(in_pipe) < 0 ) return -1;
//	if ( pipe(out_pipe) < 0 ) return -1;

	fcntl ( in_pipe[0], F_SETFL, O_NONBLOCK );

	pid = fork();
	if ( pid < 0 ) return -1;

	if (pid == 0)					// Child
	{
		close(in_pipe[0]);
//		close(out_pipe[1]);
		dup2(in_pipe[1], STDOUT_FILENO);
//		dup2(out_pipe[0], STDIN_FILENO);

		if (directory) chdir(directory);	// Change directory
		execvp(argv[0], &argv[0]);		// Run program
		exit(-1);				// Error running program if we get here
	}

	spawn_output_init( argv[2] );

	input_channel = g_io_channel_unix_new(in_pipe[0]);
//	output_channel = g_io_channel_unix_new(out_pipe[1]);

	g_io_add_watch(input_channel, G_IO_IN, (GIOFunc)deliver_signal, NULL);
	g_io_add_watch(input_channel, G_IO_HUP, (GIOFunc)close_pipes, NULL );

//	g_io_add_watch (in_pipe[0], G_IO_IN | G_IO_HUP, deliver_signal, NULL);

printf("leaving setup\n");

	return (0);
}

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

int spawn_expansion(char *cline, char *directory, gboolean output)
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
			if (output) res = spawn_process_output(argv, directory);
			else res = spawn_process(argv, directory);
			free(s2);
		}
		else return -1;
	}
	else
	{
		if (output) res = spawn_process_output(argv, directory);
		else res = spawn_process(argv, directory);
	}

		// Note that on Linux systems, returning 0 means that the shell was
		// launched OK, but gives no info on the success of the program being run.
		// To find out what happened you will need to use the output window.
		// MT 18-1-2007

	return res;
}



void pressed_file_action( GtkMenuItem *menu_item, gpointer user_data, gint item )
{
}


void pressed_file_configure()
{
}
